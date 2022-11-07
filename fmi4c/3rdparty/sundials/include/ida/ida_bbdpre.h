/*
 * -----------------------------------------------------------------
 * $Revision: 4378 $
 * $Date: 2015-02-19 10:55:14 -0800 (Thu, 19 Feb 2015) $
 * ----------------------------------------------------------------- 
 * Programmer(s): Alan C. Hindmarsh, Radu Serban and
 *                Aaron Collier @ LLNL
 * -----------------------------------------------------------------
 * LLNS Copyright Start
 * Copyright (c) 2014, Lawrence Livermore National Security
 * This work was performed under the auspices of the U.S. Department 
 * of Energy by Lawrence Livermore National Laboratory in part under 
 * Contract W-7405-Eng-48 and in part under Contract DE-AC52-07NA27344.
 * Produced at the Lawrence Livermore National Laboratory.
 * All rights reserved.
 * For details, see the LICENSE file.
 * LLNS Copyright End
 * -----------------------------------------------------------------
 * This is the header file for the IDABBDPRE module, for a
 * band-block-diagonal preconditioner, i.e. a block-diagonal
 * matrix with banded blocks, for use with IDA and
 * IDASPGMR/IDASPBCG/IDASPTFQMR.
 *
 * Summary:
 *
 * These routines provide a preconditioner matrix that is
 * block-diagonal with banded blocks. The blocking corresponds
 * to the distribution of the dependent variable vector y among
 * the processors. Each preconditioner block is generated from
 * the Jacobian of the local part (on the current processor) of a
 * given function G(t,y,y') approximating F(t,y,y'). The blocks
 * are generated by a difference quotient scheme on each processor
 * independently. This scheme utilizes an assumed banded structure
 * with given half-bandwidths, mudq and mldq. However, the banded
 * Jacobian block kept by the scheme has half-bandwiths mukeep and
 * mlkeep, which may be smaller.
 *
 * The user's calling program should have the following form:
 *
 *   #include <ida/ida_bbdpre.h>
 *   #include <nvector_parallel.h>
 *   ...
 *   y0  = FMIC_N_VNew_Parallel(...);
 *   yp0 = FMIC_N_VNew_Parallel(...);
 *   ...
 *   ida_mem = IDACreate(...);
 *   ier = IDAInit(...);
 *   ...
 *   flag = IDASptfqmr(ida_mem, maxl);
 *       -or-
 *   flag = IDASpgmr(ida_mem, maxl);
 *       -or-
 *   flag = IDASpbcg(ida_mem, maxl);
 *   ...
 *   flag = IDABBDPrecInit(ida_mem, Nlocal, mudq, mldq,
 *                         mukeep, mlkeep, dq_rel_yy, Gres, Gcomm);
 *   ...
 *   ier = IDASolve(...);
 *   ...
 *   IDAFree(&ida_mem);
 *
 *   FMIC_N_VDestroy(y0);
 *   FMIC_N_VDestroy(yp0);
 *
 * The user-supplied routines required are:
 *
 *   res  is the function F(t,y,y') defining the DAE system to
 *   be solved:  F(t,y,y') = 0.
 *
 *   Gres  is the function defining a local approximation
 *   G(t,y,y') to F, for the purposes of the preconditioner.
 *
 *   Gcomm  is the function performing communication needed
 *   for Glocal.
 *
 * Notes:
 *
 * 1) This header file is included by the user for the definition
 *    of the IBBDPrecData type and for needed function prototypes.
 *
 * 2) The IDABBDPrecInit call includes half-bandwidths mudq and
 *    mldq to be used in the approximate Jacobian. They need
 *    not be the true half-bandwidths of the Jacobian of the
 *    local block of G, when smaller values may provide a greater
 *    efficiency. Similarly, mukeep and mlkeep, specifying the
 *    bandwidth kept for the approximate Jacobian, need not be
 *    the true half-bandwidths. Also, mukeep, mlkeep, mudq, and
 *    mldq need not be the same on every processor.
 *
 * 3) The actual name of the user's res function is passed to
 *    IDAInit, and the names of the user's Gres and Gcomm
 *    functions are passed to IDABBDPrecInit.        
 *
 * 4) The pointer to the user-defined data block user_data, which
 *    is set through IDASetUserData is also available to the user
 *    in glocal and gcomm.
 *
 * 5) Optional outputs specific to this module are available by
 *    way of routines listed below. These include work space sizes
 *    and the cumulative number of glocal calls. The costs
 *    associated with this module also include nsetups banded LU
 *    factorizations, nsetups gcomm calls, and nps banded
 *    backsolve calls, where nsetups and nps are integrator
 *    optional outputs.
 * -----------------------------------------------------------------
 */

#ifndef _IDABBDPRE_H
#define _IDABBDPRE_H

#include <sundials/sundials_nvector.h>

#ifdef __cplusplus     /* wrapper to enable C++ usage */
extern "C" {
#endif

/*
 * -----------------------------------------------------------------
 * Type : IDABBDLocalFn
 * -----------------------------------------------------------------
 * The user must supply a function G(t,y,y') which approximates
 * the function F for the system F(t,y,y') = 0, and which is
 * computed locally (without interprocess communication).
 * (The case where G is mathematically identical to F is allowed.)
 * The implementation of this function must have type IDABBDLocalFn.
 *
 * This function takes as input the independent variable value tt,
 * the current solution vector yy, the current solution
 * derivative vector yp, and a pointer to the user-defined data
 * block user_data. It is to compute the local part of G(t,y,y')
 * and store it in the vector gval. (Providing memory for yy and
 * gval is handled within this preconditioner module.) It is
 * expected that this routine will save communicated data in work
 * space defined by the user, and made available to the
 * preconditioner function for the problem. The user_data
 * parameter is the same as that passed by the user to the
 * IDASetUserdata routine.
 *
 * An IDABBDLocalFn Gres is to return an int, defined in the same
 * way as for the residual function: 0 (success), +1 or -1 (fail).
 * -----------------------------------------------------------------
 */

typedef int (*IDABBDLocalFn)(long int Nlocal, realtype tt,
			     FMIC_N_Vector yy, FMIC_N_Vector yp, FMIC_N_Vector gval,
			     void *user_data);

/*
 * -----------------------------------------------------------------
 * Type : IDABBDCommFn
 * -----------------------------------------------------------------
 * The user may supply a function of type IDABBDCommFn which
 * performs all interprocess communication necessary to
 * evaluate the approximate system function described above.
 *
 * This function takes as input the solution vectors yy and yp,
 * and a pointer to the user-defined data block user_data. The
 * user_data parameter is the same as that passed by the user to
 * the IDASetUserData routine.
 *
 * The IDABBDCommFn Gcomm is expected to save communicated data in
 * space defined with the structure *user_data.
 *
 * A IDABBDCommFn Gcomm returns an int value equal to 0 (success),
 * > 0 (recoverable error), or < 0 (unrecoverable error).
 *
 * Each call to the IDABBDCommFn is preceded by a call to the system
 * function res with the same vectors yy and yp. Thus the
 * IDABBDCommFn gcomm can omit any communications done by res if
 * relevant to the evaluation of the local function glocal.
 * A NULL communication function can be passed to IDABBDPrecInit
 * if all necessary communication was done by res.
 * -----------------------------------------------------------------
 */

typedef int (*IDABBDCommFn)(long int Nlocal, realtype tt,
			    FMIC_N_Vector yy, FMIC_N_Vector yp,
			    void *user_data);

/*
 * -----------------------------------------------------------------
 * Function : IDABBDPrecInit
 * -----------------------------------------------------------------
 * IDABBDPrecInit allocates and initializes the BBD preconditioner.
 *
 * The parameters of IDABBDPrecInit are as follows:
 *
 * ida_mem  is a pointer to the memory blockreturned by IDACreate.
 *
 * Nlocal  is the length of the local block of the vectors yy etc.
 *         on the current processor.
 *
 * mudq, mldq  are the upper and lower half-bandwidths to be used
 *             in the computation of the local Jacobian blocks.
 *
 * mukeep, mlkeep are the upper and lower half-bandwidths to be
 *                used in saving the Jacobian elements in the local
 *                block of the preconditioner matrix PP.
 *
 * dq_rel_yy is an optional input. It is the relative increment
 *           to be used in the difference quotient routine for
 *           Jacobian calculation in the preconditioner. The
 *           default is sqrt(unit roundoff), and specified by
 *           passing dq_rel_yy = 0.
 *
 * Gres    is the name of the user-supplied function G(t,y,y')
 *         that approximates F and whose local Jacobian blocks
 *         are to form the preconditioner.
 *
 * Gcomm   is the name of the user-defined function that performs
 *         necessary interprocess communication for the
 *         execution of glocal.
 *
 * The return value of IDABBDPrecInit is one of:
 *   IDASPILS_SUCCESS if no errors occurred
 *   IDASPILS_MEM_NULL if the integrator memory is NULL
 *   IDASPILS_LMEM_NULL if the linear solver memory is NULL
 *   IDASPILS_ILL_INPUT if an input has an illegal value
 *   IDASPILS_MEM_FAIL if a memory allocation request failed
 * -----------------------------------------------------------------
 */

SUNDIALS_EXPORT int IDABBDPrecInit(void *ida_mem, long int Nlocal,
                                   long int mudq, long int mldq,
                                   long int mukeep, long int mlkeep,
                                   realtype dq_rel_yy,
                                   IDABBDLocalFn Gres, IDABBDCommFn Gcomm);

/*
 * -----------------------------------------------------------------
 * Function : IDABBDPrecReInit
 * -----------------------------------------------------------------
 * IDABBDPrecReInit reinitializes the IDABBDPRE module when
 * solving a sequence of problems of the same size with
 * IDASPGMR/IDABBDPRE, IDASPBCG/IDABBDPRE, or IDASPTFQMR/IDABBDPRE
 * provided there is no change in Nlocal, mukeep, or mlkeep. After
 * solving one problem, and after calling IDAReInit to reinitialize
 * the integrator for a subsequent problem, call IDABBDPrecReInit.
 *
 * All arguments have the same names and meanings as those
 * of IDABBDPrecInit.
 *
 * The return value of IDABBDPrecReInit is one of:
 *   IDASPILS_SUCCESS if no errors occurred
 *   IDASPILS_MEM_NULL if the integrator memory is NULL
 *   IDASPILS_LMEM_NULL if the linear solver memory is NULL
 *   IDASPILS_PMEM_NULL if the preconditioner memory is NULL
 * -----------------------------------------------------------------
 */

SUNDIALS_EXPORT int IDABBDPrecReInit(void *ida_mem,
				     long int mudq, long int mldq,
				     realtype dq_rel_yy);

/*
 * -----------------------------------------------------------------
 * Optional outputs for IDABBDPRE
 * -----------------------------------------------------------------
 * IDABBDPrecGetWorkSpace returns the real and integer work space
 *                        for IDABBDPRE.
 * IDABBDPrecGetNumGfnEvals returns the number of calls to the
 *                          user Gres function.
 * 
 * The return value of IDABBDPrecGet* is one of:
 *   IDASPILS_SUCCESS if no errors occurred
 *   IDASPILS_MEM_NULL if the integrator memory is NULL
 *   IDASPILS_LMEM_NULL if the linear solver memory is NULL
 *   IDASPILS_PMEM_NULL if the preconditioner memory is NULL
 * -----------------------------------------------------------------
 */

SUNDIALS_EXPORT int IDABBDPrecGetWorkSpace(void *ida_mem, 
                                           long int *lenrwBBDP, long int *leniwBBDP);
SUNDIALS_EXPORT int IDABBDPrecGetNumGfnEvals(void *ida_mem, long int *ngevalsBBDP);

#ifdef __cplusplus
}
#endif

#endif

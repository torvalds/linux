/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Userland implementation of gettimeofday() for processes
 * for use in the vDSO
 *
 * Copyright (C) 2004 Benjamin Herrenschmuidt (benh@kernel.crashing.org,
 *                    IBM Corp.
 */
#include <asm/processor.h>
#include <asm/ppc_asm.h>
#include <asm/vdso.h>
#include <asm/vdso_datapage.h>
#include <asm/asm-offsets.h>
#include <asm/unistd.h>

/*
 * The macro sets two stack frames, one for the caller and one for the callee
 * because there are no requirement for the caller to set a stack frame when
 * calling VDSO so it may have omitted to set one, especially on PPC64
 */

.macro cvdso_call funct call_time=0
  .cfi_startproc
	PPC_STLU	r1, -PPC_MIN_STKFRM(r1)
  .cfi_adjust_cfa_offset PPC_MIN_STKFRM
	mflr		r0
	PPC_STLU	r1, -PPC_MIN_STKFRM(r1)
  .cfi_adjust_cfa_offset PPC_MIN_STKFRM
	PPC_STL		r0, PPC_MIN_STKFRM + PPC_LR_STKOFF(r1)
  .cfi_rel_offset lr, PPC_MIN_STKFRM + PPC_LR_STKOFF
#ifdef __powerpc64__
	PPC_STL		r2, PPC_MIN_STKFRM + STK_GOT(r1)
  .cfi_rel_offset r2, PPC_MIN_STKFRM + STK_GOT
#endif
	get_datapage	r5
	.ifeq	\call_time
	addi		r5, r5, VDSO_DATA_OFFSET
	.else
	addi		r4, r5, VDSO_DATA_OFFSET
	.endif
	bl		CFUNC(DOTSYM(\funct))
	PPC_LL		r0, PPC_MIN_STKFRM + PPC_LR_STKOFF(r1)
#ifdef __powerpc64__
	PPC_LL		r2, PPC_MIN_STKFRM + STK_GOT(r1)
  .cfi_restore r2
#endif
	.ifeq	\call_time
	cmpwi		r3, 0
	.endif
	mtlr		r0
	addi		r1, r1, 2 * PPC_MIN_STKFRM
  .cfi_restore lr
  .cfi_def_cfa_offset 0
	crclr		so
	.ifeq	\call_time
	beqlr+
	crset		so
	neg		r3, r3
	.endif
	blr
  .cfi_endproc
.endm

	.text
/*
 * Exact prototype of gettimeofday
 *
 * int __kernel_gettimeofday(struct timeval *tv, struct timezone *tz);
 *
 */
V_FUNCTION_BEGIN(__kernel_gettimeofday)
	cvdso_call __c_kernel_gettimeofday
V_FUNCTION_END(__kernel_gettimeofday)

/*
 * Exact prototype of clock_gettime()
 *
 * int __kernel_clock_gettime(clockid_t clock_id, struct timespec *tp);
 *
 */
V_FUNCTION_BEGIN(__kernel_clock_gettime)
	cvdso_call __c_kernel_clock_gettime
V_FUNCTION_END(__kernel_clock_gettime)

/*
 * Exact prototype of clock_gettime64()
 *
 * int __kernel_clock_gettime64(clockid_t clock_id, struct __timespec64 *ts);
 *
 */
#ifndef __powerpc64__
V_FUNCTION_BEGIN(__kernel_clock_gettime64)
	cvdso_call __c_kernel_clock_gettime64
V_FUNCTION_END(__kernel_clock_gettime64)
#endif

/*
 * Exact prototype of clock_getres()
 *
 * int __kernel_clock_getres(clockid_t clock_id, struct timespec *res);
 *
 */
V_FUNCTION_BEGIN(__kernel_clock_getres)
	cvdso_call __c_kernel_clock_getres
V_FUNCTION_END(__kernel_clock_getres)


/*
 * Exact prototype of time()
 *
 * time_t time(time *t);
 *
 */
V_FUNCTION_BEGIN(__kernel_time)
	cvdso_call __c_kernel_time call_time=1
V_FUNCTION_END(__kernel_time)

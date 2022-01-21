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
#include <asm/vdso/gettimeofday.h>

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
	cvdso_call_time __c_kernel_time
V_FUNCTION_END(__kernel_time)

/* Routines for restoring integer registers, called by the compiler.  */
/* Called with r11 pointing to the stack header word of the caller of the */
/* function, just beyond the end of the integer restore area.  */
#ifndef __powerpc64__
_GLOBAL(_restgpr_31_x)
_GLOBAL(_rest32gpr_31_x)
	lwz	r0,4(r11)
	lwz	r31,-4(r11)
	mtlr	r0
	mr	r1,r11
	blr
#endif

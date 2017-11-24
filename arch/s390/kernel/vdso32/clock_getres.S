/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Userland implementation of clock_getres() for 32 bits processes in a
 * s390 kernel for use in the vDSO
 *
 *  Copyright IBM Corp. 2008
 *  Author(s): Martin Schwidefsky (schwidefsky@de.ibm.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License (version 2 only)
 * as published by the Free Software Foundation.
 */
#include <asm/vdso.h>
#include <asm/asm-offsets.h>
#include <asm/unistd.h>

	.text
	.align 4
	.globl __kernel_clock_getres
	.type  __kernel_clock_getres,@function
__kernel_clock_getres:
	.cfi_startproc
	basr	%r1,0
	la	%r1,4f-.(%r1)
	chi	%r2,__CLOCK_REALTIME
	je	0f
	chi	%r2,__CLOCK_MONOTONIC
	je	0f
	la	%r1,5f-4f(%r1)
	chi	%r2,__CLOCK_REALTIME_COARSE
	je	0f
	chi	%r2,__CLOCK_MONOTONIC_COARSE
	jne	3f
0:	ltr	%r3,%r3
	jz	2f				/* res == NULL */
1:	l	%r0,0(%r1)
	xc	0(4,%r3),0(%r3)			/* set tp->tv_sec to zero */
	st	%r0,4(%r3)			/* store tp->tv_usec */
2:	lhi	%r2,0
	br	%r14
3:	lhi	%r1,__NR_clock_getres		/* fallback to svc */
	svc	0
	br	%r14
4:	.long	__CLOCK_REALTIME_RES
5:	.long	__CLOCK_COARSE_RES
	.cfi_endproc
	.size	__kernel_clock_getres,.-__kernel_clock_getres

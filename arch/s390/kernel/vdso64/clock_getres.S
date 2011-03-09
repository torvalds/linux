/*
 * Userland implementation of clock_getres() for 64 bits processes in a
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
	cghi	%r2,__CLOCK_REALTIME
	je	0f
	cghi	%r2,__CLOCK_MONOTONIC
	je	0f
	cghi	%r2,-2		/* CLOCK_THREAD_CPUTIME_ID for this thread */
	jne	2f
	larl	%r5,_vdso_data
	icm	%r0,15,__LC_ECTG_OK(%r5)
	jz	2f
0:	ltgr	%r3,%r3
	jz	1f				/* res == NULL */
	larl	%r1,3f
	lg	%r0,0(%r1)
	xc	0(8,%r3),0(%r3)			/* set tp->tv_sec to zero */
	stg	%r0,8(%r3)			/* store tp->tv_usec */
1:	lghi	%r2,0
	br	%r14
2:	lghi	%r1,__NR_clock_getres		/* fallback to svc */
	svc	0
	br	%r14
3:	.quad	__CLOCK_REALTIME_RES
	.cfi_endproc
	.size	__kernel_clock_getres,.-__kernel_clock_getres

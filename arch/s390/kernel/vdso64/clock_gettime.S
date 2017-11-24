/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Userland implementation of clock_gettime() for 64 bits processes in a
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
	.globl __kernel_clock_gettime
	.type  __kernel_clock_gettime,@function
__kernel_clock_gettime:
	.cfi_startproc
	aghi	%r15,-16
	larl	%r5,_vdso_data
	cghi	%r2,__CLOCK_REALTIME_COARSE
	je	4f
	cghi	%r2,__CLOCK_REALTIME
	je	5f
	cghi	%r2,-3		/* Per-thread CPUCLOCK with PID=0, VIRT=1 */
	je	9f
	cghi	%r2,__CLOCK_MONOTONIC_COARSE
	je	3f
	cghi	%r2,__CLOCK_MONOTONIC
	jne	12f

	/* CLOCK_MONOTONIC */
0:	lg	%r4,__VDSO_UPD_COUNT(%r5)	/* load update counter */
	tmll	%r4,0x0001			/* pending update ? loop */
	jnz	0b
	stcke	0(%r15)				/* Store TOD clock */
	lgf	%r2,__VDSO_TK_SHIFT(%r5)	/* Timekeeper shift */
	lg	%r0,__VDSO_WTOM_SEC(%r5)
	lg	%r1,1(%r15)
	sg	%r1,__VDSO_XTIME_STAMP(%r5)	/* TOD - cycle_last */
	msgf	%r1,__VDSO_TK_MULT(%r5)		/*  * tk->mult */
	alg	%r1,__VDSO_WTOM_NSEC(%r5)
	srlg	%r1,%r1,0(%r2)			/*  >> tk->shift */
	clg	%r4,__VDSO_UPD_COUNT(%r5)	/* check update counter */
	jne	0b
	larl	%r5,13f
1:	clg	%r1,0(%r5)
	jl	2f
	slg	%r1,0(%r5)
	aghi	%r0,1
	j	1b
2:	stg	%r0,0(%r3)			/* store tp->tv_sec */
	stg	%r1,8(%r3)			/* store tp->tv_nsec */
	lghi	%r2,0
	aghi	%r15,16
	br	%r14

	/* CLOCK_MONOTONIC_COARSE */
3:	lg	%r4,__VDSO_UPD_COUNT(%r5)	/* load update counter */
	tmll	%r4,0x0001			/* pending update ? loop */
	jnz	3b
	lg	%r0,__VDSO_WTOM_CRS_SEC(%r5)
	lg	%r1,__VDSO_WTOM_CRS_NSEC(%r5)
	clg	%r4,__VDSO_UPD_COUNT(%r5)	/* check update counter */
	jne	3b
	j	2b

	/* CLOCK_REALTIME_COARSE */
4:	lg	%r4,__VDSO_UPD_COUNT(%r5)	/* load update counter */
	tmll	%r4,0x0001			/* pending update ? loop */
	jnz	4b
	lg	%r0,__VDSO_XTIME_CRS_SEC(%r5)
	lg	%r1,__VDSO_XTIME_CRS_NSEC(%r5)
	clg	%r4,__VDSO_UPD_COUNT(%r5)	/* check update counter */
	jne	4b
	j	7f

	/* CLOCK_REALTIME */
5:	lg	%r4,__VDSO_UPD_COUNT(%r5)	/* load update counter */
	tmll	%r4,0x0001			/* pending update ? loop */
	jnz	5b
	stcke	0(%r15)				/* Store TOD clock */
	lg	%r1,1(%r15)
	lg	%r0,__VDSO_TS_END(%r5)		/* TOD steering end time */
	slgr	%r0,%r1				/* now - ts_steering_end */
	ltgr	%r0,%r0				/* past end of steering ? */
	jm	17f
	srlg	%r0,%r0,15			/* 1 per 2^16 */
	tm	__VDSO_TS_DIR+3(%r5),0x01	/* steering direction? */
	jz	18f
	lcgr	%r0,%r0				/* negative TOD offset */
18:	algr	%r1,%r0				/* add steering offset */
17:	lgf	%r2,__VDSO_TK_SHIFT(%r5)	/* Timekeeper shift */
	sg	%r1,__VDSO_XTIME_STAMP(%r5)	/* TOD - cycle_last */
	msgf	%r1,__VDSO_TK_MULT(%r5)		/*  * tk->mult */
	alg	%r1,__VDSO_XTIME_NSEC(%r5)	/*  + tk->xtime_nsec */
	srlg	%r1,%r1,0(%r2)			/*  >> tk->shift */
	lg	%r0,__VDSO_XTIME_SEC(%r5)	/* tk->xtime_sec */
	clg	%r4,__VDSO_UPD_COUNT(%r5)	/* check update counter */
	jne	5b
	larl	%r5,13f
6:	clg	%r1,0(%r5)
	jl	7f
	slg	%r1,0(%r5)
	aghi	%r0,1
	j	6b
7:	stg	%r0,0(%r3)			/* store tp->tv_sec */
	stg	%r1,8(%r3)			/* store tp->tv_nsec */
	lghi	%r2,0
	aghi	%r15,16
	br	%r14

	/* CPUCLOCK_VIRT for this thread */
9:	lghi	%r4,0
	icm	%r0,15,__VDSO_ECTG_OK(%r5)
	jz	12f
	sacf	256				/* Magic ectg instruction */
	.insn	ssf,0xc80100000000,__VDSO_ECTG_BASE(4),__VDSO_ECTG_USER(4),4
	sacf	0
	algr	%r1,%r0				/* r1 = cputime as TOD value */
	mghi	%r1,1000			/* convert to nanoseconds */
	srlg	%r1,%r1,12			/* r1 = cputime in nanosec */
	lgr	%r4,%r1
	larl	%r5,13f
	srlg	%r1,%r1,9			/* divide by 1000000000 */
	mlg	%r0,8(%r5)
	srlg	%r0,%r0,11			/* r0 = tv_sec */
	stg	%r0,0(%r3)
	msg	%r0,0(%r5)			/* calculate tv_nsec */
	slgr	%r4,%r0				/* r4 = tv_nsec */
	stg	%r4,8(%r3)
	lghi	%r2,0
	aghi	%r15,16
	br	%r14

	/* Fallback to system call */
12:	lghi	%r1,__NR_clock_gettime
	svc	0
	aghi	%r15,16
	br	%r14

13:	.quad	1000000000
14:	.quad	19342813113834067
	.cfi_endproc
	.size	__kernel_clock_gettime,.-__kernel_clock_gettime

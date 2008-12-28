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
	larl	%r5,_vdso_data
	cghi	%r2,CLOCK_REALTIME
	je	4f
	cghi	%r2,CLOCK_MONOTONIC
	jne	9f

	/* CLOCK_MONOTONIC */
	ltgr	%r3,%r3
	jz	3f				/* tp == NULL */
0:	lg	%r4,__VDSO_UPD_COUNT(%r5)	/* load update counter */
	tmll	%r4,0x0001			/* pending update ? loop */
	jnz	0b
	stck	48(%r15)			/* Store TOD clock */
	lg	%r1,48(%r15)
	sg	%r1,__VDSO_XTIME_STAMP(%r5)	/* TOD - cycle_last */
	mghi	%r1,1000
	srlg	%r1,%r1,12			/* cyc2ns(clock,cycle_delta) */
	alg	%r1,__VDSO_XTIME_NSEC(%r5)	/*  + xtime */
	lg	%r0,__VDSO_XTIME_SEC(%r5)
	alg	%r1,__VDSO_WTOM_NSEC(%r5)	/*  + wall_to_monotonic */
	alg	%r0,__VDSO_WTOM_SEC(%r5)
	clg	%r4,__VDSO_UPD_COUNT(%r5)	/* check update counter */
	jne	0b
	larl	%r5,10f
1:	clg	%r1,0(%r5)
	jl	2f
	slg	%r1,0(%r5)
	aghi	%r0,1
	j	1b
2:	stg	%r0,0(%r3)			/* store tp->tv_sec */
	stg	%r1,8(%r3)			/* store tp->tv_nsec */
3:	lghi	%r2,0
	br	%r14

	/* CLOCK_REALTIME */
4:	ltr	%r3,%r3				/* tp == NULL */
	jz	8f
5:	lg	%r4,__VDSO_UPD_COUNT(%r5)	/* load update counter */
	tmll	%r4,0x0001			/* pending update ? loop */
	jnz	5b
	stck	48(%r15)			/* Store TOD clock */
	lg	%r1,48(%r15)
	sg	%r1,__VDSO_XTIME_STAMP(%r5)	/* TOD - cycle_last */
	mghi	%r1,1000
	srlg	%r1,%r1,12			/* cyc2ns(clock,cycle_delta) */
	alg	%r1,__VDSO_XTIME_NSEC(%r5)	/*  + xtime */
	lg	%r0,__VDSO_XTIME_SEC(%r5)
	clg	%r4,__VDSO_UPD_COUNT(%r5)	/* check update counter */
	jne	5b
	larl	%r5,10f
6:	clg	%r1,0(%r5)
	jl	7f
	slg	%r1,0(%r5)
	aghi	%r0,1
	j	6b
7:	stg	%r0,0(%r3)			/* store tp->tv_sec */
	stg	%r1,8(%r3)			/* store tp->tv_nsec */
8:	lghi	%r2,0
	br	%r14

	/* Fallback to system call */
9:	lghi	%r1,__NR_clock_gettime
	svc	0
	br	%r14

10:	.quad	1000000000
	.cfi_endproc
	.size	__kernel_clock_gettime,.-__kernel_clock_gettime

/*
 * Userland implementation of gettimeofday() for 32 bits processes in a
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
	.globl __kernel_gettimeofday
	.type  __kernel_gettimeofday,@function
__kernel_gettimeofday:
	.cfi_startproc
	ahi	%r15,-16
	basr	%r5,0
0:	al	%r5,13f-0b(%r5)			/* get &_vdso_data */
1:	ltr	%r3,%r3				/* check if tz is NULL */
	je	2f
	mvc	0(8,%r3),__VDSO_TIMEZONE(%r5)
2:	ltr	%r2,%r2				/* check if tv is NULL */
	je	10f
	l	%r4,__VDSO_UPD_COUNT+4(%r5)	/* load update counter */
	tml	%r4,0x0001			/* pending update ? loop */
	jnz	1b
	stcke	0(%r15)				/* Store TOD clock */
	lm	%r0,%r1,1(%r15)
	s	%r0,__VDSO_XTIME_STAMP(%r5)	/* TOD - cycle_last */
	sl	%r1,__VDSO_XTIME_STAMP+4(%r5)
	brc	3,3f
	ahi	%r0,-1
3:	ms	%r0,__VDSO_TK_MULT(%r5)		/*  * tk->mult */
	st	%r0,0(%r15)
	l	%r0,__VDSO_TK_MULT(%r5)
	ltr	%r1,%r1
	mr	%r0,%r0
	jnm	4f
	a	%r0,__VDSO_TK_MULT(%r5)
4:	al	%r0,0(%r15)
	al	%r0,__VDSO_XTIME_NSEC(%r5)	/*  + xtime */
	al	%r1,__VDSO_XTIME_NSEC+4(%r5)
	brc	12,5f
	ahi	%r0,1
5:	mvc	0(4,%r15),__VDSO_XTIME_SEC+4(%r5)
	cl	%r4,__VDSO_UPD_COUNT+4(%r5)	/* check update counter */
	jne	1b
	l	%r4,__VDSO_TK_SHIFT(%r5)	/* Timekeeper shift */
	srdl	%r0,0(%r4)			/*  >> tk->shift */
	l	%r4,0(%r15)			/* get tv_sec from stack */
	basr	%r5,0
6:	ltr	%r0,%r0
	jnz	7f
	cl	%r1,11f-6b(%r5)
	jl	8f
7:	ahi	%r4,1
	sl	%r1,11f-6b(%r5)
	brc	3,6b
	ahi	%r0,-1
	j	6b
8:	st	%r4,0(%r2)			/* store tv->tv_sec */
	ltr	%r1,%r1
	m	%r0,12f-6b(%r5)
	jnm	9f
	al	%r0,12f-6b(%r5)
9:	srl	%r0,6
	st	%r0,4(%r2)			/* store tv->tv_usec */
10:	slr	%r2,%r2
	ahi	%r15,16
	br	%r14
11:	.long	1000000000
12:	.long	274877907
13:	.long	_vdso_data - 0b
	.cfi_endproc
	.size	__kernel_gettimeofday,.-__kernel_gettimeofday

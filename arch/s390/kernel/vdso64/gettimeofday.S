/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Userland implementation of gettimeofday() for 64 bits processes in a
 * s390 kernel for use in the vDSO
 *
 *  Copyright IBM Corp. 2008
 *  Author(s): Martin Schwidefsky (schwidefsky@de.ibm.com)
 */
#include <asm/vdso.h>
#include <asm/asm-offsets.h>
#include <asm/unistd.h>
#include <asm/dwarf.h>

	.text
	.align 4
	.globl __kernel_gettimeofday
	.type  __kernel_gettimeofday,@function
__kernel_gettimeofday:
	CFI_STARTPROC
	aghi	%r15,-16
	CFI_ADJUST_CFA_OFFSET 16
	CFI_VAL_OFFSET 15, -160
	larl	%r5,_vdso_data
0:	ltgr	%r3,%r3				/* check if tz is NULL */
	je	1f
	mvc	0(8,%r3),__VDSO_TIMEZONE(%r5)
1:	ltgr	%r2,%r2				/* check if tv is NULL */
	je	4f
	lg	%r4,__VDSO_UPD_COUNT(%r5)	/* load update counter */
	tmll	%r4,0x0001			/* pending update ? loop */
	jnz	0b
	stcke	0(%r15)				/* Store TOD clock */
	lg	%r1,1(%r15)
	lg	%r0,__VDSO_TS_END(%r5)		/* TOD steering end time */
	slgr	%r0,%r1				/* now - ts_steering_end */
	ltgr	%r0,%r0				/* past end of steering ? */
	jm	6f
	srlg	%r0,%r0,15			/* 1 per 2^16 */
	tm	__VDSO_TS_DIR+3(%r5),0x01	/* steering direction? */
	jz	7f
	lcgr	%r0,%r0				/* negative TOD offset */
7:	algr	%r1,%r0				/* add steering offset */
6:	sg	%r1,__VDSO_XTIME_STAMP(%r5)	/* TOD - cycle_last */
	msgf	%r1,__VDSO_TK_MULT(%r5)		/*  * tk->mult */
	alg	%r1,__VDSO_XTIME_NSEC(%r5)	/*  + tk->xtime_nsec */
	lg	%r0,__VDSO_XTIME_SEC(%r5)	/* tk->xtime_sec */
	clg	%r4,__VDSO_UPD_COUNT(%r5)	/* check update counter */
	jne	0b
	lgf	%r5,__VDSO_TK_SHIFT(%r5)	/* Timekeeper shift */
	srlg	%r1,%r1,0(%r5)			/*  >> tk->shift */
	larl	%r5,5f
2:	clg	%r1,0(%r5)
	jl	3f
	slg	%r1,0(%r5)
	aghi	%r0,1
	j	2b
3:	stg	%r0,0(%r2)			/* store tv->tv_sec */
	slgr	%r0,%r0				/* tv_nsec -> tv_usec */
	ml	%r0,8(%r5)
	srlg	%r0,%r0,6
	stg	%r0,8(%r2)			/* store tv->tv_usec */
4:	lghi	%r2,0
	aghi	%r15,16
	CFI_ADJUST_CFA_OFFSET -16
	CFI_RESTORE 15
	br	%r14
	CFI_ENDPROC
5:	.quad	1000000000
	.long	274877907
	.size	__kernel_gettimeofday,.-__kernel_gettimeofday

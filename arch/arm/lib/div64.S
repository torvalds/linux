/*
 *  linux/arch/arm/lib/div64.S
 *
 *  Optimized computation of 64-bit dividend / 32-bit divisor
 *
 *  Author:	Nicolas Pitre
 *  Created:	Oct 5, 2003
 *  Copyright:	Monta Vista Software, Inc.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */

#include <linux/linkage.h>

#ifdef __ARMEB__
#define xh r0
#define xl r1
#define yh r2
#define yl r3
#else
#define xl r0
#define xh r1
#define yl r2
#define yh r3
#endif

/*
 * __do_div64: perform a division with 64-bit dividend and 32-bit divisor.
 *
 * Note: Calling convention is totally non standard for optimal code.
 *       This is meant to be used by do_div() from include/asm/div64.h only.
 *
 * Input parameters:
 * 	xh-xl	= dividend (clobbered)
 * 	r4	= divisor (preserved)
 *
 * Output values:
 * 	yh-yl	= result
 * 	xh	= remainder
 *
 * Clobbered regs: xl, ip
 */

ENTRY(__do_div64)

	@ Test for easy paths first.
	subs	ip, r4, #1
	bls	9f			@ divisor is 0 or 1
	tst	ip, r4
	beq	8f			@ divisor is power of 2

	@ See if we need to handle upper 32-bit result.
	cmp	xh, r4
	mov	yh, #0
	blo	3f

	@ Align divisor with upper part of dividend.
	@ The aligned divisor is stored in yl preserving the original.
	@ The bit position is stored in ip.

#if __LINUX_ARM_ARCH__ >= 5

	clz	yl, r4
	clz	ip, xh
	sub	yl, yl, ip
	mov	ip, #1
	mov	ip, ip, lsl yl
	mov	yl, r4, lsl yl

#else

	mov	yl, r4
	mov	ip, #1
1:	cmp	yl, #0x80000000
	cmpcc	yl, xh
	movcc	yl, yl, lsl #1
	movcc	ip, ip, lsl #1
	bcc	1b

#endif

	@ The division loop for needed upper bit positions.
 	@ Break out early if dividend reaches 0.
2:	cmp	xh, yl
	orrcs	yh, yh, ip
	subcss	xh, xh, yl
	movnes	ip, ip, lsr #1
	mov	yl, yl, lsr #1
	bne	2b

	@ See if we need to handle lower 32-bit result.
3:	cmp	xh, #0
	mov	yl, #0
	cmpeq	xl, r4
	movlo	xh, xl
	movlo	pc, lr

	@ The division loop for lower bit positions.
	@ Here we shift remainer bits leftwards rather than moving the
	@ divisor for comparisons, considering the carry-out bit as well.
	mov	ip, #0x80000000
4:	movs	xl, xl, lsl #1
	adcs	xh, xh, xh
	beq	6f
	cmpcc	xh, r4
5:	orrcs	yl, yl, ip
	subcs	xh, xh, r4
	movs	ip, ip, lsr #1
	bne	4b
	mov	pc, lr

	@ The top part of remainder became zero.  If carry is set
	@ (the 33th bit) this is a false positive so resume the loop.
	@ Otherwise, if lower part is also null then we are done.
6:	bcs	5b
	cmp	xl, #0
	moveq	pc, lr

	@ We still have remainer bits in the low part.  Bring them up.

#if __LINUX_ARM_ARCH__ >= 5

	clz	xh, xl			@ we know xh is zero here so...
	add	xh, xh, #1
	mov	xl, xl, lsl xh
	mov	ip, ip, lsr xh

#else

7:	movs	xl, xl, lsl #1
	mov	ip, ip, lsr #1
	bcc	7b

#endif

	@ Current remainder is now 1.  It is worthless to compare with
	@ divisor at this point since divisor can not be smaller than 3 here.
	@ If possible, branch for another shift in the division loop.
	@ If no bit position left then we are done.
	movs	ip, ip, lsr #1
	mov	xh, #1
	bne	4b
	mov	pc, lr

8:	@ Division by a power of 2: determine what that divisor order is
	@ then simply shift values around

#if __LINUX_ARM_ARCH__ >= 5

	clz	ip, r4
	rsb	ip, ip, #31

#else

	mov	yl, r4
	cmp	r4, #(1 << 16)
	mov	ip, #0
	movhs	yl, yl, lsr #16
	movhs	ip, #16

	cmp	yl, #(1 << 8)
	movhs	yl, yl, lsr #8
	addhs	ip, ip, #8

	cmp	yl, #(1 << 4)
	movhs	yl, yl, lsr #4
	addhs	ip, ip, #4

	cmp	yl, #(1 << 2)
	addhi	ip, ip, #3
	addls	ip, ip, yl, lsr #1

#endif

	mov	yh, xh, lsr ip
	mov	yl, xl, lsr ip
	rsb	ip, ip, #32
	orr	yl, yl, xh, lsl ip
	mov	xh, xl, lsl ip
	mov	xh, xh, lsr ip
	mov	pc, lr

	@ eq -> division by 1: obvious enough...
9:	moveq	yl, xl
	moveq	yh, xh
	moveq	xh, #0
	moveq	pc, lr

	@ Division by 0:
	str	lr, [sp, #-4]!
	bl	__div0

	@ as wrong as it could be...
	mov	yl, #0
	mov	yh, #0
	mov	xh, #0
	ldr	pc, [sp], #4


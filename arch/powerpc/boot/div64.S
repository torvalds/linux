/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Divide a 64-bit unsigned number by a 32-bit unsigned number.
 * This routine assumes that the top 32 bits of the dividend are
 * non-zero to start with.
 * On entry, r3 points to the dividend, which get overwritten with
 * the 64-bit quotient, and r4 contains the divisor.
 * On exit, r3 contains the remainder.
 *
 * Copyright (C) 2002 Paul Mackerras, IBM Corp.
 */
#include "ppc_asm.h"

	.globl __div64_32
__div64_32:
	lwz	r5,0(r3)	# get the dividend into r5/r6
	lwz	r6,4(r3)
	cmplw	r5,r4
	li	r7,0
	li	r8,0
	blt	1f
	divwu	r7,r5,r4	# if dividend.hi >= divisor,
	mullw	r0,r7,r4	# quotient.hi = dividend.hi / divisor
	subf.	r5,r0,r5	# dividend.hi %= divisor
	beq	3f
1:	mr	r11,r5		# here dividend.hi != 0
	andis.	r0,r5,0xc000
	bne	2f
	cntlzw	r0,r5		# we are shifting the dividend right
	li	r10,-1		# to make it < 2^32, and shifting
	srw	r10,r10,r0	# the divisor right the same amount,
	addc	r9,r4,r10	# rounding up (so the estimate cannot
	andc	r11,r6,r10	# ever be too large, only too small)
	andc	r9,r9,r10
	addze	r9,r9
	or	r11,r5,r11
	rotlw	r9,r9,r0
	rotlw	r11,r11,r0
	divwu	r11,r11,r9	# then we divide the shifted quantities
2:	mullw	r10,r11,r4	# to get an estimate of the quotient,
	mulhwu	r9,r11,r4	# multiply the estimate by the divisor,
	subfc	r6,r10,r6	# take the product from the divisor,
	add	r8,r8,r11	# and add the estimate to the accumulated
	subfe.	r5,r9,r5	# quotient
	bne	1b
3:	cmplw	r6,r4
	blt	4f
	divwu	r0,r6,r4	# perform the remaining 32-bit division
	mullw	r10,r0,r4	# and get the remainder
	add	r8,r8,r0
	subf	r6,r10,r6
4:	stw	r7,0(r3)	# return the quotient in *r3
	stw	r8,4(r3)
	mr	r3,r6		# return the remainder in r3
	blr

/*
 * Extended precision shifts.
 *
 * Updated to be valid for shift counts from 0 to 63 inclusive.
 * -- Gabriel
 *
 * R3/R4 has 64 bit value
 * R5    has shift count
 * result in R3/R4
 *
 *  ashrdi3: arithmetic right shift (sign propagation)	
 *  lshrdi3: logical right shift
 *  ashldi3: left shift
 */
	.globl __ashrdi3
__ashrdi3:
	subfic	r6,r5,32
	srw	r4,r4,r5	# LSW = count > 31 ? 0 : LSW >> count
	addi	r7,r5,32	# could be xori, or addi with -32
	slw	r6,r3,r6	# t1 = count > 31 ? 0 : MSW << (32-count)
	rlwinm	r8,r7,0,32	# t3 = (count < 32) ? 32 : 0
	sraw	r7,r3,r7	# t2 = MSW >> (count-32)
	or	r4,r4,r6	# LSW |= t1
	slw	r7,r7,r8	# t2 = (count < 32) ? 0 : t2
	sraw	r3,r3,r5	# MSW = MSW >> count
	or	r4,r4,r7	# LSW |= t2
	blr

	.globl __ashldi3
__ashldi3:
	subfic	r6,r5,32
	slw	r3,r3,r5	# MSW = count > 31 ? 0 : MSW << count
	addi	r7,r5,32	# could be xori, or addi with -32
	srw	r6,r4,r6	# t1 = count > 31 ? 0 : LSW >> (32-count)
	slw	r7,r4,r7	# t2 = count < 32 ? 0 : LSW << (count-32)
	or	r3,r3,r6	# MSW |= t1
	slw	r4,r4,r5	# LSW = LSW << count
	or	r3,r3,r7	# MSW |= t2
	blr

	.globl __lshrdi3
__lshrdi3:
	subfic	r6,r5,32
	srw	r4,r4,r5	# LSW = count > 31 ? 0 : LSW >> count
	addi	r7,r5,32	# could be xori, or addi with -32
	slw	r6,r3,r6	# t1 = count > 31 ? 0 : MSW << (32-count)
	srw	r7,r3,r7	# t2 = count < 32 ? 0 : MSW >> (count-32)
	or	r4,r4,r6	# LSW |= t1
	srw	r3,r3,r5	# MSW = MSW >> count
	or	r4,r4,r7	# LSW |= t2
	blr

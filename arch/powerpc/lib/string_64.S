/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *
 * Copyright (C) IBM Corporation, 2012
 *
 * Author: Anton Blanchard <anton@au.ibm.com>
 */

#include <asm/ppc_asm.h>
#include <asm/linkage.h>
#include <asm/asm-offsets.h>
#include <asm/export.h>

	.section	".toc","aw"
PPC64_CACHES:
	.tc		ppc64_caches[TC],ppc64_caches
	.section	".text"

/**
 * __arch_clear_user: - Zero a block of memory in user space, with less checking.
 * @to:   Destination address, in user space.
 * @n:    Number of bytes to zero.
 *
 * Zero a block of memory in user space.  Caller must check
 * the specified block with access_ok() before calling this function.
 *
 * Returns number of bytes that could not be cleared.
 * On success, this will be zero.
 */

	.macro err1
100:
	EX_TABLE(100b,.Ldo_err1)
	.endm

	.macro err2
200:
	EX_TABLE(200b,.Ldo_err2)
	.endm

	.macro err3
300:
	EX_TABLE(300b,.Ldo_err3)
	.endm

.Ldo_err1:
	mr	r3,r8

.Ldo_err2:
	mtctr	r4
1:
err3;	stb	r0,0(r3)
	addi	r3,r3,1
	addi	r4,r4,-1
	bdnz	1b

.Ldo_err3:
	mr	r3,r4
	blr

_GLOBAL_TOC(__arch_clear_user)
	cmpdi	r4,32
	neg	r6,r3
	li	r0,0
	blt	.Lshort_clear
	mr	r8,r3
	mtocrf	0x01,r6
	clrldi	r6,r6,(64-3)

	/* Get the destination 8 byte aligned */
	bf	cr7*4+3,1f
err1;	stb	r0,0(r3)
	addi	r3,r3,1

1:	bf	cr7*4+2,2f
err1;	sth	r0,0(r3)
	addi	r3,r3,2

2:	bf	cr7*4+1,3f
err1;	stw	r0,0(r3)
	addi	r3,r3,4

3:	sub	r4,r4,r6

	cmpdi	r4,32
	cmpdi	cr1,r4,512
	blt	.Lshort_clear
	bgt	cr1,.Llong_clear

.Lmedium_clear:
	srdi	r6,r4,5
	mtctr	r6

	/* Do 32 byte chunks */
4:
err2;	std	r0,0(r3)
err2;	std	r0,8(r3)
err2;	std	r0,16(r3)
err2;	std	r0,24(r3)
	addi	r3,r3,32
	addi	r4,r4,-32
	bdnz	4b

.Lshort_clear:
	/* up to 31 bytes to go */
	cmpdi	r4,16
	blt	6f
err2;	std	r0,0(r3)
err2;	std	r0,8(r3)
	addi	r3,r3,16
	addi	r4,r4,-16

	/* Up to 15 bytes to go */
6:	mr	r8,r3
	clrldi	r4,r4,(64-4)
	mtocrf	0x01,r4
	bf	cr7*4+0,7f
err1;	std	r0,0(r3)
	addi	r3,r3,8

7:	bf	cr7*4+1,8f
err1;	stw	r0,0(r3)
	addi	r3,r3,4

8:	bf	cr7*4+2,9f
err1;	sth	r0,0(r3)
	addi	r3,r3,2

9:	bf	cr7*4+3,10f
err1;	stb	r0,0(r3)

10:	li	r3,0
	blr

.Llong_clear:
	ld	r5,PPC64_CACHES@toc(r2)

	bf	cr7*4+0,11f
err2;	std	r0,0(r3)
	addi	r3,r3,8
	addi	r4,r4,-8

	/* Destination is 16 byte aligned, need to get it cache block aligned */
11:	lwz	r7,DCACHEL1LOGBLOCKSIZE(r5)
	lwz	r9,DCACHEL1BLOCKSIZE(r5)

	/*
	 * With worst case alignment the long clear loop takes a minimum
	 * of 1 byte less than 2 cachelines.
	 */
	sldi	r10,r9,2
	cmpd	r4,r10
	blt	.Lmedium_clear

	neg	r6,r3
	addi	r10,r9,-1
	and.	r5,r6,r10
	beq	13f

	srdi	r6,r5,4
	mtctr	r6
	mr	r8,r3
12:
err1;	std	r0,0(r3)
err1;	std	r0,8(r3)
	addi	r3,r3,16
	bdnz	12b

	sub	r4,r4,r5

13:	srd	r6,r4,r7
	mtctr	r6
	mr	r8,r3
14:
err1;	dcbz	0,r3
	add	r3,r3,r9
	bdnz	14b

	and	r4,r4,r10

	cmpdi	r4,32
	blt	.Lshort_clear
	b	.Lmedium_clear
EXPORT_SYMBOL(__arch_clear_user)

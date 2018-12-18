/* SPDX-License-Identifier: GPL-2.0 */

/*
 * String handling functions for PowerPC32
 *
 * Copyright (C) 1996 Paul Mackerras.
 *
 */

#include <asm/ppc_asm.h>
#include <asm/export.h>
#include <asm/cache.h>

	.text

CACHELINE_BYTES = L1_CACHE_BYTES
LG_CACHELINE_BYTES = L1_CACHE_SHIFT
CACHELINE_MASK = (L1_CACHE_BYTES-1)

_GLOBAL(__clear_user)
/*
 * Use dcbz on the complete cache lines in the destination
 * to set them to zero.  This requires that the destination
 * area is cacheable.
 */
	cmplwi	cr0, r4, 4
	mr	r10, r3
	li	r3, 0
	blt	7f

11:	stw	r3, 0(r10)
	beqlr
	andi.	r0, r10, 3
	add	r11, r0, r4
	subf	r6, r0, r10

	clrlwi	r7, r6, 32 - LG_CACHELINE_BYTES
	add	r8, r7, r11
	srwi	r9, r8, LG_CACHELINE_BYTES
	addic.	r9, r9, -1	/* total number of complete cachelines */
	ble	2f
	xori	r0, r7, CACHELINE_MASK & ~3
	srwi.	r0, r0, 2
	beq	3f
	mtctr	r0
4:	stwu	r3, 4(r6)
	bdnz	4b
3:	mtctr	r9
	li	r7, 4
10:	dcbz	r7, r6
	addi	r6, r6, CACHELINE_BYTES
	bdnz	10b
	clrlwi	r11, r8, 32 - LG_CACHELINE_BYTES
	addi	r11, r11, 4

2:	srwi	r0 ,r11 ,2
	mtctr	r0
	bdz	6f
1:	stwu	r3, 4(r6)
	bdnz	1b
6:	andi.	r11, r11, 3
	beqlr
	mtctr	r11
	addi	r6, r6, 3
8:	stbu	r3, 1(r6)
	bdnz	8b
	blr

7:	cmpwi	cr0, r4, 0
	beqlr
	mtctr	r4
	addi	r6, r10, -1
9:	stbu	r3, 1(r6)
	bdnz	9b
	blr

90:	mr	r3, r4
	blr
91:	add	r3, r10, r4
	subf	r3, r6, r3
	blr

	EX_TABLE(11b, 90b)
	EX_TABLE(4b, 91b)
	EX_TABLE(10b, 91b)
	EX_TABLE(1b, 91b)
	EX_TABLE(8b, 91b)
	EX_TABLE(9b, 91b)

EXPORT_SYMBOL(__clear_user)

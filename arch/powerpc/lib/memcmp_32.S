/* SPDX-License-Identifier: GPL-2.0 */

/*
 * memcmp for PowerPC32
 *
 * Copyright (C) 1996 Paul Mackerras.
 *
 */

#include <linux/export.h>
#include <asm/ppc_asm.h>

	.text

_GLOBAL(memcmp)
	srawi.	r7, r5, 2		/* Divide len by 4 */
	mr	r6, r3
	beq-	3f
	mtctr	r7
	li	r7, 0
1:	lwzx	r3, r6, r7
	lwzx	r0, r4, r7
	addi	r7, r7, 4
	cmplw	cr0, r3, r0
	bdnzt	eq, 1b
	bne	5f
3:	andi.	r3, r5, 3
	beqlr
	cmplwi	cr1, r3, 2
	blt-	cr1, 4f
	lhzx	r3, r6, r7
	lhzx	r0, r4, r7
	addi	r7, r7, 2
	subf.	r3, r0, r3
	beqlr	cr1
	bnelr
4:	lbzx	r3, r6, r7
	lbzx	r0, r4, r7
	subf.	r3, r0, r3
	blr
5:	li	r3, 1
	bgtlr
	li	r3, -1
	blr
EXPORT_SYMBOL(memcmp)

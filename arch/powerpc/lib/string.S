/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * String handling functions for PowerPC.
 *
 * Copyright (C) 1996 Paul Mackerras.
 */
#include <linux/export.h>
#include <asm/ppc_asm.h>
#include <asm/cache.h>

	.text
	
_GLOBAL(strncmp)
	PPC_LCMPI 0,r5,0
	beq-	2f
	mtctr	r5
	addi	r5,r3,-1
	addi	r4,r4,-1
	.balign IFETCH_ALIGN_BYTES
1:	lbzu	r3,1(r5)
	cmpwi	1,r3,0
	lbzu	r0,1(r4)
	subf.	r3,r0,r3
	beqlr	1
	bdnzt	eq,1b
	blr
2:	li	r3,0
	blr
EXPORT_SYMBOL(strncmp)

_GLOBAL(memchr)
	PPC_LCMPI 0,r5,0
	beq-	2f
	mtctr	r5
	addi	r3,r3,-1
	.balign IFETCH_ALIGN_BYTES
1:	lbzu	r0,1(r3)
	cmpw	0,r0,r4
	bdnzf	2,1b
	beqlr
2:	li	r3,0
	blr
EXPORT_SYMBOL(memchr)

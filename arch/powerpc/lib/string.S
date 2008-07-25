/*
 * String handling functions for PowerPC.
 *
 * Copyright (C) 1996 Paul Mackerras.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */
#include <asm/processor.h>
#include <asm/errno.h>
#include <asm/ppc_asm.h>

	.section __ex_table,"a"
	PPC_LONG_ALIGN
	.text
	
_GLOBAL(strcpy)
	addi	r5,r3,-1
	addi	r4,r4,-1
1:	lbzu	r0,1(r4)
	cmpwi	0,r0,0
	stbu	r0,1(r5)
	bne	1b
	blr

/* This clears out any unused part of the destination buffer,
   just as the libc version does.  -- paulus */
_GLOBAL(strncpy)
	cmpwi	0,r5,0
	beqlr
	mtctr	r5
	addi	r6,r3,-1
	addi	r4,r4,-1
1:	lbzu	r0,1(r4)
	cmpwi	0,r0,0
	stbu	r0,1(r6)
	bdnzf	2,1b		/* dec ctr, branch if ctr != 0 && !cr0.eq */
	bnelr			/* if we didn't hit a null char, we're done */
	mfctr	r5
	cmpwi	0,r5,0		/* any space left in destination buffer? */
	beqlr			/* we know r0 == 0 here */
2:	stbu	r0,1(r6)	/* clear it out if so */
	bdnz	2b
	blr

_GLOBAL(strcat)
	addi	r5,r3,-1
	addi	r4,r4,-1
1:	lbzu	r0,1(r5)
	cmpwi	0,r0,0
	bne	1b
	addi	r5,r5,-1
1:	lbzu	r0,1(r4)
	cmpwi	0,r0,0
	stbu	r0,1(r5)
	bne	1b
	blr

_GLOBAL(strcmp)
	addi	r5,r3,-1
	addi	r4,r4,-1
1:	lbzu	r3,1(r5)
	cmpwi	1,r3,0
	lbzu	r0,1(r4)
	subf.	r3,r0,r3
	beqlr	1
	beq	1b
	blr

_GLOBAL(strncmp)
	PPC_LCMPI r5,0
	beqlr
	mtctr	r5
	addi	r5,r3,-1
	addi	r4,r4,-1
1:	lbzu	r3,1(r5)
	cmpwi	1,r3,0
	lbzu	r0,1(r4)
	subf.	r3,r0,r3
	beqlr	1
	bdnzt	eq,1b
	blr

_GLOBAL(strlen)
	addi	r4,r3,-1
1:	lbzu	r0,1(r4)
	cmpwi	0,r0,0
	bne	1b
	subf	r3,r3,r4
	blr

_GLOBAL(memcmp)
	cmpwi	0,r5,0
	ble-	2f
	mtctr	r5
	addi	r6,r3,-1
	addi	r4,r4,-1
1:	lbzu	r3,1(r6)
	lbzu	r0,1(r4)
	subf.	r3,r0,r3
	bdnzt	2,1b
	blr
2:	li	r3,0
	blr

_GLOBAL(memchr)
	cmpwi	0,r5,0
	ble-	2f
	mtctr	r5
	addi	r3,r3,-1
1:	lbzu	r0,1(r3)
	cmpw	0,r0,r4
	bdnzf	2,1b
	beqlr
2:	li	r3,0
	blr

_GLOBAL(__clear_user)
	addi	r6,r3,-4
	li	r3,0
	li	r5,0
	cmplwi	0,r4,4
	blt	7f
	/* clear a single word */
11:	stwu	r5,4(r6)
	beqlr
	/* clear word sized chunks */
	andi.	r0,r6,3
	add	r4,r0,r4
	subf	r6,r0,r6
	srwi	r0,r4,2
	andi.	r4,r4,3
	mtctr	r0
	bdz	7f
1:	stwu	r5,4(r6)
	bdnz	1b
	/* clear byte sized chunks */
7:	cmpwi	0,r4,0
	beqlr
	mtctr	r4
	addi	r6,r6,3
8:	stbu	r5,1(r6)
	bdnz	8b
	blr
90:	mr	r3,r4
	blr
91:	mfctr	r3
	slwi	r3,r3,2
	add	r3,r3,r4
	blr
92:	mfctr	r3
	blr

	.section __ex_table,"a"
	PPC_LONG	11b,90b
	PPC_LONG	1b,91b
	PPC_LONG	8b,92b
	.text

_GLOBAL(__strncpy_from_user)
	addi	r6,r3,-1
	addi	r4,r4,-1
	cmpwi	0,r5,0
	beq	2f
	mtctr	r5
1:	lbzu	r0,1(r4)
	cmpwi	0,r0,0
	stbu	r0,1(r6)
	bdnzf	2,1b		/* dec ctr, branch if ctr != 0 && !cr0.eq */
	beq	3f
2:	addi	r6,r6,1
3:	subf	r3,r3,r6
	blr
99:	li	r3,-EFAULT
	blr

	.section __ex_table,"a"
	PPC_LONG	1b,99b
	.text

/* r3 = str, r4 = len (> 0), r5 = top (highest addr) */
_GLOBAL(__strnlen_user)
	addi	r7,r3,-1
	subf	r6,r7,r5	/* top+1 - str */
	cmplw	0,r4,r6
	bge	0f
	mr	r6,r4
0:	mtctr	r6		/* ctr = min(len, top - str) */
1:	lbzu	r0,1(r7)	/* get next byte */
	cmpwi	0,r0,0
	bdnzf	2,1b		/* loop if --ctr != 0 && byte != 0 */
	addi	r7,r7,1
	subf	r3,r3,r7	/* number of bytes we have looked at */
	beqlr			/* return if we found a 0 byte */
	cmpw	0,r3,r4		/* did we look at all len bytes? */
	blt	99f		/* if not, must have hit top */
	addi	r3,r4,1		/* return len + 1 to indicate no null found */
	blr
99:	li	r3,0		/* bad address, return 0 */
	blr

	.section __ex_table,"a"
	PPC_LONG	1b,99b

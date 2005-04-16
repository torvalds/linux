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
#define r0	0
#define r3	3
#define r4	4
#define r5	5
#define r6	6
#define r7	7
#define r8	8

	.globl	strlen
strlen:
	addi	r4,r3,-1
1:	lbzu	r0,1(r4)
	cmpwi	0,r0,0
	bne	1b
	subf	r3,r3,r4
	blr

	.globl	memset
memset:
	rlwimi	r4,r4,8,16,23
	rlwimi	r4,r4,16,0,15
	addi	r6,r3,-4
	cmplwi	0,r5,4
	blt	7f
	stwu	r4,4(r6)
	beqlr
	andi.	r0,r6,3
	add	r5,r0,r5
	subf	r6,r0,r6
	rlwinm	r0,r5,32-2,2,31
	mtctr	r0
	bdz	6f
1:	stwu	r4,4(r6)
	bdnz	1b
6:	andi.	r5,r5,3
7:	cmpwi	0,r5,0
	beqlr
	mtctr	r5
	addi	r6,r6,3
8:	stbu	r4,1(r6)
	bdnz	8b
	blr

	.globl	memmove
memmove:
	cmplw	0,r3,r4
	bgt	backwards_memcpy
	/* fall through */

	.globl	memcpy
memcpy:
	rlwinm.	r7,r5,32-3,3,31		/* r0 = r5 >> 3 */
	addi	r6,r3,-4
	addi	r4,r4,-4
	beq	2f			/* if less than 8 bytes to do */
	andi.	r0,r6,3			/* get dest word aligned */
	mtctr	r7
	bne	5f
1:	lwz	r7,4(r4)
	lwzu	r8,8(r4)
	stw	r7,4(r6)
	stwu	r8,8(r6)
	bdnz	1b
	andi.	r5,r5,7
2:	cmplwi	0,r5,4
	blt	3f
	lwzu	r0,4(r4)
	addi	r5,r5,-4
	stwu	r0,4(r6)
3:	cmpwi	0,r5,0
	beqlr
	mtctr	r5
	addi	r4,r4,3
	addi	r6,r6,3
4:	lbzu	r0,1(r4)
	stbu	r0,1(r6)
	bdnz	4b
	blr
5:	subfic	r0,r0,4
	mtctr	r0
6:	lbz	r7,4(r4)
	addi	r4,r4,1
	stb	r7,4(r6)
	addi	r6,r6,1
	bdnz	6b
	subf	r5,r0,r5
	rlwinm.	r7,r5,32-3,3,31
	beq	2b
	mtctr	r7
	b	1b

	.globl	backwards_memcpy
backwards_memcpy:
	rlwinm.	r7,r5,32-3,3,31		/* r0 = r5 >> 3 */
	add	r6,r3,r5
	add	r4,r4,r5
	beq	2f
	andi.	r0,r6,3
	mtctr	r7
	bne	5f
1:	lwz	r7,-4(r4)
	lwzu	r8,-8(r4)
	stw	r7,-4(r6)
	stwu	r8,-8(r6)
	bdnz	1b
	andi.	r5,r5,7
2:	cmplwi	0,r5,4
	blt	3f
	lwzu	r0,-4(r4)
	subi	r5,r5,4
	stwu	r0,-4(r6)
3:	cmpwi	0,r5,0
	beqlr
	mtctr	r5
4:	lbzu	r0,-1(r4)
	stbu	r0,-1(r6)
	bdnz	4b
	blr
5:	mtctr	r0
6:	lbzu	r7,-1(r4)
	stbu	r7,-1(r6)
	bdnz	6b
	subf	r5,r0,r5
	rlwinm.	r7,r5,32-3,3,31
	beq	2b
	mtctr	r7
	b	1b

	.globl	memcmp
memcmp:
	cmpwi	0,r5,0
	blelr
	mtctr	r5
	addi	r6,r3,-1
	addi	r4,r4,-1
1:	lbzu	r3,1(r6)
	lbzu	r0,1(r4)
	subf.	r3,r0,r3
	bdnzt	2,1b
	blr

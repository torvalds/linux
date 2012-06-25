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

_GLOBAL(memset)
	neg	r0,r3
	rlwimi	r4,r4,8,16,23
	andi.	r0,r0,7			/* # bytes to be 8-byte aligned */
	rlwimi	r4,r4,16,0,15
	cmplw	cr1,r5,r0		/* do we get that far? */
	rldimi	r4,r4,32,0
	PPC_MTOCRF(1,R0)
	mr	r6,r3
	blt	cr1,8f
	beq+	3f			/* if already 8-byte aligned */
	subf	r5,r0,r5
	bf	31,1f
	stb	r4,0(r6)
	addi	r6,r6,1
1:	bf	30,2f
	sth	r4,0(r6)
	addi	r6,r6,2
2:	bf	29,3f
	stw	r4,0(r6)
	addi	r6,r6,4
3:	srdi.	r0,r5,6
	clrldi	r5,r5,58
	mtctr	r0
	beq	5f
4:	std	r4,0(r6)
	std	r4,8(r6)
	std	r4,16(r6)
	std	r4,24(r6)
	std	r4,32(r6)
	std	r4,40(r6)
	std	r4,48(r6)
	std	r4,56(r6)
	addi	r6,r6,64
	bdnz	4b
5:	srwi.	r0,r5,3
	clrlwi	r5,r5,29
	PPC_MTOCRF(1,R0)
	beq	8f
	bf	29,6f
	std	r4,0(r6)
	std	r4,8(r6)
	std	r4,16(r6)
	std	r4,24(r6)
	addi	r6,r6,32
6:	bf	30,7f
	std	r4,0(r6)
	std	r4,8(r6)
	addi	r6,r6,16
7:	bf	31,8f
	std	r4,0(r6)
	addi	r6,r6,8
8:	cmpwi	r5,0
	PPC_MTOCRF(1,R5)
	beqlr+
	bf	29,9f
	stw	r4,0(r6)
	addi	r6,r6,4
9:	bf	30,10f
	sth	r4,0(r6)
	addi	r6,r6,2
10:	bflr	31
	stb	r4,0(r6)
	blr

_GLOBAL(memmove)
	cmplw	0,r3,r4
	bgt	.backwards_memcpy
	b	.memcpy

_GLOBAL(backwards_memcpy)
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

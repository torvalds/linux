/*
 * Author: Anton Blanchard <anton@au.ibm.com>
 * Copyright 2015 IBM Corporation.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */
#include <asm/ppc_asm.h>
#include <asm/export.h>

#define off8	r6
#define off16	r7
#define off24	r8

#define rA	r9
#define rB	r10
#define rC	r11
#define rD	r27
#define rE	r28
#define rF	r29
#define rG	r30
#define rH	r31

#ifdef __LITTLE_ENDIAN__
#define LD	ldbrx
#else
#define LD	ldx
#endif

_GLOBAL(memcmp)
	cmpdi	cr1,r5,0

	/* Use the short loop if both strings are not 8B aligned */
	or	r6,r3,r4
	andi.	r6,r6,7

	/* Use the short loop if length is less than 32B */
	cmpdi	cr6,r5,31

	beq	cr1,.Lzero
	bne	.Lshort
	bgt	cr6,.Llong

.Lshort:
	mtctr	r5

1:	lbz	rA,0(r3)
	lbz	rB,0(r4)
	subf.	rC,rB,rA
	bne	.Lnon_zero
	bdz	.Lzero

	lbz	rA,1(r3)
	lbz	rB,1(r4)
	subf.	rC,rB,rA
	bne	.Lnon_zero
	bdz	.Lzero

	lbz	rA,2(r3)
	lbz	rB,2(r4)
	subf.	rC,rB,rA
	bne	.Lnon_zero
	bdz	.Lzero

	lbz	rA,3(r3)
	lbz	rB,3(r4)
	subf.	rC,rB,rA
	bne	.Lnon_zero

	addi	r3,r3,4
	addi	r4,r4,4

	bdnz	1b

.Lzero:
	li	r3,0
	blr

.Lnon_zero:
	mr	r3,rC
	blr

.Llong:
	li	off8,8
	li	off16,16
	li	off24,24

	std	r31,-8(r1)
	std	r30,-16(r1)
	std	r29,-24(r1)
	std	r28,-32(r1)
	std	r27,-40(r1)

	srdi	r0,r5,5
	mtctr	r0
	andi.	r5,r5,31

	LD	rA,0,r3
	LD	rB,0,r4

	LD	rC,off8,r3
	LD	rD,off8,r4

	LD	rE,off16,r3
	LD	rF,off16,r4

	LD	rG,off24,r3
	LD	rH,off24,r4
	cmpld	cr0,rA,rB

	addi	r3,r3,32
	addi	r4,r4,32

	bdz	.Lfirst32

	LD	rA,0,r3
	LD	rB,0,r4
	cmpld	cr1,rC,rD

	LD	rC,off8,r3
	LD	rD,off8,r4
	cmpld	cr6,rE,rF

	LD	rE,off16,r3
	LD	rF,off16,r4
	cmpld	cr7,rG,rH
	bne	cr0,.LcmpAB

	LD	rG,off24,r3
	LD	rH,off24,r4
	cmpld	cr0,rA,rB
	bne	cr1,.LcmpCD

	addi	r3,r3,32
	addi	r4,r4,32

	bdz	.Lsecond32

	.balign	16

1:	LD	rA,0,r3
	LD	rB,0,r4
	cmpld	cr1,rC,rD
	bne	cr6,.LcmpEF

	LD	rC,off8,r3
	LD	rD,off8,r4
	cmpld	cr6,rE,rF
	bne	cr7,.LcmpGH

	LD	rE,off16,r3
	LD	rF,off16,r4
	cmpld	cr7,rG,rH
	bne	cr0,.LcmpAB

	LD	rG,off24,r3
	LD	rH,off24,r4
	cmpld	cr0,rA,rB
	bne	cr1,.LcmpCD

	addi	r3,r3,32
	addi	r4,r4,32

	bdnz	1b

.Lsecond32:
	cmpld	cr1,rC,rD
	bne	cr6,.LcmpEF

	cmpld	cr6,rE,rF
	bne	cr7,.LcmpGH

	cmpld	cr7,rG,rH
	bne	cr0,.LcmpAB

	bne	cr1,.LcmpCD
	bne	cr6,.LcmpEF
	bne	cr7,.LcmpGH

.Ltail:
	ld	r31,-8(r1)
	ld	r30,-16(r1)
	ld	r29,-24(r1)
	ld	r28,-32(r1)
	ld	r27,-40(r1)

	cmpdi	r5,0
	beq	.Lzero
	b	.Lshort

.Lfirst32:
	cmpld	cr1,rC,rD
	cmpld	cr6,rE,rF
	cmpld	cr7,rG,rH

	bne	cr0,.LcmpAB
	bne	cr1,.LcmpCD
	bne	cr6,.LcmpEF
	bne	cr7,.LcmpGH

	b	.Ltail

.LcmpAB:
	li	r3,1
	bgt	cr0,.Lout
	li	r3,-1
	b	.Lout

.LcmpCD:
	li	r3,1
	bgt	cr1,.Lout
	li	r3,-1
	b	.Lout

.LcmpEF:
	li	r3,1
	bgt	cr6,.Lout
	li	r3,-1
	b	.Lout

.LcmpGH:
	li	r3,1
	bgt	cr7,.Lout
	li	r3,-1

.Lout:
	ld	r31,-8(r1)
	ld	r30,-16(r1)
	ld	r29,-24(r1)
	ld	r28,-32(r1)
	ld	r27,-40(r1)
	blr
EXPORT_SYMBOL(memcmp)

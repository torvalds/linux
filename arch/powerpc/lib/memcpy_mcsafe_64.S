/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) IBM Corporation, 2011
 * Derived from copyuser_power7.s by Anton Blanchard <anton@au.ibm.com>
 * Author - Balbir Singh <bsingharora@gmail.com>
 */
#include <asm/ppc_asm.h>
#include <asm/errno.h>
#include <asm/export.h>

	.macro err1
100:
	EX_TABLE(100b,.Ldo_err1)
	.endm

	.macro err2
200:
	EX_TABLE(200b,.Ldo_err2)
	.endm

	.macro err3
300:	EX_TABLE(300b,.Ldone)
	.endm

.Ldo_err2:
	ld	r22,STK_REG(R22)(r1)
	ld	r21,STK_REG(R21)(r1)
	ld	r20,STK_REG(R20)(r1)
	ld	r19,STK_REG(R19)(r1)
	ld	r18,STK_REG(R18)(r1)
	ld	r17,STK_REG(R17)(r1)
	ld	r16,STK_REG(R16)(r1)
	ld	r15,STK_REG(R15)(r1)
	ld	r14,STK_REG(R14)(r1)
	addi	r1,r1,STACKFRAMESIZE
.Ldo_err1:
	/* Do a byte by byte copy to get the exact remaining size */
	mtctr	r7
46:
err3;	lbz	r0,0(r4)
	addi	r4,r4,1
err3;	stb	r0,0(r3)
	addi	r3,r3,1
	bdnz	46b
	li	r3,0
	blr

.Ldone:
	mfctr	r3
	blr


_GLOBAL(memcpy_mcsafe)
	mr	r7,r5
	cmpldi	r5,16
	blt	.Lshort_copy

.Lcopy:
	/* Get the source 8B aligned */
	neg	r6,r4
	mtocrf	0x01,r6
	clrldi	r6,r6,(64-3)

	bf	cr7*4+3,1f
err1;	lbz	r0,0(r4)
	addi	r4,r4,1
err1;	stb	r0,0(r3)
	addi	r3,r3,1
	subi	r7,r7,1

1:	bf	cr7*4+2,2f
err1;	lhz	r0,0(r4)
	addi	r4,r4,2
err1;	sth	r0,0(r3)
	addi	r3,r3,2
	subi	r7,r7,2

2:	bf	cr7*4+1,3f
err1;	lwz	r0,0(r4)
	addi	r4,r4,4
err1;	stw	r0,0(r3)
	addi	r3,r3,4
	subi	r7,r7,4

3:	sub	r5,r5,r6
	cmpldi	r5,128

	mflr	r0
	stdu	r1,-STACKFRAMESIZE(r1)
	std	r14,STK_REG(R14)(r1)
	std	r15,STK_REG(R15)(r1)
	std	r16,STK_REG(R16)(r1)
	std	r17,STK_REG(R17)(r1)
	std	r18,STK_REG(R18)(r1)
	std	r19,STK_REG(R19)(r1)
	std	r20,STK_REG(R20)(r1)
	std	r21,STK_REG(R21)(r1)
	std	r22,STK_REG(R22)(r1)
	std	r0,STACKFRAMESIZE+16(r1)

	blt	5f
	srdi	r6,r5,7
	mtctr	r6

	/* Now do cacheline (128B) sized loads and stores. */
	.align	5
4:
err2;	ld	r0,0(r4)
err2;	ld	r6,8(r4)
err2;	ld	r8,16(r4)
err2;	ld	r9,24(r4)
err2;	ld	r10,32(r4)
err2;	ld	r11,40(r4)
err2;	ld	r12,48(r4)
err2;	ld	r14,56(r4)
err2;	ld	r15,64(r4)
err2;	ld	r16,72(r4)
err2;	ld	r17,80(r4)
err2;	ld	r18,88(r4)
err2;	ld	r19,96(r4)
err2;	ld	r20,104(r4)
err2;	ld	r21,112(r4)
err2;	ld	r22,120(r4)
	addi	r4,r4,128
err2;	std	r0,0(r3)
err2;	std	r6,8(r3)
err2;	std	r8,16(r3)
err2;	std	r9,24(r3)
err2;	std	r10,32(r3)
err2;	std	r11,40(r3)
err2;	std	r12,48(r3)
err2;	std	r14,56(r3)
err2;	std	r15,64(r3)
err2;	std	r16,72(r3)
err2;	std	r17,80(r3)
err2;	std	r18,88(r3)
err2;	std	r19,96(r3)
err2;	std	r20,104(r3)
err2;	std	r21,112(r3)
err2;	std	r22,120(r3)
	addi	r3,r3,128
	subi	r7,r7,128
	bdnz	4b

	clrldi	r5,r5,(64-7)

	/* Up to 127B to go */
5:	srdi	r6,r5,4
	mtocrf	0x01,r6

6:	bf	cr7*4+1,7f
err2;	ld	r0,0(r4)
err2;	ld	r6,8(r4)
err2;	ld	r8,16(r4)
err2;	ld	r9,24(r4)
err2;	ld	r10,32(r4)
err2;	ld	r11,40(r4)
err2;	ld	r12,48(r4)
err2;	ld	r14,56(r4)
	addi	r4,r4,64
err2;	std	r0,0(r3)
err2;	std	r6,8(r3)
err2;	std	r8,16(r3)
err2;	std	r9,24(r3)
err2;	std	r10,32(r3)
err2;	std	r11,40(r3)
err2;	std	r12,48(r3)
err2;	std	r14,56(r3)
	addi	r3,r3,64
	subi	r7,r7,64

7:	ld	r14,STK_REG(R14)(r1)
	ld	r15,STK_REG(R15)(r1)
	ld	r16,STK_REG(R16)(r1)
	ld	r17,STK_REG(R17)(r1)
	ld	r18,STK_REG(R18)(r1)
	ld	r19,STK_REG(R19)(r1)
	ld	r20,STK_REG(R20)(r1)
	ld	r21,STK_REG(R21)(r1)
	ld	r22,STK_REG(R22)(r1)
	addi	r1,r1,STACKFRAMESIZE

	/* Up to 63B to go */
	bf	cr7*4+2,8f
err1;	ld	r0,0(r4)
err1;	ld	r6,8(r4)
err1;	ld	r8,16(r4)
err1;	ld	r9,24(r4)
	addi	r4,r4,32
err1;	std	r0,0(r3)
err1;	std	r6,8(r3)
err1;	std	r8,16(r3)
err1;	std	r9,24(r3)
	addi	r3,r3,32
	subi	r7,r7,32

	/* Up to 31B to go */
8:	bf	cr7*4+3,9f
err1;	ld	r0,0(r4)
err1;	ld	r6,8(r4)
	addi	r4,r4,16
err1;	std	r0,0(r3)
err1;	std	r6,8(r3)
	addi	r3,r3,16
	subi	r7,r7,16

9:	clrldi	r5,r5,(64-4)

	/* Up to 15B to go */
.Lshort_copy:
	mtocrf	0x01,r5
	bf	cr7*4+0,12f
err1;	lwz	r0,0(r4)	/* Less chance of a reject with word ops */
err1;	lwz	r6,4(r4)
	addi	r4,r4,8
err1;	stw	r0,0(r3)
err1;	stw	r6,4(r3)
	addi	r3,r3,8
	subi	r7,r7,8

12:	bf	cr7*4+1,13f
err1;	lwz	r0,0(r4)
	addi	r4,r4,4
err1;	stw	r0,0(r3)
	addi	r3,r3,4
	subi	r7,r7,4

13:	bf	cr7*4+2,14f
err1;	lhz	r0,0(r4)
	addi	r4,r4,2
err1;	sth	r0,0(r3)
	addi	r3,r3,2
	subi	r7,r7,2

14:	bf	cr7*4+3,15f
err1;	lbz	r0,0(r4)
err1;	stb	r0,0(r3)

15:	li	r3,0
	blr

EXPORT_SYMBOL_GPL(memcpy_mcsafe);

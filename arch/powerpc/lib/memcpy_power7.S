/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *
 * Copyright (C) IBM Corporation, 2012
 *
 * Author: Anton Blanchard <anton@au.ibm.com>
 */
#include <asm/ppc_asm.h>

#ifndef SELFTEST_CASE
/* 0 == don't use VMX, 1 == use VMX */
#define SELFTEST_CASE	0
#endif

#ifdef __BIG_ENDIAN__
#define LVS(VRT,RA,RB)		lvsl	VRT,RA,RB
#define VPERM(VRT,VRA,VRB,VRC)	vperm	VRT,VRA,VRB,VRC
#else
#define LVS(VRT,RA,RB)		lvsr	VRT,RA,RB
#define VPERM(VRT,VRA,VRB,VRC)	vperm	VRT,VRB,VRA,VRC
#endif

_GLOBAL(memcpy_power7)
	cmpldi	r5,16
	cmpldi	cr1,r5,4096
	std	r3,-STACKFRAMESIZE+STK_REG(R31)(r1)
	blt	.Lshort_copy

#ifdef CONFIG_ALTIVEC
test_feature = SELFTEST_CASE
BEGIN_FTR_SECTION
	bgt	cr1, .Lvmx_copy
END_FTR_SECTION_IFSET(CPU_FTR_ALTIVEC)
#endif

.Lnonvmx_copy:
	/* Get the source 8B aligned */
	neg	r6,r4
	mtocrf	0x01,r6
	clrldi	r6,r6,(64-3)

	bf	cr7*4+3,1f
	lbz	r0,0(r4)
	addi	r4,r4,1
	stb	r0,0(r3)
	addi	r3,r3,1

1:	bf	cr7*4+2,2f
	lhz	r0,0(r4)
	addi	r4,r4,2
	sth	r0,0(r3)
	addi	r3,r3,2

2:	bf	cr7*4+1,3f
	lwz	r0,0(r4)
	addi	r4,r4,4
	stw	r0,0(r3)
	addi	r3,r3,4

3:	sub	r5,r5,r6
	cmpldi	r5,128
	blt	5f

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

	srdi	r6,r5,7
	mtctr	r6

	/* Now do cacheline (128B) sized loads and stores. */
	.align	5
4:
	ld	r0,0(r4)
	ld	r6,8(r4)
	ld	r7,16(r4)
	ld	r8,24(r4)
	ld	r9,32(r4)
	ld	r10,40(r4)
	ld	r11,48(r4)
	ld	r12,56(r4)
	ld	r14,64(r4)
	ld	r15,72(r4)
	ld	r16,80(r4)
	ld	r17,88(r4)
	ld	r18,96(r4)
	ld	r19,104(r4)
	ld	r20,112(r4)
	ld	r21,120(r4)
	addi	r4,r4,128
	std	r0,0(r3)
	std	r6,8(r3)
	std	r7,16(r3)
	std	r8,24(r3)
	std	r9,32(r3)
	std	r10,40(r3)
	std	r11,48(r3)
	std	r12,56(r3)
	std	r14,64(r3)
	std	r15,72(r3)
	std	r16,80(r3)
	std	r17,88(r3)
	std	r18,96(r3)
	std	r19,104(r3)
	std	r20,112(r3)
	std	r21,120(r3)
	addi	r3,r3,128
	bdnz	4b

	clrldi	r5,r5,(64-7)

	ld	r14,STK_REG(R14)(r1)
	ld	r15,STK_REG(R15)(r1)
	ld	r16,STK_REG(R16)(r1)
	ld	r17,STK_REG(R17)(r1)
	ld	r18,STK_REG(R18)(r1)
	ld	r19,STK_REG(R19)(r1)
	ld	r20,STK_REG(R20)(r1)
	ld	r21,STK_REG(R21)(r1)
	ld	r22,STK_REG(R22)(r1)
	addi	r1,r1,STACKFRAMESIZE

	/* Up to 127B to go */
5:	srdi	r6,r5,4
	mtocrf	0x01,r6

6:	bf	cr7*4+1,7f
	ld	r0,0(r4)
	ld	r6,8(r4)
	ld	r7,16(r4)
	ld	r8,24(r4)
	ld	r9,32(r4)
	ld	r10,40(r4)
	ld	r11,48(r4)
	ld	r12,56(r4)
	addi	r4,r4,64
	std	r0,0(r3)
	std	r6,8(r3)
	std	r7,16(r3)
	std	r8,24(r3)
	std	r9,32(r3)
	std	r10,40(r3)
	std	r11,48(r3)
	std	r12,56(r3)
	addi	r3,r3,64

	/* Up to 63B to go */
7:	bf	cr7*4+2,8f
	ld	r0,0(r4)
	ld	r6,8(r4)
	ld	r7,16(r4)
	ld	r8,24(r4)
	addi	r4,r4,32
	std	r0,0(r3)
	std	r6,8(r3)
	std	r7,16(r3)
	std	r8,24(r3)
	addi	r3,r3,32

	/* Up to 31B to go */
8:	bf	cr7*4+3,9f
	ld	r0,0(r4)
	ld	r6,8(r4)
	addi	r4,r4,16
	std	r0,0(r3)
	std	r6,8(r3)
	addi	r3,r3,16

9:	clrldi	r5,r5,(64-4)

	/* Up to 15B to go */
.Lshort_copy:
	mtocrf	0x01,r5
	bf	cr7*4+0,12f
	lwz	r0,0(r4)	/* Less chance of a reject with word ops */
	lwz	r6,4(r4)
	addi	r4,r4,8
	stw	r0,0(r3)
	stw	r6,4(r3)
	addi	r3,r3,8

12:	bf	cr7*4+1,13f
	lwz	r0,0(r4)
	addi	r4,r4,4
	stw	r0,0(r3)
	addi	r3,r3,4

13:	bf	cr7*4+2,14f
	lhz	r0,0(r4)
	addi	r4,r4,2
	sth	r0,0(r3)
	addi	r3,r3,2

14:	bf	cr7*4+3,15f
	lbz	r0,0(r4)
	stb	r0,0(r3)

15:	ld	r3,-STACKFRAMESIZE+STK_REG(R31)(r1)
	blr

.Lunwind_stack_nonvmx_copy:
	addi	r1,r1,STACKFRAMESIZE
	b	.Lnonvmx_copy

.Lvmx_copy:
#ifdef CONFIG_ALTIVEC
	mflr	r0
	std	r4,-STACKFRAMESIZE+STK_REG(R30)(r1)
	std	r5,-STACKFRAMESIZE+STK_REG(R29)(r1)
	std	r0,16(r1)
	stdu	r1,-STACKFRAMESIZE(r1)
	bl	enter_vmx_ops
	cmpwi	cr1,r3,0
	ld	r0,STACKFRAMESIZE+16(r1)
	ld	r3,STK_REG(R31)(r1)
	ld	r4,STK_REG(R30)(r1)
	ld	r5,STK_REG(R29)(r1)
	mtlr	r0

	/*
	 * We prefetch both the source and destination using enhanced touch
	 * instructions. We use a stream ID of 0 for the load side and
	 * 1 for the store side.
	 */
	clrrdi	r6,r4,7
	clrrdi	r9,r3,7
	ori	r9,r9,1		/* stream=1 */

	srdi	r7,r5,7		/* length in cachelines, capped at 0x3FF */
	cmpldi	r7,0x3FF
	ble	1f
	li	r7,0x3FF
1:	lis	r0,0x0E00	/* depth=7 */
	sldi	r7,r7,7
	or	r7,r7,r0
	ori	r10,r7,1	/* stream=1 */

	lis	r8,0x8000	/* GO=1 */
	clrldi	r8,r8,32

	dcbt	0,r6,0b01000
	dcbt	0,r7,0b01010
	dcbtst	0,r9,0b01000
	dcbtst	0,r10,0b01010
	eieio
	dcbt	0,r8,0b01010	/* GO */

	beq	cr1,.Lunwind_stack_nonvmx_copy

	/*
	 * If source and destination are not relatively aligned we use a
	 * slower permute loop.
	 */
	xor	r6,r4,r3
	rldicl.	r6,r6,0,(64-4)
	bne	.Lvmx_unaligned_copy

	/* Get the destination 16B aligned */
	neg	r6,r3
	mtocrf	0x01,r6
	clrldi	r6,r6,(64-4)

	bf	cr7*4+3,1f
	lbz	r0,0(r4)
	addi	r4,r4,1
	stb	r0,0(r3)
	addi	r3,r3,1

1:	bf	cr7*4+2,2f
	lhz	r0,0(r4)
	addi	r4,r4,2
	sth	r0,0(r3)
	addi	r3,r3,2

2:	bf	cr7*4+1,3f
	lwz	r0,0(r4)
	addi	r4,r4,4
	stw	r0,0(r3)
	addi	r3,r3,4

3:	bf	cr7*4+0,4f
	ld	r0,0(r4)
	addi	r4,r4,8
	std	r0,0(r3)
	addi	r3,r3,8

4:	sub	r5,r5,r6

	/* Get the desination 128B aligned */
	neg	r6,r3
	srdi	r7,r6,4
	mtocrf	0x01,r7
	clrldi	r6,r6,(64-7)

	li	r9,16
	li	r10,32
	li	r11,48

	bf	cr7*4+3,5f
	lvx	v1,0,r4
	addi	r4,r4,16
	stvx	v1,0,r3
	addi	r3,r3,16

5:	bf	cr7*4+2,6f
	lvx	v1,0,r4
	lvx	v0,r4,r9
	addi	r4,r4,32
	stvx	v1,0,r3
	stvx	v0,r3,r9
	addi	r3,r3,32

6:	bf	cr7*4+1,7f
	lvx	v3,0,r4
	lvx	v2,r4,r9
	lvx	v1,r4,r10
	lvx	v0,r4,r11
	addi	r4,r4,64
	stvx	v3,0,r3
	stvx	v2,r3,r9
	stvx	v1,r3,r10
	stvx	v0,r3,r11
	addi	r3,r3,64

7:	sub	r5,r5,r6
	srdi	r6,r5,7

	std	r14,STK_REG(R14)(r1)
	std	r15,STK_REG(R15)(r1)
	std	r16,STK_REG(R16)(r1)

	li	r12,64
	li	r14,80
	li	r15,96
	li	r16,112

	mtctr	r6

	/*
	 * Now do cacheline sized loads and stores. By this stage the
	 * cacheline stores are also cacheline aligned.
	 */
	.align	5
8:
	lvx	v7,0,r4
	lvx	v6,r4,r9
	lvx	v5,r4,r10
	lvx	v4,r4,r11
	lvx	v3,r4,r12
	lvx	v2,r4,r14
	lvx	v1,r4,r15
	lvx	v0,r4,r16
	addi	r4,r4,128
	stvx	v7,0,r3
	stvx	v6,r3,r9
	stvx	v5,r3,r10
	stvx	v4,r3,r11
	stvx	v3,r3,r12
	stvx	v2,r3,r14
	stvx	v1,r3,r15
	stvx	v0,r3,r16
	addi	r3,r3,128
	bdnz	8b

	ld	r14,STK_REG(R14)(r1)
	ld	r15,STK_REG(R15)(r1)
	ld	r16,STK_REG(R16)(r1)

	/* Up to 127B to go */
	clrldi	r5,r5,(64-7)
	srdi	r6,r5,4
	mtocrf	0x01,r6

	bf	cr7*4+1,9f
	lvx	v3,0,r4
	lvx	v2,r4,r9
	lvx	v1,r4,r10
	lvx	v0,r4,r11
	addi	r4,r4,64
	stvx	v3,0,r3
	stvx	v2,r3,r9
	stvx	v1,r3,r10
	stvx	v0,r3,r11
	addi	r3,r3,64

9:	bf	cr7*4+2,10f
	lvx	v1,0,r4
	lvx	v0,r4,r9
	addi	r4,r4,32
	stvx	v1,0,r3
	stvx	v0,r3,r9
	addi	r3,r3,32

10:	bf	cr7*4+3,11f
	lvx	v1,0,r4
	addi	r4,r4,16
	stvx	v1,0,r3
	addi	r3,r3,16

	/* Up to 15B to go */
11:	clrldi	r5,r5,(64-4)
	mtocrf	0x01,r5
	bf	cr7*4+0,12f
	ld	r0,0(r4)
	addi	r4,r4,8
	std	r0,0(r3)
	addi	r3,r3,8

12:	bf	cr7*4+1,13f
	lwz	r0,0(r4)
	addi	r4,r4,4
	stw	r0,0(r3)
	addi	r3,r3,4

13:	bf	cr7*4+2,14f
	lhz	r0,0(r4)
	addi	r4,r4,2
	sth	r0,0(r3)
	addi	r3,r3,2

14:	bf	cr7*4+3,15f
	lbz	r0,0(r4)
	stb	r0,0(r3)

15:	addi	r1,r1,STACKFRAMESIZE
	ld	r3,-STACKFRAMESIZE+STK_REG(R31)(r1)
	b	exit_vmx_ops		/* tail call optimise */

.Lvmx_unaligned_copy:
	/* Get the destination 16B aligned */
	neg	r6,r3
	mtocrf	0x01,r6
	clrldi	r6,r6,(64-4)

	bf	cr7*4+3,1f
	lbz	r0,0(r4)
	addi	r4,r4,1
	stb	r0,0(r3)
	addi	r3,r3,1

1:	bf	cr7*4+2,2f
	lhz	r0,0(r4)
	addi	r4,r4,2
	sth	r0,0(r3)
	addi	r3,r3,2

2:	bf	cr7*4+1,3f
	lwz	r0,0(r4)
	addi	r4,r4,4
	stw	r0,0(r3)
	addi	r3,r3,4

3:	bf	cr7*4+0,4f
	lwz	r0,0(r4)	/* Less chance of a reject with word ops */
	lwz	r7,4(r4)
	addi	r4,r4,8
	stw	r0,0(r3)
	stw	r7,4(r3)
	addi	r3,r3,8

4:	sub	r5,r5,r6

	/* Get the desination 128B aligned */
	neg	r6,r3
	srdi	r7,r6,4
	mtocrf	0x01,r7
	clrldi	r6,r6,(64-7)

	li	r9,16
	li	r10,32
	li	r11,48

	LVS(v16,0,r4)		/* Setup permute control vector */
	lvx	v0,0,r4
	addi	r4,r4,16

	bf	cr7*4+3,5f
	lvx	v1,0,r4
	VPERM(v8,v0,v1,v16)
	addi	r4,r4,16
	stvx	v8,0,r3
	addi	r3,r3,16
	vor	v0,v1,v1

5:	bf	cr7*4+2,6f
	lvx	v1,0,r4
	VPERM(v8,v0,v1,v16)
	lvx	v0,r4,r9
	VPERM(v9,v1,v0,v16)
	addi	r4,r4,32
	stvx	v8,0,r3
	stvx	v9,r3,r9
	addi	r3,r3,32

6:	bf	cr7*4+1,7f
	lvx	v3,0,r4
	VPERM(v8,v0,v3,v16)
	lvx	v2,r4,r9
	VPERM(v9,v3,v2,v16)
	lvx	v1,r4,r10
	VPERM(v10,v2,v1,v16)
	lvx	v0,r4,r11
	VPERM(v11,v1,v0,v16)
	addi	r4,r4,64
	stvx	v8,0,r3
	stvx	v9,r3,r9
	stvx	v10,r3,r10
	stvx	v11,r3,r11
	addi	r3,r3,64

7:	sub	r5,r5,r6
	srdi	r6,r5,7

	std	r14,STK_REG(R14)(r1)
	std	r15,STK_REG(R15)(r1)
	std	r16,STK_REG(R16)(r1)

	li	r12,64
	li	r14,80
	li	r15,96
	li	r16,112

	mtctr	r6

	/*
	 * Now do cacheline sized loads and stores. By this stage the
	 * cacheline stores are also cacheline aligned.
	 */
	.align	5
8:
	lvx	v7,0,r4
	VPERM(v8,v0,v7,v16)
	lvx	v6,r4,r9
	VPERM(v9,v7,v6,v16)
	lvx	v5,r4,r10
	VPERM(v10,v6,v5,v16)
	lvx	v4,r4,r11
	VPERM(v11,v5,v4,v16)
	lvx	v3,r4,r12
	VPERM(v12,v4,v3,v16)
	lvx	v2,r4,r14
	VPERM(v13,v3,v2,v16)
	lvx	v1,r4,r15
	VPERM(v14,v2,v1,v16)
	lvx	v0,r4,r16
	VPERM(v15,v1,v0,v16)
	addi	r4,r4,128
	stvx	v8,0,r3
	stvx	v9,r3,r9
	stvx	v10,r3,r10
	stvx	v11,r3,r11
	stvx	v12,r3,r12
	stvx	v13,r3,r14
	stvx	v14,r3,r15
	stvx	v15,r3,r16
	addi	r3,r3,128
	bdnz	8b

	ld	r14,STK_REG(R14)(r1)
	ld	r15,STK_REG(R15)(r1)
	ld	r16,STK_REG(R16)(r1)

	/* Up to 127B to go */
	clrldi	r5,r5,(64-7)
	srdi	r6,r5,4
	mtocrf	0x01,r6

	bf	cr7*4+1,9f
	lvx	v3,0,r4
	VPERM(v8,v0,v3,v16)
	lvx	v2,r4,r9
	VPERM(v9,v3,v2,v16)
	lvx	v1,r4,r10
	VPERM(v10,v2,v1,v16)
	lvx	v0,r4,r11
	VPERM(v11,v1,v0,v16)
	addi	r4,r4,64
	stvx	v8,0,r3
	stvx	v9,r3,r9
	stvx	v10,r3,r10
	stvx	v11,r3,r11
	addi	r3,r3,64

9:	bf	cr7*4+2,10f
	lvx	v1,0,r4
	VPERM(v8,v0,v1,v16)
	lvx	v0,r4,r9
	VPERM(v9,v1,v0,v16)
	addi	r4,r4,32
	stvx	v8,0,r3
	stvx	v9,r3,r9
	addi	r3,r3,32

10:	bf	cr7*4+3,11f
	lvx	v1,0,r4
	VPERM(v8,v0,v1,v16)
	addi	r4,r4,16
	stvx	v8,0,r3
	addi	r3,r3,16

	/* Up to 15B to go */
11:	clrldi	r5,r5,(64-4)
	addi	r4,r4,-16	/* Unwind the +16 load offset */
	mtocrf	0x01,r5
	bf	cr7*4+0,12f
	lwz	r0,0(r4)	/* Less chance of a reject with word ops */
	lwz	r6,4(r4)
	addi	r4,r4,8
	stw	r0,0(r3)
	stw	r6,4(r3)
	addi	r3,r3,8

12:	bf	cr7*4+1,13f
	lwz	r0,0(r4)
	addi	r4,r4,4
	stw	r0,0(r3)
	addi	r3,r3,4

13:	bf	cr7*4+2,14f
	lhz	r0,0(r4)
	addi	r4,r4,2
	sth	r0,0(r3)
	addi	r3,r3,2

14:	bf	cr7*4+3,15f
	lbz	r0,0(r4)
	stb	r0,0(r3)

15:	addi	r1,r1,STACKFRAMESIZE
	ld	r3,-STACKFRAMESIZE+STK_REG(R31)(r1)
	b	exit_vmx_ops		/* tail call optimise */
#endif /* CONFIG_ALTIVEC */

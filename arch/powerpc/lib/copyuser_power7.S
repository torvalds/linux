/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * Copyright (C) IBM Corporation, 2011
 *
 * Author: Anton Blanchard <anton@au.ibm.com>
 */
#include <asm/ppc_asm.h>

	.macro err1
100:
	.section __ex_table,"a"
	.align 3
	.llong 100b,.Ldo_err1
	.previous
	.endm

	.macro err2
200:
	.section __ex_table,"a"
	.align 3
	.llong 200b,.Ldo_err2
	.previous
	.endm

#ifdef CONFIG_ALTIVEC
	.macro err3
300:
	.section __ex_table,"a"
	.align 3
	.llong 300b,.Ldo_err3
	.previous
	.endm

	.macro err4
400:
	.section __ex_table,"a"
	.align 3
	.llong 400b,.Ldo_err4
	.previous
	.endm


.Ldo_err4:
	ld	r16,STK_REG(R16)(r1)
	ld	r15,STK_REG(R15)(r1)
	ld	r14,STK_REG(R14)(r1)
.Ldo_err3:
	bl	.exit_vmx_usercopy
	ld	r0,STACKFRAMESIZE+16(r1)
	mtlr	r0
	b	.Lexit
#endif /* CONFIG_ALTIVEC */

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
.Lexit:
	addi	r1,r1,STACKFRAMESIZE
.Ldo_err1:
	ld	r3,48(r1)
	ld	r4,56(r1)
	ld	r5,64(r1)
	b	__copy_tofrom_user_base


_GLOBAL(__copy_tofrom_user_power7)
#ifdef CONFIG_ALTIVEC
	cmpldi	r5,16
	cmpldi	cr1,r5,4096

	std	r3,48(r1)
	std	r4,56(r1)
	std	r5,64(r1)

	blt	.Lshort_copy
	bgt	cr1,.Lvmx_copy
#else
	cmpldi	r5,16

	std	r3,48(r1)
	std	r4,56(r1)
	std	r5,64(r1)

	blt	.Lshort_copy
#endif

.Lnonvmx_copy:
	/* Get the source 8B aligned */
	neg	r6,r4
	mtocrf	0x01,r6
	clrldi	r6,r6,(64-3)

	bf	cr7*4+3,1f
err1;	lbz	r0,0(r4)
	addi	r4,r4,1
err1;	stb	r0,0(r3)
	addi	r3,r3,1

1:	bf	cr7*4+2,2f
err1;	lhz	r0,0(r4)
	addi	r4,r4,2
err1;	sth	r0,0(r3)
	addi	r3,r3,2

2:	bf	cr7*4+1,3f
err1;	lwz	r0,0(r4)
	addi	r4,r4,4
err1;	stw	r0,0(r3)
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
err2;	ld	r0,0(r4)
err2;	ld	r6,8(r4)
err2;	ld	r7,16(r4)
err2;	ld	r8,24(r4)
err2;	ld	r9,32(r4)
err2;	ld	r10,40(r4)
err2;	ld	r11,48(r4)
err2;	ld	r12,56(r4)
err2;	ld	r14,64(r4)
err2;	ld	r15,72(r4)
err2;	ld	r16,80(r4)
err2;	ld	r17,88(r4)
err2;	ld	r18,96(r4)
err2;	ld	r19,104(r4)
err2;	ld	r20,112(r4)
err2;	ld	r21,120(r4)
	addi	r4,r4,128
err2;	std	r0,0(r3)
err2;	std	r6,8(r3)
err2;	std	r7,16(r3)
err2;	std	r8,24(r3)
err2;	std	r9,32(r3)
err2;	std	r10,40(r3)
err2;	std	r11,48(r3)
err2;	std	r12,56(r3)
err2;	std	r14,64(r3)
err2;	std	r15,72(r3)
err2;	std	r16,80(r3)
err2;	std	r17,88(r3)
err2;	std	r18,96(r3)
err2;	std	r19,104(r3)
err2;	std	r20,112(r3)
err2;	std	r21,120(r3)
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
err1;	ld	r0,0(r4)
err1;	ld	r6,8(r4)
err1;	ld	r7,16(r4)
err1;	ld	r8,24(r4)
err1;	ld	r9,32(r4)
err1;	ld	r10,40(r4)
err1;	ld	r11,48(r4)
err1;	ld	r12,56(r4)
	addi	r4,r4,64
err1;	std	r0,0(r3)
err1;	std	r6,8(r3)
err1;	std	r7,16(r3)
err1;	std	r8,24(r3)
err1;	std	r9,32(r3)
err1;	std	r10,40(r3)
err1;	std	r11,48(r3)
err1;	std	r12,56(r3)
	addi	r3,r3,64

	/* Up to 63B to go */
7:	bf	cr7*4+2,8f
err1;	ld	r0,0(r4)
err1;	ld	r6,8(r4)
err1;	ld	r7,16(r4)
err1;	ld	r8,24(r4)
	addi	r4,r4,32
err1;	std	r0,0(r3)
err1;	std	r6,8(r3)
err1;	std	r7,16(r3)
err1;	std	r8,24(r3)
	addi	r3,r3,32

	/* Up to 31B to go */
8:	bf	cr7*4+3,9f
err1;	ld	r0,0(r4)
err1;	ld	r6,8(r4)
	addi	r4,r4,16
err1;	std	r0,0(r3)
err1;	std	r6,8(r3)
	addi	r3,r3,16

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

12:	bf	cr7*4+1,13f
err1;	lwz	r0,0(r4)
	addi	r4,r4,4
err1;	stw	r0,0(r3)
	addi	r3,r3,4

13:	bf	cr7*4+2,14f
err1;	lhz	r0,0(r4)
	addi	r4,r4,2
err1;	sth	r0,0(r3)
	addi	r3,r3,2

14:	bf	cr7*4+3,15f
err1;	lbz	r0,0(r4)
err1;	stb	r0,0(r3)

15:	li	r3,0
	blr

.Lunwind_stack_nonvmx_copy:
	addi	r1,r1,STACKFRAMESIZE
	b	.Lnonvmx_copy

#ifdef CONFIG_ALTIVEC
.Lvmx_copy:
	mflr	r0
	std	r0,16(r1)
	stdu	r1,-STACKFRAMESIZE(r1)
	bl	.enter_vmx_usercopy
	cmpwi	r3,0
	ld	r0,STACKFRAMESIZE+16(r1)
	ld	r3,STACKFRAMESIZE+48(r1)
	ld	r4,STACKFRAMESIZE+56(r1)
	ld	r5,STACKFRAMESIZE+64(r1)
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

.machine push
.machine "power4"
	dcbt	r0,r6,0b01000
	dcbt	r0,r7,0b01010
	dcbtst	r0,r9,0b01000
	dcbtst	r0,r10,0b01010
	eieio
	dcbt	r0,r8,0b01010	/* GO */
.machine pop

	/*
	 * We prefetch both the source and destination using enhanced touch
	 * instructions. We use a stream ID of 0 for the load side and
	 * 1 for the store side.
	 */
	clrrdi	r6,r4,7
	clrrdi	r9,r3,7
	ori	r9,r9,1		/* stream=1 */

	srdi	r7,r5,7		/* length in cachelines, capped at 0x3FF */
	cmpldi	cr1,r7,0x3FF
	ble	cr1,1f
	li	r7,0x3FF
1:	lis	r0,0x0E00	/* depth=7 */
	sldi	r7,r7,7
	or	r7,r7,r0
	ori	r10,r7,1	/* stream=1 */

	lis	r8,0x8000	/* GO=1 */
	clrldi	r8,r8,32

.machine push
.machine "power4"
	dcbt	r0,r6,0b01000
	dcbt	r0,r7,0b01010
	dcbtst	r0,r9,0b01000
	dcbtst	r0,r10,0b01010
	eieio
	dcbt	r0,r8,0b01010	/* GO */
.machine pop

	beq	.Lunwind_stack_nonvmx_copy

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
err3;	lbz	r0,0(r4)
	addi	r4,r4,1
err3;	stb	r0,0(r3)
	addi	r3,r3,1

1:	bf	cr7*4+2,2f
err3;	lhz	r0,0(r4)
	addi	r4,r4,2
err3;	sth	r0,0(r3)
	addi	r3,r3,2

2:	bf	cr7*4+1,3f
err3;	lwz	r0,0(r4)
	addi	r4,r4,4
err3;	stw	r0,0(r3)
	addi	r3,r3,4

3:	bf	cr7*4+0,4f
err3;	ld	r0,0(r4)
	addi	r4,r4,8
err3;	std	r0,0(r3)
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
err3;	lvx	vr1,r0,r4
	addi	r4,r4,16
err3;	stvx	vr1,r0,r3
	addi	r3,r3,16

5:	bf	cr7*4+2,6f
err3;	lvx	vr1,r0,r4
err3;	lvx	vr0,r4,r9
	addi	r4,r4,32
err3;	stvx	vr1,r0,r3
err3;	stvx	vr0,r3,r9
	addi	r3,r3,32

6:	bf	cr7*4+1,7f
err3;	lvx	vr3,r0,r4
err3;	lvx	vr2,r4,r9
err3;	lvx	vr1,r4,r10
err3;	lvx	vr0,r4,r11
	addi	r4,r4,64
err3;	stvx	vr3,r0,r3
err3;	stvx	vr2,r3,r9
err3;	stvx	vr1,r3,r10
err3;	stvx	vr0,r3,r11
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
err4;	lvx	vr7,r0,r4
err4;	lvx	vr6,r4,r9
err4;	lvx	vr5,r4,r10
err4;	lvx	vr4,r4,r11
err4;	lvx	vr3,r4,r12
err4;	lvx	vr2,r4,r14
err4;	lvx	vr1,r4,r15
err4;	lvx	vr0,r4,r16
	addi	r4,r4,128
err4;	stvx	vr7,r0,r3
err4;	stvx	vr6,r3,r9
err4;	stvx	vr5,r3,r10
err4;	stvx	vr4,r3,r11
err4;	stvx	vr3,r3,r12
err4;	stvx	vr2,r3,r14
err4;	stvx	vr1,r3,r15
err4;	stvx	vr0,r3,r16
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
err3;	lvx	vr3,r0,r4
err3;	lvx	vr2,r4,r9
err3;	lvx	vr1,r4,r10
err3;	lvx	vr0,r4,r11
	addi	r4,r4,64
err3;	stvx	vr3,r0,r3
err3;	stvx	vr2,r3,r9
err3;	stvx	vr1,r3,r10
err3;	stvx	vr0,r3,r11
	addi	r3,r3,64

9:	bf	cr7*4+2,10f
err3;	lvx	vr1,r0,r4
err3;	lvx	vr0,r4,r9
	addi	r4,r4,32
err3;	stvx	vr1,r0,r3
err3;	stvx	vr0,r3,r9
	addi	r3,r3,32

10:	bf	cr7*4+3,11f
err3;	lvx	vr1,r0,r4
	addi	r4,r4,16
err3;	stvx	vr1,r0,r3
	addi	r3,r3,16

	/* Up to 15B to go */
11:	clrldi	r5,r5,(64-4)
	mtocrf	0x01,r5
	bf	cr7*4+0,12f
err3;	ld	r0,0(r4)
	addi	r4,r4,8
err3;	std	r0,0(r3)
	addi	r3,r3,8

12:	bf	cr7*4+1,13f
err3;	lwz	r0,0(r4)
	addi	r4,r4,4
err3;	stw	r0,0(r3)
	addi	r3,r3,4

13:	bf	cr7*4+2,14f
err3;	lhz	r0,0(r4)
	addi	r4,r4,2
err3;	sth	r0,0(r3)
	addi	r3,r3,2

14:	bf	cr7*4+3,15f
err3;	lbz	r0,0(r4)
err3;	stb	r0,0(r3)

15:	addi	r1,r1,STACKFRAMESIZE
	b	.exit_vmx_usercopy	/* tail call optimise */

.Lvmx_unaligned_copy:
	/* Get the destination 16B aligned */
	neg	r6,r3
	mtocrf	0x01,r6
	clrldi	r6,r6,(64-4)

	bf	cr7*4+3,1f
err3;	lbz	r0,0(r4)
	addi	r4,r4,1
err3;	stb	r0,0(r3)
	addi	r3,r3,1

1:	bf	cr7*4+2,2f
err3;	lhz	r0,0(r4)
	addi	r4,r4,2
err3;	sth	r0,0(r3)
	addi	r3,r3,2

2:	bf	cr7*4+1,3f
err3;	lwz	r0,0(r4)
	addi	r4,r4,4
err3;	stw	r0,0(r3)
	addi	r3,r3,4

3:	bf	cr7*4+0,4f
err3;	lwz	r0,0(r4)	/* Less chance of a reject with word ops */
err3;	lwz	r7,4(r4)
	addi	r4,r4,8
err3;	stw	r0,0(r3)
err3;	stw	r7,4(r3)
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

	lvsl	vr16,0,r4	/* Setup permute control vector */
err3;	lvx	vr0,0,r4
	addi	r4,r4,16

	bf	cr7*4+3,5f
err3;	lvx	vr1,r0,r4
	vperm	vr8,vr0,vr1,vr16
	addi	r4,r4,16
err3;	stvx	vr8,r0,r3
	addi	r3,r3,16
	vor	vr0,vr1,vr1

5:	bf	cr7*4+2,6f
err3;	lvx	vr1,r0,r4
	vperm	vr8,vr0,vr1,vr16
err3;	lvx	vr0,r4,r9
	vperm	vr9,vr1,vr0,vr16
	addi	r4,r4,32
err3;	stvx	vr8,r0,r3
err3;	stvx	vr9,r3,r9
	addi	r3,r3,32

6:	bf	cr7*4+1,7f
err3;	lvx	vr3,r0,r4
	vperm	vr8,vr0,vr3,vr16
err3;	lvx	vr2,r4,r9
	vperm	vr9,vr3,vr2,vr16
err3;	lvx	vr1,r4,r10
	vperm	vr10,vr2,vr1,vr16
err3;	lvx	vr0,r4,r11
	vperm	vr11,vr1,vr0,vr16
	addi	r4,r4,64
err3;	stvx	vr8,r0,r3
err3;	stvx	vr9,r3,r9
err3;	stvx	vr10,r3,r10
err3;	stvx	vr11,r3,r11
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
err4;	lvx	vr7,r0,r4
	vperm	vr8,vr0,vr7,vr16
err4;	lvx	vr6,r4,r9
	vperm	vr9,vr7,vr6,vr16
err4;	lvx	vr5,r4,r10
	vperm	vr10,vr6,vr5,vr16
err4;	lvx	vr4,r4,r11
	vperm	vr11,vr5,vr4,vr16
err4;	lvx	vr3,r4,r12
	vperm	vr12,vr4,vr3,vr16
err4;	lvx	vr2,r4,r14
	vperm	vr13,vr3,vr2,vr16
err4;	lvx	vr1,r4,r15
	vperm	vr14,vr2,vr1,vr16
err4;	lvx	vr0,r4,r16
	vperm	vr15,vr1,vr0,vr16
	addi	r4,r4,128
err4;	stvx	vr8,r0,r3
err4;	stvx	vr9,r3,r9
err4;	stvx	vr10,r3,r10
err4;	stvx	vr11,r3,r11
err4;	stvx	vr12,r3,r12
err4;	stvx	vr13,r3,r14
err4;	stvx	vr14,r3,r15
err4;	stvx	vr15,r3,r16
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
err3;	lvx	vr3,r0,r4
	vperm	vr8,vr0,vr3,vr16
err3;	lvx	vr2,r4,r9
	vperm	vr9,vr3,vr2,vr16
err3;	lvx	vr1,r4,r10
	vperm	vr10,vr2,vr1,vr16
err3;	lvx	vr0,r4,r11
	vperm	vr11,vr1,vr0,vr16
	addi	r4,r4,64
err3;	stvx	vr8,r0,r3
err3;	stvx	vr9,r3,r9
err3;	stvx	vr10,r3,r10
err3;	stvx	vr11,r3,r11
	addi	r3,r3,64

9:	bf	cr7*4+2,10f
err3;	lvx	vr1,r0,r4
	vperm	vr8,vr0,vr1,vr16
err3;	lvx	vr0,r4,r9
	vperm	vr9,vr1,vr0,vr16
	addi	r4,r4,32
err3;	stvx	vr8,r0,r3
err3;	stvx	vr9,r3,r9
	addi	r3,r3,32

10:	bf	cr7*4+3,11f
err3;	lvx	vr1,r0,r4
	vperm	vr8,vr0,vr1,vr16
	addi	r4,r4,16
err3;	stvx	vr8,r0,r3
	addi	r3,r3,16

	/* Up to 15B to go */
11:	clrldi	r5,r5,(64-4)
	addi	r4,r4,-16	/* Unwind the +16 load offset */
	mtocrf	0x01,r5
	bf	cr7*4+0,12f
err3;	lwz	r0,0(r4)	/* Less chance of a reject with word ops */
err3;	lwz	r6,4(r4)
	addi	r4,r4,8
err3;	stw	r0,0(r3)
err3;	stw	r6,4(r3)
	addi	r3,r3,8

12:	bf	cr7*4+1,13f
err3;	lwz	r0,0(r4)
	addi	r4,r4,4
err3;	stw	r0,0(r3)
	addi	r3,r3,4

13:	bf	cr7*4+2,14f
err3;	lhz	r0,0(r4)
	addi	r4,r4,2
err3;	sth	r0,0(r3)
	addi	r3,r3,2

14:	bf	cr7*4+3,15f
err3;	lbz	r0,0(r4)
err3;	stb	r0,0(r3)

15:	addi	r1,r1,STACKFRAMESIZE
	b	.exit_vmx_usercopy	/* tail call optimise */
#endif /* CONFiG_ALTIVEC */

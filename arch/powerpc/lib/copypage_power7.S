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
 * Copyright (C) IBM Corporation, 2012
 *
 * Author: Anton Blanchard <anton@au.ibm.com>
 */
#include <asm/page.h>
#include <asm/ppc_asm.h>

_GLOBAL(copypage_power7)
	/*
	 * We prefetch both the source and destination using enhanced touch
	 * instructions. We use a stream ID of 0 for the load side and
	 * 1 for the store side. Since source and destination are page
	 * aligned we don't need to clear the bottom 7 bits of either
	 * address.
	 */
	ori	r9,r3,1		/* stream=1 => to */

#ifdef CONFIG_PPC_64K_PAGES
	lis	r7,0x0E01	/* depth=7
				 * units/cachelines=512 */
#else
	lis	r7,0x0E00	/* depth=7 */
	ori	r7,r7,0x1000	/* units/cachelines=32 */
#endif
	ori	r10,r7,1	/* stream=1 */

	lis	r8,0x8000	/* GO=1 */
	clrldi	r8,r8,32

.machine push
.machine "power4"
	/* setup read stream 0  */
	dcbt	0,r4,0b01000  	/* addr from */
	dcbt	0,r7,0b01010   /* length and depth from */
	/* setup write stream 1 */
	dcbtst	0,r9,0b01000   /* addr to */
	dcbtst	0,r10,0b01010  /* length and depth to */
	eieio
	dcbt	0,r8,0b01010	/* all streams GO */
.machine pop

#ifdef CONFIG_ALTIVEC
	mflr	r0
	std	r3,-STACKFRAMESIZE+STK_REG(R31)(r1)
	std	r4,-STACKFRAMESIZE+STK_REG(R30)(r1)
	std	r0,16(r1)
	stdu	r1,-STACKFRAMESIZE(r1)
	bl	enter_vmx_copy
	cmpwi	r3,0
	ld	r0,STACKFRAMESIZE+16(r1)
	ld	r3,STK_REG(R31)(r1)
	ld	r4,STK_REG(R30)(r1)
	mtlr	r0

	li	r0,(PAGE_SIZE/128)
	mtctr	r0

	beq	.Lnonvmx_copy

	addi	r1,r1,STACKFRAMESIZE

	li	r6,16
	li	r7,32
	li	r8,48
	li	r9,64
	li	r10,80
	li	r11,96
	li	r12,112

	.align	5
1:	lvx	v7,0,r4
	lvx	v6,r4,r6
	lvx	v5,r4,r7
	lvx	v4,r4,r8
	lvx	v3,r4,r9
	lvx	v2,r4,r10
	lvx	v1,r4,r11
	lvx	v0,r4,r12
	addi	r4,r4,128
	stvx	v7,0,r3
	stvx	v6,r3,r6
	stvx	v5,r3,r7
	stvx	v4,r3,r8
	stvx	v3,r3,r9
	stvx	v2,r3,r10
	stvx	v1,r3,r11
	stvx	v0,r3,r12
	addi	r3,r3,128
	bdnz	1b

	b	exit_vmx_copy		/* tail call optimise */

#else
	li	r0,(PAGE_SIZE/128)
	mtctr	r0

	stdu	r1,-STACKFRAMESIZE(r1)
#endif

.Lnonvmx_copy:
	std	r14,STK_REG(R14)(r1)
	std	r15,STK_REG(R15)(r1)
	std	r16,STK_REG(R16)(r1)
	std	r17,STK_REG(R17)(r1)
	std	r18,STK_REG(R18)(r1)
	std	r19,STK_REG(R19)(r1)
	std	r20,STK_REG(R20)(r1)

1:	ld	r0,0(r4)
	ld	r5,8(r4)
	ld	r6,16(r4)
	ld	r7,24(r4)
	ld	r8,32(r4)
	ld	r9,40(r4)
	ld	r10,48(r4)
	ld	r11,56(r4)
	ld	r12,64(r4)
	ld	r14,72(r4)
	ld	r15,80(r4)
	ld	r16,88(r4)
	ld	r17,96(r4)
	ld	r18,104(r4)
	ld	r19,112(r4)
	ld	r20,120(r4)
	addi	r4,r4,128
	std	r0,0(r3)
	std	r5,8(r3)
	std	r6,16(r3)
	std	r7,24(r3)
	std	r8,32(r3)
	std	r9,40(r3)
	std	r10,48(r3)
	std	r11,56(r3)
	std	r12,64(r3)
	std	r14,72(r3)
	std	r15,80(r3)
	std	r16,88(r3)
	std	r17,96(r3)
	std	r18,104(r3)
	std	r19,112(r3)
	std	r20,120(r3)
	addi	r3,r3,128
	bdnz	1b

	ld	r14,STK_REG(R14)(r1)
	ld	r15,STK_REG(R15)(r1)
	ld	r16,STK_REG(R16)(r1)
	ld	r17,STK_REG(R17)(r1)
	ld	r18,STK_REG(R18)(r1)
	ld	r19,STK_REG(R19)(r1)
	ld	r20,STK_REG(R20)(r1)
	addi	r1,r1,STACKFRAMESIZE
	blr

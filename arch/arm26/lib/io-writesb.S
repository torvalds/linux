/*
 *  linux/arch/arm26/lib/io-writesb.S
 *
 *  Copyright (C) 1995-2000 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/linkage.h>
#include <asm/assembler.h>
#include <asm/hardware.h>

.outsb_align:	rsb	ip, ip, #4
		cmp	ip, r2
		movgt	ip, r2
		cmp	ip, #2
		ldrb	r3, [r1], #1
		strb	r3, [r0]
		ldrgeb	r3, [r1], #1
		strgeb	r3, [r0]
		ldrgtb	r3, [r1], #1
		strgtb	r3, [r0]
		subs	r2, r2, ip
		bne	.outsb_aligned

ENTRY(__raw_writesb)
		teq	r2, #0		@ do we have to check for the zero len?
		moveq	pc, lr
		ands	ip, r1, #3
		bne	.outsb_align

.outsb_aligned:	stmfd	sp!, {r4 - r6, lr}

		subs	r2, r2, #16
		bmi	.outsb_no_16

.outsb_16_lp:	ldmia	r1!, {r3 - r6}

		strb	r3, [r0]
		mov	r3, r3, lsr #8
		strb	r3, [r0]
		mov	r3, r3, lsr #8
		strb	r3, [r0]
		mov	r3, r3, lsr #8
		strb	r3, [r0]

		strb	r4, [r0]
		mov	r4, r4, lsr #8
		strb	r4, [r0]
		mov	r4, r4, lsr #8
		strb	r4, [r0]
		mov	r4, r4, lsr #8
		strb	r4, [r0]

		strb	r5, [r0]
		mov	r5, r5, lsr #8
		strb	r5, [r0]
		mov	r5, r5, lsr #8
		strb	r5, [r0]
		mov	r5, r5, lsr #8
		strb	r5, [r0]

		strb	r6, [r0]
		mov	r6, r6, lsr #8
		strb	r6, [r0]
		mov	r6, r6, lsr #8
		strb	r6, [r0]
		mov	r6, r6, lsr #8
		strb	r6, [r0]

		subs	r2, r2, #16
		bpl	.outsb_16_lp

		tst	r2, #15
		LOADREGS(eqfd, sp!, {r4 - r6, pc})

.outsb_no_16:	tst	r2, #8
		beq	.outsb_no_8

		ldmia	r1!, {r3, r4}

		strb	r3, [r0]
		mov	r3, r3, lsr #8
		strb	r3, [r0]
		mov	r3, r3, lsr #8
		strb	r3, [r0]
		mov	r3, r3, lsr #8
		strb	r3, [r0]

		strb	r4, [r0]
		mov	r4, r4, lsr #8
		strb	r4, [r0]
		mov	r4, r4, lsr #8
		strb	r4, [r0]
		mov	r4, r4, lsr #8
		strb	r4, [r0]

.outsb_no_8:	tst	r2, #4
		beq	.outsb_no_4

		ldr	r3, [r1], #4
		strb	r3, [r0]
		mov	r3, r3, lsr #8
		strb	r3, [r0]
		mov	r3, r3, lsr #8
		strb	r3, [r0]
		mov	r3, r3, lsr #8
		strb	r3, [r0]

.outsb_no_4:	ands	r2, r2, #3
		LOADREGS(eqfd, sp!, {r4 - r6, pc})

		cmp	r2, #2
		ldrb	r3, [r1], #1
		strb	r3, [r0]
		ldrgeb	r3, [r1], #1
		strgeb	r3, [r0]
		ldrgtb	r3, [r1]
		strgtb	r3, [r0]

		LOADREGS(fd, sp!, {r4 - r6, pc})

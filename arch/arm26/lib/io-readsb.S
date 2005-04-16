/*
 *  linux/arch/arm26/lib/io-readsb.S
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

.insb_align:	rsb	ip, ip, #4
		cmp	ip, r2
		movgt	ip, r2
		cmp	ip, #2
		ldrb	r3, [r0]
		strb	r3, [r1], #1
		ldrgeb	r3, [r0]
		strgeb	r3, [r1], #1
		ldrgtb	r3, [r0]
		strgtb	r3, [r1], #1
		subs	r2, r2, ip
		bne	.insb_aligned

ENTRY(__raw_readsb)
		teq	r2, #0		@ do we have to check for the zero len?
		moveq	pc, lr
		ands	ip, r1, #3
		bne	.insb_align

.insb_aligned:	stmfd	sp!, {r4 - r6, lr}

		subs	r2, r2, #16
		bmi	.insb_no_16

.insb_16_lp:	ldrb	r3, [r0]
		ldrb	r4, [r0]
		orr	r3, r3, r4, lsl #8
		ldrb	r4, [r0]
		orr	r3, r3, r4, lsl #16
		ldrb	r4, [r0]
		orr	r3, r3, r4, lsl #24
		ldrb	r4, [r0]
		ldrb	r5, [r0]
		orr	r4, r4, r5, lsl #8
		ldrb	r5, [r0]
		orr	r4, r4, r5, lsl #16
		ldrb	r5, [r0]
		orr	r4, r4, r5, lsl #24
		ldrb	r5, [r0]
		ldrb	r6, [r0]
		orr	r5, r5, r6, lsl #8
		ldrb	r6, [r0]
		orr	r5, r5, r6, lsl #16
		ldrb	r6, [r0]
		orr	r5, r5, r6, lsl #24
		ldrb	r6, [r0]
		ldrb	ip, [r0]
		orr	r6, r6, ip, lsl #8
		ldrb	ip, [r0]
		orr	r6, r6, ip, lsl #16
		ldrb	ip, [r0]
		orr	r6, r6, ip, lsl #24
		stmia	r1!, {r3 - r6}

		subs	r2, r2, #16
		bpl	.insb_16_lp

		tst	r2, #15
		LOADREGS(eqfd, sp!, {r4 - r6, pc})

.insb_no_16:	tst	r2, #8
		beq	.insb_no_8

		ldrb	r3, [r0]
		ldrb	r4, [r0]
		orr	r3, r3, r4, lsl #8
		ldrb	r4, [r0]
		orr	r3, r3, r4, lsl #16
		ldrb	r4, [r0]
		orr	r3, r3, r4, lsl #24
		ldrb	r4, [r0]
		ldrb	r5, [r0]
		orr	r4, r4, r5, lsl #8
		ldrb	r5, [r0]
		orr	r4, r4, r5, lsl #16
		ldrb	r5, [r0]
		orr	r4, r4, r5, lsl #24
		stmia	r1!, {r3, r4}

.insb_no_8:	tst	r2, #4
		beq	.insb_no_4

		ldrb	r3, [r0]
		ldrb	r4, [r0]
		orr	r3, r3, r4, lsl #8
		ldrb	r4, [r0]
		orr	r3, r3, r4, lsl #16
		ldrb	r4, [r0]
		orr	r3, r3, r4, lsl #24
		str	r3, [r1], #4

.insb_no_4:	ands	r2, r2, #3
		LOADREGS(eqfd, sp!, {r4 - r6, pc})

		cmp	r2, #2
		ldrb	r3, [r0]
		strb	r3, [r1], #1
		ldrgeb	r3, [r0]
		strgeb	r3, [r1], #1
		ldrgtb	r3, [r0]
		strgtb	r3, [r1]

		LOADREGS(fd, sp!, {r4 - r6, pc})

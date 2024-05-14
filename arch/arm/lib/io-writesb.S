/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *  linux/arch/arm/lib/io-writesb.S
 *
 *  Copyright (C) 1995-2000 Russell King
 */
#include <linux/linkage.h>
#include <asm/assembler.h>

		.macro	outword, rd
#ifndef __ARMEB__
		strb	\rd, [r0]
		mov	\rd, \rd, lsr #8
		strb	\rd, [r0]
		mov	\rd, \rd, lsr #8
		strb	\rd, [r0]
		mov	\rd, \rd, lsr #8
		strb	\rd, [r0]
#else
		mov	lr, \rd, lsr #24
		strb	lr, [r0]
		mov	lr, \rd, lsr #16
		strb	lr, [r0]
		mov	lr, \rd, lsr #8
		strb	lr, [r0]
		strb	\rd, [r0]
#endif
		.endm

.Loutsb_align:	rsb	ip, ip, #4
		cmp	ip, r2
		movgt	ip, r2
		cmp	ip, #2
		ldrb	r3, [r1], #1
		strb	r3, [r0]
		ldrbge	r3, [r1], #1
		strbge	r3, [r0]
		ldrbgt	r3, [r1], #1
		strbgt	r3, [r0]
		subs	r2, r2, ip
		bne	.Loutsb_aligned

ENTRY(__raw_writesb)
		teq	r2, #0		@ do we have to check for the zero len?
		reteq	lr
		ands	ip, r1, #3
		bne	.Loutsb_align

.Loutsb_aligned:
		stmfd	sp!, {r4, r5, lr}

		subs	r2, r2, #16
		bmi	.Loutsb_no_16

.Loutsb_16_lp:	ldmia	r1!, {r3, r4, r5, ip}
		outword	r3
		outword	r4
		outword	r5
		outword	ip
		subs	r2, r2, #16
		bpl	.Loutsb_16_lp

		tst	r2, #15
		ldmfdeq	sp!, {r4, r5, pc}

.Loutsb_no_16:	tst	r2, #8
		beq	.Loutsb_no_8

		ldmia	r1!, {r3, r4}
		outword	r3
		outword	r4

.Loutsb_no_8:	tst	r2, #4
		beq	.Loutsb_no_4

		ldr	r3, [r1], #4
		outword	r3

.Loutsb_no_4:	ands	r2, r2, #3
		ldmfdeq	sp!, {r4, r5, pc}

		cmp	r2, #2
		ldrb	r3, [r1], #1
		strb	r3, [r0]
		ldrbge	r3, [r1], #1
		strbge	r3, [r0]
		ldrbgt	r3, [r1]
		strbgt	r3, [r0]

		ldmfd	sp!, {r4, r5, pc}
ENDPROC(__raw_writesb)

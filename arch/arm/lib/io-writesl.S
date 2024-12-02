/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *  linux/arch/arm/lib/io-writesl.S
 *
 *  Copyright (C) 1995-2000 Russell King
 */
#include <linux/linkage.h>
#include <asm/assembler.h>

ENTRY(__raw_writesl)
		teq	r2, #0		@ do we have to check for the zero len?
		reteq	lr
		ands	ip, r1, #3
		bne	3f

		subs	r2, r2, #4
		bmi	2f
		stmfd	sp!, {r4, lr}
1:		ldmia	r1!, {r3, r4, ip, lr}
		subs	r2, r2, #4
		str	r3, [r0, #0]
		str	r4, [r0, #0]
		str	ip, [r0, #0]
		str	lr, [r0, #0]
		bpl	1b
		ldmfd	sp!, {r4, lr}
2:		movs	r2, r2, lsl #31
		ldmiacs	r1!, {r3, ip}
		strcs	r3, [r0, #0]
		ldrne	r3, [r1, #0]
		strcs	ip, [r0, #0]
		strne	r3, [r0, #0]
		ret	lr

3:		bic	r1, r1, #3
		ldr	r3, [r1], #4
		cmp	ip, #2
		blt	5f
		bgt	6f

4:		mov	ip, r3, lspull #16
		ldr	r3, [r1], #4
		subs	r2, r2, #1
		orr	ip, ip, r3, lspush #16
		str	ip, [r0]
		bne	4b
		ret	lr

5:		mov	ip, r3, lspull #8
		ldr	r3, [r1], #4
		subs	r2, r2, #1
		orr	ip, ip, r3, lspush #24
		str	ip, [r0]
		bne	5b
		ret	lr

6:		mov	ip, r3, lspull #24
		ldr	r3, [r1], #4
		subs	r2, r2, #1
		orr	ip, ip, r3, lspush #8
		str	ip, [r0]
		bne	6b
		ret	lr
ENDPROC(__raw_writesl)

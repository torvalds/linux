/*
 *  linux/arch/arm26/lib/io-readsl.S
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

/*
 * Note that some reads can be aligned on half-word boundaries.
 */
ENTRY(__raw_readsl)
		teq	r2, #0		@ do we have to check for the zero len?
		moveq	pc, lr
		ands	ip, r1, #3
		bne	2f

1:		ldr	r3, [r0]
		str	r3, [r1], #4
		subs	r2, r2, #1
		bne	1b
		mov	pc, lr

2:		cmp	ip, #2
		ldr	ip, [r0]
		blt	4f
		bgt	6f

		strb	ip, [r1], #1
		mov	ip, ip, lsr #8
		strb	ip, [r1], #1
		mov	ip, ip, lsr #8
3:		subs	r2, r2, #1
		ldrne	r3, [r0]
		orrne	ip, ip, r3, lsl #16
		strne	ip, [r1], #4
		movne	ip, r3, lsr #16
		bne	3b
		strb	ip, [r1], #1
		mov	ip, ip, lsr #8
		strb	ip, [r1], #1
		mov	pc, lr

4:		strb	ip, [r1], #1
		mov	ip, ip, lsr #8
		strb	ip, [r1], #1
		mov	ip, ip, lsr #8
		strb	ip, [r1], #1
		mov	ip, ip, lsr #8
5:		subs	r2, r2, #1
		ldrne	r3, [r0]
		orrne	ip, ip, r3, lsl #8
		strne	ip, [r1], #4
		movne	ip, r3, lsr #24
		bne	5b
		strb	ip, [r1], #1
		mov	pc, lr

6:		strb	ip, [r1], #1
		mov	ip, ip, lsr #8
7:		subs	r2, r2, #1
		ldrne	r3, [r0]
		orrne	ip, ip, r3, lsl #24
		strne	ip, [r1], #4
		movne	ip, r3, lsr #8
		bne	7b
		strb	ip, [r1], #1
		mov	ip, ip, lsr #8
		strb	ip, [r1], #1
		mov	ip, ip, lsr #8
		strb	ip, [r1], #1
		mov	pc, lr


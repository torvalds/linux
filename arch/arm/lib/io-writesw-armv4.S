/*
 *  linux/arch/arm/lib/io-writesw-armv4.S
 *
 *  Copyright (C) 1995-2000 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/linkage.h>
#include <asm/assembler.h>

		.macro	outword, rd
#ifndef __ARMEB__
		strh	\rd, [r0]
		mov	\rd, \rd, lsr #16
		strh	\rd, [r0]
#else
		mov	lr, \rd, lsr #16
		strh	lr, [r0]
		strh	\rd, [r0]
#endif
		.endm

.Loutsw_align:	movs	ip, r1, lsl #31
		bne	.Loutsw_noalign

		ldrh	r3, [r1], #2
		sub	r2, r2, #1
		strh	r3, [r0]

ENTRY(__raw_writesw)
		teq	r2, #0
		reteq	lr
		ands	r3, r1, #3
		bne	.Loutsw_align

		stmfd	sp!, {r4, r5, lr}

		subs	r2, r2, #8
		bmi	.Lno_outsw_8

.Loutsw_8_lp:	ldmia	r1!, {r3, r4, r5, ip}
		subs	r2, r2, #8
		outword	r3
		outword	r4
		outword	r5
		outword	ip
		bpl	.Loutsw_8_lp

.Lno_outsw_8:	tst	r2, #4
		beq	.Lno_outsw_4

		ldmia	r1!, {r3, ip}
		outword	r3
		outword	ip

.Lno_outsw_4:	movs	r2, r2, lsl #31
		bcc	.Lno_outsw_2

		ldr	r3, [r1], #4
		outword	r3

.Lno_outsw_2:	ldrneh	r3, [r1]
		strneh	r3, [r0]

		ldmfd	sp!, {r4, r5, pc}

#ifdef __ARMEB__
#define pull_hbyte0	lsl #8
#define push_hbyte1	lsr #24
#else
#define pull_hbyte0	lsr #24
#define push_hbyte1	lsl #8
#endif

.Loutsw_noalign:
 ARM(		ldr	r3, [r1, -r3]!	)
 THUMB(		rsb	r3, r3, #0	)
 THUMB(		ldr	r3, [r1, r3]	)
 THUMB(		sub	r1, r3		)
		subcs	r2, r2, #1
		bcs	2f
		subs	r2, r2, #2
		bmi	3f

1:		mov	ip, r3, lsr #8
		strh	ip, [r0]
2:		mov	ip, r3, pull_hbyte0
		ldr	r3, [r1, #4]!
		subs	r2, r2, #2
		orr	ip, ip, r3, push_hbyte1
		strh	ip, [r0]
		bpl	1b

		tst	r2, #1
3:		movne	ip, r3, lsr #8
		strneh	ip, [r0]
		ret	lr
ENDPROC(__raw_writesw)

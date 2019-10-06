/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *  linux/arch/arm/lib/strchr.S
 *
 *  Copyright (C) 1995-2000 Russell King
 *
 *  ASM optimised string functions
 */
#include <linux/linkage.h>
#include <asm/assembler.h>

		.text
		.align	5
ENTRY(strchr)
		and	r1, r1, #0xff
1:		ldrb	r2, [r0], #1
		teq	r2, r1
		teqne	r2, #0
		bne	1b
		teq	r2, r1
		movne	r0, #0
		subeq	r0, r0, #1
		ret	lr
ENDPROC(strchr)

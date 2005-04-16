/*
 *  linux/arch/arm26/lib/strchr.S
 *
 *  Copyright (C) 1995-2000 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  ASM optimised string functions
 */
#include <linux/linkage.h>
#include <asm/assembler.h>

		.text
		.align	5
ENTRY(strchr)
1:		ldrb	r2, [r0], #1
		teq	r2, r1
		teqne	r2, #0
		bne	1b
		teq	r2, #0
		moveq	r0, #0
		subne	r0, r0, #1
		RETINSTR(mov,pc,lr)

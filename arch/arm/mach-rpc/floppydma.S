/*
 *  linux/arch/arm/lib/floppydma.S
 *
 *  Copyright (C) 1995, 1996 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/linkage.h>
#include <asm/assembler.h>
		.text

		.global	floppy_fiqin_end
ENTRY(floppy_fiqin_start)
		subs	r9, r9, #1
		ldrbgt	r12, [r11, #-4]
		ldrble	r12, [r11], #0
		strb	r12, [r10], #1
		subs	pc, lr, #4
floppy_fiqin_end:

		.global	floppy_fiqout_end
ENTRY(floppy_fiqout_start)
		subs	r9, r9, #1
		ldrbge	r12, [r10], #1
		movlt	r12, #0
		strble	r12, [r11], #0
		subsle	pc, lr, #4
		strb	r12, [r11, #-4]
		subs	pc, lr, #4
floppy_fiqout_end:

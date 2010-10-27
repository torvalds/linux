/*  arch/arm/mach-aaec2000/include/mach/debug-macro.S
 *
 *  Debugging macro include header
 *
 *  Copyright (c) 2005 Nicolas Bellido Y Ortega
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */

#include "hardware.h"
		.macro	addruart, rx, tmp
		mrc	p15, 0, \rx, c1, c0
		tst	\rx, #1			@ MMU enabled?
		moveq	\rx, #0x80000000		@ physical
		movne	\rx, #io_p2v(0x80000000)	@ virtual
		orr	\rx, \rx, #0x00000800
		.endm

		.macro	senduart,rd,rx
		str	\rd, [\rx, #0]
		.endm

		.macro	busyuart,rd,rx
1002:		ldr	\rd, [\rx, #0x10]
		tst	\rd, #(1 << 7)
		beq	1002b
		.endm

		.macro	waituart,rd,rx
#if 0
1001:		ldr	\rd, [\rx, #0x10]
		tst	\rd, #(1 << 5)
		beq	1001b
#endif
		.endm

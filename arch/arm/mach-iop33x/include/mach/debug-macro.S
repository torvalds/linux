/*
 * arch/arm/mach-iop33x/include/mach/debug-macro.S
 *
 * Debugging macro include header
 *
 * Copyright (C) 1994-1999 Russell King
 * Moved from linux/arch/arm/kernel/debug.S by Ben Dooks
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

		.macro	addruart, rx
		mrc	p15, 0, \rx, c1, c0
		tst	\rx, #1			@ mmu enabled?
		moveq	\rx, #0xff000000	@ physical
		movne	\rx, #0xfe000000	@ virtual
		orr	\rx, \rx, #0x00ff0000
		orr	\rx, \rx, #0x0000f700
		.endm

#define UART_SHIFT	2
#include <asm/hardware/debug-8250.S>

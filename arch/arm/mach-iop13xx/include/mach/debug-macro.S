/*
 * arch/arm/mach-iop13xx/include/mach/debug-macro.S
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

	.macro	addruart, rp, rv
	mov	\rp, #0x00002300
	orr	\rp, \rp, #0x00000040
	orr	\rv, \rp, #0xfe000000	@ virtual
	orr	\rv, \rv, #0x00e80000
	orr	\rp, \rp, #0xff000000	@ physical
	orr	\rp, \rp, #0x00d80000
	.endm

#define UART_SHIFT	2
#include <asm/hardware/debug-8250.S>

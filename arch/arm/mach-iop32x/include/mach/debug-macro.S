/*
 * arch/arm/mach-iop32x/include/mach/debug-macro.S
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

		.macro	addruart, rx, tmp
		mov	\rx, #0xfe000000	@ physical as well as virtual
		orr	\rx, \rx, #0x00800000	@ location of the UART
		.endm

#define UART_SHIFT	0
#include <asm/hardware/debug-8250.S>

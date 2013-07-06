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

		.macro	addruart, rp, rv, tmp
		mov	\rp, #0x00ff0000
		orr	\rp, \rp, #0x0000f700
		orr	\rv, #0xfe000000	@ virtual
		orr	\rp, #0xff000000	@ physical
		.endm

#include <asm/hardware/debug-8250.S>

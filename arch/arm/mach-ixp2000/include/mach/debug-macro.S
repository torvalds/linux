/* arch/arm/mach-ixp2000/include/mach/debug-macro.S
 *
 * Debugging macro include header
 *
 *  Copyright (C) 1994-1999 Russell King
 *  Moved from linux/arch/arm/kernel/debug.S by Ben Dooks
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
*/

		.macro  addruart, rp, rv, tmp
		mov	\rp, #0x00030000
#ifdef	__ARMEB__
		orr	\rp, \rp, #0x00000003
#endif
		orr	\rv, \rp, #0xfe000000	@ virtual base
		orr	\rv, \rv, #0x00f00000
		orr	\rp, \rp, #0xc0000000	@ Physical base
		.endm

#define UART_SHIFT	2
#include <asm/hardware/debug-8250.S>

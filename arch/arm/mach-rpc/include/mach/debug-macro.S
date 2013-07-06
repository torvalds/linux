/* arch/arm/mach-rpc/include/mach/debug-macro.S
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

		.macro	addruart, rp, rv, tmp
		mov	\rp, #0x00010000
		orr	\rp, \rp, #0x00000fe0
		orr	\rv, \rp, #0xe0000000	@ virtual
		orr	\rp, \rp, #0x03000000	@ physical
		.endm

#define UART_SHIFT	2
#include <asm/hardware/debug-8250.S>

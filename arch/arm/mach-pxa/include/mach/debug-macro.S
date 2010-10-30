/* arch/arm/mach-pxa/include/mach/debug-macro.S
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

#include "hardware.h"

		.macro	addruart, rp, rv
		mov	\rp, #0x00100000
		orr	\rv, \rp, #io_p2v(0x40000000)	@ virtual
		orr	\rp, \rp, #0x40000000		@ physical
		.endm

#define UART_SHIFT	2
#include <asm/hardware/debug-8250.S>

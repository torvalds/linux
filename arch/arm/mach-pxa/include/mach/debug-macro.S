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

		.macro	addruart, rx, tmp
		mrc	p15, 0, \rx, c1, c0
		tst	\rx, #1			@ MMU enabled?
		moveq	\rx, #0x40000000		@ physical
		movne	\rx, #io_p2v(0x40000000)	@ virtual
		orr	\rx, \rx, #0x00100000
		.endm

#define UART_SHIFT	2
#include <asm/hardware/debug-8250.S>

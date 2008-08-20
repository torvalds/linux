/* arch/arm/mach-ebsa110/include/mach/debug-macro.S
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
**/

		.macro	addruart,rx
		mov	\rx, #0xf0000000
		orr	\rx, \rx, #0x00000be0
		.endm

#define UART_SHIFT	2
#define FLOW_CONTROL
#include <asm/hardware/debug-8250.S>

/* arch/arm/mach-netx/include/mach/debug-macro.S
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
		mov	\rp, #0x00000a00
		orr	\rv, \rp, #io_p2v(0x00100000)	@ virtual
		orr	\rp, \rp, #0x00100000		@ physical
		.endm

		.macro	senduart,rd,rx
		str	\rd, [\rx, #0]
		.endm

		.macro	busyuart,rd,rx
1002:		ldr	\rd, [\rx, #0x18]
		tst	\rd, #(1 << 3)
		bne	1002b
		.endm

		.macro	waituart,rd,rx
1001:		ldr	\rd, [\rx, #0x18]
		tst	\rd, #(1 << 3)
		bne	1001b
		.endm

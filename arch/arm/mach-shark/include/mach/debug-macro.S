/* arch/arm/mach-shark/include/mach/debug-macro.S
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

		.macro	addruart, rp, rv
		mov	\rp, #0xe0000000
		orr	\rp, \rp, #0x000003f8
		mov	\rv, \rp
		.endm

		.macro	senduart,rd,rx
		strb	\rd, [\rx]
		.endm

		.macro waituart,rd,rx
		.endm

		.macro	busyuart,rd,rx
		mov	\rd, #0
1001:		add	\rd, \rd, #1
		teq	\rd, #0x10000
		bne	1001b
		.endm


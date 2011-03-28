/*
 * arch/arm/mach-vt8500/include/mach/debug-macro.S
 *
 *  Copyright (C) 2010 Alexey Charkov <alchark@gmail.com>
 *
 * Debugging macro include header
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
*/

	.macro	addruart, rp, rv
	mov	\rp,      #0x00200000
	orr	\rv, \rp, #0xf8000000
	orr	\rp, \rp, #0xd8000000
	.endm

	.macro	senduart,rd,rx
	strb	\rd, [\rx, #0]
	.endm

	.macro	busyuart,rd,rx
1001:	ldr	\rd, [\rx, #0x1c]
	ands	\rd, \rd, #0x2
	bne	1001b
	.endm

	.macro	waituart,rd,rx
	.endm

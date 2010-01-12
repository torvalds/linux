/*
 * Debugging macro include header
 *
 *  Copyright (C) 2009 ST-Ericsson
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */
	.macro	addruart,rx
	mrc	p15, 0, \rx, c1, c0
	tst	\rx, #1			@MMU enabled?
	moveq	\rx, #0x80000000	@MMU off, Physical address
	movne	\rx, #0xF0000000	@MMU on, Virtual address
	orr	\rx, \rx, #0x7000
	.endm

#include <asm/hardware/debug-pl01x.S>

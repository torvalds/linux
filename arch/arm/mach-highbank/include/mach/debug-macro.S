/*
 * Debugging macro include header
 *
 *  Copyright (C) 1994-1999 Russell King
 *  Moved from linux/arch/arm/kernel/debug.S by Ben Dooks
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

		.macro	addruart,rp,rv,tmp
		movw	\rv, #0x6000
		movt	\rv, #0xfee3
		movw	\rp, #0x6000
		movt	\rp, #0xfff3
		.endm

#include <asm/hardware/debug-pl01x.S>

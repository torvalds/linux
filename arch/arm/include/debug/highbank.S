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
		ldr	\rv, =0xfee36000
		ldr	\rp, =0xfff36000
		.endm

#include <asm/hardware/debug-pl01x.S>

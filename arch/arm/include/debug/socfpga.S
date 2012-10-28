/*
 *  Copyright (C) 1994-1999 Russell King
 *  Moved from linux/arch/arm/kernel/debug.S by Ben Dooks
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

		.macro	addruart, rp, rv, tmp
		mov	\rp, #DEBUG_LL_UART_OFFSET
		orr	\rp, \rp, #0x00c00000
		orr	\rv, \rp, #0xfe000000	@ virtual base
		orr	\rp, \rp, #0xff000000	@ physical base
		.endm


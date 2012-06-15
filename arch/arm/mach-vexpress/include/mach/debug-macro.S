/* arch/arm/mach-realview/include/mach/debug-macro.S
 *
 * Debugging macro include header
 *
 *  Copyright (C) 1994-1999 Russell King
 *  Moved from linux/arch/arm/kernel/debug.S by Ben Dooks
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#define DEBUG_LL_PHYS_BASE		0x10000000
#define DEBUG_LL_UART_OFFSET		0x00009000

#define DEBUG_LL_PHYS_BASE_RS1		0x1c000000
#define DEBUG_LL_UART_OFFSET_RS1	0x00090000

#define DEBUG_LL_VIRT_BASE		0xf8000000

		.macro	addruart,rp,rv,tmp

		@ Make an educated guess regarding the memory map:
		@ - the original A9 core tile, which has MPCore peripherals
		@   located at 0x1e000000, should use UART at 0x10009000
		@ - all other (RS1 complaint) tiles use UART mapped
		@   at 0x1c090000
		mrc	p15, 4, \tmp, c15, c0, 0
		cmp	\tmp, #0x1e000000

		@ Original memory map
		moveq	\rp, #DEBUG_LL_UART_OFFSET
		orreq	\rv, \rp, #DEBUG_LL_VIRT_BASE
		orreq	\rp, \rp, #DEBUG_LL_PHYS_BASE

		@ RS1 memory map
		movne	\rp, #DEBUG_LL_UART_OFFSET_RS1
		orrne	\rv, \rp, #DEBUG_LL_VIRT_BASE
		orrne	\rp, \rp, #DEBUG_LL_PHYS_BASE_RS1

		.endm

#include <asm/hardware/debug-pl01x.S>

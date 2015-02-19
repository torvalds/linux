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

#define DEBUG_LL_UART_PHYS_CRX		0xb0090000

#define DEBUG_LL_VIRT_BASE		0xf8000000

#if defined(CONFIG_DEBUG_VEXPRESS_UART0_DETECT)

		.macro	addruart,rp,rv,tmp
		.arch   armv7-a

		@ Make an educated guess regarding the memory map:
		@ - the original A9 core tile (based on ARM Cortex-A9 r0p1)
		@   should use UART at 0x10009000
		@ - all other (RS1 complaint) tiles use UART mapped
		@   at 0x1c090000
		mrc	p15, 0, \rp, c0, c0, 0
		movw	\rv, #0xc091
		movt	\rv, #0x410f
		cmp	\rp, \rv

		@ Original memory map
		moveq	\rp, #DEBUG_LL_UART_OFFSET
		orreq	\rv, \rp, #DEBUG_LL_VIRT_BASE
		orreq	\rp, \rp, #DEBUG_LL_PHYS_BASE

		@ RS1 memory map
		movne	\rp, #DEBUG_LL_UART_OFFSET_RS1
		orrne	\rv, \rp, #DEBUG_LL_VIRT_BASE
		orrne	\rp, \rp, #DEBUG_LL_PHYS_BASE_RS1

		.endm

#include <debug/pl01x.S>
#endif

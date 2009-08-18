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

#if defined(CONFIG_MACH_REALVIEW_EB) || \
    defined(CONFIG_MACH_REALVIEW_PB11MP) || \
    defined(CONFIG_MACH_REALVIEW_PBA8) || \
    defined(CONFIG_MACH_REALVIEW_PBX)
#ifndef DEBUG_LL_UART_OFFSET
#define DEBUG_LL_UART_OFFSET	0x00009000
#elif DEBUG_LL_UART_OFFSET != 0x00009000
#warning "DEBUG_LL_UART_OFFSET already defined to a different value"
#endif
#endif

#ifdef CONFIG_MACH_REALVIEW_PB1176
#ifndef DEBUG_LL_UART_OFFSET
#define DEBUG_LL_UART_OFFSET	0x0010c000
#elif DEBUG_LL_UART_OFFSET != 0x0010c000
#warning "DEBUG_LL_UART_OFFSET already defined to a different value"
#endif
#endif

#ifndef DEBUG_LL_UART_OFFSET
#error "Unknown RealView platform"
#endif

		.macro	addruart,rx
		mrc	p15, 0, \rx, c1, c0
		tst	\rx, #1			@ MMU enabled?
		moveq	\rx,      #0x10000000
		movne	\rx,      #0xfb000000	@ virtual base
		orr	\rx, \rx, #DEBUG_LL_UART_OFFSET
		.endm

#include <asm/hardware/debug-pl01x.S>

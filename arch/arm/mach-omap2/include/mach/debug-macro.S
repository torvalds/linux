/* arch/arm/mach-omap2/include/mach/debug-macro.S
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

#include <linux/serial_reg.h>

#include <plat/serial.h>

		.macro	addruart, rx, tmp
		mrc	p15, 0, \rx, c1, c0
		tst	\rx, #1			@ MMU enabled?
#ifdef  CONFIG_ARCH_OMAP2
		moveq	\rx, #0x48000000	@ physical base address
		movne	\rx, #0xfa000000	@ virtual base
		orr	\rx, \rx, #0x0006a000
#ifdef CONFIG_OMAP_LL_DEBUG_UART2
		add	\rx, \rx, #0x00002000	@ UART 2
#endif
#ifdef CONFIG_OMAP_LL_DEBUG_UART3
		add	\rx, \rx, #0x00004000	@ UART 3
#endif

#elif defined(CONFIG_ARCH_OMAP3) || defined(CONFIG_ARCH_OMAP4)
		moveq	\rx, #0x48000000	@ physical base address
		movne	\rx, #0xfa000000	@ virtual base
		orr	\rx, \rx, #0x0006a000
#ifdef CONFIG_OMAP_LL_DEBUG_UART2
		add	\rx, \rx, #0x00002000	@ UART 2
#endif
#ifdef CONFIG_OMAP_LL_DEBUG_UART3
		add	\rx, \rx, #0x00fb0000	@ UART 3
		add	\rx, \rx, #0x00006000
#endif
#endif
		.endm

		.macro	senduart,rd,rx
		strb	\rd, [\rx]
		.endm

		.macro	busyuart,rd,rx
1001:		ldrb	\rd, [\rx, #(UART_LSR << OMAP_PORT_SHIFT)]
		and	\rd, \rd, #(UART_LSR_TEMT | UART_LSR_THRE)
		teq	\rd, #(UART_LSR_TEMT | UART_LSR_THRE)
		bne	1001b
		.endm

		.macro	waituart,rd,rx
		.endm

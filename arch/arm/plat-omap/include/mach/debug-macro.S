/* arch/arm/plat-omap/include/mach/debug-macro.S
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

		.macro	addruart,rx
		mrc	p15, 0, \rx, c1, c0
		tst	\rx, #1			@ MMU enabled?
#ifdef CONFIG_ARCH_OMAP1
		moveq	\rx, #0xff000000	@ physical base address
		movne	\rx, #0xfe000000	@ virtual base
		orr	\rx, \rx, #0x00fb0000
#ifdef CONFIG_OMAP_LL_DEBUG_UART3
		orr	\rx, \rx, #0x00009000	@ UART 3
#endif
#if defined(CONFIG_OMAP_LL_DEBUG_UART2) || defined(CONFIG_OMAP_LL_DEBUG_UART3)
		orr	\rx, \rx, #0x00000800	@ UART 2 & 3
#endif

#elif  CONFIG_ARCH_OMAP2
		moveq	\rx, #0x48000000	@ physical base address
		movne	\rx, #0xd8000000	@ virtual base
		orr	\rx, \rx, #0x0006a000
#ifdef CONFIG_OMAP_LL_DEBUG_UART2
		add	\rx, \rx, #0x00002000	@ UART 2
#endif
#ifdef CONFIG_OMAP_LL_DEBUG_UART3
		add	\rx, \rx, #0x00004000	@ UART 3
#endif

#elif	CONFIG_ARCH_OMAP3
		moveq	\rx, #0x48000000	@ physical base address
		movne	\rx, #0xd8000000	@ virtual base
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
1001:		ldrb	\rd, [\rx, #(0x5 << 2)]	@ OMAP-1510 and friends
		and	\rd, \rd, #0x60
		teq	\rd, #0x60
		beq	1002f
		ldrb	\rd, [\rx, #(0x5 << 0)]	@ OMAP-730 only
		and	\rd, \rd, #0x60
		teq	\rd, #0x60
		bne	1001b
1002:
		.endm

		.macro	waituart,rd,rx
		.endm

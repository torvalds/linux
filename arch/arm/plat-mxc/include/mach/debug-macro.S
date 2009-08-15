/* arch/arm/mach-imx/include/mach/debug-macro.S
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

#include <mach/hardware.h>

#ifdef CONFIG_MACH_MX31ADS
#include <mach/board-mx31ads.h>
#endif
#ifdef CONFIG_MACH_PCM037
#include <mach/board-pcm037.h>
#endif
#ifdef CONFIG_MACH_MX31LITE
#include <mach/board-mx31lite.h>
#endif
#ifdef CONFIG_MACH_MX27ADS
#include <mach/board-mx27ads.h>
#endif
#ifdef CONFIG_MACH_MX21ADS
#include <mach/board-mx21ads.h>
#endif
#ifdef CONFIG_MACH_PCM038
#include <mach/board-pcm038.h>
#endif
#ifdef CONFIG_MACH_MX31_3DS
#include <mach/board-mx31pdk.h>
#endif
#ifdef CONFIG_MACH_QONG
#include <mach/board-qong.h>
#endif
#ifdef CONFIG_MACH_PCM043
#include <mach/board-pcm043.h>
#endif
#ifdef CONFIG_MACH_MX27_3DS
#include <mach/board-mx27pdk.h>
#endif
#ifdef CONFIG_MACH_ARMADILLO5X0
#include <mach/board-armadillo5x0.h>
#endif
#ifdef CONFIG_MACH_MX35_3DS
#include <mach/board-mx35pdk.h>
#endif
#ifdef CONFIG_MACH_MX27LITE
#include <mach/board-mx27lite.h>
#endif
		.macro	addruart,rx
		mrc	p15, 0, \rx, c1, c0
		tst	\rx, #1			@ MMU enabled?
		ldreq	\rx, =MXC_LL_UART_PADDR	@ physical
		ldrne	\rx, =MXC_LL_UART_VADDR	@ virtual
		.endm

		.macro	senduart,rd,rx
		str	\rd, [\rx, #0x40]	@ TXDATA
		.endm

		.macro	waituart,rd,rx
		.endm

		.macro	busyuart,rd,rx
1002:		ldr	\rd, [\rx, #0x98]	@ SR2
		tst	\rd, #1 << 3		@ TXDC
		beq	1002b			@ wait until transmit done
		.endm

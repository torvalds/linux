/*
 * arch/arm/mach-tegra/include/mach/debug-macro.S
 *
 * Copyright (C) 2010 Google, Inc.
 *
 * Author:
 *	Colin Cross <ccross@google.com>
 *	Erik Gilling <konkers@google.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <mach/io.h>

	.macro  addruart,rx, tmp
        mrc     p15, 0, \rx, c1, c0
        tst     \rx, #1                 @ MMU enabled?
        ldreq   \rx, =IO_APB_PHYS       @ physical
        ldrne   \rx, =IO_APB_VIRT        @ virtual
#if defined(CONFIG_TEGRA_DEBUG_UART_NONE)
#error "A debug UART must be selected in the kernel config to use DEBUG_LL"
#elif defined(CONFIG_TEGRA_DEBUG_UARTA)
        orr     \rx, \rx, #0x6000
#elif defined(CONFIG_TEGRA_DEBUG_UARTB)
	ldr	\tmp, =0x6040
        orr     \rx, \rx, \tmp
#elif defined(CONFIG_TEGRA_DEBUG_UARTC)
        orr     \rx, \rx, #0x6200
#elif defined(CONFIG_TEGRA_DEBUG_UARTD)
        orr     \rx, \rx, #0x6300
#elif defined(CONFIG_TEGRA_DEBUG_UARTE)
        orr     \rx, \rx, #0x6400
#endif
	.endm

#define UART_SHIFT	2
#include <asm/hardware/debug-8250.S>


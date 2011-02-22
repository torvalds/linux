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

	.macro  addruart, rp, rv
        ldr     \rp, =IO_APB_PHYS       @ physical
        ldr     \rv, =IO_APB_VIRT        @ virtual
#if defined(CONFIG_TEGRA_DEBUG_UART_NONE)
#error "A debug UART must be selected in the kernel config to use DEBUG_LL"
#elif defined(CONFIG_TEGRA_DEBUG_UARTA)
        orr     \rp, \rp, #0x6000
        orr     \rv, \rv, #0x6000
#elif defined(CONFIG_TEGRA_DEBUG_UARTB)
        orr     \rp, \rp, #0x6000
	orr	\rp, \rp, #0x40
        orr     \rv, \rv, #0x6000
	orr	\rv, \rv, #0x40
#elif defined(CONFIG_TEGRA_DEBUG_UARTC)
        orr     \rp, \rp, #0x6200
        orr     \rv, \rv, #0x6200
#elif defined(CONFIG_TEGRA_DEBUG_UARTD)
        orr     \rp, \rp, #0x6300
        orr     \rv, \rv, #0x6300
#elif defined(CONFIG_TEGRA_DEBUG_UARTE)
        orr     \rp, \rp, #0x6400
        orr     \rv, \rv, #0x6400
#endif
	.endm

#define UART_SHIFT	2
#include <asm/hardware/debug-8250.S>


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
#include <mach/iomap.h>

	.macro  addruart, rp, rv
        ldr     \rp, =IO_APB_PHYS       @ physical
        ldr     \rv, =IO_APB_VIRT        @ virtual
	orr	\rp, \rp, #(TEGRA_DEBUG_UART_BASE & 0xFF)
	orr	\rp, \rp, #(TEGRA_DEBUG_UART_BASE & 0xFF00)
	orr	\rv, \rv, #(TEGRA_DEBUG_UART_BASE & 0xFF)
	orr	\rv, \rv, #(TEGRA_DEBUG_UART_BASE & 0xFF00)
	.endm

#define UART_SHIFT	2
#include <asm/hardware/debug-8250.S>


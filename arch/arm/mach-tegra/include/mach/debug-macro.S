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

	.macro  addruart,rx, tmp
        mrc     p15, 0, \rx, c1, c0
        tst     \rx, #1                 @ MMU enabled?
        ldreq   \rx, =IO_APB_PHYS       @ physical
        ldrne   \rx, =IO_APB_VIRT        @ virtual
	ldr	\tmp, =(TEGRA_DEBUG_UART_BASE & 0xFFFF)
        orr     \rx, \rx, \tmp
	.endm

#define UART_SHIFT	2
#include <asm/hardware/debug-8250.S>


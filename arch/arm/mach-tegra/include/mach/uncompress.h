/*
 * arch/arm/mach-tegra/include/mach/uncompress.h
 *
 * Copyright (C) 2010 Google, Inc.
 * Copyright (C) 2011 Google, Inc.
 *
 * Author:
 *	Colin Cross <ccross@google.com>
 *	Erik Gilling <konkers@google.com>
 *	Doug Anderson <dianders@chromium.org>
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

#ifndef __MACH_TEGRA_UNCOMPRESS_H
#define __MACH_TEGRA_UNCOMPRESS_H

#include <linux/types.h>
#include <linux/serial_reg.h>

#include <mach/iomap.h>

#define DEBUG_UART_SHIFT 2

volatile u8 *uart;

static void putc(int c)
{
	if (uart == NULL)
		return;

	while (!(uart[UART_LSR << DEBUG_UART_SHIFT] & UART_LSR_THRE))
		barrier();
	uart[UART_TX << DEBUG_UART_SHIFT] = c;
}

static inline void flush(void)
{
}

static inline void arch_decomp_setup(void)
{
	volatile u32 *apb_misc = (volatile u32 *)TEGRA_APB_MISC_BASE;
	u32 chip, div;

	uart = (volatile u8 *)TEGRA_DEBUG_UART_BASE;
	if (uart == NULL)
		return;

	chip = (apb_misc[0x804 / 4] >> 8) & 0xff;
	if (chip == 0x20)
		div = 0x0075;
	else
		div = 0x00dd;

	uart[UART_LCR << DEBUG_UART_SHIFT] |= UART_LCR_DLAB;
	uart[UART_DLL << DEBUG_UART_SHIFT] = div & 0xff;
	uart[UART_DLM << DEBUG_UART_SHIFT] = div >> 8;
	uart[UART_LCR << DEBUG_UART_SHIFT] = 3;
}

static inline void arch_decomp_wdog(void)
{
}

#endif

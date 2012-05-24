/*
 * arch/arm/mach-tegra/include/mach/uncompress.h
 *
 * Copyright (C) 2010 Google, Inc.
 * Copyright (C) 2011 Google, Inc.
 * Copyright (C) 2011-2012 NVIDIA CORPORATION. All Rights Reserved.
 *
 * Author:
 *	Colin Cross <ccross@google.com>
 *	Erik Gilling <konkers@google.com>
 *	Doug Anderson <dianders@chromium.org>
 *	Stephen Warren <swarren@nvidia.com>
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
#include <mach/irammap.h>

#define BIT(x) (1 << (x))
#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

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

static inline void save_uart_address(void)
{
	u32 *buf = (u32 *)(TEGRA_IRAM_BASE + TEGRA_IRAM_DEBUG_UART_OFFSET);

	if (uart) {
		buf[0] = TEGRA_IRAM_DEBUG_UART_COOKIE;
		buf[1] = (u32)uart;
	} else
		buf[0] = 0;
}

/*
 * Setup before decompression.  This is where we do UART selection for
 * earlyprintk and init the uart_base register.
 */
static inline void arch_decomp_setup(void)
{
	static const struct {
		u32 base;
		u32 reset_reg;
		u32 clock_reg;
		u32 bit;
	} uarts[] = {
		{
			TEGRA_UARTA_BASE,
			TEGRA_CLK_RESET_BASE + 0x04,
			TEGRA_CLK_RESET_BASE + 0x10,
			6,
		},
		{
			TEGRA_UARTB_BASE,
			TEGRA_CLK_RESET_BASE + 0x04,
			TEGRA_CLK_RESET_BASE + 0x10,
			7,
		},
		{
			TEGRA_UARTC_BASE,
			TEGRA_CLK_RESET_BASE + 0x08,
			TEGRA_CLK_RESET_BASE + 0x14,
			23,
		},
		{
			TEGRA_UARTD_BASE,
			TEGRA_CLK_RESET_BASE + 0x0c,
			TEGRA_CLK_RESET_BASE + 0x18,
			1,
		},
		{
			TEGRA_UARTE_BASE,
			TEGRA_CLK_RESET_BASE + 0x0c,
			TEGRA_CLK_RESET_BASE + 0x18,
			2,
		},
	};
	int i;
	volatile u32 *apb_misc = (volatile u32 *)TEGRA_APB_MISC_BASE;
	u32 chip, div;

	/*
	 * Look for the first UART that:
	 * a) Is not in reset.
	 * b) Is clocked.
	 * c) Has a 'D' in the scratchpad register.
	 *
	 * Note that on Tegra30, the first two conditions are required, since
	 * if not true, accesses to the UART scratch register will hang.
	 * Tegra20 doesn't have this issue.
	 *
	 * The intent is that the bootloader will tell the kernel which UART
	 * to use by setting up those conditions. If nothing found, we'll fall
	 * back to what's specified in TEGRA_DEBUG_UART_BASE.
	 */
	for (i = 0; i < ARRAY_SIZE(uarts); i++) {
		if (*(u8 *)uarts[i].reset_reg & BIT(uarts[i].bit))
			continue;

		if (!(*(u8 *)uarts[i].clock_reg & BIT(uarts[i].bit)))
			continue;

		uart = (volatile u8 *)uarts[i].base;
		if (uart[UART_SCR << DEBUG_UART_SHIFT] != 'D')
			continue;

		break;
	}
	if (i == ARRAY_SIZE(uarts))
		uart = (volatile u8 *)TEGRA_DEBUG_UART_BASE;
	save_uart_address();
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

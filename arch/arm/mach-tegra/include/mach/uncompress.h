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

#include "../../iomap.h"

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

static inline bool uart_clocked(int i)
{
	if (*(u8 *)uarts[i].reset_reg & BIT(uarts[i].bit))
		return false;

	if (!(*(u8 *)uarts[i].clock_reg & BIT(uarts[i].bit)))
		return false;

	return true;
}

#ifdef CONFIG_TEGRA_DEBUG_UART_AUTO_ODMDATA
int auto_odmdata(void)
{
	volatile u32 *pmc = (volatile u32 *)TEGRA_PMC_BASE;
	u32 odmdata = pmc[0xa0 / 4];

	/*
	 * Bits 19:18 are the console type: 0=default, 1=none, 2==DCC, 3==UART
	 * Some boards apparently swap the last two values, but we don't have
	 * any way of catering for that here, so we just accept either. If this
	 * doesn't make sense for your board, just don't enable this feature.
	 *
	 * Bits 17:15 indicate the UART to use, 0/1/2/3/4 are UART A/B/C/D/E.
	 */

	switch  ((odmdata >> 18) & 3) {
	case 2:
	case 3:
		break;
	default:
		return -1;
	}

	return (odmdata >> 15) & 7;
}
#endif

/*
 * Setup before decompression.  This is where we do UART selection for
 * earlyprintk and init the uart_base register.
 */
static inline void arch_decomp_setup(void)
{
	int uart_id;
	volatile u32 *apb_misc = (volatile u32 *)TEGRA_APB_MISC_BASE;
	u32 chip, div;

#if defined(CONFIG_TEGRA_DEBUG_UART_AUTO_ODMDATA)
	uart_id = auto_odmdata();
#elif defined(CONFIG_TEGRA_DEBUG_UARTA)
	uart_id = 0;
#elif defined(CONFIG_TEGRA_DEBUG_UARTB)
	uart_id = 1;
#elif defined(CONFIG_TEGRA_DEBUG_UARTC)
	uart_id = 2;
#elif defined(CONFIG_TEGRA_DEBUG_UARTD)
	uart_id = 3;
#elif defined(CONFIG_TEGRA_DEBUG_UARTE)
	uart_id = 4;
#endif

	if (uart_id < 0 || uart_id >= ARRAY_SIZE(uarts) ||
	    !uart_clocked(uart_id))
		uart = NULL;
	else
		uart = (volatile u8 *)uarts[uart_id].base;

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

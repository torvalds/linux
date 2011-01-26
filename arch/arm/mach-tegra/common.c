/*
 * arch/arm/mach-tegra/board-harmony.c
 *
 * Copyright (C) 2010 Google, Inc.
 *
 * Author:
 *	Colin Cross <ccross@android.com>
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

#include <linux/init.h>
#include <linux/io.h>
#include <linux/clk.h>
#include <linux/delay.h>

#include <asm/hardware/cache-l2x0.h>

#include <mach/iomap.h>
#include <mach/dma.h>

#include "board.h"
#include "clock.h"
#include "fuse.h"

static __initdata struct tegra_clk_init_table common_clk_init_table[] = {
	/* name		parent		rate		enabled */
	{ "clk_m",	NULL,		0,		true },
	{ "pll_p",	"clk_m",	216000000,	true },
	{ "pll_p_out1",	"pll_p",	28800000,	true },
	{ "pll_p_out2",	"pll_p",	48000000,	true },
	{ "pll_p_out3",	"pll_p",	72000000,	true },
	{ "pll_p_out4",	"pll_p",	108000000,	true },
	{ "sclk",	"pll_p_out4",	108000000,	true },
	{ "hclk",	"sclk",		108000000,	true },
	{ "pclk",	"hclk",		54000000,	true },
	{ NULL,		NULL,		0,		0},
};

void __init tegra_init_cache(void)
{
#ifdef CONFIG_CACHE_L2X0
	void __iomem *p = IO_ADDRESS(TEGRA_ARM_PERIF_BASE) + 0x3000;

	writel(0x331, p + L2X0_TAG_LATENCY_CTRL);
	writel(0x441, p + L2X0_DATA_LATENCY_CTRL);

	l2x0_init(p, 0x6C080001, 0x8200c3fe);
#endif

}

void __init tegra_common_init(void)
{
	tegra_init_fuse();
	tegra_init_clock();
	tegra_clk_init_from_table(common_clk_init_table);
	tegra_init_cache();
#ifdef CONFIG_TEGRA_SYSTEM_DMA
	tegra_dma_init();
#endif
}

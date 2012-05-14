/*
 * arch/arm/mach-tegra/common.c
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
#include <linux/of_irq.h>

#include <asm/hardware/cache-l2x0.h>
#include <asm/hardware/gic.h>

#include <mach/iomap.h>
#include <mach/powergate.h>

#include "board.h"
#include "clock.h"
#include "fuse.h"
#include "pmc.h"

/*
 * Storage for debug-macro.S's state.
 *
 * This must be in .data not .bss so that it gets initialized each time the
 * kernel is loaded. The data is declared here rather than debug-macro.S so
 * that multiple inclusions of debug-macro.S point at the same data.
 */
#define TEGRA_DEBUG_UART_OFFSET (TEGRA_DEBUG_UART_BASE & 0xFFFF)
u32 tegra_uart_config[3] = {
	/* Debug UART initialization required */
	1,
	/* Debug UART physical address */
	(u32)(IO_APB_PHYS + TEGRA_DEBUG_UART_OFFSET),
	/* Debug UART virtual address */
	(u32)(IO_APB_VIRT + TEGRA_DEBUG_UART_OFFSET),
};

#ifdef CONFIG_OF
static const struct of_device_id tegra_dt_irq_match[] __initconst = {
	{ .compatible = "arm,cortex-a9-gic", .data = gic_of_init },
	{ }
};

void __init tegra_dt_init_irq(void)
{
	tegra_init_irq();
	of_irq_init(tegra_dt_irq_match);
}
#endif

void tegra_assert_system_reset(char mode, const char *cmd)
{
	void __iomem *reset = IO_ADDRESS(TEGRA_PMC_BASE + 0);
	u32 reg;

	reg = readl_relaxed(reset);
	reg |= 0x10;
	writel_relaxed(reg, reset);
}

#ifdef CONFIG_ARCH_TEGRA_2x_SOC
static __initdata struct tegra_clk_init_table tegra20_clk_init_table[] = {
	/* name		parent		rate		enabled */
	{ "clk_m",	NULL,		0,		true },
	{ "pll_p",	"clk_m",	216000000,	true },
	{ "pll_p_out1",	"pll_p",	28800000,	true },
	{ "pll_p_out2",	"pll_p",	48000000,	true },
	{ "pll_p_out3",	"pll_p",	72000000,	true },
	{ "pll_p_out4",	"pll_p",	24000000,	true },
	{ "pll_c",	"clk_m",	600000000,	true },
	{ "pll_c_out1",	"pll_c",	120000000,	true },
	{ "sclk",	"pll_c_out1",	120000000,	true },
	{ "hclk",	"sclk",		120000000,	true },
	{ "pclk",	"hclk",		60000000,	true },
	{ "csite",	NULL,		0,		true },
	{ "emc",	NULL,		0,		true },
	{ "cpu",	NULL,		0,		true },
	{ NULL,		NULL,		0,		0},
};
#endif

#ifdef CONFIG_ARCH_TEGRA_3x_SOC
static __initdata struct tegra_clk_init_table tegra30_clk_init_table[] = {
	/* name		parent		rate		enabled */
	{ "clk_m",	NULL,		0,		true },
	{ "pll_p",	"clk_m",	408000000,	true },
	{ "pll_p_out1",	"pll_p",	9600000,	true },
	{ NULL,		NULL,		0,		0},
};
#endif


static void __init tegra_init_cache(u32 tag_latency, u32 data_latency)
{
#ifdef CONFIG_CACHE_L2X0
	void __iomem *p = IO_ADDRESS(TEGRA_ARM_PERIF_BASE) + 0x3000;
	u32 aux_ctrl, cache_type;

	writel_relaxed(tag_latency, p + L2X0_TAG_LATENCY_CTRL);
	writel_relaxed(data_latency, p + L2X0_DATA_LATENCY_CTRL);

	cache_type = readl(p + L2X0_CACHE_TYPE);
	aux_ctrl = (cache_type & 0x700) << (17-8);
	aux_ctrl |= 0x6C000001;

	l2x0_init(p, aux_ctrl, 0x8200c3fe);
#endif

}

#ifdef CONFIG_ARCH_TEGRA_2x_SOC
void __init tegra20_init_early(void)
{
	tegra_init_fuse();
	tegra2_init_clocks();
	tegra_clk_init_from_table(tegra20_clk_init_table);
	tegra_init_cache(0x331, 0x441);
	tegra_pmc_init();
	tegra_powergate_init();
}
#endif
#ifdef CONFIG_ARCH_TEGRA_3x_SOC
void __init tegra30_init_early(void)
{
	tegra_init_fuse();
	tegra30_init_clocks();
	tegra_clk_init_from_table(tegra30_clk_init_table);
	tegra_init_cache(0x441, 0x551);
	tegra_pmc_init();
	tegra_powergate_init();
}
#endif

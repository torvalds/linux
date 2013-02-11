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
#include <linux/irqchip.h>

#include <asm/hardware/cache-l2x0.h>

#include <mach/powergate.h>

#include "board.h"
#include "clock.h"
#include "common.h"
#include "fuse.h"
#include "iomap.h"
#include "pmc.h"
#include "apbio.h"
#include "sleep.h"
#include "pm.h"

/*
 * Storage for debug-macro.S's state.
 *
 * This must be in .data not .bss so that it gets initialized each time the
 * kernel is loaded. The data is declared here rather than debug-macro.S so
 * that multiple inclusions of debug-macro.S point at the same data.
 */
u32 tegra_uart_config[4] = {
	/* Debug UART initialization required */
	1,
	/* Debug UART physical address */
	0,
	/* Debug UART virtual address */
	0,
	/* Scratch space for debug macro */
	0,
};

#ifdef CONFIG_OF
void __init tegra_dt_init_irq(void)
{
	tegra_init_irq();
	irqchip_init();
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
	{ "pll_p",	"pll_ref",	408000000,	true },
	{ "pll_p_out1",	"pll_p",	9600000,	true },
	{ "pll_p_out4",	"pll_p",	102000000,	true },
	{ "sclk",	"pll_p_out4",	102000000,	true },
	{ "hclk",	"sclk",		102000000,	true },
	{ "pclk",	"hclk",		51000000,	true },
	{ "csite",	NULL,		0,		true },
	{ NULL,		NULL,		0,		0},
};
#endif


static void __init tegra_init_cache(void)
{
#ifdef CONFIG_CACHE_L2X0
	int ret;
	void __iomem *p = IO_ADDRESS(TEGRA_ARM_PERIF_BASE) + 0x3000;
	u32 aux_ctrl, cache_type;

	cache_type = readl(p + L2X0_CACHE_TYPE);
	aux_ctrl = (cache_type & 0x700) << (17-8);
	aux_ctrl |= 0x7C400001;

	ret = l2x0_of_init(aux_ctrl, 0x8200c3fe);
	if (!ret)
		l2x0_saved_regs_addr = virt_to_phys(&l2x0_saved_regs);
#endif

}

#ifdef CONFIG_ARCH_TEGRA_2x_SOC
void __init tegra20_init_early(void)
{
	tegra_apb_io_init();
	tegra_init_fuse();
	tegra2_init_clocks();
	tegra_clk_init_from_table(tegra20_clk_init_table);
	tegra_init_cache();
	tegra_pmc_init();
	tegra_powergate_init();
	tegra20_hotplug_init();
}
#endif
#ifdef CONFIG_ARCH_TEGRA_3x_SOC
void __init tegra30_init_early(void)
{
	tegra_apb_io_init();
	tegra_init_fuse();
	tegra30_init_clocks();
	tegra_clk_init_from_table(tegra30_clk_init_table);
	tegra_init_cache();
	tegra_pmc_init();
	tegra_powergate_init();
	tegra30_hotplug_init();
}
#endif

void __init tegra_init_late(void)
{
	tegra_powergate_debugfs_init();
}

// SPDX-License-Identifier: GPL-2.0
/*
 * Marvell Armada 370 SoC clocks
 *
 * Copyright (C) 2012 Marvell
 *
 * Gregory CLEMENT <gregory.clement@free-electrons.com>
 * Sebastian Hesselbarth <sebastian.hesselbarth@gmail.com>
 * Andrew Lunn <andrew@lunn.ch>
 *
 */

#include <linux/kernel.h>
#include <linux/clk-provider.h>
#include <linux/io.h>
#include <linux/of.h>
#include "common.h"

/*
 * Core Clocks
 */

#define SARL				0	/* Low part [0:31] */
#define	 SARL_A370_SSCG_ENABLE		BIT(10)
#define	 SARL_A370_PCLK_FREQ_OPT	11
#define	 SARL_A370_PCLK_FREQ_OPT_MASK	0xF
#define	 SARL_A370_FAB_FREQ_OPT		15
#define	 SARL_A370_FAB_FREQ_OPT_MASK	0x1F
#define	 SARL_A370_TCLK_FREQ_OPT	20
#define	 SARL_A370_TCLK_FREQ_OPT_MASK	0x1

enum { A370_CPU_TO_NBCLK, A370_CPU_TO_HCLK, A370_CPU_TO_DRAMCLK };

static const struct coreclk_ratio a370_coreclk_ratios[] __initconst = {
	{ .id = A370_CPU_TO_NBCLK, .name = "nbclk" },
	{ .id = A370_CPU_TO_HCLK, .name = "hclk" },
	{ .id = A370_CPU_TO_DRAMCLK, .name = "dramclk" },
};

static const u32 a370_tclk_freqs[] __initconst = {
	166000000,
	200000000,
};

static u32 __init a370_get_tclk_freq(void __iomem *sar)
{
	u8 tclk_freq_select = 0;

	tclk_freq_select = ((readl(sar) >> SARL_A370_TCLK_FREQ_OPT) &
			    SARL_A370_TCLK_FREQ_OPT_MASK);
	return a370_tclk_freqs[tclk_freq_select];
}

static const u32 a370_cpu_freqs[] __initconst = {
	400000000,
	533000000,
	667000000,
	800000000,
	1000000000,
	1067000000,
	1200000000,
};

static u32 __init a370_get_cpu_freq(void __iomem *sar)
{
	u32 cpu_freq;
	u8 cpu_freq_select = 0;

	cpu_freq_select = ((readl(sar) >> SARL_A370_PCLK_FREQ_OPT) &
			   SARL_A370_PCLK_FREQ_OPT_MASK);
	if (cpu_freq_select >= ARRAY_SIZE(a370_cpu_freqs)) {
		pr_err("CPU freq select unsupported %d\n", cpu_freq_select);
		cpu_freq = 0;
	} else
		cpu_freq = a370_cpu_freqs[cpu_freq_select];

	return cpu_freq;
}

static const int a370_nbclk_ratios[32][2] __initconst = {
	{0, 1}, {1, 2}, {2, 2}, {2, 2},
	{1, 2}, {1, 2}, {1, 1}, {2, 3},
	{0, 1}, {1, 2}, {2, 4}, {0, 1},
	{1, 2}, {0, 1}, {0, 1}, {2, 2},
	{0, 1}, {0, 1}, {0, 1}, {1, 1},
	{2, 3}, {0, 1}, {0, 1}, {0, 1},
	{0, 1}, {0, 1}, {0, 1}, {1, 1},
	{0, 1}, {0, 1}, {0, 1}, {0, 1},
};

static const int a370_hclk_ratios[32][2] __initconst = {
	{0, 1}, {1, 2}, {2, 6}, {2, 3},
	{1, 3}, {1, 4}, {1, 2}, {2, 6},
	{0, 1}, {1, 6}, {2, 10}, {0, 1},
	{1, 4}, {0, 1}, {0, 1}, {2, 5},
	{0, 1}, {0, 1}, {0, 1}, {1, 2},
	{2, 6}, {0, 1}, {0, 1}, {0, 1},
	{0, 1}, {0, 1}, {0, 1}, {1, 1},
	{0, 1}, {0, 1}, {0, 1}, {0, 1},
};

static const int a370_dramclk_ratios[32][2] __initconst = {
	{0, 1}, {1, 2}, {2, 3}, {2, 3},
	{1, 3}, {1, 2}, {1, 2}, {2, 6},
	{0, 1}, {1, 3}, {2, 5}, {0, 1},
	{1, 4}, {0, 1}, {0, 1}, {2, 5},
	{0, 1}, {0, 1}, {0, 1}, {1, 1},
	{2, 3}, {0, 1}, {0, 1}, {0, 1},
	{0, 1}, {0, 1}, {0, 1}, {1, 1},
	{0, 1}, {0, 1}, {0, 1}, {0, 1},
};

static void __init a370_get_clk_ratio(
	void __iomem *sar, int id, int *mult, int *div)
{
	u32 opt = ((readl(sar) >> SARL_A370_FAB_FREQ_OPT) &
		SARL_A370_FAB_FREQ_OPT_MASK);

	switch (id) {
	case A370_CPU_TO_NBCLK:
		*mult = a370_nbclk_ratios[opt][0];
		*div = a370_nbclk_ratios[opt][1];
		break;
	case A370_CPU_TO_HCLK:
		*mult = a370_hclk_ratios[opt][0];
		*div = a370_hclk_ratios[opt][1];
		break;
	case A370_CPU_TO_DRAMCLK:
		*mult = a370_dramclk_ratios[opt][0];
		*div = a370_dramclk_ratios[opt][1];
		break;
	}
}

static bool a370_is_sscg_enabled(void __iomem *sar)
{
	return !(readl(sar) & SARL_A370_SSCG_ENABLE);
}

static const struct coreclk_soc_desc a370_coreclks = {
	.get_tclk_freq = a370_get_tclk_freq,
	.get_cpu_freq = a370_get_cpu_freq,
	.get_clk_ratio = a370_get_clk_ratio,
	.is_sscg_enabled = a370_is_sscg_enabled,
	.fix_sscg_deviation = kirkwood_fix_sscg_deviation,
	.ratios = a370_coreclk_ratios,
	.num_ratios = ARRAY_SIZE(a370_coreclk_ratios),
};

/*
 * Clock Gating Control
 */

static const struct clk_gating_soc_desc a370_gating_desc[] __initconst = {
	{ "audio", NULL, 0, 0 },
	{ "pex0_en", NULL, 1, 0 },
	{ "pex1_en", NULL,  2, 0 },
	{ "ge1", NULL, 3, 0 },
	{ "ge0", NULL, 4, 0 },
	{ "pex0", "pex0_en", 5, 0 },
	{ "pex1", "pex1_en", 9, 0 },
	{ "sata0", NULL, 15, 0 },
	{ "sdio", NULL, 17, 0 },
	{ "crypto", NULL, 23, CLK_IGNORE_UNUSED },
	{ "tdm", NULL, 25, 0 },
	{ "ddr", NULL, 28, CLK_IGNORE_UNUSED },
	{ "sata1", NULL, 30, 0 },
	{ }
};

static void __init a370_clk_init(struct device_node *np)
{
	struct device_node *cgnp =
		of_find_compatible_node(NULL, NULL, "marvell,armada-370-gating-clock");

	mvebu_coreclk_setup(np, &a370_coreclks);

	if (cgnp) {
		mvebu_clk_gating_setup(cgnp, a370_gating_desc);
		of_node_put(cgnp);
	}
}
CLK_OF_DECLARE(a370_clk, "marvell,armada-370-core-clock", a370_clk_init);


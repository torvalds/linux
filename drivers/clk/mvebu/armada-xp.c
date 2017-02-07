/*
 * Marvell Armada XP SoC clocks
 *
 * Copyright (C) 2012 Marvell
 *
 * Gregory CLEMENT <gregory.clement@free-electrons.com>
 * Sebastian Hesselbarth <sebastian.hesselbarth@gmail.com>
 * Andrew Lunn <andrew@lunn.ch>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/kernel.h>
#include <linux/clk-provider.h>
#include <linux/io.h>
#include <linux/of.h>
#include "common.h"

/*
 * Core Clocks
 *
 * Armada XP Sample At Reset is a 64 bit bitfiled split in two
 * register of 32 bits
 */

#define SARL				0	/* Low part [0:31] */
#define	 SARL_AXP_PCLK_FREQ_OPT		21
#define	 SARL_AXP_PCLK_FREQ_OPT_MASK	0x7
#define	 SARL_AXP_FAB_FREQ_OPT		24
#define	 SARL_AXP_FAB_FREQ_OPT_MASK	0xF
#define SARH				4	/* High part [32:63] */
#define	 SARH_AXP_PCLK_FREQ_OPT		(52-32)
#define	 SARH_AXP_PCLK_FREQ_OPT_MASK	0x1
#define	 SARH_AXP_PCLK_FREQ_OPT_SHIFT	3
#define	 SARH_AXP_FAB_FREQ_OPT		(51-32)
#define	 SARH_AXP_FAB_FREQ_OPT_MASK	0x1
#define	 SARH_AXP_FAB_FREQ_OPT_SHIFT	4

enum { AXP_CPU_TO_NBCLK, AXP_CPU_TO_HCLK, AXP_CPU_TO_DRAMCLK };

static const struct coreclk_ratio axp_coreclk_ratios[] __initconst = {
	{ .id = AXP_CPU_TO_NBCLK, .name = "nbclk" },
	{ .id = AXP_CPU_TO_HCLK, .name = "hclk" },
	{ .id = AXP_CPU_TO_DRAMCLK, .name = "dramclk" },
};

/* Armada XP TCLK frequency is fixed to 250MHz */
static u32 __init axp_get_tclk_freq(void __iomem *sar)
{
	return 250000000;
}

/* MV98DX3236 TCLK frequency is fixed to 200MHz */
static u32 __init mv98dx3236_get_tclk_freq(void __iomem *sar)
{
	return 200000000;
}

static const u32 axp_cpu_freqs[] __initconst = {
	1000000000,
	1066000000,
	1200000000,
	1333000000,
	1500000000,
	1666000000,
	1800000000,
	2000000000,
	667000000,
	0,
	800000000,
	1600000000,
};

static u32 __init axp_get_cpu_freq(void __iomem *sar)
{
	u32 cpu_freq;
	u8 cpu_freq_select = 0;

	cpu_freq_select = ((readl(sar + SARL) >> SARL_AXP_PCLK_FREQ_OPT) &
			   SARL_AXP_PCLK_FREQ_OPT_MASK);
	/*
	 * The upper bit is not contiguous to the other ones and
	 * located in the high part of the SAR registers
	 */
	cpu_freq_select |= (((readl(sar + SARH) >> SARH_AXP_PCLK_FREQ_OPT) &
	     SARH_AXP_PCLK_FREQ_OPT_MASK) << SARH_AXP_PCLK_FREQ_OPT_SHIFT);
	if (cpu_freq_select >= ARRAY_SIZE(axp_cpu_freqs)) {
		pr_err("CPU freq select unsupported: %d\n", cpu_freq_select);
		cpu_freq = 0;
	} else
		cpu_freq = axp_cpu_freqs[cpu_freq_select];

	return cpu_freq;
}

/* MV98DX3236 CLK frequency is fixed to 800MHz */
static u32 __init mv98dx3236_get_cpu_freq(void __iomem *sar)
{
	return 800000000;
}

static const int axp_nbclk_ratios[32][2] __initconst = {
	{0, 1}, {1, 2}, {2, 2}, {2, 2},
	{1, 2}, {1, 2}, {1, 1}, {2, 3},
	{0, 1}, {1, 2}, {2, 4}, {0, 1},
	{1, 2}, {0, 1}, {0, 1}, {2, 2},
	{0, 1}, {0, 1}, {0, 1}, {1, 1},
	{2, 3}, {0, 1}, {0, 1}, {0, 1},
	{0, 1}, {0, 1}, {0, 1}, {1, 1},
	{0, 1}, {0, 1}, {0, 1}, {0, 1},
};

static const int axp_hclk_ratios[32][2] __initconst = {
	{0, 1}, {1, 2}, {2, 6}, {2, 3},
	{1, 3}, {1, 4}, {1, 2}, {2, 6},
	{0, 1}, {1, 6}, {2, 10}, {0, 1},
	{1, 4}, {0, 1}, {0, 1}, {2, 5},
	{0, 1}, {0, 1}, {0, 1}, {1, 2},
	{2, 6}, {0, 1}, {0, 1}, {0, 1},
	{0, 1}, {0, 1}, {0, 1}, {1, 1},
	{0, 1}, {0, 1}, {0, 1}, {0, 1},
};

static const int axp_dramclk_ratios[32][2] __initconst = {
	{0, 1}, {1, 2}, {2, 3}, {2, 3},
	{1, 3}, {1, 2}, {1, 2}, {2, 6},
	{0, 1}, {1, 3}, {2, 5}, {0, 1},
	{1, 4}, {0, 1}, {0, 1}, {2, 5},
	{0, 1}, {0, 1}, {0, 1}, {1, 1},
	{2, 3}, {0, 1}, {0, 1}, {0, 1},
	{0, 1}, {0, 1}, {0, 1}, {1, 1},
	{0, 1}, {0, 1}, {0, 1}, {0, 1},
};

static void __init axp_get_clk_ratio(
	void __iomem *sar, int id, int *mult, int *div)
{
	u32 opt = ((readl(sar + SARL) >> SARL_AXP_FAB_FREQ_OPT) &
	      SARL_AXP_FAB_FREQ_OPT_MASK);
	/*
	 * The upper bit is not contiguous to the other ones and
	 * located in the high part of the SAR registers
	 */
	opt |= (((readl(sar + SARH) >> SARH_AXP_FAB_FREQ_OPT) &
		 SARH_AXP_FAB_FREQ_OPT_MASK) << SARH_AXP_FAB_FREQ_OPT_SHIFT);

	switch (id) {
	case AXP_CPU_TO_NBCLK:
		*mult = axp_nbclk_ratios[opt][0];
		*div = axp_nbclk_ratios[opt][1];
		break;
	case AXP_CPU_TO_HCLK:
		*mult = axp_hclk_ratios[opt][0];
		*div = axp_hclk_ratios[opt][1];
		break;
	case AXP_CPU_TO_DRAMCLK:
		*mult = axp_dramclk_ratios[opt][0];
		*div = axp_dramclk_ratios[opt][1];
		break;
	}
}

static const struct coreclk_soc_desc axp_coreclks = {
	.get_tclk_freq = axp_get_tclk_freq,
	.get_cpu_freq = axp_get_cpu_freq,
	.get_clk_ratio = axp_get_clk_ratio,
	.ratios = axp_coreclk_ratios,
	.num_ratios = ARRAY_SIZE(axp_coreclk_ratios),
};

static const struct coreclk_soc_desc mv98dx3236_coreclks = {
	.get_tclk_freq = mv98dx3236_get_tclk_freq,
	.get_cpu_freq = mv98dx3236_get_cpu_freq,
};

/*
 * Clock Gating Control
 */

static const struct clk_gating_soc_desc axp_gating_desc[] __initconst = {
	{ "audio", NULL, 0, 0 },
	{ "ge3", NULL, 1, 0 },
	{ "ge2", NULL,  2, 0 },
	{ "ge1", NULL, 3, 0 },
	{ "ge0", NULL, 4, 0 },
	{ "pex00", NULL, 5, 0 },
	{ "pex01", NULL, 6, 0 },
	{ "pex02", NULL, 7, 0 },
	{ "pex03", NULL, 8, 0 },
	{ "pex10", NULL, 9, 0 },
	{ "pex11", NULL, 10, 0 },
	{ "pex12", NULL, 11, 0 },
	{ "pex13", NULL, 12, 0 },
	{ "bp", NULL, 13, 0 },
	{ "sata0lnk", NULL, 14, 0 },
	{ "sata0", "sata0lnk", 15, 0 },
	{ "lcd", NULL, 16, 0 },
	{ "sdio", NULL, 17, 0 },
	{ "usb0", NULL, 18, 0 },
	{ "usb1", NULL, 19, 0 },
	{ "usb2", NULL, 20, 0 },
	{ "xor0", NULL, 22, 0 },
	{ "crypto", NULL, 23, 0 },
	{ "tdm", NULL, 25, 0 },
	{ "pex20", NULL, 26, 0 },
	{ "pex30", NULL, 27, 0 },
	{ "xor1", NULL, 28, 0 },
	{ "sata1lnk", NULL, 29, 0 },
	{ "sata1", "sata1lnk", 30, 0 },
	{ }
};

static const struct clk_gating_soc_desc mv98dx3236_gating_desc[] __initconst = {
	{ "ge1", NULL, 3, 0 },
	{ "ge0", NULL, 4, 0 },
	{ "pex00", NULL, 5, 0 },
	{ "sdio", NULL, 17, 0 },
	{ "xor0", NULL, 22, 0 },
	{ }
};

static void __init axp_clk_init(struct device_node *np)
{
	struct device_node *cgnp =
		of_find_compatible_node(NULL, NULL, "marvell,armada-xp-gating-clock");

	mvebu_coreclk_setup(np, &axp_coreclks);

	if (cgnp)
		mvebu_clk_gating_setup(cgnp, axp_gating_desc);
}
CLK_OF_DECLARE(axp_clk, "marvell,armada-xp-core-clock", axp_clk_init);

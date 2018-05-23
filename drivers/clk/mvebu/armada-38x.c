/*
 * Marvell Armada 380/385 SoC clocks
 *
 * Copyright (C) 2014 Marvell
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
 * SAR[14:10] : Ratios between PCLK0, NBCLK, HCLK and DRAM clocks
 *
 * SAR[15]    : TCLK frequency
 *		 0 = 250 MHz
 *		 1 = 200 MHz
 */

#define SAR_A380_TCLK_FREQ_OPT		  15
#define SAR_A380_TCLK_FREQ_OPT_MASK	  0x1
#define SAR_A380_CPU_DDR_L2_FREQ_OPT	  10
#define SAR_A380_CPU_DDR_L2_FREQ_OPT_MASK 0x1F

static const u32 armada_38x_tclk_frequencies[] __initconst = {
	250000000,
	200000000,
};

static u32 __init armada_38x_get_tclk_freq(void __iomem *sar)
{
	u8 tclk_freq_select;

	tclk_freq_select = ((readl(sar) >> SAR_A380_TCLK_FREQ_OPT) &
			    SAR_A380_TCLK_FREQ_OPT_MASK);
	return armada_38x_tclk_frequencies[tclk_freq_select];
}

static const u32 armada_38x_cpu_frequencies[] __initconst = {
	666 * 1000 * 1000,  0, 800 * 1000 * 1000, 0,
	1066 * 1000 * 1000, 0, 1200 * 1000 * 1000, 0,
	1332 * 1000 * 1000, 0, 0, 0,
	1600 * 1000 * 1000, 0, 0, 0,
	1866 * 1000 * 1000, 0, 0, 2000 * 1000 * 1000,
};

static u32 __init armada_38x_get_cpu_freq(void __iomem *sar)
{
	u8 cpu_freq_select;

	cpu_freq_select = ((readl(sar) >> SAR_A380_CPU_DDR_L2_FREQ_OPT) &
			   SAR_A380_CPU_DDR_L2_FREQ_OPT_MASK);
	if (cpu_freq_select >= ARRAY_SIZE(armada_38x_cpu_frequencies)) {
		pr_err("Selected CPU frequency (%d) unsupported\n",
			cpu_freq_select);
		return 0;
	}

	return armada_38x_cpu_frequencies[cpu_freq_select];
}

enum { A380_CPU_TO_DDR, A380_CPU_TO_L2 };

static const struct coreclk_ratio armada_38x_coreclk_ratios[] __initconst = {
	{ .id = A380_CPU_TO_L2,	 .name = "l2clk" },
	{ .id = A380_CPU_TO_DDR, .name = "ddrclk" },
};

static const int armada_38x_cpu_l2_ratios[32][2] __initconst = {
	{1, 2}, {0, 1}, {1, 2}, {0, 1},
	{1, 2}, {0, 1}, {1, 2}, {0, 1},
	{1, 2}, {0, 1}, {0, 1}, {0, 1},
	{1, 2}, {0, 1}, {0, 1}, {0, 1},
	{1, 2}, {0, 1}, {0, 1}, {1, 2},
	{0, 1}, {0, 1}, {0, 1}, {0, 1},
	{0, 1}, {0, 1}, {0, 1}, {0, 1},
	{0, 1}, {0, 1}, {0, 1}, {0, 1},
};

static const int armada_38x_cpu_ddr_ratios[32][2] __initconst = {
	{0, 1}, {0, 1}, {0, 1}, {0, 1},
	{1, 2}, {0, 1}, {0, 1}, {0, 1},
	{1, 2}, {0, 1}, {0, 1}, {0, 1},
	{1, 2}, {0, 1}, {0, 1}, {0, 1},
	{1, 2}, {0, 1}, {0, 1}, {7, 15},
	{0, 1}, {0, 1}, {0, 1}, {0, 1},
	{0, 1}, {0, 1}, {0, 1}, {0, 1},
	{0, 1}, {0, 1}, {0, 1}, {0, 1},
};

static void __init armada_38x_get_clk_ratio(
	void __iomem *sar, int id, int *mult, int *div)
{
	u32 opt = ((readl(sar) >> SAR_A380_CPU_DDR_L2_FREQ_OPT) &
		SAR_A380_CPU_DDR_L2_FREQ_OPT_MASK);

	switch (id) {
	case A380_CPU_TO_L2:
		*mult = armada_38x_cpu_l2_ratios[opt][0];
		*div = armada_38x_cpu_l2_ratios[opt][1];
		break;
	case A380_CPU_TO_DDR:
		*mult = armada_38x_cpu_ddr_ratios[opt][0];
		*div = armada_38x_cpu_ddr_ratios[opt][1];
		break;
	}
}

static const struct coreclk_soc_desc armada_38x_coreclks = {
	.get_tclk_freq = armada_38x_get_tclk_freq,
	.get_cpu_freq = armada_38x_get_cpu_freq,
	.get_clk_ratio = armada_38x_get_clk_ratio,
	.ratios = armada_38x_coreclk_ratios,
	.num_ratios = ARRAY_SIZE(armada_38x_coreclk_ratios),
};

static void __init armada_38x_coreclk_init(struct device_node *np)
{
	mvebu_coreclk_setup(np, &armada_38x_coreclks);
}
CLK_OF_DECLARE(armada_38x_core_clk, "marvell,armada-380-core-clock",
	       armada_38x_coreclk_init);

/*
 * Clock Gating Control
 */
static const struct clk_gating_soc_desc armada_38x_gating_desc[] __initconst = {
	{ "audio", NULL, 0 },
	{ "ge2", NULL, 2 },
	{ "ge1", NULL, 3 },
	{ "ge0", NULL, 4 },
	{ "pex1", NULL, 5 },
	{ "pex2", NULL, 6 },
	{ "pex3", NULL, 7 },
	{ "pex0", NULL, 8 },
	{ "usb3h0", NULL, 9 },
	{ "usb3h1", NULL, 10 },
	{ "usb3d", NULL, 11 },
	{ "bm", NULL, 13 },
	{ "crypto0z", NULL, 14 },
	{ "sata0", NULL, 15 },
	{ "crypto1z", NULL, 16 },
	{ "sdio", NULL, 17 },
	{ "usb2", NULL, 18 },
	{ "crypto1", NULL, 21 },
	{ "xor0", NULL, 22 },
	{ "crypto0", NULL, 23 },
	{ "tdm", NULL, 25 },
	{ "xor1", NULL, 28 },
	{ "sata1", NULL, 30 },
	{ }
};

static void __init armada_38x_clk_gating_init(struct device_node *np)
{
	mvebu_clk_gating_setup(np, armada_38x_gating_desc);
}
CLK_OF_DECLARE(armada_38x_clk_gating, "marvell,armada-380-gating-clock",
	       armada_38x_clk_gating_init);

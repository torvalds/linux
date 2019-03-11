// SPDX-License-Identifier: GPL-2.0
/*
 * Marvell Armada 39x SoC clocks
 *
 * Copyright (C) 2015 Marvell
 *
 * Gregory CLEMENT <gregory.clement@free-electrons.com>
 * Sebastian Hesselbarth <sebastian.hesselbarth@gmail.com>
 * Andrew Lunn <andrew@lunn.ch>
 * Thomas Petazzoni <thomas.petazzoni@free-electrons.com>
 *
 */

#include <linux/kernel.h>
#include <linux/clk-provider.h>
#include <linux/io.h>
#include <linux/of.h>
#include "common.h"

/*
 * SARL[14:10] : Ratios between CPU, NBCLK, HCLK and DCLK.
 *
 * SARL[15]    : TCLK frequency
 *		 0 = 250 MHz
 *		 1 = 200 MHz
 *
 * SARH[0]     : Reference clock frequency
 *               0 = 25 Mhz
 *               1 = 40 Mhz
 */

#define SARL 					0
#define  SARL_A390_TCLK_FREQ_OPT		15
#define  SARL_A390_TCLK_FREQ_OPT_MASK		0x1
#define  SARL_A390_CPU_DDR_L2_FREQ_OPT		10
#define  SARL_A390_CPU_DDR_L2_FREQ_OPT_MASK	0x1F
#define SARH					4
#define  SARH_A390_REFCLK_FREQ			BIT(0)

static const u32 armada_39x_tclk_frequencies[] __initconst = {
	250000000,
	200000000,
};

static u32 __init armada_39x_get_tclk_freq(void __iomem *sar)
{
	u8 tclk_freq_select;

	tclk_freq_select = ((readl(sar + SARL) >> SARL_A390_TCLK_FREQ_OPT) &
			    SARL_A390_TCLK_FREQ_OPT_MASK);
	return armada_39x_tclk_frequencies[tclk_freq_select];
}

static const u32 armada_39x_cpu_frequencies[] __initconst = {
	[0x0] = 666 * 1000 * 1000,
	[0x2] = 800 * 1000 * 1000,
	[0x3] = 800 * 1000 * 1000,
	[0x4] = 1066 * 1000 * 1000,
	[0x5] = 1066 * 1000 * 1000,
	[0x6] = 1200 * 1000 * 1000,
	[0x8] = 1332 * 1000 * 1000,
	[0xB] = 1600 * 1000 * 1000,
	[0xC] = 1600 * 1000 * 1000,
	[0x12] = 1800 * 1000 * 1000,
	[0x1E] = 1800 * 1000 * 1000,
};

static u32 __init armada_39x_get_cpu_freq(void __iomem *sar)
{
	u8 cpu_freq_select;

	cpu_freq_select = ((readl(sar + SARL) >> SARL_A390_CPU_DDR_L2_FREQ_OPT) &
			   SARL_A390_CPU_DDR_L2_FREQ_OPT_MASK);
	if (cpu_freq_select >= ARRAY_SIZE(armada_39x_cpu_frequencies)) {
		pr_err("Selected CPU frequency (%d) unsupported\n",
			cpu_freq_select);
		return 0;
	}

	return armada_39x_cpu_frequencies[cpu_freq_select];
}

enum { A390_CPU_TO_NBCLK, A390_CPU_TO_HCLK, A390_CPU_TO_DCLK };

static const struct coreclk_ratio armada_39x_coreclk_ratios[] __initconst = {
	{ .id = A390_CPU_TO_NBCLK, .name = "nbclk" },
	{ .id = A390_CPU_TO_HCLK, .name = "hclk" },
	{ .id = A390_CPU_TO_DCLK, .name = "dclk" },
};

static void __init armada_39x_get_clk_ratio(
	void __iomem *sar, int id, int *mult, int *div)
{
	switch (id) {
	case A390_CPU_TO_NBCLK:
		*mult = 1;
		*div = 2;
		break;
	case A390_CPU_TO_HCLK:
		*mult = 1;
		*div = 4;
		break;
	case A390_CPU_TO_DCLK:
		*mult = 1;
		*div = 2;
		break;
	}
}

static u32 __init armada_39x_refclk_ratio(void __iomem *sar)
{
	if (readl(sar + SARH) & SARH_A390_REFCLK_FREQ)
		return 40 * 1000 * 1000;
	else
		return 25 * 1000 * 1000;
}

static const struct coreclk_soc_desc armada_39x_coreclks = {
	.get_tclk_freq = armada_39x_get_tclk_freq,
	.get_cpu_freq = armada_39x_get_cpu_freq,
	.get_clk_ratio = armada_39x_get_clk_ratio,
	.get_refclk_freq = armada_39x_refclk_ratio,
	.ratios = armada_39x_coreclk_ratios,
	.num_ratios = ARRAY_SIZE(armada_39x_coreclk_ratios),
};

static void __init armada_39x_coreclk_init(struct device_node *np)
{
	mvebu_coreclk_setup(np, &armada_39x_coreclks);
}
CLK_OF_DECLARE(armada_39x_core_clk, "marvell,armada-390-core-clock",
	       armada_39x_coreclk_init);

/*
 * Clock Gating Control
 */
static const struct clk_gating_soc_desc armada_39x_gating_desc[] __initconst = {
	{ "pex1", NULL, 5 },
	{ "pex2", NULL, 6 },
	{ "pex3", NULL, 7 },
	{ "pex0", NULL, 8 },
	{ "usb3h0", NULL, 9 },
	{ "usb3h1", NULL, 10 },
	{ "sata0", NULL, 15 },
	{ "sdio", NULL, 17 },
	{ "xor0", NULL, 22 },
	{ "xor1", NULL, 28 },
	{ }
};

static void __init armada_39x_clk_gating_init(struct device_node *np)
{
	mvebu_clk_gating_setup(np, armada_39x_gating_desc);
}
CLK_OF_DECLARE(armada_39x_clk_gating, "marvell,armada-390-gating-clock",
	       armada_39x_clk_gating_init);

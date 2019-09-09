// SPDX-License-Identifier: GPL-2.0
/*
 * Marvell Kirkwood SoC clocks
 *
 * Copyright (C) 2012 Marvell
 *
 * Gregory CLEMENT <gregory.clement@free-electrons.com>
 * Sebastian Hesselbarth <sebastian.hesselbarth@gmail.com>
 * Andrew Lunn <andrew@lunn.ch>
 *
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/clk-provider.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include "common.h"

/*
 * Core Clocks
 *
 * Kirkwood PLL sample-at-reset configuration
 * (6180 has different SAR layout than other Kirkwood SoCs)
 *
 * SAR0[4:3,22,1] : CPU frequency (6281,6292,6282)
 *	4  =  600 MHz
 *	6  =  800 MHz
 *	7  = 1000 MHz
 *	9  = 1200 MHz
 *	12 = 1500 MHz
 *	13 = 1600 MHz
 *	14 = 1800 MHz
 *	15 = 2000 MHz
 *	others reserved.
 *
 * SAR0[19,10:9] : CPU to L2 Clock divider ratio (6281,6292,6282)
 *	1 = (1/2) * CPU
 *	3 = (1/3) * CPU
 *	5 = (1/4) * CPU
 *	others reserved.
 *
 * SAR0[8:5] : CPU to DDR DRAM Clock divider ratio (6281,6292,6282)
 *	2 = (1/2) * CPU
 *	4 = (1/3) * CPU
 *	6 = (1/4) * CPU
 *	7 = (2/9) * CPU
 *	8 = (1/5) * CPU
 *	9 = (1/6) * CPU
 *	others reserved.
 *
 * SAR0[4:2] : Kirkwood 6180 cpu/l2/ddr clock configuration (6180 only)
 *	5 = [CPU =  600 MHz, L2 = (1/2) * CPU, DDR = 200 MHz = (1/3) * CPU]
 *	6 = [CPU =  800 MHz, L2 = (1/2) * CPU, DDR = 200 MHz = (1/4) * CPU]
 *	7 = [CPU = 1000 MHz, L2 = (1/2) * CPU, DDR = 200 MHz = (1/5) * CPU]
 *	others reserved.
 *
 * SAR0[21] : TCLK frequency
 *	0 = 200 MHz
 *	1 = 166 MHz
 *	others reserved.
 */

#define SAR_KIRKWOOD_CPU_FREQ(x)	\
	(((x & (1 <<  1)) >>  1) |	\
	 ((x & (1 << 22)) >> 21) |	\
	 ((x & (3 <<  3)) >>  1))
#define SAR_KIRKWOOD_L2_RATIO(x)	\
	(((x & (3 <<  9)) >> 9) |	\
	 (((x & (1 << 19)) >> 17)))
#define SAR_KIRKWOOD_DDR_RATIO		5
#define SAR_KIRKWOOD_DDR_RATIO_MASK	0xf
#define SAR_MV88F6180_CLK		2
#define SAR_MV88F6180_CLK_MASK		0x7
#define SAR_KIRKWOOD_TCLK_FREQ		21
#define SAR_KIRKWOOD_TCLK_FREQ_MASK	0x1

enum { KIRKWOOD_CPU_TO_L2, KIRKWOOD_CPU_TO_DDR };

static const struct coreclk_ratio kirkwood_coreclk_ratios[] __initconst = {
	{ .id = KIRKWOOD_CPU_TO_L2, .name = "l2clk", },
	{ .id = KIRKWOOD_CPU_TO_DDR, .name = "ddrclk", }
};

static u32 __init kirkwood_get_tclk_freq(void __iomem *sar)
{
	u32 opt = (readl(sar) >> SAR_KIRKWOOD_TCLK_FREQ) &
		SAR_KIRKWOOD_TCLK_FREQ_MASK;
	return (opt) ? 166666667 : 200000000;
}

static const u32 kirkwood_cpu_freqs[] __initconst = {
	0, 0, 0, 0,
	600000000,
	0,
	800000000,
	1000000000,
	0,
	1200000000,
	0, 0,
	1500000000,
	1600000000,
	1800000000,
	2000000000
};

static u32 __init kirkwood_get_cpu_freq(void __iomem *sar)
{
	u32 opt = SAR_KIRKWOOD_CPU_FREQ(readl(sar));
	return kirkwood_cpu_freqs[opt];
}

static const int kirkwood_cpu_l2_ratios[8][2] __initconst = {
	{ 0, 1 }, { 1, 2 }, { 0, 1 }, { 1, 3 },
	{ 0, 1 }, { 1, 4 }, { 0, 1 }, { 0, 1 }
};

static const int kirkwood_cpu_ddr_ratios[16][2] __initconst = {
	{ 0, 1 }, { 0, 1 }, { 1, 2 }, { 0, 1 },
	{ 1, 3 }, { 0, 1 }, { 1, 4 }, { 2, 9 },
	{ 1, 5 }, { 1, 6 }, { 0, 1 }, { 0, 1 },
	{ 0, 1 }, { 0, 1 }, { 0, 1 }, { 0, 1 }
};

static void __init kirkwood_get_clk_ratio(
	void __iomem *sar, int id, int *mult, int *div)
{
	switch (id) {
	case KIRKWOOD_CPU_TO_L2:
	{
		u32 opt = SAR_KIRKWOOD_L2_RATIO(readl(sar));
		*mult = kirkwood_cpu_l2_ratios[opt][0];
		*div = kirkwood_cpu_l2_ratios[opt][1];
		break;
	}
	case KIRKWOOD_CPU_TO_DDR:
	{
		u32 opt = (readl(sar) >> SAR_KIRKWOOD_DDR_RATIO) &
			SAR_KIRKWOOD_DDR_RATIO_MASK;
		*mult = kirkwood_cpu_ddr_ratios[opt][0];
		*div = kirkwood_cpu_ddr_ratios[opt][1];
		break;
	}
	}
}

static const u32 mv88f6180_cpu_freqs[] __initconst = {
	0, 0, 0, 0, 0,
	600000000,
	800000000,
	1000000000
};

static u32 __init mv88f6180_get_cpu_freq(void __iomem *sar)
{
	u32 opt = (readl(sar) >> SAR_MV88F6180_CLK) & SAR_MV88F6180_CLK_MASK;
	return mv88f6180_cpu_freqs[opt];
}

static const int mv88f6180_cpu_ddr_ratios[8][2] __initconst = {
	{ 0, 1 }, { 0, 1 }, { 0, 1 }, { 0, 1 },
	{ 0, 1 }, { 1, 3 }, { 1, 4 }, { 1, 5 }
};

static void __init mv88f6180_get_clk_ratio(
	void __iomem *sar, int id, int *mult, int *div)
{
	switch (id) {
	case KIRKWOOD_CPU_TO_L2:
	{
		/* mv88f6180 has a fixed 1:2 CPU-to-L2 ratio */
		*mult = 1;
		*div = 2;
		break;
	}
	case KIRKWOOD_CPU_TO_DDR:
	{
		u32 opt = (readl(sar) >> SAR_MV88F6180_CLK) &
			SAR_MV88F6180_CLK_MASK;
		*mult = mv88f6180_cpu_ddr_ratios[opt][0];
		*div = mv88f6180_cpu_ddr_ratios[opt][1];
		break;
	}
	}
}

static u32 __init mv98dx1135_get_tclk_freq(void __iomem *sar)
{
	return 166666667;
}

static const struct coreclk_soc_desc kirkwood_coreclks = {
	.get_tclk_freq = kirkwood_get_tclk_freq,
	.get_cpu_freq = kirkwood_get_cpu_freq,
	.get_clk_ratio = kirkwood_get_clk_ratio,
	.ratios = kirkwood_coreclk_ratios,
	.num_ratios = ARRAY_SIZE(kirkwood_coreclk_ratios),
};

static const struct coreclk_soc_desc mv88f6180_coreclks = {
	.get_tclk_freq = kirkwood_get_tclk_freq,
	.get_cpu_freq = mv88f6180_get_cpu_freq,
	.get_clk_ratio = mv88f6180_get_clk_ratio,
	.ratios = kirkwood_coreclk_ratios,
	.num_ratios = ARRAY_SIZE(kirkwood_coreclk_ratios),
};

static const struct coreclk_soc_desc mv98dx1135_coreclks = {
	.get_tclk_freq = mv98dx1135_get_tclk_freq,
	.get_cpu_freq = kirkwood_get_cpu_freq,
	.get_clk_ratio = kirkwood_get_clk_ratio,
	.ratios = kirkwood_coreclk_ratios,
	.num_ratios = ARRAY_SIZE(kirkwood_coreclk_ratios),
};

/*
 * Clock Gating Control
 */

static const struct clk_gating_soc_desc kirkwood_gating_desc[] __initconst = {
	{ "ge0", NULL, 0, 0 },
	{ "pex0", NULL, 2, 0 },
	{ "usb0", NULL, 3, 0 },
	{ "sdio", NULL, 4, 0 },
	{ "tsu", NULL, 5, 0 },
	{ "runit", NULL, 7, 0 },
	{ "xor0", NULL, 8, 0 },
	{ "audio", NULL, 9, 0 },
	{ "sata0", NULL, 14, 0 },
	{ "sata1", NULL, 15, 0 },
	{ "xor1", NULL, 16, 0 },
	{ "crypto", NULL, 17, 0 },
	{ "pex1", NULL, 18, 0 },
	{ "ge1", NULL, 19, 0 },
	{ "tdm", NULL, 20, 0 },
	{ }
};


/*
 * Clock Muxing Control
 */

struct clk_muxing_soc_desc {
	const char *name;
	const char **parents;
	int num_parents;
	int shift;
	int width;
	unsigned long flags;
};

struct clk_muxing_ctrl {
	spinlock_t *lock;
	struct clk **muxes;
	int num_muxes;
};

static const char *powersave_parents[] = {
	"cpuclk",
	"ddrclk",
};

static const struct clk_muxing_soc_desc kirkwood_mux_desc[] __initconst = {
	{ "powersave", powersave_parents, ARRAY_SIZE(powersave_parents),
		11, 1, 0 },
};

static struct clk *clk_muxing_get_src(
	struct of_phandle_args *clkspec, void *data)
{
	struct clk_muxing_ctrl *ctrl = (struct clk_muxing_ctrl *)data;
	int n;

	if (clkspec->args_count < 1)
		return ERR_PTR(-EINVAL);

	for (n = 0; n < ctrl->num_muxes; n++) {
		struct clk_mux *mux =
			to_clk_mux(__clk_get_hw(ctrl->muxes[n]));
		if (clkspec->args[0] == mux->shift)
			return ctrl->muxes[n];
	}
	return ERR_PTR(-ENODEV);
}

static void __init kirkwood_clk_muxing_setup(struct device_node *np,
				   const struct clk_muxing_soc_desc *desc)
{
	struct clk_muxing_ctrl *ctrl;
	void __iomem *base;
	int n;

	base = of_iomap(np, 0);
	if (WARN_ON(!base))
		return;

	ctrl = kzalloc(sizeof(*ctrl), GFP_KERNEL);
	if (WARN_ON(!ctrl))
		goto ctrl_out;

	/* lock must already be initialized */
	ctrl->lock = &ctrl_gating_lock;

	/* Count, allocate, and register clock muxes */
	for (n = 0; desc[n].name;)
		n++;

	ctrl->num_muxes = n;
	ctrl->muxes = kcalloc(ctrl->num_muxes, sizeof(struct clk *),
			GFP_KERNEL);
	if (WARN_ON(!ctrl->muxes))
		goto muxes_out;

	for (n = 0; n < ctrl->num_muxes; n++) {
		ctrl->muxes[n] = clk_register_mux(NULL, desc[n].name,
				desc[n].parents, desc[n].num_parents,
				desc[n].flags, base, desc[n].shift,
				desc[n].width, desc[n].flags, ctrl->lock);
		WARN_ON(IS_ERR(ctrl->muxes[n]));
	}

	of_clk_add_provider(np, clk_muxing_get_src, ctrl);

	return;
muxes_out:
	kfree(ctrl);
ctrl_out:
	iounmap(base);
}

static void __init kirkwood_clk_init(struct device_node *np)
{
	struct device_node *cgnp =
		of_find_compatible_node(NULL, NULL, "marvell,kirkwood-gating-clock");


	if (of_device_is_compatible(np, "marvell,mv88f6180-core-clock"))
		mvebu_coreclk_setup(np, &mv88f6180_coreclks);
	else if (of_device_is_compatible(np, "marvell,mv98dx1135-core-clock"))
		mvebu_coreclk_setup(np, &mv98dx1135_coreclks);
	else
		mvebu_coreclk_setup(np, &kirkwood_coreclks);

	if (cgnp) {
		mvebu_clk_gating_setup(cgnp, kirkwood_gating_desc);
		kirkwood_clk_muxing_setup(cgnp, kirkwood_mux_desc);

		of_node_put(cgnp);
	}
}
CLK_OF_DECLARE(kirkwood_clk, "marvell,kirkwood-core-clock",
	       kirkwood_clk_init);
CLK_OF_DECLARE(mv88f6180_clk, "marvell,mv88f6180-core-clock",
	       kirkwood_clk_init);
CLK_OF_DECLARE(98dx1135_clk, "marvell,mv98dx1135-core-clock",
	       kirkwood_clk_init);

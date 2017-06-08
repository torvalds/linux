/*
 * Marvell Orion SoC clocks
 *
 * Copyright (C) 2014 Thomas Petazzoni
 *
 * Thomas Petazzoni <thomas.petazzoni@free-electrons.com>
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

static const struct coreclk_ratio orion_coreclk_ratios[] __initconst = {
	{ .id = 0, .name = "ddrclk", }
};

/*
 * Orion 5181
 */

#define SAR_MV88F5181_TCLK_FREQ      8
#define SAR_MV88F5181_TCLK_FREQ_MASK 0x3

static u32 __init mv88f5181_get_tclk_freq(void __iomem *sar)
{
	u32 opt = (readl(sar) >> SAR_MV88F5181_TCLK_FREQ) &
		SAR_MV88F5181_TCLK_FREQ_MASK;
	if (opt == 0)
		return 133333333;
	else if (opt == 1)
		return 150000000;
	else if (opt == 2)
		return 166666667;
	else
		return 0;
}

#define SAR_MV88F5181_CPU_FREQ       4
#define SAR_MV88F5181_CPU_FREQ_MASK  0xf

static u32 __init mv88f5181_get_cpu_freq(void __iomem *sar)
{
	u32 opt = (readl(sar) >> SAR_MV88F5181_CPU_FREQ) &
		SAR_MV88F5181_CPU_FREQ_MASK;
	if (opt == 0)
		return 333333333;
	else if (opt == 1 || opt == 2)
		return 400000000;
	else if (opt == 3)
		return 500000000;
	else
		return 0;
}

static void __init mv88f5181_get_clk_ratio(void __iomem *sar, int id,
					   int *mult, int *div)
{
	u32 opt = (readl(sar) >> SAR_MV88F5181_CPU_FREQ) &
		SAR_MV88F5181_CPU_FREQ_MASK;
	if (opt == 0 || opt == 1) {
		*mult = 1;
		*div  = 2;
	} else if (opt == 2 || opt == 3) {
		*mult = 1;
		*div  = 3;
	} else {
		*mult = 0;
		*div  = 1;
	}
}

static const struct coreclk_soc_desc mv88f5181_coreclks = {
	.get_tclk_freq = mv88f5181_get_tclk_freq,
	.get_cpu_freq = mv88f5181_get_cpu_freq,
	.get_clk_ratio = mv88f5181_get_clk_ratio,
	.ratios = orion_coreclk_ratios,
	.num_ratios = ARRAY_SIZE(orion_coreclk_ratios),
};

static void __init mv88f5181_clk_init(struct device_node *np)
{
	return mvebu_coreclk_setup(np, &mv88f5181_coreclks);
}

CLK_OF_DECLARE(mv88f5181_clk, "marvell,mv88f5181-core-clock", mv88f5181_clk_init);

/*
 * Orion 5182
 */

#define SAR_MV88F5182_TCLK_FREQ      8
#define SAR_MV88F5182_TCLK_FREQ_MASK 0x3

static u32 __init mv88f5182_get_tclk_freq(void __iomem *sar)
{
	u32 opt = (readl(sar) >> SAR_MV88F5182_TCLK_FREQ) &
		SAR_MV88F5182_TCLK_FREQ_MASK;
	if (opt == 1)
		return 150000000;
	else if (opt == 2)
		return 166666667;
	else
		return 0;
}

#define SAR_MV88F5182_CPU_FREQ       4
#define SAR_MV88F5182_CPU_FREQ_MASK  0xf

static u32 __init mv88f5182_get_cpu_freq(void __iomem *sar)
{
	u32 opt = (readl(sar) >> SAR_MV88F5182_CPU_FREQ) &
		SAR_MV88F5182_CPU_FREQ_MASK;
	if (opt == 0)
		return 333333333;
	else if (opt == 1 || opt == 2)
		return 400000000;
	else if (opt == 3)
		return 500000000;
	else
		return 0;
}

static void __init mv88f5182_get_clk_ratio(void __iomem *sar, int id,
					   int *mult, int *div)
{
	u32 opt = (readl(sar) >> SAR_MV88F5182_CPU_FREQ) &
		SAR_MV88F5182_CPU_FREQ_MASK;
	if (opt == 0 || opt == 1) {
		*mult = 1;
		*div  = 2;
	} else if (opt == 2 || opt == 3) {
		*mult = 1;
		*div  = 3;
	} else {
		*mult = 0;
		*div  = 1;
	}
}

static const struct coreclk_soc_desc mv88f5182_coreclks = {
	.get_tclk_freq = mv88f5182_get_tclk_freq,
	.get_cpu_freq = mv88f5182_get_cpu_freq,
	.get_clk_ratio = mv88f5182_get_clk_ratio,
	.ratios = orion_coreclk_ratios,
	.num_ratios = ARRAY_SIZE(orion_coreclk_ratios),
};

static void __init mv88f5182_clk_init(struct device_node *np)
{
	return mvebu_coreclk_setup(np, &mv88f5182_coreclks);
}

CLK_OF_DECLARE(mv88f5182_clk, "marvell,mv88f5182-core-clock", mv88f5182_clk_init);

/*
 * Orion 5281
 */

static u32 __init mv88f5281_get_tclk_freq(void __iomem *sar)
{
	/* On 5281, tclk is always 166 Mhz */
	return 166666667;
}

#define SAR_MV88F5281_CPU_FREQ       4
#define SAR_MV88F5281_CPU_FREQ_MASK  0xf

static u32 __init mv88f5281_get_cpu_freq(void __iomem *sar)
{
	u32 opt = (readl(sar) >> SAR_MV88F5281_CPU_FREQ) &
		SAR_MV88F5281_CPU_FREQ_MASK;
	if (opt == 1 || opt == 2)
		return 400000000;
	else if (opt == 3)
		return 500000000;
	else
		return 0;
}

static void __init mv88f5281_get_clk_ratio(void __iomem *sar, int id,
					   int *mult, int *div)
{
	u32 opt = (readl(sar) >> SAR_MV88F5281_CPU_FREQ) &
		SAR_MV88F5281_CPU_FREQ_MASK;
	if (opt == 1) {
		*mult = 1;
		*div = 2;
	} else if (opt == 2 || opt == 3) {
		*mult = 1;
		*div = 3;
	} else {
		*mult = 0;
		*div = 1;
	}
}

static const struct coreclk_soc_desc mv88f5281_coreclks = {
	.get_tclk_freq = mv88f5281_get_tclk_freq,
	.get_cpu_freq = mv88f5281_get_cpu_freq,
	.get_clk_ratio = mv88f5281_get_clk_ratio,
	.ratios = orion_coreclk_ratios,
	.num_ratios = ARRAY_SIZE(orion_coreclk_ratios),
};

static void __init mv88f5281_clk_init(struct device_node *np)
{
	return mvebu_coreclk_setup(np, &mv88f5281_coreclks);
}

CLK_OF_DECLARE(mv88f5281_clk, "marvell,mv88f5281-core-clock", mv88f5281_clk_init);

/*
 * Orion 6183
 */

#define SAR_MV88F6183_TCLK_FREQ      9
#define SAR_MV88F6183_TCLK_FREQ_MASK 0x1

static u32 __init mv88f6183_get_tclk_freq(void __iomem *sar)
{
	u32 opt = (readl(sar) >> SAR_MV88F6183_TCLK_FREQ) &
		SAR_MV88F6183_TCLK_FREQ_MASK;
	if (opt == 0)
		return 133333333;
	else if (opt == 1)
		return 166666667;
	else
		return 0;
}

#define SAR_MV88F6183_CPU_FREQ       1
#define SAR_MV88F6183_CPU_FREQ_MASK  0x3f

static u32 __init mv88f6183_get_cpu_freq(void __iomem *sar)
{
	u32 opt = (readl(sar) >> SAR_MV88F6183_CPU_FREQ) &
		SAR_MV88F6183_CPU_FREQ_MASK;
	if (opt == 9)
		return 333333333;
	else if (opt == 17)
		return 400000000;
	else
		return 0;
}

static void __init mv88f6183_get_clk_ratio(void __iomem *sar, int id,
					   int *mult, int *div)
{
	u32 opt = (readl(sar) >> SAR_MV88F6183_CPU_FREQ) &
		SAR_MV88F6183_CPU_FREQ_MASK;
	if (opt == 9 || opt == 17) {
		*mult = 1;
		*div  = 2;
	} else {
		*mult = 0;
		*div  = 1;
	}
}

static const struct coreclk_soc_desc mv88f6183_coreclks = {
	.get_tclk_freq = mv88f6183_get_tclk_freq,
	.get_cpu_freq = mv88f6183_get_cpu_freq,
	.get_clk_ratio = mv88f6183_get_clk_ratio,
	.ratios = orion_coreclk_ratios,
	.num_ratios = ARRAY_SIZE(orion_coreclk_ratios),
};


static void __init mv88f6183_clk_init(struct device_node *np)
{
	return mvebu_coreclk_setup(np, &mv88f6183_coreclks);
}

CLK_OF_DECLARE(mv88f6183_clk, "marvell,mv88f6183-core-clock", mv88f6183_clk_init);

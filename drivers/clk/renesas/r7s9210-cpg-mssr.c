// SPDX-License-Identifier: GPL-2.0
/*
 * R7S9210 Clock Pulse Generator / Module Standby
 *
 * Based on r8a7795-cpg-mssr.c
 *
 * Copyright (C) 2018 Chris Brandt
 * Copyright (C) 2018 Renesas Electronics Corp.
 *
 */

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/io.h>
#include <dt-bindings/clock/r7s9210-cpg-mssr.h>
#include "renesas-cpg-mssr.h"

#define CPG_FRQCR	0x00

static u8 cpg_mode;

/* Internal Clock ratio table */
static const struct {
	unsigned int i;
	unsigned int g;
	unsigned int b;
	unsigned int p1;
	/* p0 is always 32 */;
} ratio_tab[5] = {	/* I,  G,  B, P1 */
			{  2,  4,  8, 16},	/* FRQCR = 0x012 */
			{  4,  4,  8, 16},	/* FRQCR = 0x112 */
			{  8,  4,  8, 16},	/* FRQCR = 0x212 */
			{ 16,  8, 16, 16},	/* FRQCR = 0x322 */
			{ 16, 16, 32, 32},	/* FRQCR = 0x333 */
			};

enum rz_clk_types {
	CLK_TYPE_RZA_MAIN = CLK_TYPE_CUSTOM,
	CLK_TYPE_RZA_PLL,
};

enum clk_ids {
	/* Core Clock Outputs exported to DT */
	LAST_DT_CORE_CLK = R7S9210_CLK_P0,

	/* External Input Clocks */
	CLK_EXTAL,

	/* Internal Core Clocks */
	CLK_MAIN,
	CLK_PLL,

	/* Module Clocks */
	MOD_CLK_BASE
};

static struct cpg_core_clk r7s9210_early_core_clks[] = {
	/* External Clock Inputs */
	DEF_INPUT("extal",     CLK_EXTAL),

	/* Internal Core Clocks */
	DEF_BASE(".main",       CLK_MAIN, CLK_TYPE_RZA_MAIN, CLK_EXTAL),
	DEF_BASE(".pll",       CLK_PLL, CLK_TYPE_RZA_PLL, CLK_MAIN),

	/* Core Clock Outputs */
	DEF_FIXED("p1c",    R7S9210_CLK_P1C,   CLK_PLL,         16, 1),
};

static const struct mssr_mod_clk r7s9210_early_mod_clks[] __initconst = {
	DEF_MOD_STB("ostm2",	 34,	R7S9210_CLK_P1C),
	DEF_MOD_STB("ostm1",	 35,	R7S9210_CLK_P1C),
	DEF_MOD_STB("ostm0",	 36,	R7S9210_CLK_P1C),
};

static struct cpg_core_clk r7s9210_core_clks[] = {
	/* Core Clock Outputs */
	DEF_FIXED("i",      R7S9210_CLK_I,     CLK_PLL,          2, 1),
	DEF_FIXED("g",      R7S9210_CLK_G,     CLK_PLL,          4, 1),
	DEF_FIXED("b",      R7S9210_CLK_B,     CLK_PLL,          8, 1),
	DEF_FIXED("p1",     R7S9210_CLK_P1,    CLK_PLL,         16, 1),
	DEF_FIXED("p0",     R7S9210_CLK_P0,    CLK_PLL,         32, 1),
};

static const struct mssr_mod_clk r7s9210_mod_clks[] __initconst = {
	DEF_MOD_STB("scif4",	 43,	R7S9210_CLK_P1C),
	DEF_MOD_STB("scif3",	 44,	R7S9210_CLK_P1C),
	DEF_MOD_STB("scif2",	 45,	R7S9210_CLK_P1C),
	DEF_MOD_STB("scif1",	 46,	R7S9210_CLK_P1C),
	DEF_MOD_STB("scif0",	 47,	R7S9210_CLK_P1C),

	DEF_MOD_STB("usb1",	 60,	R7S9210_CLK_B),
	DEF_MOD_STB("usb0",	 61,	R7S9210_CLK_B),
	DEF_MOD_STB("ether1",	 64,	R7S9210_CLK_B),
	DEF_MOD_STB("ether0",	 65,	R7S9210_CLK_B),

	DEF_MOD_STB("spibsc",	 83,	R7S9210_CLK_P1),
	DEF_MOD_STB("i2c3",	 84,	R7S9210_CLK_P1),
	DEF_MOD_STB("i2c2",	 85,	R7S9210_CLK_P1),
	DEF_MOD_STB("i2c1",	 86,	R7S9210_CLK_P1),
	DEF_MOD_STB("i2c0",	 87,	R7S9210_CLK_P1),

	DEF_MOD_STB("spi2",	 95,	R7S9210_CLK_P1),
	DEF_MOD_STB("spi1",	 96,	R7S9210_CLK_P1),
	DEF_MOD_STB("spi0",	 97,	R7S9210_CLK_P1),

	DEF_MOD_STB("sdhi11",	100,	R7S9210_CLK_B),
	DEF_MOD_STB("sdhi10",	101,	R7S9210_CLK_B),
	DEF_MOD_STB("sdhi01",	102,	R7S9210_CLK_B),
	DEF_MOD_STB("sdhi00",	103,	R7S9210_CLK_B),
};

/* The clock dividers in the table vary based on DT and register settings */
static void __init r7s9210_update_clk_table(struct clk *extal_clk,
					    void __iomem *base)
{
	int i;
	u16 frqcr;
	u8 index;

	/* If EXTAL is above 12MHz, then we know it is Mode 1 */
	if (clk_get_rate(extal_clk) > 12000000)
		cpg_mode = 1;

	frqcr = readl(base + CPG_FRQCR) & 0xFFF;
	if (frqcr == 0x012)
		index = 0;
	else if (frqcr == 0x112)
		index = 1;
	else if (frqcr == 0x212)
		index = 2;
	else if (frqcr == 0x322)
		index = 3;
	else if (frqcr == 0x333)
		index = 4;
	else
		BUG_ON(1);	/* Illegal FRQCR value */

	for (i = 0; i < ARRAY_SIZE(r7s9210_core_clks); i++) {
		switch (r7s9210_core_clks[i].id) {
		case R7S9210_CLK_I:
			r7s9210_core_clks[i].div = ratio_tab[index].i;
			break;
		case R7S9210_CLK_G:
			r7s9210_core_clks[i].div = ratio_tab[index].g;
			break;
		case R7S9210_CLK_B:
			r7s9210_core_clks[i].div = ratio_tab[index].b;
			break;
		case R7S9210_CLK_P1:
		case R7S9210_CLK_P1C:
			r7s9210_core_clks[i].div = ratio_tab[index].p1;
			break;
		case R7S9210_CLK_P0:
			r7s9210_core_clks[i].div = 32;
			break;
		}
	}
}

static struct clk * __init rza2_cpg_clk_register(struct device *dev,
	const struct cpg_core_clk *core, const struct cpg_mssr_info *info,
	struct clk **clks, void __iomem *base,
	struct raw_notifier_head *notifiers)
{
	struct clk *parent;
	unsigned int mult = 1;
	unsigned int div = 1;

	parent = clks[core->parent];
	if (IS_ERR(parent))
		return ERR_CAST(parent);

	switch (core->id) {
	case CLK_MAIN:
		break;

	case CLK_PLL:
		if (cpg_mode)
			mult = 44;	/* Divider 1 is 1/2 */
		else
			mult = 88;	/* Divider 1 is 1 */
		break;

	default:
		return ERR_PTR(-EINVAL);
	}

	if (core->id == CLK_MAIN)
		r7s9210_update_clk_table(parent, base);

	return clk_register_fixed_factor(NULL, core->name,
					 __clk_get_name(parent), 0, mult, div);
}

const struct cpg_mssr_info r7s9210_cpg_mssr_info __initconst = {
	/* Early Clocks */
	.early_core_clks = r7s9210_early_core_clks,
	.num_early_core_clks = ARRAY_SIZE(r7s9210_early_core_clks),
	.early_mod_clks = r7s9210_early_mod_clks,
	.num_early_mod_clks = ARRAY_SIZE(r7s9210_early_mod_clks),

	/* Core Clocks */
	.core_clks = r7s9210_core_clks,
	.num_core_clks = ARRAY_SIZE(r7s9210_core_clks),
	.last_dt_core_clk = LAST_DT_CORE_CLK,
	.num_total_core_clks = MOD_CLK_BASE,

	/* Module Clocks */
	.mod_clks = r7s9210_mod_clks,
	.num_mod_clks = ARRAY_SIZE(r7s9210_mod_clks),
	.num_hw_mod_clks = 11 * 32, /* includes STBCR0 which doesn't exist */

	/* Callbacks */
	.cpg_clk_register = rza2_cpg_clk_register,

	/* RZ/A2 has Standby Control Registers */
	.stbyctrl = true,
};

static void __init r7s9210_cpg_mssr_early_init(struct device_node *np)
{
	cpg_mssr_early_init(np, &r7s9210_cpg_mssr_info);
}

CLK_OF_DECLARE_DRIVER(cpg_mstp_clks, "renesas,r7s9210-cpg-mssr",
		      r7s9210_cpg_mssr_early_init);

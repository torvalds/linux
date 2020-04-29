// SPDX-License-Identifier: GPL-2.0
/*
 * r8a77970 Clock Pulse Generator / Module Standby and Software Reset
 *
 * Copyright (C) 2017-2018 Cogent Embedded Inc.
 *
 * Based on r8a7795-cpg-mssr.c
 *
 * Copyright (C) 2015 Glider bvba
 */

#include <linux/clk-provider.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/soc/renesas/rcar-rst.h>

#include <dt-bindings/clock/r8a77970-cpg-mssr.h>

#include "renesas-cpg-mssr.h"
#include "rcar-gen3-cpg.h"

#define CPG_SD0CKCR		0x0074

enum r8a77970_clk_types {
	CLK_TYPE_R8A77970_SD0H = CLK_TYPE_GEN3_SOC_BASE,
	CLK_TYPE_R8A77970_SD0,
};

enum clk_ids {
	/* Core Clock Outputs exported to DT */
	LAST_DT_CORE_CLK = R8A77970_CLK_OSC,

	/* External Input Clocks */
	CLK_EXTAL,
	CLK_EXTALR,

	/* Internal Core Clocks */
	CLK_MAIN,
	CLK_PLL0,
	CLK_PLL1,
	CLK_PLL3,
	CLK_PLL1_DIV2,
	CLK_PLL1_DIV4,

	/* Module Clocks */
	MOD_CLK_BASE
};

static spinlock_t cpg_lock;

static const struct clk_div_table cpg_sd0h_div_table[] = {
	{  0,  2 }, {  1,  3 }, {  2,  4 }, {  3,  6 },
	{  4,  8 }, {  5, 12 }, {  6, 16 }, {  7, 18 },
	{  8, 24 }, { 10, 36 }, { 11, 48 }, {  0,  0 },
};

static const struct clk_div_table cpg_sd0_div_table[] = {
	{  4,  8 }, {  5, 12 }, {  6, 16 }, {  7, 18 },
	{  8, 24 }, { 10, 36 }, { 11, 48 }, { 12, 10 },
	{  0,  0 },
};

static const struct cpg_core_clk r8a77970_core_clks[] __initconst = {
	/* External Clock Inputs */
	DEF_INPUT("extal",	CLK_EXTAL),
	DEF_INPUT("extalr",	CLK_EXTALR),

	/* Internal Core Clocks */
	DEF_BASE(".main",	CLK_MAIN, CLK_TYPE_GEN3_MAIN, CLK_EXTAL),
	DEF_BASE(".pll0",	CLK_PLL0, CLK_TYPE_GEN3_PLL0, CLK_MAIN),
	DEF_BASE(".pll1",	CLK_PLL1, CLK_TYPE_GEN3_PLL1, CLK_MAIN),
	DEF_BASE(".pll3",	CLK_PLL3, CLK_TYPE_GEN3_PLL3, CLK_MAIN),

	DEF_FIXED(".pll1_div2",	CLK_PLL1_DIV2,	CLK_PLL1,	2, 1),
	DEF_FIXED(".pll1_div4",	CLK_PLL1_DIV4,	CLK_PLL1_DIV2,	2, 1),

	/* Core Clock Outputs */
	DEF_FIXED("ztr",	R8A77970_CLK_ZTR,   CLK_PLL1_DIV2,  6, 1),
	DEF_FIXED("ztrd2",	R8A77970_CLK_ZTRD2, CLK_PLL1_DIV2, 12, 1),
	DEF_FIXED("zt",		R8A77970_CLK_ZT,    CLK_PLL1_DIV2,  4, 1),
	DEF_FIXED("zx",		R8A77970_CLK_ZX,    CLK_PLL1_DIV2,  3, 1),
	DEF_FIXED("s1d1",	R8A77970_CLK_S1D1,  CLK_PLL1_DIV2,  4, 1),
	DEF_FIXED("s1d2",	R8A77970_CLK_S1D2,  CLK_PLL1_DIV2,  8, 1),
	DEF_FIXED("s1d4",	R8A77970_CLK_S1D4,  CLK_PLL1_DIV2, 16, 1),
	DEF_FIXED("s2d1",	R8A77970_CLK_S2D1,  CLK_PLL1_DIV2,  6, 1),
	DEF_FIXED("s2d2",	R8A77970_CLK_S2D2,  CLK_PLL1_DIV2, 12, 1),
	DEF_FIXED("s2d4",	R8A77970_CLK_S2D4,  CLK_PLL1_DIV2, 24, 1),

	DEF_BASE("sd0h", R8A77970_CLK_SD0H, CLK_TYPE_R8A77970_SD0H,
		 CLK_PLL1_DIV2),
	DEF_BASE("sd0",	R8A77970_CLK_SD0, CLK_TYPE_R8A77970_SD0, CLK_PLL1_DIV2),

	DEF_FIXED("rpc",	R8A77970_CLK_RPC,   CLK_PLL1_DIV2,  5, 1),
	DEF_FIXED("rpcd2",	R8A77970_CLK_RPCD2, CLK_PLL1_DIV2, 10, 1),

	DEF_FIXED("cl",		R8A77970_CLK_CL,    CLK_PLL1_DIV2, 48, 1),
	DEF_FIXED("cp",		R8A77970_CLK_CP,    CLK_EXTAL,	    2, 1),
	DEF_FIXED("cpex",	R8A77970_CLK_CPEX,  CLK_EXTAL,	    2, 1),

	DEF_DIV6P1("canfd",	R8A77970_CLK_CANFD, CLK_PLL1_DIV4, 0x244),
	DEF_DIV6P1("mso",	R8A77970_CLK_MSO,   CLK_PLL1_DIV4, 0x014),
	DEF_DIV6P1("csi0",	R8A77970_CLK_CSI0,  CLK_PLL1_DIV4, 0x00c),

	DEF_FIXED("osc",	R8A77970_CLK_OSC,   CLK_PLL1_DIV2, 12*1024, 1),
	DEF_FIXED("r",		R8A77970_CLK_R,	    CLK_EXTALR,	   1, 1),
};

static const struct mssr_mod_clk r8a77970_mod_clks[] __initconst = {
	DEF_MOD("tmu4",			 121,	R8A77970_CLK_S2D2),
	DEF_MOD("tmu3",			 122,	R8A77970_CLK_S2D2),
	DEF_MOD("tmu2",			 123,	R8A77970_CLK_S2D2),
	DEF_MOD("tmu1",			 124,	R8A77970_CLK_S2D2),
	DEF_MOD("tmu0",			 125,	R8A77970_CLK_CP),
	DEF_MOD("ivcp1e",		 127,	R8A77970_CLK_S2D1),
	DEF_MOD("scif4",		 203,	R8A77970_CLK_S2D4),
	DEF_MOD("scif3",		 204,	R8A77970_CLK_S2D4),
	DEF_MOD("scif1",		 206,	R8A77970_CLK_S2D4),
	DEF_MOD("scif0",		 207,	R8A77970_CLK_S2D4),
	DEF_MOD("msiof3",		 208,	R8A77970_CLK_MSO),
	DEF_MOD("msiof2",		 209,	R8A77970_CLK_MSO),
	DEF_MOD("msiof1",		 210,	R8A77970_CLK_MSO),
	DEF_MOD("msiof0",		 211,	R8A77970_CLK_MSO),
	DEF_MOD("mfis",			 213,	R8A77970_CLK_S2D2),
	DEF_MOD("sys-dmac2",		 217,	R8A77970_CLK_S2D1),
	DEF_MOD("sys-dmac1",		 218,	R8A77970_CLK_S2D1),
	DEF_MOD("cmt3",			 300,	R8A77970_CLK_R),
	DEF_MOD("cmt2",			 301,	R8A77970_CLK_R),
	DEF_MOD("cmt1",			 302,	R8A77970_CLK_R),
	DEF_MOD("cmt0",			 303,	R8A77970_CLK_R),
	DEF_MOD("tpu0",			 304,	R8A77970_CLK_S2D4),
	DEF_MOD("sd-if",		 314,	R8A77970_CLK_SD0),
	DEF_MOD("rwdt",			 402,	R8A77970_CLK_R),
	DEF_MOD("intc-ex",		 407,	R8A77970_CLK_CP),
	DEF_MOD("intc-ap",		 408,	R8A77970_CLK_S2D1),
	DEF_MOD("hscif3",		 517,	R8A77970_CLK_S2D1),
	DEF_MOD("hscif2",		 518,	R8A77970_CLK_S2D1),
	DEF_MOD("hscif1",		 519,	R8A77970_CLK_S2D1),
	DEF_MOD("hscif0",		 520,	R8A77970_CLK_S2D1),
	DEF_MOD("thermal",		 522,	R8A77970_CLK_CP),
	DEF_MOD("pwm",			 523,	R8A77970_CLK_S2D4),
	DEF_MOD("fcpvd0",		 603,	R8A77970_CLK_S2D1),
	DEF_MOD("vspd0",		 623,	R8A77970_CLK_S2D1),
	DEF_MOD("csi40",		 716,	R8A77970_CLK_CSI0),
	DEF_MOD("du0",			 724,	R8A77970_CLK_S2D1),
	DEF_MOD("lvds",			 727,	R8A77970_CLK_S2D1),
	DEF_MOD("vin3",			 808,	R8A77970_CLK_S2D1),
	DEF_MOD("vin2",			 809,	R8A77970_CLK_S2D1),
	DEF_MOD("vin1",			 810,	R8A77970_CLK_S2D1),
	DEF_MOD("vin0",			 811,	R8A77970_CLK_S2D1),
	DEF_MOD("etheravb",		 812,	R8A77970_CLK_S2D2),
	DEF_MOD("gpio5",		 907,	R8A77970_CLK_CP),
	DEF_MOD("gpio4",		 908,	R8A77970_CLK_CP),
	DEF_MOD("gpio3",		 909,	R8A77970_CLK_CP),
	DEF_MOD("gpio2",		 910,	R8A77970_CLK_CP),
	DEF_MOD("gpio1",		 911,	R8A77970_CLK_CP),
	DEF_MOD("gpio0",		 912,	R8A77970_CLK_CP),
	DEF_MOD("can-fd",		 914,	R8A77970_CLK_S2D2),
	DEF_MOD("rpc-if",		 917,	R8A77970_CLK_RPC),
	DEF_MOD("i2c4",			 927,	R8A77970_CLK_S2D2),
	DEF_MOD("i2c3",			 928,	R8A77970_CLK_S2D2),
	DEF_MOD("i2c2",			 929,	R8A77970_CLK_S2D2),
	DEF_MOD("i2c1",			 930,	R8A77970_CLK_S2D2),
	DEF_MOD("i2c0",			 931,	R8A77970_CLK_S2D2),
};

static const unsigned int r8a77970_crit_mod_clks[] __initconst = {
	MOD_CLK_ID(408),	/* INTC-AP (GIC) */
};


/*
 * CPG Clock Data
 */

/*
 *   MD		EXTAL		PLL0	PLL1	PLL3
 * 14 13 19	(MHz)
 *-------------------------------------------------
 * 0  0  0	16.66 x 1	x192	x192	x96
 * 0  0  1	16.66 x 1	x192	x192	x80
 * 0  1  0	20    x 1	x160	x160	x80
 * 0  1  1	20    x 1	x160	x160	x66
 * 1  0  0	27    / 2	x236	x236	x118
 * 1  0  1	27    / 2	x236	x236	x98
 * 1  1  0	33.33 / 2	x192	x192	x96
 * 1  1  1	33.33 / 2	x192	x192	x80
 */
#define CPG_PLL_CONFIG_INDEX(md)	((((md) & BIT(14)) >> 12) | \
					 (((md) & BIT(13)) >> 12) | \
					 (((md) & BIT(19)) >> 19))

static const struct rcar_gen3_cpg_pll_config cpg_pll_configs[8] __initconst = {
	/* EXTAL div	PLL1 mult/div	PLL3 mult/div */
	{ 1,		192,	1,	96,	1,	},
	{ 1,		192,	1,	80,	1,	},
	{ 1,		160,	1,	80,	1,	},
	{ 1,		160,	1,	66,	1,	},
	{ 2,		236,	1,	118,	1,	},
	{ 2,		236,	1,	98,	1,	},
	{ 2,		192,	1,	96,	1,	},
	{ 2,		192,	1,	80,	1,	},
};

static int __init r8a77970_cpg_mssr_init(struct device *dev)
{
	const struct rcar_gen3_cpg_pll_config *cpg_pll_config;
	u32 cpg_mode;
	int error;

	error = rcar_rst_read_mode_pins(&cpg_mode);
	if (error)
		return error;

	spin_lock_init(&cpg_lock);

	cpg_pll_config = &cpg_pll_configs[CPG_PLL_CONFIG_INDEX(cpg_mode)];

	return rcar_gen3_cpg_init(cpg_pll_config, CLK_EXTALR, cpg_mode);
}

static struct clk * __init r8a77970_cpg_clk_register(struct device *dev,
	const struct cpg_core_clk *core, const struct cpg_mssr_info *info,
	struct clk **clks, void __iomem *base,
	struct raw_notifier_head *notifiers)
{
	const struct clk_div_table *table;
	const struct clk *parent;
	unsigned int shift;

	switch (core->type) {
	case CLK_TYPE_R8A77970_SD0H:
		table = cpg_sd0h_div_table;
		shift = 8;
		break;
	case CLK_TYPE_R8A77970_SD0:
		table = cpg_sd0_div_table;
		shift = 4;
		break;
	default:
		return rcar_gen3_cpg_clk_register(dev, core, info, clks, base,
						  notifiers);
	}

	parent = clks[core->parent];
	if (IS_ERR(parent))
		return ERR_CAST(parent);

	return clk_register_divider_table(NULL, core->name,
					  __clk_get_name(parent), 0,
					  base + CPG_SD0CKCR,
					  shift, 4, 0, table, &cpg_lock);
}

const struct cpg_mssr_info r8a77970_cpg_mssr_info __initconst = {
	/* Core Clocks */
	.core_clks = r8a77970_core_clks,
	.num_core_clks = ARRAY_SIZE(r8a77970_core_clks),
	.last_dt_core_clk = LAST_DT_CORE_CLK,
	.num_total_core_clks = MOD_CLK_BASE,

	/* Module Clocks */
	.mod_clks = r8a77970_mod_clks,
	.num_mod_clks = ARRAY_SIZE(r8a77970_mod_clks),
	.num_hw_mod_clks = 12 * 32,

	/* Critical Module Clocks */
	.crit_mod_clks = r8a77970_crit_mod_clks,
	.num_crit_mod_clks = ARRAY_SIZE(r8a77970_crit_mod_clks),

	/* Callbacks */
	.init = r8a77970_cpg_mssr_init,
	.cpg_clk_register = r8a77970_cpg_clk_register,
};

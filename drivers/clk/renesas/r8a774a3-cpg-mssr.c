// SPDX-License-Identifier: GPL-2.0
/*
 * r8a774a3 Clock Pulse Generator / Module Standby and Software Reset
 *
 * Copyright (C) 2026 Renesas Electronics Corp.
 *
 * Based on r8a7796-cpg-mssr.c
 *
 * Copyright (C) 2016 Glider bvba
 */

#include <linux/device.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/soc/renesas/rcar-rst.h>

#include <dt-bindings/clock/renesas,r8a774a3-cpg-mssr.h>

#include "renesas-cpg-mssr.h"
#include "rcar-gen3-cpg.h"

enum clk_ids {
	/* Core Clock Outputs exported to DT */
	LAST_DT_CORE_CLK = R8A774A3_CLK_OSC,

	/* External Input Clocks */
	CLK_EXTAL,
	CLK_EXTALR,

	/* Internal Core Clocks */
	CLK_MAIN,
	CLK_PLL0,
	CLK_PLL1,
	CLK_PLL2,
	CLK_PLL3,
	CLK_PLL4,
	CLK_PLL1_DIV2,
	CLK_PLL1_DIV4,
	CLK_S0,
	CLK_S1,
	CLK_S2,
	CLK_S3,

	/* Module Clocks */
	MOD_CLK_BASE
};

static const struct cpg_core_clk r8a774a3_core_clks[] __initconst = {
	/* External Clock Inputs */
	DEF_INPUT("extal",      CLK_EXTAL),
	DEF_INPUT("extalr",     CLK_EXTALR),

	/* Internal Core Clocks */
	DEF_BASE(".main",       CLK_MAIN, CLK_TYPE_GEN3_MAIN, CLK_EXTAL),
	DEF_BASE(".pll0",       CLK_PLL0, CLK_TYPE_GEN3_PLL0, CLK_MAIN),
	DEF_BASE(".pll1",       CLK_PLL1, CLK_TYPE_GEN3_PLL1, CLK_MAIN),
	DEF_BASE(".pll2",       CLK_PLL2, CLK_TYPE_GEN3_PLL2, CLK_MAIN),

	DEF_FIXED(".pll1_div2", CLK_PLL1_DIV2,     CLK_PLL1,       2, 1),
	DEF_FIXED(".s0",        CLK_S0,            CLK_PLL1_DIV2,  2, 1),
	DEF_FIXED(".s3",        CLK_S3,            CLK_PLL1_DIV2,  6, 1),

	/* Core Clock Outputs */
	DEF_GEN3_Z("z",         R8A774A3_CLK_Z,     CLK_TYPE_GEN3_Z, CLK_PLL0, 2, 8),
	DEF_GEN3_Z("z2",        R8A774A3_CLK_Z2,    CLK_TYPE_GEN3_Z,  CLK_PLL2, 2, 0),
	DEF_FIXED("s0d3",       R8A774A3_CLK_S0D3,  CLK_S0,         3, 1),
	DEF_FIXED("s3d1",       R8A774A3_CLK_S3D1,  CLK_S3,         1, 1),
	DEF_FIXED("s3d4",       R8A774A3_CLK_S3D4,  CLK_S3,         4, 1),
};

static const struct mssr_mod_clk r8a774a3_mod_clks[] __initconst = {
	DEF_MOD("scif2",		 310,	R8A774A3_CLK_S3D4),
	DEF_MOD("intc-ap",		 408,	R8A774A3_CLK_S0D3),
};

static const unsigned int r8a774a3_crit_mod_clks[] __initconst = {
	MOD_CLK_ID(408),	/* INTC-AP (GIC) */
};

/*
 * CPG Clock Data
 */

/*
 *   MD		EXTAL		PLL0	PLL1	PLL2	PLL3	PLL4	OSC
 * 14 13 19 17	(MHz)
 *-------------------------------------------------------------------------
 * 0  0  0  0	16.66 x 1	x180	x192	x144	x192	x144	/16
 * 0  0  0  1	16.66 x 1	x180	x192	x144	x128	x144	/16
 * 0  0  1  0	Prohibited setting
 * 0  0  1  1	16.66 x 1	x180	x192	x144	x192	x144	/16
 * 0  1  0  0	20    x 1	x150	x160	x120	x160	x120	/19
 * 0  1  0  1	20    x 1	x150	x160	x120	x106	x120	/19
 * 0  1  1  0	Prohibited setting
 * 0  1  1  1	20    x 1	x150	x160	x120	x160	x120	/19
 * 1  0  0  0	25    x 1	x120	x128	x96	x128	x96	/24
 * 1  0  0  1	25    x 1	x120	x128	x96	x84	x96	/24
 * 1  0  1  0	Prohibited setting
 * 1  0  1  1	25    x 1	x120	x128	x96	x128	x96	/24
 * 1  1  0  0	33.33 / 2	x180	x192	x144	x192	x144	/32
 * 1  1  0  1	33.33 / 2	x180	x192	x144	x128	x144	/32
 * 1  1  1  0	Prohibited setting
 * 1  1  1  1	33.33 / 2	x180	x192	x144	x192	x144	/32
 */
#define CPG_PLL_CONFIG_INDEX(md)	((((md) & BIT(14)) >> 11) | \
					 (((md) & BIT(13)) >> 11) | \
					 (((md) & BIT(19)) >> 18) | \
					 (((md) & BIT(17)) >> 17))

static const struct rcar_gen3_cpg_pll_config cpg_pll_configs[16] __initconst = {
	/* EXTAL div	PLL1 mult/div	PLL3 mult/div	OSC prediv */
	{ 1,		192,	1,	192,	1,	16,	},
	{ 1,		192,	1,	128,	1,	16,	},
	{ 0, /* Prohibited setting */				},
	{ 1,		192,	1,	192,	1,	16,	},
	{ 1,		160,	1,	160,	1,	19,	},
	{ 1,		160,	1,	106,	1,	19,	},
	{ 0, /* Prohibited setting */				},
	{ 1,		160,	1,	160,	1,	19,	},
	{ 1,		128,	1,	128,	1,	24,	},
	{ 1,		128,	1,	84,	1,	24,	},
	{ 0, /* Prohibited setting */				},
	{ 1,		128,	1,	128,	1,	24,	},
	{ 2,		192,	1,	192,	1,	32,	},
	{ 2,		192,	1,	128,	1,	32,	},
	{ 0, /* Prohibited setting */				},
	{ 2,		192,	1,	192,	1,	32,	},
};

static int __init r8a774a3_cpg_mssr_init(struct device *dev)
{
	const struct rcar_gen3_cpg_pll_config *cpg_pll_config;
	u32 cpg_mode;
	int error;

	error = rcar_rst_read_mode_pins(&cpg_mode);
	if (error)
		return error;

	cpg_pll_config = &cpg_pll_configs[CPG_PLL_CONFIG_INDEX(cpg_mode)];
	if (!cpg_pll_config->extal_div) {
		dev_err(dev, "Prohibited setting (cpg_mode=0x%x)\n", cpg_mode);
		return -EINVAL;
	}

	return rcar_gen3_cpg_init(cpg_pll_config, CLK_EXTALR, cpg_mode);
}

const struct cpg_mssr_info r8a774a3_cpg_mssr_info __initconst = {
	/* Core Clocks */
	.core_clks = r8a774a3_core_clks,
	.num_core_clks = ARRAY_SIZE(r8a774a3_core_clks),
	.last_dt_core_clk = LAST_DT_CORE_CLK,
	.num_total_core_clks = MOD_CLK_BASE,

	/* Module Clocks */
	.mod_clks = r8a774a3_mod_clks,
	.num_mod_clks = ARRAY_SIZE(r8a774a3_mod_clks),
	.num_hw_mod_clks = 12 * 32,

	/* Critical Module Clocks */
	.crit_mod_clks = r8a774a3_crit_mod_clks,
	.num_crit_mod_clks = ARRAY_SIZE(r8a774a3_crit_mod_clks),

	/* Callbacks */
	.init = r8a774a3_cpg_mssr_init,
	.cpg_clk_register = rcar_gen3_cpg_clk_register,
};

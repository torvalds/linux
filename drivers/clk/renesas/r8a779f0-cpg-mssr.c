// SPDX-License-Identifier: GPL-2.0
/*
 * r8a779f0 Clock Pulse Generator / Module Standby and Software Reset
 *
 * Copyright (C) 2021 Renesas Electronics Corp.
 *
 * Based on r8a779a0-cpg-mssr.c
 */

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/soc/renesas/rcar-rst.h>

#include <dt-bindings/clock/r8a779f0-cpg-mssr.h>

#include "renesas-cpg-mssr.h"
#include "rcar-gen4-cpg.h"

enum clk_ids {
	/* Core Clock Outputs exported to DT */
	LAST_DT_CORE_CLK = R8A779F0_CLK_R,

	/* External Input Clocks */
	CLK_EXTAL,
	CLK_EXTALR,

	/* Internal Core Clocks */
	CLK_MAIN,
	CLK_PLL1,
	CLK_PLL2,
	CLK_PLL3,
	CLK_PLL5,
	CLK_PLL6,
	CLK_PLL1_DIV2,
	CLK_PLL2_DIV2,
	CLK_PLL3_DIV2,
	CLK_PLL5_DIV2,
	CLK_PLL5_DIV4,
	CLK_PLL6_DIV2,
	CLK_S0,
	CLK_SDSRC,
	CLK_RPCSRC,
	CLK_OCO,

	/* Module Clocks */
	MOD_CLK_BASE
};

static const struct cpg_core_clk r8a779f0_core_clks[] __initconst = {
	/* External Clock Inputs */
	DEF_INPUT("extal",	CLK_EXTAL),
	DEF_INPUT("extalr",	CLK_EXTALR),

	/* Internal Core Clocks */
	DEF_BASE(".main", CLK_MAIN,	CLK_TYPE_GEN4_MAIN, CLK_EXTAL),
	DEF_BASE(".pll1", CLK_PLL1,	CLK_TYPE_GEN4_PLL1, CLK_MAIN),
	DEF_BASE(".pll2", CLK_PLL2,	CLK_TYPE_GEN4_PLL2, CLK_MAIN),
	DEF_BASE(".pll3", CLK_PLL3,	CLK_TYPE_GEN4_PLL3, CLK_MAIN),
	DEF_BASE(".pll5", CLK_PLL5,	CLK_TYPE_GEN4_PLL5, CLK_MAIN),
	DEF_BASE(".pll6", CLK_PLL6,	CLK_TYPE_GEN4_PLL6, CLK_MAIN),

	DEF_FIXED(".pll1_div2",	CLK_PLL1_DIV2,	CLK_PLL1,	2, 1),
	DEF_FIXED(".pll2_div2",	CLK_PLL2_DIV2,	CLK_PLL2,	2, 1),
	DEF_FIXED(".pll3_div2",	CLK_PLL3_DIV2,	CLK_PLL3,	2, 1),
	DEF_FIXED(".pll5_div2",	CLK_PLL5_DIV2,	CLK_PLL5,	2, 1),
	DEF_FIXED(".pll5_div4",	CLK_PLL5_DIV4,	CLK_PLL5_DIV2,	2, 1),
	DEF_FIXED(".pll6_div2",	CLK_PLL6_DIV2,	CLK_PLL6,	2, 1),
	DEF_FIXED(".s0",	CLK_S0,		CLK_PLL1_DIV2,	2, 1),
	DEF_BASE(".sdsrc",	CLK_SDSRC,	CLK_TYPE_GEN4_SDSRC, CLK_PLL5),
	DEF_RATE(".oco",	CLK_OCO,	32768),

	DEF_BASE(".rpcsrc",	CLK_RPCSRC,		CLK_TYPE_GEN4_RPCSRC, CLK_PLL5),
	DEF_BASE(".rpc",	R8A779F0_CLK_RPC,	CLK_TYPE_GEN4_RPC, CLK_RPCSRC),
	DEF_BASE("rpcd2",	R8A779F0_CLK_RPCD2,	CLK_TYPE_GEN4_RPCD2, R8A779F0_CLK_RPC),

	/* Core Clock Outputs */
	DEF_FIXED("s0d2",	R8A779F0_CLK_S0D2,	CLK_S0,		2, 1),
	DEF_FIXED("s0d3",	R8A779F0_CLK_S0D3,	CLK_S0,		3, 1),
	DEF_FIXED("s0d4",	R8A779F0_CLK_S0D4,	CLK_S0,		4, 1),
	DEF_FIXED("cl16m",	R8A779F0_CLK_CL16M,	CLK_S0,		48, 1),
	DEF_FIXED("s0d2_mm",	R8A779F0_CLK_S0D2_MM,	CLK_S0,		2, 1),
	DEF_FIXED("s0d3_mm",	R8A779F0_CLK_S0D3_MM,	CLK_S0,		3, 1),
	DEF_FIXED("s0d4_mm",	R8A779F0_CLK_S0D4_MM,	CLK_S0,		4, 1),
	DEF_FIXED("cl16m_mm",	R8A779F0_CLK_CL16M_MM,	CLK_S0,		48, 1),
	DEF_FIXED("s0d2_rt",	R8A779F0_CLK_S0D2_RT,	CLK_S0,		2, 1),
	DEF_FIXED("s0d3_rt",	R8A779F0_CLK_S0D3_RT,	CLK_S0,		3, 1),
	DEF_FIXED("s0d4_rt",	R8A779F0_CLK_S0D4_RT,	CLK_S0,		4, 1),
	DEF_FIXED("s0d6_rt",	R8A779F0_CLK_S0D6_RT,	CLK_S0,		6, 1),
	DEF_FIXED("cl16m_rt",	R8A779F0_CLK_CL16M_RT,	CLK_S0,		48, 1),
	DEF_FIXED("s0d3_per",	R8A779F0_CLK_S0D3_PER,	CLK_S0,		3, 1),
	DEF_FIXED("s0d6_per",	R8A779F0_CLK_S0D6_PER,	CLK_S0,		6, 1),
	DEF_FIXED("s0d12_per",	R8A779F0_CLK_S0D12_PER,	CLK_S0,		12, 1),
	DEF_FIXED("s0d24_per",	R8A779F0_CLK_S0D24_PER,	CLK_S0,		24, 1),
	DEF_FIXED("cl16m_per",	R8A779F0_CLK_CL16M_PER,	CLK_S0,		48, 1),
	DEF_FIXED("s0d2_hsc",	R8A779F0_CLK_S0D2_HSC,	CLK_S0,		2, 1),
	DEF_FIXED("s0d3_hsc",	R8A779F0_CLK_S0D3_HSC,	CLK_S0,		3, 1),
	DEF_FIXED("s0d4_hsc",	R8A779F0_CLK_S0D4_HSC,	CLK_S0,		4, 1),
	DEF_FIXED("s0d6_hsc",	R8A779F0_CLK_S0D6_HSC,	CLK_S0,		6, 1),
	DEF_FIXED("s0d12_hsc",	R8A779F0_CLK_S0D12_HSC,	CLK_S0,		12, 1),
	DEF_FIXED("cl16m_hsc",	R8A779F0_CLK_CL16M_HSC,	CLK_S0,		48, 1),
	DEF_FIXED("s0d2_cc",	R8A779F0_CLK_S0D2_CC,	CLK_S0,		2, 1),
	DEF_FIXED("rsw2",	R8A779F0_CLK_RSW2,	CLK_PLL5,	2, 1),
	DEF_FIXED("cbfusa",	R8A779F0_CLK_CBFUSA,	CLK_EXTAL,	2, 1),
	DEF_FIXED("cpex",	R8A779F0_CLK_CPEX,	CLK_EXTAL,	2, 1),

	DEF_GEN4_SD("sd0",	R8A779F0_CLK_SD0,	CLK_SDSRC,	0x870),
	DEF_DIV6P1("mso",	R8A779F0_CLK_MSO,	CLK_PLL5_DIV4,	0x87c),

	DEF_GEN4_OSC("osc",	R8A779F0_CLK_OSC,	CLK_EXTAL,	8),
	DEF_GEN4_MDSEL("r",	R8A779F0_CLK_R, 29, CLK_EXTALR, 1, CLK_OCO, 1),
};

static const struct mssr_mod_clk r8a779f0_mod_clks[] __initconst = {
	DEF_MOD("scif0",	702,	R8A779F0_CLK_S0D12_PER),
	DEF_MOD("scif1",	703,	R8A779F0_CLK_S0D12_PER),
	DEF_MOD("scif3",	704,	R8A779F0_CLK_S0D12_PER),
	DEF_MOD("scif4",	705,	R8A779F0_CLK_S0D12_PER),
	DEF_MOD("sys-dmac0",	709,	R8A779F0_CLK_S0D3_PER),
	DEF_MOD("sys-dmac1",	710,	R8A779F0_CLK_S0D3_PER),
};

/*
 * CPG Clock Data
 */
/*
 *   MD	 EXTAL		PLL1	PLL2	PLL3	PLL5	PLL6	OSC
 * 14 13 (MHz)
 * ----------------------------------------------------------------
 * 0  0	 16    / 1	x200	x150	x200	x200	x134	/15
 * 0  1	 20    / 1	x160	x120	x160	x160	x106	/19
 * 1  0	 Prohibited setting
 * 1  1	 40    / 2	x160	x120	x160	x160	x106	/38
 */
#define CPG_PLL_CONFIG_INDEX(md)	((((md) & BIT(14)) >> 13) | \
					 (((md) & BIT(13)) >> 13))

static const struct rcar_gen4_cpg_pll_config cpg_pll_configs[4] = {
	/* EXTAL div	PLL1 mult/div	PLL2 mult/div	PLL3 mult/div	PLL5 mult/div	PLL6 mult/div	OSC prediv */
	{ 1,		200,	1,	150,	1,	200,	1,	200,	1,	134,	1,	15,	},
	{ 1,		160,	1,	120,	1,	160,	1,	160,	1,	106,	1,	19,	},
	{ 0,		0,	0,	0,	0,	0,	0,	0,	0,	0,	0,	0,	},
	{ 2,		160,	1,	120,	1,	160,	1,	160,	1,	106,	1,	38,	},
};

static int __init r8a779f0_cpg_mssr_init(struct device *dev)
{
	const struct rcar_gen4_cpg_pll_config *cpg_pll_config;
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

	return rcar_gen4_cpg_init(cpg_pll_config, CLK_EXTALR, cpg_mode);
}

const struct cpg_mssr_info r8a779f0_cpg_mssr_info __initconst = {
	/* Core Clocks */
	.core_clks = r8a779f0_core_clks,
	.num_core_clks = ARRAY_SIZE(r8a779f0_core_clks),
	.last_dt_core_clk = LAST_DT_CORE_CLK,
	.num_total_core_clks = MOD_CLK_BASE,

	/* Module Clocks */
	.mod_clks = r8a779f0_mod_clks,
	.num_mod_clks = ARRAY_SIZE(r8a779f0_mod_clks),
	.num_hw_mod_clks = 28 * 32,

	/* Callbacks */
	.init = r8a779f0_cpg_mssr_init,
	.cpg_clk_register = rcar_gen4_cpg_clk_register,

	.reg_layout = CLK_REG_LAYOUT_RCAR_GEN4,
};

// SPDX-License-Identifier: GPL-2.0
/*
 * RZ/G2L CPG driver
 *
 * Copyright (C) 2021 Renesas Electronics Corp.
 */

#include <linux/clk-provider.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/kernel.h>

#include <dt-bindings/clock/r9a07g044-cpg.h>

#include "renesas-rzg2l-cpg.h"

enum clk_ids {
	/* Core Clock Outputs exported to DT */
	LAST_DT_CORE_CLK = R9A07G044_OSCCLK,

	/* External Input Clocks */
	CLK_EXTAL,

	/* Internal Core Clocks */
	CLK_OSC_DIV1000,
	CLK_PLL1,
	CLK_PLL2,
	CLK_PLL2_DIV2,
	CLK_PLL2_DIV16,
	CLK_PLL2_DIV20,
	CLK_PLL3,
	CLK_PLL3_DIV2,
	CLK_PLL3_DIV4,
	CLK_PLL3_DIV8,
	CLK_PLL4,
	CLK_PLL5,
	CLK_PLL5_DIV2,
	CLK_PLL6,

	/* Module Clocks */
	MOD_CLK_BASE,
};

/* Divider tables */
static const struct clk_div_table dtable_3b[] = {
	{0, 1},
	{1, 2},
	{2, 4},
	{3, 8},
	{4, 32},
};

static const struct cpg_core_clk r9a07g044_core_clks[] __initconst = {
	/* External Clock Inputs */
	DEF_INPUT("extal", CLK_EXTAL),

	/* Internal Core Clocks */
	DEF_FIXED(".osc", R9A07G044_OSCCLK, CLK_EXTAL, 1, 1),
	DEF_FIXED(".osc_div1000", CLK_OSC_DIV1000, CLK_EXTAL, 1, 1000),
	DEF_SAMPLL(".pll1", CLK_PLL1, CLK_EXTAL, PLL146_CONF(0)),
	DEF_FIXED(".pll2", CLK_PLL2, CLK_EXTAL, 133, 2),
	DEF_FIXED(".pll3", CLK_PLL3, CLK_EXTAL, 133, 2),

	DEF_FIXED(".pll2_div2", CLK_PLL2_DIV2, CLK_PLL2, 1, 2),
	DEF_FIXED(".pll2_div16", CLK_PLL2_DIV16, CLK_PLL2, 1, 16),
	DEF_FIXED(".pll2_div20", CLK_PLL2_DIV20, CLK_PLL2, 1, 20),

	DEF_FIXED(".pll3_div2", CLK_PLL3_DIV2, CLK_PLL3, 1, 2),
	DEF_FIXED(".pll3_div4", CLK_PLL3_DIV4, CLK_PLL3, 1, 4),
	DEF_FIXED(".pll3_div8", CLK_PLL3_DIV8, CLK_PLL3, 1, 8),

	/* Core output clk */
	DEF_FIXED("I", R9A07G044_CLK_I, CLK_PLL1, 1, 1),
	DEF_DIV("P0", R9A07G044_CLK_P0, CLK_PLL2_DIV16, DIVPL2A,
		dtable_3b, CLK_DIVIDER_HIWORD_MASK),
	DEF_FIXED("TSU", R9A07G044_CLK_TSU, CLK_PLL2_DIV20, 1, 1),
	DEF_DIV("P1", R9A07G044_CLK_P1, CLK_PLL3_DIV8,
		DIVPL3B, dtable_3b, CLK_DIVIDER_HIWORD_MASK),
};

static struct rzg2l_mod_clk r9a07g044_mod_clks[] = {
	DEF_MOD("gic",		R9A07G044_CLK_GIC600,
				R9A07G044_CLK_P1,
				0x514, BIT(0), (BIT(0) | BIT(1))),
	DEF_MOD("ia55",		R9A07G044_CLK_IA55,
				R9A07G044_CLK_P1,
				0x518, (BIT(0) | BIT(1)), BIT(0)),
	DEF_MOD("scif0",	R9A07G044_CLK_SCIF0,
				R9A07G044_CLK_P0,
				0x584, BIT(0), BIT(0)),
	DEF_MOD("scif1",	R9A07G044_CLK_SCIF1,
				R9A07G044_CLK_P0,
				0x584, BIT(1), BIT(1)),
	DEF_MOD("scif2",	R9A07G044_CLK_SCIF2,
				R9A07G044_CLK_P0,
				0x584, BIT(2), BIT(2)),
	DEF_MOD("scif3",	R9A07G044_CLK_SCIF3,
				R9A07G044_CLK_P0,
				0x584, BIT(3), BIT(3)),
	DEF_MOD("scif4",	R9A07G044_CLK_SCIF4,
				R9A07G044_CLK_P0,
				0x584, BIT(4), BIT(4)),
	DEF_MOD("sci0",		R9A07G044_CLK_SCI0,
				R9A07G044_CLK_P0,
				0x588, BIT(0), BIT(0)),
};

static const unsigned int r9a07g044_crit_mod_clks[] __initconst = {
	MOD_CLK_BASE + R9A07G044_CLK_GIC600,
};

const struct rzg2l_cpg_info r9a07g044_cpg_info = {
	/* Core Clocks */
	.core_clks = r9a07g044_core_clks,
	.num_core_clks = ARRAY_SIZE(r9a07g044_core_clks),
	.last_dt_core_clk = LAST_DT_CORE_CLK,
	.num_total_core_clks = MOD_CLK_BASE,

	/* Critical Module Clocks */
	.crit_mod_clks = r9a07g044_crit_mod_clks,
	.num_crit_mod_clks = ARRAY_SIZE(r9a07g044_crit_mod_clks),

	/* Module Clocks */
	.mod_clks = r9a07g044_mod_clks,
	.num_mod_clks = ARRAY_SIZE(r9a07g044_mod_clks),
	.num_hw_mod_clks = R9A07G044_CLK_MIPI_DSI_PIN + 1,
};

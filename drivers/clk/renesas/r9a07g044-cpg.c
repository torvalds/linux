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
	CLK_PLL3_DIV2_4,
	CLK_PLL3_DIV2_4_2,
	CLK_PLL3_DIV4,
	CLK_PLL4,
	CLK_PLL5,
	CLK_PLL5_DIV2,
	CLK_PLL6,

	/* Module Clocks */
	MOD_CLK_BASE,
};

/* Divider tables */
static const struct clk_div_table dtable_1_32[] = {
	{0, 1},
	{1, 2},
	{2, 4},
	{3, 8},
	{4, 32},
	{0, 0},
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
	DEF_FIXED(".pll3_div2_4", CLK_PLL3_DIV2_4, CLK_PLL3_DIV2, 1, 4),
	DEF_FIXED(".pll3_div2_4_2", CLK_PLL3_DIV2_4_2, CLK_PLL3_DIV2_4, 1, 2),
	DEF_FIXED(".pll3_div4", CLK_PLL3_DIV4, CLK_PLL3, 1, 4),

	/* Core output clk */
	DEF_FIXED("I", R9A07G044_CLK_I, CLK_PLL1, 1, 1),
	DEF_DIV("P0", R9A07G044_CLK_P0, CLK_PLL2_DIV16, DIVPL2A,
		dtable_1_32, CLK_DIVIDER_HIWORD_MASK),
	DEF_FIXED("TSU", R9A07G044_CLK_TSU, CLK_PLL2_DIV20, 1, 1),
	DEF_DIV("P1", R9A07G044_CLK_P1, CLK_PLL3_DIV2_4,
		DIVPL3B, dtable_1_32, CLK_DIVIDER_HIWORD_MASK),
	DEF_DIV("P2", R9A07G044_CLK_P2, CLK_PLL3_DIV2_4_2,
		DIVPL3A, dtable_1_32, CLK_DIVIDER_HIWORD_MASK),
};

static struct rzg2l_mod_clk r9a07g044_mod_clks[] = {
	DEF_MOD("gic",		R9A07G044_GIC600_GICCLK, R9A07G044_CLK_P1,
				0x514, 0),
	DEF_MOD("ia55_pclk",	R9A07G044_IA55_PCLK, R9A07G044_CLK_P2,
				0x518, 0),
	DEF_MOD("ia55_clk",	R9A07G044_IA55_CLK, R9A07G044_CLK_P1,
				0x518, 1),
	DEF_MOD("scif0",	R9A07G044_SCIF0_CLK_PCK, R9A07G044_CLK_P0,
				0x584, 0),
	DEF_MOD("scif1",	R9A07G044_SCIF1_CLK_PCK, R9A07G044_CLK_P0,
				0x584, 1),
	DEF_MOD("scif2",	R9A07G044_SCIF2_CLK_PCK, R9A07G044_CLK_P0,
				0x584, 2),
	DEF_MOD("scif3",	R9A07G044_SCIF3_CLK_PCK, R9A07G044_CLK_P0,
				0x584, 3),
	DEF_MOD("scif4",	R9A07G044_SCIF4_CLK_PCK, R9A07G044_CLK_P0,
				0x584, 4),
	DEF_MOD("sci0",		R9A07G044_SCI0_CLKP, R9A07G044_CLK_P0,
				0x588, 0),
};

static struct rzg2l_reset r9a07g044_resets[] = {
	DEF_RST(R9A07G044_GIC600_GICRESET_N, 0x814, 0),
	DEF_RST(R9A07G044_GIC600_DBG_GICRESET_N, 0x814, 1),
	DEF_RST(R9A07G044_IA55_RESETN, 0x818, 0),
	DEF_RST(R9A07G044_SCIF0_RST_SYSTEM_N, 0x884, 0),
	DEF_RST(R9A07G044_SCIF1_RST_SYSTEM_N, 0x884, 1),
	DEF_RST(R9A07G044_SCIF2_RST_SYSTEM_N, 0x884, 2),
	DEF_RST(R9A07G044_SCIF3_RST_SYSTEM_N, 0x884, 3),
	DEF_RST(R9A07G044_SCIF4_RST_SYSTEM_N, 0x884, 4),
	DEF_RST(R9A07G044_SCI0_RST, 0x888, 0),
};

static const unsigned int r9a07g044_crit_mod_clks[] __initconst = {
	MOD_CLK_BASE + R9A07G044_GIC600_GICCLK,
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
	.num_hw_mod_clks = R9A07G044_TSU_PCLK + 1,

	/* Resets */
	.resets = r9a07g044_resets,
	.num_resets = ARRAY_SIZE(r9a07g044_resets),
};

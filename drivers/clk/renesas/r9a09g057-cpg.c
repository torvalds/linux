// SPDX-License-Identifier: GPL-2.0
/*
 * Renesas RZ/V2H(P) CPG driver
 *
 * Copyright (C) 2024 Renesas Electronics Corp.
 */

#include <linux/clk-provider.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/kernel.h>

#include <dt-bindings/clock/renesas,r9a09g057-cpg.h>

#include "rzv2h-cpg.h"

enum clk_ids {
	/* Core Clock Outputs exported to DT */
	LAST_DT_CORE_CLK = R9A09G057_IOTOP_0_SHCLK,

	/* External Input Clocks */
	CLK_AUDIO_EXTAL,
	CLK_RTXIN,
	CLK_QEXTAL,

	/* PLL Clocks */
	CLK_PLLCM33,
	CLK_PLLDTY,
	CLK_PLLCA55,

	/* Internal Core Clocks */
	CLK_PLLCM33_DIV16,

	/* Module Clocks */
	MOD_CLK_BASE,
};

static const struct cpg_core_clk r9a09g057_core_clks[] __initconst = {
	/* External Clock Inputs */
	DEF_INPUT("audio_extal", CLK_AUDIO_EXTAL),
	DEF_INPUT("rtxin", CLK_RTXIN),
	DEF_INPUT("qextal", CLK_QEXTAL),

	/* PLL Clocks */
	DEF_FIXED(".pllcm33", CLK_PLLCM33, CLK_QEXTAL, 200, 3),
	DEF_FIXED(".plldty", CLK_PLLDTY, CLK_QEXTAL, 200, 3),
	DEF_PLL(".pllca55", CLK_PLLCA55, CLK_QEXTAL, PLL_CONF(0x64)),

	/* Internal Core Clocks */
	DEF_FIXED(".pllcm33_div16", CLK_PLLCM33_DIV16, CLK_PLLCM33, 1, 16),

	/* Core Clocks */
	DEF_FIXED("sys_0_pclk", R9A09G057_SYS_0_PCLK, CLK_QEXTAL, 1, 1),
	DEF_FIXED("iotop_0_shclk", R9A09G057_IOTOP_0_SHCLK, CLK_PLLCM33_DIV16, 1, 1),
};

static const struct rzv2h_mod_clk r9a09g057_mod_clks[] __initconst = {
	DEF_MOD("scif_0_clk_pck",		CLK_PLLCM33_DIV16, 8, 15, 4, 15),
};

static const struct rzv2h_reset r9a09g057_resets[] __initconst = {
	DEF_RST(9, 5, 4, 6),		/* SCIF_0_RST_SYSTEM_N */
};

const struct rzv2h_cpg_info r9a09g057_cpg_info __initconst = {
	/* Core Clocks */
	.core_clks = r9a09g057_core_clks,
	.num_core_clks = ARRAY_SIZE(r9a09g057_core_clks),
	.last_dt_core_clk = LAST_DT_CORE_CLK,
	.num_total_core_clks = MOD_CLK_BASE,

	/* Module Clocks */
	.mod_clks = r9a09g057_mod_clks,
	.num_mod_clks = ARRAY_SIZE(r9a09g057_mod_clks),
	.num_hw_mod_clks = 25 * 16,

	/* Resets */
	.resets = r9a09g057_resets,
	.num_resets = ARRAY_SIZE(r9a09g057_resets),
};

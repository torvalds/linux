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
	CLK_PLLCLN,
	CLK_PLLDTY,
	CLK_PLLCA55,

	/* Internal Core Clocks */
	CLK_PLLCM33_DIV16,
	CLK_PLLCLN_DIV2,
	CLK_PLLCLN_DIV8,
	CLK_PLLCLN_DIV16,
	CLK_PLLDTY_ACPU,
	CLK_PLLDTY_ACPU_DIV4,

	/* Module Clocks */
	MOD_CLK_BASE,
};

static const struct clk_div_table dtable_2_64[] = {
	{0, 2},
	{1, 4},
	{2, 8},
	{3, 16},
	{4, 64},
	{0, 0},
};

static const struct cpg_core_clk r9a09g057_core_clks[] __initconst = {
	/* External Clock Inputs */
	DEF_INPUT("audio_extal", CLK_AUDIO_EXTAL),
	DEF_INPUT("rtxin", CLK_RTXIN),
	DEF_INPUT("qextal", CLK_QEXTAL),

	/* PLL Clocks */
	DEF_FIXED(".pllcm33", CLK_PLLCM33, CLK_QEXTAL, 200, 3),
	DEF_FIXED(".pllcln", CLK_PLLCLN, CLK_QEXTAL, 200, 3),
	DEF_FIXED(".plldty", CLK_PLLDTY, CLK_QEXTAL, 200, 3),
	DEF_PLL(".pllca55", CLK_PLLCA55, CLK_QEXTAL, PLL_CONF(0x64)),

	/* Internal Core Clocks */
	DEF_FIXED(".pllcm33_div16", CLK_PLLCM33_DIV16, CLK_PLLCM33, 1, 16),

	DEF_FIXED(".pllcln_div2", CLK_PLLCLN_DIV2, CLK_PLLCLN, 1, 2),
	DEF_FIXED(".pllcln_div8", CLK_PLLCLN_DIV8, CLK_PLLCLN, 1, 8),
	DEF_FIXED(".pllcln_div16", CLK_PLLCLN_DIV16, CLK_PLLCLN, 1, 16),

	DEF_DDIV(".plldty_acpu", CLK_PLLDTY_ACPU, CLK_PLLDTY, CDDIV0_DIVCTL2, dtable_2_64),
	DEF_FIXED(".plldty_acpu_div4", CLK_PLLDTY_ACPU_DIV4, CLK_PLLDTY_ACPU, 1, 4),

	/* Core Clocks */
	DEF_FIXED("sys_0_pclk", R9A09G057_SYS_0_PCLK, CLK_QEXTAL, 1, 1),
	DEF_FIXED("iotop_0_shclk", R9A09G057_IOTOP_0_SHCLK, CLK_PLLCM33_DIV16, 1, 1),
};

static const struct rzv2h_mod_clk r9a09g057_mod_clks[] __initconst = {
	DEF_MOD("gtm_0_pclk",			CLK_PLLCM33_DIV16, 4, 3, 2, 3),
	DEF_MOD("gtm_1_pclk",			CLK_PLLCM33_DIV16, 4, 4, 2, 4),
	DEF_MOD("gtm_2_pclk",			CLK_PLLCLN_DIV16, 4, 5, 2, 5),
	DEF_MOD("gtm_3_pclk",			CLK_PLLCLN_DIV16, 4, 6, 2, 6),
	DEF_MOD("gtm_4_pclk",			CLK_PLLCLN_DIV16, 4, 7, 2, 7),
	DEF_MOD("gtm_5_pclk",			CLK_PLLCLN_DIV16, 4, 8, 2, 8),
	DEF_MOD("gtm_6_pclk",			CLK_PLLCLN_DIV16, 4, 9, 2, 9),
	DEF_MOD("gtm_7_pclk",			CLK_PLLCLN_DIV16, 4, 10, 2, 10),
	DEF_MOD("wdt_0_clkp",			CLK_PLLCM33_DIV16, 4, 11, 2, 11),
	DEF_MOD("wdt_0_clk_loco",		CLK_QEXTAL, 4, 12, 2, 12),
	DEF_MOD("wdt_1_clkp",			CLK_PLLCLN_DIV16, 4, 13, 2, 13),
	DEF_MOD("wdt_1_clk_loco",		CLK_QEXTAL, 4, 14, 2, 14),
	DEF_MOD("wdt_2_clkp",			CLK_PLLCLN_DIV16, 4, 15, 2, 15),
	DEF_MOD("wdt_2_clk_loco",		CLK_QEXTAL, 5, 0, 2, 16),
	DEF_MOD("wdt_3_clkp",			CLK_PLLCLN_DIV16, 5, 1, 2, 17),
	DEF_MOD("wdt_3_clk_loco",		CLK_QEXTAL, 5, 2, 2, 18),
	DEF_MOD("scif_0_clk_pck",		CLK_PLLCM33_DIV16, 8, 15, 4, 15),
	DEF_MOD("riic_8_ckm",			CLK_PLLCM33_DIV16, 9, 3, 4, 19),
	DEF_MOD("riic_0_ckm",			CLK_PLLCLN_DIV16, 9, 4, 4, 20),
	DEF_MOD("riic_1_ckm",			CLK_PLLCLN_DIV16, 9, 5, 4, 21),
	DEF_MOD("riic_2_ckm",			CLK_PLLCLN_DIV16, 9, 6, 4, 22),
	DEF_MOD("riic_3_ckm",			CLK_PLLCLN_DIV16, 9, 7, 4, 23),
	DEF_MOD("riic_4_ckm",			CLK_PLLCLN_DIV16, 9, 8, 4, 24),
	DEF_MOD("riic_5_ckm",			CLK_PLLCLN_DIV16, 9, 9, 4, 25),
	DEF_MOD("riic_6_ckm",			CLK_PLLCLN_DIV16, 9, 10, 4, 26),
	DEF_MOD("riic_7_ckm",			CLK_PLLCLN_DIV16, 9, 11, 4, 27),
	DEF_MOD("sdhi_0_imclk",			CLK_PLLCLN_DIV8, 10, 3, 5, 3),
	DEF_MOD("sdhi_0_imclk2",		CLK_PLLCLN_DIV8, 10, 4, 5, 4),
	DEF_MOD("sdhi_0_clk_hs",		CLK_PLLCLN_DIV2, 10, 5, 5, 5),
	DEF_MOD("sdhi_0_aclk",			CLK_PLLDTY_ACPU_DIV4, 10, 6, 5, 6),
	DEF_MOD("sdhi_1_imclk",			CLK_PLLCLN_DIV8, 10, 7, 5, 7),
	DEF_MOD("sdhi_1_imclk2",		CLK_PLLCLN_DIV8, 10, 8, 5, 8),
	DEF_MOD("sdhi_1_clk_hs",		CLK_PLLCLN_DIV2, 10, 9, 5, 9),
	DEF_MOD("sdhi_1_aclk",			CLK_PLLDTY_ACPU_DIV4, 10, 10, 5, 10),
	DEF_MOD("sdhi_2_imclk",			CLK_PLLCLN_DIV8, 10, 11, 5, 11),
	DEF_MOD("sdhi_2_imclk2",		CLK_PLLCLN_DIV8, 10, 12, 5, 12),
	DEF_MOD("sdhi_2_clk_hs",		CLK_PLLCLN_DIV2, 10, 13, 5, 13),
	DEF_MOD("sdhi_2_aclk",			CLK_PLLDTY_ACPU_DIV4, 10, 14, 5, 14),
};

static const struct rzv2h_reset r9a09g057_resets[] __initconst = {
	DEF_RST(6, 13, 2, 30),		/* GTM_0_PRESETZ */
	DEF_RST(6, 14, 2, 31),		/* GTM_1_PRESETZ */
	DEF_RST(6, 15, 3, 0),		/* GTM_2_PRESETZ */
	DEF_RST(7, 0, 3, 1),		/* GTM_3_PRESETZ */
	DEF_RST(7, 1, 3, 2),		/* GTM_4_PRESETZ */
	DEF_RST(7, 2, 3, 3),		/* GTM_5_PRESETZ */
	DEF_RST(7, 3, 3, 4),		/* GTM_6_PRESETZ */
	DEF_RST(7, 4, 3, 5),		/* GTM_7_PRESETZ */
	DEF_RST(7, 5, 3, 6),		/* WDT_0_RESET */
	DEF_RST(7, 6, 3, 7),		/* WDT_1_RESET */
	DEF_RST(7, 7, 3, 8),		/* WDT_2_RESET */
	DEF_RST(7, 8, 3, 9),		/* WDT_3_RESET */
	DEF_RST(9, 5, 4, 6),		/* SCIF_0_RST_SYSTEM_N */
	DEF_RST(9, 8, 4, 9),		/* RIIC_0_MRST */
	DEF_RST(9, 9, 4, 10),		/* RIIC_1_MRST */
	DEF_RST(9, 10, 4, 11),		/* RIIC_2_MRST */
	DEF_RST(9, 11, 4, 12),		/* RIIC_3_MRST */
	DEF_RST(9, 12, 4, 13),		/* RIIC_4_MRST */
	DEF_RST(9, 13, 4, 14),		/* RIIC_5_MRST */
	DEF_RST(9, 14, 4, 15),		/* RIIC_6_MRST */
	DEF_RST(9, 15, 4, 16),		/* RIIC_7_MRST */
	DEF_RST(10, 0, 4, 17),		/* RIIC_8_MRST */
	DEF_RST(10, 7, 4, 24),		/* SDHI_0_IXRST */
	DEF_RST(10, 8, 4, 25),		/* SDHI_1_IXRST */
	DEF_RST(10, 9, 4, 26),		/* SDHI_2_IXRST */
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

// SPDX-License-Identifier: GPL-2.0
/*
 * Renesas RZ/G3E CPG driver
 *
 * Copyright (C) 2024 Renesas Electronics Corp.
 */

#include <linux/clk-provider.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/kernel.h>

#include <dt-bindings/clock/renesas,r9a09g047-cpg.h>

#include "rzv2h-cpg.h"

enum clk_ids {
	/* Core Clock Outputs exported to DT */
	LAST_DT_CORE_CLK = R9A09G047_IOTOP_0_SHCLK,

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
	CLK_PLLCLN_DIV16,
	CLK_PLLDTY_ACPU,
	CLK_PLLDTY_ACPU_DIV4,

	/* Module Clocks */
	MOD_CLK_BASE,
};

static const struct clk_div_table dtable_1_8[] = {
	{0, 1},
	{1, 2},
	{2, 4},
	{3, 8},
	{0, 0},
};

static const struct clk_div_table dtable_2_64[] = {
	{0, 2},
	{1, 4},
	{2, 8},
	{3, 16},
	{4, 64},
	{0, 0},
};

static const struct cpg_core_clk r9a09g047_core_clks[] __initconst = {
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

	DEF_FIXED(".pllcln_div16", CLK_PLLCLN_DIV16, CLK_PLLCLN, 1, 16),

	DEF_DDIV(".plldty_acpu", CLK_PLLDTY_ACPU, CLK_PLLDTY, CDDIV0_DIVCTL2, dtable_2_64),
	DEF_FIXED(".plldty_acpu_div4", CLK_PLLDTY_ACPU_DIV4, CLK_PLLDTY_ACPU, 1, 4),

	/* Core Clocks */
	DEF_FIXED("sys_0_pclk", R9A09G047_SYS_0_PCLK, CLK_QEXTAL, 1, 1),
	DEF_DDIV("ca55_0_coreclk0", R9A09G047_CA55_0_CORECLK0, CLK_PLLCA55,
		 CDDIV1_DIVCTL0, dtable_1_8),
	DEF_DDIV("ca55_0_coreclk1", R9A09G047_CA55_0_CORECLK1, CLK_PLLCA55,
		 CDDIV1_DIVCTL1, dtable_1_8),
	DEF_DDIV("ca55_0_coreclk2", R9A09G047_CA55_0_CORECLK2, CLK_PLLCA55,
		 CDDIV1_DIVCTL2, dtable_1_8),
	DEF_DDIV("ca55_0_coreclk3", R9A09G047_CA55_0_CORECLK3, CLK_PLLCA55,
		 CDDIV1_DIVCTL3, dtable_1_8),
	DEF_FIXED("iotop_0_shclk", R9A09G047_IOTOP_0_SHCLK, CLK_PLLCM33_DIV16, 1, 1),
};

static const struct rzv2h_mod_clk r9a09g047_mod_clks[] __initconst = {
	DEF_MOD_CRITICAL("gic_0_gicclk",	CLK_PLLDTY_ACPU_DIV4, 1, 3, 0, 19,
						BUS_MSTOP(3, BIT(5))),
	DEF_MOD("scif_0_clk_pck",		CLK_PLLCM33_DIV16, 8, 15, 4, 15,
						BUS_MSTOP(3, BIT(14))),
	DEF_MOD("riic_8_ckm",			CLK_PLLCM33_DIV16, 9, 3, 4, 19,
						BUS_MSTOP(3, BIT(13))),
	DEF_MOD("riic_0_ckm",			CLK_PLLCLN_DIV16, 9, 4, 4, 20,
						BUS_MSTOP(1, BIT(1))),
	DEF_MOD("riic_1_ckm",			CLK_PLLCLN_DIV16, 9, 5, 4, 21,
						BUS_MSTOP(1, BIT(2))),
	DEF_MOD("riic_2_ckm",			CLK_PLLCLN_DIV16, 9, 6, 4, 22,
						BUS_MSTOP(1, BIT(3))),
	DEF_MOD("riic_3_ckm",			CLK_PLLCLN_DIV16, 9, 7, 4, 23,
						BUS_MSTOP(1, BIT(4))),
	DEF_MOD("riic_4_ckm",			CLK_PLLCLN_DIV16, 9, 8, 4, 24,
						BUS_MSTOP(1, BIT(5))),
	DEF_MOD("riic_5_ckm",			CLK_PLLCLN_DIV16, 9, 9, 4, 25,
						BUS_MSTOP(1, BIT(6))),
	DEF_MOD("riic_6_ckm",			CLK_PLLCLN_DIV16, 9, 10, 4, 26,
						BUS_MSTOP(1, BIT(7))),
	DEF_MOD("riic_7_ckm",			CLK_PLLCLN_DIV16, 9, 11, 4, 27,
						BUS_MSTOP(1, BIT(8))),
};

static const struct rzv2h_reset r9a09g047_resets[] __initconst = {
	DEF_RST(3, 0, 1, 1),		/* SYS_0_PRESETN */
	DEF_RST(3, 8, 1, 9),		/* GIC_0_GICRESET_N */
	DEF_RST(3, 9, 1, 10),		/* GIC_0_DBG_GICRESET_N */
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
};

const struct rzv2h_cpg_info r9a09g047_cpg_info __initconst = {
	/* Core Clocks */
	.core_clks = r9a09g047_core_clks,
	.num_core_clks = ARRAY_SIZE(r9a09g047_core_clks),
	.last_dt_core_clk = LAST_DT_CORE_CLK,
	.num_total_core_clks = MOD_CLK_BASE,

	/* Module Clocks */
	.mod_clks = r9a09g047_mod_clks,
	.num_mod_clks = ARRAY_SIZE(r9a09g047_mod_clks),
	.num_hw_mod_clks = 28 * 16,

	/* Resets */
	.resets = r9a09g047_resets,
	.num_resets = ARRAY_SIZE(r9a09g047_resets),

	.num_mstop_bits = 208,
};

// SPDX-License-Identifier: GPL-2.0
/*
 * Renesas RZ/V2N CPG driver
 *
 * Copyright (C) 2025 Renesas Electronics Corp.
 */

#include <linux/clk-provider.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/kernel.h>

#include <dt-bindings/clock/renesas,r9a09g056-cpg.h>

#include "rzv2h-cpg.h"

enum clk_ids {
	/* Core Clock Outputs exported to DT */
	LAST_DT_CORE_CLK = R9A09G056_GBETH_1_CLK_PTP_REF_I,

	/* External Input Clocks */
	CLK_AUDIO_EXTAL,
	CLK_RTXIN,
	CLK_QEXTAL,

	/* PLL Clocks */
	CLK_PLLCM33,
	CLK_PLLCLN,
	CLK_PLLDTY,
	CLK_PLLCA55,
	CLK_PLLETH,

	/* Internal Core Clocks */
	CLK_PLLCM33_DIV16,
	CLK_PLLCLN_DIV2,
	CLK_PLLCLN_DIV8,
	CLK_PLLCLN_DIV16,
	CLK_PLLDTY_ACPU,
	CLK_PLLDTY_ACPU_DIV4,
	CLK_PLLDTY_DIV8,
	CLK_PLLETH_DIV_250_FIX,
	CLK_PLLETH_DIV_125_FIX,
	CLK_CSDIV_PLLETH_GBE0,
	CLK_CSDIV_PLLETH_GBE1,
	CLK_SMUX2_GBE0_TXCLK,
	CLK_SMUX2_GBE0_RXCLK,
	CLK_SMUX2_GBE1_TXCLK,
	CLK_SMUX2_GBE1_RXCLK,

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

static const struct clk_div_table dtable_2_100[] = {
	{0, 2},
	{1, 10},
	{2, 100},
	{0, 0},
};

/* Mux clock tables */
static const char * const smux2_gbe0_rxclk[] = { ".plleth_gbe0", "et0_rxclk" };
static const char * const smux2_gbe0_txclk[] = { ".plleth_gbe0", "et0_txclk" };
static const char * const smux2_gbe1_rxclk[] = { ".plleth_gbe1", "et1_rxclk" };
static const char * const smux2_gbe1_txclk[] = { ".plleth_gbe1", "et1_txclk" };

static const struct cpg_core_clk r9a09g056_core_clks[] __initconst = {
	/* External Clock Inputs */
	DEF_INPUT("audio_extal", CLK_AUDIO_EXTAL),
	DEF_INPUT("rtxin", CLK_RTXIN),
	DEF_INPUT("qextal", CLK_QEXTAL),

	/* PLL Clocks */
	DEF_FIXED(".pllcm33", CLK_PLLCM33, CLK_QEXTAL, 200, 3),
	DEF_FIXED(".pllcln", CLK_PLLCLN, CLK_QEXTAL, 200, 3),
	DEF_FIXED(".plldty", CLK_PLLDTY, CLK_QEXTAL, 200, 3),
	DEF_PLL(".pllca55", CLK_PLLCA55, CLK_QEXTAL, PLLCA55),
	DEF_FIXED(".plleth", CLK_PLLETH, CLK_QEXTAL, 125, 3),

	/* Internal Core Clocks */
	DEF_FIXED(".pllcm33_div16", CLK_PLLCM33_DIV16, CLK_PLLCM33, 1, 16),

	DEF_FIXED(".pllcln_div2", CLK_PLLCLN_DIV2, CLK_PLLCLN, 1, 2),
	DEF_FIXED(".pllcln_div8", CLK_PLLCLN_DIV8, CLK_PLLCLN, 1, 8),
	DEF_FIXED(".pllcln_div16", CLK_PLLCLN_DIV16, CLK_PLLCLN, 1, 16),

	DEF_DDIV(".plldty_acpu", CLK_PLLDTY_ACPU, CLK_PLLDTY, CDDIV0_DIVCTL2, dtable_2_64),
	DEF_FIXED(".plldty_acpu_div4", CLK_PLLDTY_ACPU_DIV4, CLK_PLLDTY_ACPU, 1, 4),
	DEF_FIXED(".plldty_div8", CLK_PLLDTY_DIV8, CLK_PLLDTY, 1, 8),

	DEF_FIXED(".plleth_250_fix", CLK_PLLETH_DIV_250_FIX, CLK_PLLETH, 1, 4),
	DEF_FIXED(".plleth_125_fix", CLK_PLLETH_DIV_125_FIX, CLK_PLLETH_DIV_250_FIX, 1, 2),
	DEF_CSDIV(".plleth_gbe0", CLK_CSDIV_PLLETH_GBE0,
		  CLK_PLLETH_DIV_250_FIX, CSDIV0_DIVCTL0, dtable_2_100),
	DEF_CSDIV(".plleth_gbe1", CLK_CSDIV_PLLETH_GBE1,
		  CLK_PLLETH_DIV_250_FIX, CSDIV0_DIVCTL1, dtable_2_100),
	DEF_SMUX(".smux2_gbe0_txclk", CLK_SMUX2_GBE0_TXCLK, SSEL0_SELCTL2, smux2_gbe0_txclk),
	DEF_SMUX(".smux2_gbe0_rxclk", CLK_SMUX2_GBE0_RXCLK, SSEL0_SELCTL3, smux2_gbe0_rxclk),
	DEF_SMUX(".smux2_gbe1_txclk", CLK_SMUX2_GBE1_TXCLK, SSEL1_SELCTL0, smux2_gbe1_txclk),
	DEF_SMUX(".smux2_gbe1_rxclk", CLK_SMUX2_GBE1_RXCLK, SSEL1_SELCTL1, smux2_gbe1_rxclk),

	/* Core Clocks */
	DEF_FIXED("sys_0_pclk", R9A09G056_SYS_0_PCLK, CLK_QEXTAL, 1, 1),
	DEF_DDIV("ca55_0_coreclk0", R9A09G056_CA55_0_CORE_CLK0, CLK_PLLCA55,
		 CDDIV1_DIVCTL0, dtable_1_8),
	DEF_DDIV("ca55_0_coreclk1", R9A09G056_CA55_0_CORE_CLK1, CLK_PLLCA55,
		 CDDIV1_DIVCTL1, dtable_1_8),
	DEF_DDIV("ca55_0_coreclk2", R9A09G056_CA55_0_CORE_CLK2, CLK_PLLCA55,
		 CDDIV1_DIVCTL2, dtable_1_8),
	DEF_DDIV("ca55_0_coreclk3", R9A09G056_CA55_0_CORE_CLK3, CLK_PLLCA55,
		 CDDIV1_DIVCTL3, dtable_1_8),
	DEF_FIXED("iotop_0_shclk", R9A09G056_IOTOP_0_SHCLK, CLK_PLLCM33_DIV16, 1, 1),
	DEF_FIXED("gbeth_0_clk_ptp_ref_i", R9A09G056_GBETH_0_CLK_PTP_REF_I,
		  CLK_PLLETH_DIV_125_FIX, 1, 1),
	DEF_FIXED("gbeth_1_clk_ptp_ref_i", R9A09G056_GBETH_1_CLK_PTP_REF_I,
		  CLK_PLLETH_DIV_125_FIX, 1, 1),
};

static const struct rzv2h_mod_clk r9a09g056_mod_clks[] __initconst = {
	DEF_MOD_CRITICAL("gic_0_gicclk",	CLK_PLLDTY_ACPU_DIV4, 1, 3, 0, 19,
						BUS_MSTOP(3, BIT(5))),
	DEF_MOD("gtm_0_pclk",			CLK_PLLCM33_DIV16, 4, 3, 2, 3,
						BUS_MSTOP(5, BIT(10))),
	DEF_MOD("gtm_1_pclk",			CLK_PLLCM33_DIV16, 4, 4, 2, 4,
						BUS_MSTOP(5, BIT(11))),
	DEF_MOD("gtm_2_pclk",			CLK_PLLCLN_DIV16, 4, 5, 2, 5,
						BUS_MSTOP(2, BIT(13))),
	DEF_MOD("gtm_3_pclk",			CLK_PLLCLN_DIV16, 4, 6, 2, 6,
						BUS_MSTOP(2, BIT(14))),
	DEF_MOD("gtm_4_pclk",			CLK_PLLCLN_DIV16, 4, 7, 2, 7,
						BUS_MSTOP(11, BIT(13))),
	DEF_MOD("gtm_5_pclk",			CLK_PLLCLN_DIV16, 4, 8, 2, 8,
						BUS_MSTOP(11, BIT(14))),
	DEF_MOD("gtm_6_pclk",			CLK_PLLCLN_DIV16, 4, 9, 2, 9,
						BUS_MSTOP(11, BIT(15))),
	DEF_MOD("gtm_7_pclk",			CLK_PLLCLN_DIV16, 4, 10, 2, 10,
						BUS_MSTOP(12, BIT(0))),
	DEF_MOD("scif_0_clk_pck",		CLK_PLLCM33_DIV16, 8, 15, 4, 15,
						BUS_MSTOP(3, BIT(14))),
	DEF_MOD("sdhi_0_imclk",			CLK_PLLCLN_DIV8, 10, 3, 5, 3,
						BUS_MSTOP(8, BIT(2))),
	DEF_MOD("sdhi_0_imclk2",		CLK_PLLCLN_DIV8, 10, 4, 5, 4,
						BUS_MSTOP(8, BIT(2))),
	DEF_MOD("sdhi_0_clk_hs",		CLK_PLLCLN_DIV2, 10, 5, 5, 5,
						BUS_MSTOP(8, BIT(2))),
	DEF_MOD("sdhi_0_aclk",			CLK_PLLDTY_ACPU_DIV4, 10, 6, 5, 6,
						BUS_MSTOP(8, BIT(2))),
	DEF_MOD("sdhi_1_imclk",			CLK_PLLCLN_DIV8, 10, 7, 5, 7,
						BUS_MSTOP(8, BIT(3))),
	DEF_MOD("sdhi_1_imclk2",		CLK_PLLCLN_DIV8, 10, 8, 5, 8,
						BUS_MSTOP(8, BIT(3))),
	DEF_MOD("sdhi_1_clk_hs",		CLK_PLLCLN_DIV2, 10, 9, 5, 9,
						BUS_MSTOP(8, BIT(3))),
	DEF_MOD("sdhi_1_aclk",			CLK_PLLDTY_ACPU_DIV4, 10, 10, 5, 10,
						BUS_MSTOP(8, BIT(3))),
	DEF_MOD("sdhi_2_imclk",			CLK_PLLCLN_DIV8, 10, 11, 5, 11,
						BUS_MSTOP(8, BIT(4))),
	DEF_MOD("sdhi_2_imclk2",		CLK_PLLCLN_DIV8, 10, 12, 5, 12,
						BUS_MSTOP(8, BIT(4))),
	DEF_MOD("sdhi_2_clk_hs",		CLK_PLLCLN_DIV2, 10, 13, 5, 13,
						BUS_MSTOP(8, BIT(4))),
	DEF_MOD("sdhi_2_aclk",			CLK_PLLDTY_ACPU_DIV4, 10, 14, 5, 14,
						BUS_MSTOP(8, BIT(4))),
	DEF_MOD_MUX_EXTERNAL("gbeth_0_clk_tx_i", CLK_SMUX2_GBE0_TXCLK, 11, 8, 5, 24,
						BUS_MSTOP(8, BIT(5)), 1),
	DEF_MOD_MUX_EXTERNAL("gbeth_0_clk_rx_i", CLK_SMUX2_GBE0_RXCLK, 11, 9, 5, 25,
						BUS_MSTOP(8, BIT(5)), 1),
	DEF_MOD_MUX_EXTERNAL("gbeth_0_clk_tx_180_i", CLK_SMUX2_GBE0_TXCLK, 11, 10, 5, 26,
						BUS_MSTOP(8, BIT(5)), 1),
	DEF_MOD_MUX_EXTERNAL("gbeth_0_clk_rx_180_i", CLK_SMUX2_GBE0_RXCLK, 11, 11, 5, 27,
						BUS_MSTOP(8, BIT(5)), 1),
	DEF_MOD("gbeth_0_aclk_csr_i",		CLK_PLLDTY_DIV8, 11, 12, 5, 28,
						BUS_MSTOP(8, BIT(5))),
	DEF_MOD("gbeth_0_aclk_i",		CLK_PLLDTY_DIV8, 11, 13, 5, 29,
						BUS_MSTOP(8, BIT(5))),
	DEF_MOD_MUX_EXTERNAL("gbeth_1_clk_tx_i", CLK_SMUX2_GBE1_TXCLK, 11, 14, 5, 30,
						BUS_MSTOP(8, BIT(6)), 1),
	DEF_MOD_MUX_EXTERNAL("gbeth_1_clk_rx_i", CLK_SMUX2_GBE1_RXCLK, 11, 15, 5, 31,
						BUS_MSTOP(8, BIT(6)), 1),
	DEF_MOD_MUX_EXTERNAL("gbeth_1_clk_tx_180_i", CLK_SMUX2_GBE1_TXCLK, 12, 0, 6, 0,
						BUS_MSTOP(8, BIT(6)), 1),
	DEF_MOD_MUX_EXTERNAL("gbeth_1_clk_rx_180_i", CLK_SMUX2_GBE1_RXCLK, 12, 1, 6, 1,
						BUS_MSTOP(8, BIT(6)), 1),
	DEF_MOD("gbeth_1_aclk_csr_i",		CLK_PLLDTY_DIV8, 12, 2, 6, 2,
						BUS_MSTOP(8, BIT(6))),
	DEF_MOD("gbeth_1_aclk_i",		CLK_PLLDTY_DIV8, 12, 3, 6, 3,
						BUS_MSTOP(8, BIT(6))),
};

static const struct rzv2h_reset r9a09g056_resets[] __initconst = {
	DEF_RST(3, 0, 1, 1),		/* SYS_0_PRESETN */
	DEF_RST(3, 8, 1, 9),		/* GIC_0_GICRESET_N */
	DEF_RST(3, 9, 1, 10),		/* GIC_0_DBG_GICRESET_N */
	DEF_RST(6, 13, 2, 30),		/* GTM_0_PRESETZ */
	DEF_RST(6, 14, 2, 31),		/* GTM_1_PRESETZ */
	DEF_RST(6, 15, 3, 0),		/* GTM_2_PRESETZ */
	DEF_RST(7, 0, 3, 1),		/* GTM_3_PRESETZ */
	DEF_RST(7, 1, 3, 2),		/* GTM_4_PRESETZ */
	DEF_RST(7, 2, 3, 3),		/* GTM_5_PRESETZ */
	DEF_RST(7, 3, 3, 4),		/* GTM_6_PRESETZ */
	DEF_RST(7, 4, 3, 5),		/* GTM_7_PRESETZ */
	DEF_RST(9, 5, 4, 6),		/* SCIF_0_RST_SYSTEM_N */
	DEF_RST(10, 7, 4, 24),		/* SDHI_0_IXRST */
	DEF_RST(10, 8, 4, 25),		/* SDHI_1_IXRST */
	DEF_RST(10, 9, 4, 26),		/* SDHI_2_IXRST */
	DEF_RST(11, 0, 5, 1),		/* GBETH_0_ARESETN_I */
	DEF_RST(11, 1, 5, 2),		/* GBETH_1_ARESETN_I */
};

const struct rzv2h_cpg_info r9a09g056_cpg_info __initconst = {
	/* Core Clocks */
	.core_clks = r9a09g056_core_clks,
	.num_core_clks = ARRAY_SIZE(r9a09g056_core_clks),
	.last_dt_core_clk = LAST_DT_CORE_CLK,
	.num_total_core_clks = MOD_CLK_BASE,

	/* Module Clocks */
	.mod_clks = r9a09g056_mod_clks,
	.num_mod_clks = ARRAY_SIZE(r9a09g056_mod_clks),
	.num_hw_mod_clks = 25 * 16,

	/* Resets */
	.resets = r9a09g056_resets,
	.num_resets = ARRAY_SIZE(r9a09g056_resets),

	.num_mstop_bits = 192,
};

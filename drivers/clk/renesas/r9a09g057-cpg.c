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
	LAST_DT_CORE_CLK = R9A09G057_SPI_CLK_SPI,

	/* External Input Clocks */
	CLK_AUDIO_EXTAL,
	CLK_RTXIN,
	CLK_QEXTAL,

	/* PLL Clocks */
	CLK_PLLCM33,
	CLK_PLLCLN,
	CLK_PLLDTY,
	CLK_PLLCA55,
	CLK_PLLVDO,
	CLK_PLLETH,
	CLK_PLLGPU,

	/* Internal Core Clocks */
	CLK_PLLCM33_DIV3,
	CLK_PLLCM33_DIV4,
	CLK_PLLCM33_DIV5,
	CLK_PLLCM33_DIV16,
	CLK_PLLCM33_GEAR,
	CLK_SMUX2_XSPI_CLK0,
	CLK_SMUX2_XSPI_CLK1,
	CLK_PLLCM33_XSPI,
	CLK_PLLCLN_DIV2,
	CLK_PLLCLN_DIV8,
	CLK_PLLCLN_DIV16,
	CLK_PLLDTY_ACPU,
	CLK_PLLDTY_ACPU_DIV2,
	CLK_PLLDTY_ACPU_DIV4,
	CLK_PLLDTY_DIV8,
	CLK_PLLDTY_DIV16,
	CLK_PLLDTY_RCPU,
	CLK_PLLDTY_RCPU_DIV4,
	CLK_PLLVDO_CRU0,
	CLK_PLLVDO_CRU1,
	CLK_PLLVDO_CRU2,
	CLK_PLLVDO_CRU3,
	CLK_PLLETH_DIV_250_FIX,
	CLK_PLLETH_DIV_125_FIX,
	CLK_CSDIV_PLLETH_GBE0,
	CLK_CSDIV_PLLETH_GBE1,
	CLK_SMUX2_GBE0_TXCLK,
	CLK_SMUX2_GBE0_RXCLK,
	CLK_SMUX2_GBE1_TXCLK,
	CLK_SMUX2_GBE1_RXCLK,
	CLK_PLLGPU_GEAR,

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

static const struct clk_div_table dtable_2_4[] = {
	{0, 2},
	{1, 4},
	{0, 0},
};

static const struct clk_div_table dtable_2_16[] = {
	{0, 2},
	{1, 4},
	{2, 8},
	{3, 16},
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
static const char * const smux2_xspi_clk0[] = { ".pllcm33_div3", ".pllcm33_div4" };
static const char * const smux2_xspi_clk1[] = { ".smux2_xspi_clk0", ".pllcm33_div5" };

static const struct cpg_core_clk r9a09g057_core_clks[] __initconst = {
	/* External Clock Inputs */
	DEF_INPUT("audio_extal", CLK_AUDIO_EXTAL),
	DEF_INPUT("rtxin", CLK_RTXIN),
	DEF_INPUT("qextal", CLK_QEXTAL),

	/* PLL Clocks */
	DEF_FIXED(".pllcm33", CLK_PLLCM33, CLK_QEXTAL, 200, 3),
	DEF_FIXED(".pllcln", CLK_PLLCLN, CLK_QEXTAL, 200, 3),
	DEF_FIXED(".plldty", CLK_PLLDTY, CLK_QEXTAL, 200, 3),
	DEF_PLL(".pllca55", CLK_PLLCA55, CLK_QEXTAL, PLLCA55),
	DEF_FIXED(".pllvdo", CLK_PLLVDO, CLK_QEXTAL, 105, 2),
	DEF_FIXED(".plleth", CLK_PLLETH, CLK_QEXTAL, 125, 3),
	DEF_PLL(".pllgpu", CLK_PLLGPU, CLK_QEXTAL, PLLGPU),

	/* Internal Core Clocks */
	DEF_FIXED(".pllcm33_div3", CLK_PLLCM33_DIV3, CLK_PLLCM33, 1, 3),
	DEF_FIXED(".pllcm33_div4", CLK_PLLCM33_DIV4, CLK_PLLCM33, 1, 4),
	DEF_FIXED(".pllcm33_div5", CLK_PLLCM33_DIV5, CLK_PLLCM33, 1, 5),
	DEF_FIXED(".pllcm33_div16", CLK_PLLCM33_DIV16, CLK_PLLCM33, 1, 16),
	DEF_DDIV(".pllcm33_gear", CLK_PLLCM33_GEAR, CLK_PLLCM33_DIV4, CDDIV0_DIVCTL1, dtable_2_64),
	DEF_SMUX(".smux2_xspi_clk0", CLK_SMUX2_XSPI_CLK0, SSEL1_SELCTL2, smux2_xspi_clk0),
	DEF_SMUX(".smux2_xspi_clk1", CLK_SMUX2_XSPI_CLK1, SSEL1_SELCTL3, smux2_xspi_clk1),
	DEF_CSDIV(".pllcm33_xspi", CLK_PLLCM33_XSPI, CLK_SMUX2_XSPI_CLK1, CSDIV0_DIVCTL3,
		  dtable_2_16),

	DEF_FIXED(".pllcln_div2", CLK_PLLCLN_DIV2, CLK_PLLCLN, 1, 2),
	DEF_FIXED(".pllcln_div8", CLK_PLLCLN_DIV8, CLK_PLLCLN, 1, 8),
	DEF_FIXED(".pllcln_div16", CLK_PLLCLN_DIV16, CLK_PLLCLN, 1, 16),

	DEF_DDIV(".plldty_acpu", CLK_PLLDTY_ACPU, CLK_PLLDTY, CDDIV0_DIVCTL2, dtable_2_64),
	DEF_FIXED(".plldty_acpu_div2", CLK_PLLDTY_ACPU_DIV2, CLK_PLLDTY_ACPU, 1, 2),
	DEF_FIXED(".plldty_acpu_div4", CLK_PLLDTY_ACPU_DIV4, CLK_PLLDTY_ACPU, 1, 4),
	DEF_FIXED(".plldty_div8", CLK_PLLDTY_DIV8, CLK_PLLDTY, 1, 8),
	DEF_FIXED(".plldty_div16", CLK_PLLDTY_DIV16, CLK_PLLDTY, 1, 16),
	DEF_DDIV(".plldty_rcpu", CLK_PLLDTY_RCPU, CLK_PLLDTY, CDDIV3_DIVCTL2, dtable_2_64),
	DEF_FIXED(".plldty_rcpu_div4", CLK_PLLDTY_RCPU_DIV4, CLK_PLLDTY_RCPU, 1, 4),

	DEF_DDIV(".pllvdo_cru0", CLK_PLLVDO_CRU0, CLK_PLLVDO, CDDIV3_DIVCTL3, dtable_2_4),
	DEF_DDIV(".pllvdo_cru1", CLK_PLLVDO_CRU1, CLK_PLLVDO, CDDIV4_DIVCTL0, dtable_2_4),
	DEF_DDIV(".pllvdo_cru2", CLK_PLLVDO_CRU2, CLK_PLLVDO, CDDIV4_DIVCTL1, dtable_2_4),
	DEF_DDIV(".pllvdo_cru3", CLK_PLLVDO_CRU3, CLK_PLLVDO, CDDIV4_DIVCTL2, dtable_2_4),

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

	DEF_DDIV(".pllgpu_gear", CLK_PLLGPU_GEAR, CLK_PLLGPU, CDDIV3_DIVCTL1, dtable_2_64),

	/* Core Clocks */
	DEF_FIXED("sys_0_pclk", R9A09G057_SYS_0_PCLK, CLK_QEXTAL, 1, 1),
	DEF_DDIV("ca55_0_coreclk0", R9A09G057_CA55_0_CORE_CLK0, CLK_PLLCA55,
		 CDDIV1_DIVCTL0, dtable_1_8),
	DEF_DDIV("ca55_0_coreclk1", R9A09G057_CA55_0_CORE_CLK1, CLK_PLLCA55,
		 CDDIV1_DIVCTL1, dtable_1_8),
	DEF_DDIV("ca55_0_coreclk2", R9A09G057_CA55_0_CORE_CLK2, CLK_PLLCA55,
		 CDDIV1_DIVCTL2, dtable_1_8),
	DEF_DDIV("ca55_0_coreclk3", R9A09G057_CA55_0_CORE_CLK3, CLK_PLLCA55,
		 CDDIV1_DIVCTL3, dtable_1_8),
	DEF_FIXED("iotop_0_shclk", R9A09G057_IOTOP_0_SHCLK, CLK_PLLCM33_DIV16, 1, 1),
	DEF_FIXED("usb2_0_clk_core0", R9A09G057_USB2_0_CLK_CORE0, CLK_QEXTAL, 1, 1),
	DEF_FIXED("usb2_0_clk_core1", R9A09G057_USB2_0_CLK_CORE1, CLK_QEXTAL, 1, 1),
	DEF_FIXED("gbeth_0_clk_ptp_ref_i", R9A09G057_GBETH_0_CLK_PTP_REF_I,
		  CLK_PLLETH_DIV_125_FIX, 1, 1),
	DEF_FIXED("gbeth_1_clk_ptp_ref_i", R9A09G057_GBETH_1_CLK_PTP_REF_I,
		  CLK_PLLETH_DIV_125_FIX, 1, 1),
	DEF_FIXED_MOD_STATUS("spi_clk_spi", R9A09G057_SPI_CLK_SPI, CLK_PLLCM33_XSPI, 1, 2,
			     FIXED_MOD_CONF_XSPI),
};

static const struct rzv2h_mod_clk r9a09g057_mod_clks[] __initconst = {
	DEF_MOD("dmac_0_aclk",			CLK_PLLCM33_GEAR, 0, 0, 0, 0,
						BUS_MSTOP(5, BIT(9))),
	DEF_MOD("dmac_1_aclk",			CLK_PLLDTY_ACPU_DIV2, 0, 1, 0, 1,
						BUS_MSTOP(3, BIT(2))),
	DEF_MOD("dmac_2_aclk",			CLK_PLLDTY_ACPU_DIV2, 0, 2, 0, 2,
						BUS_MSTOP(3, BIT(3))),
	DEF_MOD("dmac_3_aclk",			CLK_PLLDTY_RCPU_DIV4, 0, 3, 0, 3,
						BUS_MSTOP(10, BIT(11))),
	DEF_MOD("dmac_4_aclk",			CLK_PLLDTY_RCPU_DIV4, 0, 4, 0, 4,
						BUS_MSTOP(10, BIT(12))),
	DEF_MOD_CRITICAL("icu_0_pclk_i",	CLK_PLLCM33_DIV16, 0, 5, 0, 5,
						BUS_MSTOP_NONE),
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
	DEF_MOD("wdt_0_clkp",			CLK_PLLCM33_DIV16, 4, 11, 2, 11,
						BUS_MSTOP(3, BIT(10))),
	DEF_MOD("wdt_0_clk_loco",		CLK_QEXTAL, 4, 12, 2, 12,
						BUS_MSTOP(3, BIT(10))),
	DEF_MOD("wdt_1_clkp",			CLK_PLLCLN_DIV16, 4, 13, 2, 13,
						BUS_MSTOP(1, BIT(0))),
	DEF_MOD("wdt_1_clk_loco",		CLK_QEXTAL, 4, 14, 2, 14,
						BUS_MSTOP(1, BIT(0))),
	DEF_MOD("wdt_2_clkp",			CLK_PLLCLN_DIV16, 4, 15, 2, 15,
						BUS_MSTOP(5, BIT(12))),
	DEF_MOD("wdt_2_clk_loco",		CLK_QEXTAL, 5, 0, 2, 16,
						BUS_MSTOP(5, BIT(12))),
	DEF_MOD("wdt_3_clkp",			CLK_PLLCLN_DIV16, 5, 1, 2, 17,
						BUS_MSTOP(5, BIT(13))),
	DEF_MOD("wdt_3_clk_loco",		CLK_QEXTAL, 5, 2, 2, 18,
						BUS_MSTOP(5, BIT(13))),
	DEF_MOD("rspi_0_pclk",			CLK_PLLCLN_DIV8, 5, 4, 2, 20,
						BUS_MSTOP(11, BIT(0))),
	DEF_MOD("rspi_0_pclk_sfr",		CLK_PLLCLN_DIV8, 5, 5, 2, 21,
						BUS_MSTOP(11, BIT(0))),
	DEF_MOD("rspi_0_tclk",			CLK_PLLCLN_DIV8, 5, 6, 2, 22,
						BUS_MSTOP(11, BIT(0))),
	DEF_MOD("rspi_1_pclk",			CLK_PLLCLN_DIV8, 5, 7, 2, 23,
						BUS_MSTOP(11, BIT(1))),
	DEF_MOD("rspi_1_pclk_sfr",		CLK_PLLCLN_DIV8, 5, 8, 2, 24,
						BUS_MSTOP(11, BIT(1))),
	DEF_MOD("rspi_1_tclk",			CLK_PLLCLN_DIV8, 5, 9, 2, 25,
						BUS_MSTOP(11, BIT(1))),
	DEF_MOD("rspi_2_pclk",			CLK_PLLCLN_DIV8, 5, 10, 2, 26,
						BUS_MSTOP(11, BIT(2))),
	DEF_MOD("rspi_2_pclk_sfr",		CLK_PLLCLN_DIV8, 5, 11, 2, 27,
						BUS_MSTOP(11, BIT(2))),
	DEF_MOD("rspi_2_tclk",			CLK_PLLCLN_DIV8, 5, 12, 2, 28,
						BUS_MSTOP(11, BIT(2))),
	DEF_MOD("scif_0_clk_pck",		CLK_PLLCM33_DIV16, 8, 15, 4, 15,
						BUS_MSTOP(3, BIT(14))),
	DEF_MOD("i3c_0_pclkrw",			CLK_PLLCLN_DIV16, 9, 0, 4, 16,
						BUS_MSTOP(10, BIT(15))),
	DEF_MOD("i3c_0_pclk",			CLK_PLLCLN_DIV16, 9, 1, 4, 17,
						BUS_MSTOP(10, BIT(15))),
	DEF_MOD("i3c_0_tclk",			CLK_PLLCLN_DIV8, 9, 2, 4, 18,
						BUS_MSTOP(10, BIT(15))),
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
	DEF_MOD("spi_hclk",			CLK_PLLCM33_GEAR, 9, 15, 4, 31,
						BUS_MSTOP(4, BIT(5))),
	DEF_MOD("spi_aclk",			CLK_PLLCM33_GEAR, 10, 0, 5, 0,
						BUS_MSTOP(4, BIT(5))),
	DEF_MOD("spi_clk_spix2",		CLK_PLLCM33_XSPI, 10, 1, 5, 2,
						BUS_MSTOP(4, BIT(5))),
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
	DEF_MOD("usb2_0_u2h0_hclk",		CLK_PLLDTY_DIV8, 11, 3, 5, 19,
						BUS_MSTOP(7, BIT(7))),
	DEF_MOD("usb2_0_u2h1_hclk",		CLK_PLLDTY_DIV8, 11, 4, 5, 20,
						BUS_MSTOP(7, BIT(8))),
	DEF_MOD("usb2_0_u2p_exr_cpuclk",	CLK_PLLDTY_ACPU_DIV4, 11, 5, 5, 21,
						BUS_MSTOP(7, BIT(9))),
	DEF_MOD("usb2_0_pclk_usbtst0",		CLK_PLLDTY_ACPU_DIV4, 11, 6, 5, 22,
						BUS_MSTOP(7, BIT(10))),
	DEF_MOD("usb2_0_pclk_usbtst1",		CLK_PLLDTY_ACPU_DIV4, 11, 7, 5, 23,
						BUS_MSTOP(7, BIT(11))),
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
	DEF_MOD("cru_0_aclk",			CLK_PLLDTY_ACPU_DIV2, 13, 2, 6, 18,
						BUS_MSTOP(9, BIT(4))),
	DEF_MOD_NO_PM("cru_0_vclk",		CLK_PLLVDO_CRU0, 13, 3, 6, 19,
						BUS_MSTOP(9, BIT(4))),
	DEF_MOD("cru_0_pclk",			CLK_PLLDTY_DIV16, 13, 4, 6, 20,
						BUS_MSTOP(9, BIT(4))),
	DEF_MOD("cru_1_aclk",			CLK_PLLDTY_ACPU_DIV2, 13, 5, 6, 21,
						BUS_MSTOP(9, BIT(5))),
	DEF_MOD_NO_PM("cru_1_vclk",		CLK_PLLVDO_CRU1, 13, 6, 6, 22,
						BUS_MSTOP(9, BIT(5))),
	DEF_MOD("cru_1_pclk",			CLK_PLLDTY_DIV16, 13, 7, 6, 23,
						BUS_MSTOP(9, BIT(5))),
	DEF_MOD("cru_2_aclk",			CLK_PLLDTY_ACPU_DIV2, 13, 8, 6, 24,
						BUS_MSTOP(9, BIT(6))),
	DEF_MOD_NO_PM("cru_2_vclk",		CLK_PLLVDO_CRU2, 13, 9, 6, 25,
						BUS_MSTOP(9, BIT(6))),
	DEF_MOD("cru_2_pclk",			CLK_PLLDTY_DIV16, 13, 10, 6, 26,
						BUS_MSTOP(9, BIT(6))),
	DEF_MOD("cru_3_aclk",			CLK_PLLDTY_ACPU_DIV2, 13, 11, 6, 27,
						BUS_MSTOP(9, BIT(7))),
	DEF_MOD_NO_PM("cru_3_vclk",		CLK_PLLVDO_CRU3, 13, 12, 6, 28,
						BUS_MSTOP(9, BIT(7))),
	DEF_MOD("cru_3_pclk",			CLK_PLLDTY_DIV16, 13, 13, 6, 29,
						BUS_MSTOP(9, BIT(7))),
	DEF_MOD("gpu_0_clk",			CLK_PLLGPU_GEAR, 15, 0, 7, 16,
						BUS_MSTOP(3, BIT(4))),
	DEF_MOD("gpu_0_axi_clk",		CLK_PLLDTY_ACPU_DIV2, 15, 1, 7, 17,
						BUS_MSTOP(3, BIT(4))),
	DEF_MOD("gpu_0_ace_clk",		CLK_PLLDTY_ACPU_DIV2, 15, 2, 7, 18,
						BUS_MSTOP(3, BIT(4))),
};

static const struct rzv2h_reset r9a09g057_resets[] __initconst = {
	DEF_RST(3, 0, 1, 1),		/* SYS_0_PRESETN */
	DEF_RST(3, 1, 1, 2),		/* DMAC_0_ARESETN */
	DEF_RST(3, 2, 1, 3),		/* DMAC_1_ARESETN */
	DEF_RST(3, 3, 1, 4),		/* DMAC_2_ARESETN */
	DEF_RST(3, 4, 1, 5),		/* DMAC_3_ARESETN */
	DEF_RST(3, 5, 1, 6),		/* DMAC_4_ARESETN */
	DEF_RST(3, 6, 1, 7),		/* ICU_0_PRESETN_I */
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
	DEF_RST(7, 5, 3, 6),		/* WDT_0_RESET */
	DEF_RST(7, 6, 3, 7),		/* WDT_1_RESET */
	DEF_RST(7, 7, 3, 8),		/* WDT_2_RESET */
	DEF_RST(7, 8, 3, 9),		/* WDT_3_RESET */
	DEF_RST(7, 11, 3, 12),		/* RSPI_0_PRESETN */
	DEF_RST(7, 12, 3, 13),		/* RSPI_0_TRESETN */
	DEF_RST(7, 13, 3, 14),		/* RSPI_1_PRESETN */
	DEF_RST(7, 14, 3, 15),		/* RSPI_1_TRESETN */
	DEF_RST(7, 15, 3, 16),		/* RSPI_2_PRESETN */
	DEF_RST(8, 0, 3, 17),		/* RSPI_2_TRESETN */
	DEF_RST(9, 5, 4, 6),		/* SCIF_0_RST_SYSTEM_N */
	DEF_RST(9, 6, 4, 7),		/* I3C_0_PRESETN */
	DEF_RST(9, 7, 4, 8),		/* I3C_0_TRESETN */
	DEF_RST(9, 8, 4, 9),		/* RIIC_0_MRST */
	DEF_RST(9, 9, 4, 10),		/* RIIC_1_MRST */
	DEF_RST(9, 10, 4, 11),		/* RIIC_2_MRST */
	DEF_RST(9, 11, 4, 12),		/* RIIC_3_MRST */
	DEF_RST(9, 12, 4, 13),		/* RIIC_4_MRST */
	DEF_RST(9, 13, 4, 14),		/* RIIC_5_MRST */
	DEF_RST(9, 14, 4, 15),		/* RIIC_6_MRST */
	DEF_RST(9, 15, 4, 16),		/* RIIC_7_MRST */
	DEF_RST(10, 0, 4, 17),		/* RIIC_8_MRST */
	DEF_RST(10, 3, 4, 20),		/* SPI_HRESETN */
	DEF_RST(10, 4, 4, 21),		/* SPI_ARESETN */
	DEF_RST(10, 7, 4, 24),		/* SDHI_0_IXRST */
	DEF_RST(10, 8, 4, 25),		/* SDHI_1_IXRST */
	DEF_RST(10, 9, 4, 26),		/* SDHI_2_IXRST */
	DEF_RST(10, 12, 4, 29),		/* USB2_0_U2H0_HRESETN */
	DEF_RST(10, 13, 4, 30),		/* USB2_0_U2H1_HRESETN */
	DEF_RST(10, 14, 4, 31),		/* USB2_0_U2P_EXL_SYSRST */
	DEF_RST(10, 15, 5, 0),		/* USB2_0_PRESETN */
	DEF_RST(11, 0, 5, 1),		/* GBETH_0_ARESETN_I */
	DEF_RST(11, 1, 5, 2),		/* GBETH_1_ARESETN_I */
	DEF_RST(12, 5, 5, 22),		/* CRU_0_PRESETN */
	DEF_RST(12, 6, 5, 23),		/* CRU_0_ARESETN */
	DEF_RST(12, 7, 5, 24),		/* CRU_0_S_RESETN */
	DEF_RST(12, 8, 5, 25),		/* CRU_1_PRESETN */
	DEF_RST(12, 9, 5, 26),		/* CRU_1_ARESETN */
	DEF_RST(12, 10, 5, 27),		/* CRU_1_S_RESETN */
	DEF_RST(12, 11, 5, 28),		/* CRU_2_PRESETN */
	DEF_RST(12, 12, 5, 29),		/* CRU_2_ARESETN */
	DEF_RST(12, 13, 5, 30),		/* CRU_2_S_RESETN */
	DEF_RST(12, 14, 5, 31),		/* CRU_3_PRESETN */
	DEF_RST(12, 15, 6, 0),		/* CRU_3_ARESETN */
	DEF_RST(13, 0, 6, 1),		/* CRU_3_S_RESETN */
	DEF_RST(13, 13, 6, 14),		/* GPU_0_RESETN */
	DEF_RST(13, 14, 6, 15),		/* GPU_0_AXI_RESETN */
	DEF_RST(13, 15, 6, 16),		/* GPU_0_ACE_RESETN */
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

	.num_mstop_bits = 192,
};

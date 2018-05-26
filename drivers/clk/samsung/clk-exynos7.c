/*
 * Copyright (c) 2014 Samsung Electronics Co., Ltd.
 * Author: Naveen Krishna Ch <naveenkrishna.ch@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
*/

#include <linux/clk-provider.h>
#include <linux/of.h>

#include "clk.h"
#include <dt-bindings/clock/exynos7-clk.h>

/* Register Offset definitions for CMU_TOPC (0x10570000) */
#define CC_PLL_LOCK		0x0000
#define BUS0_PLL_LOCK		0x0004
#define BUS1_DPLL_LOCK		0x0008
#define MFC_PLL_LOCK		0x000C
#define AUD_PLL_LOCK		0x0010
#define CC_PLL_CON0		0x0100
#define BUS0_PLL_CON0		0x0110
#define BUS1_DPLL_CON0		0x0120
#define MFC_PLL_CON0		0x0130
#define AUD_PLL_CON0		0x0140
#define MUX_SEL_TOPC0		0x0200
#define MUX_SEL_TOPC1		0x0204
#define MUX_SEL_TOPC2		0x0208
#define MUX_SEL_TOPC3		0x020C
#define DIV_TOPC0		0x0600
#define DIV_TOPC1		0x0604
#define DIV_TOPC3		0x060C
#define ENABLE_ACLK_TOPC0	0x0800
#define ENABLE_ACLK_TOPC1	0x0804
#define ENABLE_SCLK_TOPC1	0x0A04

static const struct samsung_fixed_factor_clock topc_fixed_factor_clks[] __initconst = {
	FFACTOR(0, "ffac_topc_bus0_pll_div2", "mout_topc_bus0_pll", 1, 2, 0),
	FFACTOR(0, "ffac_topc_bus0_pll_div4",
		"ffac_topc_bus0_pll_div2", 1, 2, 0),
	FFACTOR(0, "ffac_topc_bus1_pll_div2", "mout_topc_bus1_pll", 1, 2, 0),
	FFACTOR(0, "ffac_topc_cc_pll_div2", "mout_topc_cc_pll", 1, 2, 0),
	FFACTOR(0, "ffac_topc_mfc_pll_div2", "mout_topc_mfc_pll", 1, 2, 0),
};

/* List of parent clocks for Muxes in CMU_TOPC */
PNAME(mout_topc_aud_pll_ctrl_p)	= { "fin_pll", "fout_aud_pll" };
PNAME(mout_topc_bus0_pll_ctrl_p)	= { "fin_pll", "fout_bus0_pll" };
PNAME(mout_topc_bus1_pll_ctrl_p)	= { "fin_pll", "fout_bus1_pll" };
PNAME(mout_topc_cc_pll_ctrl_p)	= { "fin_pll", "fout_cc_pll" };
PNAME(mout_topc_mfc_pll_ctrl_p)	= { "fin_pll", "fout_mfc_pll" };

PNAME(mout_topc_group2) = { "mout_topc_bus0_pll_half",
	"mout_topc_bus1_pll_half", "mout_topc_cc_pll_half",
	"mout_topc_mfc_pll_half" };

PNAME(mout_topc_bus0_pll_half_p) = { "mout_topc_bus0_pll",
	"ffac_topc_bus0_pll_div2", "ffac_topc_bus0_pll_div4"};
PNAME(mout_topc_bus1_pll_half_p) = { "mout_topc_bus1_pll",
	"ffac_topc_bus1_pll_div2"};
PNAME(mout_topc_cc_pll_half_p) = { "mout_topc_cc_pll",
	"ffac_topc_cc_pll_div2"};
PNAME(mout_topc_mfc_pll_half_p) = { "mout_topc_mfc_pll",
	"ffac_topc_mfc_pll_div2"};


PNAME(mout_topc_bus0_pll_out_p) = {"mout_topc_bus0_pll",
	"ffac_topc_bus0_pll_div2"};

static const unsigned long topc_clk_regs[] __initconst = {
	CC_PLL_LOCK,
	BUS0_PLL_LOCK,
	BUS1_DPLL_LOCK,
	MFC_PLL_LOCK,
	AUD_PLL_LOCK,
	CC_PLL_CON0,
	BUS0_PLL_CON0,
	BUS1_DPLL_CON0,
	MFC_PLL_CON0,
	AUD_PLL_CON0,
	MUX_SEL_TOPC0,
	MUX_SEL_TOPC1,
	MUX_SEL_TOPC2,
	MUX_SEL_TOPC3,
	DIV_TOPC0,
	DIV_TOPC1,
	DIV_TOPC3,
};

static const struct samsung_mux_clock topc_mux_clks[] __initconst = {
	MUX(0, "mout_topc_bus0_pll", mout_topc_bus0_pll_ctrl_p,
		MUX_SEL_TOPC0, 0, 1),
	MUX(0, "mout_topc_bus1_pll", mout_topc_bus1_pll_ctrl_p,
		MUX_SEL_TOPC0, 4, 1),
	MUX(0, "mout_topc_cc_pll", mout_topc_cc_pll_ctrl_p,
		MUX_SEL_TOPC0, 8, 1),
	MUX(0, "mout_topc_mfc_pll", mout_topc_mfc_pll_ctrl_p,
		MUX_SEL_TOPC0, 12, 1),
	MUX(0, "mout_topc_bus0_pll_half", mout_topc_bus0_pll_half_p,
		MUX_SEL_TOPC0, 16, 2),
	MUX(0, "mout_topc_bus1_pll_half", mout_topc_bus1_pll_half_p,
		MUX_SEL_TOPC0, 20, 1),
	MUX(0, "mout_topc_cc_pll_half", mout_topc_cc_pll_half_p,
		MUX_SEL_TOPC0, 24, 1),
	MUX(0, "mout_topc_mfc_pll_half", mout_topc_mfc_pll_half_p,
		MUX_SEL_TOPC0, 28, 1),

	MUX(0, "mout_topc_aud_pll", mout_topc_aud_pll_ctrl_p,
		MUX_SEL_TOPC1, 0, 1),
	MUX(0, "mout_topc_bus0_pll_out", mout_topc_bus0_pll_out_p,
		MUX_SEL_TOPC1, 16, 1),

	MUX(0, "mout_aclk_ccore_133", mout_topc_group2,	MUX_SEL_TOPC2, 4, 2),

	MUX(0, "mout_aclk_mscl_532", mout_topc_group2, MUX_SEL_TOPC3, 20, 2),
	MUX(0, "mout_aclk_peris_66", mout_topc_group2, MUX_SEL_TOPC3, 24, 2),
};

static const struct samsung_div_clock topc_div_clks[] __initconst = {
	DIV(DOUT_ACLK_CCORE_133, "dout_aclk_ccore_133", "mout_aclk_ccore_133",
		DIV_TOPC0, 4, 4),

	DIV(DOUT_ACLK_MSCL_532, "dout_aclk_mscl_532", "mout_aclk_mscl_532",
		DIV_TOPC1, 20, 4),
	DIV(DOUT_ACLK_PERIS, "dout_aclk_peris_66", "mout_aclk_peris_66",
		DIV_TOPC1, 24, 4),

	DIV(DOUT_SCLK_BUS0_PLL, "dout_sclk_bus0_pll", "mout_topc_bus0_pll_out",
		DIV_TOPC3, 0, 4),
	DIV(DOUT_SCLK_BUS1_PLL, "dout_sclk_bus1_pll", "mout_topc_bus1_pll",
		DIV_TOPC3, 8, 4),
	DIV(DOUT_SCLK_CC_PLL, "dout_sclk_cc_pll", "mout_topc_cc_pll",
		DIV_TOPC3, 12, 4),
	DIV(DOUT_SCLK_MFC_PLL, "dout_sclk_mfc_pll", "mout_topc_mfc_pll",
		DIV_TOPC3, 16, 4),
	DIV(DOUT_SCLK_AUD_PLL, "dout_sclk_aud_pll", "mout_topc_aud_pll",
		DIV_TOPC3, 28, 4),
};

static const struct samsung_pll_rate_table pll1460x_24mhz_tbl[] __initconst = {
	PLL_36XX_RATE(491519897, 20, 1, 0, 31457),
	{},
};

static const struct samsung_gate_clock topc_gate_clks[] __initconst = {
	GATE(ACLK_CCORE_133, "aclk_ccore_133", "dout_aclk_ccore_133",
		ENABLE_ACLK_TOPC0, 4, CLK_IS_CRITICAL, 0),

	GATE(ACLK_MSCL_532, "aclk_mscl_532", "dout_aclk_mscl_532",
		ENABLE_ACLK_TOPC1, 20, 0, 0),

	GATE(ACLK_PERIS_66, "aclk_peris_66", "dout_aclk_peris_66",
		ENABLE_ACLK_TOPC1, 24, 0, 0),

	GATE(SCLK_AUD_PLL, "sclk_aud_pll", "dout_sclk_aud_pll",
		ENABLE_SCLK_TOPC1, 20, 0, 0),
	GATE(SCLK_MFC_PLL_B, "sclk_mfc_pll_b", "dout_sclk_mfc_pll",
		ENABLE_SCLK_TOPC1, 17, 0, 0),
	GATE(SCLK_MFC_PLL_A, "sclk_mfc_pll_a", "dout_sclk_mfc_pll",
		ENABLE_SCLK_TOPC1, 16, 0, 0),
	GATE(SCLK_BUS1_PLL_B, "sclk_bus1_pll_b", "dout_sclk_bus1_pll",
		ENABLE_SCLK_TOPC1, 13, 0, 0),
	GATE(SCLK_BUS1_PLL_A, "sclk_bus1_pll_a", "dout_sclk_bus1_pll",
		ENABLE_SCLK_TOPC1, 12, 0, 0),
	GATE(SCLK_BUS0_PLL_B, "sclk_bus0_pll_b", "dout_sclk_bus0_pll",
		ENABLE_SCLK_TOPC1, 5, 0, 0),
	GATE(SCLK_BUS0_PLL_A, "sclk_bus0_pll_a", "dout_sclk_bus0_pll",
		ENABLE_SCLK_TOPC1, 4, 0, 0),
	GATE(SCLK_CC_PLL_B, "sclk_cc_pll_b", "dout_sclk_cc_pll",
		ENABLE_SCLK_TOPC1, 1, 0, 0),
	GATE(SCLK_CC_PLL_A, "sclk_cc_pll_a", "dout_sclk_cc_pll",
		ENABLE_SCLK_TOPC1, 0, 0, 0),
};

static const struct samsung_pll_clock topc_pll_clks[] __initconst = {
	PLL(pll_1451x, 0, "fout_bus0_pll", "fin_pll", BUS0_PLL_LOCK,
		BUS0_PLL_CON0, NULL),
	PLL(pll_1452x, 0, "fout_cc_pll", "fin_pll", CC_PLL_LOCK,
		CC_PLL_CON0, NULL),
	PLL(pll_1452x, 0, "fout_bus1_pll", "fin_pll", BUS1_DPLL_LOCK,
		BUS1_DPLL_CON0, NULL),
	PLL(pll_1452x, 0, "fout_mfc_pll", "fin_pll", MFC_PLL_LOCK,
		MFC_PLL_CON0, NULL),
	PLL(pll_1460x, FOUT_AUD_PLL, "fout_aud_pll", "fin_pll", AUD_PLL_LOCK,
		AUD_PLL_CON0, pll1460x_24mhz_tbl),
};

static const struct samsung_cmu_info topc_cmu_info __initconst = {
	.pll_clks		= topc_pll_clks,
	.nr_pll_clks		= ARRAY_SIZE(topc_pll_clks),
	.mux_clks		= topc_mux_clks,
	.nr_mux_clks		= ARRAY_SIZE(topc_mux_clks),
	.div_clks		= topc_div_clks,
	.nr_div_clks		= ARRAY_SIZE(topc_div_clks),
	.gate_clks		= topc_gate_clks,
	.nr_gate_clks		= ARRAY_SIZE(topc_gate_clks),
	.fixed_factor_clks	= topc_fixed_factor_clks,
	.nr_fixed_factor_clks	= ARRAY_SIZE(topc_fixed_factor_clks),
	.nr_clk_ids		= TOPC_NR_CLK,
	.clk_regs		= topc_clk_regs,
	.nr_clk_regs		= ARRAY_SIZE(topc_clk_regs),
};

static void __init exynos7_clk_topc_init(struct device_node *np)
{
	samsung_cmu_register_one(np, &topc_cmu_info);
}

CLK_OF_DECLARE(exynos7_clk_topc, "samsung,exynos7-clock-topc",
	exynos7_clk_topc_init);

/* Register Offset definitions for CMU_TOP0 (0x105D0000) */
#define MUX_SEL_TOP00			0x0200
#define MUX_SEL_TOP01			0x0204
#define MUX_SEL_TOP03			0x020C
#define MUX_SEL_TOP0_PERIC0		0x0230
#define MUX_SEL_TOP0_PERIC1		0x0234
#define MUX_SEL_TOP0_PERIC2		0x0238
#define MUX_SEL_TOP0_PERIC3		0x023C
#define DIV_TOP03			0x060C
#define DIV_TOP0_PERIC0			0x0630
#define DIV_TOP0_PERIC1			0x0634
#define DIV_TOP0_PERIC2			0x0638
#define DIV_TOP0_PERIC3			0x063C
#define ENABLE_ACLK_TOP03		0x080C
#define ENABLE_SCLK_TOP0_PERIC0		0x0A30
#define ENABLE_SCLK_TOP0_PERIC1		0x0A34
#define ENABLE_SCLK_TOP0_PERIC2		0x0A38
#define ENABLE_SCLK_TOP0_PERIC3		0x0A3C

/* List of parent clocks for Muxes in CMU_TOP0 */
PNAME(mout_top0_bus0_pll_user_p)	= { "fin_pll", "sclk_bus0_pll_a" };
PNAME(mout_top0_bus1_pll_user_p)	= { "fin_pll", "sclk_bus1_pll_a" };
PNAME(mout_top0_cc_pll_user_p)	= { "fin_pll", "sclk_cc_pll_a" };
PNAME(mout_top0_mfc_pll_user_p)	= { "fin_pll", "sclk_mfc_pll_a" };
PNAME(mout_top0_aud_pll_user_p)	= { "fin_pll", "sclk_aud_pll" };

PNAME(mout_top0_bus0_pll_half_p) = {"mout_top0_bus0_pll_user",
	"ffac_top0_bus0_pll_div2"};
PNAME(mout_top0_bus1_pll_half_p) = {"mout_top0_bus1_pll_user",
	"ffac_top0_bus1_pll_div2"};
PNAME(mout_top0_cc_pll_half_p) = {"mout_top0_cc_pll_user",
	"ffac_top0_cc_pll_div2"};
PNAME(mout_top0_mfc_pll_half_p) = {"mout_top0_mfc_pll_user",
	"ffac_top0_mfc_pll_div2"};

PNAME(mout_top0_group1) = {"mout_top0_bus0_pll_half",
	"mout_top0_bus1_pll_half", "mout_top0_cc_pll_half",
	"mout_top0_mfc_pll_half"};
PNAME(mout_top0_group3) = {"ioclk_audiocdclk0",
	"ioclk_audiocdclk1", "ioclk_spdif_extclk",
	"mout_top0_aud_pll_user", "mout_top0_bus0_pll_half",
	"mout_top0_bus1_pll_half"};
PNAME(mout_top0_group4) = {"ioclk_audiocdclk1", "mout_top0_aud_pll_user",
	"mout_top0_bus0_pll_half", "mout_top0_bus1_pll_half"};

static const unsigned long top0_clk_regs[] __initconst = {
	MUX_SEL_TOP00,
	MUX_SEL_TOP01,
	MUX_SEL_TOP03,
	MUX_SEL_TOP0_PERIC0,
	MUX_SEL_TOP0_PERIC1,
	MUX_SEL_TOP0_PERIC2,
	MUX_SEL_TOP0_PERIC3,
	DIV_TOP03,
	DIV_TOP0_PERIC0,
	DIV_TOP0_PERIC1,
	DIV_TOP0_PERIC2,
	DIV_TOP0_PERIC3,
	ENABLE_SCLK_TOP0_PERIC0,
	ENABLE_SCLK_TOP0_PERIC1,
	ENABLE_SCLK_TOP0_PERIC2,
	ENABLE_SCLK_TOP0_PERIC3,
};

static const struct samsung_mux_clock top0_mux_clks[] __initconst = {
	MUX(0, "mout_top0_aud_pll_user", mout_top0_aud_pll_user_p,
		MUX_SEL_TOP00, 0, 1),
	MUX(0, "mout_top0_mfc_pll_user", mout_top0_mfc_pll_user_p,
		MUX_SEL_TOP00, 4, 1),
	MUX(0, "mout_top0_cc_pll_user", mout_top0_cc_pll_user_p,
		MUX_SEL_TOP00, 8, 1),
	MUX(0, "mout_top0_bus1_pll_user", mout_top0_bus1_pll_user_p,
		MUX_SEL_TOP00, 12, 1),
	MUX(0, "mout_top0_bus0_pll_user", mout_top0_bus0_pll_user_p,
		MUX_SEL_TOP00, 16, 1),

	MUX(0, "mout_top0_mfc_pll_half", mout_top0_mfc_pll_half_p,
		MUX_SEL_TOP01, 4, 1),
	MUX(0, "mout_top0_cc_pll_half", mout_top0_cc_pll_half_p,
		MUX_SEL_TOP01, 8, 1),
	MUX(0, "mout_top0_bus1_pll_half", mout_top0_bus1_pll_half_p,
		MUX_SEL_TOP01, 12, 1),
	MUX(0, "mout_top0_bus0_pll_half", mout_top0_bus0_pll_half_p,
		MUX_SEL_TOP01, 16, 1),

	MUX(0, "mout_aclk_peric1_66", mout_top0_group1, MUX_SEL_TOP03, 12, 2),
	MUX(0, "mout_aclk_peric0_66", mout_top0_group1, MUX_SEL_TOP03, 20, 2),

	MUX(0, "mout_sclk_spdif", mout_top0_group3, MUX_SEL_TOP0_PERIC0, 4, 3),
	MUX(0, "mout_sclk_pcm1", mout_top0_group4, MUX_SEL_TOP0_PERIC0, 8, 2),
	MUX(0, "mout_sclk_i2s1", mout_top0_group4, MUX_SEL_TOP0_PERIC0, 20, 2),

	MUX(0, "mout_sclk_spi1", mout_top0_group1, MUX_SEL_TOP0_PERIC1, 8, 2),
	MUX(0, "mout_sclk_spi0", mout_top0_group1, MUX_SEL_TOP0_PERIC1, 20, 2),

	MUX(0, "mout_sclk_spi3", mout_top0_group1, MUX_SEL_TOP0_PERIC2, 8, 2),
	MUX(0, "mout_sclk_spi2", mout_top0_group1, MUX_SEL_TOP0_PERIC2, 20, 2),
	MUX(0, "mout_sclk_uart3", mout_top0_group1, MUX_SEL_TOP0_PERIC3, 4, 2),
	MUX(0, "mout_sclk_uart2", mout_top0_group1, MUX_SEL_TOP0_PERIC3, 8, 2),
	MUX(0, "mout_sclk_uart1", mout_top0_group1, MUX_SEL_TOP0_PERIC3, 12, 2),
	MUX(0, "mout_sclk_uart0", mout_top0_group1, MUX_SEL_TOP0_PERIC3, 16, 2),
	MUX(0, "mout_sclk_spi4", mout_top0_group1, MUX_SEL_TOP0_PERIC3, 20, 2),
};

static const struct samsung_div_clock top0_div_clks[] __initconst = {
	DIV(DOUT_ACLK_PERIC1, "dout_aclk_peric1_66", "mout_aclk_peric1_66",
		DIV_TOP03, 12, 6),
	DIV(DOUT_ACLK_PERIC0, "dout_aclk_peric0_66", "mout_aclk_peric0_66",
		DIV_TOP03, 20, 6),

	DIV(0, "dout_sclk_spdif", "mout_sclk_spdif", DIV_TOP0_PERIC0, 4, 4),
	DIV(0, "dout_sclk_pcm1", "mout_sclk_pcm1", DIV_TOP0_PERIC0, 8, 12),
	DIV(0, "dout_sclk_i2s1", "mout_sclk_i2s1", DIV_TOP0_PERIC0, 20, 10),

	DIV(0, "dout_sclk_spi1", "mout_sclk_spi1", DIV_TOP0_PERIC1, 8, 12),
	DIV(0, "dout_sclk_spi0", "mout_sclk_spi0", DIV_TOP0_PERIC1, 20, 12),

	DIV(0, "dout_sclk_spi3", "mout_sclk_spi3", DIV_TOP0_PERIC2, 8, 12),
	DIV(0, "dout_sclk_spi2", "mout_sclk_spi2", DIV_TOP0_PERIC2, 20, 12),

	DIV(0, "dout_sclk_uart3", "mout_sclk_uart3", DIV_TOP0_PERIC3, 4, 4),
	DIV(0, "dout_sclk_uart2", "mout_sclk_uart2", DIV_TOP0_PERIC3, 8, 4),
	DIV(0, "dout_sclk_uart1", "mout_sclk_uart1", DIV_TOP0_PERIC3, 12, 4),
	DIV(0, "dout_sclk_uart0", "mout_sclk_uart0", DIV_TOP0_PERIC3, 16, 4),
	DIV(0, "dout_sclk_spi4", "mout_sclk_spi4", DIV_TOP0_PERIC3, 20, 12),
};

static const struct samsung_gate_clock top0_gate_clks[] __initconst = {
	GATE(CLK_ACLK_PERIC0_66, "aclk_peric0_66", "dout_aclk_peric0_66",
		ENABLE_ACLK_TOP03, 20, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_ACLK_PERIC1_66, "aclk_peric1_66", "dout_aclk_peric1_66",
		ENABLE_ACLK_TOP03, 12, CLK_SET_RATE_PARENT, 0),

	GATE(CLK_SCLK_SPDIF, "sclk_spdif", "dout_sclk_spdif",
		ENABLE_SCLK_TOP0_PERIC0, 4, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_SCLK_PCM1, "sclk_pcm1", "dout_sclk_pcm1",
		ENABLE_SCLK_TOP0_PERIC0, 8, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_SCLK_I2S1, "sclk_i2s1", "dout_sclk_i2s1",
		ENABLE_SCLK_TOP0_PERIC0, 20, CLK_SET_RATE_PARENT, 0),

	GATE(CLK_SCLK_SPI1, "sclk_spi1", "dout_sclk_spi1",
		ENABLE_SCLK_TOP0_PERIC1, 8, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_SCLK_SPI0, "sclk_spi0", "dout_sclk_spi0",
		ENABLE_SCLK_TOP0_PERIC1, 20, CLK_SET_RATE_PARENT, 0),

	GATE(CLK_SCLK_SPI3, "sclk_spi3", "dout_sclk_spi3",
		ENABLE_SCLK_TOP0_PERIC2, 8, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_SCLK_SPI2, "sclk_spi2", "dout_sclk_spi2",
		ENABLE_SCLK_TOP0_PERIC2, 20, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_SCLK_UART3, "sclk_uart3", "dout_sclk_uart3",
		ENABLE_SCLK_TOP0_PERIC3, 4, 0, 0),
	GATE(CLK_SCLK_UART2, "sclk_uart2", "dout_sclk_uart2",
		ENABLE_SCLK_TOP0_PERIC3, 8, 0, 0),
	GATE(CLK_SCLK_UART1, "sclk_uart1", "dout_sclk_uart1",
		ENABLE_SCLK_TOP0_PERIC3, 12, 0, 0),
	GATE(CLK_SCLK_UART0, "sclk_uart0", "dout_sclk_uart0",
		ENABLE_SCLK_TOP0_PERIC3, 16, 0, 0),
	GATE(CLK_SCLK_SPI4, "sclk_spi4", "dout_sclk_spi4",
		ENABLE_SCLK_TOP0_PERIC3, 20, CLK_SET_RATE_PARENT, 0),
};

static const struct samsung_fixed_factor_clock top0_fixed_factor_clks[] __initconst = {
	FFACTOR(0, "ffac_top0_bus0_pll_div2", "mout_top0_bus0_pll_user",
		1, 2, 0),
	FFACTOR(0, "ffac_top0_bus1_pll_div2", "mout_top0_bus1_pll_user",
		1, 2, 0),
	FFACTOR(0, "ffac_top0_cc_pll_div2", "mout_top0_cc_pll_user", 1, 2, 0),
	FFACTOR(0, "ffac_top0_mfc_pll_div2", "mout_top0_mfc_pll_user", 1, 2, 0),
};

static const struct samsung_cmu_info top0_cmu_info __initconst = {
	.mux_clks		= top0_mux_clks,
	.nr_mux_clks		= ARRAY_SIZE(top0_mux_clks),
	.div_clks		= top0_div_clks,
	.nr_div_clks		= ARRAY_SIZE(top0_div_clks),
	.gate_clks		= top0_gate_clks,
	.nr_gate_clks		= ARRAY_SIZE(top0_gate_clks),
	.fixed_factor_clks	= top0_fixed_factor_clks,
	.nr_fixed_factor_clks	= ARRAY_SIZE(top0_fixed_factor_clks),
	.nr_clk_ids		= TOP0_NR_CLK,
	.clk_regs		= top0_clk_regs,
	.nr_clk_regs		= ARRAY_SIZE(top0_clk_regs),
};

static void __init exynos7_clk_top0_init(struct device_node *np)
{
	samsung_cmu_register_one(np, &top0_cmu_info);
}

CLK_OF_DECLARE(exynos7_clk_top0, "samsung,exynos7-clock-top0",
	exynos7_clk_top0_init);

/* Register Offset definitions for CMU_TOP1 (0x105E0000) */
#define MUX_SEL_TOP10			0x0200
#define MUX_SEL_TOP11			0x0204
#define MUX_SEL_TOP13			0x020C
#define MUX_SEL_TOP1_FSYS0		0x0224
#define MUX_SEL_TOP1_FSYS1		0x0228
#define MUX_SEL_TOP1_FSYS11		0x022C
#define DIV_TOP13			0x060C
#define DIV_TOP1_FSYS0			0x0624
#define DIV_TOP1_FSYS1			0x0628
#define DIV_TOP1_FSYS11			0x062C
#define ENABLE_ACLK_TOP13		0x080C
#define ENABLE_SCLK_TOP1_FSYS0		0x0A24
#define ENABLE_SCLK_TOP1_FSYS1		0x0A28
#define ENABLE_SCLK_TOP1_FSYS11		0x0A2C

/* List of parent clocks for Muxes in CMU_TOP1 */
PNAME(mout_top1_bus0_pll_user_p)	= { "fin_pll", "sclk_bus0_pll_b" };
PNAME(mout_top1_bus1_pll_user_p)	= { "fin_pll", "sclk_bus1_pll_b" };
PNAME(mout_top1_cc_pll_user_p)	= { "fin_pll", "sclk_cc_pll_b" };
PNAME(mout_top1_mfc_pll_user_p)	= { "fin_pll", "sclk_mfc_pll_b" };

PNAME(mout_top1_bus0_pll_half_p) = {"mout_top1_bus0_pll_user",
	"ffac_top1_bus0_pll_div2"};
PNAME(mout_top1_bus1_pll_half_p) = {"mout_top1_bus1_pll_user",
	"ffac_top1_bus1_pll_div2"};
PNAME(mout_top1_cc_pll_half_p) = {"mout_top1_cc_pll_user",
	"ffac_top1_cc_pll_div2"};
PNAME(mout_top1_mfc_pll_half_p) = {"mout_top1_mfc_pll_user",
	"ffac_top1_mfc_pll_div2"};

PNAME(mout_top1_group1) = {"mout_top1_bus0_pll_half",
	"mout_top1_bus1_pll_half", "mout_top1_cc_pll_half",
	"mout_top1_mfc_pll_half"};

static const unsigned long top1_clk_regs[] __initconst = {
	MUX_SEL_TOP10,
	MUX_SEL_TOP11,
	MUX_SEL_TOP13,
	MUX_SEL_TOP1_FSYS0,
	MUX_SEL_TOP1_FSYS1,
	MUX_SEL_TOP1_FSYS11,
	DIV_TOP13,
	DIV_TOP1_FSYS0,
	DIV_TOP1_FSYS1,
	DIV_TOP1_FSYS11,
	ENABLE_ACLK_TOP13,
	ENABLE_SCLK_TOP1_FSYS0,
	ENABLE_SCLK_TOP1_FSYS1,
	ENABLE_SCLK_TOP1_FSYS11,
};

static const struct samsung_mux_clock top1_mux_clks[] __initconst = {
	MUX(0, "mout_top1_mfc_pll_user", mout_top1_mfc_pll_user_p,
		MUX_SEL_TOP10, 4, 1),
	MUX(0, "mout_top1_cc_pll_user", mout_top1_cc_pll_user_p,
		MUX_SEL_TOP10, 8, 1),
	MUX(0, "mout_top1_bus1_pll_user", mout_top1_bus1_pll_user_p,
		MUX_SEL_TOP10, 12, 1),
	MUX(0, "mout_top1_bus0_pll_user", mout_top1_bus0_pll_user_p,
		MUX_SEL_TOP10, 16, 1),

	MUX(0, "mout_top1_mfc_pll_half", mout_top1_mfc_pll_half_p,
		MUX_SEL_TOP11, 4, 1),
	MUX(0, "mout_top1_cc_pll_half", mout_top1_cc_pll_half_p,
		MUX_SEL_TOP11, 8, 1),
	MUX(0, "mout_top1_bus1_pll_half", mout_top1_bus1_pll_half_p,
		MUX_SEL_TOP11, 12, 1),
	MUX(0, "mout_top1_bus0_pll_half", mout_top1_bus0_pll_half_p,
		MUX_SEL_TOP11, 16, 1),

	MUX(0, "mout_aclk_fsys1_200", mout_top1_group1, MUX_SEL_TOP13, 24, 2),
	MUX(0, "mout_aclk_fsys0_200", mout_top1_group1, MUX_SEL_TOP13, 28, 2),

	MUX(0, "mout_sclk_phy_fsys0_26m", mout_top1_group1,
		MUX_SEL_TOP1_FSYS0, 0, 2),
	MUX(0, "mout_sclk_mmc2", mout_top1_group1, MUX_SEL_TOP1_FSYS0, 16, 2),
	MUX(0, "mout_sclk_usbdrd300", mout_top1_group1,
		MUX_SEL_TOP1_FSYS0, 28, 2),

	MUX(0, "mout_sclk_phy_fsys1", mout_top1_group1,
		MUX_SEL_TOP1_FSYS1, 0, 2),
	MUX(0, "mout_sclk_ufsunipro20", mout_top1_group1,
		MUX_SEL_TOP1_FSYS1, 16, 2),

	MUX(0, "mout_sclk_mmc1", mout_top1_group1, MUX_SEL_TOP1_FSYS11, 0, 2),
	MUX(0, "mout_sclk_mmc0", mout_top1_group1, MUX_SEL_TOP1_FSYS11, 12, 2),
	MUX(0, "mout_sclk_phy_fsys1_26m", mout_top1_group1,
		MUX_SEL_TOP1_FSYS11, 24, 2),
};

static const struct samsung_div_clock top1_div_clks[] __initconst = {
	DIV(DOUT_ACLK_FSYS1_200, "dout_aclk_fsys1_200", "mout_aclk_fsys1_200",
		DIV_TOP13, 24, 4),
	DIV(DOUT_ACLK_FSYS0_200, "dout_aclk_fsys0_200", "mout_aclk_fsys0_200",
		DIV_TOP13, 28, 4),

	DIV(DOUT_SCLK_PHY_FSYS1, "dout_sclk_phy_fsys1",
		"mout_sclk_phy_fsys1", DIV_TOP1_FSYS1, 0, 6),

	DIV(DOUT_SCLK_UFSUNIPRO20, "dout_sclk_ufsunipro20",
		"mout_sclk_ufsunipro20",
		DIV_TOP1_FSYS1, 16, 6),

	DIV(DOUT_SCLK_MMC2, "dout_sclk_mmc2", "mout_sclk_mmc2",
		DIV_TOP1_FSYS0, 16, 10),
	DIV(0, "dout_sclk_usbdrd300", "mout_sclk_usbdrd300",
		DIV_TOP1_FSYS0, 28, 4),

	DIV(DOUT_SCLK_MMC1, "dout_sclk_mmc1", "mout_sclk_mmc1",
		DIV_TOP1_FSYS11, 0, 10),
	DIV(DOUT_SCLK_MMC0, "dout_sclk_mmc0", "mout_sclk_mmc0",
		DIV_TOP1_FSYS11, 12, 10),

	DIV(DOUT_SCLK_PHY_FSYS1_26M, "dout_sclk_phy_fsys1_26m",
		"mout_sclk_phy_fsys1_26m", DIV_TOP1_FSYS11, 24, 6),
};

static const struct samsung_gate_clock top1_gate_clks[] __initconst = {
	GATE(CLK_SCLK_MMC2, "sclk_mmc2", "dout_sclk_mmc2",
		ENABLE_SCLK_TOP1_FSYS0, 16, CLK_SET_RATE_PARENT, 0),
	GATE(0, "sclk_usbdrd300", "dout_sclk_usbdrd300",
		ENABLE_SCLK_TOP1_FSYS0, 28, 0, 0),

	GATE(CLK_SCLK_PHY_FSYS1, "sclk_phy_fsys1", "dout_sclk_phy_fsys1",
		ENABLE_SCLK_TOP1_FSYS1, 0, CLK_SET_RATE_PARENT, 0),

	GATE(CLK_SCLK_UFSUNIPRO20, "sclk_ufsunipro20", "dout_sclk_ufsunipro20",
		ENABLE_SCLK_TOP1_FSYS1, 16, CLK_SET_RATE_PARENT, 0),

	GATE(CLK_SCLK_MMC1, "sclk_mmc1", "dout_sclk_mmc1",
		ENABLE_SCLK_TOP1_FSYS11, 0, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_SCLK_MMC0, "sclk_mmc0", "dout_sclk_mmc0",
		ENABLE_SCLK_TOP1_FSYS11, 12, CLK_SET_RATE_PARENT, 0),

	GATE(CLK_ACLK_FSYS0_200, "aclk_fsys0_200", "dout_aclk_fsys0_200",
		ENABLE_ACLK_TOP13, 28, CLK_SET_RATE_PARENT |
		CLK_IS_CRITICAL, 0),
	GATE(CLK_ACLK_FSYS1_200, "aclk_fsys1_200", "dout_aclk_fsys1_200",
		ENABLE_ACLK_TOP13, 24, CLK_SET_RATE_PARENT, 0),

	GATE(CLK_SCLK_PHY_FSYS1_26M, "sclk_phy_fsys1_26m",
		"dout_sclk_phy_fsys1_26m", ENABLE_SCLK_TOP1_FSYS11,
		24, CLK_SET_RATE_PARENT, 0),
};

static const struct samsung_fixed_factor_clock top1_fixed_factor_clks[] __initconst = {
	FFACTOR(0, "ffac_top1_bus0_pll_div2", "mout_top1_bus0_pll_user",
		1, 2, 0),
	FFACTOR(0, "ffac_top1_bus1_pll_div2", "mout_top1_bus1_pll_user",
		1, 2, 0),
	FFACTOR(0, "ffac_top1_cc_pll_div2", "mout_top1_cc_pll_user", 1, 2, 0),
	FFACTOR(0, "ffac_top1_mfc_pll_div2", "mout_top1_mfc_pll_user", 1, 2, 0),
};

static const struct samsung_cmu_info top1_cmu_info __initconst = {
	.mux_clks		= top1_mux_clks,
	.nr_mux_clks		= ARRAY_SIZE(top1_mux_clks),
	.div_clks		= top1_div_clks,
	.nr_div_clks		= ARRAY_SIZE(top1_div_clks),
	.gate_clks		= top1_gate_clks,
	.nr_gate_clks		= ARRAY_SIZE(top1_gate_clks),
	.fixed_factor_clks	= top1_fixed_factor_clks,
	.nr_fixed_factor_clks	= ARRAY_SIZE(top1_fixed_factor_clks),
	.nr_clk_ids		= TOP1_NR_CLK,
	.clk_regs		= top1_clk_regs,
	.nr_clk_regs		= ARRAY_SIZE(top1_clk_regs),
};

static void __init exynos7_clk_top1_init(struct device_node *np)
{
	samsung_cmu_register_one(np, &top1_cmu_info);
}

CLK_OF_DECLARE(exynos7_clk_top1, "samsung,exynos7-clock-top1",
	exynos7_clk_top1_init);

/* Register Offset definitions for CMU_CCORE (0x105B0000) */
#define MUX_SEL_CCORE			0x0200
#define DIV_CCORE			0x0600
#define ENABLE_ACLK_CCORE0		0x0800
#define ENABLE_ACLK_CCORE1		0x0804
#define ENABLE_PCLK_CCORE		0x0900

/*
 * List of parent clocks for Muxes in CMU_CCORE
 */
PNAME(mout_aclk_ccore_133_user_p)	= { "fin_pll", "aclk_ccore_133" };

static const unsigned long ccore_clk_regs[] __initconst = {
	MUX_SEL_CCORE,
	ENABLE_PCLK_CCORE,
};

static const struct samsung_mux_clock ccore_mux_clks[] __initconst = {
	MUX(0, "mout_aclk_ccore_133_user", mout_aclk_ccore_133_user_p,
		MUX_SEL_CCORE, 1, 1),
};

static const struct samsung_gate_clock ccore_gate_clks[] __initconst = {
	GATE(PCLK_RTC, "pclk_rtc", "mout_aclk_ccore_133_user",
		ENABLE_PCLK_CCORE, 8, 0, 0),
};

static const struct samsung_cmu_info ccore_cmu_info __initconst = {
	.mux_clks		= ccore_mux_clks,
	.nr_mux_clks		= ARRAY_SIZE(ccore_mux_clks),
	.gate_clks		= ccore_gate_clks,
	.nr_gate_clks		= ARRAY_SIZE(ccore_gate_clks),
	.nr_clk_ids		= CCORE_NR_CLK,
	.clk_regs		= ccore_clk_regs,
	.nr_clk_regs		= ARRAY_SIZE(ccore_clk_regs),
};

static void __init exynos7_clk_ccore_init(struct device_node *np)
{
	samsung_cmu_register_one(np, &ccore_cmu_info);
}

CLK_OF_DECLARE(exynos7_clk_ccore, "samsung,exynos7-clock-ccore",
	exynos7_clk_ccore_init);

/* Register Offset definitions for CMU_PERIC0 (0x13610000) */
#define MUX_SEL_PERIC0			0x0200
#define ENABLE_PCLK_PERIC0		0x0900
#define ENABLE_SCLK_PERIC0		0x0A00

/* List of parent clocks for Muxes in CMU_PERIC0 */
PNAME(mout_aclk_peric0_66_user_p)	= { "fin_pll", "aclk_peric0_66" };
PNAME(mout_sclk_uart0_user_p)	= { "fin_pll", "sclk_uart0" };

static const unsigned long peric0_clk_regs[] __initconst = {
	MUX_SEL_PERIC0,
	ENABLE_PCLK_PERIC0,
	ENABLE_SCLK_PERIC0,
};

static const struct samsung_mux_clock peric0_mux_clks[] __initconst = {
	MUX(0, "mout_aclk_peric0_66_user", mout_aclk_peric0_66_user_p,
		MUX_SEL_PERIC0, 0, 1),
	MUX(0, "mout_sclk_uart0_user", mout_sclk_uart0_user_p,
		MUX_SEL_PERIC0, 16, 1),
};

static const struct samsung_gate_clock peric0_gate_clks[] __initconst = {
	GATE(PCLK_HSI2C0, "pclk_hsi2c0", "mout_aclk_peric0_66_user",
		ENABLE_PCLK_PERIC0, 8, 0, 0),
	GATE(PCLK_HSI2C1, "pclk_hsi2c1", "mout_aclk_peric0_66_user",
		ENABLE_PCLK_PERIC0, 9, 0, 0),
	GATE(PCLK_HSI2C4, "pclk_hsi2c4", "mout_aclk_peric0_66_user",
		ENABLE_PCLK_PERIC0, 10, 0, 0),
	GATE(PCLK_HSI2C5, "pclk_hsi2c5", "mout_aclk_peric0_66_user",
		ENABLE_PCLK_PERIC0, 11, 0, 0),
	GATE(PCLK_HSI2C9, "pclk_hsi2c9", "mout_aclk_peric0_66_user",
		ENABLE_PCLK_PERIC0, 12, 0, 0),
	GATE(PCLK_HSI2C10, "pclk_hsi2c10", "mout_aclk_peric0_66_user",
		ENABLE_PCLK_PERIC0, 13, 0, 0),
	GATE(PCLK_HSI2C11, "pclk_hsi2c11", "mout_aclk_peric0_66_user",
		ENABLE_PCLK_PERIC0, 14, 0, 0),
	GATE(PCLK_UART0, "pclk_uart0", "mout_aclk_peric0_66_user",
		ENABLE_PCLK_PERIC0, 16, 0, 0),
	GATE(PCLK_ADCIF, "pclk_adcif", "mout_aclk_peric0_66_user",
		ENABLE_PCLK_PERIC0, 20, 0, 0),
	GATE(PCLK_PWM, "pclk_pwm", "mout_aclk_peric0_66_user",
		ENABLE_PCLK_PERIC0, 21, 0, 0),

	GATE(SCLK_UART0, "sclk_uart0_user", "mout_sclk_uart0_user",
		ENABLE_SCLK_PERIC0, 16, 0, 0),
	GATE(SCLK_PWM, "sclk_pwm", "fin_pll", ENABLE_SCLK_PERIC0, 21, 0, 0),
};

static const struct samsung_cmu_info peric0_cmu_info __initconst = {
	.mux_clks		= peric0_mux_clks,
	.nr_mux_clks		= ARRAY_SIZE(peric0_mux_clks),
	.gate_clks		= peric0_gate_clks,
	.nr_gate_clks		= ARRAY_SIZE(peric0_gate_clks),
	.nr_clk_ids		= PERIC0_NR_CLK,
	.clk_regs		= peric0_clk_regs,
	.nr_clk_regs		= ARRAY_SIZE(peric0_clk_regs),
};

static void __init exynos7_clk_peric0_init(struct device_node *np)
{
	samsung_cmu_register_one(np, &peric0_cmu_info);
}

/* Register Offset definitions for CMU_PERIC1 (0x14C80000) */
#define MUX_SEL_PERIC10			0x0200
#define MUX_SEL_PERIC11			0x0204
#define MUX_SEL_PERIC12			0x0208
#define ENABLE_PCLK_PERIC1		0x0900
#define ENABLE_SCLK_PERIC10		0x0A00

CLK_OF_DECLARE(exynos7_clk_peric0, "samsung,exynos7-clock-peric0",
	exynos7_clk_peric0_init);

/* List of parent clocks for Muxes in CMU_PERIC1 */
PNAME(mout_aclk_peric1_66_user_p)	= { "fin_pll", "aclk_peric1_66" };
PNAME(mout_sclk_uart1_user_p)	= { "fin_pll", "sclk_uart1" };
PNAME(mout_sclk_uart2_user_p)	= { "fin_pll", "sclk_uart2" };
PNAME(mout_sclk_uart3_user_p)	= { "fin_pll", "sclk_uart3" };
PNAME(mout_sclk_spi0_user_p)		= { "fin_pll", "sclk_spi0" };
PNAME(mout_sclk_spi1_user_p)		= { "fin_pll", "sclk_spi1" };
PNAME(mout_sclk_spi2_user_p)		= { "fin_pll", "sclk_spi2" };
PNAME(mout_sclk_spi3_user_p)		= { "fin_pll", "sclk_spi3" };
PNAME(mout_sclk_spi4_user_p)		= { "fin_pll", "sclk_spi4" };

static const unsigned long peric1_clk_regs[] __initconst = {
	MUX_SEL_PERIC10,
	MUX_SEL_PERIC11,
	MUX_SEL_PERIC12,
	ENABLE_PCLK_PERIC1,
	ENABLE_SCLK_PERIC10,
};

static const struct samsung_mux_clock peric1_mux_clks[] __initconst = {
	MUX(0, "mout_aclk_peric1_66_user", mout_aclk_peric1_66_user_p,
		MUX_SEL_PERIC10, 0, 1),

	MUX_F(0, "mout_sclk_spi0_user", mout_sclk_spi0_user_p,
		MUX_SEL_PERIC11, 0, 1, CLK_SET_RATE_PARENT, 0),
	MUX_F(0, "mout_sclk_spi1_user", mout_sclk_spi1_user_p,
		MUX_SEL_PERIC11, 4, 1, CLK_SET_RATE_PARENT, 0),
	MUX_F(0, "mout_sclk_spi2_user", mout_sclk_spi2_user_p,
		MUX_SEL_PERIC11, 8, 1, CLK_SET_RATE_PARENT, 0),
	MUX_F(0, "mout_sclk_spi3_user", mout_sclk_spi3_user_p,
		MUX_SEL_PERIC11, 12, 1, CLK_SET_RATE_PARENT, 0),
	MUX_F(0, "mout_sclk_spi4_user", mout_sclk_spi4_user_p,
		MUX_SEL_PERIC11, 16, 1, CLK_SET_RATE_PARENT, 0),
	MUX(0, "mout_sclk_uart1_user", mout_sclk_uart1_user_p,
		MUX_SEL_PERIC11, 20, 1),
	MUX(0, "mout_sclk_uart2_user", mout_sclk_uart2_user_p,
		MUX_SEL_PERIC11, 24, 1),
	MUX(0, "mout_sclk_uart3_user", mout_sclk_uart3_user_p,
		MUX_SEL_PERIC11, 28, 1),
};

static const struct samsung_gate_clock peric1_gate_clks[] __initconst = {
	GATE(PCLK_HSI2C2, "pclk_hsi2c2", "mout_aclk_peric1_66_user",
		ENABLE_PCLK_PERIC1, 4, 0, 0),
	GATE(PCLK_HSI2C3, "pclk_hsi2c3", "mout_aclk_peric1_66_user",
		ENABLE_PCLK_PERIC1, 5, 0, 0),
	GATE(PCLK_HSI2C6, "pclk_hsi2c6", "mout_aclk_peric1_66_user",
		ENABLE_PCLK_PERIC1, 6, 0, 0),
	GATE(PCLK_HSI2C7, "pclk_hsi2c7", "mout_aclk_peric1_66_user",
		ENABLE_PCLK_PERIC1, 7, 0, 0),
	GATE(PCLK_HSI2C8, "pclk_hsi2c8", "mout_aclk_peric1_66_user",
		ENABLE_PCLK_PERIC1, 8, 0, 0),
	GATE(PCLK_UART1, "pclk_uart1", "mout_aclk_peric1_66_user",
		ENABLE_PCLK_PERIC1, 9, 0, 0),
	GATE(PCLK_UART2, "pclk_uart2", "mout_aclk_peric1_66_user",
		ENABLE_PCLK_PERIC1, 10, 0, 0),
	GATE(PCLK_UART3, "pclk_uart3", "mout_aclk_peric1_66_user",
		ENABLE_PCLK_PERIC1, 11, 0, 0),
	GATE(PCLK_SPI0, "pclk_spi0", "mout_aclk_peric1_66_user",
		ENABLE_PCLK_PERIC1, 12, 0, 0),
	GATE(PCLK_SPI1, "pclk_spi1", "mout_aclk_peric1_66_user",
		ENABLE_PCLK_PERIC1, 13, 0, 0),
	GATE(PCLK_SPI2, "pclk_spi2", "mout_aclk_peric1_66_user",
		ENABLE_PCLK_PERIC1, 14, 0, 0),
	GATE(PCLK_SPI3, "pclk_spi3", "mout_aclk_peric1_66_user",
		ENABLE_PCLK_PERIC1, 15, 0, 0),
	GATE(PCLK_SPI4, "pclk_spi4", "mout_aclk_peric1_66_user",
		ENABLE_PCLK_PERIC1, 16, 0, 0),
	GATE(PCLK_I2S1, "pclk_i2s1", "mout_aclk_peric1_66_user",
		ENABLE_PCLK_PERIC1, 17, CLK_SET_RATE_PARENT, 0),
	GATE(PCLK_PCM1, "pclk_pcm1", "mout_aclk_peric1_66_user",
		ENABLE_PCLK_PERIC1, 18, 0, 0),
	GATE(PCLK_SPDIF, "pclk_spdif", "mout_aclk_peric1_66_user",
		ENABLE_PCLK_PERIC1, 19, 0, 0),

	GATE(SCLK_UART1, "sclk_uart1_user", "mout_sclk_uart1_user",
		ENABLE_SCLK_PERIC10, 9, 0, 0),
	GATE(SCLK_UART2, "sclk_uart2_user", "mout_sclk_uart2_user",
		ENABLE_SCLK_PERIC10, 10, 0, 0),
	GATE(SCLK_UART3, "sclk_uart3_user", "mout_sclk_uart3_user",
		ENABLE_SCLK_PERIC10, 11, 0, 0),
	GATE(SCLK_SPI0, "sclk_spi0_user", "mout_sclk_spi0_user",
		ENABLE_SCLK_PERIC10, 12, CLK_SET_RATE_PARENT, 0),
	GATE(SCLK_SPI1, "sclk_spi1_user", "mout_sclk_spi1_user",
		ENABLE_SCLK_PERIC10, 13, CLK_SET_RATE_PARENT, 0),
	GATE(SCLK_SPI2, "sclk_spi2_user", "mout_sclk_spi2_user",
		ENABLE_SCLK_PERIC10, 14, CLK_SET_RATE_PARENT, 0),
	GATE(SCLK_SPI3, "sclk_spi3_user", "mout_sclk_spi3_user",
		ENABLE_SCLK_PERIC10, 15, CLK_SET_RATE_PARENT, 0),
	GATE(SCLK_SPI4, "sclk_spi4_user", "mout_sclk_spi4_user",
		ENABLE_SCLK_PERIC10, 16, CLK_SET_RATE_PARENT, 0),
	GATE(SCLK_I2S1, "sclk_i2s1_user", "sclk_i2s1",
		ENABLE_SCLK_PERIC10, 17, CLK_SET_RATE_PARENT, 0),
	GATE(SCLK_PCM1, "sclk_pcm1_user", "sclk_pcm1",
		ENABLE_SCLK_PERIC10, 18, CLK_SET_RATE_PARENT, 0),
	GATE(SCLK_SPDIF, "sclk_spdif_user", "sclk_spdif",
		ENABLE_SCLK_PERIC10, 19, CLK_SET_RATE_PARENT, 0),
};

static const struct samsung_cmu_info peric1_cmu_info __initconst = {
	.mux_clks		= peric1_mux_clks,
	.nr_mux_clks		= ARRAY_SIZE(peric1_mux_clks),
	.gate_clks		= peric1_gate_clks,
	.nr_gate_clks		= ARRAY_SIZE(peric1_gate_clks),
	.nr_clk_ids		= PERIC1_NR_CLK,
	.clk_regs		= peric1_clk_regs,
	.nr_clk_regs		= ARRAY_SIZE(peric1_clk_regs),
};

static void __init exynos7_clk_peric1_init(struct device_node *np)
{
	samsung_cmu_register_one(np, &peric1_cmu_info);
}

CLK_OF_DECLARE(exynos7_clk_peric1, "samsung,exynos7-clock-peric1",
	exynos7_clk_peric1_init);

/* Register Offset definitions for CMU_PERIS (0x10040000) */
#define MUX_SEL_PERIS			0x0200
#define ENABLE_PCLK_PERIS		0x0900
#define ENABLE_PCLK_PERIS_SECURE_CHIPID	0x0910
#define ENABLE_SCLK_PERIS		0x0A00
#define ENABLE_SCLK_PERIS_SECURE_CHIPID	0x0A10

/* List of parent clocks for Muxes in CMU_PERIS */
PNAME(mout_aclk_peris_66_user_p) = { "fin_pll", "aclk_peris_66" };

static const unsigned long peris_clk_regs[] __initconst = {
	MUX_SEL_PERIS,
	ENABLE_PCLK_PERIS,
	ENABLE_PCLK_PERIS_SECURE_CHIPID,
	ENABLE_SCLK_PERIS,
	ENABLE_SCLK_PERIS_SECURE_CHIPID,
};

static const struct samsung_mux_clock peris_mux_clks[] __initconst = {
	MUX(0, "mout_aclk_peris_66_user",
		mout_aclk_peris_66_user_p, MUX_SEL_PERIS, 0, 1),
};

static const struct samsung_gate_clock peris_gate_clks[] __initconst = {
	GATE(PCLK_WDT, "pclk_wdt", "mout_aclk_peris_66_user",
		ENABLE_PCLK_PERIS, 6, 0, 0),
	GATE(PCLK_TMU, "pclk_tmu_apbif", "mout_aclk_peris_66_user",
		ENABLE_PCLK_PERIS, 10, 0, 0),

	GATE(PCLK_CHIPID, "pclk_chipid", "mout_aclk_peris_66_user",
		ENABLE_PCLK_PERIS_SECURE_CHIPID, 0, 0, 0),
	GATE(SCLK_CHIPID, "sclk_chipid", "fin_pll",
		ENABLE_SCLK_PERIS_SECURE_CHIPID, 0, 0, 0),

	GATE(SCLK_TMU, "sclk_tmu", "fin_pll", ENABLE_SCLK_PERIS, 10, 0, 0),
};

static const struct samsung_cmu_info peris_cmu_info __initconst = {
	.mux_clks		= peris_mux_clks,
	.nr_mux_clks		= ARRAY_SIZE(peris_mux_clks),
	.gate_clks		= peris_gate_clks,
	.nr_gate_clks		= ARRAY_SIZE(peris_gate_clks),
	.nr_clk_ids		= PERIS_NR_CLK,
	.clk_regs		= peris_clk_regs,
	.nr_clk_regs		= ARRAY_SIZE(peris_clk_regs),
};

static void __init exynos7_clk_peris_init(struct device_node *np)
{
	samsung_cmu_register_one(np, &peris_cmu_info);
}

CLK_OF_DECLARE(exynos7_clk_peris, "samsung,exynos7-clock-peris",
	exynos7_clk_peris_init);

/* Register Offset definitions for CMU_FSYS0 (0x10E90000) */
#define MUX_SEL_FSYS00			0x0200
#define MUX_SEL_FSYS01			0x0204
#define MUX_SEL_FSYS02			0x0208
#define ENABLE_ACLK_FSYS00		0x0800
#define ENABLE_ACLK_FSYS01		0x0804
#define ENABLE_SCLK_FSYS01		0x0A04
#define ENABLE_SCLK_FSYS02		0x0A08
#define ENABLE_SCLK_FSYS04		0x0A10

/*
 * List of parent clocks for Muxes in CMU_FSYS0
 */
PNAME(mout_aclk_fsys0_200_user_p)	= { "fin_pll", "aclk_fsys0_200" };
PNAME(mout_sclk_mmc2_user_p)		= { "fin_pll", "sclk_mmc2" };

PNAME(mout_sclk_usbdrd300_user_p)	= { "fin_pll", "sclk_usbdrd300" };
PNAME(mout_phyclk_usbdrd300_udrd30_phyclk_user_p)	= { "fin_pll",
				"phyclk_usbdrd300_udrd30_phyclock" };
PNAME(mout_phyclk_usbdrd300_udrd30_pipe_pclk_user_p)	= { "fin_pll",
				"phyclk_usbdrd300_udrd30_pipe_pclk" };

/* fixed rate clocks used in the FSYS0 block */
static const struct samsung_fixed_rate_clock fixed_rate_clks_fsys0[] __initconst = {
	FRATE(0, "phyclk_usbdrd300_udrd30_phyclock", NULL, 0, 60000000),
	FRATE(0, "phyclk_usbdrd300_udrd30_pipe_pclk", NULL, 0, 125000000),
};

static const unsigned long fsys0_clk_regs[] __initconst = {
	MUX_SEL_FSYS00,
	MUX_SEL_FSYS01,
	MUX_SEL_FSYS02,
	ENABLE_ACLK_FSYS00,
	ENABLE_ACLK_FSYS01,
	ENABLE_SCLK_FSYS01,
	ENABLE_SCLK_FSYS02,
	ENABLE_SCLK_FSYS04,
};

static const struct samsung_mux_clock fsys0_mux_clks[] __initconst = {
	MUX(0, "mout_aclk_fsys0_200_user", mout_aclk_fsys0_200_user_p,
		MUX_SEL_FSYS00, 24, 1),

	MUX(0, "mout_sclk_mmc2_user", mout_sclk_mmc2_user_p,
		MUX_SEL_FSYS01, 24, 1),
	MUX(0, "mout_sclk_usbdrd300_user", mout_sclk_usbdrd300_user_p,
		MUX_SEL_FSYS01, 28, 1),

	MUX(0, "mout_phyclk_usbdrd300_udrd30_pipe_pclk_user",
		mout_phyclk_usbdrd300_udrd30_pipe_pclk_user_p,
		MUX_SEL_FSYS02, 24, 1),
	MUX(0, "mout_phyclk_usbdrd300_udrd30_phyclk_user",
		mout_phyclk_usbdrd300_udrd30_phyclk_user_p,
		MUX_SEL_FSYS02, 28, 1),
};

static const struct samsung_gate_clock fsys0_gate_clks[] __initconst = {
	GATE(ACLK_PDMA1, "aclk_pdma1", "mout_aclk_fsys0_200_user",
			ENABLE_ACLK_FSYS00, 3, 0, 0),
	GATE(ACLK_PDMA0, "aclk_pdma0", "mout_aclk_fsys0_200_user",
			ENABLE_ACLK_FSYS00, 4, 0, 0),
	GATE(ACLK_AXIUS_USBDRD30X_FSYS0X, "aclk_axius_usbdrd30x_fsys0x",
		"mout_aclk_fsys0_200_user",
		ENABLE_ACLK_FSYS00, 19, 0, 0),

	GATE(ACLK_USBDRD300, "aclk_usbdrd300", "mout_aclk_fsys0_200_user",
		ENABLE_ACLK_FSYS01, 29, 0, 0),
	GATE(ACLK_MMC2, "aclk_mmc2", "mout_aclk_fsys0_200_user",
		ENABLE_ACLK_FSYS01, 31, 0, 0),

	GATE(SCLK_USBDRD300_SUSPENDCLK, "sclk_usbdrd300_suspendclk",
		"mout_sclk_usbdrd300_user",
		ENABLE_SCLK_FSYS01, 4, 0, 0),
	GATE(SCLK_USBDRD300_REFCLK, "sclk_usbdrd300_refclk", "fin_pll",
		ENABLE_SCLK_FSYS01, 8, 0, 0),

	GATE(PHYCLK_USBDRD300_UDRD30_PIPE_PCLK_USER,
		"phyclk_usbdrd300_udrd30_pipe_pclk_user",
		"mout_phyclk_usbdrd300_udrd30_pipe_pclk_user",
		ENABLE_SCLK_FSYS02, 24, 0, 0),
	GATE(PHYCLK_USBDRD300_UDRD30_PHYCLK_USER,
		"phyclk_usbdrd300_udrd30_phyclk_user",
		"mout_phyclk_usbdrd300_udrd30_phyclk_user",
		ENABLE_SCLK_FSYS02, 28, 0, 0),

	GATE(OSCCLK_PHY_CLKOUT_USB30_PHY, "oscclk_phy_clkout_usb30_phy",
		"fin_pll",
		ENABLE_SCLK_FSYS04, 28, 0, 0),
};

static const struct samsung_cmu_info fsys0_cmu_info __initconst = {
	.fixed_clks		= fixed_rate_clks_fsys0,
	.nr_fixed_clks		= ARRAY_SIZE(fixed_rate_clks_fsys0),
	.mux_clks		= fsys0_mux_clks,
	.nr_mux_clks		= ARRAY_SIZE(fsys0_mux_clks),
	.gate_clks		= fsys0_gate_clks,
	.nr_gate_clks		= ARRAY_SIZE(fsys0_gate_clks),
	.nr_clk_ids		= FSYS0_NR_CLK,
	.clk_regs		= fsys0_clk_regs,
	.nr_clk_regs		= ARRAY_SIZE(fsys0_clk_regs),
};

static void __init exynos7_clk_fsys0_init(struct device_node *np)
{
	samsung_cmu_register_one(np, &fsys0_cmu_info);
}

CLK_OF_DECLARE(exynos7_clk_fsys0, "samsung,exynos7-clock-fsys0",
	exynos7_clk_fsys0_init);

/* Register Offset definitions for CMU_FSYS1 (0x156E0000) */
#define MUX_SEL_FSYS10			0x0200
#define MUX_SEL_FSYS11			0x0204
#define MUX_SEL_FSYS12			0x0208
#define DIV_FSYS1			0x0600
#define ENABLE_ACLK_FSYS1		0x0800
#define ENABLE_PCLK_FSYS1               0x0900
#define ENABLE_SCLK_FSYS11              0x0A04
#define ENABLE_SCLK_FSYS12              0x0A08
#define ENABLE_SCLK_FSYS13              0x0A0C

/*
 * List of parent clocks for Muxes in CMU_FSYS1
 */
PNAME(mout_aclk_fsys1_200_user_p)	= { "fin_pll", "aclk_fsys1_200" };
PNAME(mout_fsys1_group_p)	= { "fin_pll", "fin_pll_26m",
				"sclk_phy_fsys1_26m" };
PNAME(mout_sclk_mmc0_user_p)		= { "fin_pll", "sclk_mmc0" };
PNAME(mout_sclk_mmc1_user_p)		= { "fin_pll", "sclk_mmc1" };
PNAME(mout_sclk_ufsunipro20_user_p)  = { "fin_pll", "sclk_ufsunipro20" };
PNAME(mout_phyclk_ufs20_tx0_user_p) = { "fin_pll", "phyclk_ufs20_tx0_symbol" };
PNAME(mout_phyclk_ufs20_rx0_user_p) = { "fin_pll", "phyclk_ufs20_rx0_symbol" };
PNAME(mout_phyclk_ufs20_rx1_user_p) = { "fin_pll", "phyclk_ufs20_rx1_symbol" };

/* fixed rate clocks used in the FSYS1 block */
static const struct samsung_fixed_rate_clock fixed_rate_clks_fsys1[] __initconst = {
	FRATE(PHYCLK_UFS20_TX0_SYMBOL, "phyclk_ufs20_tx0_symbol", NULL,
			0, 300000000),
	FRATE(PHYCLK_UFS20_RX0_SYMBOL, "phyclk_ufs20_rx0_symbol", NULL,
			0, 300000000),
	FRATE(PHYCLK_UFS20_RX1_SYMBOL, "phyclk_ufs20_rx1_symbol", NULL,
			0, 300000000),
};

static const unsigned long fsys1_clk_regs[] __initconst = {
	MUX_SEL_FSYS10,
	MUX_SEL_FSYS11,
	MUX_SEL_FSYS12,
	DIV_FSYS1,
	ENABLE_ACLK_FSYS1,
	ENABLE_PCLK_FSYS1,
	ENABLE_SCLK_FSYS11,
	ENABLE_SCLK_FSYS12,
	ENABLE_SCLK_FSYS13,
};

static const struct samsung_mux_clock fsys1_mux_clks[] __initconst = {
	MUX(MOUT_FSYS1_PHYCLK_SEL1, "mout_fsys1_phyclk_sel1",
		mout_fsys1_group_p, MUX_SEL_FSYS10, 16, 2),
	MUX(0, "mout_fsys1_phyclk_sel0", mout_fsys1_group_p,
		 MUX_SEL_FSYS10, 20, 2),
	MUX(0, "mout_aclk_fsys1_200_user", mout_aclk_fsys1_200_user_p,
		MUX_SEL_FSYS10, 28, 1),

	MUX(0, "mout_sclk_mmc1_user", mout_sclk_mmc1_user_p,
		MUX_SEL_FSYS11, 24, 1),
	MUX(0, "mout_sclk_mmc0_user", mout_sclk_mmc0_user_p,
		MUX_SEL_FSYS11, 28, 1),
	MUX(0, "mout_sclk_ufsunipro20_user", mout_sclk_ufsunipro20_user_p,
		MUX_SEL_FSYS11, 20, 1),

	MUX(0, "mout_phyclk_ufs20_rx1_symbol_user",
		mout_phyclk_ufs20_rx1_user_p, MUX_SEL_FSYS12, 16, 1),
	MUX(0, "mout_phyclk_ufs20_rx0_symbol_user",
		mout_phyclk_ufs20_rx0_user_p, MUX_SEL_FSYS12, 24, 1),
	MUX(0, "mout_phyclk_ufs20_tx0_symbol_user",
		mout_phyclk_ufs20_tx0_user_p, MUX_SEL_FSYS12, 28, 1),
};

static const struct samsung_div_clock fsys1_div_clks[] __initconst = {
	DIV(DOUT_PCLK_FSYS1, "dout_pclk_fsys1", "mout_aclk_fsys1_200_user",
		DIV_FSYS1, 0, 2),
};

static const struct samsung_gate_clock fsys1_gate_clks[] __initconst = {
	GATE(SCLK_UFSUNIPRO20_USER, "sclk_ufsunipro20_user",
		"mout_sclk_ufsunipro20_user",
		ENABLE_SCLK_FSYS11, 20, 0, 0),

	GATE(ACLK_MMC1, "aclk_mmc1", "mout_aclk_fsys1_200_user",
		ENABLE_ACLK_FSYS1, 29, 0, 0),
	GATE(ACLK_MMC0, "aclk_mmc0", "mout_aclk_fsys1_200_user",
		ENABLE_ACLK_FSYS1, 30, 0, 0),

	GATE(ACLK_UFS20_LINK, "aclk_ufs20_link", "dout_pclk_fsys1",
		ENABLE_ACLK_FSYS1, 31, 0, 0),
	GATE(PCLK_GPIO_FSYS1, "pclk_gpio_fsys1", "mout_aclk_fsys1_200_user",
		ENABLE_PCLK_FSYS1, 30, 0, 0),

	GATE(PHYCLK_UFS20_RX1_SYMBOL_USER, "phyclk_ufs20_rx1_symbol_user",
		"mout_phyclk_ufs20_rx1_symbol_user",
		ENABLE_SCLK_FSYS12, 16, 0, 0),
	GATE(PHYCLK_UFS20_RX0_SYMBOL_USER, "phyclk_ufs20_rx0_symbol_user",
		"mout_phyclk_ufs20_rx0_symbol_user",
		ENABLE_SCLK_FSYS12, 24, 0, 0),
	GATE(PHYCLK_UFS20_TX0_SYMBOL_USER, "phyclk_ufs20_tx0_symbol_user",
		"mout_phyclk_ufs20_tx0_symbol_user",
		ENABLE_SCLK_FSYS12, 28, 0, 0),

	GATE(OSCCLK_PHY_CLKOUT_EMBEDDED_COMBO_PHY,
		"oscclk_phy_clkout_embedded_combo_phy",
		"fin_pll",
		ENABLE_SCLK_FSYS12, 4, CLK_IGNORE_UNUSED, 0),

	GATE(SCLK_COMBO_PHY_EMBEDDED_26M, "sclk_combo_phy_embedded_26m",
		"mout_fsys1_phyclk_sel1",
		ENABLE_SCLK_FSYS13, 24, CLK_IGNORE_UNUSED, 0),
};

static const struct samsung_cmu_info fsys1_cmu_info __initconst = {
	.fixed_clks		= fixed_rate_clks_fsys1,
	.nr_fixed_clks		= ARRAY_SIZE(fixed_rate_clks_fsys1),
	.mux_clks		= fsys1_mux_clks,
	.nr_mux_clks		= ARRAY_SIZE(fsys1_mux_clks),
	.div_clks		= fsys1_div_clks,
	.nr_div_clks		= ARRAY_SIZE(fsys1_div_clks),
	.gate_clks		= fsys1_gate_clks,
	.nr_gate_clks		= ARRAY_SIZE(fsys1_gate_clks),
	.nr_clk_ids		= FSYS1_NR_CLK,
	.clk_regs		= fsys1_clk_regs,
	.nr_clk_regs		= ARRAY_SIZE(fsys1_clk_regs),
};

static void __init exynos7_clk_fsys1_init(struct device_node *np)
{
	samsung_cmu_register_one(np, &fsys1_cmu_info);
}

CLK_OF_DECLARE(exynos7_clk_fsys1, "samsung,exynos7-clock-fsys1",
	exynos7_clk_fsys1_init);

#define MUX_SEL_MSCL			0x0200
#define DIV_MSCL			0x0600
#define ENABLE_ACLK_MSCL		0x0800
#define ENABLE_PCLK_MSCL		0x0900

/* List of parent clocks for Muxes in CMU_MSCL */
PNAME(mout_aclk_mscl_532_user_p)	= { "fin_pll", "aclk_mscl_532" };

static const unsigned long mscl_clk_regs[] __initconst = {
	MUX_SEL_MSCL,
	DIV_MSCL,
	ENABLE_ACLK_MSCL,
	ENABLE_PCLK_MSCL,
};

static const struct samsung_mux_clock mscl_mux_clks[] __initconst = {
	MUX(USERMUX_ACLK_MSCL_532, "usermux_aclk_mscl_532",
		mout_aclk_mscl_532_user_p, MUX_SEL_MSCL, 0, 1),
};
static const struct samsung_div_clock mscl_div_clks[] __initconst = {
	DIV(DOUT_PCLK_MSCL, "dout_pclk_mscl", "usermux_aclk_mscl_532",
			DIV_MSCL, 0, 3),
};
static const struct samsung_gate_clock mscl_gate_clks[] __initconst = {

	GATE(ACLK_MSCL_0, "aclk_mscl_0", "usermux_aclk_mscl_532",
			ENABLE_ACLK_MSCL, 31, 0, 0),
	GATE(ACLK_MSCL_1, "aclk_mscl_1", "usermux_aclk_mscl_532",
			ENABLE_ACLK_MSCL, 30, 0, 0),
	GATE(ACLK_JPEG, "aclk_jpeg", "usermux_aclk_mscl_532",
			ENABLE_ACLK_MSCL, 29, 0, 0),
	GATE(ACLK_G2D, "aclk_g2d", "usermux_aclk_mscl_532",
			ENABLE_ACLK_MSCL, 28, 0, 0),
	GATE(ACLK_LH_ASYNC_SI_MSCL_0, "aclk_lh_async_si_mscl_0",
			"usermux_aclk_mscl_532",
			ENABLE_ACLK_MSCL, 27, 0, 0),
	GATE(ACLK_LH_ASYNC_SI_MSCL_1, "aclk_lh_async_si_mscl_1",
			"usermux_aclk_mscl_532",
			ENABLE_ACLK_MSCL, 26, 0, 0),
	GATE(ACLK_XIU_MSCLX_0, "aclk_xiu_msclx_0", "usermux_aclk_mscl_532",
			ENABLE_ACLK_MSCL, 25, 0, 0),
	GATE(ACLK_XIU_MSCLX_1, "aclk_xiu_msclx_1", "usermux_aclk_mscl_532",
			ENABLE_ACLK_MSCL, 24, 0, 0),
	GATE(ACLK_AXI2ACEL_BRIDGE, "aclk_axi2acel_bridge",
			"usermux_aclk_mscl_532",
			ENABLE_ACLK_MSCL, 23, 0, 0),
	GATE(ACLK_QE_MSCL_0, "aclk_qe_mscl_0", "usermux_aclk_mscl_532",
			ENABLE_ACLK_MSCL, 22, 0, 0),
	GATE(ACLK_QE_MSCL_1, "aclk_qe_mscl_1", "usermux_aclk_mscl_532",
			ENABLE_ACLK_MSCL, 21, 0, 0),
	GATE(ACLK_QE_JPEG, "aclk_qe_jpeg", "usermux_aclk_mscl_532",
			ENABLE_ACLK_MSCL, 20, 0, 0),
	GATE(ACLK_QE_G2D, "aclk_qe_g2d", "usermux_aclk_mscl_532",
			ENABLE_ACLK_MSCL, 19, 0, 0),
	GATE(ACLK_PPMU_MSCL_0, "aclk_ppmu_mscl_0", "usermux_aclk_mscl_532",
			ENABLE_ACLK_MSCL, 18, 0, 0),
	GATE(ACLK_PPMU_MSCL_1, "aclk_ppmu_mscl_1", "usermux_aclk_mscl_532",
			ENABLE_ACLK_MSCL, 17, 0, 0),
	GATE(ACLK_MSCLNP_133, "aclk_msclnp_133", "usermux_aclk_mscl_532",
			ENABLE_ACLK_MSCL, 16, 0, 0),
	GATE(ACLK_AHB2APB_MSCL0P, "aclk_ahb2apb_mscl0p",
			"usermux_aclk_mscl_532",
			ENABLE_ACLK_MSCL, 15, 0, 0),
	GATE(ACLK_AHB2APB_MSCL1P, "aclk_ahb2apb_mscl1p",
			"usermux_aclk_mscl_532",
			ENABLE_ACLK_MSCL, 14, 0, 0),

	GATE(PCLK_MSCL_0, "pclk_mscl_0", "dout_pclk_mscl",
			ENABLE_PCLK_MSCL, 31, 0, 0),
	GATE(PCLK_MSCL_1, "pclk_mscl_1", "dout_pclk_mscl",
			ENABLE_PCLK_MSCL, 30, 0, 0),
	GATE(PCLK_JPEG, "pclk_jpeg", "dout_pclk_mscl",
			ENABLE_PCLK_MSCL, 29, 0, 0),
	GATE(PCLK_G2D, "pclk_g2d", "dout_pclk_mscl",
			ENABLE_PCLK_MSCL, 28, 0, 0),
	GATE(PCLK_QE_MSCL_0, "pclk_qe_mscl_0", "dout_pclk_mscl",
			ENABLE_PCLK_MSCL, 27, 0, 0),
	GATE(PCLK_QE_MSCL_1, "pclk_qe_mscl_1", "dout_pclk_mscl",
			ENABLE_PCLK_MSCL, 26, 0, 0),
	GATE(PCLK_QE_JPEG, "pclk_qe_jpeg", "dout_pclk_mscl",
			ENABLE_PCLK_MSCL, 25, 0, 0),
	GATE(PCLK_QE_G2D, "pclk_qe_g2d", "dout_pclk_mscl",
			ENABLE_PCLK_MSCL, 24, 0, 0),
	GATE(PCLK_PPMU_MSCL_0, "pclk_ppmu_mscl_0", "dout_pclk_mscl",
			ENABLE_PCLK_MSCL, 23, 0, 0),
	GATE(PCLK_PPMU_MSCL_1, "pclk_ppmu_mscl_1", "dout_pclk_mscl",
			ENABLE_PCLK_MSCL, 22, 0, 0),
	GATE(PCLK_AXI2ACEL_BRIDGE, "pclk_axi2acel_bridge", "dout_pclk_mscl",
			ENABLE_PCLK_MSCL, 21, 0, 0),
	GATE(PCLK_PMU_MSCL, "pclk_pmu_mscl", "dout_pclk_mscl",
			ENABLE_PCLK_MSCL, 20, 0, 0),
};

static const struct samsung_cmu_info mscl_cmu_info __initconst = {
	.mux_clks		= mscl_mux_clks,
	.nr_mux_clks		= ARRAY_SIZE(mscl_mux_clks),
	.div_clks		= mscl_div_clks,
	.nr_div_clks		= ARRAY_SIZE(mscl_div_clks),
	.gate_clks		= mscl_gate_clks,
	.nr_gate_clks		= ARRAY_SIZE(mscl_gate_clks),
	.nr_clk_ids		= MSCL_NR_CLK,
	.clk_regs		= mscl_clk_regs,
	.nr_clk_regs		= ARRAY_SIZE(mscl_clk_regs),
};

static void __init exynos7_clk_mscl_init(struct device_node *np)
{
	samsung_cmu_register_one(np, &mscl_cmu_info);
}

CLK_OF_DECLARE(exynos7_clk_mscl, "samsung,exynos7-clock-mscl",
		exynos7_clk_mscl_init);

/* Register Offset definitions for CMU_AUD (0x114C0000) */
#define	MUX_SEL_AUD			0x0200
#define	DIV_AUD0			0x0600
#define	DIV_AUD1			0x0604
#define	ENABLE_ACLK_AUD			0x0800
#define	ENABLE_PCLK_AUD			0x0900
#define	ENABLE_SCLK_AUD			0x0A00

/*
 * List of parent clocks for Muxes in CMU_AUD
 */
PNAME(mout_aud_pll_user_p) = { "fin_pll", "fout_aud_pll" };
PNAME(mout_aud_group_p) = { "dout_aud_cdclk", "ioclk_audiocdclk0" };

static const unsigned long aud_clk_regs[] __initconst = {
	MUX_SEL_AUD,
	DIV_AUD0,
	DIV_AUD1,
	ENABLE_ACLK_AUD,
	ENABLE_PCLK_AUD,
	ENABLE_SCLK_AUD,
};

static const struct samsung_mux_clock aud_mux_clks[] __initconst = {
	MUX(0, "mout_sclk_i2s", mout_aud_group_p, MUX_SEL_AUD, 12, 1),
	MUX(0, "mout_sclk_pcm", mout_aud_group_p, MUX_SEL_AUD, 16, 1),
	MUX(0, "mout_aud_pll_user", mout_aud_pll_user_p, MUX_SEL_AUD, 20, 1),
};

static const struct samsung_div_clock aud_div_clks[] __initconst = {
	DIV(0, "dout_aud_ca5", "mout_aud_pll_user", DIV_AUD0, 0, 4),
	DIV(0, "dout_aclk_aud", "dout_aud_ca5", DIV_AUD0, 4, 4),
	DIV(0, "dout_aud_pclk_dbg", "dout_aud_ca5", DIV_AUD0, 8, 4),

	DIV(0, "dout_sclk_i2s", "mout_sclk_i2s", DIV_AUD1, 0, 4),
	DIV(0, "dout_sclk_pcm", "mout_sclk_pcm", DIV_AUD1, 4, 8),
	DIV(0, "dout_sclk_uart", "dout_aud_cdclk", DIV_AUD1, 12, 4),
	DIV(0, "dout_sclk_slimbus", "dout_aud_cdclk", DIV_AUD1, 16, 5),
	DIV(0, "dout_aud_cdclk", "mout_aud_pll_user", DIV_AUD1, 24, 4),
};

static const struct samsung_gate_clock aud_gate_clks[] __initconst = {
	GATE(SCLK_PCM, "sclk_pcm", "dout_sclk_pcm",
			ENABLE_SCLK_AUD, 27, CLK_SET_RATE_PARENT, 0),
	GATE(SCLK_I2S, "sclk_i2s", "dout_sclk_i2s",
			ENABLE_SCLK_AUD, 28, CLK_SET_RATE_PARENT, 0),
	GATE(0, "sclk_uart", "dout_sclk_uart", ENABLE_SCLK_AUD, 29, 0, 0),
	GATE(0, "sclk_slimbus", "dout_sclk_slimbus",
			ENABLE_SCLK_AUD, 30, 0, 0),

	GATE(0, "pclk_dbg_aud", "dout_aud_pclk_dbg", ENABLE_PCLK_AUD, 19, 0, 0),
	GATE(0, "pclk_gpio_aud", "dout_aclk_aud", ENABLE_PCLK_AUD, 20, 0, 0),
	GATE(0, "pclk_wdt1", "dout_aclk_aud", ENABLE_PCLK_AUD, 22, 0, 0),
	GATE(0, "pclk_wdt0", "dout_aclk_aud", ENABLE_PCLK_AUD, 23, 0, 0),
	GATE(0, "pclk_slimbus", "dout_aclk_aud", ENABLE_PCLK_AUD, 24, 0, 0),
	GATE(0, "pclk_uart", "dout_aclk_aud", ENABLE_PCLK_AUD, 25, 0, 0),
	GATE(PCLK_PCM, "pclk_pcm", "dout_aclk_aud",
			ENABLE_PCLK_AUD, 26, CLK_SET_RATE_PARENT, 0),
	GATE(PCLK_I2S, "pclk_i2s", "dout_aclk_aud",
			ENABLE_PCLK_AUD, 27, CLK_SET_RATE_PARENT, 0),
	GATE(0, "pclk_timer", "dout_aclk_aud", ENABLE_PCLK_AUD, 28, 0, 0),
	GATE(0, "pclk_smmu_aud", "dout_aclk_aud", ENABLE_PCLK_AUD, 31, 0, 0),

	GATE(0, "aclk_smmu_aud", "dout_aclk_aud", ENABLE_ACLK_AUD, 27, 0, 0),
	GATE(0, "aclk_acel_lh_async_si_top", "dout_aclk_aud",
			 ENABLE_ACLK_AUD, 28, 0, 0),
	GATE(ACLK_ADMA, "aclk_dmac", "dout_aclk_aud", ENABLE_ACLK_AUD, 31, 0, 0),
};

static const struct samsung_cmu_info aud_cmu_info __initconst = {
	.mux_clks		= aud_mux_clks,
	.nr_mux_clks		= ARRAY_SIZE(aud_mux_clks),
	.div_clks		= aud_div_clks,
	.nr_div_clks		= ARRAY_SIZE(aud_div_clks),
	.gate_clks		= aud_gate_clks,
	.nr_gate_clks		= ARRAY_SIZE(aud_gate_clks),
	.nr_clk_ids		= AUD_NR_CLK,
	.clk_regs		= aud_clk_regs,
	.nr_clk_regs		= ARRAY_SIZE(aud_clk_regs),
};

static void __init exynos7_clk_aud_init(struct device_node *np)
{
	samsung_cmu_register_one(np, &aud_cmu_info);
}

CLK_OF_DECLARE(exynos7_clk_aud, "samsung,exynos7-clock-aud",
		exynos7_clk_aud_init);

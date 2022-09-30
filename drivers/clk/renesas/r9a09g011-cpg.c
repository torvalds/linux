// SPDX-License-Identifier: GPL-2.0
/*
 * RZ/V2M Clock Pulse Generator / Module Standby and Software Reset
 *
 * Copyright (C) 2022 Renesas Electronics Corp.
 *
 * Based on r9a07g044-cpg.c
 */

#include <linux/clk-provider.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/kernel.h>

#include <dt-bindings/clock/r9a09g011-cpg.h>

#include "rzg2l-cpg.h"

#define RZV2M_SAMPLL4_CLK1	0x104
#define RZV2M_SAMPLL4_CLK2	0x108

#define PLL4_CONF	(RZV2M_SAMPLL4_CLK1 << 22 | RZV2M_SAMPLL4_CLK2 << 12)

#define DIV_A		DDIV_PACK(0x200, 0, 3)
#define DIV_B		DDIV_PACK(0x204, 0, 2)
#define DIV_E		DDIV_PACK(0x204, 8, 1)
#define DIV_W		DDIV_PACK(0x328, 0, 3)

#define SEL_B		SEL_PLL_PACK(0x214, 0, 1)
#define SEL_E		SEL_PLL_PACK(0x214, 2, 1)
#define SEL_W0		SEL_PLL_PACK(0x32C, 0, 1)

enum clk_ids {
	/* Core Clock Outputs exported to DT */
	LAST_DT_CORE_CLK = 0,

	/* External Input Clocks */
	CLK_EXTAL,

	/* Internal Core Clocks */
	CLK_MAIN,
	CLK_MAIN_24,
	CLK_MAIN_2,
	CLK_PLL1,
	CLK_PLL2,
	CLK_PLL2_800,
	CLK_PLL2_400,
	CLK_PLL2_200,
	CLK_PLL2_100,
	CLK_PLL4,
	CLK_DIV_A,
	CLK_DIV_B,
	CLK_DIV_E,
	CLK_DIV_W,
	CLK_SEL_B,
	CLK_SEL_B_D2,
	CLK_SEL_E,
	CLK_SEL_W0,

	/* Module Clocks */
	MOD_CLK_BASE
};

/* Divider tables */
static const struct clk_div_table dtable_diva[] = {
	{0, 1},
	{1, 2},
	{2, 3},
	{3, 4},
	{4, 6},
	{5, 12},
	{6, 24},
	{0, 0},
};

static const struct clk_div_table dtable_divb[] = {
	{0, 1},
	{1, 2},
	{2, 4},
	{3, 8},
	{0, 0},
};

static const struct clk_div_table dtable_divw[] = {
	{0, 6},
	{1, 7},
	{2, 8},
	{3, 9},
	{4, 10},
	{5, 11},
	{6, 12},
	{0, 0},
};

/* Mux clock tables */
static const char * const sel_b[] = { ".main", ".divb" };
static const char * const sel_e[] = { ".main", ".dive" };
static const char * const sel_w[] = { ".main", ".divw" };

static const struct cpg_core_clk r9a09g011_core_clks[] __initconst = {
	/* External Clock Inputs */
	DEF_INPUT("extal",	CLK_EXTAL),

	/* Internal Core Clocks */
	DEF_FIXED(".main",	CLK_MAIN,	CLK_EXTAL,	1,	1),
	DEF_FIXED(".main_24",	CLK_MAIN_24,	CLK_MAIN,	1,	2),
	DEF_FIXED(".main_2",	CLK_MAIN_2,	CLK_MAIN,	1,	24),
	DEF_FIXED(".pll1",	CLK_PLL1,	CLK_MAIN_2,	498,	1),
	DEF_FIXED(".pll2",	CLK_PLL2,	CLK_MAIN_2,	800,	1),
	DEF_FIXED(".pll2_800",	CLK_PLL2_800,	CLK_PLL2,	1,	2),
	DEF_FIXED(".pll2_400",	CLK_PLL2_400,	CLK_PLL2_800,	1,	2),
	DEF_FIXED(".pll2_200",	CLK_PLL2_200,	CLK_PLL2_800,	1,	4),
	DEF_FIXED(".pll2_100",	CLK_PLL2_100,	CLK_PLL2_800,	1,	8),
	DEF_SAMPLL(".pll4",	CLK_PLL4,	CLK_MAIN_2,	PLL4_CONF),

	DEF_DIV_RO(".diva",	CLK_DIV_A,	CLK_PLL1,	DIV_A,	dtable_diva),
	DEF_DIV_RO(".divb",	CLK_DIV_B,	CLK_PLL2_400,	DIV_B,	dtable_divb),
	DEF_DIV_RO(".dive",	CLK_DIV_E,	CLK_PLL2_100,	DIV_E,	NULL),
	DEF_DIV_RO(".divw",	CLK_DIV_W,	CLK_PLL4,	DIV_W,	dtable_divw),

	DEF_MUX_RO(".selb",	CLK_SEL_B,	SEL_B,		sel_b),
	DEF_MUX_RO(".sele",	CLK_SEL_E,	SEL_E,		sel_e),
	DEF_MUX(".selw0",	CLK_SEL_W0,	SEL_W0,		sel_w),

	DEF_FIXED(".selb_d2",	CLK_SEL_B_D2,	CLK_SEL_B,	1,	2),
};

static const struct rzg2l_mod_clk r9a09g011_mod_clks[] __initconst = {
	DEF_MOD("pfc",		R9A09G011_PFC_PCLK,	 CLK_MAIN,     0x400, 2),
	DEF_MOD("gic",		R9A09G011_GIC_CLK,	 CLK_SEL_B_D2, 0x400, 5),
	DEF_COUPLED("eth_axi",	R9A09G011_ETH0_CLK_AXI,	 CLK_PLL2_200, 0x40c, 8),
	DEF_COUPLED("eth_chi",	R9A09G011_ETH0_CLK_CHI,	 CLK_PLL2_100, 0x40c, 8),
	DEF_MOD("eth_clk_gptp",	R9A09G011_ETH0_GPTP_EXT, CLK_PLL2_100, 0x40c, 9),
	DEF_MOD("syc_cnt_clk",	R9A09G011_SYC_CNT_CLK,	 CLK_MAIN_24,  0x41c, 12),
	DEF_MOD("wdt0_pclk",	R9A09G011_WDT0_PCLK,	 CLK_SEL_E,    0x428, 12),
	DEF_MOD("wdt0_clk",	R9A09G011_WDT0_CLK,	 CLK_MAIN,     0x428, 13),
	DEF_MOD("urt_pclk",	R9A09G011_URT_PCLK,	 CLK_SEL_E,    0x438, 4),
	DEF_MOD("urt0_clk",	R9A09G011_URT0_CLK,	 CLK_SEL_W0,   0x438, 5),
	DEF_MOD("ca53",		R9A09G011_CA53_CLK,	 CLK_DIV_A,    0x448, 0),
};

static const struct rzg2l_reset r9a09g011_resets[] = {
	DEF_RST(R9A09G011_PFC_PRESETN,		0x600, 2),
	DEF_RST_MON(R9A09G011_ETH0_RST_HW_N,	0x608, 11, 11),
	DEF_RST_MON(R9A09G011_SYC_RST_N,	0x610, 9,  13),
	DEF_RST_MON(R9A09G011_WDT0_PRESETN,	0x614, 12, 19),
};

static const unsigned int r9a09g011_crit_mod_clks[] __initconst = {
	MOD_CLK_BASE + R9A09G011_CA53_CLK,
	MOD_CLK_BASE + R9A09G011_GIC_CLK,
	MOD_CLK_BASE + R9A09G011_SYC_CNT_CLK,
	MOD_CLK_BASE + R9A09G011_URT_PCLK,
};

const struct rzg2l_cpg_info r9a09g011_cpg_info = {
	/* Core Clocks */
	.core_clks = r9a09g011_core_clks,
	.num_core_clks = ARRAY_SIZE(r9a09g011_core_clks),
	.last_dt_core_clk = LAST_DT_CORE_CLK,
	.num_total_core_clks = MOD_CLK_BASE,

	/* Critical Module Clocks */
	.crit_mod_clks = r9a09g011_crit_mod_clks,
	.num_crit_mod_clks = ARRAY_SIZE(r9a09g011_crit_mod_clks),

	/* Module Clocks */
	.mod_clks = r9a09g011_mod_clks,
	.num_mod_clks = ARRAY_SIZE(r9a09g011_mod_clks),
	.num_hw_mod_clks = R9A09G011_CA53_CLK + 1,

	/* Resets */
	.resets = r9a09g011_resets,
	.num_resets = ARRAY_SIZE(r9a09g011_resets),

	.has_clk_mon_regs = false,
};

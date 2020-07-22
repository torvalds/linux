// SPDX-License-Identifier: GPL-2.0
/*
 * SAMA7G5 PMC code.
 *
 * Copyright (C) 2020 Microchip Technology Inc. and its subsidiaries
 *
 * Author: Claudiu Beznea <claudiu.beznea@microchip.com>
 *
 */
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/mfd/syscon.h>
#include <linux/slab.h>

#include <dt-bindings/clock/at91.h>

#include "pmc.h"

#define SAMA7G5_INIT_TABLE(_table, _count)		\
	do {						\
		u8 _i;					\
		for (_i = 0; _i < (_count); _i++)	\
			(_table)[_i] = _i;		\
	} while (0)

#define SAMA7G5_FILL_TABLE(_to, _from, _count)		\
	do {						\
		u8 _i;					\
		for (_i = 0; _i < (_count); _i++) {	\
			(_to)[_i] = (_from)[_i];	\
		}					\
	} while (0)

static DEFINE_SPINLOCK(pmc_pll_lock);
static DEFINE_SPINLOCK(pmc_mckX_lock);

/**
 * PLL clocks identifiers
 * @PLL_ID_CPU:		CPU PLL identifier
 * @PLL_ID_SYS:		System PLL identifier
 * @PLL_ID_DDR:		DDR PLL identifier
 * @PLL_ID_IMG:		Image subsystem PLL identifier
 * @PLL_ID_BAUD:	Baud PLL identifier
 * @PLL_ID_AUDIO:	Audio PLL identifier
 * @PLL_ID_ETH:		Ethernet PLL identifier
 */
enum pll_ids {
	PLL_ID_CPU,
	PLL_ID_SYS,
	PLL_ID_DDR,
	PLL_ID_IMG,
	PLL_ID_BAUD,
	PLL_ID_AUDIO,
	PLL_ID_ETH,
	PLL_ID_MAX,
};

/**
 * PLL type identifiers
 * @PLL_TYPE_FRAC:	fractional PLL identifier
 * @PLL_TYPE_DIV:	divider PLL identifier
 */
enum pll_type {
	PLL_TYPE_FRAC,
	PLL_TYPE_DIV,
};

/* Layout for fractional PLLs. */
static const struct clk_pll_layout pll_layout_frac = {
	.mul_mask	= GENMASK(31, 24),
	.frac_mask	= GENMASK(21, 0),
	.mul_shift	= 24,
	.frac_shift	= 0,
};

/* Layout for DIVPMC dividers. */
static const struct clk_pll_layout pll_layout_divpmc = {
	.div_mask	= GENMASK(7, 0),
	.endiv_mask	= BIT(29),
	.div_shift	= 0,
	.endiv_shift	= 29,
};

/* Layout for DIVIO dividers. */
static const struct clk_pll_layout pll_layout_divio = {
	.div_mask	= GENMASK(19, 12),
	.endiv_mask	= BIT(30),
	.div_shift	= 12,
	.endiv_shift	= 30,
};

/**
 * PLL clocks description
 * @n:		clock name
 * @p:		clock parent
 * @l:		clock layout
 * @t:		clock type
 * @f:		true if clock is critical and cannot be disabled
 * @eid:	export index in sama7g5->chws[] array
 */
static const struct {
	const char *n;
	const char *p;
	const struct clk_pll_layout *l;
	u8 t;
	u8 c;
	u8 eid;
} sama7g5_plls[][PLL_ID_MAX] = {
	[PLL_ID_CPU] = {
		{ .n = "cpupll_fracck",
		  .p = "mainck",
		  .l = &pll_layout_frac,
		  .t = PLL_TYPE_FRAC,
		  .c = 1, },

		{ .n = "cpupll_divpmcck",
		  .p = "cpupll_fracck",
		  .l = &pll_layout_divpmc,
		  .t = PLL_TYPE_DIV,
		  .c = 1, },
	},

	[PLL_ID_SYS] = {
		{ .n = "syspll_fracck",
		  .p = "mainck",
		  .l = &pll_layout_frac,
		  .t = PLL_TYPE_FRAC,
		  .c = 1, },

		{ .n = "syspll_divpmcck",
		  .p = "syspll_fracck",
		  .l = &pll_layout_divpmc,
		  .t = PLL_TYPE_DIV,
		  .c = 1, },
	},

	[PLL_ID_DDR] = {
		{ .n = "ddrpll_fracck",
		  .p = "mainck",
		  .l = &pll_layout_frac,
		  .t = PLL_TYPE_FRAC,
		  .c = 1, },

		{ .n = "ddrpll_divpmcck",
		  .p = "ddrpll_fracck",
		  .l = &pll_layout_divpmc,
		  .t = PLL_TYPE_DIV,
		  .c = 1, },
	},

	[PLL_ID_IMG] = {
		{ .n = "imgpll_fracck",
		  .p = "mainck",
		  .l = &pll_layout_frac,
		  .t = PLL_TYPE_FRAC, },

		{ .n = "imgpll_divpmcck",
		  .p = "imgpll_fracck",
		  .l = &pll_layout_divpmc,
		  .t = PLL_TYPE_DIV, },
	},

	[PLL_ID_BAUD] = {
		{ .n = "baudpll_fracck",
		  .p = "mainck",
		  .l = &pll_layout_frac,
		  .t = PLL_TYPE_FRAC, },

		{ .n = "baudpll_divpmcck",
		  .p = "baudpll_fracck",
		  .l = &pll_layout_divpmc,
		  .t = PLL_TYPE_DIV, },
	},

	[PLL_ID_AUDIO] = {
		{ .n = "audiopll_fracck",
		  .p = "main_xtal",
		  .l = &pll_layout_frac,
		  .t = PLL_TYPE_FRAC, },

		{ .n = "audiopll_divpmcck",
		  .p = "audiopll_fracck",
		  .l = &pll_layout_divpmc,
		  .t = PLL_TYPE_DIV,
		  .eid = PMC_I2S0_MUX, },

		{ .n = "audiopll_diviock",
		  .p = "audiopll_fracck",
		  .l = &pll_layout_divio,
		  .t = PLL_TYPE_DIV,
		  .eid = PMC_I2S1_MUX, },
	},

	[PLL_ID_ETH] = {
		{ .n = "ethpll_fracck",
		  .p = "main_xtal",
		  .l = &pll_layout_frac,
		  .t = PLL_TYPE_FRAC, },

		{ .n = "ethpll_divpmcck",
		  .p = "ethpll_fracck",
		  .l = &pll_layout_divpmc,
		  .t = PLL_TYPE_DIV, },
	},
};

/**
 * Master clock (MCK[1..4]) description
 * @n:			clock name
 * @ep:			extra parents names array
 * @ep_chg_chg_id:	index in parents array that specifies the changeable
 *			parent
 * @ep_count:		extra parents count
 * @ep_mux_table:	mux table for extra parents
 * @id:			clock id
 * @c:			true if clock is critical and cannot be disabled
 */
static const struct {
	const char *n;
	const char *ep[4];
	int ep_chg_id;
	u8 ep_count;
	u8 ep_mux_table[4];
	u8 id;
	u8 c;
} sama7g5_mckx[] = {
	{ .n = "mck1",
	  .id = 1,
	  .ep = { "syspll_divpmcck", },
	  .ep_mux_table = { 5, },
	  .ep_count = 1,
	  .ep_chg_id = INT_MIN,
	  .c = 1, },

	{ .n = "mck2",
	  .id = 2,
	  .ep = { "ddrpll_divpmcck", },
	  .ep_mux_table = { 6, },
	  .ep_count = 1,
	  .ep_chg_id = INT_MIN,
	  .c = 1, },

	{ .n = "mck3",
	  .id = 3,
	  .ep = { "syspll_divpmcck", "ddrpll_divpmcck", "imgpll_divpmcck", },
	  .ep_mux_table = { 5, 6, 7, },
	  .ep_count = 3,
	  .ep_chg_id = 6, },

	{ .n = "mck4",
	  .id = 4,
	  .ep = { "syspll_divpmcck", },
	  .ep_mux_table = { 5, },
	  .ep_count = 1,
	  .ep_chg_id = INT_MIN,
	  .c = 1, },
};

/**
 * System clock description
 * @n:	clock name
 * @p:	clock parent name
 * @id: clock id
 */
static const struct {
	const char *n;
	const char *p;
	u8 id;
} sama7g5_systemck[] = {
	{ .n = "pck0",		.p = "prog0", .id = 8, },
	{ .n = "pck1",		.p = "prog1", .id = 9, },
	{ .n = "pck2",		.p = "prog2", .id = 10, },
	{ .n = "pck3",		.p = "prog3", .id = 11, },
	{ .n = "pck4",		.p = "prog4", .id = 12, },
	{ .n = "pck5",		.p = "prog5", .id = 13, },
	{ .n = "pck6",		.p = "prog6", .id = 14, },
	{ .n = "pck7",		.p = "prog7", .id = 15, },
};

/* Mux table for programmable clocks. */
static u32 sama7g5_prog_mux_table[] = { 0, 1, 2, 3, 5, 6, 7, 8, 9, 10, };

/**
 * Peripheral clock description
 * @n:		clock name
 * @p:		clock parent name
 * @r:		clock range values
 * @id:		clock id
 * @chgp:	index in parent array of the changeable parent
 */
static const struct {
	const char *n;
	const char *p;
	struct clk_range r;
	u8 chgp;
	u8 id;
} sama7g5_periphck[] = {
	{ .n = "pioA_clk",	.p = "mck0", .id = 11, },
	{ .n = "sfr_clk",	.p = "mck1", .id = 19, },
	{ .n = "hsmc_clk",	.p = "mck1", .id = 21, },
	{ .n = "xdmac0_clk",	.p = "mck1", .id = 22, },
	{ .n = "xdmac1_clk",	.p = "mck1", .id = 23, },
	{ .n = "xdmac2_clk",	.p = "mck1", .id = 24, },
	{ .n = "acc_clk",	.p = "mck1", .id = 25, },
	{ .n = "aes_clk",	.p = "mck1", .id = 27, },
	{ .n = "tzaesbasc_clk",	.p = "mck1", .id = 28, },
	{ .n = "asrc_clk",	.p = "mck1", .id = 30, .r = { .max = 200000000, }, },
	{ .n = "cpkcc_clk",	.p = "mck0", .id = 32, },
	{ .n = "csi_clk",	.p = "mck3", .id = 33, .r = { .max = 266000000, }, .chgp = 1, },
	{ .n = "csi2dc_clk",	.p = "mck3", .id = 34, .r = { .max = 266000000, }, .chgp = 1, },
	{ .n = "eic_clk",	.p = "mck1", .id = 37, },
	{ .n = "flex0_clk",	.p = "mck1", .id = 38, },
	{ .n = "flex1_clk",	.p = "mck1", .id = 39, },
	{ .n = "flex2_clk",	.p = "mck1", .id = 40, },
	{ .n = "flex3_clk",	.p = "mck1", .id = 41, },
	{ .n = "flex4_clk",	.p = "mck1", .id = 42, },
	{ .n = "flex5_clk",	.p = "mck1", .id = 43, },
	{ .n = "flex6_clk",	.p = "mck1", .id = 44, },
	{ .n = "flex7_clk",	.p = "mck1", .id = 45, },
	{ .n = "flex8_clk",	.p = "mck1", .id = 46, },
	{ .n = "flex9_clk",	.p = "mck1", .id = 47, },
	{ .n = "flex10_clk",	.p = "mck1", .id = 48, },
	{ .n = "flex11_clk",	.p = "mck1", .id = 49, },
	{ .n = "gmac0_clk",	.p = "mck1", .id = 51, },
	{ .n = "gmac1_clk",	.p = "mck1", .id = 52, },
	{ .n = "icm_clk",	.p = "mck1", .id = 55, },
	{ .n = "isc_clk",	.p = "mck3", .id = 56, .r = { .max = 266000000, }, .chgp = 1, },
	{ .n = "i2smcc0_clk",	.p = "mck1", .id = 57, .r = { .max = 200000000, }, },
	{ .n = "i2smcc1_clk",	.p = "mck1", .id = 58, .r = { .max = 200000000, }, },
	{ .n = "matrix_clk",	.p = "mck1", .id = 60, },
	{ .n = "mcan0_clk",	.p = "mck1", .id = 61, .r = { .max = 200000000, }, },
	{ .n = "mcan1_clk",	.p = "mck1", .id = 62, .r = { .max = 200000000, }, },
	{ .n = "mcan2_clk",	.p = "mck1", .id = 63, .r = { .max = 200000000, }, },
	{ .n = "mcan3_clk",	.p = "mck1", .id = 64, .r = { .max = 200000000, }, },
	{ .n = "mcan4_clk",	.p = "mck1", .id = 65, .r = { .max = 200000000, }, },
	{ .n = "mcan5_clk",	.p = "mck1", .id = 66, .r = { .max = 200000000, }, },
	{ .n = "pdmc0_clk",	.p = "mck1", .id = 68, .r = { .max = 200000000, }, },
	{ .n = "pdmc1_clk",	.p = "mck1", .id = 69, .r = { .max = 200000000, }, },
	{ .n = "pit64b0_clk",	.p = "mck1", .id = 70, },
	{ .n = "pit64b1_clk",	.p = "mck1", .id = 71, },
	{ .n = "pit64b2_clk",	.p = "mck1", .id = 72, },
	{ .n = "pit64b3_clk",	.p = "mck1", .id = 73, },
	{ .n = "pit64b4_clk",	.p = "mck1", .id = 74, },
	{ .n = "pit64b5_clk",	.p = "mck1", .id = 75, },
	{ .n = "pwm_clk",	.p = "mck1", .id = 77, },
	{ .n = "qspi0_clk",	.p = "mck1", .id = 78, },
	{ .n = "qspi1_clk",	.p = "mck1", .id = 79, },
	{ .n = "sdmmc0_clk",	.p = "mck1", .id = 80, },
	{ .n = "sdmmc1_clk",	.p = "mck1", .id = 81, },
	{ .n = "sdmmc2_clk",	.p = "mck1", .id = 82, },
	{ .n = "sha_clk",	.p = "mck1", .id = 83, },
	{ .n = "spdifrx_clk",	.p = "mck1", .id = 84, .r = { .max = 200000000, }, },
	{ .n = "spdiftx_clk",	.p = "mck1", .id = 85, .r = { .max = 200000000, }, },
	{ .n = "ssc0_clk",	.p = "mck1", .id = 86, .r = { .max = 200000000, }, },
	{ .n = "ssc1_clk",	.p = "mck1", .id = 87, .r = { .max = 200000000, }, },
	{ .n = "tcb0_ch0_clk",	.p = "mck1", .id = 88, .r = { .max = 200000000, }, },
	{ .n = "tcb0_ch1_clk",	.p = "mck1", .id = 89, .r = { .max = 200000000, }, },
	{ .n = "tcb0_ch2_clk",	.p = "mck1", .id = 90, .r = { .max = 200000000, }, },
	{ .n = "tcb1_ch0_clk",	.p = "mck1", .id = 91, .r = { .max = 200000000, }, },
	{ .n = "tcb1_ch1_clk",	.p = "mck1", .id = 92, .r = { .max = 200000000, }, },
	{ .n = "tcb1_ch2_clk",	.p = "mck1", .id = 93, .r = { .max = 200000000, }, },
	{ .n = "tcpca_clk",	.p = "mck1", .id = 94, },
	{ .n = "tcpcb_clk",	.p = "mck1", .id = 95, },
	{ .n = "tdes_clk",	.p = "mck1", .id = 96, },
	{ .n = "trng_clk",	.p = "mck1", .id = 97, },
	{ .n = "udphsa_clk",	.p = "mck1", .id = 104, },
	{ .n = "udphsb_clk",	.p = "mck1", .id = 105, },
	{ .n = "uhphs_clk",	.p = "mck1", .id = 106, },
};

/**
 * Generic clock description
 * @n:			clock name
 * @pp:			PLL parents
 * @pp_mux_table:	PLL parents mux table
 * @r:			clock output range
 * @pp_chg_id:		id in parrent array of changeable PLL parent
 * @pp_count:		PLL parents count
 * @id:			clock id
 */
static const struct {
	const char *n;
	const char *pp[8];
	const char pp_mux_table[8];
	struct clk_range r;
	int pp_chg_id;
	u8 pp_count;
	u8 id;
} sama7g5_gck[] = {
	{ .n  = "adc_gclk",
	  .id = 26,
	  .r = { .max = 100000000, },
	  .pp = { "syspll_divpmcck", "imgpll_divpmcck", "audiopll_divpmcck", },
	  .pp_mux_table = { 5, 7, 9, },
	  .pp_count = 3,
	  .pp_chg_id = INT_MIN, },

	{ .n  = "asrc_gclk",
	  .id = 30,
	  .r = { .max = 200000000 },
	  .pp = { "audiopll_divpmcck", },
	  .pp_mux_table = { 9, },
	  .pp_count = 1,
	  .pp_chg_id = 4, },

	{ .n  = "csi_gclk",
	  .id = 33,
	  .r = { .max = 27000000  },
	  .pp = { "ddrpll_divpmcck", "imgpll_divpmcck", },
	  .pp_mux_table = { 6, 7, },
	  .pp_count = 2,
	  .pp_chg_id = INT_MIN, },

	{ .n  = "flex0_gclk",
	  .id = 38,
	  .r = { .max = 200000000 },
	  .pp = { "syspll_divpmcck", "baudpll_divpmcck", },
	  .pp_mux_table = { 5, 8, },
	  .pp_count = 2,
	  .pp_chg_id = INT_MIN, },

	{ .n  = "flex1_gclk",
	  .id = 39,
	  .r = { .max = 200000000 },
	  .pp = { "syspll_divpmcck", "baudpll_divpmcck", },
	  .pp_mux_table = { 5, 8, },
	  .pp_count = 2,
	  .pp_chg_id = INT_MIN, },

	{ .n  = "flex2_gclk",
	  .id = 40,
	  .r = { .max = 200000000 },
	  .pp = { "syspll_divpmcck", "baudpll_divpmcck", },
	  .pp_mux_table = { 5, 8, },
	  .pp_count = 2,
	  .pp_chg_id = INT_MIN, },

	{ .n  = "flex3_gclk",
	  .id = 41,
	  .r = { .max = 200000000 },
	  .pp = { "syspll_divpmcck", "baudpll_divpmcck", },
	  .pp_mux_table = { 5, 8, },
	  .pp_count = 2,
	  .pp_chg_id = INT_MIN, },

	{ .n  = "flex4_gclk",
	  .id = 42,
	  .r = { .max = 200000000 },
	  .pp = { "syspll_divpmcck", "baudpll_divpmcck", },
	  .pp_mux_table = { 5, 8, },
	  .pp_count = 2,
	  .pp_chg_id = INT_MIN, },

	{ .n  = "flex5_gclk",
	  .id = 43,
	  .r = { .max = 200000000 },
	  .pp = { "syspll_divpmcck", "baudpll_divpmcck", },
	  .pp_mux_table = { 5, 8, },
	  .pp_count = 2,
	  .pp_chg_id = INT_MIN, },

	{ .n  = "flex6_gclk",
	  .id = 44,
	  .r = { .max = 200000000 },
	  .pp = { "syspll_divpmcck", "baudpll_divpmcck", },
	  .pp_mux_table = { 5, 8, },
	  .pp_count = 2,
	  .pp_chg_id = INT_MIN, },

	{ .n  = "flex7_gclk",
	  .id = 45,
	  .r = { .max = 200000000 },
	  .pp = { "syspll_divpmcck", "baudpll_divpmcck", },
	  .pp_mux_table = { 5, 8, },
	  .pp_count = 2,
	  .pp_chg_id = INT_MIN, },

	{ .n  = "flex8_gclk",
	  .id = 46,
	  .r = { .max = 200000000 },
	  .pp = { "syspll_divpmcck", "baudpll_divpmcck", },
	  .pp_mux_table = { 5, 8, },
	  .pp_count = 2,
	  .pp_chg_id = INT_MIN, },

	{ .n  = "flex9_gclk",
	  .id = 47,
	  .r = { .max = 200000000 },
	  .pp = { "syspll_divpmcck", "baudpll_divpmcck", },
	  .pp_mux_table = { 5, 8, },
	  .pp_count = 2,
	  .pp_chg_id = INT_MIN, },

	{ .n  = "flex10_gclk",
	  .id = 48,
	  .r = { .max = 200000000 },
	  .pp = { "syspll_divpmcck", "baudpll_divpmcck", },
	  .pp_mux_table = { 5, 8, },
	  .pp_count = 2,
	  .pp_chg_id = INT_MIN, },

	{ .n  = "flex11_gclk",
	  .id = 49,
	  .r = { .max = 200000000 },
	  .pp = { "syspll_divpmcck", "baudpll_divpmcck", },
	  .pp_mux_table = { 5, 8, },
	  .pp_count = 2,
	  .pp_chg_id = INT_MIN, },

	{ .n  = "gmac0_gclk",
	  .id = 51,
	  .r = { .max = 125000000 },
	  .pp = { "ethpll_divpmcck", },
	  .pp_mux_table = { 10, },
	  .pp_count = 1,
	  .pp_chg_id = 4, },

	{ .n  = "gmac1_gclk",
	  .id = 52,
	  .r = { .max = 50000000  },
	  .pp = { "ethpll_divpmcck", },
	  .pp_mux_table = { 10, },
	  .pp_count = 1,
	  .pp_chg_id = INT_MIN, },

	{ .n  = "gmac0_tsu_gclk",
	  .id = 53,
	  .r = { .max = 300000000 },
	  .pp = { "audiopll_divpmcck", "ethpll_divpmcck", },
	  .pp_mux_table = { 9, 10, },
	  .pp_count = 2,
	  .pp_chg_id = INT_MIN, },

	{ .n  = "gmac1_tsu_gclk",
	  .id = 54,
	  .r = { .max = 300000000 },
	  .pp = { "audiopll_divpmcck", "ethpll_divpmcck", },
	  .pp_mux_table = { 9, 10, },
	  .pp_count = 2,
	  .pp_chg_id = INT_MIN, },

	{ .n  = "i2smcc0_gclk",
	  .id = 57,
	  .r = { .max = 100000000 },
	  .pp = { "syspll_divpmcck", "audiopll_divpmcck", },
	  .pp_mux_table = { 5, 9, },
	  .pp_count = 2,
	  .pp_chg_id = 5, },

	{ .n  = "i2smcc1_gclk",
	  .id = 58,
	  .r = { .max = 100000000 },
	  .pp = { "syspll_divpmcck", "audiopll_divpmcck", },
	  .pp_mux_table = { 5, 9, },
	  .pp_count = 2,
	  .pp_chg_id = 5, },

	{ .n  = "mcan0_gclk",
	  .id = 61,
	  .r = { .max = 200000000 },
	  .pp = { "syspll_divpmcck", "baudpll_divpmcck", },
	  .pp_mux_table = { 5, 8, },
	  .pp_count = 2,
	  .pp_chg_id = INT_MIN, },

	{ .n  = "mcan1_gclk",
	  .id = 62,
	  .r = { .max = 200000000 },
	  .pp = { "syspll_divpmcck", "baudpll_divpmcck", },
	  .pp_mux_table = { 5, 8, },
	  .pp_count = 2,
	  .pp_chg_id = INT_MIN, },

	{ .n  = "mcan2_gclk",
	  .id = 63,
	  .r = { .max = 200000000 },
	  .pp = { "syspll_divpmcck", "baudpll_divpmcck", },
	  .pp_mux_table = { 5, 8, },
	  .pp_count = 2,
	  .pp_chg_id = INT_MIN, },

	{ .n  = "mcan3_gclk",
	  .id = 64,
	  .r = { .max = 200000000 },
	  .pp = { "syspll_divpmcck", "baudpll_divpmcck", },
	  .pp_mux_table = { 5, 8, },
	  .pp_count = 2,
	  .pp_chg_id = INT_MIN, },

	{ .n  = "mcan4_gclk",
	  .id = 65,
	  .r = { .max = 200000000 },
	  .pp = { "syspll_divpmcck", "baudpll_divpmcck", },
	  .pp_mux_table = { 5, 8, },
	  .pp_count = 2,
	  .pp_chg_id = INT_MIN, },

	{ .n  = "mcan5_gclk",
	  .id = 66,
	  .r = { .max = 200000000 },
	  .pp = { "syspll_divpmcck", "baudpll_divpmcck", },
	  .pp_mux_table = { 5, 8, },
	  .pp_count = 2,
	  .pp_chg_id = INT_MIN, },

	{ .n  = "pdmc0_gclk",
	  .id = 68,
	  .r = { .max = 50000000  },
	  .pp = { "syspll_divpmcck", "baudpll_divpmcck", },
	  .pp_mux_table = { 5, 8, },
	  .pp_count = 2,
	  .pp_chg_id = INT_MIN, },

	{ .n  = "pdmc1_gclk",
	  .id = 69,
	  .r = { .max = 50000000, },
	  .pp = { "syspll_divpmcck", "baudpll_divpmcck", },
	  .pp_mux_table = { 5, 8, },
	  .pp_count = 2,
	  .pp_chg_id = INT_MIN, },

	{ .n  = "pit64b0_gclk",
	  .id = 70,
	  .r = { .max = 200000000 },
	  .pp = { "syspll_divpmcck", "imgpll_divpmcck", "baudpll_divpmcck",
		  "audiopll_divpmcck", "ethpll_divpmcck", },
	  .pp_mux_table = { 5, 7, 8, 9, 10, },
	  .pp_count = 5,
	  .pp_chg_id = INT_MIN, },

	{ .n  = "pit64b1_gclk",
	  .id = 71,
	  .r = { .max = 200000000 },
	  .pp = { "syspll_divpmcck", "imgpll_divpmcck", "baudpll_divpmcck",
		  "audiopll_divpmcck", "ethpll_divpmcck", },
	  .pp_mux_table = { 5, 7, 8, 9, 10, },
	  .pp_count = 5,
	  .pp_chg_id = INT_MIN, },

	{ .n  = "pit64b2_gclk",
	  .id = 72,
	  .r = { .max = 200000000 },
	  .pp = { "syspll_divpmcck", "imgpll_divpmcck", "baudpll_divpmcck",
		  "audiopll_divpmcck", "ethpll_divpmcck", },
	  .pp_mux_table = { 5, 7, 8, 9, 10, },
	  .pp_count = 5,
	  .pp_chg_id = INT_MIN, },

	{ .n  = "pit64b3_gclk",
	  .id = 73,
	  .r = { .max = 200000000 },
	  .pp = { "syspll_divpmcck", "imgpll_divpmcck", "baudpll_divpmcck",
		  "audiopll_divpmcck", "ethpll_divpmcck", },
	  .pp_mux_table = { 5, 7, 8, 9, 10, },
	  .pp_count = 5,
	  .pp_chg_id = INT_MIN, },

	{ .n  = "pit64b4_gclk",
	  .id = 74,
	  .r = { .max = 200000000 },
	  .pp = { "syspll_divpmcck", "imgpll_divpmcck", "baudpll_divpmcck",
		  "audiopll_divpmcck", "ethpll_divpmcck", },
	  .pp_mux_table = { 5, 7, 8, 9, 10, },
	  .pp_count = 5,
	  .pp_chg_id = INT_MIN, },

	{ .n  = "pit64b5_gclk",
	  .id = 75,
	  .r = { .max = 200000000 },
	  .pp = { "syspll_divpmcck", "imgpll_divpmcck", "baudpll_divpmcck",
		  "audiopll_divpmcck", "ethpll_divpmcck", },
	  .pp_mux_table = { 5, 7, 8, 9, 10, },
	  .pp_count = 5,
	  .pp_chg_id = INT_MIN, },

	{ .n  = "qspi0_gclk",
	  .id = 78,
	  .r = { .max = 200000000 },
	  .pp = { "syspll_divpmcck", "baudpll_divpmcck", },
	  .pp_mux_table = { 5, 8, },
	  .pp_count = 2,
	  .pp_chg_id = INT_MIN, },

	{ .n  = "qspi1_gclk",
	  .id = 79,
	  .r = { .max = 200000000 },
	  .pp = { "syspll_divpmcck", "baudpll_divpmcck", },
	  .pp_mux_table = { 5, 8, },
	  .pp_count = 2,
	  .pp_chg_id = INT_MIN, },

	{ .n  = "sdmmc0_gclk",
	  .id = 80,
	  .r = { .max = 208000000 },
	  .pp = { "syspll_divpmcck", "baudpll_divpmcck", },
	  .pp_mux_table = { 5, 8, },
	  .pp_count = 2,
	  .pp_chg_id = 5, },

	{ .n  = "sdmmc1_gclk",
	  .id = 81,
	  .r = { .max = 208000000 },
	  .pp = { "syspll_divpmcck", "baudpll_divpmcck", },
	  .pp_mux_table = { 5, 8, },
	  .pp_count = 2,
	  .pp_chg_id = 5, },

	{ .n  = "sdmmc2_gclk",
	  .id = 82,
	  .r = { .max = 208000000 },
	  .pp = { "syspll_divpmcck", "baudpll_divpmcck", },
	  .pp_mux_table = { 5, 8, },
	  .pp_count = 2,
	  .pp_chg_id = 5, },

	{ .n  = "spdifrx_gclk",
	  .id = 84,
	  .r = { .max = 150000000 },
	  .pp = { "syspll_divpmcck", "audiopll_divpmcck", },
	  .pp_mux_table = { 5, 9, },
	  .pp_count = 2,
	  .pp_chg_id = 5, },

	{ .n = "spdiftx_gclk",
	  .id = 85,
	  .r = { .max = 25000000  },
	  .pp = { "syspll_divpmcck", "audiopll_divpmcck", },
	  .pp_mux_table = { 5, 9, },
	  .pp_count = 2,
	  .pp_chg_id = 5, },

	{ .n  = "tcb0_ch0_gclk",
	  .id = 88,
	  .r = { .max = 200000000 },
	  .pp = { "syspll_divpmcck", "imgpll_divpmcck", "baudpll_divpmcck",
		  "audiopll_divpmcck", "ethpll_divpmcck", },
	  .pp_mux_table = { 5, 7, 8, 9, 10, },
	  .pp_count = 5,
	  .pp_chg_id = INT_MIN, },

	{ .n  = "tcb1_ch0_gclk",
	  .id = 91,
	  .r = { .max = 200000000 },
	  .pp = { "syspll_divpmcck", "imgpll_divpmcck", "baudpll_divpmcck",
		  "audiopll_divpmcck", "ethpll_divpmcck", },
	  .pp_mux_table = { 5, 7, 8, 9, 10, },
	  .pp_count = 5,
	  .pp_chg_id = INT_MIN, },

	{ .n  = "tcpca_gclk",
	  .id = 94,
	  .r = { .max = 32768, },
	  .pp_chg_id = INT_MIN, },

	{ .n  = "tcpcb_gclk",
	  .id = 95,
	  .r = { .max = 32768, },
	  .pp_chg_id = INT_MIN, },
};

/* PLL output range. */
static const struct clk_range pll_outputs[] = {
	{ .min = 2343750, .max = 1200000000 },
};

/* PLL characteristics. */
static const struct clk_pll_characteristics pll_characteristics = {
	.input = { .min = 12000000, .max = 50000000 },
	.num_output = ARRAY_SIZE(pll_outputs),
	.output = pll_outputs,
};

/* MCK0 characteristics. */
static const struct clk_master_characteristics mck0_characteristics = {
	.output = { .min = 140000000, .max = 200000000 },
	.divisors = { 1, 2, 4, 3 },
	.have_div3_pres = 1,
};

/* MCK0 layout. */
static const struct clk_master_layout mck0_layout = {
	.mask = 0x373,
	.pres_shift = 4,
	.offset = 0x28,
};

/* Programmable clock layout. */
static const struct clk_programmable_layout programmable_layout = {
	.pres_mask = 0xff,
	.pres_shift = 8,
	.css_mask = 0x1f,
	.have_slck_mck = 0,
	.is_pres_direct = 1,
};

/* Peripheral clock layout. */
static const struct clk_pcr_layout sama7g5_pcr_layout = {
	.offset = 0x88,
	.cmd = BIT(31),
	.gckcss_mask = GENMASK(12, 8),
	.pid_mask = GENMASK(6, 0),
};

static void __init sama7g5_pmc_setup(struct device_node *np)
{
	const char *td_slck_name, *md_slck_name, *mainxtal_name;
	struct pmc_data *sama7g5_pmc;
	const char *parent_names[10];
	void **alloc_mem = NULL;
	int alloc_mem_size = 0;
	struct regmap *regmap;
	struct clk_hw *hw;
	bool bypass;
	int i, j;

	i = of_property_match_string(np, "clock-names", "td_slck");
	if (i < 0)
		return;

	td_slck_name = of_clk_get_parent_name(np, i);

	i = of_property_match_string(np, "clock-names", "md_slck");
	if (i < 0)
		return;

	md_slck_name = of_clk_get_parent_name(np, i);

	i = of_property_match_string(np, "clock-names", "main_xtal");
	if (i < 0)
		return;

	mainxtal_name = of_clk_get_parent_name(np, i);

	regmap = device_node_to_regmap(np);
	if (IS_ERR(regmap))
		return;

	sama7g5_pmc = pmc_data_allocate(PMC_I2S1_MUX + 1,
					nck(sama7g5_systemck),
					nck(sama7g5_periphck),
					nck(sama7g5_gck));
	if (!sama7g5_pmc)
		return;

	alloc_mem = kmalloc(sizeof(void *) *
			    (ARRAY_SIZE(sama7g5_mckx) + ARRAY_SIZE(sama7g5_gck)),
			    GFP_KERNEL);
	if (!alloc_mem)
		goto err_free;

	hw = at91_clk_register_main_rc_osc(regmap, "main_rc_osc", 12000000,
					   50000000);
	if (IS_ERR(hw))
		goto err_free;

	bypass = of_property_read_bool(np, "atmel,osc-bypass");

	hw = at91_clk_register_main_osc(regmap, "main_osc", mainxtal_name,
					bypass);
	if (IS_ERR(hw))
		goto err_free;

	parent_names[0] = "main_rc_osc";
	parent_names[1] = "main_osc";
	hw = at91_clk_register_sam9x5_main(regmap, "mainck", parent_names, 2);
	if (IS_ERR(hw))
		goto err_free;

	sama7g5_pmc->chws[PMC_MAIN] = hw;

	for (i = 0; i < PLL_ID_MAX; i++) {
		for (j = 0; j < 3; j++) {
			struct clk_hw *parent_hw;

			if (!sama7g5_plls[i][j].n)
				continue;

			switch (sama7g5_plls[i][j].t) {
			case PLL_TYPE_FRAC:
				if (!strcmp(sama7g5_plls[i][j].p, "mainck"))
					parent_hw = sama7g5_pmc->chws[PMC_MAIN];
				else
					parent_hw = __clk_get_hw(of_clk_get_by_name(np,
						sama7g5_plls[i][j].p));

				hw = sam9x60_clk_register_frac_pll(regmap,
					&pmc_pll_lock, sama7g5_plls[i][j].n,
					sama7g5_plls[i][j].p, parent_hw, i,
					&pll_characteristics,
					sama7g5_plls[i][j].l,
					sama7g5_plls[i][j].c);
				break;

			case PLL_TYPE_DIV:
				hw = sam9x60_clk_register_div_pll(regmap,
					&pmc_pll_lock, sama7g5_plls[i][j].n,
					sama7g5_plls[i][j].p, i,
					&pll_characteristics,
					sama7g5_plls[i][j].l,
					sama7g5_plls[i][j].c);
				break;

			default:
				continue;
			}

			if (IS_ERR(hw))
				goto err_free;

			if (sama7g5_plls[i][j].eid)
				sama7g5_pmc->chws[sama7g5_plls[i][j].eid] = hw;
		}
	}

	parent_names[0] = md_slck_name;
	parent_names[1] = "mainck";
	parent_names[2] = "cpupll_divpmcck";
	parent_names[3] = "syspll_divpmcck";
	hw = at91_clk_register_master(regmap, "mck0", 4, parent_names,
				      &mck0_layout, &mck0_characteristics);
	if (IS_ERR(hw))
		goto err_free;

	sama7g5_pmc->chws[PMC_MCK] = hw;

	parent_names[0] = md_slck_name;
	parent_names[1] = td_slck_name;
	parent_names[2] = "mainck";
	parent_names[3] = "mck0";
	for (i = 0; i < ARRAY_SIZE(sama7g5_mckx); i++) {
		u8 num_parents = 4 + sama7g5_mckx[i].ep_count;
		u32 *mux_table;

		mux_table = kmalloc_array(num_parents, sizeof(*mux_table),
					  GFP_KERNEL);
		if (!mux_table)
			goto err_free;

		SAMA7G5_INIT_TABLE(mux_table, 4);
		SAMA7G5_FILL_TABLE(&mux_table[4], sama7g5_mckx[i].ep_mux_table,
				   sama7g5_mckx[i].ep_count);
		SAMA7G5_FILL_TABLE(&parent_names[4], sama7g5_mckx[i].ep,
				   sama7g5_mckx[i].ep_count);

		hw = at91_clk_sama7g5_register_master(regmap, sama7g5_mckx[i].n,
				   num_parents, parent_names, mux_table,
				   &pmc_mckX_lock, sama7g5_mckx[i].id,
				   sama7g5_mckx[i].c,
				   sama7g5_mckx[i].ep_chg_id);
		if (IS_ERR(hw))
			goto err_free;

		alloc_mem[alloc_mem_size++] = mux_table;
	}

	hw = at91_clk_sama7g5_register_utmi(regmap, "utmick", "main_xtal");
	if (IS_ERR(hw))
		goto err_free;

	sama7g5_pmc->chws[PMC_UTMI] = hw;

	parent_names[0] = md_slck_name;
	parent_names[1] = td_slck_name;
	parent_names[2] = "mainck";
	parent_names[3] = "mck0";
	parent_names[4] = "syspll_divpmcck";
	parent_names[5] = "ddrpll_divpmcck";
	parent_names[6] = "imgpll_divpmcck";
	parent_names[7] = "baudpll_divpmcck";
	parent_names[8] = "audiopll_divpmcck";
	parent_names[9] = "ethpll_divpmcck";
	for (i = 0; i < 8; i++) {
		char name[6];

		snprintf(name, sizeof(name), "prog%d", i);

		hw = at91_clk_register_programmable(regmap, name, parent_names,
						    10, i,
						    &programmable_layout,
						    sama7g5_prog_mux_table);
		if (IS_ERR(hw))
			goto err_free;
	}

	for (i = 0; i < ARRAY_SIZE(sama7g5_systemck); i++) {
		hw = at91_clk_register_system(regmap, sama7g5_systemck[i].n,
					      sama7g5_systemck[i].p,
					      sama7g5_systemck[i].id);
		if (IS_ERR(hw))
			goto err_free;

		sama7g5_pmc->shws[sama7g5_systemck[i].id] = hw;
	}

	for (i = 0; i < ARRAY_SIZE(sama7g5_periphck); i++) {
		hw = at91_clk_register_sam9x5_peripheral(regmap, &pmc_pcr_lock,
						&sama7g5_pcr_layout,
						sama7g5_periphck[i].n,
						sama7g5_periphck[i].p,
						sama7g5_periphck[i].id,
						&sama7g5_periphck[i].r,
						sama7g5_periphck[i].chgp ? 0 :
						INT_MIN);
		if (IS_ERR(hw))
			goto err_free;

		sama7g5_pmc->phws[sama7g5_periphck[i].id] = hw;
	}

	parent_names[0] = md_slck_name;
	parent_names[1] = td_slck_name;
	parent_names[2] = "mainck";
	parent_names[3] = "mck0";
	for (i = 0; i < ARRAY_SIZE(sama7g5_gck); i++) {
		u8 num_parents = 4 + sama7g5_gck[i].pp_count;
		u32 *mux_table;

		mux_table = kmalloc_array(num_parents, sizeof(*mux_table),
					  GFP_KERNEL);
		if (!mux_table)
			goto err_free;

		SAMA7G5_INIT_TABLE(mux_table, 4);
		SAMA7G5_FILL_TABLE(&mux_table[4], sama7g5_gck[i].pp_mux_table,
				   sama7g5_gck[i].pp_count);
		SAMA7G5_FILL_TABLE(&parent_names[4], sama7g5_gck[i].pp,
				   sama7g5_gck[i].pp_count);

		hw = at91_clk_register_generated(regmap, &pmc_pcr_lock,
						 &sama7g5_pcr_layout,
						 sama7g5_gck[i].n,
						 parent_names, mux_table,
						 num_parents,
						 sama7g5_gck[i].id,
						 &sama7g5_gck[i].r,
						 sama7g5_gck[i].pp_chg_id);
		if (IS_ERR(hw))
			goto err_free;

		sama7g5_pmc->ghws[sama7g5_gck[i].id] = hw;
		alloc_mem[alloc_mem_size++] = mux_table;
	}

	of_clk_add_hw_provider(np, of_clk_hw_pmc_get, sama7g5_pmc);

	return;

err_free:
	if (alloc_mem) {
		for (i = 0; i < alloc_mem_size; i++)
			kfree(alloc_mem[i]);
		kfree(alloc_mem);
	}

	pmc_data_free(sama7g5_pmc);
}

/* Some clks are used for a clocksource */
CLK_OF_DECLARE(sama7g5_pmc, "microchip,sama7g5-pmc", sama7g5_pmc_setup);

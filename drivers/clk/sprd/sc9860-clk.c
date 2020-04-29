// SPDX-License-Identifier: GPL-2.0
//
// Spreatrum SC9860 clock driver
//
// Copyright (C) 2017 Spreadtrum, Inc.
// Author: Chunyan Zhang <chunyan.zhang@spreadtrum.com>

#include <linux/clk-provider.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include <dt-bindings/clock/sprd,sc9860-clk.h>

#include "common.h"
#include "composite.h"
#include "div.h"
#include "gate.h"
#include "mux.h"
#include "pll.h"

static CLK_FIXED_FACTOR(fac_4m,		"fac-4m",	"ext-26m",
			6, 1, 0);
static CLK_FIXED_FACTOR(fac_2m,		"fac-2m",	"ext-26m",
			13, 1, 0);
static CLK_FIXED_FACTOR(fac_1m,		"fac-1m",	"ext-26m",
			26, 1, 0);
static CLK_FIXED_FACTOR(fac_250k,	"fac-250k",	"ext-26m",
			104, 1, 0);
static CLK_FIXED_FACTOR(fac_rpll0_26m,	"rpll0-26m",	"ext-26m",
			1, 1, 0);
static CLK_FIXED_FACTOR(fac_rpll1_26m,	"rpll1-26m",	"ext-26m",
			1, 1, 0);
static CLK_FIXED_FACTOR(fac_rco_25m,	"rco-25m",	"ext-rc0-100m",
			4, 1, 0);
static CLK_FIXED_FACTOR(fac_rco_4m,	"rco-4m",	"ext-rc0-100m",
			25, 1, 0);
static CLK_FIXED_FACTOR(fac_rco_2m,	"rco-2m",	"ext-rc0-100m",
			50, 1, 0);
static CLK_FIXED_FACTOR(fac_3k2,	"fac-3k2",	"ext-32k",
			10, 1, 0);
static CLK_FIXED_FACTOR(fac_1k,		"fac-1k",	"ext-32k",
			32, 1, 0);

static SPRD_SC_GATE_CLK(mpll0_gate,	"mpll0-gate",	"ext-26m", 0xb0,
		     0x1000, BIT(2), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK(mpll1_gate,	"mpll1-gate",	"ext-26m", 0xb0,
		     0x1000, BIT(18), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK(dpll0_gate,	"dpll0-gate",	"ext-26m", 0xb4,
		     0x1000, BIT(2), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK(dpll1_gate,	"dpll1-gate",	"ext-26m", 0xb4,
		     0x1000, BIT(18), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK(ltepll0_gate,	"ltepll0-gate",	"ext-26m", 0xb8,
		     0x1000, BIT(2), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK(twpll_gate,	"twpll-gate",	"ext-26m", 0xbc,
		     0x1000, BIT(2), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK(ltepll1_gate,	"ltepll1-gate",	"ext-26m", 0x10c,
		     0x1000, BIT(2), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK(rpll0_gate,	"rpll0-gate",	"ext-26m", 0x16c,
		     0x1000, BIT(2), 0, 0);
static SPRD_SC_GATE_CLK(rpll1_gate,	"rpll1-gate",	"ext-26m", 0x16c,
		     0x1000, BIT(18), 0, 0);
static SPRD_SC_GATE_CLK(cppll_gate,	"cppll-gate",	"ext-26m", 0x2b4,
		     0x1000, BIT(2), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK(gpll_gate,	"gpll-gate",	"ext-26m", 0x32c,
		0x1000, BIT(0), CLK_IGNORE_UNUSED, CLK_GATE_SET_TO_DISABLE);

static struct sprd_clk_common *sc9860_pmu_gate_clks[] = {
	/* address base is 0x402b0000 */
	&mpll0_gate.common,
	&mpll1_gate.common,
	&dpll0_gate.common,
	&dpll1_gate.common,
	&ltepll0_gate.common,
	&twpll_gate.common,
	&ltepll1_gate.common,
	&rpll0_gate.common,
	&rpll1_gate.common,
	&cppll_gate.common,
	&gpll_gate.common,
};

static struct clk_hw_onecell_data sc9860_pmu_gate_hws = {
	.hws	= {
		[CLK_FAC_4M]		= &fac_4m.hw,
		[CLK_FAC_2M]		= &fac_2m.hw,
		[CLK_FAC_1M]		= &fac_1m.hw,
		[CLK_FAC_250K]		= &fac_250k.hw,
		[CLK_FAC_RPLL0_26M]	= &fac_rpll0_26m.hw,
		[CLK_FAC_RPLL1_26M]	= &fac_rpll1_26m.hw,
		[CLK_FAC_RCO25M]	= &fac_rco_25m.hw,
		[CLK_FAC_RCO4M]		= &fac_rco_4m.hw,
		[CLK_FAC_RCO2M]		= &fac_rco_2m.hw,
		[CLK_FAC_3K2]		= &fac_3k2.hw,
		[CLK_FAC_1K]		= &fac_1k.hw,
		[CLK_MPLL0_GATE]	= &mpll0_gate.common.hw,
		[CLK_MPLL1_GATE]	= &mpll1_gate.common.hw,
		[CLK_DPLL0_GATE]	= &dpll0_gate.common.hw,
		[CLK_DPLL1_GATE]	= &dpll1_gate.common.hw,
		[CLK_LTEPLL0_GATE]	= &ltepll0_gate.common.hw,
		[CLK_TWPLL_GATE]	= &twpll_gate.common.hw,
		[CLK_LTEPLL1_GATE]	= &ltepll1_gate.common.hw,
		[CLK_RPLL0_GATE]	= &rpll0_gate.common.hw,
		[CLK_RPLL1_GATE]	= &rpll1_gate.common.hw,
		[CLK_CPPLL_GATE]	= &cppll_gate.common.hw,
		[CLK_GPLL_GATE]		= &gpll_gate.common.hw,
	},
	.num	= CLK_PMU_GATE_NUM,
};

static const struct sprd_clk_desc sc9860_pmu_gate_desc = {
	.clk_clks	= sc9860_pmu_gate_clks,
	.num_clk_clks	= ARRAY_SIZE(sc9860_pmu_gate_clks),
	.hw_clks        = &sc9860_pmu_gate_hws,
};

/* GPLL/LPLL/DPLL/RPLL/CPLL */
static const u64 itable1[4] = {3, 780000000, 988000000, 1196000000};

/* TWPLL/MPLL0/MPLL1 */
static const u64 itable2[4] = {3, 1638000000, 2080000000, 2600000000UL};

static const struct clk_bit_field f_mpll0[PLL_FACT_MAX] = {
	{ .shift = 20,	.width = 1 },	/* lock_done	*/
	{ .shift = 19,	.width = 1 },	/* div_s	*/
	{ .shift = 18,	.width = 1 },	/* mod_en	*/
	{ .shift = 17,	.width = 1 },	/* sdm_en	*/
	{ .shift = 0,	.width = 0 },	/* refin	*/
	{ .shift = 11,	.width = 2 },	/* ibias	*/
	{ .shift = 0,	.width = 7 },	/* n		*/
	{ .shift = 57,	.width = 7 },	/* nint		*/
	{ .shift = 32,	.width = 23},	/* kint		*/
	{ .shift = 0,	.width = 0 },	/* prediv	*/
	{ .shift = 56,	.width = 1 },	/* postdiv	*/
};
static SPRD_PLL_WITH_ITABLE_K_FVCO(mpll0_clk, "mpll0", "mpll0-gate", 0x24,
				   2, itable2, f_mpll0, 200,
				   1000, 1000, 1, 1300000000);

static const struct clk_bit_field f_mpll1[PLL_FACT_MAX] = {
	{ .shift = 20,	.width = 1 },	/* lock_done	*/
	{ .shift = 19,	.width = 1 },	/* div_s	*/
	{ .shift = 18,	.width = 1 },	/* mod_en	*/
	{ .shift = 17,	.width = 1 },	/* sdm_en	*/
	{ .shift = 0,	.width = 0 },	/* refin	*/
	{ .shift = 11,	.width = 2 },	/* ibias	*/
	{ .shift = 0,	.width = 7 },	/* n		*/
	{ .shift = 57,	.width = 7 },	/* nint		*/
	{ .shift = 32,	.width = 23},	/* kint		*/
	{ .shift = 56,	.width = 1 },	/* prediv	*/
	{ .shift = 0,	.width = 0 },	/* postdiv	*/
};
static SPRD_PLL_WITH_ITABLE_1K(mpll1_clk, "mpll1", "mpll1-gate", 0x2c,
			       2, itable2, f_mpll1, 200);

static const struct clk_bit_field f_dpll[PLL_FACT_MAX] = {
	{ .shift = 16,	.width = 1 },	/* lock_done	*/
	{ .shift = 15,	.width = 1 },	/* div_s	*/
	{ .shift = 14,	.width = 1 },	/* mod_en	*/
	{ .shift = 13,	.width = 1 },	/* sdm_en	*/
	{ .shift = 0,	.width = 0 },	/* refin	*/
	{ .shift = 8,	.width = 2 },	/* ibias	*/
	{ .shift = 0,	.width = 7 },	/* n		*/
	{ .shift = 57,	.width = 7 },	/* nint		*/
	{ .shift = 32,	.width = 23},	/* kint		*/
	{ .shift = 0,	.width = 0 },	/* prediv	*/
	{ .shift = 0,	.width = 0 },	/* postdiv	*/
};
static SPRD_PLL_WITH_ITABLE_1K(dpll0_clk, "dpll0", "dpll0-gate", 0x34,
			       2, itable1, f_dpll, 200);

static SPRD_PLL_WITH_ITABLE_1K(dpll1_clk, "dpll1", "dpll1-gate", 0x3c,
			       2, itable1, f_dpll, 200);

static const struct clk_bit_field f_rpll[PLL_FACT_MAX] = {
	{ .shift = 0,	.width = 1 },	/* lock_done	*/
	{ .shift = 3,	.width = 1 },	/* div_s	*/
	{ .shift = 80,	.width = 1 },	/* mod_en	*/
	{ .shift = 81,	.width = 1 },	/* sdm_en	*/
	{ .shift = 0,	.width = 0 },	/* refin	*/
	{ .shift = 14,	.width = 2 },	/* ibias	*/
	{ .shift = 16,	.width = 7 },	/* n		*/
	{ .shift = 4,	.width = 7 },	/* nint		*/
	{ .shift = 32,	.width = 23},	/* kint		*/
	{ .shift = 0,	.width = 0 },	/* prediv	*/
	{ .shift = 0,	.width = 0 },	/* postdiv	*/
};
static SPRD_PLL_WITH_ITABLE_1K(rpll0_clk, "rpll0", "rpll0-gate", 0x44,
			       3, itable1, f_rpll, 200);

static SPRD_PLL_WITH_ITABLE_1K(rpll1_clk, "rpll1", "rpll1-gate", 0x50,
			       3, itable1, f_rpll, 200);

static const struct clk_bit_field f_twpll[PLL_FACT_MAX] = {
	{ .shift = 21,	.width = 1 },	/* lock_done	*/
	{ .shift = 20,	.width = 1 },	/* div_s	*/
	{ .shift = 19,	.width = 1 },	/* mod_en	*/
	{ .shift = 18,	.width = 1 },	/* sdm_en	*/
	{ .shift = 0,	.width = 0 },	/* refin	*/
	{ .shift = 13,	.width = 2 },	/* ibias	*/
	{ .shift = 0,	.width = 7 },	/* n		*/
	{ .shift = 57,	.width = 7 },	/* nint		*/
	{ .shift = 32,	.width = 23},	/* kint		*/
	{ .shift = 0,	.width = 0 },	/* prediv	*/
	{ .shift = 0,	.width = 0 },	/* postdiv	*/
};
static SPRD_PLL_WITH_ITABLE_1K(twpll_clk, "twpll", "twpll-gate", 0x5c,
			       2, itable2, f_twpll, 200);

static const struct clk_bit_field f_ltepll[PLL_FACT_MAX] = {
	{ .shift = 31,	.width = 1 },	/* lock_done	*/
	{ .shift = 27,	.width = 1 },	/* div_s	*/
	{ .shift = 26,	.width = 1 },	/* mod_en	*/
	{ .shift = 25,	.width = 1 },	/* sdm_en	*/
	{ .shift = 0,	.width = 0 },	/* refin	*/
	{ .shift = 20,	.width = 2 },	/* ibias	*/
	{ .shift = 0,	.width = 7 },	/* n		*/
	{ .shift = 57,	.width = 7 },	/* nint		*/
	{ .shift = 32,	.width = 23},	/* kint		*/
	{ .shift = 0,	.width = 0 },	/* prediv	*/
	{ .shift = 0,	.width = 0 },	/* postdiv	*/
};
static SPRD_PLL_WITH_ITABLE_1K(ltepll0_clk, "ltepll0", "ltepll0-gate",
			       0x64, 2, itable1,
			       f_ltepll, 200);
static SPRD_PLL_WITH_ITABLE_1K(ltepll1_clk, "ltepll1", "ltepll1-gate",
			       0x6c, 2, itable1,
			       f_ltepll, 200);

static const struct clk_bit_field f_gpll[PLL_FACT_MAX] = {
	{ .shift = 18,	.width = 1 },	/* lock_done	*/
	{ .shift = 15,	.width = 1 },	/* div_s	*/
	{ .shift = 14,	.width = 1 },	/* mod_en	*/
	{ .shift = 13,	.width = 1 },	/* sdm_en	*/
	{ .shift = 0,	.width = 0 },	/* refin	*/
	{ .shift = 8,	.width = 2 },	/* ibias	*/
	{ .shift = 0,	.width = 7 },	/* n		*/
	{ .shift = 57,	.width = 7 },	/* nint		*/
	{ .shift = 32,	.width = 23},	/* kint		*/
	{ .shift = 0,	.width = 0 },	/* prediv	*/
	{ .shift = 17,	.width = 1 },	/* postdiv	*/
};
static SPRD_PLL_WITH_ITABLE_K_FVCO(gpll_clk, "gpll", "gpll-gate", 0x9c,
				   2, itable1, f_gpll, 200,
				   1000, 1000, 1, 600000000);

static const struct clk_bit_field f_cppll[PLL_FACT_MAX] = {
	{ .shift = 17,	.width = 1 },	/* lock_done	*/
	{ .shift = 15,	.width = 1 },	/* div_s	*/
	{ .shift = 14,	.width = 1 },	/* mod_en	*/
	{ .shift = 13,	.width = 1 },	/* sdm_en	*/
	{ .shift = 0,	.width = 0 },	/* refin	*/
	{ .shift = 8,	.width = 2 },	/* ibias	*/
	{ .shift = 0,	.width = 7 },	/* n		*/
	{ .shift = 57,	.width = 7 },	/* nint		*/
	{ .shift = 32,	.width = 23},	/* kint		*/
	{ .shift = 0,	.width = 0 },	/* prediv	*/
	{ .shift = 0,	.width = 0 },	/* postdiv	*/
};
static SPRD_PLL_WITH_ITABLE_1K(cppll_clk, "cppll", "cppll-gate", 0xc4,
			       2, itable1, f_cppll, 200);

static CLK_FIXED_FACTOR(gpll_42m5, "gpll-42m5", "gpll", 20, 1, 0);
static CLK_FIXED_FACTOR(twpll_768m, "twpll-768m", "twpll", 2, 1, 0);
static CLK_FIXED_FACTOR(twpll_384m, "twpll-384m", "twpll", 4, 1, 0);
static CLK_FIXED_FACTOR(twpll_192m, "twpll-192m", "twpll", 8, 1, 0);
static CLK_FIXED_FACTOR(twpll_96m, "twpll-96m", "twpll", 16, 1, 0);
static CLK_FIXED_FACTOR(twpll_48m, "twpll-48m", "twpll", 32, 1, 0);
static CLK_FIXED_FACTOR(twpll_24m, "twpll-24m", "twpll", 64, 1, 0);
static CLK_FIXED_FACTOR(twpll_12m, "twpll-12m", "twpll", 128, 1, 0);
static CLK_FIXED_FACTOR(twpll_512m, "twpll-512m", "twpll", 3, 1, 0);
static CLK_FIXED_FACTOR(twpll_256m, "twpll-256m", "twpll", 6, 1, 0);
static CLK_FIXED_FACTOR(twpll_128m, "twpll-128m", "twpll", 12, 1, 0);
static CLK_FIXED_FACTOR(twpll_64m, "twpll-64m", "twpll", 24, 1, 0);
static CLK_FIXED_FACTOR(twpll_307m2, "twpll-307m2", "twpll", 5, 1, 0);
static CLK_FIXED_FACTOR(twpll_153m6, "twpll-153m6", "twpll", 10, 1, 0);
static CLK_FIXED_FACTOR(twpll_76m8, "twpll-76m8", "twpll", 20, 1, 0);
static CLK_FIXED_FACTOR(twpll_51m2, "twpll-51m2", "twpll", 30, 1, 0);
static CLK_FIXED_FACTOR(twpll_38m4, "twpll-38m4", "twpll", 40, 1, 0);
static CLK_FIXED_FACTOR(twpll_19m2, "twpll-19m2", "twpll", 80, 1, 0);
static CLK_FIXED_FACTOR(l0_614m4, "l0-614m4", "ltepll0", 2, 1, 0);
static CLK_FIXED_FACTOR(l0_409m6, "l0-409m6", "ltepll0", 3, 1, 0);
static CLK_FIXED_FACTOR(l0_38m, "l0-38m", "ltepll0", 32, 1, 0);
static CLK_FIXED_FACTOR(l1_38m, "l1-38m", "ltepll1", 32, 1, 0);
static CLK_FIXED_FACTOR(rpll0_192m, "rpll0-192m", "rpll0", 6, 1, 0);
static CLK_FIXED_FACTOR(rpll0_96m, "rpll0-96m", "rpll0", 12, 1, 0);
static CLK_FIXED_FACTOR(rpll0_48m, "rpll0-48m", "rpll0", 24, 1, 0);
static CLK_FIXED_FACTOR(rpll1_468m, "rpll1-468m", "rpll1", 2, 1, 0);
static CLK_FIXED_FACTOR(rpll1_192m, "rpll1-192m", "rpll1", 6, 1, 0);
static CLK_FIXED_FACTOR(rpll1_96m, "rpll1-96m", "rpll1", 12, 1, 0);
static CLK_FIXED_FACTOR(rpll1_64m, "rpll1-64m", "rpll1", 18, 1, 0);
static CLK_FIXED_FACTOR(rpll1_48m, "rpll1-48m", "rpll1", 24, 1, 0);
static CLK_FIXED_FACTOR(dpll0_50m, "dpll0-50m", "dpll0", 16, 1, 0);
static CLK_FIXED_FACTOR(dpll1_50m, "dpll1-50m", "dpll1", 16, 1, 0);
static CLK_FIXED_FACTOR(cppll_50m, "cppll-50m", "cppll", 18, 1, 0);
static CLK_FIXED_FACTOR(m0_39m, "m0-39m", "mpll0", 32, 1, 0);
static CLK_FIXED_FACTOR(m1_63m, "m1-63m", "mpll1", 32, 1, 0);

static struct sprd_clk_common *sc9860_pll_clks[] = {
	/* address base is 0x40400000 */
	&mpll0_clk.common,
	&mpll1_clk.common,
	&dpll0_clk.common,
	&dpll1_clk.common,
	&rpll0_clk.common,
	&rpll1_clk.common,
	&twpll_clk.common,
	&ltepll0_clk.common,
	&ltepll1_clk.common,
	&gpll_clk.common,
	&cppll_clk.common,
};

static struct clk_hw_onecell_data sc9860_pll_hws = {
	.hws	= {
		[CLK_MPLL0]		= &mpll0_clk.common.hw,
		[CLK_MPLL1]		= &mpll1_clk.common.hw,
		[CLK_DPLL0]		= &dpll0_clk.common.hw,
		[CLK_DPLL1]		= &dpll1_clk.common.hw,
		[CLK_RPLL0]		= &rpll0_clk.common.hw,
		[CLK_RPLL1]		= &rpll1_clk.common.hw,
		[CLK_TWPLL]		= &twpll_clk.common.hw,
		[CLK_LTEPLL0]		= &ltepll0_clk.common.hw,
		[CLK_LTEPLL1]		= &ltepll1_clk.common.hw,
		[CLK_GPLL]		= &gpll_clk.common.hw,
		[CLK_CPPLL]		= &cppll_clk.common.hw,
		[CLK_GPLL_42M5]		= &gpll_42m5.hw,
		[CLK_TWPLL_768M]	= &twpll_768m.hw,
		[CLK_TWPLL_384M]	= &twpll_384m.hw,
		[CLK_TWPLL_192M]	= &twpll_192m.hw,
		[CLK_TWPLL_96M]		= &twpll_96m.hw,
		[CLK_TWPLL_48M]		= &twpll_48m.hw,
		[CLK_TWPLL_24M]		= &twpll_24m.hw,
		[CLK_TWPLL_12M]		= &twpll_12m.hw,
		[CLK_TWPLL_512M]	= &twpll_512m.hw,
		[CLK_TWPLL_256M]	= &twpll_256m.hw,
		[CLK_TWPLL_128M]	= &twpll_128m.hw,
		[CLK_TWPLL_64M]		= &twpll_64m.hw,
		[CLK_TWPLL_307M2]	= &twpll_307m2.hw,
		[CLK_TWPLL_153M6]	= &twpll_153m6.hw,
		[CLK_TWPLL_76M8]	= &twpll_76m8.hw,
		[CLK_TWPLL_51M2]	= &twpll_51m2.hw,
		[CLK_TWPLL_38M4]	= &twpll_38m4.hw,
		[CLK_TWPLL_19M2]	= &twpll_19m2.hw,
		[CLK_L0_614M4]		= &l0_614m4.hw,
		[CLK_L0_409M6]		= &l0_409m6.hw,
		[CLK_L0_38M]		= &l0_38m.hw,
		[CLK_L1_38M]		= &l1_38m.hw,
		[CLK_RPLL0_192M]	= &rpll0_192m.hw,
		[CLK_RPLL0_96M]		= &rpll0_96m.hw,
		[CLK_RPLL0_48M]		= &rpll0_48m.hw,
		[CLK_RPLL1_468M]	= &rpll1_468m.hw,
		[CLK_RPLL1_192M]	= &rpll1_192m.hw,
		[CLK_RPLL1_96M]		= &rpll1_96m.hw,
		[CLK_RPLL1_64M]		= &rpll1_64m.hw,
		[CLK_RPLL1_48M]		= &rpll1_48m.hw,
		[CLK_DPLL0_50M]		= &dpll0_50m.hw,
		[CLK_DPLL1_50M]		= &dpll1_50m.hw,
		[CLK_CPPLL_50M]		= &cppll_50m.hw,
		[CLK_M0_39M]		= &m0_39m.hw,
		[CLK_M1_63M]		= &m1_63m.hw,
	},
	.num	= CLK_PLL_NUM,
};

static const struct sprd_clk_desc sc9860_pll_desc = {
	.clk_clks	= sc9860_pll_clks,
	.num_clk_clks	= ARRAY_SIZE(sc9860_pll_clks),
	.hw_clks	= &sc9860_pll_hws,
};

#define SC9860_MUX_FLAG	\
	(CLK_GET_RATE_NOCACHE | CLK_SET_RATE_NO_REPARENT)

static const char * const ap_apb_parents[] = { "ext-26m", "twpll-64m",
					       "twpll-96m", "twpll-128m" };
static SPRD_MUX_CLK(ap_apb, "ap-apb", ap_apb_parents,
		    0x20, 0, 1, SC9860_MUX_FLAG);

static const char * const ap_apb_usb3[] = { "ext-32k", "twpll-24m" };
static SPRD_MUX_CLK(ap_usb3, "ap-usb3", ap_apb_usb3,
		    0x2c, 0, 1, SC9860_MUX_FLAG);

static const char * const uart_parents[] = {	"ext-26m",	"twpll-48m",
						"twpll-51m2",	"twpll-96m" };
static SPRD_COMP_CLK(uart0_clk,	"uart0",	uart_parents, 0x30,
		     0, 2, 8, 3, 0);
static SPRD_COMP_CLK(uart1_clk,	"uart1",	uart_parents, 0x34,
		     0, 2, 8, 3, 0);
static SPRD_COMP_CLK(uart2_clk,	"uart2",	uart_parents, 0x38,
		     0, 2, 8, 3, 0);
static SPRD_COMP_CLK(uart3_clk,	"uart3",	uart_parents, 0x3c,
		     0, 2, 8, 3, 0);
static SPRD_COMP_CLK(uart4_clk,	"uart4",	uart_parents, 0x40,
		     0, 2, 8, 3, 0);

static const char * const i2c_parents[] = { "ext-26m", "twpll-48m",
					    "twpll-51m2", "twpll-153m6" };
static SPRD_COMP_CLK(i2c0_clk,	"i2c0", i2c_parents, 0x44,
		     0, 2, 8, 3, 0);
static SPRD_COMP_CLK(i2c1_clk,	"i2c1", i2c_parents, 0x48,
		     0, 2, 8, 3, 0);
static SPRD_COMP_CLK(i2c2_clk,	"i2c2", i2c_parents, 0x4c,
		     0, 2, 8, 3, 0);
static SPRD_COMP_CLK(i2c3_clk,	"i2c3", i2c_parents, 0x50,
		     0, 2, 8, 3, 0);
static SPRD_COMP_CLK(i2c4_clk,	"i2c4", i2c_parents, 0x54,
		     0, 2, 8, 3, 0);
static SPRD_COMP_CLK(i2c5_clk,	"i2c5", i2c_parents, 0x58,
		     0, 2, 8, 3, 0);

static const char * const spi_parents[] = {	"ext-26m",	"twpll-128m",
						"twpll-153m6",	"twpll-192m" };
static SPRD_COMP_CLK(spi0_clk,	"spi0",	spi_parents, 0x5c,
		     0, 2, 8, 3, 0);
static SPRD_COMP_CLK(spi1_clk,	"spi1",	spi_parents, 0x60,
		     0, 2, 8, 3, 0);
static SPRD_COMP_CLK(spi2_clk,	"spi2",	spi_parents, 0x64,
		     0, 2, 8, 3, 0);
static SPRD_COMP_CLK(spi3_clk,	"spi3",	spi_parents, 0x68,
		     0, 2, 8, 3, 0);

static const char * const iis_parents[] = { "ext-26m",
					    "twpll-128m",
					    "twpll-153m6" };
static SPRD_COMP_CLK(iis0_clk,	"iis0",	iis_parents, 0x6c,
		     0, 2, 8, 6, 0);
static SPRD_COMP_CLK(iis1_clk,	"iis1",	iis_parents, 0x70,
		     0, 2, 8, 6, 0);
static SPRD_COMP_CLK(iis2_clk,	"iis2",	iis_parents, 0x74,
		     0, 2, 8, 6, 0);
static SPRD_COMP_CLK(iis3_clk,	"iis3",	iis_parents, 0x78,
		     0, 2, 8, 6, 0);

static struct sprd_clk_common *sc9860_ap_clks[] = {
	/* address base is 0x20000000 */
	&ap_apb.common,
	&ap_usb3.common,
	&uart0_clk.common,
	&uart1_clk.common,
	&uart2_clk.common,
	&uart3_clk.common,
	&uart4_clk.common,
	&i2c0_clk.common,
	&i2c1_clk.common,
	&i2c2_clk.common,
	&i2c3_clk.common,
	&i2c4_clk.common,
	&i2c5_clk.common,
	&spi0_clk.common,
	&spi1_clk.common,
	&spi2_clk.common,
	&spi3_clk.common,
	&iis0_clk.common,
	&iis1_clk.common,
	&iis2_clk.common,
	&iis3_clk.common,
};

static struct clk_hw_onecell_data sc9860_ap_clk_hws = {
	.hws	= {
		[CLK_AP_APB]	= &ap_apb.common.hw,
		[CLK_AP_USB3]	= &ap_usb3.common.hw,
		[CLK_UART0]	= &uart0_clk.common.hw,
		[CLK_UART1]	= &uart1_clk.common.hw,
		[CLK_UART2]	= &uart2_clk.common.hw,
		[CLK_UART3]	= &uart3_clk.common.hw,
		[CLK_UART4]	= &uart4_clk.common.hw,
		[CLK_I2C0]	= &i2c0_clk.common.hw,
		[CLK_I2C1]	= &i2c1_clk.common.hw,
		[CLK_I2C2]	= &i2c2_clk.common.hw,
		[CLK_I2C3]	= &i2c3_clk.common.hw,
		[CLK_I2C4]	= &i2c4_clk.common.hw,
		[CLK_I2C5]	= &i2c5_clk.common.hw,
		[CLK_SPI0]	= &spi0_clk.common.hw,
		[CLK_SPI1]	= &spi1_clk.common.hw,
		[CLK_SPI2]	= &spi2_clk.common.hw,
		[CLK_SPI3]	= &spi3_clk.common.hw,
		[CLK_IIS0]	= &iis0_clk.common.hw,
		[CLK_IIS1]	= &iis1_clk.common.hw,
		[CLK_IIS2]	= &iis2_clk.common.hw,
		[CLK_IIS3]	= &iis3_clk.common.hw,
	},
	.num	= CLK_AP_CLK_NUM,
};

static const struct sprd_clk_desc sc9860_ap_clk_desc = {
	.clk_clks	= sc9860_ap_clks,
	.num_clk_clks	= ARRAY_SIZE(sc9860_ap_clks),
	.hw_clks	= &sc9860_ap_clk_hws,
};

static const char * const aon_apb_parents[] = { "rco-25m",	"ext-26m",
						"ext-rco-100m",	"twpll-96m",
						"twpll-128m",
						"twpll-153m6" };
static SPRD_COMP_CLK(aon_apb, "aon-apb", aon_apb_parents, 0x230,
		     0, 3, 8, 2, 0);

static const char * const aux_parents[] = { "ext-32k",		"rpll0-26m",
					    "rpll1-26m",	"ext-26m",
					    "cppll-50m",	"rco-25m",
					    "dpll0-50m",	"dpll1-50m",
					    "gpll-42m5",	"twpll-48m",
					    "m0-39m",		"m1-63m",
					    "l0-38m",		"l1-38m" };

static SPRD_COMP_CLK(aux0_clk,	"aux0",		aux_parents, 0x238,
		     0, 5, 8, 4, 0);
static SPRD_COMP_CLK(aux1_clk,	"aux1",		aux_parents, 0x23c,
		     0, 5, 8, 4, 0);
static SPRD_COMP_CLK(aux2_clk,	"aux2",		aux_parents, 0x240,
		     0, 5, 8, 4, 0);
static SPRD_COMP_CLK(probe_clk,	"probe",	aux_parents, 0x244,
		     0, 5, 8, 4, 0);

static const char * const sp_ahb_parents[] = {	"rco-4m",	"ext-26m",
						"ext-rco-100m",	"twpll-96m",
						"twpll-128m",
						"twpll-153m6" };
static SPRD_COMP_CLK(sp_ahb,	"sp-ahb",	sp_ahb_parents, 0x2d0,
		     0, 3, 8, 2, 0);

static const char * const cci_parents[] = {	"ext-26m",	"twpll-384m",
						"l0-614m4",	"twpll-768m" };
static SPRD_COMP_CLK(cci_clk,	"cci",		cci_parents, 0x300,
		     0, 2, 8, 2, 0);
static SPRD_COMP_CLK(gic_clk,	"gic",		cci_parents, 0x304,
		     0, 2, 8, 2, 0);
static SPRD_COMP_CLK(cssys_clk,	"cssys",	cci_parents, 0x310,
		     0, 2, 8, 2, 0);

static const char * const sdio_2x_parents[] = {	"fac-1m",	"ext-26m",
						"twpll-307m2",	"twpll-384m",
						"l0-409m6" };
static SPRD_COMP_CLK(sdio0_2x,	"sdio0-2x",	sdio_2x_parents, 0x328,
		     0, 3, 8, 4, 0);
static SPRD_COMP_CLK(sdio1_2x,	"sdio1-2x",	sdio_2x_parents, 0x330,
		     0, 3, 8, 4, 0);
static SPRD_COMP_CLK(sdio2_2x,	"sdio2-2x",	sdio_2x_parents, 0x338,
		     0, 3, 8, 4, 0);
static SPRD_COMP_CLK(emmc_2x,	"emmc-2x",	sdio_2x_parents, 0x340,
		     0, 3, 8, 4, 0);

static SPRD_DIV_CLK(sdio0_1x,	"sdio0-1x",	"sdio0-2x",	0x32c,
		    8, 1, 0);
static SPRD_DIV_CLK(sdio1_1x,	"sdio1-1x",	"sdio1-2x",	0x334,
		    8, 1, 0);
static SPRD_DIV_CLK(sdio2_1x,	"sdio2-1x",	"sdio2-2x",	0x33c,
		    8, 1, 0);
static SPRD_DIV_CLK(emmc_1x,	"emmc-1x",	"emmc-2x",	0x344,
		    8, 1, 0);

static const char * const adi_parents[] = {	"rco-4m",	"ext-26m",
						"rco-25m",	"twpll-38m4",
						"twpll-51m2" };
static SPRD_MUX_CLK(adi_clk,	"adi",	adi_parents, 0x234,
		    0, 3, SC9860_MUX_FLAG);

static const char * const pwm_parents[] = {	"ext-32k",	"ext-26m",
						"rco-4m",	"rco-25m",
						"twpll-48m" };
static SPRD_MUX_CLK(pwm0_clk,	"pwm0",	pwm_parents, 0x248,
		    0, 3, SC9860_MUX_FLAG);
static SPRD_MUX_CLK(pwm1_clk,	"pwm1",	pwm_parents, 0x24c,
		    0, 3, SC9860_MUX_FLAG);
static SPRD_MUX_CLK(pwm2_clk,	"pwm2",	pwm_parents, 0x250,
		    0, 3, SC9860_MUX_FLAG);
static SPRD_MUX_CLK(pwm3_clk,	"pwm3",	pwm_parents, 0x254,
		    0, 3, SC9860_MUX_FLAG);

static const char * const efuse_parents[] = { "rco-25m", "ext-26m" };
static SPRD_MUX_CLK(efuse_clk, "efuse", efuse_parents, 0x258,
		    0, 1, SC9860_MUX_FLAG);

static const char * const cm3_uart_parents[] = { "rco-4m",	"ext-26m",
						 "rco-100m",	"twpll-48m",
						 "twpll-51m2",	"twpll-96m",
						 "twpll-128m" };
static SPRD_MUX_CLK(cm3_uart0, "cm3-uart0", cm3_uart_parents, 0x25c,
		    0, 3, SC9860_MUX_FLAG);
static SPRD_MUX_CLK(cm3_uart1, "cm3-uart1", cm3_uart_parents, 0x260,
		    0, 3, SC9860_MUX_FLAG);

static const char * const thm_parents[] = { "ext-32k", "fac-250k" };
static SPRD_MUX_CLK(thm_clk,	"thm",	thm_parents, 0x270,
		    0, 1, SC9860_MUX_FLAG);

static const char * const cm3_i2c_parents[] = {	"rco-4m",
						"ext-26m",
						"rco-100m",
						"twpll-48m",
						"twpll-51m2",
						"twpll-153m6" };
static SPRD_MUX_CLK(cm3_i2c0, "cm3-i2c0", cm3_i2c_parents, 0x274,
		    0, 3, SC9860_MUX_FLAG);
static SPRD_MUX_CLK(cm3_i2c1, "cm3-i2c1", cm3_i2c_parents, 0x278,
		    0, 3, SC9860_MUX_FLAG);
static SPRD_MUX_CLK(aon_i2c, "aon-i2c",	cm3_i2c_parents, 0x280,
		    0, 3, SC9860_MUX_FLAG);

static const char * const cm4_spi_parents[] = {	"ext-26m",	"twpll-96m",
						"rco-100m",	"twpll-128m",
						"twpll-153m6",	"twpll-192m" };
static SPRD_MUX_CLK(cm4_spi, "cm4-spi", cm4_spi_parents, 0x27c,
		    0, 3, SC9860_MUX_FLAG);

static SPRD_MUX_CLK(avs_clk, "avs", uart_parents, 0x284,
		    0, 2, SC9860_MUX_FLAG);

static const char * const ca53_dap_parents[] = { "ext-26m",	"rco-4m",
						 "rco-100m",	"twpll-76m8",
						 "twpll-128m",	"twpll-153m6" };
static SPRD_MUX_CLK(ca53_dap, "ca53-dap", ca53_dap_parents, 0x288,
		    0, 3, SC9860_MUX_FLAG);

static const char * const ca53_ts_parents[] = {	"ext-32k", "ext-26m",
						"clk-twpll-128m",
						"clk-twpll-153m6" };
static SPRD_MUX_CLK(ca53_ts, "ca53-ts", ca53_ts_parents, 0x290,
		    0, 2, SC9860_MUX_FLAG);

static const char * const djtag_tck_parents[] = { "rco-4m", "ext-26m" };
static SPRD_MUX_CLK(djtag_tck, "djtag-tck", djtag_tck_parents, 0x2c8,
		    0, 1, SC9860_MUX_FLAG);

static const char * const pmu_parents[] = { "ext-32k", "rco-4m", "clk-4m" };
static SPRD_MUX_CLK(pmu_clk, "pmu", pmu_parents, 0x2e0,
		    0, 2, SC9860_MUX_FLAG);

static const char * const pmu_26m_parents[] = { "rco-25m", "ext-26m" };
static SPRD_MUX_CLK(pmu_26m, "pmu-26m", pmu_26m_parents, 0x2e4,
		    0, 1, SC9860_MUX_FLAG);

static const char * const debounce_parents[] = { "ext-32k", "rco-4m",
						 "rco-25m", "ext-26m" };
static SPRD_MUX_CLK(debounce_clk, "debounce", debounce_parents, 0x2e8,
		    0, 2, SC9860_MUX_FLAG);

static const char * const otg2_ref_parents[] = { "twpll-12m", "twpll-24m" };
static SPRD_MUX_CLK(otg2_ref, "otg2-ref", otg2_ref_parents, 0x2f4,
		    0, 1, SC9860_MUX_FLAG);

static const char * const usb3_ref_parents[] = { "twpll-24m", "twpll-19m2",
						 "twpll-48m" };
static SPRD_MUX_CLK(usb3_ref, "usb3-ref", usb3_ref_parents, 0x2f8,
		    0, 2, SC9860_MUX_FLAG);

static const char * const ap_axi_parents[] = { "ext-26m", "twpll-76m8",
					       "twpll-128m", "twpll-256m" };
static SPRD_MUX_CLK(ap_axi, "ap-axi", ap_axi_parents, 0x324,
		    0, 2, SC9860_MUX_FLAG);

static struct sprd_clk_common *sc9860_aon_prediv[] = {
	/* address base is 0x402d0000 */
	&aon_apb.common,
	&aux0_clk.common,
	&aux1_clk.common,
	&aux2_clk.common,
	&probe_clk.common,
	&sp_ahb.common,
	&cci_clk.common,
	&gic_clk.common,
	&cssys_clk.common,
	&sdio0_2x.common,
	&sdio1_2x.common,
	&sdio2_2x.common,
	&emmc_2x.common,
	&sdio0_1x.common,
	&sdio1_1x.common,
	&sdio2_1x.common,
	&emmc_1x.common,
	&adi_clk.common,
	&pwm0_clk.common,
	&pwm1_clk.common,
	&pwm2_clk.common,
	&pwm3_clk.common,
	&efuse_clk.common,
	&cm3_uart0.common,
	&cm3_uart1.common,
	&thm_clk.common,
	&cm3_i2c0.common,
	&cm3_i2c1.common,
	&cm4_spi.common,
	&aon_i2c.common,
	&avs_clk.common,
	&ca53_dap.common,
	&ca53_ts.common,
	&djtag_tck.common,
	&pmu_clk.common,
	&pmu_26m.common,
	&debounce_clk.common,
	&otg2_ref.common,
	&usb3_ref.common,
	&ap_axi.common,
};

static struct clk_hw_onecell_data sc9860_aon_prediv_hws = {
	.hws	= {
		[CLK_AON_APB]		= &aon_apb.common.hw,
		[CLK_AUX0]		= &aux0_clk.common.hw,
		[CLK_AUX1]		= &aux1_clk.common.hw,
		[CLK_AUX2]		= &aux2_clk.common.hw,
		[CLK_PROBE]		= &probe_clk.common.hw,
		[CLK_SP_AHB]		= &sp_ahb.common.hw,
		[CLK_CCI]		= &cci_clk.common.hw,
		[CLK_GIC]		= &gic_clk.common.hw,
		[CLK_CSSYS]		= &cssys_clk.common.hw,
		[CLK_SDIO0_2X]		= &sdio0_2x.common.hw,
		[CLK_SDIO1_2X]		= &sdio1_2x.common.hw,
		[CLK_SDIO2_2X]		= &sdio2_2x.common.hw,
		[CLK_EMMC_2X]		= &emmc_2x.common.hw,
		[CLK_SDIO0_1X]		= &sdio0_1x.common.hw,
		[CLK_SDIO1_1X]		= &sdio1_1x.common.hw,
		[CLK_SDIO2_1X]		= &sdio2_1x.common.hw,
		[CLK_EMMC_1X]		= &emmc_1x.common.hw,
		[CLK_ADI]		= &adi_clk.common.hw,
		[CLK_PWM0]		= &pwm0_clk.common.hw,
		[CLK_PWM1]		= &pwm1_clk.common.hw,
		[CLK_PWM2]		= &pwm2_clk.common.hw,
		[CLK_PWM3]		= &pwm3_clk.common.hw,
		[CLK_EFUSE]		= &efuse_clk.common.hw,
		[CLK_CM3_UART0]		= &cm3_uart0.common.hw,
		[CLK_CM3_UART1]		= &cm3_uart1.common.hw,
		[CLK_THM]		= &thm_clk.common.hw,
		[CLK_CM3_I2C0]		= &cm3_i2c0.common.hw,
		[CLK_CM3_I2C1]		= &cm3_i2c1.common.hw,
		[CLK_CM4_SPI]		= &cm4_spi.common.hw,
		[CLK_AON_I2C]		= &aon_i2c.common.hw,
		[CLK_AVS]		= &avs_clk.common.hw,
		[CLK_CA53_DAP]		= &ca53_dap.common.hw,
		[CLK_CA53_TS]		= &ca53_ts.common.hw,
		[CLK_DJTAG_TCK]		= &djtag_tck.common.hw,
		[CLK_PMU]		= &pmu_clk.common.hw,
		[CLK_PMU_26M]		= &pmu_26m.common.hw,
		[CLK_DEBOUNCE]		= &debounce_clk.common.hw,
		[CLK_OTG2_REF]		= &otg2_ref.common.hw,
		[CLK_USB3_REF]		= &usb3_ref.common.hw,
		[CLK_AP_AXI]		= &ap_axi.common.hw,
	},
	.num	= CLK_AON_PREDIV_NUM,
};

static const struct sprd_clk_desc sc9860_aon_prediv_desc = {
	.clk_clks	= sc9860_aon_prediv,
	.num_clk_clks	= ARRAY_SIZE(sc9860_aon_prediv),
	.hw_clks	= &sc9860_aon_prediv_hws,
};

static SPRD_SC_GATE_CLK(usb3_eb,		"usb3-eb",	"ap-axi", 0x0,
		     0x1000, BIT(2), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK(usb3_suspend,	"usb3-suspend", "ap-axi", 0x0,
		     0x1000, BIT(3), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK(usb3_ref_eb,	"usb3-ref-eb",	"ap-axi", 0x0,
		     0x1000, BIT(4), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK(dma_eb,		"dma-eb",	"ap-axi", 0x0,
		     0x1000, BIT(5), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK(sdio0_eb,		"sdio0-eb",	"ap-axi", 0x0,
		     0x1000, BIT(7), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK(sdio1_eb,		"sdio1-eb",	"ap-axi", 0x0,
		     0x1000, BIT(8), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK(sdio2_eb,		"sdio2-eb",	"ap-axi", 0x0,
		     0x1000, BIT(9), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK(emmc_eb,		"emmc-eb",	"ap-axi", 0x0,
		     0x1000, BIT(10), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK(rom_eb,		"rom-eb",	"ap-axi", 0x0,
		     0x1000, BIT(12), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK(busmon_eb,		"busmon-eb",	"ap-axi", 0x0,
		     0x1000, BIT(13), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK(cc63s_eb,		"cc63s-eb",	"ap-axi", 0x0,
		     0x1000, BIT(22), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK(cc63p_eb,		"cc63p-eb",	"ap-axi", 0x0,
		     0x1000, BIT(23), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK(ce0_eb,		"ce0-eb",	"ap-axi", 0x0,
		     0x1000, BIT(24), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK(ce1_eb,		"ce1-eb",	"ap-axi", 0x0,
		     0x1000, BIT(25), CLK_IGNORE_UNUSED, 0);

static struct sprd_clk_common *sc9860_apahb_gate[] = {
	/* address base is 0x20210000 */
	&usb3_eb.common,
	&usb3_suspend.common,
	&usb3_ref_eb.common,
	&dma_eb.common,
	&sdio0_eb.common,
	&sdio1_eb.common,
	&sdio2_eb.common,
	&emmc_eb.common,
	&rom_eb.common,
	&busmon_eb.common,
	&cc63s_eb.common,
	&cc63p_eb.common,
	&ce0_eb.common,
	&ce1_eb.common,
};

static struct clk_hw_onecell_data sc9860_apahb_gate_hws = {
	.hws	= {
		[CLK_USB3_EB]		= &usb3_eb.common.hw,
		[CLK_USB3_SUSPEND_EB]	= &usb3_suspend.common.hw,
		[CLK_USB3_REF_EB]	= &usb3_ref_eb.common.hw,
		[CLK_DMA_EB]		= &dma_eb.common.hw,
		[CLK_SDIO0_EB]		= &sdio0_eb.common.hw,
		[CLK_SDIO1_EB]		= &sdio1_eb.common.hw,
		[CLK_SDIO2_EB]		= &sdio2_eb.common.hw,
		[CLK_EMMC_EB]		= &emmc_eb.common.hw,
		[CLK_ROM_EB]		= &rom_eb.common.hw,
		[CLK_BUSMON_EB]		= &busmon_eb.common.hw,
		[CLK_CC63S_EB]		= &cc63s_eb.common.hw,
		[CLK_CC63P_EB]		= &cc63p_eb.common.hw,
		[CLK_CE0_EB]		= &ce0_eb.common.hw,
		[CLK_CE1_EB]		= &ce1_eb.common.hw,
	},
	.num	= CLK_APAHB_GATE_NUM,
};

static const struct sprd_clk_desc sc9860_apahb_gate_desc = {
	.clk_clks	= sc9860_apahb_gate,
	.num_clk_clks	= ARRAY_SIZE(sc9860_apahb_gate),
	.hw_clks	= &sc9860_apahb_gate_hws,
};

static SPRD_SC_GATE_CLK(avs_lit_eb,	"avs-lit-eb",	"aon-apb", 0x0,
		     0x1000, BIT(0), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK(avs_big_eb,	"avs-big-eb",	"aon-apb", 0x0,
		     0x1000, BIT(1), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK(ap_intc5_eb,	"ap-intc5-eb",	"aon-apb", 0x0,
		     0x1000, BIT(2), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK(gpio_eb,		"gpio-eb",	"aon-apb", 0x0,
		     0x1000, BIT(3), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK(pwm0_eb,		"pwm0-eb",	"aon-apb", 0x0,
		     0x1000, BIT(4), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK(pwm1_eb,		"pwm1-eb",	"aon-apb", 0x0,
		     0x1000, BIT(5), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK(pwm2_eb,		"pwm2-eb",	"aon-apb", 0x0,
		     0x1000, BIT(6), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK(pwm3_eb,		"pwm3-eb",	"aon-apb", 0x0,
		     0x1000, BIT(7), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK(kpd_eb,		"kpd-eb",	"aon-apb", 0x0,
		     0x1000, BIT(8), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK(aon_sys_eb,	"aon-sys-eb",	"aon-apb", 0x0,
		     0x1000, BIT(9), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK(ap_sys_eb,	"ap-sys-eb",	"aon-apb", 0x0,
		     0x1000, BIT(10), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK(aon_tmr_eb,	"aon-tmr-eb",	"aon-apb", 0x0,
		     0x1000, BIT(11), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK(ap_tmr0_eb,	"ap-tmr0-eb",	"aon-apb", 0x0,
		     0x1000, BIT(12), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK(efuse_eb,	"efuse-eb",	"aon-apb", 0x0,
		     0x1000, BIT(13), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK(eic_eb,		"eic-eb",	"aon-apb", 0x0,
		     0x1000, BIT(14), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK(pub1_reg_eb,	"pub1-reg-eb",	"aon-apb", 0x0,
		     0x1000, BIT(15), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK(adi_eb,		"adi-eb",	"aon-apb", 0x0,
		     0x1000, BIT(16), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK(ap_intc0_eb,	"ap-intc0-eb",	"aon-apb", 0x0,
		     0x1000, BIT(17), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK(ap_intc1_eb,	"ap-intc1-eb",	"aon-apb", 0x0,
		     0x1000, BIT(18), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK(ap_intc2_eb,	"ap-intc2-eb",	"aon-apb", 0x0,
		     0x1000, BIT(19), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK(ap_intc3_eb,	"ap-intc3-eb",	"aon-apb", 0x0,
		     0x1000, BIT(20), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK(ap_intc4_eb,	"ap-intc4-eb",	"aon-apb", 0x0,
		     0x1000, BIT(21), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK(splk_eb,		"splk-eb",	"aon-apb", 0x0,
		     0x1000, BIT(22), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK(mspi_eb,		"mspi-eb",	"aon-apb", 0x0,
		     0x1000, BIT(23), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK(pub0_reg_eb,	"pub0-reg-eb",	"aon-apb", 0x0,
		     0x1000, BIT(24), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK(pin_eb,		"pin-eb",	"aon-apb", 0x0,
		     0x1000, BIT(25), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK(aon_ckg_eb,	"aon-ckg-eb",	"aon-apb", 0x0,
		     0x1000, BIT(26), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK(gpu_eb,		"gpu-eb",	"aon-apb", 0x0,
		     0x1000, BIT(27), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK(apcpu_ts0_eb,	"apcpu-ts0-eb",	"aon-apb", 0x0,
		     0x1000, BIT(28), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK(apcpu_ts1_eb,	"apcpu-ts1-eb",	"aon-apb", 0x0,
		     0x1000, BIT(29), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK(dap_eb,		"dap-eb",	"aon-apb", 0x0,
		     0x1000, BIT(30), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK(i2c_eb,		"i2c-eb",	"aon-apb", 0x0,
		     0x1000, BIT(31), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK(pmu_eb,		"pmu-eb",	"aon-apb", 0x4,
		     0x1000, BIT(0), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK(thm_eb,		"thm-eb",	"aon-apb", 0x4,
		     0x1000, BIT(1), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK(aux0_eb,		"aux0-eb",	"aon-apb", 0x4,
		     0x1000, BIT(2), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK(aux1_eb,		"aux1-eb",	"aon-apb", 0x4,
		     0x1000, BIT(3), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK(aux2_eb,		"aux2-eb",	"aon-apb", 0x4,
		     0x1000, BIT(4), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK(probe_eb,		"probe-eb",	"aon-apb", 0x4,
		     0x1000, BIT(5), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK(gpu0_avs_eb,	"gpu0-avs-eb",	"aon-apb", 0x4,
		     0x1000, BIT(6), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK(gpu1_avs_eb,	"gpu1-avs-eb",	"aon-apb", 0x4,
		     0x1000, BIT(7), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK(apcpu_wdg_eb,	"apcpu-wdg-eb",	"aon-apb", 0x4,
		     0x1000, BIT(8), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK(ap_tmr1_eb,	"ap-tmr1-eb",	"aon-apb", 0x4,
		     0x1000, BIT(9), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK(ap_tmr2_eb,	"ap-tmr2-eb",	"aon-apb", 0x4,
		     0x1000, BIT(10), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK(disp_emc_eb,	"disp-emc-eb",	"aon-apb", 0x4,
		     0x1000, BIT(11), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK(zip_emc_eb,	"zip-emc-eb",	"aon-apb", 0x4,
		     0x1000, BIT(12), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK(gsp_emc_eb,	"gsp-emc-eb",	"aon-apb", 0x4,
		     0x1000, BIT(13), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK(osc_aon_eb,	"osc-aon-eb",	"aon-apb", 0x4,
		     0x1000, BIT(14), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK(lvds_trx_eb,	"lvds-trx-eb",	"aon-apb", 0x4,
		     0x1000, BIT(15), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK(lvds_tcxo_eb,	"lvds-tcxo-eb",	"aon-apb", 0x4,
		     0x1000, BIT(16), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK(mdar_eb,		"mdar-eb",	"aon-apb", 0x4,
		     0x1000, BIT(17), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK(rtc4m0_cal_eb, "rtc4m0-cal-eb",	"aon-apb", 0x4,
		     0x1000, BIT(18), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK(rct100m_cal_eb, "rct100m-cal-eb",	"aon-apb", 0x4,
		     0x1000, BIT(19), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK(djtag_eb,		"djtag-eb",	"aon-apb", 0x4,
		     0x1000, BIT(20), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK(mbox_eb,		"mbox-eb",	"aon-apb", 0x4,
		     0x1000, BIT(21), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK(aon_dma_eb,	"aon-dma-eb",	"aon-apb", 0x4,
		     0x1000, BIT(22), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK(dbg_emc_eb,	"dbg-emc-eb",	"aon-apb", 0x4,
		     0x1000, BIT(23), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK(lvds_pll_div_en, "lvds-pll-div-en", "aon-apb", 0x4,
		     0x1000, BIT(24), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK(def_eb,		"def-eb",	"aon-apb", 0x4,
		     0x1000, BIT(25), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK(aon_apb_rsv0,	"aon-apb-rsv0",	"aon-apb", 0x4,
		     0x1000, BIT(26), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK(orp_jtag_eb,	"orp-jtag-eb",	"aon-apb", 0x4,
		     0x1000, BIT(27), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK(vsp_eb,		"vsp-eb",	"aon-apb", 0x4,
		     0x1000, BIT(28), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK(cam_eb,		"cam-eb",	"aon-apb", 0x4,
		     0x1000, BIT(29), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK(disp_eb,		"disp-eb",	"aon-apb", 0x4,
		     0x1000, BIT(30), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK(dbg_axi_if_eb, "dbg-axi-if-eb",	"aon-apb", 0x4,
		     0x1000, BIT(31), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK(sdio0_2x_en,	"sdio0-2x-en",	"aon-apb", 0x13c,
			       0x1000, BIT(2), 0, 0);
static SPRD_SC_GATE_CLK(sdio1_2x_en,	"sdio1-2x-en",	"aon-apb", 0x13c,
			       0x1000, BIT(4), 0, 0);
static SPRD_SC_GATE_CLK(sdio2_2x_en,	"sdio2-2x-en",	"aon-apb", 0x13c,
			       0x1000, BIT(6), 0, 0);
static SPRD_SC_GATE_CLK(emmc_2x_en,	"emmc-2x-en",	"aon-apb", 0x13c,
			       0x1000, BIT(9), 0, 0);
static SPRD_SC_GATE_CLK(arch_rtc_eb, "arch-rtc-eb",	"aon-apb", 0x10,
		     0x1000, BIT(0), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK(kpb_rtc_eb, "kpb-rtc-eb",	"aon-apb", 0x10,
		     0x1000, BIT(1), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK(aon_syst_rtc_eb, "aon-syst-rtc-eb",	"aon-apb", 0x10,
		     0x1000, BIT(2), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK(ap_syst_rtc_eb, "ap-syst-rtc-eb",	"aon-apb", 0x10,
		     0x1000, BIT(3), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK(aon_tmr_rtc_eb, "aon-tmr-rtc-eb",	"aon-apb", 0x10,
		     0x1000, BIT(4), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK(ap_tmr0_rtc_eb, "ap-tmr0-rtc-eb",	"aon-apb", 0x10,
		     0x1000, BIT(5), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK(eic_rtc_eb, "eic-rtc-eb",	"aon-apb", 0x10,
		     0x1000, BIT(6), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK(eic_rtcdv5_eb, "eic-rtcdv5-eb",	"aon-apb", 0x10,
		     0x1000, BIT(7), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK(ap_wdg_rtc_eb, "ap-wdg-rtc-eb",	"aon-apb", 0x10,
		     0x1000, BIT(9), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK(ap_tmr1_rtc_eb, "ap-tmr1-rtc-eb",	"aon-apb", 0x10,
		     0x1000, BIT(15), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK(ap_tmr2_rtc_eb, "ap-tmr2-rtc-eb",	"aon-apb", 0x10,
		     0x1000, BIT(16), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK(dcxo_tmr_rtc_eb, "dcxo-tmr-rtc-eb",	"aon-apb", 0x10,
		     0x1000, BIT(17), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK(bb_cal_rtc_eb, "bb-cal-rtc-eb",	"aon-apb", 0x10,
		     0x1000, BIT(18), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK(avs_big_rtc_eb, "avs-big-rtc-eb",	"aon-apb", 0x10,
		     0x1000, BIT(20), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK(avs_lit_rtc_eb, "avs-lit-rtc-eb",	"aon-apb", 0x10,
		     0x1000, BIT(21), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK(avs_gpu0_rtc_eb, "avs-gpu0-rtc-eb",	"aon-apb", 0x10,
		     0x1000, BIT(22), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK(avs_gpu1_rtc_eb, "avs-gpu1-rtc-eb",	"aon-apb", 0x10,
		     0x1000, BIT(23), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK(gpu_ts_eb, "gpu-ts-eb",	"aon-apb", 0x10,
		     0x1000, BIT(24), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK(rtcdv10_eb, "rtcdv10-eb",	"aon-apb", 0x10,
		     0x1000, BIT(27), CLK_IGNORE_UNUSED, 0);

static struct sprd_clk_common *sc9860_aon_gate[] = {
	/* address base is 0x402e0000 */
	&avs_lit_eb.common,
	&avs_big_eb.common,
	&ap_intc5_eb.common,
	&gpio_eb.common,
	&pwm0_eb.common,
	&pwm1_eb.common,
	&pwm2_eb.common,
	&pwm3_eb.common,
	&kpd_eb.common,
	&aon_sys_eb.common,
	&ap_sys_eb.common,
	&aon_tmr_eb.common,
	&ap_tmr0_eb.common,
	&efuse_eb.common,
	&eic_eb.common,
	&pub1_reg_eb.common,
	&adi_eb.common,
	&ap_intc0_eb.common,
	&ap_intc1_eb.common,
	&ap_intc2_eb.common,
	&ap_intc3_eb.common,
	&ap_intc4_eb.common,
	&splk_eb.common,
	&mspi_eb.common,
	&pub0_reg_eb.common,
	&pin_eb.common,
	&aon_ckg_eb.common,
	&gpu_eb.common,
	&apcpu_ts0_eb.common,
	&apcpu_ts1_eb.common,
	&dap_eb.common,
	&i2c_eb.common,
	&pmu_eb.common,
	&thm_eb.common,
	&aux0_eb.common,
	&aux1_eb.common,
	&aux2_eb.common,
	&probe_eb.common,
	&gpu0_avs_eb.common,
	&gpu1_avs_eb.common,
	&apcpu_wdg_eb.common,
	&ap_tmr1_eb.common,
	&ap_tmr2_eb.common,
	&disp_emc_eb.common,
	&zip_emc_eb.common,
	&gsp_emc_eb.common,
	&osc_aon_eb.common,
	&lvds_trx_eb.common,
	&lvds_tcxo_eb.common,
	&mdar_eb.common,
	&rtc4m0_cal_eb.common,
	&rct100m_cal_eb.common,
	&djtag_eb.common,
	&mbox_eb.common,
	&aon_dma_eb.common,
	&dbg_emc_eb.common,
	&lvds_pll_div_en.common,
	&def_eb.common,
	&aon_apb_rsv0.common,
	&orp_jtag_eb.common,
	&vsp_eb.common,
	&cam_eb.common,
	&disp_eb.common,
	&dbg_axi_if_eb.common,
	&sdio0_2x_en.common,
	&sdio1_2x_en.common,
	&sdio2_2x_en.common,
	&emmc_2x_en.common,
	&arch_rtc_eb.common,
	&kpb_rtc_eb.common,
	&aon_syst_rtc_eb.common,
	&ap_syst_rtc_eb.common,
	&aon_tmr_rtc_eb.common,
	&ap_tmr0_rtc_eb.common,
	&eic_rtc_eb.common,
	&eic_rtcdv5_eb.common,
	&ap_wdg_rtc_eb.common,
	&ap_tmr1_rtc_eb.common,
	&ap_tmr2_rtc_eb.common,
	&dcxo_tmr_rtc_eb.common,
	&bb_cal_rtc_eb.common,
	&avs_big_rtc_eb.common,
	&avs_lit_rtc_eb.common,
	&avs_gpu0_rtc_eb.common,
	&avs_gpu1_rtc_eb.common,
	&gpu_ts_eb.common,
	&rtcdv10_eb.common,
};

static struct clk_hw_onecell_data sc9860_aon_gate_hws = {
	.hws	= {
		[CLK_AVS_LIT_EB]	= &avs_lit_eb.common.hw,
		[CLK_AVS_BIG_EB]	= &avs_big_eb.common.hw,
		[CLK_AP_INTC5_EB]	= &ap_intc5_eb.common.hw,
		[CLK_GPIO_EB]		= &gpio_eb.common.hw,
		[CLK_PWM0_EB]		= &pwm0_eb.common.hw,
		[CLK_PWM1_EB]		= &pwm1_eb.common.hw,
		[CLK_PWM2_EB]		= &pwm2_eb.common.hw,
		[CLK_PWM3_EB]		= &pwm3_eb.common.hw,
		[CLK_KPD_EB]		= &kpd_eb.common.hw,
		[CLK_AON_SYS_EB]	= &aon_sys_eb.common.hw,
		[CLK_AP_SYS_EB]		= &ap_sys_eb.common.hw,
		[CLK_AON_TMR_EB]	= &aon_tmr_eb.common.hw,
		[CLK_AP_TMR0_EB]	= &ap_tmr0_eb.common.hw,
		[CLK_EFUSE_EB]		= &efuse_eb.common.hw,
		[CLK_EIC_EB]		= &eic_eb.common.hw,
		[CLK_PUB1_REG_EB]	= &pub1_reg_eb.common.hw,
		[CLK_ADI_EB]		= &adi_eb.common.hw,
		[CLK_AP_INTC0_EB]	= &ap_intc0_eb.common.hw,
		[CLK_AP_INTC1_EB]	= &ap_intc1_eb.common.hw,
		[CLK_AP_INTC2_EB]	= &ap_intc2_eb.common.hw,
		[CLK_AP_INTC3_EB]	= &ap_intc3_eb.common.hw,
		[CLK_AP_INTC4_EB]	= &ap_intc4_eb.common.hw,
		[CLK_SPLK_EB]		= &splk_eb.common.hw,
		[CLK_MSPI_EB]		= &mspi_eb.common.hw,
		[CLK_PUB0_REG_EB]	= &pub0_reg_eb.common.hw,
		[CLK_PIN_EB]		= &pin_eb.common.hw,
		[CLK_AON_CKG_EB]	= &aon_ckg_eb.common.hw,
		[CLK_GPU_EB]		= &gpu_eb.common.hw,
		[CLK_APCPU_TS0_EB]	= &apcpu_ts0_eb.common.hw,
		[CLK_APCPU_TS1_EB]	= &apcpu_ts1_eb.common.hw,
		[CLK_DAP_EB]		= &dap_eb.common.hw,
		[CLK_I2C_EB]		= &i2c_eb.common.hw,
		[CLK_PMU_EB]		= &pmu_eb.common.hw,
		[CLK_THM_EB]		= &thm_eb.common.hw,
		[CLK_AUX0_EB]		= &aux0_eb.common.hw,
		[CLK_AUX1_EB]		= &aux1_eb.common.hw,
		[CLK_AUX2_EB]		= &aux2_eb.common.hw,
		[CLK_PROBE_EB]		= &probe_eb.common.hw,
		[CLK_GPU0_AVS_EB]	= &gpu0_avs_eb.common.hw,
		[CLK_GPU1_AVS_EB]	= &gpu1_avs_eb.common.hw,
		[CLK_APCPU_WDG_EB]	= &apcpu_wdg_eb.common.hw,
		[CLK_AP_TMR1_EB]	= &ap_tmr1_eb.common.hw,
		[CLK_AP_TMR2_EB]	= &ap_tmr2_eb.common.hw,
		[CLK_DISP_EMC_EB]	= &disp_emc_eb.common.hw,
		[CLK_ZIP_EMC_EB]	= &zip_emc_eb.common.hw,
		[CLK_GSP_EMC_EB]	= &gsp_emc_eb.common.hw,
		[CLK_OSC_AON_EB]	= &osc_aon_eb.common.hw,
		[CLK_LVDS_TRX_EB]	= &lvds_trx_eb.common.hw,
		[CLK_LVDS_TCXO_EB]	= &lvds_tcxo_eb.common.hw,
		[CLK_MDAR_EB]		= &mdar_eb.common.hw,
		[CLK_RTC4M0_CAL_EB]	= &rtc4m0_cal_eb.common.hw,
		[CLK_RCT100M_CAL_EB]	= &rct100m_cal_eb.common.hw,
		[CLK_DJTAG_EB]		= &djtag_eb.common.hw,
		[CLK_MBOX_EB]		= &mbox_eb.common.hw,
		[CLK_AON_DMA_EB]	= &aon_dma_eb.common.hw,
		[CLK_DBG_EMC_EB]	= &dbg_emc_eb.common.hw,
		[CLK_LVDS_PLL_DIV_EN]	= &lvds_pll_div_en.common.hw,
		[CLK_DEF_EB]		= &def_eb.common.hw,
		[CLK_AON_APB_RSV0]	= &aon_apb_rsv0.common.hw,
		[CLK_ORP_JTAG_EB]	= &orp_jtag_eb.common.hw,
		[CLK_VSP_EB]		= &vsp_eb.common.hw,
		[CLK_CAM_EB]		= &cam_eb.common.hw,
		[CLK_DISP_EB]		= &disp_eb.common.hw,
		[CLK_DBG_AXI_IF_EB]	= &dbg_axi_if_eb.common.hw,
		[CLK_SDIO0_2X_EN]	= &sdio0_2x_en.common.hw,
		[CLK_SDIO1_2X_EN]	= &sdio1_2x_en.common.hw,
		[CLK_SDIO2_2X_EN]	= &sdio2_2x_en.common.hw,
		[CLK_EMMC_2X_EN]	= &emmc_2x_en.common.hw,
		[CLK_ARCH_RTC_EB]	= &arch_rtc_eb.common.hw,
		[CLK_KPB_RTC_EB]	= &kpb_rtc_eb.common.hw,
		[CLK_AON_SYST_RTC_EB]	= &aon_syst_rtc_eb.common.hw,
		[CLK_AP_SYST_RTC_EB]	= &ap_syst_rtc_eb.common.hw,
		[CLK_AON_TMR_RTC_EB]	= &aon_tmr_rtc_eb.common.hw,
		[CLK_AP_TMR0_RTC_EB]	= &ap_tmr0_rtc_eb.common.hw,
		[CLK_EIC_RTC_EB]	= &eic_rtc_eb.common.hw,
		[CLK_EIC_RTCDV5_EB]	= &eic_rtcdv5_eb.common.hw,
		[CLK_AP_WDG_RTC_EB]	= &ap_wdg_rtc_eb.common.hw,
		[CLK_AP_TMR1_RTC_EB]	= &ap_tmr1_rtc_eb.common.hw,
		[CLK_AP_TMR2_RTC_EB]	= &ap_tmr2_rtc_eb.common.hw,
		[CLK_DCXO_TMR_RTC_EB]	= &dcxo_tmr_rtc_eb.common.hw,
		[CLK_BB_CAL_RTC_EB]	= &bb_cal_rtc_eb.common.hw,
		[CLK_AVS_BIG_RTC_EB]	= &avs_big_rtc_eb.common.hw,
		[CLK_AVS_LIT_RTC_EB]	= &avs_lit_rtc_eb.common.hw,
		[CLK_AVS_GPU0_RTC_EB]	= &avs_gpu0_rtc_eb.common.hw,
		[CLK_AVS_GPU1_RTC_EB]	= &avs_gpu1_rtc_eb.common.hw,
		[CLK_GPU_TS_EB]		= &gpu_ts_eb.common.hw,
		[CLK_RTCDV10_EB]	= &rtcdv10_eb.common.hw,
	},
	.num	= CLK_AON_GATE_NUM,
};

static const struct sprd_clk_desc sc9860_aon_gate_desc = {
	.clk_clks	= sc9860_aon_gate,
	.num_clk_clks	= ARRAY_SIZE(sc9860_aon_gate),
	.hw_clks	= &sc9860_aon_gate_hws,
};

static const u8 mcu_table[] = { 0, 1, 2, 3, 4, 8 };
static const char * const lit_mcu_parents[] = {	"ext-26m",	"twpll-512m",
						"twpll-768m",	"ltepll0",
						"twpll",	"mpll0" };
static SPRD_COMP_CLK_TABLE(lit_mcu, "lit-mcu", lit_mcu_parents, 0x20,
			   mcu_table, 0, 4, 4, 3, 0);

static const char * const big_mcu_parents[] = {	"ext-26m",	"twpll-512m",
						"twpll-768m",	"ltepll0",
						"twpll",	"mpll1" };
static SPRD_COMP_CLK_TABLE(big_mcu, "big-mcu", big_mcu_parents, 0x24,
			   mcu_table, 0, 4, 4, 3, 0);

static struct sprd_clk_common *sc9860_aonsecure_clk[] = {
	/* address base is 0x40880000 */
	&lit_mcu.common,
	&big_mcu.common,
};

static struct clk_hw_onecell_data sc9860_aonsecure_clk_hws = {
	.hws	= {
		[CLK_LIT_MCU]		= &lit_mcu.common.hw,
		[CLK_BIG_MCU]		= &big_mcu.common.hw,
	},
	.num	= CLK_AONSECURE_NUM,
};

static const struct sprd_clk_desc sc9860_aonsecure_clk_desc = {
	.clk_clks	= sc9860_aonsecure_clk,
	.num_clk_clks	= ARRAY_SIZE(sc9860_aonsecure_clk),
	.hw_clks	= &sc9860_aonsecure_clk_hws,
};

static SPRD_SC_GATE_CLK(agcp_iis0_eb,	"agcp-iis0-eb",		"aon-apb",
		     0x0, 0x100, BIT(0), 0, 0);
static SPRD_SC_GATE_CLK(agcp_iis1_eb,	"agcp-iis1-eb",		"aon-apb",
		     0x0, 0x100, BIT(1), 0, 0);
static SPRD_SC_GATE_CLK(agcp_iis2_eb,	"agcp-iis2-eb",		"aon-apb",
		     0x0, 0x100, BIT(2), 0, 0);
static SPRD_SC_GATE_CLK(agcp_iis3_eb,	"agcp-iis3-eb",		"aon-apb",
		     0x0, 0x100, BIT(3), 0, 0);
static SPRD_SC_GATE_CLK(agcp_uart_eb,	"agcp-uart-eb",		"aon-apb",
		     0x0, 0x100, BIT(4), 0, 0);
static SPRD_SC_GATE_CLK(agcp_dmacp_eb,	"agcp-dmacp-eb",	"aon-apb",
		     0x0, 0x100, BIT(5), 0, 0);
static SPRD_SC_GATE_CLK(agcp_dmaap_eb,	"agcp-dmaap-eb",	"aon-apb",
		     0x0, 0x100, BIT(6), 0, 0);
static SPRD_SC_GATE_CLK(agcp_arc48k_eb,	"agcp-arc48k-eb",	"aon-apb",
		     0x0, 0x100, BIT(10), 0, 0);
static SPRD_SC_GATE_CLK(agcp_src44p1k_eb, "agcp-src44p1k-eb",	"aon-apb",
		     0x0, 0x100, BIT(11), 0, 0);
static SPRD_SC_GATE_CLK(agcp_mcdt_eb,	"agcp-mcdt-eb",		"aon-apb",
		     0x0, 0x100, BIT(12), 0, 0);
static SPRD_SC_GATE_CLK(agcp_vbcifd_eb,	"agcp-vbcifd-eb",	"aon-apb",
		     0x0, 0x100, BIT(13), 0, 0);
static SPRD_SC_GATE_CLK(agcp_vbc_eb,	"agcp-vbc-eb",		"aon-apb",
		     0x0, 0x100, BIT(14), 0, 0);
static SPRD_SC_GATE_CLK(agcp_spinlock_eb, "agcp-spinlock-eb",	"aon-apb",
		     0x0, 0x100, BIT(15), 0, 0);
static SPRD_SC_GATE_CLK(agcp_icu_eb,	"agcp-icu-eb",		"aon-apb",
		     0x0, 0x100, BIT(16), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK(agcp_ap_ashb_eb, "agcp-ap-ashb-eb",	"aon-apb",
		     0x0, 0x100, BIT(17), 0, 0);
static SPRD_SC_GATE_CLK(agcp_cp_ashb_eb, "agcp-cp-ashb-eb",	"aon-apb",
		     0x0, 0x100, BIT(18), 0, 0);
static SPRD_SC_GATE_CLK(agcp_aud_eb,	"agcp-aud-eb",		"aon-apb",
		     0x0, 0x100, BIT(19), 0, 0);
static SPRD_SC_GATE_CLK(agcp_audif_eb,	"agcp-audif-eb",	"aon-apb",
		     0x0, 0x100, BIT(20), 0, 0);

static struct sprd_clk_common *sc9860_agcp_gate[] = {
	/* address base is 0x415e0000 */
	&agcp_iis0_eb.common,
	&agcp_iis1_eb.common,
	&agcp_iis2_eb.common,
	&agcp_iis3_eb.common,
	&agcp_uart_eb.common,
	&agcp_dmacp_eb.common,
	&agcp_dmaap_eb.common,
	&agcp_arc48k_eb.common,
	&agcp_src44p1k_eb.common,
	&agcp_mcdt_eb.common,
	&agcp_vbcifd_eb.common,
	&agcp_vbc_eb.common,
	&agcp_spinlock_eb.common,
	&agcp_icu_eb.common,
	&agcp_ap_ashb_eb.common,
	&agcp_cp_ashb_eb.common,
	&agcp_aud_eb.common,
	&agcp_audif_eb.common,
};

static struct clk_hw_onecell_data sc9860_agcp_gate_hws = {
	.hws	= {
		[CLK_AGCP_IIS0_EB]	= &agcp_iis0_eb.common.hw,
		[CLK_AGCP_IIS1_EB]	= &agcp_iis1_eb.common.hw,
		[CLK_AGCP_IIS2_EB]	= &agcp_iis2_eb.common.hw,
		[CLK_AGCP_IIS3_EB]	= &agcp_iis3_eb.common.hw,
		[CLK_AGCP_UART_EB]	= &agcp_uart_eb.common.hw,
		[CLK_AGCP_DMACP_EB]	= &agcp_dmacp_eb.common.hw,
		[CLK_AGCP_DMAAP_EB]	= &agcp_dmaap_eb.common.hw,
		[CLK_AGCP_ARC48K_EB]	= &agcp_arc48k_eb.common.hw,
		[CLK_AGCP_SRC44P1K_EB]	= &agcp_src44p1k_eb.common.hw,
		[CLK_AGCP_MCDT_EB]	= &agcp_mcdt_eb.common.hw,
		[CLK_AGCP_VBCIFD_EB]	= &agcp_vbcifd_eb.common.hw,
		[CLK_AGCP_VBC_EB]	= &agcp_vbc_eb.common.hw,
		[CLK_AGCP_SPINLOCK_EB]	= &agcp_spinlock_eb.common.hw,
		[CLK_AGCP_ICU_EB]	= &agcp_icu_eb.common.hw,
		[CLK_AGCP_AP_ASHB_EB]	= &agcp_ap_ashb_eb.common.hw,
		[CLK_AGCP_CP_ASHB_EB]	= &agcp_cp_ashb_eb.common.hw,
		[CLK_AGCP_AUD_EB]	= &agcp_aud_eb.common.hw,
		[CLK_AGCP_AUDIF_EB]	= &agcp_audif_eb.common.hw,
	},
	.num	= CLK_AGCP_GATE_NUM,
};

static const struct sprd_clk_desc sc9860_agcp_gate_desc = {
	.clk_clks	= sc9860_agcp_gate,
	.num_clk_clks	= ARRAY_SIZE(sc9860_agcp_gate),
	.hw_clks	= &sc9860_agcp_gate_hws,
};

static const char * const gpu_parents[] = { "twpll-512m",
					    "twpll-768m",
					    "gpll" };
static SPRD_COMP_CLK(gpu_clk,	"gpu",	gpu_parents, 0x20,
		     0, 2, 8, 4, 0);

static struct sprd_clk_common *sc9860_gpu_clk[] = {
	/* address base is 0x60200000 */
	&gpu_clk.common,
};

static struct clk_hw_onecell_data sc9860_gpu_clk_hws = {
	.hws	= {
		[CLK_GPU]	= &gpu_clk.common.hw,
	},
	.num	= CLK_GPU_NUM,
};

static const struct sprd_clk_desc sc9860_gpu_clk_desc = {
	.clk_clks	= sc9860_gpu_clk,
	.num_clk_clks	= ARRAY_SIZE(sc9860_gpu_clk),
	.hw_clks	= &sc9860_gpu_clk_hws,
};

static const char * const ahb_parents[] = { "ext-26m", "twpll-96m",
					    "twpll-128m", "twpll-153m6" };
static SPRD_MUX_CLK(ahb_vsp, "ahb-vsp", ahb_parents, 0x20,
		    0, 2, SC9860_MUX_FLAG);

static const char * const vsp_parents[] = {	"twpll-76m8",	"twpll-128m",
						"twpll-256m",	"twpll-307m2",
						"twpll-384m" };
static SPRD_COMP_CLK(vsp_clk, "vsp", vsp_parents, 0x24, 0, 3, 8, 2, 0);

static const char * const dispc_parents[] = {	"twpll-76m8",	"twpll-128m",
						"twpll-256m",	"twpll-307m2" };
static SPRD_COMP_CLK(vsp_enc, "vsp-enc", dispc_parents, 0x28, 0, 2, 8, 2, 0);

static const char * const vpp_parents[] = { "twpll-96m", "twpll-153m6",
					    "twpll-192m", "twpll-256m" };
static SPRD_MUX_CLK(vpp_clk, "vpp", vpp_parents, 0x2c,
		    0, 2, SC9860_MUX_FLAG);
static const char * const vsp_26m_parents[] = { "ext-26m" };
static SPRD_MUX_CLK(vsp_26m, "vsp-26m", vsp_26m_parents, 0x30,
		    0, 1, SC9860_MUX_FLAG);

static struct sprd_clk_common *sc9860_vsp_clk[] = {
	/* address base is 0x61000000 */
	&ahb_vsp.common,
	&vsp_clk.common,
	&vsp_enc.common,
	&vpp_clk.common,
	&vsp_26m.common,
};

static struct clk_hw_onecell_data sc9860_vsp_clk_hws = {
	.hws	= {
		[CLK_AHB_VSP]	= &ahb_vsp.common.hw,
		[CLK_VSP]	= &vsp_clk.common.hw,
		[CLK_VSP_ENC]	= &vsp_enc.common.hw,
		[CLK_VPP]	= &vpp_clk.common.hw,
		[CLK_VSP_26M]	= &vsp_26m.common.hw,
	},
	.num	= CLK_VSP_NUM,
};

static const struct sprd_clk_desc sc9860_vsp_clk_desc = {
	.clk_clks	= sc9860_vsp_clk,
	.num_clk_clks	= ARRAY_SIZE(sc9860_vsp_clk),
	.hw_clks	= &sc9860_vsp_clk_hws,
};

static SPRD_SC_GATE_CLK(vsp_dec_eb,	"vsp-dec-eb",	"ahb-vsp", 0x0,
		     0x1000, BIT(0), 0, 0);
static SPRD_SC_GATE_CLK(vsp_ckg_eb,	"vsp-ckg-eb",	"ahb-vsp", 0x0,
		     0x1000, BIT(1), 0, 0);
static SPRD_SC_GATE_CLK(vsp_mmu_eb,	"vsp-mmu-eb",	"ahb-vsp", 0x0,
		     0x1000, BIT(2), 0, 0);
static SPRD_SC_GATE_CLK(vsp_enc_eb,	"vsp-enc-eb",	"ahb-vsp", 0x0,
		     0x1000, BIT(3), 0, 0);
static SPRD_SC_GATE_CLK(vpp_eb,		"vpp-eb",	"ahb-vsp", 0x0,
		     0x1000, BIT(4), 0, 0);
static SPRD_SC_GATE_CLK(vsp_26m_eb,	"vsp-26m-eb",	"ahb-vsp", 0x0,
		     0x1000, BIT(5), 0, 0);
static SPRD_GATE_CLK(vsp_axi_gate,	"vsp-axi-gate",	"ahb-vsp", 0x8,
		     BIT(0), 0, 0);
static SPRD_GATE_CLK(vsp_enc_gate,	"vsp-enc-gate",	"ahb-vsp", 0x8,
		     BIT(1), 0, 0);
static SPRD_GATE_CLK(vpp_axi_gate,	"vpp-axi-gate",	"ahb-vsp", 0x8,
		     BIT(2), 0, 0);
static SPRD_GATE_CLK(vsp_bm_gate,	"vsp-bm-gate",	"ahb-vsp", 0x8,
		     BIT(8), 0, 0);
static SPRD_GATE_CLK(vsp_enc_bm_gate, "vsp-enc-bm-gate", "ahb-vsp", 0x8,
		     BIT(9), 0, 0);
static SPRD_GATE_CLK(vpp_bm_gate,	"vpp-bm-gate",	"ahb-vsp", 0x8,
		     BIT(10), 0, 0);

static struct sprd_clk_common *sc9860_vsp_gate[] = {
	/* address base is 0x61100000 */
	&vsp_dec_eb.common,
	&vsp_ckg_eb.common,
	&vsp_mmu_eb.common,
	&vsp_enc_eb.common,
	&vpp_eb.common,
	&vsp_26m_eb.common,
	&vsp_axi_gate.common,
	&vsp_enc_gate.common,
	&vpp_axi_gate.common,
	&vsp_bm_gate.common,
	&vsp_enc_bm_gate.common,
	&vpp_bm_gate.common,
};

static struct clk_hw_onecell_data sc9860_vsp_gate_hws = {
	.hws	= {
		[CLK_VSP_DEC_EB]	= &vsp_dec_eb.common.hw,
		[CLK_VSP_CKG_EB]	= &vsp_ckg_eb.common.hw,
		[CLK_VSP_MMU_EB]	= &vsp_mmu_eb.common.hw,
		[CLK_VSP_ENC_EB]	= &vsp_enc_eb.common.hw,
		[CLK_VPP_EB]		= &vpp_eb.common.hw,
		[CLK_VSP_26M_EB]	= &vsp_26m_eb.common.hw,
		[CLK_VSP_AXI_GATE]	= &vsp_axi_gate.common.hw,
		[CLK_VSP_ENC_GATE]	= &vsp_enc_gate.common.hw,
		[CLK_VPP_AXI_GATE]	= &vpp_axi_gate.common.hw,
		[CLK_VSP_BM_GATE]	= &vsp_bm_gate.common.hw,
		[CLK_VSP_ENC_BM_GATE]	= &vsp_enc_bm_gate.common.hw,
		[CLK_VPP_BM_GATE]	= &vpp_bm_gate.common.hw,
	},
	.num	= CLK_VSP_GATE_NUM,
};

static const struct sprd_clk_desc sc9860_vsp_gate_desc = {
	.clk_clks	= sc9860_vsp_gate,
	.num_clk_clks	= ARRAY_SIZE(sc9860_vsp_gate),
	.hw_clks	= &sc9860_vsp_gate_hws,
};

static SPRD_MUX_CLK(ahb_cam, "ahb-cam", ahb_parents, 0x20,
		    0, 2, SC9860_MUX_FLAG);
static const char * const sensor_parents[] = {	"ext-26m",	"twpll-48m",
						"twpll-76m8",	"twpll-96m" };
static SPRD_COMP_CLK(sensor0_clk, "sensor0", sensor_parents, 0x24,
		     0, 2, 8, 3, 0);
static SPRD_COMP_CLK(sensor1_clk, "sensor1", sensor_parents, 0x28,
		     0, 2, 8, 3, 0);
static SPRD_COMP_CLK(sensor2_clk, "sensor2", sensor_parents, 0x2c,
		     0, 2, 8, 3, 0);
static SPRD_GATE_CLK(mipi_csi0_eb, "mipi-csi0-eb", "ahb-cam", 0x4c,
		     BIT(16), 0, 0);
static SPRD_GATE_CLK(mipi_csi1_eb, "mipi-csi1-eb", "ahb-cam", 0x50,
		     BIT(16), 0, 0);

static struct sprd_clk_common *sc9860_cam_clk[] = {
	/* address base is 0x62000000 */
	&ahb_cam.common,
	&sensor0_clk.common,
	&sensor1_clk.common,
	&sensor2_clk.common,
	&mipi_csi0_eb.common,
	&mipi_csi1_eb.common,
};

static struct clk_hw_onecell_data sc9860_cam_clk_hws = {
	.hws	= {
		[CLK_AHB_CAM]		= &ahb_cam.common.hw,
		[CLK_SENSOR0]		= &sensor0_clk.common.hw,
		[CLK_SENSOR1]		= &sensor1_clk.common.hw,
		[CLK_SENSOR2]		= &sensor2_clk.common.hw,
		[CLK_MIPI_CSI0_EB]	= &mipi_csi0_eb.common.hw,
		[CLK_MIPI_CSI1_EB]	= &mipi_csi1_eb.common.hw,
	},
	.num	= CLK_CAM_NUM,
};

static const struct sprd_clk_desc sc9860_cam_clk_desc = {
	.clk_clks	= sc9860_cam_clk,
	.num_clk_clks	= ARRAY_SIZE(sc9860_cam_clk),
	.hw_clks	= &sc9860_cam_clk_hws,
};

static SPRD_SC_GATE_CLK(dcam0_eb,		"dcam0-eb",	"ahb-cam", 0x0,
		     0x1000, BIT(0), 0, 0);
static SPRD_SC_GATE_CLK(dcam1_eb,		"dcam1-eb",	"ahb-cam", 0x0,
		     0x1000, BIT(1), 0, 0);
static SPRD_SC_GATE_CLK(isp0_eb,		"isp0-eb",	"ahb-cam", 0x0,
		     0x1000, BIT(2), 0, 0);
static SPRD_SC_GATE_CLK(csi0_eb,		"csi0-eb",	"ahb-cam", 0x0,
		     0x1000, BIT(3), 0, 0);
static SPRD_SC_GATE_CLK(csi1_eb,		"csi1-eb",	"ahb-cam", 0x0,
		     0x1000, BIT(4), 0, 0);
static SPRD_SC_GATE_CLK(jpg0_eb,		"jpg0-eb",	"ahb-cam", 0x0,
		     0x1000, BIT(5), 0, 0);
static SPRD_SC_GATE_CLK(jpg1_eb,		"jpg1-eb",	"ahb-cam", 0x0,
		     0x1000, BIT(6), 0, 0);
static SPRD_SC_GATE_CLK(cam_ckg_eb,	"cam-ckg-eb",	"ahb-cam", 0x0,
		     0x1000, BIT(7), 0, 0);
static SPRD_SC_GATE_CLK(cam_mmu_eb,	"cam-mmu-eb",	"ahb-cam", 0x0,
		     0x1000, BIT(8), 0, 0);
static SPRD_SC_GATE_CLK(isp1_eb,		"isp1-eb",	"ahb-cam", 0x0,
		     0x1000, BIT(9), 0, 0);
static SPRD_SC_GATE_CLK(cpp_eb,		"cpp-eb",	"ahb-cam", 0x0,
		     0x1000, BIT(10), 0, 0);
static SPRD_SC_GATE_CLK(mmu_pf_eb,		"mmu-pf-eb",	"ahb-cam", 0x0,
		     0x1000, BIT(11), 0, 0);
static SPRD_SC_GATE_CLK(isp2_eb,		"isp2-eb",	"ahb-cam", 0x0,
		     0x1000, BIT(12), 0, 0);
static SPRD_SC_GATE_CLK(dcam2isp_if_eb, "dcam2isp-if-eb",	"ahb-cam", 0x0,
		     0x1000, BIT(13), 0, 0);
static SPRD_SC_GATE_CLK(isp2dcam_if_eb, "isp2dcam-if-eb",	"ahb-cam", 0x0,
		     0x1000, BIT(14), 0, 0);
static SPRD_SC_GATE_CLK(isp_lclk_eb,	"isp-lclk-eb",	"ahb-cam", 0x0,
		     0x1000, BIT(15), 0, 0);
static SPRD_SC_GATE_CLK(isp_iclk_eb,	"isp-iclk-eb",	"ahb-cam", 0x0,
		     0x1000, BIT(16), 0, 0);
static SPRD_SC_GATE_CLK(isp_mclk_eb,	"isp-mclk-eb",	"ahb-cam", 0x0,
		     0x1000, BIT(17), 0, 0);
static SPRD_SC_GATE_CLK(isp_pclk_eb,	"isp-pclk-eb",	"ahb-cam", 0x0,
		     0x1000, BIT(18), 0, 0);
static SPRD_SC_GATE_CLK(isp_isp2dcam_eb, "isp-isp2dcam-eb", "ahb-cam", 0x0,
		     0x1000, BIT(19), 0, 0);
static SPRD_SC_GATE_CLK(dcam0_if_eb,	"dcam0-if-eb",	"ahb-cam", 0x0,
		     0x1000, BIT(20), 0, 0);
static SPRD_SC_GATE_CLK(clk26m_if_eb,	"clk26m-if-eb",	"ahb-cam", 0x0,
		     0x1000, BIT(21), 0, 0);
static SPRD_GATE_CLK(cphy0_gate, "cphy0-gate", "ahb-cam", 0x8,
		     BIT(0), 0, 0);
static SPRD_GATE_CLK(mipi_csi0_gate, "mipi-csi0-gate", "ahb-cam", 0x8,
		     BIT(1), 0, 0);
static SPRD_GATE_CLK(cphy1_gate,	"cphy1-gate",	"ahb-cam", 0x8,
		     BIT(2), 0, 0);
static SPRD_GATE_CLK(mipi_csi1,		"mipi-csi1",	"ahb-cam", 0x8,
		     BIT(3), 0, 0);
static SPRD_GATE_CLK(dcam0_axi_gate,	"dcam0-axi-gate", "ahb-cam", 0x8,
		     BIT(4), 0, 0);
static SPRD_GATE_CLK(dcam1_axi_gate,	"dcam1-axi-gate", "ahb-cam", 0x8,
		     BIT(5), 0, 0);
static SPRD_GATE_CLK(sensor0_gate,	"sensor0-gate",	"ahb-cam", 0x8,
		     BIT(6), 0, 0);
static SPRD_GATE_CLK(sensor1_gate,	"sensor1-gate",	"ahb-cam", 0x8,
		     BIT(7), 0, 0);
static SPRD_GATE_CLK(jpg0_axi_gate,	"jpg0-axi-gate", "ahb-cam", 0x8,
		     BIT(8), 0, 0);
static SPRD_GATE_CLK(gpg1_axi_gate,	"gpg1-axi-gate", "ahb-cam", 0x8,
		     BIT(9), 0, 0);
static SPRD_GATE_CLK(isp0_axi_gate,	"isp0-axi-gate", "ahb-cam", 0x8,
		     BIT(10), 0, 0);
static SPRD_GATE_CLK(isp1_axi_gate,	"isp1-axi-gate", "ahb-cam", 0x8,
		     BIT(11), 0, 0);
static SPRD_GATE_CLK(isp2_axi_gate,	"isp2-axi-gate", "ahb-cam", 0x8,
		     BIT(12), 0, 0);
static SPRD_GATE_CLK(cpp_axi_gate,	"cpp-axi-gate",	"ahb-cam", 0x8,
		     BIT(13), 0, 0);
static SPRD_GATE_CLK(d0_if_axi_gate,	"d0-if-axi-gate", "ahb-cam", 0x8,
		     BIT(14), 0, 0);
static SPRD_GATE_CLK(d2i_if_axi_gate, "d2i-if-axi-gate", "ahb-cam", 0x8,
		     BIT(15), 0, 0);
static SPRD_GATE_CLK(i2d_if_axi_gate, "i2d-if-axi-gate", "ahb-cam", 0x8,
		     BIT(16), 0, 0);
static SPRD_GATE_CLK(spare_axi_gate, "spare-axi-gate",	"ahb-cam", 0x8,
		     BIT(17), 0, 0);
static SPRD_GATE_CLK(sensor2_gate, "sensor2-gate",	"ahb-cam", 0x8,
		     BIT(18), 0, 0);
static SPRD_SC_GATE_CLK(d0if_in_d_en, "d0if-in-d-en", "ahb-cam", 0x28,
		     0x1000, BIT(0), 0, 0);
static SPRD_SC_GATE_CLK(d1if_in_d_en, "d1if-in-d-en", "ahb-cam", 0x28,
		     0x1000, BIT(1), 0, 0);
static SPRD_SC_GATE_CLK(d0if_in_d2i_en, "d0if-in-d2i-en", "ahb-cam", 0x28,
		     0x1000, BIT(2), 0, 0);
static SPRD_SC_GATE_CLK(d1if_in_d2i_en, "d1if-in-d2i-en",	"ahb-cam", 0x28,
		     0x1000, BIT(3), 0, 0);
static SPRD_SC_GATE_CLK(ia_in_d2i_en, "ia-in-d2i-en",	"ahb-cam", 0x28,
		     0x1000, BIT(4), 0, 0);
static SPRD_SC_GATE_CLK(ib_in_d2i_en,	"ib-in-d2i-en",	"ahb-cam", 0x28,
		     0x1000, BIT(5), 0, 0);
static SPRD_SC_GATE_CLK(ic_in_d2i_en,	"ic-in-d2i-en",	"ahb-cam", 0x28,
		     0x1000, BIT(6), 0, 0);
static SPRD_SC_GATE_CLK(ia_in_i_en,	"ia-in-i-en",	"ahb-cam", 0x28,
		     0x1000, BIT(7), 0, 0);
static SPRD_SC_GATE_CLK(ib_in_i_en,	"ib-in-i-en",	"ahb-cam", 0x28,
		     0x1000, BIT(8), 0, 0);
static SPRD_SC_GATE_CLK(ic_in_i_en,	"ic-in-i-en",	"ahb-cam", 0x28,
		     0x1000, BIT(9), 0, 0);

static struct sprd_clk_common *sc9860_cam_gate[] = {
	/* address base is 0x62100000 */
	&dcam0_eb.common,
	&dcam1_eb.common,
	&isp0_eb.common,
	&csi0_eb.common,
	&csi1_eb.common,
	&jpg0_eb.common,
	&jpg1_eb.common,
	&cam_ckg_eb.common,
	&cam_mmu_eb.common,
	&isp1_eb.common,
	&cpp_eb.common,
	&mmu_pf_eb.common,
	&isp2_eb.common,
	&dcam2isp_if_eb.common,
	&isp2dcam_if_eb.common,
	&isp_lclk_eb.common,
	&isp_iclk_eb.common,
	&isp_mclk_eb.common,
	&isp_pclk_eb.common,
	&isp_isp2dcam_eb.common,
	&dcam0_if_eb.common,
	&clk26m_if_eb.common,
	&cphy0_gate.common,
	&mipi_csi0_gate.common,
	&cphy1_gate.common,
	&mipi_csi1.common,
	&dcam0_axi_gate.common,
	&dcam1_axi_gate.common,
	&sensor0_gate.common,
	&sensor1_gate.common,
	&jpg0_axi_gate.common,
	&gpg1_axi_gate.common,
	&isp0_axi_gate.common,
	&isp1_axi_gate.common,
	&isp2_axi_gate.common,
	&cpp_axi_gate.common,
	&d0_if_axi_gate.common,
	&d2i_if_axi_gate.common,
	&i2d_if_axi_gate.common,
	&spare_axi_gate.common,
	&sensor2_gate.common,
	&d0if_in_d_en.common,
	&d1if_in_d_en.common,
	&d0if_in_d2i_en.common,
	&d1if_in_d2i_en.common,
	&ia_in_d2i_en.common,
	&ib_in_d2i_en.common,
	&ic_in_d2i_en.common,
	&ia_in_i_en.common,
	&ib_in_i_en.common,
	&ic_in_i_en.common,
};

static struct clk_hw_onecell_data sc9860_cam_gate_hws = {
	.hws	= {
		[CLK_DCAM0_EB]		= &dcam0_eb.common.hw,
		[CLK_DCAM1_EB]		= &dcam1_eb.common.hw,
		[CLK_ISP0_EB]		= &isp0_eb.common.hw,
		[CLK_CSI0_EB]		= &csi0_eb.common.hw,
		[CLK_CSI1_EB]		= &csi1_eb.common.hw,
		[CLK_JPG0_EB]		= &jpg0_eb.common.hw,
		[CLK_JPG1_EB]		= &jpg1_eb.common.hw,
		[CLK_CAM_CKG_EB]	= &cam_ckg_eb.common.hw,
		[CLK_CAM_MMU_EB]	= &cam_mmu_eb.common.hw,
		[CLK_ISP1_EB]		= &isp1_eb.common.hw,
		[CLK_CPP_EB]		= &cpp_eb.common.hw,
		[CLK_MMU_PF_EB]		= &mmu_pf_eb.common.hw,
		[CLK_ISP2_EB]		= &isp2_eb.common.hw,
		[CLK_DCAM2ISP_IF_EB]	= &dcam2isp_if_eb.common.hw,
		[CLK_ISP2DCAM_IF_EB]	= &isp2dcam_if_eb.common.hw,
		[CLK_ISP_LCLK_EB]	= &isp_lclk_eb.common.hw,
		[CLK_ISP_ICLK_EB]	= &isp_iclk_eb.common.hw,
		[CLK_ISP_MCLK_EB]	= &isp_mclk_eb.common.hw,
		[CLK_ISP_PCLK_EB]	= &isp_pclk_eb.common.hw,
		[CLK_ISP_ISP2DCAM_EB]	= &isp_isp2dcam_eb.common.hw,
		[CLK_DCAM0_IF_EB]	= &dcam0_if_eb.common.hw,
		[CLK_CLK26M_IF_EB]	= &clk26m_if_eb.common.hw,
		[CLK_CPHY0_GATE]	= &cphy0_gate.common.hw,
		[CLK_MIPI_CSI0_GATE]	= &mipi_csi0_gate.common.hw,
		[CLK_CPHY1_GATE]	= &cphy1_gate.common.hw,
		[CLK_MIPI_CSI1]		= &mipi_csi1.common.hw,
		[CLK_DCAM0_AXI_GATE]	= &dcam0_axi_gate.common.hw,
		[CLK_DCAM1_AXI_GATE]	= &dcam1_axi_gate.common.hw,
		[CLK_SENSOR0_GATE]	= &sensor0_gate.common.hw,
		[CLK_SENSOR1_GATE]	= &sensor1_gate.common.hw,
		[CLK_JPG0_AXI_GATE]	= &jpg0_axi_gate.common.hw,
		[CLK_GPG1_AXI_GATE]	= &gpg1_axi_gate.common.hw,
		[CLK_ISP0_AXI_GATE]	= &isp0_axi_gate.common.hw,
		[CLK_ISP1_AXI_GATE]	= &isp1_axi_gate.common.hw,
		[CLK_ISP2_AXI_GATE]	= &isp2_axi_gate.common.hw,
		[CLK_CPP_AXI_GATE]	= &cpp_axi_gate.common.hw,
		[CLK_D0_IF_AXI_GATE]	= &d0_if_axi_gate.common.hw,
		[CLK_D2I_IF_AXI_GATE]	= &d2i_if_axi_gate.common.hw,
		[CLK_I2D_IF_AXI_GATE]	= &i2d_if_axi_gate.common.hw,
		[CLK_SPARE_AXI_GATE]	= &spare_axi_gate.common.hw,
		[CLK_SENSOR2_GATE]	= &sensor2_gate.common.hw,
		[CLK_D0IF_IN_D_EN]	= &d0if_in_d_en.common.hw,
		[CLK_D1IF_IN_D_EN]	= &d1if_in_d_en.common.hw,
		[CLK_D0IF_IN_D2I_EN]	= &d0if_in_d2i_en.common.hw,
		[CLK_D1IF_IN_D2I_EN]	= &d1if_in_d2i_en.common.hw,
		[CLK_IA_IN_D2I_EN]	= &ia_in_d2i_en.common.hw,
		[CLK_IB_IN_D2I_EN]	= &ib_in_d2i_en.common.hw,
		[CLK_IC_IN_D2I_EN]	= &ic_in_d2i_en.common.hw,
		[CLK_IA_IN_I_EN]	= &ia_in_i_en.common.hw,
		[CLK_IB_IN_I_EN]	= &ib_in_i_en.common.hw,
		[CLK_IC_IN_I_EN]	= &ic_in_i_en.common.hw,
	},
	.num	= CLK_CAM_GATE_NUM,
};

static const struct sprd_clk_desc sc9860_cam_gate_desc = {
	.clk_clks	= sc9860_cam_gate,
	.num_clk_clks	= ARRAY_SIZE(sc9860_cam_gate),
	.hw_clks	= &sc9860_cam_gate_hws,
};

static SPRD_MUX_CLK(ahb_disp, "ahb-disp", ahb_parents, 0x20,
		    0, 2, SC9860_MUX_FLAG);
static SPRD_COMP_CLK(dispc0_dpi, "dispc0-dpi", dispc_parents,	0x34,
		     0, 2, 8, 2, 0);
static SPRD_COMP_CLK(dispc1_dpi, "dispc1-dpi", dispc_parents,	0x40,
		     0, 2, 8, 2, 0);

static struct sprd_clk_common *sc9860_disp_clk[] = {
	/* address base is 0x63000000 */
	&ahb_disp.common,
	&dispc0_dpi.common,
	&dispc1_dpi.common,
};

static struct clk_hw_onecell_data sc9860_disp_clk_hws = {
	.hws	= {
		[CLK_AHB_DISP]		= &ahb_disp.common.hw,
		[CLK_DISPC0_DPI]	= &dispc0_dpi.common.hw,
		[CLK_DISPC1_DPI]	= &dispc1_dpi.common.hw,
	},
	.num	= CLK_DISP_NUM,
};

static const struct sprd_clk_desc sc9860_disp_clk_desc = {
	.clk_clks	= sc9860_disp_clk,
	.num_clk_clks	= ARRAY_SIZE(sc9860_disp_clk),
	.hw_clks	= &sc9860_disp_clk_hws,
};

static SPRD_SC_GATE_CLK(dispc0_eb,	"dispc0-eb",	"ahb-disp", 0x0,
		     0x1000, BIT(0), 0, 0);
static SPRD_SC_GATE_CLK(dispc1_eb,	"dispc1-eb",	"ahb-disp", 0x0,
		     0x1000, BIT(1), 0, 0);
static SPRD_SC_GATE_CLK(dispc_mmu_eb,	"dispc-mmu-eb",	"ahb-disp", 0x0,
		     0x1000, BIT(2), 0, 0);
static SPRD_SC_GATE_CLK(gsp0_eb,		"gsp0-eb",	"ahb-disp", 0x0,
		     0x1000, BIT(3), 0, 0);
static SPRD_SC_GATE_CLK(gsp1_eb,		"gsp1-eb",	"ahb-disp", 0x0,
		     0x1000, BIT(4), 0, 0);
static SPRD_SC_GATE_CLK(gsp0_mmu_eb,	"gsp0-mmu-eb",	"ahb-disp", 0x0,
		     0x1000, BIT(5), 0, 0);
static SPRD_SC_GATE_CLK(gsp1_mmu_eb,	"gsp1-mmu-eb",	"ahb-disp", 0x0,
		     0x1000, BIT(6), 0, 0);
static SPRD_SC_GATE_CLK(dsi0_eb,		"dsi0-eb",	"ahb-disp", 0x0,
		     0x1000, BIT(7), 0, 0);
static SPRD_SC_GATE_CLK(dsi1_eb,		"dsi1-eb",	"ahb-disp", 0x0,
		     0x1000, BIT(8), 0, 0);
static SPRD_SC_GATE_CLK(disp_ckg_eb,	"disp-ckg-eb",	"ahb-disp", 0x0,
		     0x1000, BIT(9), 0, 0);
static SPRD_SC_GATE_CLK(disp_gpu_eb,	"disp-gpu-eb",	"ahb-disp", 0x0,
		     0x1000, BIT(10), 0, 0);
static SPRD_SC_GATE_CLK(gpu_mtx_eb,	"gpu-mtx-eb",	"ahb-disp", 0x0,
		     0x1000, BIT(13), 0, 0);
static SPRD_SC_GATE_CLK(gsp_mtx_eb,	"gsp-mtx-eb",	"ahb-disp", 0x0,
		     0x1000, BIT(14), 0, 0);
static SPRD_SC_GATE_CLK(tmc_mtx_eb,	"tmc-mtx-eb",	"ahb-disp", 0x0,
		     0x1000, BIT(15), 0, 0);
static SPRD_SC_GATE_CLK(dispc_mtx_eb,	"dispc-mtx-eb",	"ahb-disp", 0x0,
		     0x1000, BIT(16), 0, 0);
static SPRD_GATE_CLK(dphy0_gate,	"dphy0-gate",	"ahb-disp", 0x8,
		     BIT(0), 0, 0);
static SPRD_GATE_CLK(dphy1_gate,	"dphy1-gate",	"ahb-disp", 0x8,
		     BIT(1), 0, 0);
static SPRD_GATE_CLK(gsp0_a_gate,	"gsp0-a-gate",	"ahb-disp", 0x8,
		     BIT(2), 0, 0);
static SPRD_GATE_CLK(gsp1_a_gate,	"gsp1-a-gate",	"ahb-disp", 0x8,
		     BIT(3), 0, 0);
static SPRD_GATE_CLK(gsp0_f_gate,	"gsp0-f-gate",	"ahb-disp", 0x8,
		     BIT(4), 0, 0);
static SPRD_GATE_CLK(gsp1_f_gate,	"gsp1-f-gate",	"ahb-disp", 0x8,
		     BIT(5), 0, 0);
static SPRD_GATE_CLK(d_mtx_f_gate,	"d-mtx-f-gate",	"ahb-disp", 0x8,
		     BIT(6), 0, 0);
static SPRD_GATE_CLK(d_mtx_a_gate,	"d-mtx-a-gate",	"ahb-disp", 0x8,
		     BIT(7), 0, 0);
static SPRD_GATE_CLK(d_noc_f_gate,	"d-noc-f-gate",	"ahb-disp", 0x8,
		     BIT(8), 0, 0);
static SPRD_GATE_CLK(d_noc_a_gate,	"d-noc-a-gate",	"ahb-disp", 0x8,
		     BIT(9), 0, 0);
static SPRD_GATE_CLK(gsp_mtx_f_gate, "gsp-mtx-f-gate", "ahb-disp",  0x8,
		     BIT(10), 0, 0);
static SPRD_GATE_CLK(gsp_mtx_a_gate, "gsp-mtx-a-gate", "ahb-disp",  0x8,
		     BIT(11), 0, 0);
static SPRD_GATE_CLK(gsp_noc_f_gate, "gsp-noc-f-gate", "ahb-disp",  0x8,
		     BIT(12), 0, 0);
static SPRD_GATE_CLK(gsp_noc_a_gate, "gsp-noc-a-gate", "ahb-disp",  0x8,
		     BIT(13), 0, 0);
static SPRD_GATE_CLK(dispm0idle_gate, "dispm0idle-gate", "ahb-disp", 0x8,
		     BIT(14), 0, 0);
static SPRD_GATE_CLK(gspm0idle_gate, "gspm0idle-gate", "ahb-disp",  0x8,
		     BIT(15), 0, 0);

static struct sprd_clk_common *sc9860_disp_gate[] = {
	/* address base is 0x63100000 */
	&dispc0_eb.common,
	&dispc1_eb.common,
	&dispc_mmu_eb.common,
	&gsp0_eb.common,
	&gsp1_eb.common,
	&gsp0_mmu_eb.common,
	&gsp1_mmu_eb.common,
	&dsi0_eb.common,
	&dsi1_eb.common,
	&disp_ckg_eb.common,
	&disp_gpu_eb.common,
	&gpu_mtx_eb.common,
	&gsp_mtx_eb.common,
	&tmc_mtx_eb.common,
	&dispc_mtx_eb.common,
	&dphy0_gate.common,
	&dphy1_gate.common,
	&gsp0_a_gate.common,
	&gsp1_a_gate.common,
	&gsp0_f_gate.common,
	&gsp1_f_gate.common,
	&d_mtx_f_gate.common,
	&d_mtx_a_gate.common,
	&d_noc_f_gate.common,
	&d_noc_a_gate.common,
	&gsp_mtx_f_gate.common,
	&gsp_mtx_a_gate.common,
	&gsp_noc_f_gate.common,
	&gsp_noc_a_gate.common,
	&dispm0idle_gate.common,
	&gspm0idle_gate.common,
};

static struct clk_hw_onecell_data sc9860_disp_gate_hws = {
	.hws	= {
		[CLK_DISPC0_EB]		= &dispc0_eb.common.hw,
		[CLK_DISPC1_EB]		= &dispc1_eb.common.hw,
		[CLK_DISPC_MMU_EB]	= &dispc_mmu_eb.common.hw,
		[CLK_GSP0_EB]		= &gsp0_eb.common.hw,
		[CLK_GSP1_EB]		= &gsp1_eb.common.hw,
		[CLK_GSP0_MMU_EB]	= &gsp0_mmu_eb.common.hw,
		[CLK_GSP1_MMU_EB]	= &gsp1_mmu_eb.common.hw,
		[CLK_DSI0_EB]		= &dsi0_eb.common.hw,
		[CLK_DSI1_EB]		= &dsi1_eb.common.hw,
		[CLK_DISP_CKG_EB]	= &disp_ckg_eb.common.hw,
		[CLK_DISP_GPU_EB]	= &disp_gpu_eb.common.hw,
		[CLK_GPU_MTX_EB]	= &gpu_mtx_eb.common.hw,
		[CLK_GSP_MTX_EB]	= &gsp_mtx_eb.common.hw,
		[CLK_TMC_MTX_EB]	= &tmc_mtx_eb.common.hw,
		[CLK_DISPC_MTX_EB]	= &dispc_mtx_eb.common.hw,
		[CLK_DPHY0_GATE]	= &dphy0_gate.common.hw,
		[CLK_DPHY1_GATE]	= &dphy1_gate.common.hw,
		[CLK_GSP0_A_GATE]	= &gsp0_a_gate.common.hw,
		[CLK_GSP1_A_GATE]	= &gsp1_a_gate.common.hw,
		[CLK_GSP0_F_GATE]	= &gsp0_f_gate.common.hw,
		[CLK_GSP1_F_GATE]	= &gsp1_f_gate.common.hw,
		[CLK_D_MTX_F_GATE]	= &d_mtx_f_gate.common.hw,
		[CLK_D_MTX_A_GATE]	= &d_mtx_a_gate.common.hw,
		[CLK_D_NOC_F_GATE]	= &d_noc_f_gate.common.hw,
		[CLK_D_NOC_A_GATE]	= &d_noc_a_gate.common.hw,
		[CLK_GSP_MTX_F_GATE]	= &gsp_mtx_f_gate.common.hw,
		[CLK_GSP_MTX_A_GATE]	= &gsp_mtx_a_gate.common.hw,
		[CLK_GSP_NOC_F_GATE]	= &gsp_noc_f_gate.common.hw,
		[CLK_GSP_NOC_A_GATE]	= &gsp_noc_a_gate.common.hw,
		[CLK_DISPM0IDLE_GATE]	= &dispm0idle_gate.common.hw,
		[CLK_GSPM0IDLE_GATE]	= &gspm0idle_gate.common.hw,
	},
	.num	= CLK_DISP_GATE_NUM,
};

static const struct sprd_clk_desc sc9860_disp_gate_desc = {
	.clk_clks	= sc9860_disp_gate,
	.num_clk_clks	= ARRAY_SIZE(sc9860_disp_gate),
	.hw_clks	= &sc9860_disp_gate_hws,
};

static SPRD_SC_GATE_CLK(sim0_eb,	"sim0-eb",	"ap-apb", 0x0,
		     0x1000, BIT(0), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK(iis0_eb,	"iis0-eb",	"ap-apb", 0x0,
		     0x1000, BIT(1), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK(iis1_eb,	"iis1-eb",	"ap-apb", 0x0,
		     0x1000, BIT(2), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK(iis2_eb,	"iis2-eb",	"ap-apb", 0x0,
		     0x1000, BIT(3), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK(iis3_eb,	"iis3-eb",	"ap-apb", 0x0,
		     0x1000, BIT(4), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK(spi0_eb,	"spi0-eb",	"ap-apb", 0x0,
		     0x1000, BIT(5), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK(spi1_eb,	"spi1-eb",	"ap-apb", 0x0,
		     0x1000, BIT(6), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK(spi2_eb,	"spi2-eb",	"ap-apb", 0x0,
		     0x1000, BIT(7), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK(i2c0_eb,	"i2c0-eb",	"ap-apb", 0x0,
		     0x1000, BIT(8), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK(i2c1_eb,	"i2c1-eb",	"ap-apb", 0x0,
		     0x1000, BIT(9), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK(i2c2_eb,	"i2c2-eb",	"ap-apb", 0x0,
		     0x1000, BIT(10), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK(i2c3_eb,	"i2c3-eb",	"ap-apb", 0x0,
		     0x1000, BIT(11), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK(i2c4_eb,	"i2c4-eb",	"ap-apb", 0x0,
		     0x1000, BIT(12), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK(i2c5_eb,	"i2c5-eb",	"ap-apb", 0x0,
		     0x1000, BIT(13), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK(uart0_eb,	"uart0-eb",	"ap-apb", 0x0,
		     0x1000, BIT(14), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK(uart1_eb,	"uart1-eb",	"ap-apb", 0x0,
		     0x1000, BIT(15), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK(uart2_eb,	"uart2-eb",	"ap-apb", 0x0,
		     0x1000, BIT(16), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK(uart3_eb,	"uart3-eb",	"ap-apb", 0x0,
		     0x1000, BIT(17), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK(uart4_eb,	"uart4-eb",	"ap-apb", 0x0,
		     0x1000, BIT(18), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK(ap_ckg_eb,	"ap-ckg-eb",	"ap-apb", 0x0,
		     0x1000, BIT(19), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK(spi3_eb,	"spi3-eb",	"ap-apb", 0x0,
		     0x1000, BIT(20), CLK_IGNORE_UNUSED, 0);

static struct sprd_clk_common *sc9860_apapb_gate[] = {
	/* address base is 0x70b00000 */
	&sim0_eb.common,
	&iis0_eb.common,
	&iis1_eb.common,
	&iis2_eb.common,
	&iis3_eb.common,
	&spi0_eb.common,
	&spi1_eb.common,
	&spi2_eb.common,
	&i2c0_eb.common,
	&i2c1_eb.common,
	&i2c2_eb.common,
	&i2c3_eb.common,
	&i2c4_eb.common,
	&i2c5_eb.common,
	&uart0_eb.common,
	&uart1_eb.common,
	&uart2_eb.common,
	&uart3_eb.common,
	&uart4_eb.common,
	&ap_ckg_eb.common,
	&spi3_eb.common,
};

static struct clk_hw_onecell_data sc9860_apapb_gate_hws = {
	.hws	= {
		[CLK_SIM0_EB]		= &sim0_eb.common.hw,
		[CLK_IIS0_EB]		= &iis0_eb.common.hw,
		[CLK_IIS1_EB]		= &iis1_eb.common.hw,
		[CLK_IIS2_EB]		= &iis2_eb.common.hw,
		[CLK_IIS3_EB]		= &iis3_eb.common.hw,
		[CLK_SPI0_EB]		= &spi0_eb.common.hw,
		[CLK_SPI1_EB]		= &spi1_eb.common.hw,
		[CLK_SPI2_EB]		= &spi2_eb.common.hw,
		[CLK_I2C0_EB]		= &i2c0_eb.common.hw,
		[CLK_I2C1_EB]		= &i2c1_eb.common.hw,
		[CLK_I2C2_EB]		= &i2c2_eb.common.hw,
		[CLK_I2C3_EB]		= &i2c3_eb.common.hw,
		[CLK_I2C4_EB]		= &i2c4_eb.common.hw,
		[CLK_I2C5_EB]		= &i2c5_eb.common.hw,
		[CLK_UART0_EB]		= &uart0_eb.common.hw,
		[CLK_UART1_EB]		= &uart1_eb.common.hw,
		[CLK_UART2_EB]		= &uart2_eb.common.hw,
		[CLK_UART3_EB]		= &uart3_eb.common.hw,
		[CLK_UART4_EB]		= &uart4_eb.common.hw,
		[CLK_AP_CKG_EB]		= &ap_ckg_eb.common.hw,
		[CLK_SPI3_EB]		= &spi3_eb.common.hw,
	},
	.num	= CLK_APAPB_GATE_NUM,
};

static const struct sprd_clk_desc sc9860_apapb_gate_desc = {
	.clk_clks	= sc9860_apapb_gate,
	.num_clk_clks	= ARRAY_SIZE(sc9860_apapb_gate),
	.hw_clks	= &sc9860_apapb_gate_hws,
};

static const struct of_device_id sprd_sc9860_clk_ids[] = {
	{ .compatible = "sprd,sc9860-pmu-gate",		/* 0x402b */
	  .data = &sc9860_pmu_gate_desc },
	{ .compatible = "sprd,sc9860-pll",		/* 0x4040 */
	  .data = &sc9860_pll_desc },
	{ .compatible = "sprd,sc9860-ap-clk",		/* 0x2000 */
	  .data = &sc9860_ap_clk_desc },
	{ .compatible = "sprd,sc9860-aon-prediv",	/* 0x402d */
	  .data = &sc9860_aon_prediv_desc },
	{ .compatible = "sprd,sc9860-apahb-gate",	/* 0x2021 */
	  .data = &sc9860_apahb_gate_desc },
	{ .compatible = "sprd,sc9860-aon-gate",		/* 0x402e */
	  .data = &sc9860_aon_gate_desc },
	{ .compatible = "sprd,sc9860-aonsecure-clk",	/* 0x4088 */
	  .data = &sc9860_aonsecure_clk_desc },
	{ .compatible = "sprd,sc9860-agcp-gate",	/* 0x415e */
	  .data = &sc9860_agcp_gate_desc },
	{ .compatible = "sprd,sc9860-gpu-clk",		/* 0x6020 */
	  .data = &sc9860_gpu_clk_desc },
	{ .compatible = "sprd,sc9860-vsp-clk",		/* 0x6100 */
	  .data = &sc9860_vsp_clk_desc },
	{ .compatible = "sprd,sc9860-vsp-gate",		/* 0x6110 */
	  .data = &sc9860_vsp_gate_desc },
	{ .compatible = "sprd,sc9860-cam-clk",		/* 0x6200 */
	  .data = &sc9860_cam_clk_desc },
	{ .compatible = "sprd,sc9860-cam-gate",		/* 0x6210 */
	  .data = &sc9860_cam_gate_desc },
	{ .compatible = "sprd,sc9860-disp-clk",		/* 0x6300 */
	  .data = &sc9860_disp_clk_desc },
	{ .compatible = "sprd,sc9860-disp-gate",	/* 0x6310 */
	  .data = &sc9860_disp_gate_desc },
	{ .compatible = "sprd,sc9860-apapb-gate",	/* 0x70b0 */
	  .data = &sc9860_apapb_gate_desc },
	{ }
};
MODULE_DEVICE_TABLE(of, sprd_sc9860_clk_ids);

static int sc9860_clk_probe(struct platform_device *pdev)
{
	const struct of_device_id *match;
	const struct sprd_clk_desc *desc;
	int ret;

	match = of_match_node(sprd_sc9860_clk_ids, pdev->dev.of_node);
	if (!match) {
		pr_err("%s: of_match_node() failed", __func__);
		return -ENODEV;
	}

	desc = match->data;
	ret = sprd_clk_regmap_init(pdev, desc);
	if (ret)
		return ret;

	return sprd_clk_probe(&pdev->dev, desc->hw_clks);
}

static struct platform_driver sc9860_clk_driver = {
	.probe	= sc9860_clk_probe,
	.driver	= {
		.name	= "sc9860-clk",
		.of_match_table	= sprd_sc9860_clk_ids,
	},
};
module_platform_driver(sc9860_clk_driver);

MODULE_DESCRIPTION("Spreadtrum SC9860 Clock Driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:sc9860-clk");

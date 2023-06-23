// SPDX-License-Identifier: GPL-2.0
/*
 * Unisoc UMS512 clock driver
 *
 * Copyright (C) 2022 Unisoc, Inc.
 * Author: Xiaolong Zhang <xiaolong.zhang@unisoc.com>
 */

#include <linux/clk-provider.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include <dt-bindings/clock/sprd,ums512-clk.h>

#include "common.h"
#include "composite.h"
#include "div.h"
#include "gate.h"
#include "mux.h"
#include "pll.h"

#define UMS512_MUX_FLAG	\
	(CLK_GET_RATE_NOCACHE | CLK_SET_RATE_NO_REPARENT)

/* pll gate clock */
/* some pll clocks configure CLK_IGNORE_UNUSED because hw dvfs does not call
 * clock interface. hw dvfs can not gate the pll clock.
 */
static CLK_FIXED_FACTOR_FW_NAME(clk_26m_aud, "clk-26m-aud", "ext-26m", 1, 1, 0);
static CLK_FIXED_FACTOR_FW_NAME(clk_13m, "clk-13m", "ext-26m", 2, 1, 0);
static CLK_FIXED_FACTOR_FW_NAME(clk_6m5, "clk-6m5", "ext-26m", 4, 1, 0);
static CLK_FIXED_FACTOR_FW_NAME(clk_4m3, "clk-4m3", "ext-26m", 6, 1, 0);
static CLK_FIXED_FACTOR_FW_NAME(clk_2m, "clk-2m", "ext-26m", 13, 1, 0);
static CLK_FIXED_FACTOR_FW_NAME(clk_1m, "clk-1m", "ext-26m", 26, 1, 0);
static CLK_FIXED_FACTOR_FW_NAME(clk_250k, "clk-250k", "ext-26m", 104, 1, 0);
static CLK_FIXED_FACTOR_FW_NAME(rco_25m, "rco-25m", "rco-100m", 4, 1, 0);
static CLK_FIXED_FACTOR_FW_NAME(rco_4m, "rco-4m", "rco-100m", 25, 1, 0);
static CLK_FIXED_FACTOR_FW_NAME(rco_2m, "rco-2m", "rco-100m", 50, 1, 0);
static SPRD_PLL_SC_GATE_CLK_FW_NAME(isppll_gate, "isppll-gate", "ext-26m", 0x8c,
				    0x1000, BIT(0), CLK_IGNORE_UNUSED, 0, 240);
static SPRD_PLL_SC_GATE_CLK_FW_NAME(dpll0_gate, "dpll0-gate", "ext-26m", 0x98,
				    0x1000, BIT(0), 0, 0, 240);
static SPRD_PLL_SC_GATE_CLK_FW_NAME(dpll1_gate, "dpll1-gate", "ext-26m", 0x9c,
				    0x1000, BIT(0), 0, 0, 240);
static SPRD_PLL_SC_GATE_CLK_FW_NAME(lpll_gate, "lpll-gate", "ext-26m", 0xa0,
				    0x1000, BIT(0), CLK_IGNORE_UNUSED, 0, 240);
static SPRD_PLL_SC_GATE_CLK_FW_NAME(twpll_gate, "twpll-gate", "ext-26m", 0xa4,
				    0x1000, BIT(0), CLK_IGNORE_UNUSED, 0, 240);
static SPRD_PLL_SC_GATE_CLK_FW_NAME(gpll_gate, "gpll-gate", "ext-26m", 0xa8,
				    0x1000, BIT(0), CLK_IGNORE_UNUSED, 0, 240);
static SPRD_PLL_SC_GATE_CLK_FW_NAME(rpll_gate, "rpll-gate", "ext-26m", 0xac,
				    0x1000, BIT(0), CLK_IGNORE_UNUSED, 0, 240);
static SPRD_PLL_SC_GATE_CLK_FW_NAME(cppll_gate, "cppll-gate", "ext-26m", 0xe4,
				    0x1000, BIT(0), CLK_IGNORE_UNUSED, 0, 240);
static SPRD_PLL_SC_GATE_CLK_FW_NAME(mpll0_gate, "mpll0-gate", "ext-26m", 0x190,
				    0x1000, BIT(0), CLK_IGNORE_UNUSED, 0, 240);
static SPRD_PLL_SC_GATE_CLK_FW_NAME(mpll1_gate, "mpll1-gate", "ext-26m", 0x194,
				    0x1000, BIT(0), CLK_IGNORE_UNUSED, 0, 240);
static SPRD_PLL_SC_GATE_CLK_FW_NAME(mpll2_gate, "mpll2-gate", "ext-26m", 0x198,
				    0x1000, BIT(0), CLK_IGNORE_UNUSED, 0, 240);

static struct sprd_clk_common *ums512_pmu_gate_clks[] = {
	/* address base is 0x327e0000 */
	&isppll_gate.common,
	&dpll0_gate.common,
	&dpll1_gate.common,
	&lpll_gate.common,
	&twpll_gate.common,
	&gpll_gate.common,
	&rpll_gate.common,
	&cppll_gate.common,
	&mpll0_gate.common,
	&mpll1_gate.common,
	&mpll2_gate.common,
};

static struct clk_hw_onecell_data ums512_pmu_gate_hws = {
	.hws	= {
		[CLK_26M_AUD]		= &clk_26m_aud.hw,
		[CLK_13M]		= &clk_13m.hw,
		[CLK_6M5]		= &clk_6m5.hw,
		[CLK_4M3]		= &clk_4m3.hw,
		[CLK_2M]		= &clk_2m.hw,
		[CLK_1M]		= &clk_1m.hw,
		[CLK_250K]		= &clk_250k.hw,
		[CLK_RCO_25M]		= &rco_25m.hw,
		[CLK_RCO_4M]		= &rco_4m.hw,
		[CLK_RCO_2M]		= &rco_2m.hw,
		[CLK_ISPPLL_GATE]	= &isppll_gate.common.hw,
		[CLK_DPLL0_GATE]	= &dpll0_gate.common.hw,
		[CLK_DPLL1_GATE]	= &dpll1_gate.common.hw,
		[CLK_LPLL_GATE]		= &lpll_gate.common.hw,
		[CLK_TWPLL_GATE]	= &twpll_gate.common.hw,
		[CLK_GPLL_GATE]		= &gpll_gate.common.hw,
		[CLK_RPLL_GATE]		= &rpll_gate.common.hw,
		[CLK_CPPLL_GATE]	= &cppll_gate.common.hw,
		[CLK_MPLL0_GATE]	= &mpll0_gate.common.hw,
		[CLK_MPLL1_GATE]	= &mpll1_gate.common.hw,
		[CLK_MPLL2_GATE]	= &mpll2_gate.common.hw,
	},
	.num = CLK_PMU_GATE_NUM,
};

static struct sprd_clk_desc ums512_pmu_gate_desc = {
	.clk_clks	= ums512_pmu_gate_clks,
	.num_clk_clks	= ARRAY_SIZE(ums512_pmu_gate_clks),
	.hw_clks        = &ums512_pmu_gate_hws,
};

/* pll clock at g0 */
static const u64 itable_dpll0[7] = { 6, 0, 0,
			1173000000ULL, 1475000000ULL,
			1855000000ULL, 1866000000ULL };

static struct clk_bit_field f_dpll0[PLL_FACT_MAX] = {
	{ .shift = 18,	.width = 1 },	/* lock_done	*/
	{ .shift = 0,	.width = 1 },	/* div_s	*/
	{ .shift = 67,	.width = 1 },	/* mod_en	*/
	{ .shift = 1,	.width = 1 },	/* sdm_en	*/
	{ .shift = 0,	.width = 0 },	/* refin	*/
	{ .shift = 4,	.width = 3 },	/* icp		*/
	{ .shift = 7,	.width = 11 },	/* n		*/
	{ .shift = 55,	.width = 7 },	/* nint		*/
	{ .shift = 32,	.width = 23},	/* kint		*/
	{ .shift = 0,	.width = 0 },	/* prediv	*/
	{ .shift = 0,	.width = 0 },	/* postdiv	*/
};
static SPRD_PLL_HW(dpll0, "dpll0", &dpll0_gate.common.hw, 0x4, 3,
		   itable_dpll0, f_dpll0, 240, 1000, 1000, 0, 0);
static CLK_FIXED_FACTOR_HW(dpll0_58m31, "dpll0-58m31", &dpll0.common.hw,
			   32, 1, 0);

static struct sprd_clk_common *ums512_g0_pll_clks[] = {
	/* address base is 0x32390000 */
	&dpll0.common,
};

static struct clk_hw_onecell_data ums512_g0_pll_hws = {
	.hws	= {
		[CLK_DPLL0]		= &dpll0.common.hw,
		[CLK_DPLL0_58M31]	= &dpll0_58m31.hw,
	},
	.num	= CLK_ANLG_PHY_G0_NUM,
};

static struct sprd_clk_desc ums512_g0_pll_desc = {
	.clk_clks	= ums512_g0_pll_clks,
	.num_clk_clks	= ARRAY_SIZE(ums512_g0_pll_clks),
	.hw_clks	= &ums512_g0_pll_hws,
};

/* pll clock at g2 */
static const u64 itable_mpll[8] = { 7, 0,
			1400000000ULL, 1600000000ULL,
			1800000000ULL, 2000000000ULL,
			2200000000ULL, 2500000000ULL };

static struct clk_bit_field f_mpll[PLL_FACT_MAX] = {
	{ .shift = 17,	.width = 1 },	/* lock_done	*/
	{ .shift = 0,	.width = 1 },	/* div_s	*/
	{ .shift = 67,	.width = 1 },	/* mod_en	*/
	{ .shift = 1,	.width = 1 },	/* sdm_en	*/
	{ .shift = 0,	.width = 0 },	/* refin	*/
	{ .shift = 2,	.width = 3 },	/* icp		*/
	{ .shift = 5,	.width = 11 },	/* n		*/
	{ .shift = 55,	.width = 7 },	/* nint		*/
	{ .shift = 32,	.width = 23},	/* kint		*/
	{ .shift = 0,	.width = 0 },	/* prediv	*/
	{ .shift = 77,	.width = 1 },	/* postdiv	*/
};
static SPRD_PLL_HW(mpll1, "mpll1", &mpll1_gate.common.hw, 0x0, 3,
		   itable_mpll, f_mpll, 240, 1000, 1000, 1, 1200000000);
static CLK_FIXED_FACTOR_HW(mpll1_63m38, "mpll1-63m38", &mpll1.common.hw,
			   32, 1, 0);

static struct sprd_clk_common *ums512_g2_pll_clks[] = {
	/* address base is 0x323B0000 */
	&mpll1.common,
};

static struct clk_hw_onecell_data ums512_g2_pll_hws = {
	.hws	= {
		[CLK_MPLL1]		= &mpll1.common.hw,
		[CLK_MPLL1_63M38]	= &mpll1_63m38.hw,
	},
	.num	= CLK_ANLG_PHY_G2_NUM,
};

static struct sprd_clk_desc ums512_g2_pll_desc = {
	.clk_clks	= ums512_g2_pll_clks,
	.num_clk_clks	= ARRAY_SIZE(ums512_g2_pll_clks),
	.hw_clks	= &ums512_g2_pll_hws,
};

/* pll at g3 */
static const u64 itable[8] = { 7, 0, 0,
			900000000ULL, 1100000000ULL,
			1300000000ULL, 1500000000ULL,
			1600000000ULL };

static struct clk_bit_field f_pll[PLL_FACT_MAX] = {
	{ .shift = 18,	.width = 1 },	/* lock_done	*/
	{ .shift = 0,	.width = 1 },	/* div_s	*/
	{ .shift = 67,	.width = 1 },	/* mod_en	*/
	{ .shift = 1,	.width = 1 },	/* sdm_en	*/
	{ .shift = 0,	.width = 0 },	/* refin	*/
	{ .shift = 2,	.width = 3 },	/* icp		*/
	{ .shift = 5,	.width = 11 },	/* n		*/
	{ .shift = 55,	.width = 7 },	/* nint		*/
	{ .shift = 32,	.width = 23},	/* kint		*/
	{ .shift = 0,	.width = 0 },	/* prediv	*/
	{ .shift = 77,	.width = 1 },	/* postdiv	*/
};

static SPRD_PLL_FW_NAME(rpll, "rpll", "ext-26m", 0x0, 3,
			itable, f_pll, 240, 1000, 1000, 1, 750000000);

static SPRD_SC_GATE_CLK_FW_NAME(audio_gate, "audio-gate", "ext-26m", 0x24,
				0x1000, BIT(1), CLK_IGNORE_UNUSED, 0);

static struct clk_bit_field f_mpll2[PLL_FACT_MAX] = {
	{ .shift = 16,	.width = 1 },	/* lock_done	*/
	{ .shift = 0,	.width = 1 },	/* div_s	*/
	{ .shift = 67,	.width = 1 },	/* mod_en	*/
	{ .shift = 1,	.width = 1 },	/* sdm_en	*/
	{ .shift = 0,	.width = 0 },	/* refin	*/
	{ .shift = 2,	.width = 3 },	/* icp		*/
	{ .shift = 5,	.width = 11 },	/* n		*/
	{ .shift = 55,	.width = 7 },	/* nint		*/
	{ .shift = 32,	.width = 23},	/* kint		*/
	{ .shift = 0,	.width = 0 },	/* prediv	*/
	{ .shift = 77,	.width = 1 },	/* postdiv	*/
};
static SPRD_PLL_HW(mpll0, "mpll0", &mpll0_gate.common.hw, 0x54, 3,
		   itable_mpll, f_mpll, 240, 1000, 1000, 1, 1200000000);
static CLK_FIXED_FACTOR_HW(mpll0_56m88, "mpll0-56m88", &mpll0.common.hw,
			   32, 1, 0);

static const u64 itable_mpll2[6] = { 5,
			1200000000ULL, 1400000000ULL,
			1600000000ULL, 1800000000ULL,
			2000000000ULL };

static SPRD_PLL_HW(mpll2, "mpll2", &mpll2_gate.common.hw, 0x9c, 3,
		   itable_mpll2, f_mpll2, 240, 1000, 1000, 1, 1000000000);
static CLK_FIXED_FACTOR_HW(mpll2_47m13, "mpll2-47m13", &mpll2.common.hw,
			   32, 1, 0);

static struct sprd_clk_common *ums512_g3_pll_clks[] = {
	/* address base is 0x323c0000 */
	&rpll.common,
	&audio_gate.common,
	&mpll0.common,
	&mpll2.common,
};

static struct clk_hw_onecell_data ums512_g3_pll_hws = {
	.hws	= {
		[CLK_RPLL]		= &rpll.common.hw,
		[CLK_AUDIO_GATE]	= &audio_gate.common.hw,
		[CLK_MPLL0]		= &mpll0.common.hw,
		[CLK_MPLL0_56M88]	= &mpll0_56m88.hw,
		[CLK_MPLL2]		= &mpll2.common.hw,
		[CLK_MPLL2_47M13]	= &mpll2_47m13.hw,
	},
	.num	= CLK_ANLG_PHY_G3_NUM,
};

static struct sprd_clk_desc ums512_g3_pll_desc = {
	.clk_clks	= ums512_g3_pll_clks,
	.num_clk_clks	= ARRAY_SIZE(ums512_g3_pll_clks),
	.hw_clks	= &ums512_g3_pll_hws,
};

/* pll clock at gc */
static SPRD_PLL_FW_NAME(twpll, "twpll", "ext-26m", 0x0, 3,
			itable, f_pll, 240, 1000, 1000, 1, 750000000);
static CLK_FIXED_FACTOR_HW(twpll_768m, "twpll-768m", &twpll.common.hw,
			   2, 1, 0);
static CLK_FIXED_FACTOR_HW(twpll_384m, "twpll-384m", &twpll.common.hw,
			   4, 1, 0);
static CLK_FIXED_FACTOR_HW(twpll_192m, "twpll-192m", &twpll.common.hw,
			   8, 1, 0);
static CLK_FIXED_FACTOR_HW(twpll_96m, "twpll-96m", &twpll.common.hw,
			   16, 1, 0);
static CLK_FIXED_FACTOR_HW(twpll_48m, "twpll-48m", &twpll.common.hw,
			   32, 1, 0);
static CLK_FIXED_FACTOR_HW(twpll_24m, "twpll-24m", &twpll.common.hw,
			   64, 1, 0);
static CLK_FIXED_FACTOR_HW(twpll_12m, "twpll-12m", &twpll.common.hw,
			   128, 1, 0);
static CLK_FIXED_FACTOR_HW(twpll_512m, "twpll-512m", &twpll.common.hw,
			   3, 1, 0);
static CLK_FIXED_FACTOR_HW(twpll_256m, "twpll-256m", &twpll.common.hw,
			   6, 1, 0);
static CLK_FIXED_FACTOR_HW(twpll_128m, "twpll-128m", &twpll.common.hw,
			   12, 1, 0);
static CLK_FIXED_FACTOR_HW(twpll_64m, "twpll-64m", &twpll.common.hw,
			   24, 1, 0);
static CLK_FIXED_FACTOR_HW(twpll_307m2, "twpll-307m2", &twpll.common.hw,
			   5, 1, 0);
static CLK_FIXED_FACTOR_HW(twpll_219m4, "twpll-219m4", &twpll.common.hw,
			   7, 1, 0);
static CLK_FIXED_FACTOR_HW(twpll_170m6, "twpll-170m6", &twpll.common.hw,
			   9, 1, 0);
static CLK_FIXED_FACTOR_HW(twpll_153m6, "twpll-153m6", &twpll.common.hw,
			   10, 1, 0);
static CLK_FIXED_FACTOR_HW(twpll_76m8, "twpll-76m8", &twpll.common.hw,
			   20, 1, 0);
static CLK_FIXED_FACTOR_HW(twpll_51m2, "twpll-51m2", &twpll.common.hw,
			   30, 1, 0);
static CLK_FIXED_FACTOR_HW(twpll_38m4, "twpll-38m4", &twpll.common.hw,
			   40, 1, 0);
static CLK_FIXED_FACTOR_HW(twpll_19m2, "twpll-19m2", &twpll.common.hw,
			   80, 1, 0);
static CLK_FIXED_FACTOR_HW(twpll_12m29, "twpll-12m29", &twpll.common.hw,
			   125, 1, 0);

static SPRD_PLL_FW_NAME(lpll, "lpll", "ext-26m", 0x18, 3,
			itable, f_pll, 240, 1000, 1000, 1, 750000000);
static CLK_FIXED_FACTOR_HW(lpll_614m4, "lpll-614m4", &lpll.common.hw,
			   2, 1, 0);
static CLK_FIXED_FACTOR_HW(lpll_409m6, "lpll-409m6", &lpll.common.hw,
			   3, 1, 0);
static CLK_FIXED_FACTOR_HW(lpll_245m76, "lpll-245m76", &lpll.common.hw,
			   5, 1, 0);
static CLK_FIXED_FACTOR_HW(lpll_30m72, "lpll-30m72", &lpll.common.hw,
			   40, 1, 0);

static SPRD_PLL_FW_NAME(isppll, "isppll", "ext-26m", 0x30, 3,
			itable, f_pll, 240, 1000, 1000, 1, 750000000);
static CLK_FIXED_FACTOR_HW(isppll_468m, "isppll-468m", &isppll.common.hw,
			   2, 1, 0);
static CLK_FIXED_FACTOR_HW(isppll_78m, "isppll-78m", &isppll.common.hw,
			   12, 1, 0);

static SPRD_PLL_HW(gpll, "gpll", &gpll_gate.common.hw, 0x48, 3,
		   itable, f_pll, 240, 1000, 1000, 1, 750000000);
static CLK_FIXED_FACTOR_HW(gpll_40m, "gpll-40m", &gpll.common.hw,
			   20, 1, 0);

static SPRD_PLL_HW(cppll, "cppll", &cppll_gate.common.hw, 0x60, 3,
		   itable, f_pll, 240, 1000, 1000, 1, 750000000);
static CLK_FIXED_FACTOR_HW(cppll_39m32, "cppll-39m32", &cppll.common.hw,
			   26, 1, 0);

static struct sprd_clk_common *ums512_gc_pll_clks[] = {
	/* address base is 0x323e0000 */
	&twpll.common,
	&lpll.common,
	&isppll.common,
	&gpll.common,
	&cppll.common,
};

static struct clk_hw_onecell_data ums512_gc_pll_hws = {
	.hws	= {
		[CLK_TWPLL]		= &twpll.common.hw,
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
		[CLK_TWPLL_219M4]	= &twpll_219m4.hw,
		[CLK_TWPLL_170M6]	= &twpll_170m6.hw,
		[CLK_TWPLL_153M6]	= &twpll_153m6.hw,
		[CLK_TWPLL_76M8]	= &twpll_76m8.hw,
		[CLK_TWPLL_51M2]	= &twpll_51m2.hw,
		[CLK_TWPLL_38M4]	= &twpll_38m4.hw,
		[CLK_TWPLL_19M2]	= &twpll_19m2.hw,
		[CLK_TWPLL_12M29]	= &twpll_12m29.hw,
		[CLK_LPLL]		= &lpll.common.hw,
		[CLK_LPLL_614M4]	= &lpll_614m4.hw,
		[CLK_LPLL_409M6]	= &lpll_409m6.hw,
		[CLK_LPLL_245M76]	= &lpll_245m76.hw,
		[CLK_LPLL_30M72]	= &lpll_30m72.hw,
		[CLK_ISPPLL]		= &isppll.common.hw,
		[CLK_ISPPLL_468M]	= &isppll_468m.hw,
		[CLK_ISPPLL_78M]	= &isppll_78m.hw,
		[CLK_GPLL]		= &gpll.common.hw,
		[CLK_GPLL_40M]		= &gpll_40m.hw,
		[CLK_CPPLL]		= &cppll.common.hw,
		[CLK_CPPLL_39M32]	= &cppll_39m32.hw,
	},
	.num	= CLK_ANLG_PHY_GC_NUM,
};

static struct sprd_clk_desc ums512_gc_pll_desc = {
	.clk_clks	= ums512_gc_pll_clks,
	.num_clk_clks	= ARRAY_SIZE(ums512_gc_pll_clks),
	.hw_clks	= &ums512_gc_pll_hws,
};

/* ap ahb gates */
static SPRD_SC_GATE_CLK_FW_NAME(dsi_eb, "dsi-eb", "ext-26m",
				0x0, 0x1000, BIT(0), 0, 0);
static SPRD_SC_GATE_CLK_FW_NAME(dispc_eb, "dispc-eb", "ext-26m",
				0x0, 0x1000, BIT(1), 0, 0);
static SPRD_SC_GATE_CLK_FW_NAME(vsp_eb, "vsp-eb", "ext-26m",
				0x0, 0x1000, BIT(2), 0, 0);
static SPRD_SC_GATE_CLK_FW_NAME(vdma_eb, "vdma-eb", "ext-26m",
				0x0, 0x1000, BIT(3), 0, 0);
static SPRD_SC_GATE_CLK_FW_NAME(dma_pub_eb, "dma-pub-eb", "ext-26m",
				0x0, 0x1000, BIT(4), 0, 0);
static SPRD_SC_GATE_CLK_FW_NAME(dma_sec_eb, "dma-sec-eb", "ext-26m",
				0x0, 0x1000, BIT(5), 0, 0);
static SPRD_SC_GATE_CLK_FW_NAME(ipi_eb, "ipi-eb", "ext-26m",
				0x0, 0x1000, BIT(6), 0, 0);
static SPRD_SC_GATE_CLK_FW_NAME(ahb_ckg_eb, "ahb-ckg-eb", "ext-26m",
				0x0, 0x1000, BIT(7), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK_FW_NAME(bm_clk_eb, "bm-clk-eb", "ext-26m",
				0x0, 0x1000, BIT(8), CLK_IGNORE_UNUSED, 0);

static struct sprd_clk_common *ums512_apahb_gate[] = {
	/* address base is 0x20100000 */
	&dsi_eb.common,
	&dispc_eb.common,
	&vsp_eb.common,
	&vdma_eb.common,
	&dma_pub_eb.common,
	&dma_sec_eb.common,
	&ipi_eb.common,
	&ahb_ckg_eb.common,
	&bm_clk_eb.common,
};

static struct clk_hw_onecell_data ums512_apahb_gate_hws = {
	.hws	= {
		[CLK_DSI_EB]		= &dsi_eb.common.hw,
		[CLK_DISPC_EB]		= &dispc_eb.common.hw,
		[CLK_VSP_EB]		= &vsp_eb.common.hw,
		[CLK_VDMA_EB]		= &vdma_eb.common.hw,
		[CLK_DMA_PUB_EB]	= &dma_pub_eb.common.hw,
		[CLK_DMA_SEC_EB]	= &dma_sec_eb.common.hw,
		[CLK_IPI_EB]		= &ipi_eb.common.hw,
		[CLK_AHB_CKG_EB]	= &ahb_ckg_eb.common.hw,
		[CLK_BM_CLK_EB]		= &bm_clk_eb.common.hw,
	},
	.num	= CLK_AP_AHB_GATE_NUM,
};

static struct sprd_clk_desc ums512_apahb_gate_desc = {
	.clk_clks	= ums512_apahb_gate,
	.num_clk_clks	= ARRAY_SIZE(ums512_apahb_gate),
	.hw_clks	= &ums512_apahb_gate_hws,
};

/* ap clks */
static const struct clk_parent_data ap_apb_parents[] = {
	{ .fw_name = "ext-26m" },
	{ .hw = &twpll_64m.hw  },
	{ .hw = &twpll_96m.hw  },
	{ .hw = &twpll_128m.hw  },
};
static SPRD_MUX_CLK_DATA(ap_apb_clk, "ap-apb-clk", ap_apb_parents,
			 0x20, 0, 2, UMS512_MUX_FLAG);

static const struct clk_parent_data ipi_parents[] = {
	{ .fw_name = "ext-26m" },
	{ .hw = &twpll_64m.hw  },
	{ .hw = &twpll_96m.hw  },
	{ .hw = &twpll_128m.hw  },
};
static SPRD_MUX_CLK_DATA(ipi_clk, "ipi-clk", ipi_parents,
			 0x24, 0, 2, UMS512_MUX_FLAG);

static const struct clk_parent_data ap_uart_parents[] = {
	{ .fw_name = "ext-26m" },
	{ .hw = &twpll_48m.hw  },
	{ .hw = &twpll_51m2.hw  },
	{ .hw = &twpll_96m.hw  },
};
static SPRD_COMP_CLK_DATA(ap_uart0_clk, "ap-uart0-clk", ap_uart_parents,
			  0x28, 0, 2, 8, 3, 0);
static SPRD_COMP_CLK_DATA(ap_uart1_clk, "ap-uart1-clk", ap_uart_parents,
			  0x2c, 0, 2, 8, 3, 0);
static SPRD_COMP_CLK_DATA(ap_uart2_clk, "ap-uart2-clk", ap_uart_parents,
			  0x30, 0, 2, 8, 3, 0);

static const struct clk_parent_data i2c_parents[] = {
	{ .fw_name = "ext-26m" },
	{ .hw = &twpll_48m.hw  },
	{ .hw = &twpll_51m2.hw  },
	{ .hw = &twpll_153m6.hw  },
};
static SPRD_COMP_CLK_DATA(ap_i2c0_clk, "ap-i2c0-clk", i2c_parents,
			  0x34, 0, 2, 8, 3, 0);
static SPRD_COMP_CLK_DATA(ap_i2c1_clk, "ap-i2c1-clk", i2c_parents,
			  0x38, 0, 2, 8, 3, 0);
static SPRD_COMP_CLK_DATA(ap_i2c2_clk, "ap-i2c2-clk", i2c_parents,
			  0x3c, 0, 2, 8, 3, 0);
static SPRD_COMP_CLK_DATA(ap_i2c3_clk, "ap-i2c3-clk", i2c_parents,
			  0x40, 0, 2, 8, 3, 0);
static SPRD_COMP_CLK_DATA(ap_i2c4_clk, "ap-i2c4-clk", i2c_parents,
			  0x44, 0, 2, 8, 3, 0);

static const struct clk_parent_data spi_parents[] = {
	{ .fw_name = "ext-26m" },
	{ .hw = &twpll_128m.hw  },
	{ .hw = &twpll_153m6.hw  },
	{ .hw = &twpll_192m.hw  },
};
static SPRD_COMP_CLK_DATA(ap_spi0_clk, "ap-spi0-clk", spi_parents,
			  0x48, 0, 2, 8, 3, 0);
static SPRD_COMP_CLK_DATA(ap_spi1_clk, "ap-spi1-clk", spi_parents,
			  0x4c, 0, 2, 8, 3, 0);
static SPRD_COMP_CLK_DATA(ap_spi2_clk, "ap-spi2-clk", spi_parents,
			  0x50, 0, 2, 8, 3, 0);
static SPRD_COMP_CLK_DATA(ap_spi3_clk, "ap-spi3-clk", spi_parents,
			  0x54, 0, 2, 8, 3, 0);

static const struct clk_parent_data iis_parents[] = {
	{ .fw_name = "ext-26m" },
	{ .hw = &twpll_128m.hw  },
	{ .hw = &twpll_153m6.hw  },
};
static SPRD_COMP_CLK_DATA(ap_iis0_clk, "ap-iis0-clk", iis_parents,
			  0x58, 0, 2, 8, 3, 0);
static SPRD_COMP_CLK_DATA(ap_iis1_clk, "ap-iis1-clk", iis_parents,
			  0x5c, 0, 2, 8, 3, 0);
static SPRD_COMP_CLK_DATA(ap_iis2_clk, "ap-iis2-clk", iis_parents,
			  0x60, 0, 2, 8, 3, 0);

static const struct clk_parent_data sim_parents[] = {
	{ .fw_name = "ext-26m" },
	{ .hw = &twpll_51m2.hw  },
	{ .hw = &twpll_64m.hw  },
	{ .hw = &twpll_96m.hw  },
	{ .hw = &twpll_128m.hw  },
};
static SPRD_COMP_CLK_DATA(ap_sim_clk, "ap-sim-clk", sim_parents,
			  0x64, 0, 3, 8, 3, 0);

static const struct clk_parent_data ap_ce_parents[] = {
	{ .fw_name = "ext-26m" },
	{ .hw = &twpll_96m.hw  },
	{ .hw = &twpll_192m.hw  },
	{ .hw = &twpll_256m.hw  },
};
static SPRD_MUX_CLK_DATA(ap_ce_clk, "ap-ce-clk", ap_ce_parents,
			 0x68, 0, 2, UMS512_MUX_FLAG);

static const struct clk_parent_data sdio_parents[] = {
	{ .hw = &clk_1m.hw  },
	{ .fw_name = "ext-26m" },
	{ .hw = &twpll_307m2.hw  },
	{ .hw = &twpll_384m.hw  },
	{ .hw = &rpll.common.hw  },
	{ .hw = &lpll_409m6.hw  },
};
static SPRD_MUX_CLK_DATA(sdio0_2x_clk, "sdio0-2x", sdio_parents,
			 0x80, 0, 3, UMS512_MUX_FLAG);
static SPRD_MUX_CLK_DATA(sdio1_2x_clk, "sdio1-2x", sdio_parents,
			 0x88, 0, 3, UMS512_MUX_FLAG);
static SPRD_MUX_CLK_DATA(emmc_2x_clk, "emmc-2x", sdio_parents,
			 0x90, 0, 3, UMS512_MUX_FLAG);

static const struct clk_parent_data vsp_parents[] = {
	{ .hw = &twpll_256m.hw  },
	{ .hw = &twpll_307m2.hw  },
	{ .hw = &twpll_384m.hw  },
};
static SPRD_MUX_CLK_DATA(vsp_clk, "vsp-clk", vsp_parents,
			 0x98, 0, 2, UMS512_MUX_FLAG);

static const struct clk_parent_data dispc0_parents[] = {
	{ .hw = &twpll_153m6.hw  },
	{ .hw = &twpll_192m.hw  },
	{ .hw = &twpll_256m.hw  },
	{ .hw = &twpll_307m2.hw  },
	{ .hw = &twpll_384m.hw  },
};
static SPRD_MUX_CLK_DATA(dispc0_clk, "dispc0-clk", dispc0_parents,
			 0x9c, 0, 3, UMS512_MUX_FLAG);

static const struct clk_parent_data dispc0_dpi_parents[] = {
	{ .hw = &twpll_96m.hw  },
	{ .hw = &twpll_128m.hw  },
	{ .hw = &twpll_153m6.hw  },
	{ .hw = &twpll_192m.hw  },
};
static SPRD_COMP_CLK_DATA(dispc0_dpi_clk, "dispc0-dpi-clk", dispc0_dpi_parents,
			  0xa0, 0, 3, 8, 4, 0);

static const struct clk_parent_data dsi_apb_parents[] = {
	{ .hw = &twpll_96m.hw  },
	{ .hw = &twpll_128m.hw  },
	{ .hw = &twpll_153m6.hw  },
	{ .hw = &twpll_192m.hw  },
};
static SPRD_MUX_CLK_DATA(dsi_apb_clk, "dsi-apb-clk", dsi_apb_parents,
			 0xa4, 0, 2, UMS512_MUX_FLAG);

static SPRD_GATE_CLK_FW_NAME(dsi_rxesc, "dsi-rxesc", "ext-26m",
			     0xa8, BIT(16), 0, 0);

static SPRD_GATE_CLK_FW_NAME(dsi_lanebyte, "dsi-lanebyte", "ext-26m",
			     0xac, BIT(16), 0, 0);

static const struct clk_parent_data vdsp_parents[] = {
	{ .hw = &twpll_256m.hw  },
	{ .hw = &twpll_384m.hw  },
	{ .hw = &twpll_512m.hw  },
	{ .hw = &lpll_614m4.hw  },
	{ .hw = &twpll_768m.hw  },
	{ .hw = &isppll.common.hw  },
};
static SPRD_MUX_CLK_DATA(vdsp_clk, "vdsp-clk", vdsp_parents,
			 0xb0, 0, 3, UMS512_MUX_FLAG);
static SPRD_DIV_CLK_HW(vdsp_m_clk, "vdsp-m-clk", &vdsp_clk.common.hw,
		       0xb4, 8, 2, 0);

static struct sprd_clk_common *ums512_ap_clks[] = {
	/* address base is 0x20200000 */
	&ap_apb_clk.common,
	&ipi_clk.common,
	&ap_uart0_clk.common,
	&ap_uart1_clk.common,
	&ap_uart2_clk.common,
	&ap_i2c0_clk.common,
	&ap_i2c1_clk.common,
	&ap_i2c2_clk.common,
	&ap_i2c3_clk.common,
	&ap_i2c4_clk.common,
	&ap_spi0_clk.common,
	&ap_spi1_clk.common,
	&ap_spi2_clk.common,
	&ap_spi3_clk.common,
	&ap_iis0_clk.common,
	&ap_iis1_clk.common,
	&ap_iis2_clk.common,
	&ap_sim_clk.common,
	&ap_ce_clk.common,
	&sdio0_2x_clk.common,
	&sdio1_2x_clk.common,
	&emmc_2x_clk.common,
	&vsp_clk.common,
	&dispc0_clk.common,
	&dispc0_dpi_clk.common,
	&dsi_apb_clk.common,
	&dsi_rxesc.common,
	&dsi_lanebyte.common,
	&vdsp_clk.common,
	&vdsp_m_clk.common,

};

static struct clk_hw_onecell_data ums512_ap_clk_hws = {
	.hws	= {
		[CLK_AP_APB]		= &ap_apb_clk.common.hw,
		[CLK_IPI]		= &ipi_clk.common.hw,
		[CLK_AP_UART0]		= &ap_uart0_clk.common.hw,
		[CLK_AP_UART1]		= &ap_uart1_clk.common.hw,
		[CLK_AP_UART2]		= &ap_uart2_clk.common.hw,
		[CLK_AP_I2C0]		= &ap_i2c0_clk.common.hw,
		[CLK_AP_I2C1]		= &ap_i2c1_clk.common.hw,
		[CLK_AP_I2C2]		= &ap_i2c2_clk.common.hw,
		[CLK_AP_I2C3]		= &ap_i2c3_clk.common.hw,
		[CLK_AP_I2C4]		= &ap_i2c4_clk.common.hw,
		[CLK_AP_SPI0]		= &ap_spi0_clk.common.hw,
		[CLK_AP_SPI1]		= &ap_spi1_clk.common.hw,
		[CLK_AP_SPI2]		= &ap_spi2_clk.common.hw,
		[CLK_AP_SPI3]		= &ap_spi3_clk.common.hw,
		[CLK_AP_IIS0]		= &ap_iis0_clk.common.hw,
		[CLK_AP_IIS1]		= &ap_iis1_clk.common.hw,
		[CLK_AP_IIS2]		= &ap_iis2_clk.common.hw,
		[CLK_AP_SIM]		= &ap_sim_clk.common.hw,
		[CLK_AP_CE]		= &ap_ce_clk.common.hw,
		[CLK_SDIO0_2X]		= &sdio0_2x_clk.common.hw,
		[CLK_SDIO1_2X]		= &sdio1_2x_clk.common.hw,
		[CLK_EMMC_2X]		= &emmc_2x_clk.common.hw,
		[CLK_VSP]		= &vsp_clk.common.hw,
		[CLK_DISPC0]		= &dispc0_clk.common.hw,
		[CLK_DISPC0_DPI]	= &dispc0_dpi_clk.common.hw,
		[CLK_DSI_APB]		= &dsi_apb_clk.common.hw,
		[CLK_DSI_RXESC]		= &dsi_rxesc.common.hw,
		[CLK_DSI_LANEBYTE]	= &dsi_lanebyte.common.hw,
		[CLK_VDSP]		= &vdsp_clk.common.hw,
		[CLK_VDSP_M]		= &vdsp_m_clk.common.hw,
	},
	.num	= CLK_AP_CLK_NUM,
};

static struct sprd_clk_desc ums512_ap_clk_desc = {
	.clk_clks	= ums512_ap_clks,
	.num_clk_clks	= ARRAY_SIZE(ums512_ap_clks),
	.hw_clks	= &ums512_ap_clk_hws,
};

/* aon apb clks */
static const struct clk_parent_data aon_apb_parents[] = {
	{ .hw = &rco_4m.hw  },
	{ .fw_name = "ext-4m"  },
	{ .hw = &clk_13m.hw  },
	{ .hw = &rco_25m.hw  },
	{ .fw_name = "ext-26m" },
	{ .hw = &twpll_96m.hw  },
	{ .fw_name = "rco-100m" },
	{ .hw = &twpll_128m.hw  },
};
static SPRD_COMP_CLK_DATA(aon_apb_clk, "aon-apb-clk", aon_apb_parents,
			  0x220, 0, 3, 8, 2, 0);


static const struct clk_parent_data adi_parents[] = {
	{ .hw = &rco_4m.hw  },
	{ .fw_name = "ext-26m" },
	{ .hw = &rco_25m.hw  },
	{ .hw = &twpll_38m4.hw  },
	{ .hw = &twpll_51m2.hw  },
};
static SPRD_MUX_CLK_DATA(adi_clk, "adi-clk", adi_parents,
			 0x224, 0, 3, UMS512_MUX_FLAG);

static const struct clk_parent_data aux_parents[] = {
	{ .fw_name = "ext-32k" },
	{ .fw_name = "ext-26m" },
	{ .hw = &clk_26m_aud.hw  },
	{ .hw = &rco_25m.hw  },
	{ .hw = &cppll_39m32.hw  },
	{ .hw = &mpll0_56m88.hw  },
	{ .hw = &mpll1_63m38.hw  },
	{ .hw = &mpll2_47m13.hw  },
	{ .hw = &dpll0_58m31.hw  },
	{ .hw = &gpll_40m.hw  },
	{ .hw = &twpll_48m.hw  },
};
static const struct clk_parent_data aux1_parents[] = {
	{ .fw_name = "ext-32k" },
	{ .fw_name = "ext-26m" },
	{ .hw = &clk_26m_aud.hw  },
	{ .hw = &rco_25m.hw  },
	{ .hw = &cppll_39m32.hw  },
	{ .hw = &mpll0_56m88.hw  },
	{ .hw = &mpll1_63m38.hw  },
	{ .hw = &mpll2_47m13.hw  },
	{ .hw = &dpll0_58m31.hw  },
	{ .hw = &gpll_40m.hw  },
	{ .hw = &twpll_19m2.hw  },
	{ .hw = &lpll_30m72.hw  },
	{ .hw = &rpll.common.hw  },
	{ .hw = &twpll_12m29.hw  },

};
static SPRD_COMP_CLK_DATA(aux0_clk, "aux0-clk", aux_parents,
			  0x228, 0, 5, 8, 4, 0);
static SPRD_COMP_CLK_DATA(aux1_clk, "aux1-clk", aux1_parents,
			  0x22c, 0, 5, 8, 4, 0);
static SPRD_COMP_CLK_DATA(aux2_clk, "aux2-clk", aux_parents,
			  0x230, 0, 5, 8, 4, 0);
static SPRD_COMP_CLK_DATA(probe_clk, "probe-clk", aux_parents,
			  0x234, 0, 5, 8, 4, 0);

static const struct clk_parent_data pwm_parents[] = {
	{ .fw_name = "ext-32k" },
	{ .fw_name = "ext-26m" },
	{ .hw = &rco_4m.hw  },
	{ .hw = &rco_25m.hw  },
	{ .hw = &twpll_48m.hw  },
};
static SPRD_MUX_CLK_DATA(pwm0_clk, "pwm0-clk", pwm_parents,
			 0x238, 0, 3, UMS512_MUX_FLAG);
static SPRD_MUX_CLK_DATA(pwm1_clk, "pwm1-clk", pwm_parents,
			 0x23c, 0, 3, UMS512_MUX_FLAG);
static SPRD_MUX_CLK_DATA(pwm2_clk, "pwm2-clk", pwm_parents,
			 0x240, 0, 3, UMS512_MUX_FLAG);
static SPRD_MUX_CLK_DATA(pwm3_clk, "pwm3-clk", pwm_parents,
			 0x244, 0, 3, UMS512_MUX_FLAG);

static const struct clk_parent_data efuse_parents[] = {
	{ .hw = &rco_25m.hw  },
	{ .fw_name = "ext-26m" },
};
static SPRD_MUX_CLK_DATA(efuse_clk, "efuse-clk", efuse_parents,
			 0x248, 0, 1, UMS512_MUX_FLAG);

static const struct clk_parent_data uart_parents[] = {
	{ .hw = &rco_4m.hw  },
	{ .fw_name = "ext-26m" },
	{ .hw = &twpll_48m.hw  },
	{ .hw = &twpll_51m2.hw  },
	{ .hw = &twpll_96m.hw  },
	{ .fw_name = "rco-100m" },
	{ .hw = &twpll_128m.hw  },
};
static SPRD_MUX_CLK_DATA(uart0_clk, "uart0-clk", uart_parents,
			 0x24c, 0, 3, UMS512_MUX_FLAG);
static SPRD_MUX_CLK_DATA(uart1_clk, "uart1-clk", uart_parents,
			 0x250, 0, 3, UMS512_MUX_FLAG);

static const struct clk_parent_data thm_parents[] = {
	{ .fw_name = "ext-32m" },
	{ .hw = &clk_250k.hw  },
};
static SPRD_MUX_CLK_DATA(thm0_clk, "thm0-clk", thm_parents,
			 0x260, 0, 1, UMS512_MUX_FLAG);
static SPRD_MUX_CLK_DATA(thm1_clk, "thm1-clk", thm_parents,
			 0x264, 0, 1, UMS512_MUX_FLAG);
static SPRD_MUX_CLK_DATA(thm2_clk, "thm2-clk", thm_parents,
			 0x268, 0, 1, UMS512_MUX_FLAG);
static SPRD_MUX_CLK_DATA(thm3_clk, "thm3-clk", thm_parents,
			 0x26c, 0, 1, UMS512_MUX_FLAG);

static const struct clk_parent_data aon_i2c_parents[] = {
	{ .hw = &rco_4m.hw  },
	{ .fw_name = "ext-26m" },
	{ .hw = &twpll_48m.hw  },
	{ .hw = &twpll_51m2.hw  },
	{ .fw_name = "rco-100m" },
	{ .hw = &twpll_153m6.hw  },
};
static SPRD_MUX_CLK_DATA(aon_i2c_clk, "aon-i2c-clk", aon_i2c_parents,
			 0x27c, 0, 3, UMS512_MUX_FLAG);

static const struct clk_parent_data aon_iis_parents[] = {
	{ .fw_name = "ext-26m" },
	{ .hw = &twpll_128m.hw  },
	{ .hw = &twpll_153m6.hw  },
};
static SPRD_MUX_CLK_DATA(aon_iis_clk, "aon-iis-clk", aon_iis_parents,
			 0x280, 0, 2, UMS512_MUX_FLAG);

static const struct clk_parent_data scc_parents[] = {
	{ .fw_name = "ext-26m" },
	{ .hw = &twpll_48m.hw  },
	{ .hw = &twpll_51m2.hw  },
	{ .hw = &twpll_96m.hw  },
};
static SPRD_MUX_CLK_DATA(scc_clk, "scc-clk", scc_parents,
			 0x284, 0, 2, UMS512_MUX_FLAG);

static const struct clk_parent_data apcpu_dap_parents[] = {
	{ .fw_name = "ext-26m" },
	{ .hw = &rco_4m.hw  },
	{ .hw = &twpll_76m8.hw  },
	{ .fw_name = "rco-100m" },
	{ .hw = &twpll_128m.hw  },
	{ .hw = &twpll_153m6.hw  },
};
static SPRD_MUX_CLK_DATA(apcpu_dap_clk, "apcpu-dap-clk", apcpu_dap_parents,
			 0x288, 0, 3, UMS512_MUX_FLAG);

static SPRD_GATE_CLK_FW_NAME(apcpu_dap_mtck, "apcpu-dap-mtck", "ext-26m",
			     0x28c, BIT(16), 0, 0);

static const struct clk_parent_data apcpu_ts_parents[] = {
	{ .fw_name = "ext-32m" },
	{ .fw_name = "ext-26m" },
	{ .hw = &twpll_128m.hw  },
	{ .hw = &twpll_153m6.hw  },
};
static SPRD_MUX_CLK_DATA(apcpu_ts_clk, "apcpu-ts-clk", apcpu_ts_parents,
			 0x290, 0, 2, UMS512_MUX_FLAG);

static const struct clk_parent_data debug_ts_parents[] = {
	{ .fw_name = "ext-26m" },
	{ .hw = &twpll_76m8.hw  },
	{ .hw = &twpll_128m.hw  },
	{ .hw = &twpll_192m.hw  },
};
static SPRD_MUX_CLK_DATA(debug_ts_clk, "debug-ts-clk", debug_ts_parents,
			 0x294, 0, 2, UMS512_MUX_FLAG);

static SPRD_GATE_CLK_FW_NAME(dsi_test_s, "dsi-test-s", "ext-26m",
			     0x298, BIT(16), 0, 0);

static const struct clk_parent_data djtag_tck_parents[] = {
	{ .hw = &rco_4m.hw  },
	{ .fw_name = "ext-26m" },
};
static SPRD_MUX_CLK_DATA(djtag_tck_clk, "djtag-tck-clk", djtag_tck_parents,
			 0x2b4, 0, 1, UMS512_MUX_FLAG);

static SPRD_GATE_CLK_FW_NAME(djtag_tck_hw, "djtag-tck-hw", "ext-26m",
			     0x2b8, BIT(16), 0, 0);

static const struct clk_parent_data aon_tmr_parents[] = {
	{ .hw = &rco_4m.hw  },
	{ .hw = &rco_25m.hw  },
	{ .fw_name = "ext-26m" },
};
static SPRD_MUX_CLK_DATA(aon_tmr_clk, "aon-tmr-clk", aon_tmr_parents,
			 0x2c0, 0, 2, UMS512_MUX_FLAG);

static const struct clk_parent_data aon_pmu_parents[] = {
	{ .fw_name = "ext-32k" },
	{ .hw = &rco_4m.hw  },
	{ .fw_name = "ext-4m" },
};
static SPRD_MUX_CLK_DATA(aon_pmu_clk, "aon-pmu-clk", aon_pmu_parents,
			 0x2c8, 0, 2, UMS512_MUX_FLAG);

static const struct clk_parent_data debounce_parents[] = {
	{ .fw_name = "ext-32k" },
	{ .hw = &rco_4m.hw  },
	{ .hw = &rco_25m.hw  },
	{ .fw_name = "ext-26m" },
};
static SPRD_MUX_CLK_DATA(debounce_clk, "debounce-clk", debounce_parents,
			 0x2cc, 0, 2, UMS512_MUX_FLAG);

static const struct clk_parent_data apcpu_pmu_parents[] = {
	{ .fw_name = "ext-26m" },
	{ .hw = &twpll_76m8.hw  },
	{ .fw_name = "rco-100m" },
	{ .hw = &twpll_128m.hw  },
};
static SPRD_MUX_CLK_DATA(apcpu_pmu_clk, "apcpu-pmu-clk", apcpu_pmu_parents,
			 0x2d0, 0, 2, UMS512_MUX_FLAG);

static const struct clk_parent_data top_dvfs_parents[] = {
	{ .fw_name = "ext-26m" },
	{ .hw = &twpll_96m.hw  },
	{ .fw_name = "rco-100m" },
	{ .hw = &twpll_128m.hw  },
};
static SPRD_MUX_CLK_DATA(top_dvfs_clk, "top-dvfs-clk", top_dvfs_parents,
			 0x2d8, 0, 2, UMS512_MUX_FLAG);

static SPRD_GATE_CLK_FW_NAME(otg_utmi, "otg-utmi", "ext-26m", 0x2dc,
				BIT(16), 0, 0);

static const struct clk_parent_data otg_ref_parents[] = {
	{ .hw = &twpll_12m.hw  },
	{ .fw_name = "ext-26m" },
};
static SPRD_MUX_CLK_DATA(otg_ref_clk, "otg-ref-clk", otg_ref_parents,
			 0x2e0, 0, 1, UMS512_MUX_FLAG);

static const struct clk_parent_data cssys_parents[] = {
	{ .hw = &rco_25m.hw  },
	{ .fw_name = "ext-26m" },
	{ .fw_name = "rco-100m" },
	{ .hw = &twpll_153m6.hw  },
	{ .hw = &twpll_384m.hw  },
	{ .hw = &twpll_512m.hw  },
};
static SPRD_COMP_CLK_DATA(cssys_clk, "cssys-clk", cssys_parents,
			  0x2e4, 0, 3, 8, 2, 0);
static SPRD_DIV_CLK_HW(cssys_pub_clk, "cssys-pub-clk", &cssys_clk.common.hw,
		       0x2e8, 8, 2, 0);
static SPRD_DIV_CLK_HW(cssys_apb_clk, "cssys-apb-clk", &cssys_clk.common.hw,
		       0x2ec, 8, 3, 0);

static const struct clk_parent_data ap_axi_parents[] = {
	{ .fw_name = "ext-26m" },
	{ .hw = &twpll_76m8.hw  },
	{ .hw = &twpll_128m.hw  },
	{ .hw = &twpll_256m.hw  },
};
static SPRD_MUX_CLK_DATA(ap_axi_clk, "ap-axi-clk", ap_axi_parents,
			 0x2f0, 0, 2, UMS512_MUX_FLAG);

static const struct clk_parent_data ap_mm_parents[] = {
	{ .fw_name = "ext-26m" },
	{ .hw = &twpll_96m.hw  },
	{ .hw = &twpll_128m.hw  },
	{ .hw = &twpll_153m6.hw  },
};
static SPRD_MUX_CLK_DATA(ap_mm_clk, "ap-mm-clk", ap_mm_parents,
			 0x2f4, 0, 2, UMS512_MUX_FLAG);

static const struct clk_parent_data sdio2_2x_parents[] = {
	{ .hw = &clk_1m.hw  },
	{ .fw_name = "ext-26m" },
	{ .hw = &twpll_307m2.hw  },
	{ .hw = &twpll_384m.hw  },
	{ .hw = &rpll.common.hw  },
	{ .hw = &lpll_409m6.hw  },
};
static SPRD_MUX_CLK_DATA(sdio2_2x_clk, "sdio2-2x-clk", sdio2_2x_parents,
			 0x2f8, 0, 3, UMS512_MUX_FLAG);

static const struct clk_parent_data analog_io_apb_parents[] = {
	{ .fw_name = "ext-26m" },
	{ .hw = &twpll_48m.hw  },
};
static SPRD_COMP_CLK_DATA(analog_io_apb, "analog-io-apb", analog_io_apb_parents,
			  0x300, 0, 1, 8, 2, 0);

static const struct clk_parent_data dmc_ref_parents[] = {
	{ .hw = &clk_6m5.hw  },
	{ .hw = &clk_13m.hw  },
	{ .fw_name = "ext-26m" },
};
static SPRD_MUX_CLK_DATA(dmc_ref_clk, "dmc-ref-clk", dmc_ref_parents,
			 0x304, 0, 2, UMS512_MUX_FLAG);

static const struct clk_parent_data emc_parents[] = {
	{ .fw_name = "ext-26m" },
	{ .hw = &twpll_384m.hw  },
	{ .hw = &twpll_512m.hw  },
	{ .hw = &twpll_768m.hw  },
};
static SPRD_MUX_CLK_DATA(emc_clk, "emc-clk", emc_parents,
			 0x30c, 0, 2, UMS512_MUX_FLAG);

static const struct clk_parent_data usb_parents[] = {
	{ .hw = &rco_25m.hw  },
	{ .fw_name = "ext-26m" },
	{ .hw = &twpll_192m.hw  },
	{ .hw = &twpll_96m.hw  },
	{ .fw_name = "rco-100m" },
	{ .hw = &twpll_128m.hw  },
};
static SPRD_COMP_CLK_DATA(usb_clk, "usb-clk", usb_parents,
			  0x310, 0, 3, 8, 2, 0);

static const struct clk_parent_data pmu_26m_parents[] = {
	{ .hw = &rco_25m.hw  },
	{ .fw_name = "ext-26m" },
};
static SPRD_MUX_CLK_DATA(pmu_26m_clk, "26m-pmu-clk", pmu_26m_parents,
			 0x318, 0, 1, UMS512_MUX_FLAG);

static struct sprd_clk_common *ums512_aon_apb[] = {
	/* address base is 0x32080200 */
	&aon_apb_clk.common,
	&adi_clk.common,
	&aux0_clk.common,
	&aux1_clk.common,
	&aux2_clk.common,
	&probe_clk.common,
	&pwm0_clk.common,
	&pwm1_clk.common,
	&pwm2_clk.common,
	&pwm3_clk.common,
	&efuse_clk.common,
	&uart0_clk.common,
	&uart1_clk.common,
	&thm0_clk.common,
	&thm1_clk.common,
	&thm2_clk.common,
	&thm3_clk.common,
	&aon_i2c_clk.common,
	&aon_iis_clk.common,
	&scc_clk.common,
	&apcpu_dap_clk.common,
	&apcpu_dap_mtck.common,
	&apcpu_ts_clk.common,
	&debug_ts_clk.common,
	&dsi_test_s.common,
	&djtag_tck_clk.common,
	&djtag_tck_hw.common,
	&aon_tmr_clk.common,
	&aon_pmu_clk.common,
	&debounce_clk.common,
	&apcpu_pmu_clk.common,
	&top_dvfs_clk.common,
	&otg_utmi.common,
	&otg_ref_clk.common,
	&cssys_clk.common,
	&cssys_pub_clk.common,
	&cssys_apb_clk.common,
	&ap_axi_clk.common,
	&ap_mm_clk.common,
	&sdio2_2x_clk.common,
	&analog_io_apb.common,
	&dmc_ref_clk.common,
	&emc_clk.common,
	&usb_clk.common,
	&pmu_26m_clk.common,
};

static struct clk_hw_onecell_data ums512_aon_apb_hws = {
	.hws	= {
		[CLK_AON_APB]		= &aon_apb_clk.common.hw,
		[CLK_ADI]		= &adi_clk.common.hw,
		[CLK_AUX0]		= &aux0_clk.common.hw,
		[CLK_AUX1]		= &aux1_clk.common.hw,
		[CLK_AUX2]		= &aux2_clk.common.hw,
		[CLK_PROBE]		= &probe_clk.common.hw,
		[CLK_PWM0]		= &pwm0_clk.common.hw,
		[CLK_PWM1]		= &pwm1_clk.common.hw,
		[CLK_PWM2]		= &pwm2_clk.common.hw,
		[CLK_PWM3]		= &pwm3_clk.common.hw,
		[CLK_EFUSE]		= &efuse_clk.common.hw,
		[CLK_UART0]		= &uart0_clk.common.hw,
		[CLK_UART1]		= &uart1_clk.common.hw,
		[CLK_THM0]		= &thm0_clk.common.hw,
		[CLK_THM1]		= &thm1_clk.common.hw,
		[CLK_THM2]		= &thm2_clk.common.hw,
		[CLK_THM3]		= &thm3_clk.common.hw,
		[CLK_AON_I2C]		= &aon_i2c_clk.common.hw,
		[CLK_AON_IIS]		= &aon_iis_clk.common.hw,
		[CLK_SCC]		= &scc_clk.common.hw,
		[CLK_APCPU_DAP]		= &apcpu_dap_clk.common.hw,
		[CLK_APCPU_DAP_MTCK]	= &apcpu_dap_mtck.common.hw,
		[CLK_APCPU_TS]		= &apcpu_ts_clk.common.hw,
		[CLK_DEBUG_TS]		= &debug_ts_clk.common.hw,
		[CLK_DSI_TEST_S]	= &dsi_test_s.common.hw,
		[CLK_DJTAG_TCK]		= &djtag_tck_clk.common.hw,
		[CLK_DJTAG_TCK_HW]	= &djtag_tck_hw.common.hw,
		[CLK_AON_TMR]		= &aon_tmr_clk.common.hw,
		[CLK_AON_PMU]		= &aon_pmu_clk.common.hw,
		[CLK_DEBOUNCE]		= &debounce_clk.common.hw,
		[CLK_APCPU_PMU]		= &apcpu_pmu_clk.common.hw,
		[CLK_TOP_DVFS]		= &top_dvfs_clk.common.hw,
		[CLK_OTG_UTMI]		= &otg_utmi.common.hw,
		[CLK_OTG_REF]		= &otg_ref_clk.common.hw,
		[CLK_CSSYS]		= &cssys_clk.common.hw,
		[CLK_CSSYS_PUB]		= &cssys_pub_clk.common.hw,
		[CLK_CSSYS_APB]		= &cssys_apb_clk.common.hw,
		[CLK_AP_AXI]		= &ap_axi_clk.common.hw,
		[CLK_AP_MM]		= &ap_mm_clk.common.hw,
		[CLK_SDIO2_2X]		= &sdio2_2x_clk.common.hw,
		[CLK_ANALOG_IO_APB]	= &analog_io_apb.common.hw,
		[CLK_DMC_REF_CLK]	= &dmc_ref_clk.common.hw,
		[CLK_EMC]		= &emc_clk.common.hw,
		[CLK_USB]		= &usb_clk.common.hw,
		[CLK_26M_PMU]		= &pmu_26m_clk.common.hw,
	},
	.num	= CLK_AON_APB_NUM,
};

static struct sprd_clk_desc ums512_aon_apb_desc = {
	.clk_clks	= ums512_aon_apb,
	.num_clk_clks	= ARRAY_SIZE(ums512_aon_apb),
	.hw_clks	= &ums512_aon_apb_hws,
};

/* aon apb gates */
static SPRD_SC_GATE_CLK_FW_NAME(rc100m_cal_eb, "rc100m-cal-eb", "ext-26m",
				0x0, 0x1000, BIT(0), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK_FW_NAME(djtag_tck_eb, "djtag-tck-eb", "ext-26m",
				0x0, 0x1000, BIT(2), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK_FW_NAME(djtag_eb, "djtag-eb", "ext-26m",
				0x0, 0x1000, BIT(3), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK_FW_NAME(aux0_eb, "aux0-eb", "ext-26m",
				0x0, 0x1000, BIT(4), 0, 0);
static SPRD_SC_GATE_CLK_FW_NAME(aux1_eb, "aux1-eb", "ext-26m",
				0x0, 0x1000, BIT(5), 0, 0);
static SPRD_SC_GATE_CLK_FW_NAME(aux2_eb, "aux2-eb", "ext-26m",
				0x0, 0x1000, BIT(6), 0, 0);
static SPRD_SC_GATE_CLK_FW_NAME(probe_eb, "probe-eb", "ext-26m",
				0x0, 0x1000, BIT(7), 0, 0);
static SPRD_SC_GATE_CLK_FW_NAME(mm_eb, "mm-eb", "ext-26m",
				0x0, 0x1000, BIT(9), 0, 0);
static SPRD_SC_GATE_CLK_FW_NAME(gpu_eb, "gpu-eb", "ext-26m",
				0x0, 0x1000, BIT(11), 0, 0);
static SPRD_SC_GATE_CLK_FW_NAME(mspi_eb, "mspi-eb", "ext-26m",
				0x0, 0x1000, BIT(12), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK_FW_NAME(apcpu_dap_eb, "apcpu-dap-eb", "ext-26m",
				0x0, 0x1000, BIT(14), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK_FW_NAME(aon_cssys_eb, "aon-cssys-eb", "ext-26m",
				0x0, 0x1000, BIT(15), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK_FW_NAME(cssys_apb_eb, "cssys-apb-eb", "ext-26m",
				0x0, 0x1000, BIT(16), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK_FW_NAME(cssys_pub_eb, "cssys-pub-eb", "ext-26m",
				0x0, 0x1000, BIT(17), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK_FW_NAME(sdphy_cfg_eb, "sdphy-cfg-eb", "ext-26m",
				0x0, 0x1000, BIT(19), 0, 0);
static SPRD_SC_GATE_CLK_FW_NAME(sdphy_ref_eb, "sdphy-ref-eb", "ext-26m",
				0x0, 0x1000, BIT(20), 0, 0);
static SPRD_SC_GATE_CLK_FW_NAME(efuse_eb, "efuse-eb", "ext-26m",
				0x4, 0x1000, BIT(0), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK_FW_NAME(gpio_eb, "gpio-eb", "ext-26m",
				0x4, 0x1000, BIT(1), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK_FW_NAME(mbox_eb, "mbox-eb", "ext-26m",
				0x4, 0x1000, BIT(2), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK_FW_NAME(kpd_eb, "kpd-eb", "ext-26m",
				0x4, 0x1000, BIT(3), 0, 0);
static SPRD_SC_GATE_CLK_FW_NAME(aon_syst_eb, "aon-syst-eb", "ext-26m",
				0x4, 0x1000, BIT(4), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK_FW_NAME(ap_syst_eb, "ap-syst-eb", "ext-26m",
				0x4, 0x1000, BIT(5), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK_FW_NAME(aon_tmr_eb, "aon-tmr-eb", "ext-26m",
				0x4, 0x1000, BIT(6), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK_FW_NAME(otg_utmi_eb, "otg-utmi-eb", "ext-26m",
				0x4, 0x1000, BIT(8), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK_FW_NAME(otg_phy_eb, "otg-phy-eb", "ext-26m",
				0x4, 0x1000, BIT(9), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK_FW_NAME(splk_eb, "splk-eb", "ext-26m",
				0x4, 0x1000, BIT(10), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK_FW_NAME(pin_eb, "pin-eb", "ext-26m",
				0x4, 0x1000, BIT(11), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK_FW_NAME(ana_eb, "ana-eb", "ext-26m",
				0x4, 0x1000, BIT(12), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK_FW_NAME(apcpu_ts0_eb, "apcpu-ts0-eb", "ext-26m",
				0x4, 0x1000, BIT(17), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK_FW_NAME(apb_busmon_eb, "apb-busmon-eb", "ext-26m",
				0x4, 0x1000, BIT(18), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK_FW_NAME(aon_iis_eb, "aon-iis-eb", "ext-26m",
				0x4, 0x1000, BIT(19), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK_FW_NAME(scc_eb, "scc-eb", "ext-26m",
				0x4, 0x1000, BIT(20), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK_FW_NAME(thm0_eb, "thm0-eb", "ext-26m",
				0x8, 0x1000, BIT(0), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK_FW_NAME(thm1_eb, "thm1-eb", "ext-26m",
				0x8, 0x1000, BIT(1), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK_FW_NAME(thm2_eb, "thm2-eb", "ext-26m",
				0x8, 0x1000, BIT(2), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK_FW_NAME(asim_top_eb, "asim-top", "ext-26m",
				0x8, 0x1000, BIT(3), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK_FW_NAME(i2c_eb, "i2c-eb", "ext-26m",
				0x8, 0x1000, BIT(7), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK_FW_NAME(pmu_eb, "pmu-eb", "ext-26m",
				0x8, 0x1000, BIT(8), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK_FW_NAME(adi_eb, "adi-eb", "ext-26m",
				0x8, 0x1000, BIT(9), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK_FW_NAME(eic_eb, "eic-eb", "ext-26m",
				0x8, 0x1000, BIT(10), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK_FW_NAME(ap_intc0_eb, "ap-intc0-eb", "ext-26m",
				0x8, 0x1000, BIT(11), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK_FW_NAME(ap_intc1_eb, "ap-intc1-eb", "ext-26m",
				0x8, 0x1000, BIT(12), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK_FW_NAME(ap_intc2_eb, "ap-intc2-eb", "ext-26m",
				0x8, 0x1000, BIT(13), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK_FW_NAME(ap_intc3_eb, "ap-intc3-eb", "ext-26m",
				0x8, 0x1000, BIT(14), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK_FW_NAME(ap_intc4_eb, "ap-intc4-eb", "ext-26m",
				0x8, 0x1000, BIT(15), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK_FW_NAME(ap_intc5_eb, "ap-intc5-eb", "ext-26m",
				0x8, 0x1000, BIT(16), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK_FW_NAME(audcp_intc_eb, "audcp-intc-eb", "ext-26m",
				0x8, 0x1000, BIT(17), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK_FW_NAME(ap_tmr0_eb, "ap-tmr0-eb", "ext-26m",
				0x8, 0x1000, BIT(22), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK_FW_NAME(ap_tmr1_eb, "ap-tmr1-eb", "ext-26m",
				0x8, 0x1000, BIT(23), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK_FW_NAME(ap_tmr2_eb, "ap-tmr2-eb", "ext-26m",
				0x8, 0x1000, BIT(24), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK_FW_NAME(pwm0_eb, "pwm0-eb", "ext-26m",
				0x8, 0x1000, BIT(25), 0, 0);
static SPRD_SC_GATE_CLK_FW_NAME(pwm1_eb, "pwm1-eb", "ext-26m",
				0x8, 0x1000, BIT(26), 0, 0);
static SPRD_SC_GATE_CLK_FW_NAME(pwm2_eb, "pwm2-eb", "ext-26m",
				0x8, 0x1000, BIT(27), 0, 0);
static SPRD_SC_GATE_CLK_FW_NAME(pwm3_eb, "pwm3-eb", "ext-26m",
				0x8, 0x1000, BIT(28), 0, 0);
static SPRD_SC_GATE_CLK_FW_NAME(ap_wdg_eb, "ap-wdg-eb", "ext-26m",
				0x8, 0x1000, BIT(29), 0, 0);
static SPRD_SC_GATE_CLK_FW_NAME(apcpu_wdg_eb, "apcpu-wdg-eb", "ext-26m",
				0x8, 0x1000, BIT(30), 0, 0);
static SPRD_SC_GATE_CLK_FW_NAME(serdes_eb, "serdes-eb", "ext-26m",
				0x8, 0x1000, BIT(31), 0, 0);
static SPRD_SC_GATE_CLK_FW_NAME(arch_rtc_eb, "arch-rtc-eb", "ext-26m",
				0x18, 0x1000, BIT(0), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK_FW_NAME(kpd_rtc_eb, "kpd-rtc-eb", "ext-26m",
				0x18, 0x1000, BIT(1), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK_FW_NAME(aon_syst_rtc_eb, "aon-syst-rtc-eb", "ext-26m",
				0x18, 0x1000, BIT(2), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK_FW_NAME(ap_syst_rtc_eb, "ap-syst-rtc-eb", "ext-26m",
				0x18, 0x1000, BIT(3), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK_FW_NAME(aon_tmr_rtc_eb, "aon-tmr-rtc-eb", "ext-26m",
				0x18, 0x1000, BIT(4), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK_FW_NAME(eic_rtc_eb, "eic-rtc-eb", "ext-26m",
				0x18, 0x1000, BIT(5), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK_FW_NAME(eic_rtcdv5_eb, "eic-rtcdv5-eb", "ext-26m",
				0x18, 0x1000, BIT(6), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK_FW_NAME(ap_wdg_rtc_eb, "ap-wdg-rtc-eb", "ext-26m",
				0x18, 0x1000, BIT(7), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK_FW_NAME(ac_wdg_rtc_eb, "ac-wdg-rtc-eb", "ext-26m",
				0x18, 0x1000, BIT(8), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK_FW_NAME(ap_tmr0_rtc_eb, "ap-tmr0-rtc-eb", "ext-26m",
				0x18, 0x1000, BIT(9), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK_FW_NAME(ap_tmr1_rtc_eb, "ap-tmr1-rtc-eb", "ext-26m",
				0x18, 0x1000, BIT(10), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK_FW_NAME(ap_tmr2_rtc_eb, "ap-tmr2-rtc-eb", "ext-26m",
				0x18, 0x1000, BIT(11), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK_FW_NAME(dcxo_lc_rtc_eb, "dcxo-lc-rtc-eb", "ext-26m",
				0x18, 0x1000, BIT(12), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK_FW_NAME(bb_cal_rtc_eb, "bb-cal-rtc-eb", "ext-26m",
				0x18, 0x1000, BIT(13), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK_FW_NAME(ap_emmc_rtc_eb, "ap-emmc-rtc-eb", "ext-26m",
				0x18, 0x1000, BIT(14), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK_FW_NAME(ap_sdio0_rtc_eb, "ap-sdio0-rtc-eb", "ext-26m",
				0x18, 0x1000, BIT(15), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK_FW_NAME(ap_sdio1_rtc_eb, "ap-sdio1-rtc-eb", "ext-26m",
				0x18, 0x1000, BIT(16), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK_FW_NAME(ap_sdio2_rtc_eb, "ap-sdio2-rtc-eb", "ext-26m",
				0x18, 0x1000, BIT(17), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK_FW_NAME(dsi_csi_test_eb, "dsi-csi-test-eb", "ext-26m",
				0x138, 0x1000, BIT(8), 0, 0);
static SPRD_SC_GATE_CLK_FW_NAME(djtag_tck_en, "djtag-tck-en", "ext-26m",
				0x138, 0x1000, BIT(9), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK_FW_NAME(dphy_ref_eb, "dphy-ref-eb", "ext-26m",
				0x138, 0x1000, BIT(10), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK_FW_NAME(dmc_ref_eb, "dmc-ref-eb", "ext-26m",
				0x138, 0x1000, BIT(11), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK_FW_NAME(otg_ref_eb, "otg-ref-eb", "ext-26m",
				0x138, 0x1000, BIT(12), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK_FW_NAME(tsen_eb, "tsen-eb", "ext-26m",
				0x138, 0x1000, BIT(13), 0, 0);
static SPRD_SC_GATE_CLK_FW_NAME(tmr_eb, "tmr-eb", "ext-26m",
				0x138, 0x1000, BIT(14), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK_FW_NAME(rc100m_ref_eb, "rc100m-ref-eb", "ext-26m",
				0x138, 0x1000, BIT(15), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK_FW_NAME(rc100m_fdk_eb, "rc100m-fdk-eb", "ext-26m",
				0x138, 0x1000, BIT(16), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK_FW_NAME(debounce_eb, "debounce-eb", "ext-26m",
				0x138, 0x1000, BIT(17), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK_FW_NAME(det_32k_eb, "det-32k-eb", "ext-26m",
				0x138, 0x1000, BIT(18), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK_FW_NAME(top_cssys_en, "top-cssys-en", "ext-26m",
				0x13c, 0x1000, BIT(0), 0, 0);
static SPRD_SC_GATE_CLK_FW_NAME(ap_axi_en, "ap-axi-en", "ext-26m",
				0x13c, 0x1000, BIT(1), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK_FW_NAME(sdio0_2x_en, "sdio0-2x-en", "ext-26m",
				0x13c, 0x1000, BIT(2), 0, 0);
static SPRD_SC_GATE_CLK_FW_NAME(sdio0_1x_en, "sdio0-1x-en", "ext-26m",
				0x13c, 0x1000, BIT(3), 0, 0);
static SPRD_SC_GATE_CLK_FW_NAME(sdio1_2x_en, "sdio1-2x-en", "ext-26m",
				0x13c, 0x1000, BIT(4), 0, 0);
static SPRD_SC_GATE_CLK_FW_NAME(sdio1_1x_en, "sdio1-1x-en", "ext-26m",
				0x13c, 0x1000, BIT(5), 0, 0);
static SPRD_SC_GATE_CLK_FW_NAME(sdio2_2x_en, "sdio2-2x-en", "ext-26m",
				0x13c, 0x1000, BIT(6), 0, 0);
static SPRD_SC_GATE_CLK_FW_NAME(sdio2_1x_en, "sdio2-1x-en", "ext-26m",
				0x13c, 0x1000, BIT(7), 0, 0);
static SPRD_SC_GATE_CLK_FW_NAME(emmc_2x_en, "emmc-2x-en", "ext-26m",
				0x13c, 0x1000, BIT(8), 0, 0);
static SPRD_SC_GATE_CLK_FW_NAME(emmc_1x_en, "emmc-1x-en", "ext-26m",
				0x13c, 0x1000, BIT(9), 0, 0);
static SPRD_SC_GATE_CLK_FW_NAME(pll_test_en, "pll-test-en", "ext-26m",
				0x13c, 0x1000, BIT(14), 0, 0);
static SPRD_SC_GATE_CLK_FW_NAME(cphy_cfg_en, "cphy-cfg-en", "ext-26m",
				0x13c, 0x1000, BIT(15), 0, 0);
static SPRD_SC_GATE_CLK_FW_NAME(debug_ts_en, "debug-ts-en", "ext-26m",
				0x13c, 0x1000, BIT(18), 0, 0);
static SPRD_SC_GATE_CLK_FW_NAME(access_aud_en, "access-aud-en",
				"ext-26m", 0x14c, 0x1000, BIT(0), 0, 0);

static struct sprd_clk_common *ums512_aon_gate[] = {
	/* address base is 0x327d0000 */
	&rc100m_cal_eb.common,
	&djtag_tck_eb.common,
	&djtag_eb.common,
	&aux0_eb.common,
	&aux1_eb.common,
	&aux2_eb.common,
	&probe_eb.common,
	&mm_eb.common,
	&gpu_eb.common,
	&mspi_eb.common,
	&apcpu_dap_eb.common,
	&aon_cssys_eb.common,
	&cssys_apb_eb.common,
	&cssys_pub_eb.common,
	&sdphy_cfg_eb.common,
	&sdphy_ref_eb.common,
	&efuse_eb.common,
	&gpio_eb.common,
	&mbox_eb.common,
	&kpd_eb.common,
	&aon_syst_eb.common,
	&ap_syst_eb.common,
	&aon_tmr_eb.common,
	&otg_utmi_eb.common,
	&otg_phy_eb.common,
	&splk_eb.common,
	&pin_eb.common,
	&ana_eb.common,
	&apcpu_ts0_eb.common,
	&apb_busmon_eb.common,
	&aon_iis_eb.common,
	&scc_eb.common,
	&thm0_eb.common,
	&thm1_eb.common,
	&thm2_eb.common,
	&asim_top_eb.common,
	&i2c_eb.common,
	&pmu_eb.common,
	&adi_eb.common,
	&eic_eb.common,
	&ap_intc0_eb.common,
	&ap_intc1_eb.common,
	&ap_intc2_eb.common,
	&ap_intc3_eb.common,
	&ap_intc4_eb.common,
	&ap_intc5_eb.common,
	&audcp_intc_eb.common,
	&ap_tmr0_eb.common,
	&ap_tmr1_eb.common,
	&ap_tmr2_eb.common,
	&pwm0_eb.common,
	&pwm1_eb.common,
	&pwm2_eb.common,
	&pwm3_eb.common,
	&ap_wdg_eb.common,
	&apcpu_wdg_eb.common,
	&serdes_eb.common,
	&arch_rtc_eb.common,
	&kpd_rtc_eb.common,
	&aon_syst_rtc_eb.common,
	&ap_syst_rtc_eb.common,
	&aon_tmr_rtc_eb.common,
	&eic_rtc_eb.common,
	&eic_rtcdv5_eb.common,
	&ap_wdg_rtc_eb.common,
	&ac_wdg_rtc_eb.common,
	&ap_tmr0_rtc_eb.common,
	&ap_tmr1_rtc_eb.common,
	&ap_tmr2_rtc_eb.common,
	&dcxo_lc_rtc_eb.common,
	&bb_cal_rtc_eb.common,
	&ap_emmc_rtc_eb.common,
	&ap_sdio0_rtc_eb.common,
	&ap_sdio1_rtc_eb.common,
	&ap_sdio2_rtc_eb.common,
	&dsi_csi_test_eb.common,
	&djtag_tck_en.common,
	&dphy_ref_eb.common,
	&dmc_ref_eb.common,
	&otg_ref_eb.common,
	&tsen_eb.common,
	&tmr_eb.common,
	&rc100m_ref_eb.common,
	&rc100m_fdk_eb.common,
	&debounce_eb.common,
	&det_32k_eb.common,
	&top_cssys_en.common,
	&ap_axi_en.common,
	&sdio0_2x_en.common,
	&sdio0_1x_en.common,
	&sdio1_2x_en.common,
	&sdio1_1x_en.common,
	&sdio2_2x_en.common,
	&sdio2_1x_en.common,
	&emmc_2x_en.common,
	&emmc_1x_en.common,
	&pll_test_en.common,
	&cphy_cfg_en.common,
	&debug_ts_en.common,
	&access_aud_en.common,
};

static struct clk_hw_onecell_data ums512_aon_gate_hws = {
	.hws	= {
		[CLK_RC100M_CAL_EB]	= &rc100m_cal_eb.common.hw,
		[CLK_DJTAG_TCK_EB]	= &djtag_tck_eb.common.hw,
		[CLK_DJTAG_EB]		= &djtag_eb.common.hw,
		[CLK_AUX0_EB]		= &aux0_eb.common.hw,
		[CLK_AUX1_EB]		= &aux1_eb.common.hw,
		[CLK_AUX2_EB]		= &aux2_eb.common.hw,
		[CLK_PROBE_EB]		= &probe_eb.common.hw,
		[CLK_MM_EB]		= &mm_eb.common.hw,
		[CLK_GPU_EB]		= &gpu_eb.common.hw,
		[CLK_MSPI_EB]		= &mspi_eb.common.hw,
		[CLK_APCPU_DAP_EB]	= &apcpu_dap_eb.common.hw,
		[CLK_AON_CSSYS_EB]	= &aon_cssys_eb.common.hw,
		[CLK_CSSYS_APB_EB]	= &cssys_apb_eb.common.hw,
		[CLK_CSSYS_PUB_EB]	= &cssys_pub_eb.common.hw,
		[CLK_SDPHY_CFG_EB]	= &sdphy_cfg_eb.common.hw,
		[CLK_SDPHY_REF_EB]	= &sdphy_ref_eb.common.hw,
		[CLK_EFUSE_EB]		= &efuse_eb.common.hw,
		[CLK_GPIO_EB]		= &gpio_eb.common.hw,
		[CLK_MBOX_EB]		= &mbox_eb.common.hw,
		[CLK_KPD_EB]		= &kpd_eb.common.hw,
		[CLK_AON_SYST_EB]	= &aon_syst_eb.common.hw,
		[CLK_AP_SYST_EB]	= &ap_syst_eb.common.hw,
		[CLK_AON_TMR_EB]	= &aon_tmr_eb.common.hw,
		[CLK_OTG_UTMI_EB]	= &otg_utmi_eb.common.hw,
		[CLK_OTG_PHY_EB]	= &otg_phy_eb.common.hw,
		[CLK_SPLK_EB]		= &splk_eb.common.hw,
		[CLK_PIN_EB]		= &pin_eb.common.hw,
		[CLK_ANA_EB]		= &ana_eb.common.hw,
		[CLK_APCPU_TS0_EB]	= &apcpu_ts0_eb.common.hw,
		[CLK_APB_BUSMON_EB]	= &apb_busmon_eb.common.hw,
		[CLK_AON_IIS_EB]	= &aon_iis_eb.common.hw,
		[CLK_SCC_EB]		= &scc_eb.common.hw,
		[CLK_THM0_EB]		= &thm0_eb.common.hw,
		[CLK_THM1_EB]		= &thm1_eb.common.hw,
		[CLK_THM2_EB]		= &thm2_eb.common.hw,
		[CLK_ASIM_TOP_EB]	= &asim_top_eb.common.hw,
		[CLK_I2C_EB]		= &i2c_eb.common.hw,
		[CLK_PMU_EB]		= &pmu_eb.common.hw,
		[CLK_ADI_EB]		= &adi_eb.common.hw,
		[CLK_EIC_EB]		= &eic_eb.common.hw,
		[CLK_AP_INTC0_EB]	= &ap_intc0_eb.common.hw,
		[CLK_AP_INTC1_EB]	= &ap_intc1_eb.common.hw,
		[CLK_AP_INTC2_EB]	= &ap_intc2_eb.common.hw,
		[CLK_AP_INTC3_EB]	= &ap_intc3_eb.common.hw,
		[CLK_AP_INTC4_EB]	= &ap_intc4_eb.common.hw,
		[CLK_AP_INTC5_EB]	= &ap_intc5_eb.common.hw,
		[CLK_AUDCP_INTC_EB]	= &audcp_intc_eb.common.hw,
		[CLK_AP_TMR0_EB]	= &ap_tmr0_eb.common.hw,
		[CLK_AP_TMR1_EB]	= &ap_tmr1_eb.common.hw,
		[CLK_AP_TMR2_EB]	= &ap_tmr2_eb.common.hw,
		[CLK_PWM0_EB]		= &pwm0_eb.common.hw,
		[CLK_PWM1_EB]		= &pwm1_eb.common.hw,
		[CLK_PWM2_EB]		= &pwm2_eb.common.hw,
		[CLK_PWM3_EB]		= &pwm3_eb.common.hw,
		[CLK_AP_WDG_EB]		= &ap_wdg_eb.common.hw,
		[CLK_APCPU_WDG_EB]	= &apcpu_wdg_eb.common.hw,
		[CLK_SERDES_EB]		= &serdes_eb.common.hw,
		[CLK_ARCH_RTC_EB]	= &arch_rtc_eb.common.hw,
		[CLK_KPD_RTC_EB]	= &kpd_rtc_eb.common.hw,
		[CLK_AON_SYST_RTC_EB]	= &aon_syst_rtc_eb.common.hw,
		[CLK_AP_SYST_RTC_EB]	= &ap_syst_rtc_eb.common.hw,
		[CLK_AON_TMR_RTC_EB]	= &aon_tmr_rtc_eb.common.hw,
		[CLK_EIC_RTC_EB]	= &eic_rtc_eb.common.hw,
		[CLK_EIC_RTCDV5_EB]	= &eic_rtcdv5_eb.common.hw,
		[CLK_AP_WDG_RTC_EB]	= &ap_wdg_rtc_eb.common.hw,
		[CLK_AC_WDG_RTC_EB]	= &ac_wdg_rtc_eb.common.hw,
		[CLK_AP_TMR0_RTC_EB]	= &ap_tmr0_rtc_eb.common.hw,
		[CLK_AP_TMR1_RTC_EB]	= &ap_tmr1_rtc_eb.common.hw,
		[CLK_AP_TMR2_RTC_EB]	= &ap_tmr2_rtc_eb.common.hw,
		[CLK_DCXO_LC_RTC_EB]	= &dcxo_lc_rtc_eb.common.hw,
		[CLK_BB_CAL_RTC_EB]	= &bb_cal_rtc_eb.common.hw,
		[CLK_AP_EMMC_RTC_EB]	= &ap_emmc_rtc_eb.common.hw,
		[CLK_AP_SDIO0_RTC_EB]	= &ap_sdio0_rtc_eb.common.hw,
		[CLK_AP_SDIO1_RTC_EB]	= &ap_sdio1_rtc_eb.common.hw,
		[CLK_AP_SDIO2_RTC_EB]	= &ap_sdio2_rtc_eb.common.hw,
		[CLK_DSI_CSI_TEST_EB]	= &dsi_csi_test_eb.common.hw,
		[CLK_DJTAG_TCK_EN]	= &djtag_tck_en.common.hw,
		[CLK_DPHY_REF_EB]	= &dphy_ref_eb.common.hw,
		[CLK_DMC_REF_EB]	= &dmc_ref_eb.common.hw,
		[CLK_OTG_REF_EB]	= &otg_ref_eb.common.hw,
		[CLK_TSEN_EB]		= &tsen_eb.common.hw,
		[CLK_TMR_EB]		= &tmr_eb.common.hw,
		[CLK_RC100M_REF_EB]	= &rc100m_ref_eb.common.hw,
		[CLK_RC100M_FDK_EB]	= &rc100m_fdk_eb.common.hw,
		[CLK_DEBOUNCE_EB]	= &debounce_eb.common.hw,
		[CLK_DET_32K_EB]	= &det_32k_eb.common.hw,
		[CLK_TOP_CSSYS_EB]	= &top_cssys_en.common.hw,
		[CLK_AP_AXI_EN]		= &ap_axi_en.common.hw,
		[CLK_SDIO0_2X_EN]	= &sdio0_2x_en.common.hw,
		[CLK_SDIO0_1X_EN]	= &sdio0_1x_en.common.hw,
		[CLK_SDIO1_2X_EN]	= &sdio1_2x_en.common.hw,
		[CLK_SDIO1_1X_EN]	= &sdio1_1x_en.common.hw,
		[CLK_SDIO2_2X_EN]	= &sdio2_2x_en.common.hw,
		[CLK_SDIO2_1X_EN]	= &sdio2_1x_en.common.hw,
		[CLK_EMMC_2X_EN]	= &emmc_2x_en.common.hw,
		[CLK_EMMC_1X_EN]	= &emmc_1x_en.common.hw,
		[CLK_PLL_TEST_EN]	= &pll_test_en.common.hw,
		[CLK_CPHY_CFG_EN]	= &cphy_cfg_en.common.hw,
		[CLK_DEBUG_TS_EN]	= &debug_ts_en.common.hw,
		[CLK_ACCESS_AUD_EN]	= &access_aud_en.common.hw,
	},
	.num	= CLK_AON_APB_GATE_NUM,
};

static struct sprd_clk_desc ums512_aon_gate_desc = {
	.clk_clks	= ums512_aon_gate,
	.num_clk_clks	= ARRAY_SIZE(ums512_aon_gate),
	.hw_clks	= &ums512_aon_gate_hws,
};

/* audcp apb gates */
/* Audcp apb clocks configure CLK_IGNORE_UNUSED because these clocks may be
 * controlled by audcp sys at the same time. It may be cause an execption if
 * kernel gates these clock.
 */
static SPRD_SC_GATE_CLK_HW(audcp_wdg_eb, "audcp-wdg-eb",
			   &access_aud_en.common.hw, 0x0, 0x100, BIT(1),
			   CLK_IGNORE_UNUSED, SPRD_GATE_NON_AON);
static SPRD_SC_GATE_CLK_HW(audcp_rtc_wdg_eb, "audcp-rtc-wdg-eb",
			   &access_aud_en.common.hw, 0x0, 0x100, BIT(2),
			   CLK_IGNORE_UNUSED, SPRD_GATE_NON_AON);
static SPRD_SC_GATE_CLK_HW(audcp_tmr0_eb, "audcp-tmr0-eb",
			   &access_aud_en.common.hw, 0x0, 0x100, BIT(5),
			   CLK_IGNORE_UNUSED, SPRD_GATE_NON_AON);
static SPRD_SC_GATE_CLK_HW(audcp_tmr1_eb, "audcp-tmr1-eb",
			   &access_aud_en.common.hw, 0x0, 0x100, BIT(6),
			   CLK_IGNORE_UNUSED, SPRD_GATE_NON_AON);

static struct sprd_clk_common *ums512_audcpapb_gate[] = {
	/* address base is 0x3350d000 */
	&audcp_wdg_eb.common,
	&audcp_rtc_wdg_eb.common,
	&audcp_tmr0_eb.common,
	&audcp_tmr1_eb.common,
};

static struct clk_hw_onecell_data ums512_audcpapb_gate_hws = {
	.hws	= {
		[CLK_AUDCP_WDG_EB]	= &audcp_wdg_eb.common.hw,
		[CLK_AUDCP_RTC_WDG_EB]	= &audcp_rtc_wdg_eb.common.hw,
		[CLK_AUDCP_TMR0_EB]	= &audcp_tmr0_eb.common.hw,
		[CLK_AUDCP_TMR1_EB]	= &audcp_tmr1_eb.common.hw,
	},
	.num	= CLK_AUDCP_APB_GATE_NUM,
};

static const struct sprd_clk_desc ums512_audcpapb_gate_desc = {
	.clk_clks	= ums512_audcpapb_gate,
	.num_clk_clks	= ARRAY_SIZE(ums512_audcpapb_gate),
	.hw_clks	= &ums512_audcpapb_gate_hws,
};

/* audcp ahb gates */
/* Audcp aphb clocks configure CLK_IGNORE_UNUSED because these clocks may be
 * controlled by audcp sys at the same time. It may be cause an execption if
 * kernel gates these clock.
 */
static SPRD_SC_GATE_CLK_HW(audcp_iis0_eb, "audcp-iis0-eb",
			   &access_aud_en.common.hw, 0x0, 0x100, BIT(0),
			   CLK_IGNORE_UNUSED, SPRD_GATE_NON_AON);
static SPRD_SC_GATE_CLK_HW(audcp_iis1_eb, "audcp-iis1-eb",
			   &access_aud_en.common.hw, 0x0, 0x100, BIT(1),
			   CLK_IGNORE_UNUSED, SPRD_GATE_NON_AON);
static SPRD_SC_GATE_CLK_HW(audcp_iis2_eb, "audcp-iis2-eb",
			   &access_aud_en.common.hw, 0x0, 0x100, BIT(2),
			   CLK_IGNORE_UNUSED, SPRD_GATE_NON_AON);
static SPRD_SC_GATE_CLK_HW(audcp_uart_eb, "audcp-uart-eb",
			   &access_aud_en.common.hw, 0x0, 0x100, BIT(4),
			   CLK_IGNORE_UNUSED, SPRD_GATE_NON_AON);
static SPRD_SC_GATE_CLK_HW(audcp_dma_cp_eb, "audcp-dma-cp-eb",
			   &access_aud_en.common.hw, 0x0, 0x100, BIT(5),
			   CLK_IGNORE_UNUSED, SPRD_GATE_NON_AON);
static SPRD_SC_GATE_CLK_HW(audcp_dma_ap_eb, "audcp-dma-ap-eb",
			   &access_aud_en.common.hw, 0x0, 0x100, BIT(6),
			   CLK_IGNORE_UNUSED, SPRD_GATE_NON_AON);
static SPRD_SC_GATE_CLK_HW(audcp_src48k_eb, "audcp-src48k-eb",
			   &access_aud_en.common.hw, 0x0, 0x100, BIT(10),
			   CLK_IGNORE_UNUSED, SPRD_GATE_NON_AON);
static SPRD_SC_GATE_CLK_HW(audcp_mcdt_eb, "audcp-mcdt-eb",
			   &access_aud_en.common.hw, 0x0, 0x100, BIT(12),
			   CLK_IGNORE_UNUSED, SPRD_GATE_NON_AON);
static SPRD_SC_GATE_CLK_HW(audcp_vbcifd_eb, "audcp-vbcifd-eb",
			   &access_aud_en.common.hw, 0x0, 0x100, BIT(13),
			   CLK_IGNORE_UNUSED, SPRD_GATE_NON_AON);
static SPRD_SC_GATE_CLK_HW(audcp_vbc_eb, "audcp-vbc-eb",
			   &access_aud_en.common.hw, 0x0, 0x100, BIT(14),
			   CLK_IGNORE_UNUSED, SPRD_GATE_NON_AON);
static SPRD_SC_GATE_CLK_HW(audcp_splk_eb,  "audcp-splk-eb",
			   &access_aud_en.common.hw, 0x0, 0x100, BIT(15),
			   CLK_IGNORE_UNUSED, SPRD_GATE_NON_AON);
static SPRD_SC_GATE_CLK_HW(audcp_icu_eb, "audcp-icu-eb",
			   &access_aud_en.common.hw, 0x0, 0x100, BIT(16),
			   CLK_IGNORE_UNUSED, SPRD_GATE_NON_AON);
static SPRD_SC_GATE_CLK_HW(dma_ap_ashb_eb, "dma-ap-ashb-eb",
			   &access_aud_en.common.hw, 0x0, 0x100, BIT(17),
			   CLK_IGNORE_UNUSED, SPRD_GATE_NON_AON);
static SPRD_SC_GATE_CLK_HW(dma_cp_ashb_eb, "dma-cp-ashb-eb",
			   &access_aud_en.common.hw, 0x0, 0x100, BIT(18),
			   CLK_IGNORE_UNUSED, SPRD_GATE_NON_AON);
static SPRD_SC_GATE_CLK_HW(audcp_aud_eb, "audcp-aud-eb",
			   &access_aud_en.common.hw, 0x0, 0x100, BIT(19),
			   CLK_IGNORE_UNUSED, SPRD_GATE_NON_AON);
static SPRD_SC_GATE_CLK_HW(audcp_vbc_24m_eb, "audcp-vbc-24m-eb",
			   &access_aud_en.common.hw, 0x0, 0x100, BIT(21),
			   CLK_IGNORE_UNUSED, SPRD_GATE_NON_AON);
static SPRD_SC_GATE_CLK_HW(audcp_tmr_26m_eb, "audcp-tmr-26m-eb",
			   &access_aud_en.common.hw, 0x0, 0x100, BIT(22),
			   CLK_IGNORE_UNUSED, SPRD_GATE_NON_AON);
static SPRD_SC_GATE_CLK_HW(audcp_dvfs_ashb_eb, "audcp-dvfs-ashb-eb",
			   &access_aud_en.common.hw, 0x0, 0x100, BIT(23),
			   CLK_IGNORE_UNUSED, SPRD_GATE_NON_AON);

static struct sprd_clk_common *ums512_audcpahb_gate[] = {
	/* address base is 0x335e0000 */
	&audcp_iis0_eb.common,
	&audcp_iis1_eb.common,
	&audcp_iis2_eb.common,
	&audcp_uart_eb.common,
	&audcp_dma_cp_eb.common,
	&audcp_dma_ap_eb.common,
	&audcp_src48k_eb.common,
	&audcp_mcdt_eb.common,
	&audcp_vbcifd_eb.common,
	&audcp_vbc_eb.common,
	&audcp_splk_eb.common,
	&audcp_icu_eb.common,
	&dma_ap_ashb_eb.common,
	&dma_cp_ashb_eb.common,
	&audcp_aud_eb.common,
	&audcp_vbc_24m_eb.common,
	&audcp_tmr_26m_eb.common,
	&audcp_dvfs_ashb_eb.common,
};

static struct clk_hw_onecell_data ums512_audcpahb_gate_hws = {
	.hws	= {
		[CLK_AUDCP_IIS0_EB]		= &audcp_iis0_eb.common.hw,
		[CLK_AUDCP_IIS1_EB]		= &audcp_iis1_eb.common.hw,
		[CLK_AUDCP_IIS2_EB]		= &audcp_iis2_eb.common.hw,
		[CLK_AUDCP_UART_EB]		= &audcp_uart_eb.common.hw,
		[CLK_AUDCP_DMA_CP_EB]		= &audcp_dma_cp_eb.common.hw,
		[CLK_AUDCP_DMA_AP_EB]		= &audcp_dma_ap_eb.common.hw,
		[CLK_AUDCP_SRC48K_EB]		= &audcp_src48k_eb.common.hw,
		[CLK_AUDCP_MCDT_EB]		= &audcp_mcdt_eb.common.hw,
		[CLK_AUDCP_VBCIFD_EB]		= &audcp_vbcifd_eb.common.hw,
		[CLK_AUDCP_VBC_EB]		= &audcp_vbc_eb.common.hw,
		[CLK_AUDCP_SPLK_EB]		= &audcp_splk_eb.common.hw,
		[CLK_AUDCP_ICU_EB]		= &audcp_icu_eb.common.hw,
		[CLK_AUDCP_DMA_AP_ASHB_EB]	= &dma_ap_ashb_eb.common.hw,
		[CLK_AUDCP_DMA_CP_ASHB_EB]	= &dma_cp_ashb_eb.common.hw,
		[CLK_AUDCP_AUD_EB]		= &audcp_aud_eb.common.hw,
		[CLK_AUDCP_VBC_24M_EB]		= &audcp_vbc_24m_eb.common.hw,
		[CLK_AUDCP_TMR_26M_EB]		= &audcp_tmr_26m_eb.common.hw,
		[CLK_AUDCP_DVFS_ASHB_EB]	= &audcp_dvfs_ashb_eb.common.hw,
	},
	.num	= CLK_AUDCP_AHB_GATE_NUM,
};

static const struct sprd_clk_desc ums512_audcpahb_gate_desc = {
	.clk_clks	= ums512_audcpahb_gate,
	.num_clk_clks	= ARRAY_SIZE(ums512_audcpahb_gate),
	.hw_clks	= &ums512_audcpahb_gate_hws,
};

/* gpu clocks */
static SPRD_GATE_CLK_HW(gpu_core_gate, "gpu-core-gate", &gpu_eb.common.hw,
			0x4, BIT(0), 0, 0);

static const struct clk_parent_data gpu_parents[] = {
	{ .fw_name = "ext-26m" },
	{ .hw = &twpll_384m.hw  },
	{ .hw = &twpll_512m.hw  },
	{ .hw = &lpll_614m4.hw  },
	{ .hw = &twpll_768m.hw  },
	{ .hw = &gpll.common.hw  },
};

static SPRD_COMP_CLK_DATA(gpu_core_clk, "gpu-core-clk", gpu_parents,
			  0x4, 4, 3, 8, 3, 0);

static SPRD_GATE_CLK_HW(gpu_mem_gate, "gpu-mem-gate", &gpu_eb.common.hw,
			0x8, BIT(0), 0, 0);

static SPRD_COMP_CLK_DATA(gpu_mem_clk, "gpu-mem-clk", gpu_parents,
			  0x8, 4, 3, 8, 3, 0);

static SPRD_GATE_CLK_HW(gpu_sys_gate, "gpu-sys-gate", &gpu_eb.common.hw,
			0xc, BIT(0), 0, 0);

static SPRD_DIV_CLK_HW(gpu_sys_clk, "gpu-sys-clk", &gpu_eb.common.hw,
		       0xc, 4, 3, 0);

static struct sprd_clk_common *ums512_gpu_clk[] = {
	/* address base is 0x60100000 */
	&gpu_core_gate.common,
	&gpu_core_clk.common,
	&gpu_mem_gate.common,
	&gpu_mem_clk.common,
	&gpu_sys_gate.common,
	&gpu_sys_clk.common,
};

static struct clk_hw_onecell_data ums512_gpu_clk_hws = {
	.hws	= {
		[CLK_GPU_CORE_EB]	= &gpu_core_gate.common.hw,
		[CLK_GPU_CORE]		= &gpu_core_clk.common.hw,
		[CLK_GPU_MEM_EB]	= &gpu_mem_gate.common.hw,
		[CLK_GPU_MEM]		= &gpu_mem_clk.common.hw,
		[CLK_GPU_SYS_EB]	= &gpu_sys_gate.common.hw,
		[CLK_GPU_SYS]		= &gpu_sys_clk.common.hw,
	},
	.num	= CLK_GPU_CLK_NUM,
};

static struct sprd_clk_desc ums512_gpu_clk_desc = {
	.clk_clks	= ums512_gpu_clk,
	.num_clk_clks	= ARRAY_SIZE(ums512_gpu_clk),
	.hw_clks	= &ums512_gpu_clk_hws,
};

/* mm clocks */
static const struct clk_parent_data mm_ahb_parents[] = {
	{ .fw_name = "ext-26m" },
	{ .hw = &twpll_96m.hw  },
	{ .hw = &twpll_128m.hw  },
	{ .hw = &twpll_153m6.hw  },
};
static SPRD_MUX_CLK_DATA(mm_ahb_clk, "mm-ahb-clk", mm_ahb_parents,
			 0x20, 0, 2, UMS512_MUX_FLAG);

static const struct clk_parent_data mm_mtx_parents[] = {
	{ .hw = &twpll_76m8.hw  },
	{ .hw = &twpll_128m.hw  },
	{ .hw = &twpll_256m.hw  },
	{ .hw = &twpll_307m2.hw  },
	{ .hw = &twpll_384m.hw  },
	{ .hw = &isppll_468m.hw  },
	{ .hw = &twpll_512m.hw  },
};
static SPRD_MUX_CLK_DATA(mm_mtx_clk, "mm-mtx-clk", mm_mtx_parents,
			 0x24, 0, 3, UMS512_MUX_FLAG);

static const struct clk_parent_data sensor_parents[] = {
	{ .fw_name = "ext-26m" },
	{ .hw = &twpll_48m.hw  },
	{ .hw = &twpll_76m8.hw  },
	{ .hw = &twpll_96m.hw  },
};
static SPRD_COMP_CLK_DATA(sensor0_clk, "sensor0-clk", sensor_parents,
			  0x28, 0, 2, 8, 3, 0);
static SPRD_COMP_CLK_DATA(sensor1_clk, "sensor1-clk", sensor_parents,
			  0x2c, 0, 2, 8, 3, 0);
static SPRD_COMP_CLK_DATA(sensor2_clk, "sensor2-clk", sensor_parents,
			  0x30, 0, 2, 8, 3, 0);

static const struct clk_parent_data cpp_parents[] = {
	{ .hw = &twpll_76m8.hw  },
	{ .hw = &twpll_128m.hw  },
	{ .hw = &twpll_256m.hw  },
	{ .hw = &twpll_384m.hw  },
};
static SPRD_MUX_CLK_DATA(cpp_clk, "cpp-clk", cpp_parents,
			 0x34, 0, 2, UMS512_MUX_FLAG);

static const struct clk_parent_data jpg_parents[] = {
	{ .hw = &twpll_76m8.hw  },
	{ .hw = &twpll_128m.hw  },
	{ .hw = &twpll_256m.hw  },
	{ .hw = &twpll_384m.hw  },
};
static SPRD_MUX_CLK_DATA(jpg_clk, "jpg-clk", jpg_parents,
			 0x38, 0, 2, UMS512_MUX_FLAG);

static const struct clk_parent_data fd_parents[] = {
	{ .hw = &twpll_76m8.hw  },
	{ .hw = &twpll_192m.hw  },
	{ .hw = &twpll_307m2.hw  },
	{ .hw = &twpll_384m.hw  },
};
static SPRD_MUX_CLK_DATA(fd_clk, "fd-clk", fd_parents,
			 0x3c, 0, 2, UMS512_MUX_FLAG);

static const struct clk_parent_data dcam_if_parents[] = {
	{ .hw = &twpll_192m.hw  },
	{ .hw = &twpll_256m.hw  },
	{ .hw = &twpll_307m2.hw  },
	{ .hw = &twpll_384m.hw  },
	{ .hw = &isppll_468m.hw  },
};
static SPRD_MUX_CLK_DATA(dcam_if_clk, "dcam-if-clk", dcam_if_parents,
			 0x40, 0, 3, UMS512_MUX_FLAG);

static const struct clk_parent_data dcam_axi_parents[] = {
	{ .hw = &twpll_256m.hw  },
	{ .hw = &twpll_307m2.hw  },
	{ .hw = &twpll_384m.hw  },
	{ .hw = &isppll_468m.hw  },
};
static SPRD_MUX_CLK_DATA(dcam_axi_clk, "dcam-axi-clk", dcam_axi_parents,
			 0x44, 0, 2, UMS512_MUX_FLAG);

static const struct clk_parent_data isp_parents[] = {
	{ .hw = &twpll_256m.hw  },
	{ .hw = &twpll_307m2.hw  },
	{ .hw = &twpll_384m.hw  },
	{ .hw = &isppll_468m.hw  },
	{ .hw = &twpll_512m.hw  },
};
static SPRD_MUX_CLK_DATA(isp_clk, "isp-clk", isp_parents,
			 0x48, 0, 3, UMS512_MUX_FLAG);

static SPRD_GATE_CLK_HW(mipi_csi0, "mipi-csi0", &mm_eb.common.hw,
			0x4c, BIT(16), CLK_IGNORE_UNUSED, 0);

static SPRD_GATE_CLK_HW(mipi_csi1, "mipi-csi1", &mm_eb.common.hw,
			0x50, BIT(16), CLK_IGNORE_UNUSED, 0);

static SPRD_GATE_CLK_HW(mipi_csi2, "mipi-csi2", &mm_eb.common.hw,
			0x54, BIT(16), CLK_IGNORE_UNUSED, 0);

static struct sprd_clk_common *ums512_mm_clk[] = {
	/* address base is 0x62100000 */
	&mm_ahb_clk.common,
	&mm_mtx_clk.common,
	&sensor0_clk.common,
	&sensor1_clk.common,
	&sensor2_clk.common,
	&cpp_clk.common,
	&jpg_clk.common,
	&fd_clk.common,
	&dcam_if_clk.common,
	&dcam_axi_clk.common,
	&isp_clk.common,
	&mipi_csi0.common,
	&mipi_csi1.common,
	&mipi_csi2.common,
};

static struct clk_hw_onecell_data ums512_mm_clk_hws = {
	.hws	= {
		[CLK_MM_AHB]	= &mm_ahb_clk.common.hw,
		[CLK_MM_MTX]	= &mm_mtx_clk.common.hw,
		[CLK_SENSOR0]	= &sensor0_clk.common.hw,
		[CLK_SENSOR1]	= &sensor1_clk.common.hw,
		[CLK_SENSOR2]	= &sensor2_clk.common.hw,
		[CLK_CPP]	= &cpp_clk.common.hw,
		[CLK_JPG]	= &jpg_clk.common.hw,
		[CLK_FD]	= &fd_clk.common.hw,
		[CLK_DCAM_IF]	= &dcam_if_clk.common.hw,
		[CLK_DCAM_AXI]	= &dcam_axi_clk.common.hw,
		[CLK_ISP]	= &isp_clk.common.hw,
		[CLK_MIPI_CSI0] = &mipi_csi0.common.hw,
		[CLK_MIPI_CSI1] = &mipi_csi1.common.hw,
		[CLK_MIPI_CSI2] = &mipi_csi2.common.hw,
	},
	.num	= CLK_MM_CLK_NUM,
};

static struct sprd_clk_desc ums512_mm_clk_desc = {
	.clk_clks	= ums512_mm_clk,
	.num_clk_clks	= ARRAY_SIZE(ums512_mm_clk),
	.hw_clks	= &ums512_mm_clk_hws,
};

/* mm gate clocks */
static SPRD_SC_GATE_CLK_HW(mm_cpp_eb, "mm-cpp-eb", &mm_eb.common.hw,
			   0x0, 0x1000, BIT(0), 0, 0);
static SPRD_SC_GATE_CLK_HW(mm_jpg_eb, "mm-jpg-eb", &mm_eb.common.hw,
			   0x0, 0x1000, BIT(1), 0, 0);
static SPRD_SC_GATE_CLK_HW(mm_dcam_eb, "mm-dcam-eb", &mm_eb.common.hw,
			   0x0, 0x1000, BIT(2), 0, 0);
static SPRD_SC_GATE_CLK_HW(mm_isp_eb, "mm-isp-eb", &mm_eb.common.hw,
			   0x0, 0x1000, BIT(3), 0, 0);
static SPRD_SC_GATE_CLK_HW(mm_csi2_eb, "mm-csi2-eb", &mm_eb.common.hw,
			   0x0, 0x1000, BIT(4), 0, 0);
static SPRD_SC_GATE_CLK_HW(mm_csi1_eb, "mm-csi1-eb", &mm_eb.common.hw,
			   0x0, 0x1000, BIT(5), 0, 0);
static SPRD_SC_GATE_CLK_HW(mm_csi0_eb, "mm-csi0-eb", &mm_eb.common.hw,
			   0x0, 0x1000, BIT(6), 0, 0);
static SPRD_SC_GATE_CLK_HW(mm_ckg_eb, "mm-ckg-eb", &mm_eb.common.hw,
			   0x0, 0x1000, BIT(7), 0, 0);
static SPRD_SC_GATE_CLK_HW(mm_isp_ahb_eb, "mm-isp-ahb-eb", &mm_eb.common.hw,
			   0x0, 0x1000, BIT(8), 0, 0);
static SPRD_SC_GATE_CLK_HW(mm_dvfs_eb, "mm-dvfs-eb", &mm_eb.common.hw,
			   0x0, 0x1000, BIT(9), 0, 0);
static SPRD_SC_GATE_CLK_HW(mm_fd_eb, "mm-fd-eb", &mm_eb.common.hw,
			   0x0, 0x1000, BIT(10), 0, 0);
static SPRD_SC_GATE_CLK_HW(mm_sensor2_en, "mm-sensor2-en", &mm_eb.common.hw,
			   0x8, 0x1000, BIT(0), 0, 0);
static SPRD_SC_GATE_CLK_HW(mm_sensor1_en, "mm-sensor1-en", &mm_eb.common.hw,
			   0x8, 0x1000, BIT(1), 0, 0);
static SPRD_SC_GATE_CLK_HW(mm_sensor0_en, "mm-sensor0-en", &mm_eb.common.hw,
			   0x8, 0x1000, BIT(2), 0, 0);
static SPRD_SC_GATE_CLK_HW(mm_mipi_csi2_en, "mm-mipi-csi2-en", &mm_eb.common.hw,
			   0x8, 0x1000, BIT(3), 0, 0);
static SPRD_SC_GATE_CLK_HW(mm_mipi_csi1_en, "mm-mipi-csi1-en", &mm_eb.common.hw,
			   0x8, 0x1000, BIT(4), 0, 0);
static SPRD_SC_GATE_CLK_HW(mm_mipi_csi0_en, "mm-mipi-csi0-en", &mm_eb.common.hw,
			   0x8, 0x1000, BIT(5), 0, 0);
static SPRD_SC_GATE_CLK_HW(mm_dcam_axi_en, "mm-dcam-axi-en", &mm_eb.common.hw,
			   0x8, 0x1000, BIT(6), 0, 0);
static SPRD_SC_GATE_CLK_HW(mm_isp_axi_en, "mm-isp-axi-en", &mm_eb.common.hw,
			   0x8, 0x1000, BIT(7), 0, 0);
static SPRD_SC_GATE_CLK_HW(mm_cphy_en, "mm-cphy-en", &mm_eb.common.hw,
			   0x8, 0x1000, BIT(8), 0, 0);

static struct sprd_clk_common *ums512_mm_gate_clk[] = {
	/* address base is 0x62200000 */
	&mm_cpp_eb.common,
	&mm_jpg_eb.common,
	&mm_dcam_eb.common,
	&mm_isp_eb.common,
	&mm_csi2_eb.common,
	&mm_csi1_eb.common,
	&mm_csi0_eb.common,
	&mm_ckg_eb.common,
	&mm_isp_ahb_eb.common,
	&mm_dvfs_eb.common,
	&mm_fd_eb.common,
	&mm_sensor2_en.common,
	&mm_sensor1_en.common,
	&mm_sensor0_en.common,
	&mm_mipi_csi2_en.common,
	&mm_mipi_csi1_en.common,
	&mm_mipi_csi0_en.common,
	&mm_dcam_axi_en.common,
	&mm_isp_axi_en.common,
	&mm_cphy_en.common,
};

static struct clk_hw_onecell_data ums512_mm_gate_clk_hws = {
	.hws	= {
		[CLK_MM_CPP_EB]		= &mm_cpp_eb.common.hw,
		[CLK_MM_JPG_EB]		= &mm_jpg_eb.common.hw,
		[CLK_MM_DCAM_EB]	= &mm_dcam_eb.common.hw,
		[CLK_MM_ISP_EB]		= &mm_isp_eb.common.hw,
		[CLK_MM_CSI2_EB]	= &mm_csi2_eb.common.hw,
		[CLK_MM_CSI1_EB]	= &mm_csi1_eb.common.hw,
		[CLK_MM_CSI0_EB]	= &mm_csi0_eb.common.hw,
		[CLK_MM_CKG_EB]		= &mm_ckg_eb.common.hw,
		[CLK_ISP_AHB_EB]	= &mm_isp_ahb_eb.common.hw,
		[CLK_MM_DVFS_EB]	= &mm_dvfs_eb.common.hw,
		[CLK_MM_FD_EB]		= &mm_fd_eb.common.hw,
		[CLK_MM_SENSOR2_EB]	= &mm_sensor2_en.common.hw,
		[CLK_MM_SENSOR1_EB]	= &mm_sensor1_en.common.hw,
		[CLK_MM_SENSOR0_EB]	= &mm_sensor0_en.common.hw,
		[CLK_MM_MIPI_CSI2_EB]	= &mm_mipi_csi2_en.common.hw,
		[CLK_MM_MIPI_CSI1_EB]	= &mm_mipi_csi1_en.common.hw,
		[CLK_MM_MIPI_CSI0_EB]	= &mm_mipi_csi0_en.common.hw,
		[CLK_DCAM_AXI_EB]	= &mm_dcam_axi_en.common.hw,
		[CLK_ISP_AXI_EB]	= &mm_isp_axi_en.common.hw,
		[CLK_MM_CPHY_EB]	= &mm_cphy_en.common.hw,
	},
	.num	= CLK_MM_GATE_CLK_NUM,
};

static struct sprd_clk_desc ums512_mm_gate_clk_desc = {
	.clk_clks	= ums512_mm_gate_clk,
	.num_clk_clks	= ARRAY_SIZE(ums512_mm_gate_clk),
	.hw_clks	= &ums512_mm_gate_clk_hws,
};

/* ap apb gates */
static SPRD_SC_GATE_CLK_FW_NAME(sim0_eb, "sim0-eb", "ext-26m",
				0x0, 0x1000, BIT(0), 0, 0);
static SPRD_SC_GATE_CLK_FW_NAME(iis0_eb, "iis0-eb", "ext-26m",
				0x0, 0x1000, BIT(1), 0, 0);
static SPRD_SC_GATE_CLK_FW_NAME(iis1_eb, "iis1-eb", "ext-26m",
				0x0, 0x1000, BIT(2), 0, 0);
static SPRD_SC_GATE_CLK_FW_NAME(iis2_eb, "iis2-eb", "ext-26m",
				0x0, 0x1000, BIT(3), 0, 0);
static SPRD_SC_GATE_CLK_FW_NAME(apb_reg_eb, "apb-reg-eb", "ext-26m",
				0x0, 0x1000, BIT(4), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK_FW_NAME(spi0_eb, "spi0-eb", "ext-26m",
				0x0, 0x1000, BIT(5), 0, 0);
static SPRD_SC_GATE_CLK_FW_NAME(spi1_eb, "spi1-eb", "ext-26m",
				0x0, 0x1000, BIT(6), 0, 0);
static SPRD_SC_GATE_CLK_FW_NAME(spi2_eb, "spi2-eb", "ext-26m",
				0x0, 0x1000, BIT(7), 0, 0);
static SPRD_SC_GATE_CLK_FW_NAME(spi3_eb, "spi3-eb", "ext-26m",
				0x0, 0x1000, BIT(8), 0, 0);
static SPRD_SC_GATE_CLK_FW_NAME(i2c0_eb, "i2c0-eb", "ext-26m",
				0x0, 0x1000, BIT(9), 0, 0);
static SPRD_SC_GATE_CLK_FW_NAME(i2c1_eb, "i2c1-eb", "ext-26m",
				0x0, 0x1000, BIT(10), 0, 0);
static SPRD_SC_GATE_CLK_FW_NAME(i2c2_eb, "i2c2-eb", "ext-26m",
				0x0, 0x1000, BIT(11), 0, 0);
static SPRD_SC_GATE_CLK_FW_NAME(i2c3_eb, "i2c3-eb", "ext-26m",
				0x0, 0x1000, BIT(12), 0, 0);
static SPRD_SC_GATE_CLK_FW_NAME(i2c4_eb, "i2c4-eb", "ext-26m",
				0x0, 0x1000, BIT(13), 0, 0);
static SPRD_SC_GATE_CLK_FW_NAME(uart0_eb, "uart0-eb", "ext-26m",
				0x0, 0x1000, BIT(14), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK_FW_NAME(uart1_eb, "uart1-eb", "ext-26m",
				0x0, 0x1000, BIT(15), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK_FW_NAME(uart2_eb, "uart2-eb", "ext-26m",
				0x0, 0x1000, BIT(16), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK_FW_NAME(sim0_32k_eb, "sim0-32k-eb", "ext-26m",
				0x0, 0x1000, BIT(17), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK_FW_NAME(spi0_lfin_eb, "spi0-lfin-eb", "ext-26m",
				0x0, 0x1000, BIT(18), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK_FW_NAME(spi1_lfin_eb, "spi1-lfin-eb", "ext-26m",
				0x0, 0x1000, BIT(19), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK_FW_NAME(spi2_lfin_eb, "spi2-lfin-eb", "ext-26m",
				0x0, 0x1000, BIT(20), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK_FW_NAME(spi3_lfin_eb, "spi3-lfin-eb", "ext-26m",
				0x0, 0x1000, BIT(21), CLK_IGNORE_UNUSED, 0);
static SPRD_SC_GATE_CLK_FW_NAME(sdio0_eb, "sdio0-eb", "ext-26m",
				0x0, 0x1000, BIT(22), 0, 0);
static SPRD_SC_GATE_CLK_FW_NAME(sdio1_eb, "sdio1-eb", "ext-26m",
				0x0, 0x1000, BIT(23), 0, 0);
static SPRD_SC_GATE_CLK_FW_NAME(sdio2_eb, "sdio2-eb", "ext-26m",
				0x0, 0x1000, BIT(24), 0, 0);
static SPRD_SC_GATE_CLK_FW_NAME(emmc_eb, "emmc-eb", "ext-26m",
				0x0, 0x1000, BIT(25), 0, 0);
static SPRD_SC_GATE_CLK_FW_NAME(sdio0_32k_eb, "sdio0-32k-eb", "ext-26m",
				0x0, 0x1000, BIT(26), 0, 0);
static SPRD_SC_GATE_CLK_FW_NAME(sdio1_32k_eb, "sdio1-32k-eb", "ext-26m",
				0x0, 0x1000, BIT(27), 0, 0);
static SPRD_SC_GATE_CLK_FW_NAME(sdio2_32k_eb, "sdio2-32k-eb", "ext-26m",
				0x0, 0x1000, BIT(28), 0, 0);
static SPRD_SC_GATE_CLK_FW_NAME(emmc_32k_eb, "emmc-32k-eb", "ext-26m",
				0x0, 0x1000, BIT(29), 0, 0);

static struct sprd_clk_common *ums512_apapb_gate[] = {
	/* address base is 0x71000000 */
	&sim0_eb.common,
	&iis0_eb.common,
	&iis1_eb.common,
	&iis2_eb.common,
	&apb_reg_eb.common,
	&spi0_eb.common,
	&spi1_eb.common,
	&spi2_eb.common,
	&spi3_eb.common,
	&i2c0_eb.common,
	&i2c1_eb.common,
	&i2c2_eb.common,
	&i2c3_eb.common,
	&i2c4_eb.common,
	&uart0_eb.common,
	&uart1_eb.common,
	&uart2_eb.common,
	&sim0_32k_eb.common,
	&spi0_lfin_eb.common,
	&spi1_lfin_eb.common,
	&spi2_lfin_eb.common,
	&spi3_lfin_eb.common,
	&sdio0_eb.common,
	&sdio1_eb.common,
	&sdio2_eb.common,
	&emmc_eb.common,
	&sdio0_32k_eb.common,
	&sdio1_32k_eb.common,
	&sdio2_32k_eb.common,
	&emmc_32k_eb.common,
};

static struct clk_hw_onecell_data ums512_apapb_gate_hws = {
	.hws	= {
		[CLK_SIM0_EB]		= &sim0_eb.common.hw,
		[CLK_IIS0_EB]		= &iis0_eb.common.hw,
		[CLK_IIS1_EB]		= &iis1_eb.common.hw,
		[CLK_IIS2_EB]		= &iis2_eb.common.hw,
		[CLK_APB_REG_EB]	= &apb_reg_eb.common.hw,
		[CLK_SPI0_EB]		= &spi0_eb.common.hw,
		[CLK_SPI1_EB]		= &spi1_eb.common.hw,
		[CLK_SPI2_EB]		= &spi2_eb.common.hw,
		[CLK_SPI3_EB]		= &spi3_eb.common.hw,
		[CLK_I2C0_EB]		= &i2c0_eb.common.hw,
		[CLK_I2C1_EB]		= &i2c1_eb.common.hw,
		[CLK_I2C2_EB]		= &i2c2_eb.common.hw,
		[CLK_I2C3_EB]		= &i2c3_eb.common.hw,
		[CLK_I2C4_EB]		= &i2c4_eb.common.hw,
		[CLK_UART0_EB]		= &uart0_eb.common.hw,
		[CLK_UART1_EB]		= &uart1_eb.common.hw,
		[CLK_UART2_EB]		= &uart2_eb.common.hw,
		[CLK_SIM0_32K_EB]	= &sim0_32k_eb.common.hw,
		[CLK_SPI0_LFIN_EB]	= &spi0_lfin_eb.common.hw,
		[CLK_SPI1_LFIN_EB]	= &spi1_lfin_eb.common.hw,
		[CLK_SPI2_LFIN_EB]	= &spi2_lfin_eb.common.hw,
		[CLK_SPI3_LFIN_EB]	= &spi3_lfin_eb.common.hw,
		[CLK_SDIO0_EB]		= &sdio0_eb.common.hw,
		[CLK_SDIO1_EB]		= &sdio1_eb.common.hw,
		[CLK_SDIO2_EB]		= &sdio2_eb.common.hw,
		[CLK_EMMC_EB]		= &emmc_eb.common.hw,
		[CLK_SDIO0_32K_EB]	= &sdio0_32k_eb.common.hw,
		[CLK_SDIO1_32K_EB]	= &sdio1_32k_eb.common.hw,
		[CLK_SDIO2_32K_EB]	= &sdio2_32k_eb.common.hw,
		[CLK_EMMC_32K_EB]	= &emmc_32k_eb.common.hw,
	},
	.num	= CLK_AP_APB_GATE_NUM,
};

static struct sprd_clk_desc ums512_apapb_gate_desc = {
	.clk_clks	= ums512_apapb_gate,
	.num_clk_clks	= ARRAY_SIZE(ums512_apapb_gate),
	.hw_clks	= &ums512_apapb_gate_hws,
};

static const struct of_device_id sprd_ums512_clk_ids[] = {
	{ .compatible = "sprd,ums512-pmu-gate",		/* 0x327e0000 */
	  .data = &ums512_pmu_gate_desc },
	{ .compatible = "sprd,ums512-g0-pll",		/* 0x32390000 */
	  .data = &ums512_g0_pll_desc },
	{ .compatible = "sprd,ums512-g2-pll",		/* 0x323b0000 */
	  .data = &ums512_g2_pll_desc },
	{ .compatible = "sprd,ums512-g3-pll",		/* 0x323c0000 */
	  .data = &ums512_g3_pll_desc },
	{ .compatible = "sprd,ums512-gc-pll",		/* 0x323e0000 */
	  .data = &ums512_gc_pll_desc },
	{ .compatible = "sprd,ums512-apahb-gate",	/* 0x20100000 */
	  .data = &ums512_apahb_gate_desc },
	{ .compatible = "sprd,ums512-ap-clk",		/* 0x20200000 */
	  .data = &ums512_ap_clk_desc },
	{ .compatible = "sprd,ums512-aonapb-clk",	/* 0x32080200 */
	  .data = &ums512_aon_apb_desc },
	{ .compatible = "sprd,ums512-aon-gate",		/* 0x327d0000 */
	  .data = &ums512_aon_gate_desc },
	{ .compatible = "sprd,ums512-audcpapb-gate",	/* 0x3350d000 */
	  .data = &ums512_audcpapb_gate_desc },
	{ .compatible = "sprd,ums512-audcpahb-gate",	/* 0x335e0000 */
	  .data = &ums512_audcpahb_gate_desc },
	{ .compatible = "sprd,ums512-gpu-clk",		/* 0x60100000 */
	  .data = &ums512_gpu_clk_desc },
	{ .compatible = "sprd,ums512-mm-clk",		/* 0x62100000 */
	  .data = &ums512_mm_clk_desc },
	{ .compatible = "sprd,ums512-mm-gate-clk",	/* 0x62200000 */
	  .data = &ums512_mm_gate_clk_desc },
	{ .compatible = "sprd,ums512-apapb-gate",	/* 0x71000000 */
	  .data = &ums512_apapb_gate_desc },
	{ }
};
MODULE_DEVICE_TABLE(of, sprd_ums512_clk_ids);

static int ums512_clk_probe(struct platform_device *pdev)
{
	const struct sprd_clk_desc *desc;
	int ret;

	desc = device_get_match_data(&pdev->dev);
	if (!desc)
		return -ENODEV;

	ret = sprd_clk_regmap_init(pdev, desc);
	if (ret)
		return ret;

	return sprd_clk_probe(&pdev->dev, desc->hw_clks);
}

static struct platform_driver ums512_clk_driver = {
	.probe	= ums512_clk_probe,
	.driver	= {
		.name	= "ums512-clk",
		.of_match_table	= sprd_ums512_clk_ids,
	},
};
module_platform_driver(ums512_clk_driver);

MODULE_DESCRIPTION("Unisoc UMS512 Clock Driver");
MODULE_LICENSE("GPL");

// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2015 The Linux Foundation. All rights reserved.
 */

#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/clk-provider.h>
#include <linux/regmap.h>
#include <linux/reset-controller.h>
#include <linux/math64.h>
#include <linux/delay.h>
#include <linux/clk.h>

#include <dt-bindings/clock/qcom,gcc-ipq4019.h>

#include "common.h"
#include "clk-regmap.h"
#include "clk-rcg.h"
#include "clk-branch.h"
#include "reset.h"
#include "clk-regmap-divider.h"

#define to_clk_regmap_div(_hw) container_of(to_clk_regmap(_hw),\
					struct clk_regmap_div, clkr)

#define to_clk_fepll(_hw) container_of(to_clk_regmap_div(_hw),\
						struct clk_fepll, cdiv)

enum {
	P_XO,
	P_FEPLL200,
	P_FEPLL500,
	P_DDRPLL,
	P_FEPLLWCSS2G,
	P_FEPLLWCSS5G,
	P_FEPLL125DLY,
	P_DDRPLLAPSS,
};

/*
 * struct clk_fepll_vco - vco feedback divider corresponds for FEPLL clocks
 * @fdbkdiv_shift: lowest bit for FDBKDIV
 * @fdbkdiv_width: number of bits in FDBKDIV
 * @refclkdiv_shift: lowest bit for REFCLKDIV
 * @refclkdiv_width: number of bits in REFCLKDIV
 * @reg: PLL_DIV register address
 */
struct clk_fepll_vco {
	u32 fdbkdiv_shift;
	u32 fdbkdiv_width;
	u32 refclkdiv_shift;
	u32 refclkdiv_width;
	u32 reg;
};

/*
 * struct clk_fepll - clk divider corresponds to FEPLL clocks
 * @fixed_div: fixed divider value if divider is fixed
 * @parent_map: map from software's parent index to hardware's src_sel field
 * @cdiv: divider values for PLL_DIV
 * @pll_vco: vco feedback divider
 * @div_table: mapping for actual divider value to register divider value
 *             in case of non fixed divider
 * @freq_tbl: frequency table
 */
struct clk_fepll {
	u32 fixed_div;
	const u8 *parent_map;
	struct clk_regmap_div cdiv;
	const struct clk_fepll_vco *pll_vco;
	const struct clk_div_table *div_table;
	const struct freq_tbl *freq_tbl;
};

/*
 * Contains index for safe clock during APSS freq change.
 * fepll500 is being used as safe clock so initialize it
 * with its index in parents list gcc_xo_ddr_500_200.
 */
static const int gcc_ipq4019_cpu_safe_parent = 2;

/* Calculates the VCO rate for FEPLL. */
static u64 clk_fepll_vco_calc_rate(struct clk_fepll *pll_div,
				   unsigned long parent_rate)
{
	const struct clk_fepll_vco *pll_vco = pll_div->pll_vco;
	u32 fdbkdiv, refclkdiv, cdiv;
	u64 vco;

	regmap_read(pll_div->cdiv.clkr.regmap, pll_vco->reg, &cdiv);
	refclkdiv = (cdiv >> pll_vco->refclkdiv_shift) &
		    (BIT(pll_vco->refclkdiv_width) - 1);
	fdbkdiv = (cdiv >> pll_vco->fdbkdiv_shift) &
		  (BIT(pll_vco->fdbkdiv_width) - 1);

	vco = parent_rate / refclkdiv;
	vco *= 2;
	vco *= fdbkdiv;

	return vco;
}

static const struct clk_fepll_vco gcc_apss_ddrpll_vco = {
	.fdbkdiv_shift = 16,
	.fdbkdiv_width = 8,
	.refclkdiv_shift = 24,
	.refclkdiv_width = 5,
	.reg = 0x2e020,
};

static const struct clk_fepll_vco gcc_fepll_vco = {
	.fdbkdiv_shift = 16,
	.fdbkdiv_width = 8,
	.refclkdiv_shift = 24,
	.refclkdiv_width = 5,
	.reg = 0x2f020,
};

/*
 * Round rate function for APSS CPU PLL Clock divider.
 * It looks up the frequency table and returns the next higher frequency
 * supported in hardware.
 */
static long clk_cpu_div_round_rate(struct clk_hw *hw, unsigned long rate,
				   unsigned long *p_rate)
{
	struct clk_fepll *pll = to_clk_fepll(hw);
	struct clk_hw *p_hw;
	const struct freq_tbl *f;

	f = qcom_find_freq(pll->freq_tbl, rate);
	if (!f)
		return -EINVAL;

	p_hw = clk_hw_get_parent_by_index(hw, f->src);
	*p_rate = clk_hw_get_rate(p_hw);

	return f->freq;
};

/*
 * Clock set rate function for APSS CPU PLL Clock divider.
 * It looks up the frequency table and updates the PLL divider to corresponding
 * divider value.
 */
static int clk_cpu_div_set_rate(struct clk_hw *hw, unsigned long rate,
				unsigned long parent_rate)
{
	struct clk_fepll *pll = to_clk_fepll(hw);
	const struct freq_tbl *f;
	u32 mask;

	f = qcom_find_freq(pll->freq_tbl, rate);
	if (!f)
		return -EINVAL;

	mask = (BIT(pll->cdiv.width) - 1) << pll->cdiv.shift;
	regmap_update_bits(pll->cdiv.clkr.regmap,
			   pll->cdiv.reg, mask,
			   f->pre_div << pll->cdiv.shift);
	/*
	 * There is no status bit which can be checked for successful CPU
	 * divider update operation so using delay for the same.
	 */
	udelay(1);

	return 0;
};

/*
 * Clock frequency calculation function for APSS CPU PLL Clock divider.
 * This clock divider is nonlinear so this function calculates the actual
 * divider and returns the output frequency by dividing VCO Frequency
 * with this actual divider value.
 */
static unsigned long
clk_cpu_div_recalc_rate(struct clk_hw *hw,
			unsigned long parent_rate)
{
	struct clk_fepll *pll = to_clk_fepll(hw);
	u32 cdiv, pre_div;
	u64 rate;

	regmap_read(pll->cdiv.clkr.regmap, pll->cdiv.reg, &cdiv);
	cdiv = (cdiv >> pll->cdiv.shift) & (BIT(pll->cdiv.width) - 1);

	/*
	 * Some dividers have value in 0.5 fraction so multiply both VCO
	 * frequency(parent_rate) and pre_div with 2 to make integer
	 * calculation.
	 */
	if (cdiv > 10)
		pre_div = (cdiv + 1) * 2;
	else
		pre_div = cdiv + 12;

	rate = clk_fepll_vco_calc_rate(pll, parent_rate) * 2;
	do_div(rate, pre_div);

	return rate;
};

static const struct clk_ops clk_regmap_cpu_div_ops = {
	.round_rate = clk_cpu_div_round_rate,
	.set_rate = clk_cpu_div_set_rate,
	.recalc_rate = clk_cpu_div_recalc_rate,
};

static const struct freq_tbl ftbl_apss_ddr_pll[] = {
	{ 384000000, P_XO, 0xd, 0, 0 },
	{ 413000000, P_XO, 0xc, 0, 0 },
	{ 448000000, P_XO, 0xb, 0, 0 },
	{ 488000000, P_XO, 0xa, 0, 0 },
	{ 512000000, P_XO, 0x9, 0, 0 },
	{ 537000000, P_XO, 0x8, 0, 0 },
	{ 565000000, P_XO, 0x7, 0, 0 },
	{ 597000000, P_XO, 0x6, 0, 0 },
	{ 632000000, P_XO, 0x5, 0, 0 },
	{ 672000000, P_XO, 0x4, 0, 0 },
	{ 716000000, P_XO, 0x3, 0, 0 },
	{ 768000000, P_XO, 0x2, 0, 0 },
	{ 823000000, P_XO, 0x1, 0, 0 },
	{ 896000000, P_XO, 0x0, 0, 0 },
	{ }
};

static struct clk_fepll gcc_apss_cpu_plldiv_clk = {
	.cdiv.reg = 0x2e020,
	.cdiv.shift = 4,
	.cdiv.width = 4,
	.cdiv.clkr = {
		.enable_reg = 0x2e000,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "ddrpllapss",
			.parent_data = &(const struct clk_parent_data){
				.fw_name = "xo",
				.name = "xo",
			},
			.num_parents = 1,
			.ops = &clk_regmap_cpu_div_ops,
		},
	},
	.freq_tbl = ftbl_apss_ddr_pll,
	.pll_vco = &gcc_apss_ddrpll_vco,
};

/* Calculates the rate for PLL divider.
 * If the divider value is not fixed then it gets the actual divider value
 * from divider table. Then, it calculate the clock rate by dividing the
 * parent rate with actual divider value.
 */
static unsigned long
clk_regmap_clk_div_recalc_rate(struct clk_hw *hw,
			       unsigned long parent_rate)
{
	struct clk_fepll *pll = to_clk_fepll(hw);
	u32 cdiv, pre_div = 1;
	u64 rate;
	const struct clk_div_table *clkt;

	if (pll->fixed_div) {
		pre_div = pll->fixed_div;
	} else {
		regmap_read(pll->cdiv.clkr.regmap, pll->cdiv.reg, &cdiv);
		cdiv = (cdiv >> pll->cdiv.shift) & (BIT(pll->cdiv.width) - 1);

		for (clkt = pll->div_table; clkt->div; clkt++) {
			if (clkt->val == cdiv)
				pre_div = clkt->div;
		}
	}

	rate = clk_fepll_vco_calc_rate(pll, parent_rate);
	do_div(rate, pre_div);

	return rate;
};

static const struct clk_ops clk_fepll_div_ops = {
	.recalc_rate = clk_regmap_clk_div_recalc_rate,
};

static struct clk_fepll gcc_apss_sdcc_clk = {
	.fixed_div = 28,
	.cdiv.clkr = {
		.hw.init = &(struct clk_init_data){
			.name = "ddrpllsdcc",
			.parent_data = &(const struct clk_parent_data){
				.fw_name = "xo",
				.name = "xo",
			},
			.num_parents = 1,
			.ops = &clk_fepll_div_ops,
		},
	},
	.pll_vco = &gcc_apss_ddrpll_vco,
};

static struct clk_fepll gcc_fepll125_clk = {
	.fixed_div = 32,
	.cdiv.clkr = {
		.hw.init = &(struct clk_init_data){
			.name = "fepll125",
			.parent_data = &(const struct clk_parent_data){
				.fw_name = "xo",
				.name = "xo",
			},
			.num_parents = 1,
			.ops = &clk_fepll_div_ops,
		},
	},
	.pll_vco = &gcc_fepll_vco,
};

static struct clk_fepll gcc_fepll125dly_clk = {
	.fixed_div = 32,
	.cdiv.clkr = {
		.hw.init = &(struct clk_init_data){
			.name = "fepll125dly",
			.parent_data = &(const struct clk_parent_data){
				.fw_name = "xo",
				.name = "xo",
			},
			.num_parents = 1,
			.ops = &clk_fepll_div_ops,
		},
	},
	.pll_vco = &gcc_fepll_vco,
};

static struct clk_fepll gcc_fepll200_clk = {
	.fixed_div = 20,
	.cdiv.clkr = {
		.hw.init = &(struct clk_init_data){
			.name = "fepll200",
			.parent_data = &(const struct clk_parent_data){
				.fw_name = "xo",
				.name = "xo",
			},
			.num_parents = 1,
			.ops = &clk_fepll_div_ops,
		},
	},
	.pll_vco = &gcc_fepll_vco,
};

static struct clk_fepll gcc_fepll500_clk = {
	.fixed_div = 8,
	.cdiv.clkr = {
		.hw.init = &(struct clk_init_data){
			.name = "fepll500",
			.parent_data = &(const struct clk_parent_data){
				.fw_name = "xo",
				.name = "xo",
			},
			.num_parents = 1,
			.ops = &clk_fepll_div_ops,
		},
	},
	.pll_vco = &gcc_fepll_vco,
};

static const struct clk_div_table fepllwcss_clk_div_table[] = {
	{ 0, 15 },
	{ 1, 16 },
	{ 2, 18 },
	{ 3, 20 },
	{ },
};

static struct clk_fepll gcc_fepllwcss2g_clk = {
	.cdiv.reg = 0x2f020,
	.cdiv.shift = 8,
	.cdiv.width = 2,
	.cdiv.clkr = {
		.hw.init = &(struct clk_init_data){
			.name = "fepllwcss2g",
			.parent_data = &(const struct clk_parent_data){
				.fw_name = "xo",
				.name = "xo",
			},
			.num_parents = 1,
			.ops = &clk_fepll_div_ops,
		},
	},
	.div_table = fepllwcss_clk_div_table,
	.pll_vco = &gcc_fepll_vco,
};

static struct clk_fepll gcc_fepllwcss5g_clk = {
	.cdiv.reg = 0x2f020,
	.cdiv.shift = 12,
	.cdiv.width = 2,
	.cdiv.clkr = {
		.hw.init = &(struct clk_init_data){
			.name = "fepllwcss5g",
			.parent_data = &(const struct clk_parent_data){
				.fw_name = "xo",
				.name = "xo",
			},
			.num_parents = 1,
			.ops = &clk_fepll_div_ops,
		},
	},
	.div_table = fepllwcss_clk_div_table,
	.pll_vco = &gcc_fepll_vco,
};

static struct parent_map gcc_xo_200_500_map[] = {
	{ P_XO, 0 },
	{ P_FEPLL200, 1 },
	{ P_FEPLL500, 2 },
};

static const struct clk_parent_data gcc_xo_200_500[] = {
	{ .fw_name = "xo", .name = "xo" },
	{ .hw = &gcc_fepll200_clk.cdiv.clkr.hw },
	{ .hw = &gcc_fepll500_clk.cdiv.clkr.hw },
};

static const struct freq_tbl ftbl_gcc_pcnoc_ahb_clk[] = {
	F(48000000,  P_XO,	 1, 0, 0),
	F(100000000, P_FEPLL200, 2, 0, 0),
	{ }
};

static struct clk_rcg2 gcc_pcnoc_ahb_clk_src = {
	.cmd_rcgr = 0x21024,
	.hid_width = 5,
	.parent_map = gcc_xo_200_500_map,
	.freq_tbl = ftbl_gcc_pcnoc_ahb_clk,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gcc_pcnoc_ahb_clk_src",
		.parent_data = gcc_xo_200_500,
		.num_parents = ARRAY_SIZE(gcc_xo_200_500),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_branch pcnoc_clk_src = {
	.halt_reg = 0x21030,
	.clkr = {
		.enable_reg = 0x21030,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "pcnoc_clk_src",
			.parent_hws = (const struct clk_hw *[]){
				&gcc_pcnoc_ahb_clk_src.clkr.hw },
			.num_parents = 1,
			.ops = &clk_branch2_ops,
			.flags = CLK_SET_RATE_PARENT |
				CLK_IS_CRITICAL,
		},
	},
};

static struct parent_map gcc_xo_200_map[] = {
	{  P_XO, 0 },
	{  P_FEPLL200, 1 },
};

static const struct clk_parent_data gcc_xo_200[] = {
	{ .fw_name = "xo", .name = "xo" },
	{ .hw = &gcc_fepll200_clk.cdiv.clkr.hw },
};

static const struct freq_tbl ftbl_gcc_audio_pwm_clk[] = {
	F(48000000, P_XO, 1, 0, 0),
	F(200000000, P_FEPLL200, 1, 0, 0),
	{ }
};

static struct clk_rcg2 audio_clk_src = {
	.cmd_rcgr = 0x1b000,
	.hid_width = 5,
	.parent_map = gcc_xo_200_map,
	.freq_tbl = ftbl_gcc_audio_pwm_clk,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "audio_clk_src",
		.parent_data = gcc_xo_200,
		.num_parents = ARRAY_SIZE(gcc_xo_200),
		.ops = &clk_rcg2_ops,

	},
};

static struct clk_branch gcc_audio_ahb_clk = {
	.halt_reg = 0x1b010,
	.clkr = {
		.enable_reg = 0x1b010,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_audio_ahb_clk",
			.parent_hws = (const struct clk_hw *[]){
				&pcnoc_clk_src.clkr.hw },
			.flags = CLK_SET_RATE_PARENT,
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_audio_pwm_clk = {
	.halt_reg = 0x1b00C,
	.clkr = {
		.enable_reg = 0x1b00C,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_audio_pwm_clk",
			.parent_hws = (const struct clk_hw *[]){
				&audio_clk_src.clkr.hw },
			.flags = CLK_SET_RATE_PARENT,
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static const struct freq_tbl ftbl_gcc_blsp1_qup1_2_i2c_apps_clk[] = {
	F(19050000, P_FEPLL200, 10.5, 1, 1),
	{ }
};

static struct clk_rcg2 blsp1_qup1_i2c_apps_clk_src = {
	.cmd_rcgr = 0x200c,
	.hid_width = 5,
	.parent_map = gcc_xo_200_map,
	.freq_tbl = ftbl_gcc_blsp1_qup1_2_i2c_apps_clk,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "blsp1_qup1_i2c_apps_clk_src",
		.parent_data = gcc_xo_200,
		.num_parents = ARRAY_SIZE(gcc_xo_200),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_branch gcc_blsp1_qup1_i2c_apps_clk = {
	.halt_reg = 0x2008,
	.clkr = {
		.enable_reg = 0x2008,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_blsp1_qup1_i2c_apps_clk",
			.parent_hws = (const struct clk_hw *[]){
				&blsp1_qup1_i2c_apps_clk_src.clkr.hw },
			.num_parents = 1,
			.ops = &clk_branch2_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static struct clk_rcg2 blsp1_qup2_i2c_apps_clk_src = {
	.cmd_rcgr = 0x3000,
	.hid_width = 5,
	.parent_map = gcc_xo_200_map,
	.freq_tbl = ftbl_gcc_blsp1_qup1_2_i2c_apps_clk,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "blsp1_qup2_i2c_apps_clk_src",
		.parent_data = gcc_xo_200,
		.num_parents = ARRAY_SIZE(gcc_xo_200),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_branch gcc_blsp1_qup2_i2c_apps_clk = {
	.halt_reg = 0x3010,
	.clkr = {
		.enable_reg = 0x3010,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_blsp1_qup2_i2c_apps_clk",
			.parent_hws = (const struct clk_hw *[]){
				&blsp1_qup2_i2c_apps_clk_src.clkr.hw },
			.num_parents = 1,
			.ops = &clk_branch2_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static struct parent_map gcc_xo_200_spi_map[] = {
	{  P_XO, 0 },
	{  P_FEPLL200, 2 },
};

static const struct clk_parent_data gcc_xo_200_spi[] = {
	{ .fw_name = "xo", .name = "xo" },
	{ .hw = &gcc_fepll200_clk.cdiv.clkr.hw },
};

static const struct freq_tbl ftbl_gcc_blsp1_qup1_2_spi_apps_clk[] = {
	F(960000, P_XO, 12, 1, 4),
	F(4800000, P_XO, 1, 1, 10),
	F(9600000, P_XO, 1, 1, 5),
	F(15000000, P_XO, 1, 1, 3),
	F(19200000, P_XO, 1, 2, 5),
	F(24000000, P_XO, 1, 1, 2),
	F(48000000, P_XO, 1, 0, 0),
	{ }
};

static struct clk_rcg2 blsp1_qup1_spi_apps_clk_src = {
	.cmd_rcgr = 0x2024,
	.mnd_width = 8,
	.hid_width = 5,
	.parent_map = gcc_xo_200_spi_map,
	.freq_tbl = ftbl_gcc_blsp1_qup1_2_spi_apps_clk,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "blsp1_qup1_spi_apps_clk_src",
		.parent_data = gcc_xo_200_spi,
		.num_parents = ARRAY_SIZE(gcc_xo_200_spi),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_branch gcc_blsp1_qup1_spi_apps_clk = {
	.halt_reg = 0x2004,
	.clkr = {
		.enable_reg = 0x2004,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_blsp1_qup1_spi_apps_clk",
			.parent_hws = (const struct clk_hw *[]){
				&blsp1_qup1_spi_apps_clk_src.clkr.hw },
			.num_parents = 1,
			.ops = &clk_branch2_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static struct clk_rcg2 blsp1_qup2_spi_apps_clk_src = {
	.cmd_rcgr = 0x3014,
	.mnd_width = 8,
	.hid_width = 5,
	.freq_tbl = ftbl_gcc_blsp1_qup1_2_spi_apps_clk,
	.parent_map = gcc_xo_200_spi_map,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "blsp1_qup2_spi_apps_clk_src",
		.parent_data = gcc_xo_200_spi,
		.num_parents = ARRAY_SIZE(gcc_xo_200_spi),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_branch gcc_blsp1_qup2_spi_apps_clk = {
	.halt_reg = 0x300c,
	.clkr = {
		.enable_reg = 0x300c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_blsp1_qup2_spi_apps_clk",
			.parent_hws = (const struct clk_hw *[]){
				&blsp1_qup2_spi_apps_clk_src.clkr.hw },
			.num_parents = 1,
			.ops = &clk_branch2_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static const struct freq_tbl ftbl_gcc_blsp1_uart1_2_apps_clk[] = {
	F(1843200, P_FEPLL200, 1, 144, 15625),
	F(3686400, P_FEPLL200, 1, 288, 15625),
	F(7372800, P_FEPLL200, 1, 576, 15625),
	F(14745600, P_FEPLL200, 1, 1152, 15625),
	F(16000000, P_FEPLL200, 1, 2, 25),
	F(24000000, P_XO, 1, 1, 2),
	F(32000000, P_FEPLL200, 1, 4, 25),
	F(40000000, P_FEPLL200, 1, 1, 5),
	F(46400000, P_FEPLL200, 1, 29, 125),
	F(48000000, P_XO, 1, 0, 0),
	{ }
};

static struct clk_rcg2 blsp1_uart1_apps_clk_src = {
	.cmd_rcgr = 0x2044,
	.mnd_width = 16,
	.hid_width = 5,
	.freq_tbl = ftbl_gcc_blsp1_uart1_2_apps_clk,
	.parent_map = gcc_xo_200_spi_map,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "blsp1_uart1_apps_clk_src",
		.parent_data = gcc_xo_200_spi,
		.num_parents = ARRAY_SIZE(gcc_xo_200_spi),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_branch gcc_blsp1_uart1_apps_clk = {
	.halt_reg = 0x203c,
	.clkr = {
		.enable_reg = 0x203c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_blsp1_uart1_apps_clk",
			.parent_hws = (const struct clk_hw *[]){
				&blsp1_uart1_apps_clk_src.clkr.hw },
			.flags = CLK_SET_RATE_PARENT,
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_rcg2 blsp1_uart2_apps_clk_src = {
	.cmd_rcgr = 0x3034,
	.mnd_width = 16,
	.hid_width = 5,
	.freq_tbl = ftbl_gcc_blsp1_uart1_2_apps_clk,
	.parent_map = gcc_xo_200_spi_map,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "blsp1_uart2_apps_clk_src",
		.parent_data = gcc_xo_200_spi,
		.num_parents = ARRAY_SIZE(gcc_xo_200_spi),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_branch gcc_blsp1_uart2_apps_clk = {
	.halt_reg = 0x302c,
	.clkr = {
		.enable_reg = 0x302c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_blsp1_uart2_apps_clk",
			.parent_hws = (const struct clk_hw *[]){
				&blsp1_uart2_apps_clk_src.clkr.hw },
			.num_parents = 1,
			.ops = &clk_branch2_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static const struct freq_tbl ftbl_gcc_gp_clk[] = {
	F(1250000,  P_FEPLL200, 1, 16, 0),
	F(2500000,  P_FEPLL200, 1,  8, 0),
	F(5000000,  P_FEPLL200, 1,  4, 0),
	{ }
};

static struct clk_rcg2 gp1_clk_src = {
	.cmd_rcgr = 0x8004,
	.mnd_width = 8,
	.hid_width = 5,
	.freq_tbl = ftbl_gcc_gp_clk,
	.parent_map = gcc_xo_200_map,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gp1_clk_src",
		.parent_data = gcc_xo_200,
		.num_parents = ARRAY_SIZE(gcc_xo_200),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_branch gcc_gp1_clk = {
	.halt_reg = 0x8000,
	.clkr = {
		.enable_reg = 0x8000,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_gp1_clk",
			.parent_hws = (const struct clk_hw *[]){
				&gp1_clk_src.clkr.hw },
			.num_parents = 1,
			.ops = &clk_branch2_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static struct clk_rcg2 gp2_clk_src = {
	.cmd_rcgr = 0x9004,
	.mnd_width = 8,
	.hid_width = 5,
	.freq_tbl = ftbl_gcc_gp_clk,
	.parent_map = gcc_xo_200_map,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gp2_clk_src",
		.parent_data = gcc_xo_200,
		.num_parents = ARRAY_SIZE(gcc_xo_200),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_branch gcc_gp2_clk = {
	.halt_reg = 0x9000,
	.clkr = {
		.enable_reg = 0x9000,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_gp2_clk",
			.parent_hws = (const struct clk_hw *[]){
				&gp2_clk_src.clkr.hw },
			.num_parents = 1,
			.ops = &clk_branch2_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static struct clk_rcg2 gp3_clk_src = {
	.cmd_rcgr = 0xa004,
	.mnd_width = 8,
	.hid_width = 5,
	.freq_tbl = ftbl_gcc_gp_clk,
	.parent_map = gcc_xo_200_map,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "gp3_clk_src",
		.parent_data = gcc_xo_200,
		.num_parents = ARRAY_SIZE(gcc_xo_200),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_branch gcc_gp3_clk = {
	.halt_reg = 0xa000,
	.clkr = {
		.enable_reg = 0xa000,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_gp3_clk",
			.parent_hws = (const struct clk_hw *[]){
				&gp3_clk_src.clkr.hw },
			.num_parents = 1,
			.ops = &clk_branch2_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static struct parent_map gcc_xo_sdcc1_500_map[] = {
	{  P_XO, 0 },
	{  P_DDRPLL, 1 },
	{  P_FEPLL500, 2 },
};

static const struct clk_parent_data gcc_xo_sdcc1_500[] = {
	{ .fw_name = "xo", .name = "xo" },
	{ .hw = &gcc_apss_sdcc_clk.cdiv.clkr.hw },
	{ .hw = &gcc_fepll500_clk.cdiv.clkr.hw },
};

static const struct freq_tbl ftbl_gcc_sdcc1_apps_clk[] = {
	F(144000,    P_XO,			1,  3, 240),
	F(400000,    P_XO,			1,  1, 0),
	F(20000000,  P_FEPLL500,		1,  1, 25),
	F(25000000,  P_FEPLL500,		1,  1, 20),
	F(50000000,  P_FEPLL500,		1,  1, 10),
	F(100000000, P_FEPLL500,		1,  1, 5),
	F(192000000, P_DDRPLL,			1,  0, 0),
	{ }
};

static struct clk_rcg2  sdcc1_apps_clk_src = {
	.cmd_rcgr = 0x18004,
	.hid_width = 5,
	.freq_tbl = ftbl_gcc_sdcc1_apps_clk,
	.parent_map = gcc_xo_sdcc1_500_map,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "sdcc1_apps_clk_src",
		.parent_data = gcc_xo_sdcc1_500,
		.num_parents = ARRAY_SIZE(gcc_xo_sdcc1_500),
		.ops = &clk_rcg2_ops,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static const struct freq_tbl ftbl_gcc_apps_clk[] = {
	F(48000000,  P_XO,         1, 0, 0),
	F(200000000, P_FEPLL200,   1, 0, 0),
	F(384000000, P_DDRPLLAPSS, 1, 0, 0),
	F(413000000, P_DDRPLLAPSS, 1, 0, 0),
	F(448000000, P_DDRPLLAPSS, 1, 0, 0),
	F(488000000, P_DDRPLLAPSS, 1, 0, 0),
	F(500000000, P_FEPLL500,   1, 0, 0),
	F(512000000, P_DDRPLLAPSS, 1, 0, 0),
	F(537000000, P_DDRPLLAPSS, 1, 0, 0),
	F(565000000, P_DDRPLLAPSS, 1, 0, 0),
	F(597000000, P_DDRPLLAPSS, 1, 0, 0),
	F(632000000, P_DDRPLLAPSS, 1, 0, 0),
	F(672000000, P_DDRPLLAPSS, 1, 0, 0),
	F(716000000, P_DDRPLLAPSS, 1, 0, 0),
	{ }
};

static struct parent_map gcc_xo_ddr_500_200_map[] = {
	{  P_XO, 0 },
	{  P_FEPLL200, 3 },
	{  P_FEPLL500, 2 },
	{  P_DDRPLLAPSS, 1 },
};

static const struct clk_parent_data gcc_xo_ddr_500_200[] = {
	{ .fw_name = "xo", .name = "xo" },
	{ .hw = &gcc_fepll200_clk.cdiv.clkr.hw },
	{ .hw = &gcc_fepll500_clk.cdiv.clkr.hw },
	{ .hw = &gcc_apss_cpu_plldiv_clk.cdiv.clkr.hw },
};

static struct clk_rcg2 apps_clk_src = {
	.cmd_rcgr = 0x1900c,
	.hid_width = 5,
	.freq_tbl = ftbl_gcc_apps_clk,
	.parent_map = gcc_xo_ddr_500_200_map,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "apps_clk_src",
		.parent_data = gcc_xo_ddr_500_200,
		.num_parents = ARRAY_SIZE(gcc_xo_ddr_500_200),
		.ops = &clk_rcg2_ops,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static const struct freq_tbl ftbl_gcc_apps_ahb_clk[] = {
	F(48000000, P_XO,	   1, 0, 0),
	F(100000000, P_FEPLL200,   2, 0, 0),
	{ }
};

static struct clk_rcg2 apps_ahb_clk_src = {
	.cmd_rcgr = 0x19014,
	.hid_width = 5,
	.parent_map = gcc_xo_200_500_map,
	.freq_tbl = ftbl_gcc_apps_ahb_clk,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "apps_ahb_clk_src",
		.parent_data = gcc_xo_200_500,
		.num_parents = ARRAY_SIZE(gcc_xo_200_500),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_branch gcc_apss_ahb_clk = {
	.halt_reg = 0x19004,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x6000,
		.enable_mask = BIT(14),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_apss_ahb_clk",
			.parent_hws = (const struct clk_hw *[]){
				&apps_ahb_clk_src.clkr.hw },
			.num_parents = 1,
			.ops = &clk_branch2_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static struct clk_branch gcc_blsp1_ahb_clk = {
	.halt_reg = 0x1008,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x6000,
		.enable_mask = BIT(10),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_blsp1_ahb_clk",
			.parent_hws = (const struct clk_hw *[]){
				&pcnoc_clk_src.clkr.hw },
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_dcd_xo_clk = {
	.halt_reg = 0x2103c,
	.clkr = {
		.enable_reg = 0x2103c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_dcd_xo_clk",
			.parent_data = &(const struct clk_parent_data){
				.fw_name = "xo",
				.name = "xo",
			},
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_boot_rom_ahb_clk = {
	.halt_reg = 0x1300c,
	.clkr = {
		.enable_reg = 0x1300c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_boot_rom_ahb_clk",
			.parent_hws = (const struct clk_hw *[]){
				&pcnoc_clk_src.clkr.hw },
			.num_parents = 1,
			.ops = &clk_branch2_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static struct clk_branch gcc_crypto_ahb_clk = {
	.halt_reg = 0x16024,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x6000,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_crypto_ahb_clk",
			.parent_hws = (const struct clk_hw *[]){
				&pcnoc_clk_src.clkr.hw },
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_crypto_axi_clk = {
	.halt_reg = 0x16020,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x6000,
		.enable_mask = BIT(1),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_crypto_axi_clk",
			.parent_hws = (const struct clk_hw *[]){
				&gcc_fepll125_clk.cdiv.clkr.hw },
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_crypto_clk = {
	.halt_reg = 0x1601c,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x6000,
		.enable_mask = BIT(2),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_crypto_clk",
			.parent_hws = (const struct clk_hw *[]){
				&gcc_fepll125_clk.cdiv.clkr.hw },
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct parent_map gcc_xo_125_dly_map[] = {
	{  P_XO, 0 },
	{  P_FEPLL125DLY, 1 },
};

static const struct clk_parent_data gcc_xo_125_dly[] = {
	{ .fw_name = "xo", .name = "xo" },
	{ .hw = &gcc_fepll125dly_clk.cdiv.clkr.hw },
};

static const struct freq_tbl ftbl_gcc_fephy_dly_clk[] = {
	F(125000000, P_FEPLL125DLY, 1, 0, 0),
	{ }
};

static struct clk_rcg2 fephy_125m_dly_clk_src = {
	.cmd_rcgr = 0x12000,
	.hid_width = 5,
	.parent_map = gcc_xo_125_dly_map,
	.freq_tbl = ftbl_gcc_fephy_dly_clk,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "fephy_125m_dly_clk_src",
		.parent_data = gcc_xo_125_dly,
		.num_parents = ARRAY_SIZE(gcc_xo_125_dly),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_branch gcc_ess_clk = {
	.halt_reg = 0x12010,
	.clkr = {
		.enable_reg = 0x12010,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_ess_clk",
			.parent_hws = (const struct clk_hw *[]){
				&fephy_125m_dly_clk_src.clkr.hw },
			.num_parents = 1,
			.ops = &clk_branch2_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static struct clk_branch gcc_imem_axi_clk = {
	.halt_reg = 0xe004,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x6000,
		.enable_mask = BIT(17),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_imem_axi_clk",
			.parent_hws = (const struct clk_hw *[]){
				&gcc_fepll200_clk.cdiv.clkr.hw },
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_imem_cfg_ahb_clk = {
	.halt_reg = 0xe008,
	.clkr = {
		.enable_reg = 0xe008,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_imem_cfg_ahb_clk",
			.parent_hws = (const struct clk_hw *[]){
				&pcnoc_clk_src.clkr.hw },
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_ahb_clk = {
	.halt_reg = 0x1d00c,
	.clkr = {
		.enable_reg = 0x1d00c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_pcie_ahb_clk",
			.parent_hws = (const struct clk_hw *[]){
				&pcnoc_clk_src.clkr.hw },
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_axi_m_clk = {
	.halt_reg = 0x1d004,
	.clkr = {
		.enable_reg = 0x1d004,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_pcie_axi_m_clk",
			.parent_hws = (const struct clk_hw *[]){
				&gcc_fepll200_clk.cdiv.clkr.hw },
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_pcie_axi_s_clk = {
	.halt_reg = 0x1d008,
	.clkr = {
		.enable_reg = 0x1d008,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_pcie_axi_s_clk",
			.parent_hws = (const struct clk_hw *[]){
				&gcc_fepll200_clk.cdiv.clkr.hw },
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_prng_ahb_clk = {
	.halt_reg = 0x13004,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x6000,
		.enable_mask = BIT(8),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_prng_ahb_clk",
			.parent_hws = (const struct clk_hw *[]){
				&pcnoc_clk_src.clkr.hw },
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qpic_ahb_clk = {
	.halt_reg = 0x1c008,
	.clkr = {
		.enable_reg = 0x1c008,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_qpic_ahb_clk",
			.parent_hws = (const struct clk_hw *[]){
				&pcnoc_clk_src.clkr.hw },
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_qpic_clk = {
	.halt_reg = 0x1c004,
	.clkr = {
		.enable_reg = 0x1c004,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_qpic_clk",
			.parent_hws = (const struct clk_hw *[]){
				&pcnoc_clk_src.clkr.hw },
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_sdcc1_ahb_clk = {
	.halt_reg = 0x18010,
	.clkr = {
		.enable_reg = 0x18010,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_sdcc1_ahb_clk",
			.parent_hws = (const struct clk_hw *[]){
				&pcnoc_clk_src.clkr.hw },
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_sdcc1_apps_clk = {
	.halt_reg = 0x1800c,
	.clkr = {
		.enable_reg = 0x1800c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_sdcc1_apps_clk",
			.parent_hws = (const struct clk_hw *[]){
				&sdcc1_apps_clk_src.clkr.hw },
			.num_parents = 1,
			.ops = &clk_branch2_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static struct clk_branch gcc_tlmm_ahb_clk = {
	.halt_reg = 0x5004,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x6000,
		.enable_mask = BIT(5),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_tlmm_ahb_clk",
			.parent_hws = (const struct clk_hw *[]){
				&pcnoc_clk_src.clkr.hw },
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb2_master_clk = {
	.halt_reg = 0x1e00c,
	.clkr = {
		.enable_reg = 0x1e00c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_usb2_master_clk",
			.parent_hws = (const struct clk_hw *[]){
				&pcnoc_clk_src.clkr.hw },
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb2_sleep_clk = {
	.halt_reg = 0x1e010,
	.clkr = {
		.enable_reg = 0x1e010,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_usb2_sleep_clk",
			.parent_data = &(const struct clk_parent_data){
				.fw_name = "sleep_clk",
				.name = "gcc_sleep_clk_src",
			},
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static const struct freq_tbl ftbl_gcc_usb30_mock_utmi_clk[] = {
	F(2000000, P_FEPLL200, 10, 0, 0),
	{ }
};

static struct clk_rcg2 usb30_mock_utmi_clk_src = {
	.cmd_rcgr = 0x1e000,
	.hid_width = 5,
	.parent_map = gcc_xo_200_map,
	.freq_tbl = ftbl_gcc_usb30_mock_utmi_clk,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "usb30_mock_utmi_clk_src",
		.parent_data = gcc_xo_200,
		.num_parents = ARRAY_SIZE(gcc_xo_200),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_branch gcc_usb2_mock_utmi_clk = {
	.halt_reg = 0x1e014,
	.clkr = {
		.enable_reg = 0x1e014,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_usb2_mock_utmi_clk",
			.parent_hws = (const struct clk_hw *[]){
				&usb30_mock_utmi_clk_src.clkr.hw },
			.num_parents = 1,
			.ops = &clk_branch2_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static struct clk_branch gcc_usb3_master_clk = {
	.halt_reg = 0x1e028,
	.clkr = {
		.enable_reg = 0x1e028,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_usb3_master_clk",
			.parent_hws = (const struct clk_hw *[]){
				&gcc_fepll125_clk.cdiv.clkr.hw },
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb3_sleep_clk = {
	.halt_reg = 0x1e02C,
	.clkr = {
		.enable_reg = 0x1e02C,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_usb3_sleep_clk",
			.parent_data = &(const struct clk_parent_data){
				.fw_name = "sleep_clk",
				.name = "gcc_sleep_clk_src",
			},
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gcc_usb3_mock_utmi_clk = {
	.halt_reg = 0x1e030,
	.clkr = {
		.enable_reg = 0x1e030,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_usb3_mock_utmi_clk",
			.parent_hws = (const struct clk_hw *[]){
				&usb30_mock_utmi_clk_src.clkr.hw },
			.num_parents = 1,
			.ops = &clk_branch2_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static struct parent_map gcc_xo_wcss2g_map[] = {
	{  P_XO, 0 },
	{  P_FEPLLWCSS2G, 1 },
};

static const struct clk_parent_data gcc_xo_wcss2g[] = {
	{ .fw_name = "xo", .name = "xo" },
	{ .hw = &gcc_fepllwcss2g_clk.cdiv.clkr.hw },
};

static const struct freq_tbl ftbl_gcc_wcss2g_clk[] = {
	F(48000000, P_XO, 1, 0, 0),
	F(250000000, P_FEPLLWCSS2G, 1, 0, 0),
	{ }
};

static struct clk_rcg2 wcss2g_clk_src = {
	.cmd_rcgr = 0x1f000,
	.hid_width = 5,
	.freq_tbl = ftbl_gcc_wcss2g_clk,
	.parent_map = gcc_xo_wcss2g_map,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "wcss2g_clk_src",
		.parent_data = gcc_xo_wcss2g,
		.num_parents = ARRAY_SIZE(gcc_xo_wcss2g),
		.ops = &clk_rcg2_ops,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_branch gcc_wcss2g_clk = {
	.halt_reg = 0x1f00C,
	.clkr = {
		.enable_reg = 0x1f00C,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_wcss2g_clk",
			.parent_hws = (const struct clk_hw *[]){
				&wcss2g_clk_src.clkr.hw },
			.num_parents = 1,
			.ops = &clk_branch2_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static struct clk_branch gcc_wcss2g_ref_clk = {
	.halt_reg = 0x1f00C,
	.clkr = {
		.enable_reg = 0x1f00C,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_wcss2g_ref_clk",
			.parent_data = &(const struct clk_parent_data){
				.fw_name = "xo",
				.name = "xo",
			},
			.num_parents = 1,
			.ops = &clk_branch2_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static struct clk_branch gcc_wcss2g_rtc_clk = {
	.halt_reg = 0x1f010,
	.clkr = {
		.enable_reg = 0x1f010,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_wcss2g_rtc_clk",
			.parent_data = &(const struct clk_parent_data){
				.fw_name = "sleep_clk",
				.name = "gcc_sleep_clk_src",
			},
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct parent_map gcc_xo_wcss5g_map[] = {
	{  P_XO, 0 },
	{  P_FEPLLWCSS5G, 1 },
};

static const struct clk_parent_data gcc_xo_wcss5g[] = {
	{ .fw_name = "xo", .name = "xo" },
	{ .hw = &gcc_fepllwcss5g_clk.cdiv.clkr.hw },
};

static const struct freq_tbl ftbl_gcc_wcss5g_clk[] = {
	F(48000000, P_XO, 1, 0, 0),
	F(250000000, P_FEPLLWCSS5G, 1, 0, 0),
	{ }
};

static struct clk_rcg2 wcss5g_clk_src = {
	.cmd_rcgr = 0x20000,
	.hid_width = 5,
	.parent_map = gcc_xo_wcss5g_map,
	.freq_tbl = ftbl_gcc_wcss5g_clk,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "wcss5g_clk_src",
		.parent_data = gcc_xo_wcss5g,
		.num_parents = ARRAY_SIZE(gcc_xo_wcss5g),
		.ops = &clk_rcg2_ops,
	},
};

static struct clk_branch gcc_wcss5g_clk = {
	.halt_reg = 0x2000c,
	.clkr = {
		.enable_reg = 0x2000c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_wcss5g_clk",
			.parent_hws = (const struct clk_hw *[]){
				&wcss5g_clk_src.clkr.hw },
			.num_parents = 1,
			.ops = &clk_branch2_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static struct clk_branch gcc_wcss5g_ref_clk = {
	.halt_reg = 0x2000c,
	.clkr = {
		.enable_reg = 0x2000c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_wcss5g_ref_clk",
			.parent_data = &(const struct clk_parent_data){
				.fw_name = "xo",
				.name = "xo",
			},
			.num_parents = 1,
			.ops = &clk_branch2_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static struct clk_branch gcc_wcss5g_rtc_clk = {
	.halt_reg = 0x20010,
	.clkr = {
		.enable_reg = 0x20010,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gcc_wcss5g_rtc_clk",
			.parent_data = &(const struct clk_parent_data){
				.fw_name = "sleep_clk",
				.name = "gcc_sleep_clk_src",
			},
			.num_parents = 1,
			.ops = &clk_branch2_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static struct clk_regmap *gcc_ipq4019_clocks[] = {
	[AUDIO_CLK_SRC] = &audio_clk_src.clkr,
	[BLSP1_QUP1_I2C_APPS_CLK_SRC] = &blsp1_qup1_i2c_apps_clk_src.clkr,
	[BLSP1_QUP1_SPI_APPS_CLK_SRC] = &blsp1_qup1_spi_apps_clk_src.clkr,
	[BLSP1_QUP2_I2C_APPS_CLK_SRC] = &blsp1_qup2_i2c_apps_clk_src.clkr,
	[BLSP1_QUP2_SPI_APPS_CLK_SRC] = &blsp1_qup2_spi_apps_clk_src.clkr,
	[BLSP1_UART1_APPS_CLK_SRC] = &blsp1_uart1_apps_clk_src.clkr,
	[BLSP1_UART2_APPS_CLK_SRC] = &blsp1_uart2_apps_clk_src.clkr,
	[GCC_USB3_MOCK_UTMI_CLK_SRC] = &usb30_mock_utmi_clk_src.clkr,
	[GCC_APPS_CLK_SRC] = &apps_clk_src.clkr,
	[GCC_APPS_AHB_CLK_SRC] = &apps_ahb_clk_src.clkr,
	[GP1_CLK_SRC] = &gp1_clk_src.clkr,
	[GP2_CLK_SRC] = &gp2_clk_src.clkr,
	[GP3_CLK_SRC] = &gp3_clk_src.clkr,
	[SDCC1_APPS_CLK_SRC] = &sdcc1_apps_clk_src.clkr,
	[FEPHY_125M_DLY_CLK_SRC] = &fephy_125m_dly_clk_src.clkr,
	[WCSS2G_CLK_SRC] = &wcss2g_clk_src.clkr,
	[WCSS5G_CLK_SRC] = &wcss5g_clk_src.clkr,
	[GCC_APSS_AHB_CLK] = &gcc_apss_ahb_clk.clkr,
	[GCC_AUDIO_AHB_CLK] = &gcc_audio_ahb_clk.clkr,
	[GCC_AUDIO_PWM_CLK] = &gcc_audio_pwm_clk.clkr,
	[GCC_BLSP1_AHB_CLK] = &gcc_blsp1_ahb_clk.clkr,
	[GCC_BLSP1_QUP1_I2C_APPS_CLK] = &gcc_blsp1_qup1_i2c_apps_clk.clkr,
	[GCC_BLSP1_QUP1_SPI_APPS_CLK] = &gcc_blsp1_qup1_spi_apps_clk.clkr,
	[GCC_BLSP1_QUP2_I2C_APPS_CLK] = &gcc_blsp1_qup2_i2c_apps_clk.clkr,
	[GCC_BLSP1_QUP2_SPI_APPS_CLK] = &gcc_blsp1_qup2_spi_apps_clk.clkr,
	[GCC_BLSP1_UART1_APPS_CLK] = &gcc_blsp1_uart1_apps_clk.clkr,
	[GCC_BLSP1_UART2_APPS_CLK] = &gcc_blsp1_uart2_apps_clk.clkr,
	[GCC_DCD_XO_CLK] = &gcc_dcd_xo_clk.clkr,
	[GCC_GP1_CLK] = &gcc_gp1_clk.clkr,
	[GCC_GP2_CLK] = &gcc_gp2_clk.clkr,
	[GCC_GP3_CLK] = &gcc_gp3_clk.clkr,
	[GCC_BOOT_ROM_AHB_CLK] = &gcc_boot_rom_ahb_clk.clkr,
	[GCC_CRYPTO_AHB_CLK] = &gcc_crypto_ahb_clk.clkr,
	[GCC_CRYPTO_AXI_CLK] = &gcc_crypto_axi_clk.clkr,
	[GCC_CRYPTO_CLK] = &gcc_crypto_clk.clkr,
	[GCC_ESS_CLK] = &gcc_ess_clk.clkr,
	[GCC_IMEM_AXI_CLK] = &gcc_imem_axi_clk.clkr,
	[GCC_IMEM_CFG_AHB_CLK] = &gcc_imem_cfg_ahb_clk.clkr,
	[GCC_PCIE_AHB_CLK] = &gcc_pcie_ahb_clk.clkr,
	[GCC_PCIE_AXI_M_CLK] = &gcc_pcie_axi_m_clk.clkr,
	[GCC_PCIE_AXI_S_CLK] = &gcc_pcie_axi_s_clk.clkr,
	[GCC_PRNG_AHB_CLK] = &gcc_prng_ahb_clk.clkr,
	[GCC_QPIC_AHB_CLK] = &gcc_qpic_ahb_clk.clkr,
	[GCC_QPIC_CLK] = &gcc_qpic_clk.clkr,
	[GCC_SDCC1_AHB_CLK] = &gcc_sdcc1_ahb_clk.clkr,
	[GCC_SDCC1_APPS_CLK] = &gcc_sdcc1_apps_clk.clkr,
	[GCC_TLMM_AHB_CLK] = &gcc_tlmm_ahb_clk.clkr,
	[GCC_USB2_MASTER_CLK] = &gcc_usb2_master_clk.clkr,
	[GCC_USB2_SLEEP_CLK] = &gcc_usb2_sleep_clk.clkr,
	[GCC_USB2_MOCK_UTMI_CLK] = &gcc_usb2_mock_utmi_clk.clkr,
	[GCC_USB3_MASTER_CLK] = &gcc_usb3_master_clk.clkr,
	[GCC_USB3_SLEEP_CLK] = &gcc_usb3_sleep_clk.clkr,
	[GCC_USB3_MOCK_UTMI_CLK] = &gcc_usb3_mock_utmi_clk.clkr,
	[GCC_WCSS2G_CLK] = &gcc_wcss2g_clk.clkr,
	[GCC_WCSS2G_REF_CLK] = &gcc_wcss2g_ref_clk.clkr,
	[GCC_WCSS2G_RTC_CLK] = &gcc_wcss2g_rtc_clk.clkr,
	[GCC_WCSS5G_CLK] = &gcc_wcss5g_clk.clkr,
	[GCC_WCSS5G_REF_CLK] = &gcc_wcss5g_ref_clk.clkr,
	[GCC_WCSS5G_RTC_CLK] = &gcc_wcss5g_rtc_clk.clkr,
	[GCC_SDCC_PLLDIV_CLK] = &gcc_apss_sdcc_clk.cdiv.clkr,
	[GCC_FEPLL125_CLK] = &gcc_fepll125_clk.cdiv.clkr,
	[GCC_FEPLL125DLY_CLK] = &gcc_fepll125dly_clk.cdiv.clkr,
	[GCC_FEPLL200_CLK] = &gcc_fepll200_clk.cdiv.clkr,
	[GCC_FEPLL500_CLK] = &gcc_fepll500_clk.cdiv.clkr,
	[GCC_FEPLL_WCSS2G_CLK] = &gcc_fepllwcss2g_clk.cdiv.clkr,
	[GCC_FEPLL_WCSS5G_CLK] = &gcc_fepllwcss5g_clk.cdiv.clkr,
	[GCC_APSS_CPU_PLLDIV_CLK] = &gcc_apss_cpu_plldiv_clk.cdiv.clkr,
	[GCC_PCNOC_AHB_CLK_SRC] = &gcc_pcnoc_ahb_clk_src.clkr,
	[GCC_PCNOC_AHB_CLK] = &pcnoc_clk_src.clkr,
};

static const struct qcom_reset_map gcc_ipq4019_resets[] = {
	[WIFI0_CPU_INIT_RESET] = { 0x1f008, 5 },
	[WIFI0_RADIO_SRIF_RESET] = { 0x1f008, 4 },
	[WIFI0_RADIO_WARM_RESET] = { 0x1f008, 3 },
	[WIFI0_RADIO_COLD_RESET] = { 0x1f008, 2 },
	[WIFI0_CORE_WARM_RESET] = { 0x1f008, 1 },
	[WIFI0_CORE_COLD_RESET] = { 0x1f008, 0 },
	[WIFI1_CPU_INIT_RESET] = { 0x20008, 5 },
	[WIFI1_RADIO_SRIF_RESET] = { 0x20008, 4 },
	[WIFI1_RADIO_WARM_RESET] = { 0x20008, 3 },
	[WIFI1_RADIO_COLD_RESET] = { 0x20008, 2 },
	[WIFI1_CORE_WARM_RESET] = { 0x20008, 1 },
	[WIFI1_CORE_COLD_RESET] = { 0x20008, 0 },
	[USB3_UNIPHY_PHY_ARES] = { 0x1e038, 5 },
	[USB3_HSPHY_POR_ARES] = { 0x1e038, 4 },
	[USB3_HSPHY_S_ARES] = { 0x1e038, 2 },
	[USB2_HSPHY_POR_ARES] = { 0x1e01c, 4 },
	[USB2_HSPHY_S_ARES] = { 0x1e01c, 2 },
	[PCIE_PHY_AHB_ARES] = { 0x1d010, 11 },
	[PCIE_AHB_ARES] = { 0x1d010, 10 },
	[PCIE_PWR_ARES] = { 0x1d010, 9 },
	[PCIE_PIPE_STICKY_ARES] = { 0x1d010, 8 },
	[PCIE_AXI_M_STICKY_ARES] = { 0x1d010, 7 },
	[PCIE_PHY_ARES] = { 0x1d010, 6 },
	[PCIE_PARF_XPU_ARES] = { 0x1d010, 5 },
	[PCIE_AXI_S_XPU_ARES] = { 0x1d010, 4 },
	[PCIE_AXI_M_VMIDMT_ARES] = { 0x1d010, 3 },
	[PCIE_PIPE_ARES] = { 0x1d010, 2 },
	[PCIE_AXI_S_ARES] = { 0x1d010, 1 },
	[PCIE_AXI_M_ARES] = { 0x1d010, 0 },
	[ESS_RESET] = { 0x12008, 0},
	[GCC_BLSP1_BCR] = {0x01000, 0},
	[GCC_BLSP1_QUP1_BCR] = {0x02000, 0},
	[GCC_BLSP1_UART1_BCR] = {0x02038, 0},
	[GCC_BLSP1_QUP2_BCR] = {0x03008, 0},
	[GCC_BLSP1_UART2_BCR] = {0x03028, 0},
	[GCC_BIMC_BCR] = {0x04000, 0},
	[GCC_TLMM_BCR] = {0x05000, 0},
	[GCC_IMEM_BCR] = {0x0E000, 0},
	[GCC_ESS_BCR] = {0x12008, 0},
	[GCC_PRNG_BCR] = {0x13000, 0},
	[GCC_BOOT_ROM_BCR] = {0x13008, 0},
	[GCC_CRYPTO_BCR] = {0x16000, 0},
	[GCC_SDCC1_BCR] = {0x18000, 0},
	[GCC_SEC_CTRL_BCR] = {0x1A000, 0},
	[GCC_AUDIO_BCR] = {0x1B008, 0},
	[GCC_QPIC_BCR] = {0x1C000, 0},
	[GCC_PCIE_BCR] = {0x1D000, 0},
	[GCC_USB2_BCR] = {0x1E008, 0},
	[GCC_USB2_PHY_BCR] = {0x1E018, 0},
	[GCC_USB3_BCR] = {0x1E024, 0},
	[GCC_USB3_PHY_BCR] = {0x1E034, 0},
	[GCC_SYSTEM_NOC_BCR] = {0x21000, 0},
	[GCC_PCNOC_BCR] = {0x2102C, 0},
	[GCC_DCD_BCR] = {0x21038, 0},
	[GCC_SNOC_BUS_TIMEOUT0_BCR] = {0x21064, 0},
	[GCC_SNOC_BUS_TIMEOUT1_BCR] = {0x2106C, 0},
	[GCC_SNOC_BUS_TIMEOUT2_BCR] = {0x21074, 0},
	[GCC_SNOC_BUS_TIMEOUT3_BCR] = {0x2107C, 0},
	[GCC_PCNOC_BUS_TIMEOUT0_BCR] = {0x21084, 0},
	[GCC_PCNOC_BUS_TIMEOUT1_BCR] = {0x2108C, 0},
	[GCC_PCNOC_BUS_TIMEOUT2_BCR] = {0x21094, 0},
	[GCC_PCNOC_BUS_TIMEOUT3_BCR] = {0x2109C, 0},
	[GCC_PCNOC_BUS_TIMEOUT4_BCR] = {0x210A4, 0},
	[GCC_PCNOC_BUS_TIMEOUT5_BCR] = {0x210AC, 0},
	[GCC_PCNOC_BUS_TIMEOUT6_BCR] = {0x210B4, 0},
	[GCC_PCNOC_BUS_TIMEOUT7_BCR] = {0x210BC, 0},
	[GCC_PCNOC_BUS_TIMEOUT8_BCR] = {0x210C4, 0},
	[GCC_PCNOC_BUS_TIMEOUT9_BCR] = {0x210CC, 0},
	[GCC_TCSR_BCR] = {0x22000, 0},
	[GCC_MPM_BCR] = {0x24000, 0},
	[GCC_SPDM_BCR] = {0x25000, 0},
};

static const struct regmap_config gcc_ipq4019_regmap_config = {
	.reg_bits	= 32,
	.reg_stride	= 4,
	.val_bits	= 32,
	.max_register	= 0x2ffff,
	.fast_io	= true,
};

static const struct qcom_cc_desc gcc_ipq4019_desc = {
	.config = &gcc_ipq4019_regmap_config,
	.clks = gcc_ipq4019_clocks,
	.num_clks = ARRAY_SIZE(gcc_ipq4019_clocks),
	.resets = gcc_ipq4019_resets,
	.num_resets = ARRAY_SIZE(gcc_ipq4019_resets),
};

static const struct of_device_id gcc_ipq4019_match_table[] = {
	{ .compatible = "qcom,gcc-ipq4019" },
	{ }
};
MODULE_DEVICE_TABLE(of, gcc_ipq4019_match_table);

static int
gcc_ipq4019_cpu_clk_notifier_fn(struct notifier_block *nb,
				unsigned long action, void *data)
{
	int err = 0;

	if (action == PRE_RATE_CHANGE)
		err = clk_rcg2_ops.set_parent(&apps_clk_src.clkr.hw,
					      gcc_ipq4019_cpu_safe_parent);

	return notifier_from_errno(err);
}

static struct notifier_block gcc_ipq4019_cpu_clk_notifier = {
	.notifier_call = gcc_ipq4019_cpu_clk_notifier_fn,
};

static int gcc_ipq4019_probe(struct platform_device *pdev)
{
	int err;

	err = qcom_cc_probe(pdev, &gcc_ipq4019_desc);
	if (err)
		return err;

	return devm_clk_notifier_register(&pdev->dev, apps_clk_src.clkr.hw.clk,
					  &gcc_ipq4019_cpu_clk_notifier);
}

static struct platform_driver gcc_ipq4019_driver = {
	.probe		= gcc_ipq4019_probe,
	.driver		= {
		.name	= "qcom,gcc-ipq4019",
		.of_match_table = gcc_ipq4019_match_table,
	},
};

static int __init gcc_ipq4019_init(void)
{
	return platform_driver_register(&gcc_ipq4019_driver);
}
core_initcall(gcc_ipq4019_init);

static void __exit gcc_ipq4019_exit(void)
{
	platform_driver_unregister(&gcc_ipq4019_driver);
}
module_exit(gcc_ipq4019_exit);

MODULE_ALIAS("platform:gcc-ipq4019");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("QCOM GCC IPQ4019 driver");

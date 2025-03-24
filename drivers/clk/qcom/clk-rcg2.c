// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2013, 2018, The Linux Foundation. All rights reserved.
 */

#include <linux/kernel.h>
#include <linux/bitops.h>
#include <linux/err.h>
#include <linux/bug.h>
#include <linux/export.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/delay.h>
#include <linux/rational.h>
#include <linux/regmap.h>
#include <linux/math64.h>
#include <linux/gcd.h>
#include <linux/minmax.h>
#include <linux/slab.h>

#include <asm/div64.h>

#include "clk-rcg.h"
#include "common.h"

#define CMD_REG			0x0
#define CMD_UPDATE		BIT(0)
#define CMD_ROOT_EN		BIT(1)
#define CMD_DIRTY_CFG		BIT(4)
#define CMD_DIRTY_N		BIT(5)
#define CMD_DIRTY_M		BIT(6)
#define CMD_DIRTY_D		BIT(7)
#define CMD_ROOT_OFF		BIT(31)

#define CFG_REG			0x4
#define CFG_SRC_DIV_SHIFT	0
#define CFG_SRC_DIV_LENGTH	8
#define CFG_SRC_SEL_SHIFT	8
#define CFG_SRC_SEL_MASK	(0x7 << CFG_SRC_SEL_SHIFT)
#define CFG_MODE_SHIFT		12
#define CFG_MODE_MASK		(0x3 << CFG_MODE_SHIFT)
#define CFG_MODE_DUAL_EDGE	(0x2 << CFG_MODE_SHIFT)
#define CFG_HW_CLK_CTRL_MASK	BIT(20)

#define M_REG			0x8
#define N_REG			0xc
#define D_REG			0x10

#define RCG_CFG_OFFSET(rcg)	((rcg)->cmd_rcgr + (rcg)->cfg_off + CFG_REG)
#define RCG_M_OFFSET(rcg)	((rcg)->cmd_rcgr + (rcg)->cfg_off + M_REG)
#define RCG_N_OFFSET(rcg)	((rcg)->cmd_rcgr + (rcg)->cfg_off + N_REG)
#define RCG_D_OFFSET(rcg)	((rcg)->cmd_rcgr + (rcg)->cfg_off + D_REG)

/* Dynamic Frequency Scaling */
#define MAX_PERF_LEVEL		8
#define SE_CMD_DFSR_OFFSET	0x14
#define SE_CMD_DFS_EN		BIT(0)
#define SE_PERF_DFSR(level)	(0x1c + 0x4 * (level))
#define SE_PERF_M_DFSR(level)	(0x5c + 0x4 * (level))
#define SE_PERF_N_DFSR(level)	(0x9c + 0x4 * (level))

enum freq_policy {
	FLOOR,
	CEIL,
};

static int clk_rcg2_is_enabled(struct clk_hw *hw)
{
	struct clk_rcg2 *rcg = to_clk_rcg2(hw);
	u32 cmd;
	int ret;

	ret = regmap_read(rcg->clkr.regmap, rcg->cmd_rcgr + CMD_REG, &cmd);
	if (ret)
		return ret;

	return (cmd & CMD_ROOT_OFF) == 0;
}

static u8 __clk_rcg2_get_parent(struct clk_hw *hw, u32 cfg)
{
	struct clk_rcg2 *rcg = to_clk_rcg2(hw);
	int num_parents = clk_hw_get_num_parents(hw);
	int i;

	cfg &= CFG_SRC_SEL_MASK;
	cfg >>= CFG_SRC_SEL_SHIFT;

	for (i = 0; i < num_parents; i++)
		if (cfg == rcg->parent_map[i].cfg)
			return i;

	pr_debug("%s: Clock %s has invalid parent, using default.\n",
		 __func__, clk_hw_get_name(hw));
	return 0;
}

static u8 clk_rcg2_get_parent(struct clk_hw *hw)
{
	struct clk_rcg2 *rcg = to_clk_rcg2(hw);
	u32 cfg;
	int ret;

	ret = regmap_read(rcg->clkr.regmap, RCG_CFG_OFFSET(rcg), &cfg);
	if (ret) {
		pr_debug("%s: Unable to read CFG register for %s\n",
			 __func__, clk_hw_get_name(hw));
		return 0;
	}

	return __clk_rcg2_get_parent(hw, cfg);
}

static int update_config(struct clk_rcg2 *rcg)
{
	int count, ret;
	u32 cmd;
	struct clk_hw *hw = &rcg->clkr.hw;
	const char *name = clk_hw_get_name(hw);

	ret = regmap_update_bits(rcg->clkr.regmap, rcg->cmd_rcgr + CMD_REG,
				 CMD_UPDATE, CMD_UPDATE);
	if (ret)
		return ret;

	/* Wait for update to take effect */
	for (count = 500; count > 0; count--) {
		ret = regmap_read(rcg->clkr.regmap, rcg->cmd_rcgr + CMD_REG, &cmd);
		if (ret)
			return ret;
		if (!(cmd & CMD_UPDATE))
			return 0;
		udelay(1);
	}

	WARN(1, "%s: rcg didn't update its configuration.", name);
	return -EBUSY;
}

static int clk_rcg2_set_parent(struct clk_hw *hw, u8 index)
{
	struct clk_rcg2 *rcg = to_clk_rcg2(hw);
	int ret;
	u32 cfg = rcg->parent_map[index].cfg << CFG_SRC_SEL_SHIFT;

	ret = regmap_update_bits(rcg->clkr.regmap, RCG_CFG_OFFSET(rcg),
				 CFG_SRC_SEL_MASK, cfg);
	if (ret)
		return ret;

	return update_config(rcg);
}

/**
 * convert_to_reg_val() - Convert divisor values to hardware values.
 *
 * @f: Frequency table with pure m/n/pre_div parameters.
 */
static void convert_to_reg_val(struct freq_tbl *f)
{
	f->pre_div *= 2;
	f->pre_div -= 1;
}

/**
 * calc_rate() - Calculate rate based on m/n:d values
 *
 * @rate: Parent rate.
 * @m: Multiplier.
 * @n: Divisor.
 * @mode: Use zero to ignore m/n calculation.
 * @hid_div: Pre divisor register value. Pre divisor value
 *                  relates to hid_div as pre_div = (hid_div + 1) / 2.
 *
 * Return calculated rate according to formula:
 *
 *          parent_rate     m
 *   rate = ----------- x  ---
 *            pre_div       n
 */
static unsigned long
calc_rate(unsigned long rate, u32 m, u32 n, u32 mode, u32 hid_div)
{
	if (hid_div)
		rate = mult_frac(rate, 2, hid_div + 1);

	if (mode)
		rate = mult_frac(rate, m, n);

	return rate;
}

static unsigned long
__clk_rcg2_recalc_rate(struct clk_hw *hw, unsigned long parent_rate, u32 cfg)
{
	struct clk_rcg2 *rcg = to_clk_rcg2(hw);
	u32 hid_div, m = 0, n = 0, mode = 0, mask;

	if (rcg->mnd_width) {
		mask = BIT(rcg->mnd_width) - 1;
		regmap_read(rcg->clkr.regmap, RCG_M_OFFSET(rcg), &m);
		m &= mask;
		regmap_read(rcg->clkr.regmap, RCG_N_OFFSET(rcg), &n);
		n =  ~n;
		n &= mask;
		n += m;
		mode = cfg & CFG_MODE_MASK;
		mode >>= CFG_MODE_SHIFT;
	}

	mask = BIT(rcg->hid_width) - 1;
	hid_div = cfg >> CFG_SRC_DIV_SHIFT;
	hid_div &= mask;

	return calc_rate(parent_rate, m, n, mode, hid_div);
}

static unsigned long
clk_rcg2_recalc_rate(struct clk_hw *hw, unsigned long parent_rate)
{
	struct clk_rcg2 *rcg = to_clk_rcg2(hw);
	u32 cfg;

	regmap_read(rcg->clkr.regmap, RCG_CFG_OFFSET(rcg), &cfg);

	return __clk_rcg2_recalc_rate(hw, parent_rate, cfg);
}

static int _freq_tbl_determine_rate(struct clk_hw *hw, const struct freq_tbl *f,
				    struct clk_rate_request *req,
				    enum freq_policy policy)
{
	unsigned long clk_flags, rate = req->rate;
	struct clk_hw *p;
	struct clk_rcg2 *rcg = to_clk_rcg2(hw);
	int index;

	switch (policy) {
	case FLOOR:
		f = qcom_find_freq_floor(f, rate);
		break;
	case CEIL:
		f = qcom_find_freq(f, rate);
		break;
	default:
		return -EINVAL;
	}

	if (!f)
		return -EINVAL;

	index = qcom_find_src_index(hw, rcg->parent_map, f->src);
	if (index < 0)
		return index;

	clk_flags = clk_hw_get_flags(hw);
	p = clk_hw_get_parent_by_index(hw, index);
	if (!p)
		return -EINVAL;

	if (clk_flags & CLK_SET_RATE_PARENT) {
		rate = f->freq;
		if (f->pre_div) {
			if (!rate)
				rate = req->rate;
			rate /= 2;
			rate *= f->pre_div + 1;
		}

		if (f->n) {
			u64 tmp = rate;
			tmp = tmp * f->n;
			do_div(tmp, f->m);
			rate = tmp;
		}
	} else {
		rate =  clk_hw_get_rate(p);
	}
	req->best_parent_hw = p;
	req->best_parent_rate = rate;
	req->rate = f->freq;

	return 0;
}

static const struct freq_conf *
__clk_rcg2_select_conf(struct clk_hw *hw, const struct freq_multi_tbl *f,
		       unsigned long req_rate)
{
	unsigned long rate_diff, best_rate_diff = ULONG_MAX;
	const struct freq_conf *conf, *best_conf = NULL;
	struct clk_rcg2 *rcg = to_clk_rcg2(hw);
	const char *name = clk_hw_get_name(hw);
	unsigned long parent_rate, rate;
	struct clk_hw *p;
	int index, i;

	/* Exit early if only one config is defined */
	if (f->num_confs == 1) {
		best_conf = f->confs;
		goto exit;
	}

	/* Search in each provided config the one that is near the wanted rate */
	for (i = 0, conf = f->confs; i < f->num_confs; i++, conf++) {
		index = qcom_find_src_index(hw, rcg->parent_map, conf->src);
		if (index < 0)
			continue;

		p = clk_hw_get_parent_by_index(hw, index);
		if (!p)
			continue;

		parent_rate =  clk_hw_get_rate(p);
		rate = calc_rate(parent_rate, conf->n, conf->m, conf->n, conf->pre_div);

		if (rate == req_rate) {
			best_conf = conf;
			goto exit;
		}

		rate_diff = abs_diff(req_rate, rate);
		if (rate_diff < best_rate_diff) {
			best_rate_diff = rate_diff;
			best_conf = conf;
		}
	}

	/*
	 * Very unlikely. Warn if we couldn't find a correct config
	 * due to parent not found in every config.
	 */
	if (unlikely(!best_conf)) {
		WARN(1, "%s: can't find a configuration for rate %lu\n",
		     name, req_rate);
		return ERR_PTR(-EINVAL);
	}

exit:
	return best_conf;
}

static int _freq_tbl_fm_determine_rate(struct clk_hw *hw, const struct freq_multi_tbl *f,
				       struct clk_rate_request *req)
{
	unsigned long clk_flags, rate = req->rate;
	struct clk_rcg2 *rcg = to_clk_rcg2(hw);
	const struct freq_conf *conf;
	struct clk_hw *p;
	int index;

	f = qcom_find_freq_multi(f, rate);
	if (!f || !f->confs)
		return -EINVAL;

	conf = __clk_rcg2_select_conf(hw, f, rate);
	if (IS_ERR(conf))
		return PTR_ERR(conf);
	index = qcom_find_src_index(hw, rcg->parent_map, conf->src);
	if (index < 0)
		return index;

	clk_flags = clk_hw_get_flags(hw);
	p = clk_hw_get_parent_by_index(hw, index);
	if (!p)
		return -EINVAL;

	if (clk_flags & CLK_SET_RATE_PARENT) {
		rate = f->freq;
		if (conf->pre_div) {
			if (!rate)
				rate = req->rate;
			rate /= 2;
			rate *= conf->pre_div + 1;
		}

		if (conf->n) {
			u64 tmp = rate;

			tmp = tmp * conf->n;
			do_div(tmp, conf->m);
			rate = tmp;
		}
	} else {
		rate =  clk_hw_get_rate(p);
	}

	req->best_parent_hw = p;
	req->best_parent_rate = rate;
	req->rate = f->freq;

	return 0;
}

static int clk_rcg2_determine_rate(struct clk_hw *hw,
				   struct clk_rate_request *req)
{
	struct clk_rcg2 *rcg = to_clk_rcg2(hw);

	return _freq_tbl_determine_rate(hw, rcg->freq_tbl, req, CEIL);
}

static int clk_rcg2_determine_floor_rate(struct clk_hw *hw,
					 struct clk_rate_request *req)
{
	struct clk_rcg2 *rcg = to_clk_rcg2(hw);

	return _freq_tbl_determine_rate(hw, rcg->freq_tbl, req, FLOOR);
}

static int clk_rcg2_fm_determine_rate(struct clk_hw *hw,
				      struct clk_rate_request *req)
{
	struct clk_rcg2 *rcg = to_clk_rcg2(hw);

	return _freq_tbl_fm_determine_rate(hw, rcg->freq_multi_tbl, req);
}

/**
 * clk_rcg2_split_div() - Split multiplier that doesn't fit in n neither in pre_div.
 *
 * @multiplier: Multiplier to split between n and pre_div.
 * @pre_div: Pointer to pre divisor value.
 * @n: Pointer to n divisor value.
 * @pre_div_max: Pre divisor maximum value.
 */
static inline void clk_rcg2_split_div(int multiplier, unsigned int *pre_div,
				      u16 *n, unsigned int pre_div_max)
{
	*n = mult_frac(multiplier * *n, *pre_div, pre_div_max);
	*pre_div = pre_div_max;
}

static void clk_rcg2_calc_mnd(u64 parent_rate, u64 rate, struct freq_tbl *f,
			unsigned int mnd_max, unsigned int pre_div_max)
{
	int i = 2;
	unsigned int pre_div = 1;
	unsigned long rates_gcd, scaled_parent_rate;
	u16 m, n = 1, n_candidate = 1, n_max;

	rates_gcd = gcd(parent_rate, rate);
	m = div64_u64(rate, rates_gcd);
	scaled_parent_rate = div64_u64(parent_rate, rates_gcd);
	while (scaled_parent_rate > (mnd_max + m) * pre_div_max) {
		// we're exceeding divisor's range, trying lower scale.
		if (m > 1) {
			m--;
			scaled_parent_rate = mult_frac(scaled_parent_rate, m, (m + 1));
		} else {
			// cannot lower scale, just set max divisor values.
			f->n = mnd_max + m;
			f->pre_div = pre_div_max;
			f->m = m;
			return;
		}
	}

	n_max = m + mnd_max;

	while (scaled_parent_rate > 1) {
		while (scaled_parent_rate % i == 0) {
			n_candidate *= i;
			if (n_candidate < n_max)
				n = n_candidate;
			else if (pre_div * i < pre_div_max)
				pre_div *= i;
			else
				clk_rcg2_split_div(i, &pre_div, &n, pre_div_max);

			scaled_parent_rate /= i;
		}
		i++;
	}

	f->m = m;
	f->n = n;
	f->pre_div = pre_div > 1 ? pre_div : 0;
}

static int clk_rcg2_determine_gp_rate(struct clk_hw *hw,
				   struct clk_rate_request *req)
{
	struct clk_rcg2 *rcg = to_clk_rcg2(hw);
	struct freq_tbl f_tbl = {}, *f = &f_tbl;
	int mnd_max = BIT(rcg->mnd_width) - 1;
	int hid_max = BIT(rcg->hid_width) - 1;
	struct clk_hw *parent;
	u64 parent_rate;

	parent = clk_hw_get_parent(hw);
	parent_rate = clk_get_rate(parent->clk);
	if (!parent_rate)
		return -EINVAL;

	clk_rcg2_calc_mnd(parent_rate, req->rate, f, mnd_max, hid_max / 2);
	convert_to_reg_val(f);
	req->rate = calc_rate(parent_rate, f->m, f->n, f->n, f->pre_div);

	return 0;
}

static int __clk_rcg2_configure_parent(struct clk_rcg2 *rcg, u8 src, u32 *_cfg)
{
	struct clk_hw *hw = &rcg->clkr.hw;
	int index = qcom_find_src_index(hw, rcg->parent_map, src);

	if (index < 0)
		return index;

	*_cfg &= ~CFG_SRC_SEL_MASK;
	*_cfg |= rcg->parent_map[index].cfg << CFG_SRC_SEL_SHIFT;

	return 0;
}

static int __clk_rcg2_configure_mnd(struct clk_rcg2 *rcg, const struct freq_tbl *f,
				u32 *_cfg)
{
	u32 cfg, mask, d_val, not2d_val, n_minus_m;
	int ret;

	if (rcg->mnd_width && f->n) {
		mask = BIT(rcg->mnd_width) - 1;
		ret = regmap_update_bits(rcg->clkr.regmap,
				RCG_M_OFFSET(rcg), mask, f->m);
		if (ret)
			return ret;

		ret = regmap_update_bits(rcg->clkr.regmap,
				RCG_N_OFFSET(rcg), mask, ~(f->n - f->m));
		if (ret)
			return ret;

		/* Calculate 2d value */
		d_val = f->n;

		n_minus_m = f->n - f->m;
		n_minus_m *= 2;

		d_val = clamp_t(u32, d_val, f->m, n_minus_m);
		not2d_val = ~d_val & mask;

		ret = regmap_update_bits(rcg->clkr.regmap,
				RCG_D_OFFSET(rcg), mask, not2d_val);
		if (ret)
			return ret;
	}

	mask = BIT(rcg->hid_width) - 1;
	mask |= CFG_MODE_MASK | CFG_HW_CLK_CTRL_MASK;
	cfg = f->pre_div << CFG_SRC_DIV_SHIFT;
	if (rcg->mnd_width && f->n && (f->m != f->n))
		cfg |= CFG_MODE_DUAL_EDGE;
	if (rcg->hw_clk_ctrl)
		cfg |= CFG_HW_CLK_CTRL_MASK;

	*_cfg &= ~mask;
	*_cfg |= cfg;

	return 0;
}

static int __clk_rcg2_configure(struct clk_rcg2 *rcg, const struct freq_tbl *f,
				u32 *_cfg)
{
	int ret;

	ret = __clk_rcg2_configure_parent(rcg, f->src, _cfg);
	if (ret)
		return ret;

	ret = __clk_rcg2_configure_mnd(rcg, f, _cfg);
	if (ret)
		return ret;

	return 0;
}

static int clk_rcg2_configure(struct clk_rcg2 *rcg, const struct freq_tbl *f)
{
	u32 cfg;
	int ret;

	ret = regmap_read(rcg->clkr.regmap, RCG_CFG_OFFSET(rcg), &cfg);
	if (ret)
		return ret;

	ret = __clk_rcg2_configure(rcg, f, &cfg);
	if (ret)
		return ret;

	ret = regmap_write(rcg->clkr.regmap, RCG_CFG_OFFSET(rcg), cfg);
	if (ret)
		return ret;

	return update_config(rcg);
}

static int clk_rcg2_configure_gp(struct clk_rcg2 *rcg, const struct freq_tbl *f)
{
	u32 cfg;
	int ret;

	ret = regmap_read(rcg->clkr.regmap, RCG_CFG_OFFSET(rcg), &cfg);
	if (ret)
		return ret;

	ret = __clk_rcg2_configure_mnd(rcg, f, &cfg);
	if (ret)
		return ret;

	ret = regmap_write(rcg->clkr.regmap, RCG_CFG_OFFSET(rcg), cfg);
	if (ret)
		return ret;

	return update_config(rcg);
}

static int __clk_rcg2_set_rate(struct clk_hw *hw, unsigned long rate,
			       enum freq_policy policy)
{
	struct clk_rcg2 *rcg = to_clk_rcg2(hw);
	const struct freq_tbl *f;

	switch (policy) {
	case FLOOR:
		f = qcom_find_freq_floor(rcg->freq_tbl, rate);
		break;
	case CEIL:
		f = qcom_find_freq(rcg->freq_tbl, rate);
		break;
	default:
		return -EINVAL;
	}

	if (!f)
		return -EINVAL;

	return clk_rcg2_configure(rcg, f);
}

static int __clk_rcg2_fm_set_rate(struct clk_hw *hw, unsigned long rate)
{
	struct clk_rcg2 *rcg = to_clk_rcg2(hw);
	const struct freq_multi_tbl *f;
	const struct freq_conf *conf;
	struct freq_tbl f_tbl = {};

	f = qcom_find_freq_multi(rcg->freq_multi_tbl, rate);
	if (!f || !f->confs)
		return -EINVAL;

	conf = __clk_rcg2_select_conf(hw, f, rate);
	if (IS_ERR(conf))
		return PTR_ERR(conf);

	f_tbl.freq = f->freq;
	f_tbl.src = conf->src;
	f_tbl.pre_div = conf->pre_div;
	f_tbl.m = conf->m;
	f_tbl.n = conf->n;

	return clk_rcg2_configure(rcg, &f_tbl);
}

static int clk_rcg2_set_rate(struct clk_hw *hw, unsigned long rate,
			    unsigned long parent_rate)
{
	return __clk_rcg2_set_rate(hw, rate, CEIL);
}

static int clk_rcg2_set_gp_rate(struct clk_hw *hw, unsigned long rate,
			    unsigned long parent_rate)
{
	struct clk_rcg2 *rcg = to_clk_rcg2(hw);
	int mnd_max = BIT(rcg->mnd_width) - 1;
	int hid_max = BIT(rcg->hid_width) - 1;
	struct freq_tbl f_tbl = {}, *f = &f_tbl;
	int ret;

	clk_rcg2_calc_mnd(parent_rate, rate, f, mnd_max, hid_max / 2);
	convert_to_reg_val(f);
	ret = clk_rcg2_configure_gp(rcg, f);

	return ret;
}

static int clk_rcg2_set_floor_rate(struct clk_hw *hw, unsigned long rate,
				   unsigned long parent_rate)
{
	return __clk_rcg2_set_rate(hw, rate, FLOOR);
}

static int clk_rcg2_fm_set_rate(struct clk_hw *hw, unsigned long rate,
				unsigned long parent_rate)
{
	return __clk_rcg2_fm_set_rate(hw, rate);
}

static int clk_rcg2_set_rate_and_parent(struct clk_hw *hw,
		unsigned long rate, unsigned long parent_rate, u8 index)
{
	return __clk_rcg2_set_rate(hw, rate, CEIL);
}

static int clk_rcg2_set_floor_rate_and_parent(struct clk_hw *hw,
		unsigned long rate, unsigned long parent_rate, u8 index)
{
	return __clk_rcg2_set_rate(hw, rate, FLOOR);
}

static int clk_rcg2_fm_set_rate_and_parent(struct clk_hw *hw,
		unsigned long rate, unsigned long parent_rate, u8 index)
{
	return __clk_rcg2_fm_set_rate(hw, rate);
}

static int clk_rcg2_get_duty_cycle(struct clk_hw *hw, struct clk_duty *duty)
{
	struct clk_rcg2 *rcg = to_clk_rcg2(hw);
	u32 notn_m, n, m, d, not2d, mask;

	if (!rcg->mnd_width) {
		/* 50 % duty-cycle for Non-MND RCGs */
		duty->num = 1;
		duty->den = 2;
		return 0;
	}

	regmap_read(rcg->clkr.regmap, RCG_D_OFFSET(rcg), &not2d);
	regmap_read(rcg->clkr.regmap, RCG_M_OFFSET(rcg), &m);
	regmap_read(rcg->clkr.regmap, RCG_N_OFFSET(rcg), &notn_m);

	if (!not2d && !m && !notn_m) {
		/* 50 % duty-cycle always */
		duty->num = 1;
		duty->den = 2;
		return 0;
	}

	mask = BIT(rcg->mnd_width) - 1;

	d = ~(not2d) & mask;
	d = DIV_ROUND_CLOSEST(d, 2);

	n = (~(notn_m) + m) & mask;

	duty->num = d;
	duty->den = n;

	return 0;
}

static int clk_rcg2_set_duty_cycle(struct clk_hw *hw, struct clk_duty *duty)
{
	struct clk_rcg2 *rcg = to_clk_rcg2(hw);
	u32 notn_m, n, m, d, not2d, mask, duty_per, cfg;
	int ret;

	/* Duty-cycle cannot be modified for non-MND RCGs */
	if (!rcg->mnd_width)
		return -EINVAL;

	mask = BIT(rcg->mnd_width) - 1;

	regmap_read(rcg->clkr.regmap, RCG_N_OFFSET(rcg), &notn_m);
	regmap_read(rcg->clkr.regmap, RCG_M_OFFSET(rcg), &m);
	regmap_read(rcg->clkr.regmap, RCG_CFG_OFFSET(rcg), &cfg);

	/* Duty-cycle cannot be modified if MND divider is in bypass mode. */
	if (!(cfg & CFG_MODE_MASK))
		return -EINVAL;

	n = (~(notn_m) + m) & mask;

	duty_per = (duty->num * 100) / duty->den;

	/* Calculate 2d value */
	d = DIV_ROUND_CLOSEST(n * duty_per * 2, 100);

	/*
	 * Check bit widths of 2d. If D is too big reduce duty cycle.
	 * Also make sure it is never zero.
	 */
	d = clamp_val(d, 1, mask);

	if ((d / 2) > (n - m))
		d = (n - m) * 2;
	else if ((d / 2) < (m / 2))
		d = m;

	not2d = ~d & mask;

	ret = regmap_update_bits(rcg->clkr.regmap, RCG_D_OFFSET(rcg), mask,
				 not2d);
	if (ret)
		return ret;

	return update_config(rcg);
}

const struct clk_ops clk_rcg2_ops = {
	.is_enabled = clk_rcg2_is_enabled,
	.get_parent = clk_rcg2_get_parent,
	.set_parent = clk_rcg2_set_parent,
	.recalc_rate = clk_rcg2_recalc_rate,
	.determine_rate = clk_rcg2_determine_rate,
	.set_rate = clk_rcg2_set_rate,
	.set_rate_and_parent = clk_rcg2_set_rate_and_parent,
	.get_duty_cycle = clk_rcg2_get_duty_cycle,
	.set_duty_cycle = clk_rcg2_set_duty_cycle,
};
EXPORT_SYMBOL_GPL(clk_rcg2_ops);

const struct clk_ops clk_rcg2_gp_ops = {
	.is_enabled = clk_rcg2_is_enabled,
	.get_parent = clk_rcg2_get_parent,
	.set_parent = clk_rcg2_set_parent,
	.recalc_rate = clk_rcg2_recalc_rate,
	.determine_rate = clk_rcg2_determine_gp_rate,
	.set_rate = clk_rcg2_set_gp_rate,
	.get_duty_cycle = clk_rcg2_get_duty_cycle,
	.set_duty_cycle = clk_rcg2_set_duty_cycle,
};
EXPORT_SYMBOL_GPL(clk_rcg2_gp_ops);

const struct clk_ops clk_rcg2_floor_ops = {
	.is_enabled = clk_rcg2_is_enabled,
	.get_parent = clk_rcg2_get_parent,
	.set_parent = clk_rcg2_set_parent,
	.recalc_rate = clk_rcg2_recalc_rate,
	.determine_rate = clk_rcg2_determine_floor_rate,
	.set_rate = clk_rcg2_set_floor_rate,
	.set_rate_and_parent = clk_rcg2_set_floor_rate_and_parent,
	.get_duty_cycle = clk_rcg2_get_duty_cycle,
	.set_duty_cycle = clk_rcg2_set_duty_cycle,
};
EXPORT_SYMBOL_GPL(clk_rcg2_floor_ops);

const struct clk_ops clk_rcg2_fm_ops = {
	.is_enabled = clk_rcg2_is_enabled,
	.get_parent = clk_rcg2_get_parent,
	.set_parent = clk_rcg2_set_parent,
	.recalc_rate = clk_rcg2_recalc_rate,
	.determine_rate = clk_rcg2_fm_determine_rate,
	.set_rate = clk_rcg2_fm_set_rate,
	.set_rate_and_parent = clk_rcg2_fm_set_rate_and_parent,
	.get_duty_cycle = clk_rcg2_get_duty_cycle,
	.set_duty_cycle = clk_rcg2_set_duty_cycle,
};
EXPORT_SYMBOL_GPL(clk_rcg2_fm_ops);

const struct clk_ops clk_rcg2_mux_closest_ops = {
	.determine_rate = __clk_mux_determine_rate_closest,
	.get_parent = clk_rcg2_get_parent,
	.set_parent = clk_rcg2_set_parent,
};
EXPORT_SYMBOL_GPL(clk_rcg2_mux_closest_ops);

struct frac_entry {
	int num;
	int den;
};

static const struct frac_entry frac_table_675m[] = {	/* link rate of 270M */
	{ 52, 295 },	/* 119 M */
	{ 11, 57 },	/* 130.25 M */
	{ 63, 307 },	/* 138.50 M */
	{ 11, 50 },	/* 148.50 M */
	{ 47, 206 },	/* 154 M */
	{ 31, 100 },	/* 205.25 M */
	{ 107, 269 },	/* 268.50 M */
	{ },
};

static struct frac_entry frac_table_810m[] = { /* Link rate of 162M */
	{ 31, 211 },	/* 119 M */
	{ 32, 199 },	/* 130.25 M */
	{ 63, 307 },	/* 138.50 M */
	{ 11, 60 },	/* 148.50 M */
	{ 50, 263 },	/* 154 M */
	{ 31, 120 },	/* 205.25 M */
	{ 119, 359 },	/* 268.50 M */
	{ },
};

static int clk_edp_pixel_set_rate(struct clk_hw *hw, unsigned long rate,
			      unsigned long parent_rate)
{
	struct clk_rcg2 *rcg = to_clk_rcg2(hw);
	struct freq_tbl f = *rcg->freq_tbl;
	const struct frac_entry *frac;
	int delta = 100000;
	s64 src_rate = parent_rate;
	s64 request;
	u32 mask = BIT(rcg->hid_width) - 1;
	u32 hid_div;

	if (src_rate == 810000000)
		frac = frac_table_810m;
	else
		frac = frac_table_675m;

	for (; frac->num; frac++) {
		request = rate;
		request *= frac->den;
		request = div_s64(request, frac->num);
		if ((src_rate < (request - delta)) ||
		    (src_rate > (request + delta)))
			continue;

		regmap_read(rcg->clkr.regmap, rcg->cmd_rcgr + CFG_REG,
				&hid_div);
		f.pre_div = hid_div;
		f.pre_div >>= CFG_SRC_DIV_SHIFT;
		f.pre_div &= mask;
		f.m = frac->num;
		f.n = frac->den;

		return clk_rcg2_configure(rcg, &f);
	}

	return -EINVAL;
}

static int clk_edp_pixel_set_rate_and_parent(struct clk_hw *hw,
		unsigned long rate, unsigned long parent_rate, u8 index)
{
	/* Parent index is set statically in frequency table */
	return clk_edp_pixel_set_rate(hw, rate, parent_rate);
}

static int clk_edp_pixel_determine_rate(struct clk_hw *hw,
					struct clk_rate_request *req)
{
	struct clk_rcg2 *rcg = to_clk_rcg2(hw);
	const struct freq_tbl *f = rcg->freq_tbl;
	const struct frac_entry *frac;
	int delta = 100000;
	s64 request;
	u32 mask = BIT(rcg->hid_width) - 1;
	u32 hid_div;
	int index = qcom_find_src_index(hw, rcg->parent_map, f->src);

	/* Force the correct parent */
	req->best_parent_hw = clk_hw_get_parent_by_index(hw, index);
	req->best_parent_rate = clk_hw_get_rate(req->best_parent_hw);

	if (req->best_parent_rate == 810000000)
		frac = frac_table_810m;
	else
		frac = frac_table_675m;

	for (; frac->num; frac++) {
		request = req->rate;
		request *= frac->den;
		request = div_s64(request, frac->num);
		if ((req->best_parent_rate < (request - delta)) ||
		    (req->best_parent_rate > (request + delta)))
			continue;

		regmap_read(rcg->clkr.regmap, rcg->cmd_rcgr + CFG_REG,
				&hid_div);
		hid_div >>= CFG_SRC_DIV_SHIFT;
		hid_div &= mask;

		req->rate = calc_rate(req->best_parent_rate,
				      frac->num, frac->den,
				      !!frac->den, hid_div);
		return 0;
	}

	return -EINVAL;
}

const struct clk_ops clk_edp_pixel_ops = {
	.is_enabled = clk_rcg2_is_enabled,
	.get_parent = clk_rcg2_get_parent,
	.set_parent = clk_rcg2_set_parent,
	.recalc_rate = clk_rcg2_recalc_rate,
	.set_rate = clk_edp_pixel_set_rate,
	.set_rate_and_parent = clk_edp_pixel_set_rate_and_parent,
	.determine_rate = clk_edp_pixel_determine_rate,
};
EXPORT_SYMBOL_GPL(clk_edp_pixel_ops);

static int clk_byte_determine_rate(struct clk_hw *hw,
				   struct clk_rate_request *req)
{
	struct clk_rcg2 *rcg = to_clk_rcg2(hw);
	const struct freq_tbl *f = rcg->freq_tbl;
	int index = qcom_find_src_index(hw, rcg->parent_map, f->src);
	unsigned long parent_rate, div;
	u32 mask = BIT(rcg->hid_width) - 1;
	struct clk_hw *p;

	if (req->rate == 0)
		return -EINVAL;

	req->best_parent_hw = p = clk_hw_get_parent_by_index(hw, index);
	req->best_parent_rate = parent_rate = clk_hw_round_rate(p, req->rate);

	div = DIV_ROUND_UP((2 * parent_rate), req->rate) - 1;
	div = min_t(u32, div, mask);

	req->rate = calc_rate(parent_rate, 0, 0, 0, div);

	return 0;
}

static int clk_byte_set_rate(struct clk_hw *hw, unsigned long rate,
			 unsigned long parent_rate)
{
	struct clk_rcg2 *rcg = to_clk_rcg2(hw);
	struct freq_tbl f = *rcg->freq_tbl;
	unsigned long div;
	u32 mask = BIT(rcg->hid_width) - 1;

	div = DIV_ROUND_UP((2 * parent_rate), rate) - 1;
	div = min_t(u32, div, mask);

	f.pre_div = div;

	return clk_rcg2_configure(rcg, &f);
}

static int clk_byte_set_rate_and_parent(struct clk_hw *hw,
		unsigned long rate, unsigned long parent_rate, u8 index)
{
	/* Parent index is set statically in frequency table */
	return clk_byte_set_rate(hw, rate, parent_rate);
}

const struct clk_ops clk_byte_ops = {
	.is_enabled = clk_rcg2_is_enabled,
	.get_parent = clk_rcg2_get_parent,
	.set_parent = clk_rcg2_set_parent,
	.recalc_rate = clk_rcg2_recalc_rate,
	.set_rate = clk_byte_set_rate,
	.set_rate_and_parent = clk_byte_set_rate_and_parent,
	.determine_rate = clk_byte_determine_rate,
};
EXPORT_SYMBOL_GPL(clk_byte_ops);

static int clk_byte2_determine_rate(struct clk_hw *hw,
				    struct clk_rate_request *req)
{
	struct clk_rcg2 *rcg = to_clk_rcg2(hw);
	unsigned long parent_rate, div;
	u32 mask = BIT(rcg->hid_width) - 1;
	struct clk_hw *p;
	unsigned long rate = req->rate;

	if (rate == 0)
		return -EINVAL;

	p = req->best_parent_hw;
	req->best_parent_rate = parent_rate = clk_hw_round_rate(p, rate);

	div = DIV_ROUND_UP((2 * parent_rate), rate) - 1;
	div = min_t(u32, div, mask);

	req->rate = calc_rate(parent_rate, 0, 0, 0, div);

	return 0;
}

static int clk_byte2_set_rate(struct clk_hw *hw, unsigned long rate,
			 unsigned long parent_rate)
{
	struct clk_rcg2 *rcg = to_clk_rcg2(hw);
	struct freq_tbl f = { 0 };
	unsigned long div;
	int i, num_parents = clk_hw_get_num_parents(hw);
	u32 mask = BIT(rcg->hid_width) - 1;
	u32 cfg;

	div = DIV_ROUND_UP((2 * parent_rate), rate) - 1;
	div = min_t(u32, div, mask);

	f.pre_div = div;

	regmap_read(rcg->clkr.regmap, rcg->cmd_rcgr + CFG_REG, &cfg);
	cfg &= CFG_SRC_SEL_MASK;
	cfg >>= CFG_SRC_SEL_SHIFT;

	for (i = 0; i < num_parents; i++) {
		if (cfg == rcg->parent_map[i].cfg) {
			f.src = rcg->parent_map[i].src;
			return clk_rcg2_configure(rcg, &f);
		}
	}

	return -EINVAL;
}

static int clk_byte2_set_rate_and_parent(struct clk_hw *hw,
		unsigned long rate, unsigned long parent_rate, u8 index)
{
	/* Read the hardware to determine parent during set_rate */
	return clk_byte2_set_rate(hw, rate, parent_rate);
}

const struct clk_ops clk_byte2_ops = {
	.is_enabled = clk_rcg2_is_enabled,
	.get_parent = clk_rcg2_get_parent,
	.set_parent = clk_rcg2_set_parent,
	.recalc_rate = clk_rcg2_recalc_rate,
	.set_rate = clk_byte2_set_rate,
	.set_rate_and_parent = clk_byte2_set_rate_and_parent,
	.determine_rate = clk_byte2_determine_rate,
};
EXPORT_SYMBOL_GPL(clk_byte2_ops);

static const struct frac_entry frac_table_pixel[] = {
	{ 3, 8 },
	{ 2, 9 },
	{ 4, 9 },
	{ 1, 1 },
	{ 2, 3 },
	{ }
};

static int clk_pixel_determine_rate(struct clk_hw *hw,
				    struct clk_rate_request *req)
{
	unsigned long request, src_rate;
	int delta = 100000;
	const struct frac_entry *frac = frac_table_pixel;

	for (; frac->num; frac++) {
		request = (req->rate * frac->den) / frac->num;

		src_rate = clk_hw_round_rate(req->best_parent_hw, request);
		if ((src_rate < (request - delta)) ||
			(src_rate > (request + delta)))
			continue;

		req->best_parent_rate = src_rate;
		req->rate = (src_rate * frac->num) / frac->den;
		return 0;
	}

	return -EINVAL;
}

static int clk_pixel_set_rate(struct clk_hw *hw, unsigned long rate,
		unsigned long parent_rate)
{
	struct clk_rcg2 *rcg = to_clk_rcg2(hw);
	struct freq_tbl f = { 0 };
	const struct frac_entry *frac = frac_table_pixel;
	unsigned long request;
	int delta = 100000;
	u32 mask = BIT(rcg->hid_width) - 1;
	u32 hid_div, cfg;
	int i, num_parents = clk_hw_get_num_parents(hw);

	regmap_read(rcg->clkr.regmap, rcg->cmd_rcgr + CFG_REG, &cfg);
	cfg &= CFG_SRC_SEL_MASK;
	cfg >>= CFG_SRC_SEL_SHIFT;

	for (i = 0; i < num_parents; i++)
		if (cfg == rcg->parent_map[i].cfg) {
			f.src = rcg->parent_map[i].src;
			break;
		}

	for (; frac->num; frac++) {
		request = (rate * frac->den) / frac->num;

		if ((parent_rate < (request - delta)) ||
			(parent_rate > (request + delta)))
			continue;

		regmap_read(rcg->clkr.regmap, rcg->cmd_rcgr + CFG_REG,
				&hid_div);
		f.pre_div = hid_div;
		f.pre_div >>= CFG_SRC_DIV_SHIFT;
		f.pre_div &= mask;
		f.m = frac->num;
		f.n = frac->den;

		return clk_rcg2_configure(rcg, &f);
	}
	return -EINVAL;
}

static int clk_pixel_set_rate_and_parent(struct clk_hw *hw, unsigned long rate,
		unsigned long parent_rate, u8 index)
{
	return clk_pixel_set_rate(hw, rate, parent_rate);
}

const struct clk_ops clk_pixel_ops = {
	.is_enabled = clk_rcg2_is_enabled,
	.get_parent = clk_rcg2_get_parent,
	.set_parent = clk_rcg2_set_parent,
	.recalc_rate = clk_rcg2_recalc_rate,
	.set_rate = clk_pixel_set_rate,
	.set_rate_and_parent = clk_pixel_set_rate_and_parent,
	.determine_rate = clk_pixel_determine_rate,
};
EXPORT_SYMBOL_GPL(clk_pixel_ops);

static int clk_gfx3d_determine_rate(struct clk_hw *hw,
				    struct clk_rate_request *req)
{
	struct clk_rate_request parent_req = { .min_rate = 0, .max_rate = ULONG_MAX };
	struct clk_rcg2_gfx3d *cgfx = to_clk_rcg2_gfx3d(hw);
	struct clk_hw *xo, *p0, *p1, *p2;
	unsigned long p0_rate;
	u8 mux_div = cgfx->div;
	int ret;

	p0 = cgfx->hws[0];
	p1 = cgfx->hws[1];
	p2 = cgfx->hws[2];
	/*
	 * This function does ping-pong the RCG between PLLs: if we don't
	 * have at least one fixed PLL and two variable ones,
	 * then it's not going to work correctly.
	 */
	if (WARN_ON(!p0 || !p1 || !p2))
		return -EINVAL;

	xo = clk_hw_get_parent_by_index(hw, 0);
	if (req->rate == clk_hw_get_rate(xo)) {
		req->best_parent_hw = xo;
		return 0;
	}

	if (mux_div == 0)
		mux_div = 1;

	parent_req.rate = req->rate * mux_div;

	/* This has to be a fixed rate PLL */
	p0_rate = clk_hw_get_rate(p0);

	if (parent_req.rate == p0_rate) {
		req->rate = req->best_parent_rate = p0_rate;
		req->best_parent_hw = p0;
		return 0;
	}

	if (req->best_parent_hw == p0) {
		/* Are we going back to a previously used rate? */
		if (clk_hw_get_rate(p2) == parent_req.rate)
			req->best_parent_hw = p2;
		else
			req->best_parent_hw = p1;
	} else if (req->best_parent_hw == p2) {
		req->best_parent_hw = p1;
	} else {
		req->best_parent_hw = p2;
	}

	clk_hw_get_rate_range(req->best_parent_hw,
			      &parent_req.min_rate, &parent_req.max_rate);

	if (req->min_rate > parent_req.min_rate)
		parent_req.min_rate = req->min_rate;

	if (req->max_rate < parent_req.max_rate)
		parent_req.max_rate = req->max_rate;

	ret = __clk_determine_rate(req->best_parent_hw, &parent_req);
	if (ret)
		return ret;

	req->rate = req->best_parent_rate = parent_req.rate;
	req->rate /= mux_div;

	return 0;
}

static int clk_gfx3d_set_rate_and_parent(struct clk_hw *hw, unsigned long rate,
		unsigned long parent_rate, u8 index)
{
	struct clk_rcg2_gfx3d *cgfx = to_clk_rcg2_gfx3d(hw);
	struct clk_rcg2 *rcg = &cgfx->rcg;
	u32 cfg;
	int ret;

	cfg = rcg->parent_map[index].cfg << CFG_SRC_SEL_SHIFT;
	/* On some targets, the GFX3D RCG may need to divide PLL frequency */
	if (cgfx->div > 1)
		cfg |= ((2 * cgfx->div) - 1) << CFG_SRC_DIV_SHIFT;

	ret = regmap_write(rcg->clkr.regmap, rcg->cmd_rcgr + CFG_REG, cfg);
	if (ret)
		return ret;

	return update_config(rcg);
}

static int clk_gfx3d_set_rate(struct clk_hw *hw, unsigned long rate,
			      unsigned long parent_rate)
{
	/*
	 * We should never get here; clk_gfx3d_determine_rate() should always
	 * make us use a different parent than what we're currently using, so
	 * clk_gfx3d_set_rate_and_parent() should always be called.
	 */
	return 0;
}

const struct clk_ops clk_gfx3d_ops = {
	.is_enabled = clk_rcg2_is_enabled,
	.get_parent = clk_rcg2_get_parent,
	.set_parent = clk_rcg2_set_parent,
	.recalc_rate = clk_rcg2_recalc_rate,
	.set_rate = clk_gfx3d_set_rate,
	.set_rate_and_parent = clk_gfx3d_set_rate_and_parent,
	.determine_rate = clk_gfx3d_determine_rate,
};
EXPORT_SYMBOL_GPL(clk_gfx3d_ops);

static int clk_rcg2_set_force_enable(struct clk_hw *hw)
{
	struct clk_rcg2 *rcg = to_clk_rcg2(hw);
	const char *name = clk_hw_get_name(hw);
	int ret, count;

	ret = regmap_update_bits(rcg->clkr.regmap, rcg->cmd_rcgr + CMD_REG,
				 CMD_ROOT_EN, CMD_ROOT_EN);
	if (ret)
		return ret;

	/* wait for RCG to turn ON */
	for (count = 500; count > 0; count--) {
		if (clk_rcg2_is_enabled(hw))
			return 0;

		udelay(1);
	}

	pr_err("%s: RCG did not turn on\n", name);
	return -ETIMEDOUT;
}

static int clk_rcg2_clear_force_enable(struct clk_hw *hw)
{
	struct clk_rcg2 *rcg = to_clk_rcg2(hw);

	return regmap_update_bits(rcg->clkr.regmap, rcg->cmd_rcgr + CMD_REG,
					CMD_ROOT_EN, 0);
}

static int
clk_rcg2_shared_force_enable_clear(struct clk_hw *hw, const struct freq_tbl *f)
{
	struct clk_rcg2 *rcg = to_clk_rcg2(hw);
	int ret;

	ret = clk_rcg2_set_force_enable(hw);
	if (ret)
		return ret;

	ret = clk_rcg2_configure(rcg, f);
	if (ret)
		return ret;

	return clk_rcg2_clear_force_enable(hw);
}

static int __clk_rcg2_shared_set_rate(struct clk_hw *hw, unsigned long rate,
				      unsigned long parent_rate,
				      enum freq_policy policy)
{
	struct clk_rcg2 *rcg = to_clk_rcg2(hw);
	const struct freq_tbl *f;

	switch (policy) {
	case FLOOR:
		f = qcom_find_freq_floor(rcg->freq_tbl, rate);
		break;
	case CEIL:
		f = qcom_find_freq(rcg->freq_tbl, rate);
		break;
	default:
		return -EINVAL;
	}

	/*
	 * In case clock is disabled, update the M, N and D registers, cache
	 * the CFG value in parked_cfg and don't hit the update bit of CMD
	 * register.
	 */
	if (!clk_hw_is_enabled(hw))
		return __clk_rcg2_configure(rcg, f, &rcg->parked_cfg);

	return clk_rcg2_shared_force_enable_clear(hw, f);
}

static int clk_rcg2_shared_set_rate(struct clk_hw *hw, unsigned long rate,
				    unsigned long parent_rate)
{
	return __clk_rcg2_shared_set_rate(hw, rate, parent_rate, CEIL);
}

static int clk_rcg2_shared_set_rate_and_parent(struct clk_hw *hw,
		unsigned long rate, unsigned long parent_rate, u8 index)
{
	return __clk_rcg2_shared_set_rate(hw, rate, parent_rate, CEIL);
}

static int clk_rcg2_shared_set_floor_rate(struct clk_hw *hw, unsigned long rate,
					  unsigned long parent_rate)
{
	return __clk_rcg2_shared_set_rate(hw, rate, parent_rate, FLOOR);
}

static int clk_rcg2_shared_set_floor_rate_and_parent(struct clk_hw *hw,
		unsigned long rate, unsigned long parent_rate, u8 index)
{
	return __clk_rcg2_shared_set_rate(hw, rate, parent_rate, FLOOR);
}

static int clk_rcg2_shared_enable(struct clk_hw *hw)
{
	struct clk_rcg2 *rcg = to_clk_rcg2(hw);
	int ret;

	/*
	 * Set the update bit because required configuration has already
	 * been written in clk_rcg2_shared_set_rate()
	 */
	ret = clk_rcg2_set_force_enable(hw);
	if (ret)
		return ret;

	/* Write back the stored configuration corresponding to current rate */
	ret = regmap_write(rcg->clkr.regmap, rcg->cmd_rcgr + CFG_REG, rcg->parked_cfg);
	if (ret)
		return ret;

	ret = update_config(rcg);
	if (ret)
		return ret;

	return clk_rcg2_clear_force_enable(hw);
}

static void clk_rcg2_shared_disable(struct clk_hw *hw)
{
	struct clk_rcg2 *rcg = to_clk_rcg2(hw);

	/*
	 * Store current configuration as switching to safe source would clear
	 * the SRC and DIV of CFG register
	 */
	regmap_read(rcg->clkr.regmap, rcg->cmd_rcgr + CFG_REG, &rcg->parked_cfg);

	/*
	 * Park the RCG at a safe configuration - sourced off of safe source.
	 * Force enable and disable the RCG while configuring it to safeguard
	 * against any update signal coming from the downstream clock.
	 * The current parent is still prepared and enabled at this point, and
	 * the safe source is always on while application processor subsystem
	 * is online. Therefore, the RCG can safely switch its parent.
	 */
	clk_rcg2_set_force_enable(hw);

	regmap_write(rcg->clkr.regmap, rcg->cmd_rcgr + CFG_REG,
		     rcg->safe_src_index << CFG_SRC_SEL_SHIFT);

	update_config(rcg);

	clk_rcg2_clear_force_enable(hw);
}

static u8 clk_rcg2_shared_get_parent(struct clk_hw *hw)
{
	struct clk_rcg2 *rcg = to_clk_rcg2(hw);

	/* If the shared rcg is parked use the cached cfg instead */
	if (!clk_hw_is_enabled(hw))
		return __clk_rcg2_get_parent(hw, rcg->parked_cfg);

	return clk_rcg2_get_parent(hw);
}

static int clk_rcg2_shared_set_parent(struct clk_hw *hw, u8 index)
{
	struct clk_rcg2 *rcg = to_clk_rcg2(hw);

	/* If the shared rcg is parked only update the cached cfg */
	if (!clk_hw_is_enabled(hw)) {
		rcg->parked_cfg &= ~CFG_SRC_SEL_MASK;
		rcg->parked_cfg |= rcg->parent_map[index].cfg << CFG_SRC_SEL_SHIFT;

		return 0;
	}

	return clk_rcg2_set_parent(hw, index);
}

static unsigned long
clk_rcg2_shared_recalc_rate(struct clk_hw *hw, unsigned long parent_rate)
{
	struct clk_rcg2 *rcg = to_clk_rcg2(hw);

	/* If the shared rcg is parked use the cached cfg instead */
	if (!clk_hw_is_enabled(hw))
		return __clk_rcg2_recalc_rate(hw, parent_rate, rcg->parked_cfg);

	return clk_rcg2_recalc_rate(hw, parent_rate);
}

static int clk_rcg2_shared_init(struct clk_hw *hw)
{
	/*
	 * This does a few things:
	 *
	 *  1. Sets rcg->parked_cfg to reflect the value at probe so that the
	 *     proper parent is reported from clk_rcg2_shared_get_parent().
	 *
	 *  2. Clears the force enable bit of the RCG because we rely on child
	 *     clks (branches) to turn the RCG on/off with a hardware feedback
	 *     mechanism and only set the force enable bit in the RCG when we
	 *     want to make sure the clk stays on for parent switches or
	 *     parking.
	 *
	 *  3. Parks shared RCGs on the safe source at registration because we
	 *     can't be certain that the parent clk will stay on during boot,
	 *     especially if the parent is shared. If this RCG is enabled at
	 *     boot, and the parent is turned off, the RCG will get stuck on. A
	 *     GDSC can wedge if is turned on and the RCG is stuck on because
	 *     the GDSC's controller will hang waiting for the clk status to
	 *     toggle on when it never does.
	 *
	 * The safest option here is to "park" the RCG at init so that the clk
	 * can never get stuck on or off. This ensures the GDSC can't get
	 * wedged.
	 */
	clk_rcg2_shared_disable(hw);

	return 0;
}

const struct clk_ops clk_rcg2_shared_ops = {
	.init = clk_rcg2_shared_init,
	.enable = clk_rcg2_shared_enable,
	.disable = clk_rcg2_shared_disable,
	.get_parent = clk_rcg2_shared_get_parent,
	.set_parent = clk_rcg2_shared_set_parent,
	.recalc_rate = clk_rcg2_shared_recalc_rate,
	.determine_rate = clk_rcg2_determine_rate,
	.set_rate = clk_rcg2_shared_set_rate,
	.set_rate_and_parent = clk_rcg2_shared_set_rate_and_parent,
};
EXPORT_SYMBOL_GPL(clk_rcg2_shared_ops);

const struct clk_ops clk_rcg2_shared_floor_ops = {
	.enable = clk_rcg2_shared_enable,
	.disable = clk_rcg2_shared_disable,
	.get_parent = clk_rcg2_shared_get_parent,
	.set_parent = clk_rcg2_shared_set_parent,
	.recalc_rate = clk_rcg2_shared_recalc_rate,
	.determine_rate = clk_rcg2_determine_floor_rate,
	.set_rate = clk_rcg2_shared_set_floor_rate,
	.set_rate_and_parent = clk_rcg2_shared_set_floor_rate_and_parent,
};
EXPORT_SYMBOL_GPL(clk_rcg2_shared_floor_ops);

static int clk_rcg2_shared_no_init_park(struct clk_hw *hw)
{
	struct clk_rcg2 *rcg = to_clk_rcg2(hw);

	/*
	 * Read the config register so that the parent is properly mapped at
	 * registration time.
	 */
	regmap_read(rcg->clkr.regmap, rcg->cmd_rcgr + CFG_REG, &rcg->parked_cfg);

	return 0;
}

/*
 * Like clk_rcg2_shared_ops but skip the init so that the clk frequency is left
 * unchanged at registration time.
 */
const struct clk_ops clk_rcg2_shared_no_init_park_ops = {
	.init = clk_rcg2_shared_no_init_park,
	.enable = clk_rcg2_shared_enable,
	.disable = clk_rcg2_shared_disable,
	.get_parent = clk_rcg2_shared_get_parent,
	.set_parent = clk_rcg2_shared_set_parent,
	.recalc_rate = clk_rcg2_shared_recalc_rate,
	.determine_rate = clk_rcg2_determine_rate,
	.set_rate = clk_rcg2_shared_set_rate,
	.set_rate_and_parent = clk_rcg2_shared_set_rate_and_parent,
};
EXPORT_SYMBOL_GPL(clk_rcg2_shared_no_init_park_ops);

/* Common APIs to be used for DFS based RCGR */
static void clk_rcg2_dfs_populate_freq(struct clk_hw *hw, unsigned int l,
				       struct freq_tbl *f)
{
	struct clk_rcg2 *rcg = to_clk_rcg2(hw);
	struct clk_hw *p;
	unsigned long prate = 0;
	u32 val, mask, cfg, mode, src;
	int i, num_parents;

	regmap_read(rcg->clkr.regmap, rcg->cmd_rcgr + SE_PERF_DFSR(l), &cfg);

	mask = BIT(rcg->hid_width) - 1;
	f->pre_div = 1;
	if (cfg & mask)
		f->pre_div = cfg & mask;

	src = cfg & CFG_SRC_SEL_MASK;
	src >>= CFG_SRC_SEL_SHIFT;

	num_parents = clk_hw_get_num_parents(hw);
	for (i = 0; i < num_parents; i++) {
		if (src == rcg->parent_map[i].cfg) {
			f->src = rcg->parent_map[i].src;
			p = clk_hw_get_parent_by_index(&rcg->clkr.hw, i);
			prate = clk_hw_get_rate(p);
		}
	}

	mode = cfg & CFG_MODE_MASK;
	mode >>= CFG_MODE_SHIFT;
	if (mode) {
		mask = BIT(rcg->mnd_width) - 1;
		regmap_read(rcg->clkr.regmap, rcg->cmd_rcgr + SE_PERF_M_DFSR(l),
			    &val);
		val &= mask;
		f->m = val;

		regmap_read(rcg->clkr.regmap, rcg->cmd_rcgr + SE_PERF_N_DFSR(l),
			    &val);
		val = ~val;
		val &= mask;
		val += f->m;
		f->n = val;
	}

	f->freq = calc_rate(prate, f->m, f->n, mode, f->pre_div);
}

static int clk_rcg2_dfs_populate_freq_table(struct clk_rcg2 *rcg)
{
	struct freq_tbl *freq_tbl;
	int i;

	/* Allocate space for 1 extra since table is NULL terminated */
	freq_tbl = kcalloc(MAX_PERF_LEVEL + 1, sizeof(*freq_tbl), GFP_KERNEL);
	if (!freq_tbl)
		return -ENOMEM;
	rcg->freq_tbl = freq_tbl;

	for (i = 0; i < MAX_PERF_LEVEL; i++)
		clk_rcg2_dfs_populate_freq(&rcg->clkr.hw, i, freq_tbl + i);

	return 0;
}

static int clk_rcg2_dfs_determine_rate(struct clk_hw *hw,
				   struct clk_rate_request *req)
{
	struct clk_rcg2 *rcg = to_clk_rcg2(hw);
	int ret;

	if (!rcg->freq_tbl) {
		ret = clk_rcg2_dfs_populate_freq_table(rcg);
		if (ret) {
			pr_err("Failed to update DFS tables for %s\n",
					clk_hw_get_name(hw));
			return ret;
		}
	}

	return clk_rcg2_determine_rate(hw, req);
}

static unsigned long
clk_rcg2_dfs_recalc_rate(struct clk_hw *hw, unsigned long parent_rate)
{
	struct clk_rcg2 *rcg = to_clk_rcg2(hw);
	u32 level, mask, cfg, m = 0, n = 0, mode, pre_div;

	regmap_read(rcg->clkr.regmap,
		    rcg->cmd_rcgr + SE_CMD_DFSR_OFFSET, &level);
	level &= GENMASK(4, 1);
	level >>= 1;

	if (rcg->freq_tbl)
		return rcg->freq_tbl[level].freq;

	/*
	 * Assume that parent_rate is actually the parent because
	 * we can't do any better at figuring it out when the table
	 * hasn't been populated yet. We only populate the table
	 * in determine_rate because we can't guarantee the parents
	 * will be registered with the framework until then.
	 */
	regmap_read(rcg->clkr.regmap, rcg->cmd_rcgr + SE_PERF_DFSR(level),
		    &cfg);

	mask = BIT(rcg->hid_width) - 1;
	pre_div = 1;
	if (cfg & mask)
		pre_div = cfg & mask;

	mode = cfg & CFG_MODE_MASK;
	mode >>= CFG_MODE_SHIFT;
	if (mode) {
		mask = BIT(rcg->mnd_width) - 1;
		regmap_read(rcg->clkr.regmap,
			    rcg->cmd_rcgr + SE_PERF_M_DFSR(level), &m);
		m &= mask;

		regmap_read(rcg->clkr.regmap,
			    rcg->cmd_rcgr + SE_PERF_N_DFSR(level), &n);
		n = ~n;
		n &= mask;
		n += m;
	}

	return calc_rate(parent_rate, m, n, mode, pre_div);
}

static const struct clk_ops clk_rcg2_dfs_ops = {
	.is_enabled = clk_rcg2_is_enabled,
	.get_parent = clk_rcg2_get_parent,
	.determine_rate = clk_rcg2_dfs_determine_rate,
	.recalc_rate = clk_rcg2_dfs_recalc_rate,
};

static int clk_rcg2_enable_dfs(const struct clk_rcg_dfs_data *data,
			       struct regmap *regmap)
{
	struct clk_rcg2 *rcg = data->rcg;
	struct clk_init_data *init = data->init;
	u32 val;
	int ret;

	ret = regmap_read(regmap, rcg->cmd_rcgr + SE_CMD_DFSR_OFFSET, &val);
	if (ret)
		return -EINVAL;

	if (!(val & SE_CMD_DFS_EN))
		return 0;

	/*
	 * Rate changes with consumer writing a register in
	 * their own I/O region
	 */
	init->flags |= CLK_GET_RATE_NOCACHE;
	init->ops = &clk_rcg2_dfs_ops;

	rcg->freq_tbl = NULL;

	return 0;
}

int qcom_cc_register_rcg_dfs(struct regmap *regmap,
			     const struct clk_rcg_dfs_data *rcgs, size_t len)
{
	int i, ret;

	for (i = 0; i < len; i++) {
		ret = clk_rcg2_enable_dfs(&rcgs[i], regmap);
		if (ret)
			return ret;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(qcom_cc_register_rcg_dfs);

static int clk_rcg2_dp_set_rate(struct clk_hw *hw, unsigned long rate,
			unsigned long parent_rate)
{
	struct clk_rcg2 *rcg = to_clk_rcg2(hw);
	struct freq_tbl f = { 0 };
	u32 mask = BIT(rcg->hid_width) - 1;
	u32 hid_div, cfg;
	int i, num_parents = clk_hw_get_num_parents(hw);
	unsigned long num, den;

	rational_best_approximation(parent_rate, rate,
			GENMASK(rcg->mnd_width - 1, 0),
			GENMASK(rcg->mnd_width - 1, 0), &den, &num);

	if (!num || !den)
		return -EINVAL;

	regmap_read(rcg->clkr.regmap, rcg->cmd_rcgr + CFG_REG, &cfg);
	hid_div = cfg;
	cfg &= CFG_SRC_SEL_MASK;
	cfg >>= CFG_SRC_SEL_SHIFT;

	for (i = 0; i < num_parents; i++) {
		if (cfg == rcg->parent_map[i].cfg) {
			f.src = rcg->parent_map[i].src;
			break;
		}
	}

	f.pre_div = hid_div;
	f.pre_div >>= CFG_SRC_DIV_SHIFT;
	f.pre_div &= mask;

	if (num != den) {
		f.m = num;
		f.n = den;
	} else {
		f.m = 0;
		f.n = 0;
	}

	return clk_rcg2_configure(rcg, &f);
}

static int clk_rcg2_dp_set_rate_and_parent(struct clk_hw *hw,
		unsigned long rate, unsigned long parent_rate, u8 index)
{
	return clk_rcg2_dp_set_rate(hw, rate, parent_rate);
}

static int clk_rcg2_dp_determine_rate(struct clk_hw *hw,
				struct clk_rate_request *req)
{
	struct clk_rcg2 *rcg = to_clk_rcg2(hw);
	unsigned long num, den;
	u64 tmp;

	/* Parent rate is a fixed phy link rate */
	rational_best_approximation(req->best_parent_rate, req->rate,
			GENMASK(rcg->mnd_width - 1, 0),
			GENMASK(rcg->mnd_width - 1, 0), &den, &num);

	if (!num || !den)
		return -EINVAL;

	tmp = req->best_parent_rate * num;
	do_div(tmp, den);
	req->rate = tmp;

	return 0;
}

const struct clk_ops clk_dp_ops = {
	.is_enabled = clk_rcg2_is_enabled,
	.get_parent = clk_rcg2_get_parent,
	.set_parent = clk_rcg2_set_parent,
	.recalc_rate = clk_rcg2_recalc_rate,
	.set_rate = clk_rcg2_dp_set_rate,
	.set_rate_and_parent = clk_rcg2_dp_set_rate_and_parent,
	.determine_rate = clk_rcg2_dp_determine_rate,
};
EXPORT_SYMBOL_GPL(clk_dp_ops);

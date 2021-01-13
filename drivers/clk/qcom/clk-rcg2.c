// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2013, 2018, The Linux Foundation. All rights reserved.
 */

#include <linux/kernel.h>
#include <linux/bitops.h>
#include <linux/err.h>
#include <linux/bug.h>
#include <linux/export.h>
#include <linux/clk-provider.h>
#include <linux/delay.h>
#include <linux/rational.h>
#include <linux/regmap.h>
#include <linux/math64.h>
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

static u8 clk_rcg2_get_parent(struct clk_hw *hw)
{
	struct clk_rcg2 *rcg = to_clk_rcg2(hw);
	int num_parents = clk_hw_get_num_parents(hw);
	u32 cfg;
	int i, ret;

	ret = regmap_read(rcg->clkr.regmap, RCG_CFG_OFFSET(rcg), &cfg);
	if (ret)
		goto err;

	cfg &= CFG_SRC_SEL_MASK;
	cfg >>= CFG_SRC_SEL_SHIFT;

	for (i = 0; i < num_parents; i++)
		if (cfg == rcg->parent_map[i].cfg)
			return i;

err:
	pr_debug("%s: Clock %s has invalid parent, using default.\n",
		 __func__, clk_hw_get_name(hw));
	return 0;
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

/*
 * Calculate m/n:d rate
 *
 *          parent_rate     m
 *   rate = ----------- x  ---
 *            hid_div       n
 */
static unsigned long
calc_rate(unsigned long rate, u32 m, u32 n, u32 mode, u32 hid_div)
{
	if (hid_div) {
		rate *= 2;
		rate /= hid_div + 1;
	}

	if (mode) {
		u64 tmp = rate;
		tmp *= m;
		do_div(tmp, n);
		rate = tmp;
	}

	return rate;
}

static unsigned long
clk_rcg2_recalc_rate(struct clk_hw *hw, unsigned long parent_rate)
{
	struct clk_rcg2 *rcg = to_clk_rcg2(hw);
	u32 cfg, hid_div, m = 0, n = 0, mode = 0, mask;

	regmap_read(rcg->clkr.regmap, RCG_CFG_OFFSET(rcg), &cfg);

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

static int __clk_rcg2_configure(struct clk_rcg2 *rcg, const struct freq_tbl *f)
{
	u32 cfg, mask;
	struct clk_hw *hw = &rcg->clkr.hw;
	int ret, index = qcom_find_src_index(hw, rcg->parent_map, f->src);

	if (index < 0)
		return index;

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

		ret = regmap_update_bits(rcg->clkr.regmap,
				RCG_D_OFFSET(rcg), mask, ~f->n);
		if (ret)
			return ret;
	}

	mask = BIT(rcg->hid_width) - 1;
	mask |= CFG_SRC_SEL_MASK | CFG_MODE_MASK | CFG_HW_CLK_CTRL_MASK;
	cfg = f->pre_div << CFG_SRC_DIV_SHIFT;
	cfg |= rcg->parent_map[index].cfg << CFG_SRC_SEL_SHIFT;
	if (rcg->mnd_width && f->n && (f->m != f->n))
		cfg |= CFG_MODE_DUAL_EDGE;
	return regmap_update_bits(rcg->clkr.regmap, RCG_CFG_OFFSET(rcg),
					mask, cfg);
}

static int clk_rcg2_configure(struct clk_rcg2 *rcg, const struct freq_tbl *f)
{
	int ret;

	ret = __clk_rcg2_configure(rcg, f);
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

static int clk_rcg2_set_rate(struct clk_hw *hw, unsigned long rate,
			    unsigned long parent_rate)
{
	return __clk_rcg2_set_rate(hw, rate, CEIL);
}

static int clk_rcg2_set_floor_rate(struct clk_hw *hw, unsigned long rate,
				   unsigned long parent_rate)
{
	return __clk_rcg2_set_rate(hw, rate, FLOOR);
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

const struct clk_ops clk_rcg2_ops = {
	.is_enabled = clk_rcg2_is_enabled,
	.get_parent = clk_rcg2_get_parent,
	.set_parent = clk_rcg2_set_parent,
	.recalc_rate = clk_rcg2_recalc_rate,
	.determine_rate = clk_rcg2_determine_rate,
	.set_rate = clk_rcg2_set_rate,
	.set_rate_and_parent = clk_rcg2_set_rate_and_parent,
};
EXPORT_SYMBOL_GPL(clk_rcg2_ops);

const struct clk_ops clk_rcg2_floor_ops = {
	.is_enabled = clk_rcg2_is_enabled,
	.get_parent = clk_rcg2_get_parent,
	.set_parent = clk_rcg2_set_parent,
	.recalc_rate = clk_rcg2_recalc_rate,
	.determine_rate = clk_rcg2_determine_floor_rate,
	.set_rate = clk_rcg2_set_floor_rate,
	.set_rate_and_parent = clk_rcg2_set_floor_rate_and_parent,
};
EXPORT_SYMBOL_GPL(clk_rcg2_floor_ops);

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
	struct clk_rate_request parent_req = { };
	struct clk_rcg2_gfx3d *cgfx = to_clk_rcg2_gfx3d(hw);
	struct clk_hw *xo, *p0, *p1, *p2;
	unsigned long request, p0_rate;
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

	request = req->rate;
	if (cgfx->div > 1)
		parent_req.rate = request = request * cgfx->div;

	/* This has to be a fixed rate PLL */
	p0_rate = clk_hw_get_rate(p0);

	if (request == p0_rate) {
		req->rate = req->best_parent_rate = p0_rate;
		req->best_parent_hw = p0;
		return 0;
	}

	if (req->best_parent_hw == p0) {
		/* Are we going back to a previously used rate? */
		if (clk_hw_get_rate(p2) == request)
			req->best_parent_hw = p2;
		else
			req->best_parent_hw = p1;
	} else if (req->best_parent_hw == p2) {
		req->best_parent_hw = p1;
	} else {
		req->best_parent_hw = p2;
	}

	ret = __clk_determine_rate(req->best_parent_hw, &parent_req);
	if (ret)
		return ret;

	req->rate = req->best_parent_rate = parent_req.rate;
	if (cgfx->div > 1)
		req->rate /= cgfx->div;

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

static int clk_rcg2_shared_set_rate(struct clk_hw *hw, unsigned long rate,
				    unsigned long parent_rate)
{
	struct clk_rcg2 *rcg = to_clk_rcg2(hw);
	const struct freq_tbl *f;

	f = qcom_find_freq(rcg->freq_tbl, rate);
	if (!f)
		return -EINVAL;

	/*
	 * In case clock is disabled, update the CFG, M, N and D registers
	 * and don't hit the update bit of CMD register.
	 */
	if (!__clk_is_enabled(hw->clk))
		return __clk_rcg2_configure(rcg, f);

	return clk_rcg2_shared_force_enable_clear(hw, f);
}

static int clk_rcg2_shared_set_rate_and_parent(struct clk_hw *hw,
		unsigned long rate, unsigned long parent_rate, u8 index)
{
	return clk_rcg2_shared_set_rate(hw, rate, parent_rate);
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

	ret = update_config(rcg);
	if (ret)
		return ret;

	return clk_rcg2_clear_force_enable(hw);
}

static void clk_rcg2_shared_disable(struct clk_hw *hw)
{
	struct clk_rcg2 *rcg = to_clk_rcg2(hw);
	u32 cfg;

	/*
	 * Store current configuration as switching to safe source would clear
	 * the SRC and DIV of CFG register
	 */
	regmap_read(rcg->clkr.regmap, rcg->cmd_rcgr + CFG_REG, &cfg);

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

	/* Write back the stored configuration corresponding to current rate */
	regmap_write(rcg->clkr.regmap, rcg->cmd_rcgr + CFG_REG, cfg);
}

const struct clk_ops clk_rcg2_shared_ops = {
	.enable = clk_rcg2_shared_enable,
	.disable = clk_rcg2_shared_disable,
	.get_parent = clk_rcg2_get_parent,
	.set_parent = clk_rcg2_set_parent,
	.recalc_rate = clk_rcg2_recalc_rate,
	.determine_rate = clk_rcg2_determine_rate,
	.set_rate = clk_rcg2_shared_set_rate,
	.set_rate_and_parent = clk_rcg2_shared_set_rate_and_parent,
};
EXPORT_SYMBOL_GPL(clk_rcg2_shared_ops);

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

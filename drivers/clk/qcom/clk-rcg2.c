// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2013, 2016-2018, 2020-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2024, Qualcomm Innovation Center, Inc. All rights reserved.
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
#include <linux/minmax.h>
#include <linux/slab.h>

#include <asm/div64.h>
#include <soc/qcom/crm.h>
#include <soc/qcom/tcs.h>

#include "clk-rcg.h"
#include "common.h"
#include "clk-debug.h"

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

/* Cesta configuration*/
#define MAX_VCD_PER_CRM	9
#define MAX_PERF_LEVEL_PER_VCD	8
#define MAX_PERF_OL_PER_VCD	4
#define MAX_CRM_SW_DRV_STATE	3

#define CLK_RCG_CRMC_CFG_RCGR_OFFSET 0x110
#define CLK_RCG_CRMC_PERF_LEVEL_PLL_L_VAL_LUT_OFFSET 0x138
#define CLK_RCG_CRMC_CURR_PERF_OL_OFFSET 0x0c
#define CLK_RCG_CRMC_CURR_PERF_OL(rcg_index) \
	(CLK_RCG_CRMC_CURR_PERF_OL_OFFSET + ((rcg_index) * 0x200))
#define CLK_RCG_CRMC_CFG_RCGR(rcg_index, level) \
	(CLK_RCG_CRMC_CFG_RCGR_OFFSET + ((rcg_index) * 0x200) + (0x4 * (level)))
#define CLK_RCG_CRMC_PERF_LEVEL_PLL_L_VAL_LUT(rcg_index, level) \
	(CLK_RCG_CRMC_PERF_LEVEL_PLL_L_VAL_LUT_OFFSET + ((rcg_index) * 0x200) + (0x4 * (level)))

#define CLK_RCG_CURR_PERF_OL_MASK 0x07
#define PLL_L_VAL_MASK	GENMASK(7, 0)
#define PLL_ALPHA_VAL_MASK	GENMASK(31, 16)
#define PLL_ALPHA_VAL_SHIFT	16

enum freq_policy {
	FLOOR,
	CEIL,
};

static struct freq_tbl cxo_f = {
	.freq = 19200000,
	.src = 0,
	.pre_div = 1,
	.m = 0,
	.n = 0,
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

static int get_update_timeout(const struct clk_rcg2 *rcg)
{
	int timeout = 0;

	/*
	 * The time it takes an RCG to update is roughly 3 clock cycles of the
	 * old and new clock rates.
	 */
	if (rcg->current_freq)
		timeout += 3 * (1000000 / rcg->current_freq);
	if (rcg->configured_freq)
		timeout += 3 * (1000000 / rcg->configured_freq);

	return max(timeout, 500);
}

static int update_config(struct clk_rcg2 *rcg)
{
	int timeout, count, ret;
	u32 cmd;
	struct clk_hw *hw = &rcg->clkr.hw;

	ret = regmap_update_bits(rcg->clkr.regmap, rcg->cmd_rcgr + CMD_REG,
				 CMD_UPDATE, CMD_UPDATE);
	if (ret)
		return ret;

	timeout = get_update_timeout(rcg);

	/* Wait for update to take effect */
	for (count = timeout; count > 0; count--) {
		ret = regmap_read(rcg->clkr.regmap, rcg->cmd_rcgr + CMD_REG, &cmd);
		if (ret)
			return ret;
		if (!(cmd & CMD_UPDATE))
			return 0;
		udelay(1);
	}

	WARN_CLK(hw, 1, "rcg didn't update its configuration after %d us.", timeout);
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

static int clk_rcg2_set_force_enable(struct clk_hw *hw)
{
	struct clk_rcg2 *rcg = to_clk_rcg2(hw);
	int ret = 0, count = 500;

	ret = regmap_update_bits(rcg->clkr.regmap, rcg->cmd_rcgr + CMD_REG,
					CMD_ROOT_EN, CMD_ROOT_EN);
	if (ret)
		return ret;

	for (; count > 0; count--) {
		if (clk_rcg2_is_enabled(hw))
			return ret;
		/* Delay for 1usec and retry polling the status bit */
		udelay(1);
	}

	WARN_CLK(hw, 1, "rcg didn't turn on.");
	return ret;
}

static int clk_rcg2_clear_force_enable(struct clk_hw *hw)
{
	struct clk_rcg2 *rcg = to_clk_rcg2(hw);

	return regmap_update_bits(rcg->clkr.regmap, rcg->cmd_rcgr + CMD_REG,
					CMD_ROOT_EN, 0);
}

static bool clk_rcg2_is_force_enabled(struct clk_hw *hw)
{
	struct clk_rcg2 *rcg = to_clk_rcg2(hw);
	u32 val = 0;

	regmap_read(rcg->clkr.regmap, rcg->cmd_rcgr + CMD_REG, &val);

	return val & CMD_ROOT_EN;
}

static int prepare_enable_rcg_srcs(struct clk *curr, struct clk *new)
{
	int rc = 0;

	rc = clk_prepare(curr);
	if (rc)
		return rc;

	rc = clk_prepare(new);
	if (rc)
		goto err_new_src_prepare;

	rc = clk_enable(curr);
	if (rc)
		goto err_curr_src_enable;

	rc = clk_enable(new);
	if (rc)
		goto err_new_src_enable;

	return rc;

err_new_src_enable:
	clk_disable(curr);
err_curr_src_enable:
	clk_unprepare(new);
err_new_src_prepare:
	clk_unprepare(curr);

	return rc;
}

static void disable_unprepare_rcg_srcs(struct clk *curr, struct clk *new)
{
	clk_disable(new);
	clk_disable(curr);

	clk_unprepare(new);
	clk_unprepare(curr);
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
__clk_rcg2_recalc_rate(struct clk_hw *hw, unsigned long parent_rate, u32 cfg)
{
	struct clk_rcg2 *rcg = to_clk_rcg2(hw);
	const struct freq_tbl *f_curr;
	u32 src, hid_div, m = 0, n = 0, mode = 0, mask;
	unsigned long rrate = 0;

	src = cfg;
	src &= CFG_SRC_SEL_MASK;
	src >>= CFG_SRC_SEL_SHIFT;

	if (rcg->enable_safe_config && (!clk_hw_is_prepared(hw)
				|| !clk_hw_is_enabled(hw)) && !src) {
		if (!rcg->current_freq)
			rcg->current_freq = cxo_f.freq;
		return rcg->current_freq;
	}

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

	if (rcg->enable_safe_config && !src) {
		f_curr = qcom_find_freq(rcg->freq_tbl, rcg->current_freq);
		if (!f_curr)
			return -EINVAL;

		hid_div = f_curr->pre_div;
	} else {
		mask = BIT(rcg->hid_width) - 1;
		hid_div = cfg >> CFG_SRC_DIV_SHIFT;
		hid_div &= mask;
	}

	rrate = calc_rate(parent_rate, m, n, mode, hid_div);

	/*
	 * Check to cover the case when the RCG has been initialized to a
	 * non-CXO frequency before the clock driver has taken control of it.
	 */
	if (rcg->enable_safe_config && !rcg->current_freq)
		rcg->current_freq = rrate;

	return rrate;
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
	struct clk_rate_request parent_req = { };
	struct clk_hw *p;
	struct clk_rcg2 *rcg = to_clk_rcg2(hw);
	int index, ret = 0;

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
	req->best_parent_rate = clk_hw_round_rate(p, rate);
	req->rate = f->freq;

	if (f->src_freq != FIXED_FREQ_SRC) {
		rate = parent_req.rate = f->src_freq;
		parent_req.best_parent_hw = p;
		ret = __clk_determine_rate(p, &parent_req);
		if (ret)
			return ret;

		ret = clk_set_rate(p->clk, parent_req.rate);
		if (ret) {
			pr_err("Failed set rate(%lu) on parent for non-fixed source\n",
							parent_req.rate);
			return ret;
		}
	}

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

static int __clk_rcg2_configure(struct clk_rcg2 *rcg, const struct freq_tbl *f,
				u32 *_cfg)
{
	u32 cfg, mask, d_val, not2d_val, n_minus_m;
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
	mask |= CFG_SRC_SEL_MASK | CFG_MODE_MASK | CFG_HW_CLK_CTRL_MASK;
	cfg = f->pre_div << CFG_SRC_DIV_SHIFT;
	cfg |= rcg->parent_map[index].cfg << CFG_SRC_SEL_SHIFT;
	if (rcg->mnd_width && f->n && (f->m != f->n))
		cfg |= CFG_MODE_DUAL_EDGE;

	if (rcg->flags & HW_CLK_CTRL_MODE)
		cfg |= CFG_HW_CLK_CTRL_MASK;

	*_cfg &= ~mask;
	*_cfg |= cfg;

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

	rcg->configured_freq = f->freq;

	return update_config(rcg);
}

static void clk_rcg2_list_registers(struct seq_file *f, struct clk_hw *hw)
{
	struct clk_rcg2 *rcg = to_clk_rcg2(hw);
	static struct clk_register_data *data;
	int i, val;

	static struct clk_register_data data1[] = {
		{"CMD_RCGR", 0x0},
		{"CFG_RCGR", 0x4},
		{ },
	};

	static struct clk_register_data data2[] = {
		{"CMD_RCGR", 0x0},
		{"CFG_RCGR", 0x4},
		{"M_VAL", 0x8},
		{"N_VAL", 0xC},
		{"D_VAL", 0x10},
		{ },
	};

	static struct clk_register_data data3[] = {
		{"CMD_RCGR", 0x0},
		{"CFG_RCGR", 0x4},
		{"M_VAL", 0x8},
		{"N_VAL", 0xC},
		{"D_VAL", 0x10},
		{"CMD_DFSR", 0x14},
		{ },
	};

	if (rcg->flags & DFS_SUPPORT)
		data = data3;
	else if (rcg->mnd_width)
		data = data2;
	else
		data = data1;

	for (i = 0; data[i].name != NULL; i++) {
		regmap_read(rcg->clkr.regmap, (rcg->cmd_rcgr +
				data[i].offset), &val);
		clock_debug_output(f, "%20s: 0x%.8x\n", data[i].name, val);
	}

}

/* Return the nth supported frequency for a given clock. */
static long clk_rcg2_list_rate(struct clk_hw *hw, unsigned int n,
		unsigned long fmax)
{
	struct clk_rcg2 *rcg = to_clk_rcg2(hw);
	const struct freq_tbl *f = rcg->freq_tbl;
	size_t freq_tbl_size = 0;

	if (!f)
		return -ENXIO;

	for (; f->freq; f++)
		freq_tbl_size++;

	if (n > freq_tbl_size - 1)
		return -EINVAL;

	return (rcg->freq_tbl + n)->freq;
}

static int __clk_rcg2_set_rate(struct clk_hw *hw, unsigned long rate,
			       enum freq_policy policy)
{
	struct clk_rcg2 *rcg = to_clk_rcg2(hw);
	const struct freq_tbl *f, *f_curr;
	int ret, curr_src_index, new_src_index;
	struct clk_hw *curr_src = NULL, *new_src = NULL;
	bool force_enabled = false;

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

	/*
	 * Return if the RCG is currently disabled. This configuration update
	 * will happen as part of the RCG enable sequence.
	 */
	if (rcg->enable_safe_config && !clk_hw_is_prepared(hw)) {
		rcg->current_freq = rate;
		return 0;
	}

	if (rcg->flags & FORCE_ENABLE_RCG) {
		rcg->current_freq = DIV_ROUND_CLOSEST_ULL(
					clk_get_rate(hw->clk), 1000) * 1000;
		if (rcg->current_freq == cxo_f.freq)
			curr_src_index = 0;
		else {
			f_curr = qcom_find_freq(rcg->freq_tbl,
							rcg->current_freq);
			if (!f_curr)
				return -EINVAL;

			curr_src_index = qcom_find_src_index(hw,
						rcg->parent_map, f_curr->src);
		}

		new_src_index = qcom_find_src_index(hw, rcg->parent_map,
							f->src);

		curr_src = clk_hw_get_parent_by_index(hw, curr_src_index);
		if (!curr_src)
			return -EINVAL;
		new_src = clk_hw_get_parent_by_index(hw, new_src_index);
		if (!new_src)
			return -EINVAL;

		/* The RCG could currently be disabled. Enable its parents. */
		ret = prepare_enable_rcg_srcs(curr_src->clk, new_src->clk);
		if (ret)
			return ret;
		force_enabled = clk_rcg2_is_force_enabled(hw);
		if (!force_enabled)
			clk_rcg2_set_force_enable(hw);
	}

	ret = clk_rcg2_configure(rcg, f);
	if (ret)
		return ret;

	if (rcg->flags & FORCE_ENABLE_RCG) {
		if (!force_enabled)
			clk_rcg2_clear_force_enable(hw);
		disable_unprepare_rcg_srcs(curr_src->clk, new_src->clk);
	}

	/* Update current frequency with the requested frequency. */
	rcg->current_freq = rate;
	return ret;
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

static int clk_rcg2_enable(struct clk_hw *hw)
{
	struct clk_rcg2 *rcg = to_clk_rcg2(hw);
	unsigned long rate;
	const struct freq_tbl *f;
	int ret;

	if (rcg->flags & FORCE_ENABLE_RCG)
		clk_rcg2_set_force_enable(hw);

	if (!rcg->enable_safe_config)
		return 0;

	/*
	 * Switch from CXO to the stashed mux selection. Force enable and
	 * disable the RCG while configuring it to safeguard against any update
	 * signal coming from the downstream clock. The current parent has
	 * already been prepared and enabled at this point, and the CXO source
	 * is always on while APPS is online. Therefore, the RCG can safely be
	 * switched.
	 */
	rate = rcg->current_freq;
	f = qcom_find_freq(rcg->freq_tbl, rate);
	if (!f)
		return -EINVAL;

	/*
	 * If CXO is not listed as a supported frequency in the frequency
	 * table, the above API would return the lowest supported frequency
	 * instead. This will lead to incorrect configuration of the RCG.
	 * Check if the RCG rate is CXO and configure it accordingly.
	 */
	if (rate == cxo_f.freq)
		f = &cxo_f;

	if (!(rcg->flags & FORCE_ENABLE_RCG))
		clk_rcg2_set_force_enable(hw);

	ret = clk_rcg2_configure(rcg, f);

	if (!(rcg->flags & FORCE_ENABLE_RCG))
		clk_rcg2_clear_force_enable(hw);

	return ret;
}

static void clk_rcg2_disable(struct clk_hw *hw)
{
	struct clk_rcg2 *rcg = to_clk_rcg2(hw);
	int ret;

	if (!rcg->enable_safe_config) {
		if (rcg->flags & FORCE_ENABLE_RCG)
			clk_rcg2_clear_force_enable(hw);
		return;
	}
	/*
	 * Park the RCG at a safe configuration - sourced off the CXO. This is
	 * needed for 2 reasons: In the case of RCGs sourcing PSCBCs, due to a
	 * default HW behavior, the RCG will turn on when its corresponding
	 * GDSC is enabled. We might also have cases when the RCG might be left
	 * enabled without the overlying SW knowing about it. This results from
	 * hard to track cases of downstream clocks being left enabled. In both
	 * these cases, scaling the RCG will fail since it's enabled but with
	 * its sources cut off.
	 *
	 * Save mux select and switch to CXO. Force enable and disable the RCG
	 * while configuring it to safeguard against any update signal coming
	 * from the downstream clock. The current parent is still prepared and
	 * enabled at this point, and the CXO source is always on while APPS is
	 * online. Therefore, the RCG can safely be switched.
	 */
	clk_rcg2_set_force_enable(hw);
	ret = clk_rcg2_configure(rcg, &cxo_f);
	if (ret)
		pr_err("%s: CXO configuration failed\n", clk_hw_get_name(hw));
	clk_rcg2_clear_force_enable(hw);
}

static struct clk_regmap_ops clk_rcg2_regmap_ops = {
	.list_rate = clk_rcg2_list_rate,
	.list_registers = clk_rcg2_list_registers,
};

static int clk_rcg2_init(struct clk_hw *hw)
{
	struct clk_regmap *rclk = to_clk_regmap(hw);

	if (!rclk->ops)
		rclk->ops = &clk_rcg2_regmap_ops;

	return 0;
}

const struct clk_ops clk_rcg2_ops = {
	.prepare = clk_prepare_regmap,
	.unprepare = clk_unprepare_regmap,
	.pre_rate_change = clk_pre_change_regmap,
	.post_rate_change = clk_post_change_regmap,
	.is_enabled = clk_rcg2_is_enabled,
	.enable = clk_rcg2_enable,
	.disable = clk_rcg2_disable,
	.get_parent = clk_rcg2_get_parent,
	.set_parent = clk_rcg2_set_parent,
	.recalc_rate = clk_rcg2_recalc_rate,
	.determine_rate = clk_rcg2_determine_rate,
	.set_rate = clk_rcg2_set_rate,
	.set_rate_and_parent = clk_rcg2_set_rate_and_parent,
	.get_duty_cycle = clk_rcg2_get_duty_cycle,
	.set_duty_cycle = clk_rcg2_set_duty_cycle,
	.init = clk_rcg2_init,
	.debug_init = clk_common_debug_init,
};
EXPORT_SYMBOL_GPL(clk_rcg2_ops);

const struct clk_ops clk_rcg2_floor_ops = {
	.prepare = clk_prepare_regmap,
	.unprepare = clk_unprepare_regmap,
	.pre_rate_change = clk_pre_change_regmap,
	.post_rate_change = clk_post_change_regmap,
	.is_enabled = clk_rcg2_is_enabled,
	.enable = clk_rcg2_enable,
	.disable = clk_rcg2_disable,
	.get_parent = clk_rcg2_get_parent,
	.set_parent = clk_rcg2_set_parent,
	.recalc_rate = clk_rcg2_recalc_rate,
	.determine_rate = clk_rcg2_determine_floor_rate,
	.set_rate = clk_rcg2_set_floor_rate,
	.set_rate_and_parent = clk_rcg2_set_floor_rate_and_parent,
	.get_duty_cycle = clk_rcg2_get_duty_cycle,
	.set_duty_cycle = clk_rcg2_set_duty_cycle,
	.init = clk_rcg2_init,
	.debug_init = clk_common_debug_init,
};
EXPORT_SYMBOL_GPL(clk_rcg2_floor_ops);

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
	if (!req->best_parent_hw)
		return -EINVAL;
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
	.prepare = clk_prepare_regmap,
	.unprepare = clk_unprepare_regmap,
	.pre_rate_change = clk_pre_change_regmap,
	.post_rate_change = clk_post_change_regmap,
	.is_enabled = clk_rcg2_is_enabled,
	.get_parent = clk_rcg2_get_parent,
	.set_parent = clk_rcg2_set_parent,
	.recalc_rate = clk_rcg2_recalc_rate,
	.set_rate = clk_edp_pixel_set_rate,
	.set_rate_and_parent = clk_edp_pixel_set_rate_and_parent,
	.determine_rate = clk_edp_pixel_determine_rate,
	.init = clk_rcg2_init,
	.debug_init = clk_common_debug_init,
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
	if (!p)
		return -EINVAL;
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
	.prepare = clk_prepare_regmap,
	.unprepare = clk_unprepare_regmap,
	.pre_rate_change = clk_pre_change_regmap,
	.post_rate_change = clk_post_change_regmap,
	.is_enabled = clk_rcg2_is_enabled,
	.get_parent = clk_rcg2_get_parent,
	.set_parent = clk_rcg2_set_parent,
	.recalc_rate = clk_rcg2_recalc_rate,
	.set_rate = clk_byte_set_rate,
	.set_rate_and_parent = clk_byte_set_rate_and_parent,
	.determine_rate = clk_byte_determine_rate,
	.init = clk_rcg2_init,
	.debug_init = clk_common_debug_init,
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

	p = req->best_parent_hw;

	if (!p || rate == 0)
		return -EINVAL;

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
	.prepare = clk_prepare_regmap,
	.unprepare = clk_unprepare_regmap,
	.pre_rate_change = clk_pre_change_regmap,
	.post_rate_change = clk_post_change_regmap,
	.is_enabled = clk_rcg2_is_enabled,
	.get_parent = clk_rcg2_get_parent,
	.set_parent = clk_rcg2_set_parent,
	.recalc_rate = clk_rcg2_recalc_rate,
	.set_rate = clk_byte2_set_rate,
	.set_rate_and_parent = clk_byte2_set_rate_and_parent,
	.determine_rate = clk_byte2_determine_rate,
	.init = clk_rcg2_init,
	.debug_init = clk_common_debug_init,
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

	if (!req->best_parent_hw)
		return -EINVAL;

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
	.prepare = clk_prepare_regmap,
	.unprepare = clk_unprepare_regmap,
	.pre_rate_change = clk_pre_change_regmap,
	.post_rate_change = clk_post_change_regmap,
	.is_enabled = clk_rcg2_is_enabled,
	.get_parent = clk_rcg2_get_parent,
	.set_parent = clk_rcg2_set_parent,
	.recalc_rate = clk_rcg2_recalc_rate,
	.set_rate = clk_pixel_set_rate,
	.set_rate_and_parent = clk_pixel_set_rate_and_parent,
	.determine_rate = clk_pixel_determine_rate,
	.init = clk_rcg2_init,
	.debug_init = clk_common_debug_init,
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
	if (!xo)
		return -EINVAL;
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
	.prepare = clk_prepare_regmap,
	.unprepare = clk_unprepare_regmap,
	.pre_rate_change = clk_pre_change_regmap,
	.post_rate_change = clk_post_change_regmap,
	.is_enabled = clk_rcg2_is_enabled,
	.get_parent = clk_rcg2_get_parent,
	.set_parent = clk_rcg2_set_parent,
	.recalc_rate = clk_rcg2_recalc_rate,
	.set_rate = clk_gfx3d_set_rate,
	.set_rate_and_parent = clk_gfx3d_set_rate_and_parent,
	.determine_rate = clk_gfx3d_determine_rate,
	.init = clk_rcg2_init,
	.debug_init = clk_common_debug_init,
};
EXPORT_SYMBOL_GPL(clk_gfx3d_ops);

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
	 * In case clock is disabled, update the M, N and D registers, cache
	 * the CFG value in parked_cfg and don't hit the update bit of CMD
	 * register.
	 */
	if (!clk_hw_is_enabled(hw))
		return __clk_rcg2_configure(rcg, f, &rcg->parked_cfg);

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

const struct clk_ops clk_rcg2_shared_ops = {
	.prepare = clk_prepare_regmap,
	.unprepare = clk_unprepare_regmap,
	.pre_rate_change = clk_pre_change_regmap,
	.post_rate_change = clk_post_change_regmap,
	.enable = clk_rcg2_shared_enable,
	.disable = clk_rcg2_shared_disable,
	.get_parent = clk_rcg2_shared_get_parent,
	.set_parent = clk_rcg2_shared_set_parent,
	.recalc_rate = clk_rcg2_shared_recalc_rate,
	.determine_rate = clk_rcg2_determine_rate,
	.set_rate = clk_rcg2_shared_set_rate,
	.set_rate_and_parent = clk_rcg2_shared_set_rate_and_parent,
	.init = clk_rcg2_init,
	.debug_init = clk_common_debug_init,
};
EXPORT_SYMBOL_GPL(clk_rcg2_shared_ops);

/* Common APIs to be used for DFS based RCGR */
static int clk_rcg2_dfs_populate_freq(struct clk_hw *hw, unsigned int l,
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
			if (!p)
				return -EINVAL;
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
	return 0;
}

static int clk_rcg2_dfs_populate_freq_table(struct clk_rcg2 *rcg)
{
	struct freq_tbl *freq_tbl;
	int i, ret;

	/* Allocate space for 1 extra since table is NULL terminated */
	freq_tbl = kcalloc(MAX_PERF_LEVEL + 1, sizeof(*freq_tbl), GFP_KERNEL);
	if (!freq_tbl)
		return -ENOMEM;
	rcg->freq_tbl = freq_tbl;

	for (i = 0; i < MAX_PERF_LEVEL; i++) {
		ret =
		clk_rcg2_dfs_populate_freq(&rcg->clkr.hw, i, freq_tbl + i);
		if (ret)
			return ret;
	}
	return ret;
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
	.prepare = clk_prepare_regmap,
	.unprepare = clk_unprepare_regmap,
	.pre_rate_change = clk_pre_change_regmap,
	.post_rate_change = clk_post_change_regmap,
	.is_enabled = clk_rcg2_is_enabled,
	.get_parent = clk_rcg2_get_parent,
	.determine_rate = clk_rcg2_dfs_determine_rate,
	.recalc_rate = clk_rcg2_dfs_recalc_rate,
	.init = clk_rcg2_init,
	.debug_init = clk_common_debug_init,
};

/* Common APIs to be used for CESTA based RCGR */
static int clk_rcg2_crmc_populate_freq(struct clk_hw *hw, unsigned int l,
				       struct freq_tbl *f)
{
	struct clk_rcg2 *rcg = to_clk_rcg2(hw);
	struct clk_crm *crm = rcg->clkr.crm;
	struct clk_hw *p;
	unsigned long prate = 0;
	u32 mask, rcgr_cfg, src, pll_lval, lval, alpha_val, num_parents, i;

	if (!crm->regmap_crmc) {
		pr_err("%s crmc regmap error\n", __func__);
		return -ENODEV;
	}

	regmap_read(crm->regmap_crmc,
		    CLK_RCG_CRMC_CFG_RCGR(rcg->clkr.crm_vcd, l), &rcgr_cfg);
	regmap_read(crm->regmap_crmc,
		    CLK_RCG_CRMC_PERF_LEVEL_PLL_L_VAL_LUT(rcg->clkr.crm_vcd, l), &pll_lval);

	mask = BIT(rcg->hid_width) - 1;
	f->pre_div = 1;
	if (rcgr_cfg & mask)
		f->pre_div = rcgr_cfg & mask;

	src = rcgr_cfg & CFG_SRC_SEL_MASK;
	src >>= CFG_SRC_SEL_SHIFT;

	lval = pll_lval & PLL_L_VAL_MASK;
	alpha_val = (pll_lval & PLL_ALPHA_VAL_MASK) >> PLL_ALPHA_VAL_SHIFT;

	num_parents = clk_hw_get_num_parents(hw);
	for (i = 0; i < num_parents; i++) {
		if (src == rcg->parent_map[i].cfg) {
			f->src = rcg->parent_map[i].src;
			p = clk_hw_get_parent_by_index(&rcg->clkr.hw, i);
			if (!p)
				return -EINVAL;

			if (!lval) {
				prate = clk_hw_get_rate(p);
			} else if (clk_is_regmap_clk(p)) {
				struct clk_regmap *rclk = to_clk_regmap(p);

				if (rclk->ops && rclk->ops->calc_pll)
					prate = rclk->ops->calc_pll(p, lval, alpha_val);
			}
			break;
		}
	}

	if (!prate) {
		pr_err("%s error clk=%s\n", __func__, qcom_clk_hw_get_name(hw));
		return -EINVAL;
	}

	f->freq = calc_rate(prate, 0, 0, 0, f->pre_div);

	return 0;
}

int clk_rcg2_crmc_populate_freq_table(struct clk_rcg2 *rcg)
{
	struct freq_tbl *freq_tbl, *curr_freq_tbl;
	u32 prev_freq = 0;
	int i, ret;

	/* Allocate space for 1 extra since table is NULL terminated */
	freq_tbl = kcalloc(MAX_PERF_LEVEL_PER_VCD + 1, sizeof(*freq_tbl), GFP_KERNEL);
	if (!freq_tbl)
		return -ENOMEM;

	rcg->freq_tbl = freq_tbl;

	/*
	 * Skipping first LUT entry as first entry is used to disable RCG
	 */
	for (i = 0; i < MAX_PERF_LEVEL_PER_VCD; i++) {
		ret = clk_rcg2_crmc_populate_freq(&rcg->clkr.hw, i + 1,
						  freq_tbl + i);
		if (ret)
			return ret;

		curr_freq_tbl = freq_tbl + i;

		/*
		 * Two of the same/decreasing frequencies means end of LUT
		 */
		if (prev_freq >= curr_freq_tbl->freq) {
			curr_freq_tbl->freq = 0;
			break;
		}

		prev_freq = curr_freq_tbl->freq;
	}

	return ret;
}

static int clk_rcg2_crmc_determine_rate(struct clk_hw *hw,
					struct clk_rate_request *req)
{
	struct clk_rcg2 *rcg = to_clk_rcg2(hw);
	struct clk_crm *crm = rcg->clkr.crm;
	int ret;

	ret = clk_runtime_get_regmap(&rcg->clkr);
	if (ret)
		return ret;

	ret = qcom_clk_crm_init(rcg->clkr.dev, crm);
	if (ret) {
		pr_err("%s Failed to initialize CRM ret=%d clk=%s\n",
		       __func__, ret, qcom_clk_hw_get_name(hw));
		goto err;
	}

	if (!rcg->freq_populated) {
		ret = clk_rcg2_crmc_populate_freq_table(rcg);
		if (ret) {
			pr_err("%s Failed to populate crmc tables for %s\n",
			       __func__, qcom_clk_hw_get_name(hw));
			goto err;
		}
		rcg->freq_populated = true;
	}
	ret = clk_rcg2_determine_rate(hw, req);

err:
	clk_runtime_put_regmap(&rcg->clkr);
	return ret;
}

static int clk_rcg2_vote_perf_level(struct clk_hw *hw, unsigned long rate)
{
	struct clk_rcg2 *rcg = to_clk_rcg2(hw);
	struct clk_crm *crm = rcg->clkr.crm;
	struct crm_cmd cmd;
	int perf_index;
	int ret, i;

	if (!rcg->freq_tbl || !crm->initialized) {
		pr_err("%s rcg=%s rate=%ld\n", __func__,
		       qcom_clk_hw_get_name(hw), rate);
		return -EINVAL;
	}

	perf_index = qcom_find_crm_freq_index(rcg->freq_tbl, rate);
	if (perf_index < 0 || perf_index >= MAX_PERF_LEVEL_PER_VCD) {
		pr_err("%s rcg name %s perf_index=%d\n", __func__,
		       qcom_clk_hw_get_name(hw), perf_index);
		return -EINVAL;
	}

	cmd.data = perf_index;
	cmd.resource_idx = rcg->clkr.crm_vcd;
	cmd.wait = 1;

	for (i = 0; i < MAX_CRM_SW_DRV_STATE; i++) {
		cmd.pwr_state.sw = i;
		ret = crm_write_perf_ol(crm->dev, CRM_SW_DRV, 0, &cmd);
		if (ret)
			pr_err("%s err write_perf_ol rcg name %s ret=%d\n",
			       __func__, qcom_clk_hw_get_name(hw), ret);
	}

	return ret;
}

static int clk_rcg2_crmc_set_rate(struct clk_hw *hw, unsigned long rate,
				  unsigned long parent_rate)
{
	struct clk_rcg2 *rcg = to_clk_rcg2(hw);

	rcg->current_freq = rate;

	if (!clk_hw_is_prepared(hw))
		return 0;

	return clk_rcg2_vote_perf_level(hw, rate);
}

/**
 * clk_rcg2_crmc_prepare() - cesta rcg/vcd prepare call back for cesta managed clks
 *
 * @hw: clk to operate on
 *
 * Vote clock by updating the perf_level to level required by
 * the current rate of the clock if it hasn't been initialized before.
 * Vdd_level and level required by current clock rate mismatches can
 * occur due to error cases and upon initial clock registration
 * if the clock becomes an orphan and is later reparented.
 *
 * Returns 0 on success, -EERROR otherwise.
 */
int clk_rcg2_crmc_prepare(struct clk_hw *hw)
{
	struct clk_rcg2 *rcg = to_clk_rcg2(hw);

	if (!rcg->current_freq)
		rcg->current_freq = cxo_f.freq;

	return clk_rcg2_vote_perf_level(hw, rcg->current_freq);
}

/**
 * clk_rcg2_crmc_unprepare() - standard prepare call back for regmap clks
 *
 * @hw: clk to operate on
 *
 * Unprepare the clock by removing the outstanding perf_level vote.
 *
 */
void clk_rcg2_crmc_unprepare(struct clk_hw *hw)
{
	int ret;

	/*
	 * Cesta will park RCG at a safe configuration that is CP0 perf level
	 */
	ret = clk_rcg2_vote_perf_level(hw, 0);
	if (ret)
		pr_err("%s rcg name=%s ret=%d\n", __func__, qcom_clk_hw_get_name(hw), ret);
}

unsigned long clk_rcg2_crmc_hw_set_rate(struct clk_hw *hw,
					enum crm_drv_type client_type, u32 client_idx,
					u32 pwr_st, unsigned long rate)
{
	struct clk_rcg2 *rcg = to_clk_rcg2(hw);
	struct clk_crm *crm = rcg->clkr.crm;
	struct crm_cmd cmd;
	int ret, perf_index;

	perf_index = qcom_find_crm_freq_index(rcg->freq_tbl, rate);
	if (perf_index < 0 || perf_index >= MAX_PERF_LEVEL_PER_VCD) {
		pr_err("%s rcg name %s perf_index=%d\n", __func__,
		       qcom_clk_hw_get_name(hw), perf_index);
		return -EINVAL;
	}

	cmd.resource_idx = rcg->clkr.crm_vcd;
	cmd.data = perf_index;
	cmd.wait = 1;
	cmd.pwr_state.hw = pwr_st;

	ret = crm_write_perf_ol(crm->dev, client_type, client_idx, &cmd);
	if (ret)
		pr_err("%s err write_perf_ol rcg name %s ret=%d\n",
		       __func__, qcom_clk_hw_get_name(hw), ret);

	return ret;
}

static long clk_rcg2_crmc_list_rate(struct clk_hw *hw, unsigned int n,
				    unsigned long fmax)
{
	struct clk_rcg2 *rcg = to_clk_rcg2(hw);
	struct clk_crm *crm = rcg->clkr.crm;
	struct clk_rate_request req = {0};
	int ret;

	if (crm->name && !crm->initialized)
		ret = clk_rcg2_crmc_determine_rate(hw, &req);

	return clk_rcg2_list_rate(hw, n, fmax);
}

static struct clk_regmap_ops clk_rcg2_crmc_regmap_ops = {
	.set_crm_rate = clk_rcg2_crmc_hw_set_rate,
	.list_rate = clk_rcg2_crmc_list_rate,
};

static int clk_rcg2_crmc_init(struct clk_hw *hw)
{
	struct clk_regmap *rclk = to_clk_regmap(hw);

	if (!rclk->ops)
		rclk->ops = &clk_rcg2_crmc_regmap_ops;

	return 0;
}

static unsigned long
clk_rcg2_crmc_recalc_rate(struct clk_hw *hw, unsigned long parent_rate)
{
	struct clk_rcg2 *rcg = to_clk_rcg2(hw);

	/*
	 * CRM-controlled clocks have multiple SW and HW voters. We need to
	 * return the Linux SW vote instead of the current HW rate. The HW rate
	 * is a result of aggregating across all clients. If we return the
	 * aggregated rate, then subsequent clk_set_rate() calls can
	 * short-circuit before calling our set_rate() callback, even if we
	 * haven't sent a vote for that new rate on behalf of our SW client
	 * yet. Failing to do so can result in the clock frequency dropping
	 * below the rate expected by the framework and consumer.
	 */
	return rcg->current_freq;
}

const struct clk_ops clk_rcg2_crmc_ops = {
	.prepare = clk_rcg2_crmc_prepare,
	.unprepare = clk_rcg2_crmc_unprepare,
	.is_enabled = clk_rcg2_is_enabled,
	.get_parent = clk_rcg2_get_parent,
	.set_rate = clk_rcg2_crmc_set_rate,
	.determine_rate = clk_rcg2_crmc_determine_rate,
	.recalc_rate = clk_rcg2_crmc_recalc_rate,
	.init = clk_rcg2_crmc_init,
	.debug_init = clk_common_debug_init,
};
EXPORT_SYMBOL(clk_rcg2_crmc_ops);

static int clk_rcg2_vote_bw(struct clk_hw *hw, unsigned long rate)
{
	struct clk_rcg2 *rcg = to_clk_rcg2(hw);
	struct clk_crm *crm = rcg->clkr.crm;
	struct crm_cmd cmd = {0};
	int ret, i;

	if (rate)
		rate /= 1000000;

	cmd.resource_idx = 0;
	cmd.wait = 1;
	cmd.data = BCM_TCS_CMD(1, 1, 0, rate);

	for (i = 0; i < MAX_CRM_SW_DRV_STATE; i++) {
		cmd.pwr_state.sw = i;
		ret = crm_write_bw_vote(crm->dev, CRM_SW_DRV, 0, &cmd);
		if (ret)
			pr_err("%s err crm_write_bw_vote rcg name %s ret=%d\n",
			       __func__, qcom_clk_hw_get_name(hw), ret);
	}

	return ret;
}

/**
 * clk_rcg2_crmb_prepare() - cesta rcg/vcd prepare call back for cesta managed clks
 *
 * @hw: clk to operate on
 *
 * Vote clock by updating the perf_level to level required by
 * the current rate of the clock if it hasn't been initialized before.
 * Vdd_level and level required by current clock rate mismatches can
 * occur due to error cases and upon initial clock registration
 * if the clock becomes an orphan and is later reparented.
 *
 * Returns 0 on success, -EERROR otherwise.
 */
int clk_rcg2_crmb_prepare(struct clk_hw *hw)
{
	struct clk_rcg2 *rcg = to_clk_rcg2(hw);
	struct clk_crm *crm = rcg->clkr.crm;

	if (!rcg->freq_tbl || !crm->initialized)
		return 0;

	return clk_rcg2_vote_bw(hw, rcg->current_freq);
}

/**
 * clk_rcg2_crmb_unprepare() - standard prepare call back for regmap clks
 *
 * @hw: clk to operate on
 *
 * Unprepare the clock by removing the outstanding perf_level vote.
 *
 */
void clk_rcg2_crmb_unprepare(struct clk_hw *hw)
{
	struct clk_rcg2 *rcg = to_clk_rcg2(hw);
	struct clk_crm *crm = rcg->clkr.crm;
	int ret;

	if (!rcg->freq_tbl || !crm->initialized)
		return;

	/*
	 * Cesta will park RCG at a safe configuration that is CP0 perf level
	 */
	ret = clk_rcg2_vote_bw(hw, 0);
	if (ret)
		pr_err("%s clk_rcg2_vote_bw rcg name %s ret=%d\n",
		       __func__, qcom_clk_hw_get_name(hw), ret);
}

static int clk_rcg2_crmb_set_rate(struct clk_hw *hw, unsigned long rate,
				  unsigned long parent_rate)
{
	struct clk_rcg2 *rcg = to_clk_rcg2(hw);
	struct clk_crm *crm = rcg->clkr.crm;
	int ret;

	rcg->current_freq = rate;

	if (!clk_hw_is_prepared(hw))
		return 0;

	if (rcg->freq_tbl && crm->initialized) {
		ret = clk_rcg2_vote_bw(hw, rate);
		if (ret)
			pr_err("%s clk_rcg2_vote_bw rcg name %s ret=%d\n",
			       __func__, qcom_clk_hw_get_name(hw), ret);
		return ret;
	}

	return -EINVAL;
}

unsigned long clk_rcg2_crmb_hw_set_bw(struct clk_hw *hw,
				      enum crm_drv_type client_type, u32 client_idx,
				      u32 pwr_st, unsigned long rate)
{
	struct clk_rcg2 *rcg = to_clk_rcg2(hw);
	struct clk_crm *crm = rcg->clkr.crm;
	struct crm_cmd cmd = {0};
	int ret;

	if (rate)
		rate /= 1000000;

	cmd.resource_idx = 0;
	cmd.pwr_state.hw = pwr_st;
	cmd.wait = 1;

	/*
	 * write AB for HW clients
	 */
	cmd.data = BCM_TCS_CMD(1, 1, rate, 0);

	ret = crm_write_bw_vote(crm->dev, client_type, client_idx, &cmd);
	if (ret)
		pr_err("%s err crm_write_bw_vote rcg name %s cmd.data=0x%x ret=%d\n",
		       __func__, qcom_clk_hw_get_name(hw), cmd.data, ret);

	return ret;
}

static struct clk_regmap_ops clk_rcg2_crmb_regmap_ops = {
	.set_crm_rate = clk_rcg2_crmb_hw_set_bw,
	.list_rate = clk_rcg2_list_rate,
};

static int clk_rcg2_crmb_init(struct clk_hw *hw)
{
	struct clk_regmap *rclk = to_clk_regmap(hw);

	if (!rclk->ops)
		rclk->ops = &clk_rcg2_crmb_regmap_ops;

	return 0;
}

const struct clk_ops clk_rcg2_crmb_ops = {
	.prepare = clk_rcg2_crmb_prepare,
	.unprepare = clk_rcg2_crmb_unprepare,
	.is_enabled = clk_rcg2_is_enabled,
	.get_parent = clk_rcg2_get_parent,
	.set_rate = clk_rcg2_crmb_set_rate,
	.determine_rate = clk_rcg2_crmc_determine_rate,
	.recalc_rate = clk_rcg2_crmc_recalc_rate,
	.init = clk_rcg2_crmb_init,
	.debug_init = clk_common_debug_init,
};
EXPORT_SYMBOL(clk_rcg2_crmb_ops);

static int clk_rcg2_enable_dfs(const struct clk_rcg_dfs_data *data,
			       struct regmap *regmap)
{
	struct clk_rcg2 *rcg = data->rcg;
	struct clk_init_data *init = data->init;
	u32 val;
	int ret;

	rcg->flags |= DFS_SUPPORT;

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
	.prepare = clk_prepare_regmap,
	.unprepare = clk_unprepare_regmap,
	.pre_rate_change = clk_pre_change_regmap,
	.post_rate_change = clk_post_change_regmap,
	.is_enabled = clk_rcg2_is_enabled,
	.get_parent = clk_rcg2_get_parent,
	.set_parent = clk_rcg2_set_parent,
	.recalc_rate = clk_rcg2_recalc_rate,
	.set_rate = clk_rcg2_dp_set_rate,
	.set_rate_and_parent = clk_rcg2_dp_set_rate_and_parent,
	.determine_rate = clk_rcg2_dp_determine_rate,
	.init = clk_rcg2_init,
	.debug_init = clk_common_debug_init,
};
EXPORT_SYMBOL_GPL(clk_dp_ops);

// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2017, Linaro Limited
 * Author: Georgi Djakov <georgi.djakov@linaro.org>
 */

#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/regmap.h>

#include "clk-regmap-mux-div.h"

#define CMD_RCGR			0x0
#define CMD_RCGR_UPDATE			BIT(0)
#define CMD_RCGR_DIRTY_CFG		BIT(4)
#define CMD_RCGR_ROOT_OFF		BIT(31)
#define CFG_RCGR			0x4

#define to_clk_regmap_mux_div(_hw) \
	container_of(to_clk_regmap(_hw), struct clk_regmap_mux_div, clkr)

int mux_div_set_src_div(struct clk_regmap_mux_div *md, u32 src, u32 div)
{
	int ret, count;
	u32 val, mask;
	const char *name = clk_hw_get_name(&md->clkr.hw);

	val = (div << md->hid_shift) | (src << md->src_shift);
	mask = ((BIT(md->hid_width) - 1) << md->hid_shift) |
	       ((BIT(md->src_width) - 1) << md->src_shift);

	ret = regmap_update_bits(md->clkr.regmap, CFG_RCGR + md->reg_offset,
				 mask, val);
	if (ret)
		return ret;

	ret = regmap_update_bits(md->clkr.regmap, CMD_RCGR + md->reg_offset,
				 CMD_RCGR_UPDATE, CMD_RCGR_UPDATE);
	if (ret)
		return ret;

	/* Wait for update to take effect */
	for (count = 500; count > 0; count--) {
		ret = regmap_read(md->clkr.regmap, CMD_RCGR + md->reg_offset,
				  &val);
		if (ret)
			return ret;
		if (!(val & CMD_RCGR_UPDATE))
			return 0;
		udelay(1);
	}

	pr_err("%s: RCG did not update its configuration", name);
	return -EBUSY;
}
EXPORT_SYMBOL_GPL(mux_div_set_src_div);

int mux_div_get_src_div(struct clk_regmap_mux_div *md, u32 *src,
				  u32 *div)
{
	int ret = 0;
	u32 val, d, s;
	const char *name = clk_hw_get_name(&md->clkr.hw);

	ret = regmap_read(md->clkr.regmap, CMD_RCGR + md->reg_offset, &val);
	if (ret)
		return ret;

	if (val & CMD_RCGR_DIRTY_CFG) {
		pr_err("%s: RCG configuration is pending\n", name);
		return -EBUSY;
	}

	ret = regmap_read(md->clkr.regmap, CFG_RCGR + md->reg_offset, &val);
	if (ret)
		return ret;

	s = (val >> md->src_shift);
	s &= BIT(md->src_width) - 1;
	*src = s;

	d = (val >> md->hid_shift);
	d &= BIT(md->hid_width) - 1;
	*div = d;

	return ret;
}
EXPORT_SYMBOL_GPL(mux_div_get_src_div);

static inline bool is_better_rate(unsigned long req, unsigned long best,
				  unsigned long new)
{
	return (req <= new && new < best) || (best < req && best < new);
}

static int mux_div_determine_rate(struct clk_hw *hw,
				  struct clk_rate_request *req)
{
	struct clk_regmap_mux_div *md = to_clk_regmap_mux_div(hw);
	unsigned int i, div, max_div;
	unsigned long actual_rate, best_rate = 0;
	unsigned long req_rate = req->rate;

	for (i = 0; i < clk_hw_get_num_parents(hw); i++) {
		struct clk_hw *parent = clk_hw_get_parent_by_index(hw, i);
		unsigned long parent_rate = clk_hw_get_rate(parent);

		max_div = BIT(md->hid_width) - 1;
		for (div = 1; div < max_div; div++) {
			parent_rate = mult_frac(req_rate, div, 2);
			parent_rate = clk_hw_round_rate(parent, parent_rate);
			actual_rate = mult_frac(parent_rate, 2, div);

			if (is_better_rate(req_rate, best_rate, actual_rate)) {
				best_rate = actual_rate;
				req->rate = best_rate;
				req->best_parent_rate = parent_rate;
				req->best_parent_hw = parent;
			}

			if (actual_rate < req_rate || best_rate <= req_rate)
				break;
		}
	}

	if (!best_rate)
		return -EINVAL;

	return 0;
}

static int __mux_div_set_rate_and_parent(struct clk_hw *hw, unsigned long rate,
					 unsigned long prate, u32 src)
{
	struct clk_regmap_mux_div *md = to_clk_regmap_mux_div(hw);
	int ret;
	u32 div, max_div, best_src = 0, best_div = 0;
	unsigned int i;
	unsigned long actual_rate, best_rate = 0;

	for (i = 0; i < clk_hw_get_num_parents(hw); i++) {
		struct clk_hw *parent = clk_hw_get_parent_by_index(hw, i);
		unsigned long parent_rate = clk_hw_get_rate(parent);

		max_div = BIT(md->hid_width) - 1;
		for (div = 1; div < max_div; div++) {
			parent_rate = mult_frac(rate, div, 2);
			parent_rate = clk_hw_round_rate(parent, parent_rate);
			actual_rate = mult_frac(parent_rate, 2, div);

			if (is_better_rate(rate, best_rate, actual_rate)) {
				best_rate = actual_rate;
				best_src = md->parent_map[i].cfg;
				best_div = div - 1;
			}

			if (actual_rate < rate || best_rate <= rate)
				break;
		}
	}

	ret = mux_div_set_src_div(md, best_src, best_div);
	if (!ret) {
		md->div = best_div;
		md->src = best_src;
	}

	return ret;
}

static u8 mux_div_get_parent(struct clk_hw *hw)
{
	struct clk_regmap_mux_div *md = to_clk_regmap_mux_div(hw);
	const char *name = clk_hw_get_name(hw);
	u32 i, div, src = 0;

	mux_div_get_src_div(md, &src, &div);

	for (i = 0; i < clk_hw_get_num_parents(hw); i++)
		if (src == md->parent_map[i].cfg)
			return i;

	pr_err("%s: Can't find parent with src %d\n", name, src);
	return 0;
}

static int mux_div_set_parent(struct clk_hw *hw, u8 index)
{
	struct clk_regmap_mux_div *md = to_clk_regmap_mux_div(hw);

	return mux_div_set_src_div(md, md->parent_map[index].cfg, md->div);
}

static int mux_div_set_rate(struct clk_hw *hw,
			    unsigned long rate, unsigned long prate)
{
	struct clk_regmap_mux_div *md = to_clk_regmap_mux_div(hw);

	return __mux_div_set_rate_and_parent(hw, rate, prate, md->src);
}

static int mux_div_set_rate_and_parent(struct clk_hw *hw,  unsigned long rate,
				       unsigned long prate, u8 index)
{
	struct clk_regmap_mux_div *md = to_clk_regmap_mux_div(hw);

	return __mux_div_set_rate_and_parent(hw, rate, prate,
					     md->parent_map[index].cfg);
}

static unsigned long mux_div_recalc_rate(struct clk_hw *hw, unsigned long prate)
{
	struct clk_regmap_mux_div *md = to_clk_regmap_mux_div(hw);
	u32 div, src;
	int i, num_parents = clk_hw_get_num_parents(hw);
	const char *name = clk_hw_get_name(hw);

	mux_div_get_src_div(md, &src, &div);
	for (i = 0; i < num_parents; i++)
		if (src == md->parent_map[i].cfg) {
			struct clk_hw *p = clk_hw_get_parent_by_index(hw, i);
			unsigned long parent_rate = clk_hw_get_rate(p);

			return mult_frac(parent_rate, 2, div + 1);
		}

	pr_err("%s: Can't find parent %d\n", name, src);
	return 0;
}

static int mux_div_enable(struct clk_hw *hw)
{
	struct clk_regmap_mux_div *md = to_clk_regmap_mux_div(hw);

	return mux_div_set_src_div(md, md->src, md->div);
}

static void mux_div_disable(struct clk_hw *hw)
{
	struct clk_regmap_mux_div *md = to_clk_regmap_mux_div(hw);

	mux_div_set_src_div(md, md->safe_src, md->safe_div);
}

const struct clk_ops clk_regmap_mux_div_ops = {
	.enable = mux_div_enable,
	.disable = mux_div_disable,
	.get_parent = mux_div_get_parent,
	.set_parent = mux_div_set_parent,
	.set_rate = mux_div_set_rate,
	.set_rate_and_parent = mux_div_set_rate_and_parent,
	.determine_rate = mux_div_determine_rate,
	.recalc_rate = mux_div_recalc_rate,
};
EXPORT_SYMBOL_GPL(clk_regmap_mux_div_ops);

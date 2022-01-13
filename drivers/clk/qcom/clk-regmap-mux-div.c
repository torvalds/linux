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
	bool changed;

	val = ((div - 1) << md->hid_shift) | (src << md->src_shift);
	mask = ((BIT(md->hid_width) - 1) << md->hid_shift) |
	       ((BIT(md->src_width) - 1) << md->src_shift);

	ret = regmap_update_bits_check(md->clkr.regmap, CFG_RCGR + md->reg_offset,
				 mask, val, &changed);
	if (ret)
		return ret;

	if (!changed)
		return 0;

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

static void mux_div_get_src_div(struct clk_regmap_mux_div *md, u32 *src,
				u32 *div)
{
	u32 val, d, s;
	const char *name = clk_hw_get_name(&md->clkr.hw);

	regmap_read(md->clkr.regmap, CMD_RCGR + md->reg_offset, &val);

	if (val & CMD_RCGR_DIRTY_CFG) {
		pr_err("%s: RCG configuration is pending\n", name);
		return;
	}

	regmap_read(md->clkr.regmap, CFG_RCGR + md->reg_offset, &val);
	s = (val >> md->src_shift);
	s &= BIT(md->src_width) - 1;
	*src = s;

	d = (val >> md->hid_shift);
	d &= BIT(md->hid_width) - 1;
	*div = d + 1;
}

static void __mux_div_determine_rate(struct clk_regmap_mux_div *md,
		struct clk_rate_request *req)
{
	u64 rounded_rate, parent_rate;
	u64 req_rate = req->rate, rate;
	u32 div, max_div = BIT(md->hid_width);

	if (clk_hw_can_set_rate_parent(&md->clkr.hw)) {
		for (div = 2, parent_rate = req_rate;
				div <= max_div;
				div++, parent_rate += req_rate / 2) {
			rounded_rate = clk_hw_round_rate(req->best_parent_hw, parent_rate);

			if (rounded_rate > parent_rate)
				continue;

			req->rate = DIV_ROUND_UP(rounded_rate * 2, div);
			req->best_parent_rate = rounded_rate;
			return;
		}
	} else {
		parent_rate = clk_hw_get_rate(req->best_parent_hw);
		for (div = 2; div <= max_div; div++) {
			rate = DIV_ROUND_UP(parent_rate * 2, div);


			if (rate > req_rate)
				continue;

			req->rate = rate;
			req->best_parent_rate = parent_rate;
			return;
		}
	}
}

static inline bool is_better_rate(signed long req, signed long best, signed long new)
{
	return new > best && best <= req && new <= req;
}

static int mux_div_determine_rate(struct clk_hw *hw,
				  struct clk_rate_request *req)
{
	struct clk_regmap_mux_div *md = to_clk_regmap_mux_div(hw);
	struct clk_rate_request best = { 0 };
	unsigned int i;

	for (i = 0; i < clk_hw_get_num_parents(hw); i++) {
		struct clk_rate_request rq = {
			.rate = req->rate,
			.best_parent_hw = clk_hw_get_parent_by_index(hw, i),
		};

		__mux_div_determine_rate(md, &rq);

		if (is_better_rate(req->rate, best.rate, rq.rate))
			best = rq;
	}

	*req = best;

	return 0;
}

static int __mux_div_set_rate_and_parent(struct clk_hw *hw, unsigned long rate,
					 unsigned long prate, u32 src)
{
	struct clk_regmap_mux_div *md = to_clk_regmap_mux_div(hw);
	u32 div, max_div;
	int ret;

	max_div = BIT(md->hid_width);

	for (div = 2;
	     div <= max_div && rate < mult_frac(prate, 2, div);
	     div++);

	ret = mux_div_set_src_div(md, src, div);
	if (ret < 0)
		return ret;

	md->div = div;
	md->src = src;

	return 0;
}

static u8 mux_div_get_parent(struct clk_hw *hw)
{
	struct clk_regmap_mux_div *md = to_clk_regmap_mux_div(hw);
	int i;

	for (i = 0; i < clk_hw_get_num_parents(hw); i++)
		if (md->src == md->parent_map[i])
			return i;

	pr_err("%s: Can't find parent with src %d\n", clk_hw_get_name(hw) ?: "NULL" , md->src);
	return 0;
}

static int mux_div_set_parent(struct clk_hw *hw, u8 index)
{
	struct clk_regmap_mux_div *md = to_clk_regmap_mux_div(hw);
	int ret;

	ret = mux_div_set_src_div(md, md->parent_map[index], md->div);
	if (ret < 0)
		return ret;

	md->src = md->parent_map[index];

	return 0;
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

	return __mux_div_set_rate_and_parent(hw, rate, prate, md->parent_map[index]);
}

static unsigned long mux_div_recalc_rate(struct clk_hw *hw, unsigned long prate)
{
	struct clk_regmap_mux_div *md = to_clk_regmap_mux_div(hw);
	u32 div, src;

	mux_div_get_src_div(md, &src, &div);

	return mult_frac(prate, 2, div);
}

static int mux_div_init(struct clk_hw *hw)
{
	struct clk_regmap_mux_div *md = to_clk_regmap_mux_div(hw);

	mux_div_get_src_div(md, &md->src, &md->div);

	return 0;
}

const struct clk_ops clk_regmap_mux_div_ops = {
	.get_parent = mux_div_get_parent,
	.set_parent = mux_div_set_parent,
	.set_rate = mux_div_set_rate,
	.set_rate_and_parent = mux_div_set_rate_and_parent,
	.determine_rate = mux_div_determine_rate,
	.recalc_rate = mux_div_recalc_rate,
	.init = mux_div_init,
};
EXPORT_SYMBOL_GPL(clk_regmap_mux_div_ops);

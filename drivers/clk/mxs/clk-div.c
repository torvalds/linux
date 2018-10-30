/*
 * Copyright 2012 Freescale Semiconductor, Inc.
 *
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

#include <linux/clk-provider.h>
#include <linux/err.h>
#include <linux/slab.h>
#include "clk.h"

/**
 * struct clk_div - mxs integer divider clock
 * @divider: the parent class
 * @ops: pointer to clk_ops of parent class
 * @reg: register address
 * @busy: busy bit shift
 *
 * The mxs divider clock is a subclass of basic clk_divider with an
 * addtional busy bit.
 */
struct clk_div {
	struct clk_divider divider;
	const struct clk_ops *ops;
	void __iomem *reg;
	u8 busy;
};

static inline struct clk_div *to_clk_div(struct clk_hw *hw)
{
	struct clk_divider *divider = to_clk_divider(hw);

	return container_of(divider, struct clk_div, divider);
}

static unsigned long clk_div_recalc_rate(struct clk_hw *hw,
					 unsigned long parent_rate)
{
	struct clk_div *div = to_clk_div(hw);

	return div->ops->recalc_rate(&div->divider.hw, parent_rate);
}

static long clk_div_round_rate(struct clk_hw *hw, unsigned long rate,
			       unsigned long *prate)
{
	struct clk_div *div = to_clk_div(hw);

	return div->ops->round_rate(&div->divider.hw, rate, prate);
}

static int clk_div_set_rate(struct clk_hw *hw, unsigned long rate,
			    unsigned long parent_rate)
{
	struct clk_div *div = to_clk_div(hw);
	int ret;

	ret = div->ops->set_rate(&div->divider.hw, rate, parent_rate);
	if (!ret)
		ret = mxs_clk_wait(div->reg, div->busy);

	return ret;
}

static const struct clk_ops clk_div_ops = {
	.recalc_rate = clk_div_recalc_rate,
	.round_rate = clk_div_round_rate,
	.set_rate = clk_div_set_rate,
};

struct clk *mxs_clk_div(const char *name, const char *parent_name,
			void __iomem *reg, u8 shift, u8 width, u8 busy)
{
	struct clk_div *div;
	struct clk *clk;
	struct clk_init_data init;

	div = kzalloc(sizeof(*div), GFP_KERNEL);
	if (!div)
		return ERR_PTR(-ENOMEM);

	init.name = name;
	init.ops = &clk_div_ops;
	init.flags = CLK_SET_RATE_PARENT;
	init.parent_names = (parent_name ? &parent_name: NULL);
	init.num_parents = (parent_name ? 1 : 0);

	div->reg = reg;
	div->busy = busy;

	div->divider.reg = reg;
	div->divider.shift = shift;
	div->divider.width = width;
	div->divider.flags = CLK_DIVIDER_ONE_BASED;
	div->divider.lock = &mxs_lock;
	div->divider.hw.init = &init;
	div->ops = &clk_divider_ops;

	clk = clk_register(NULL, &div->divider.hw);
	if (IS_ERR(clk))
		kfree(div);

	return clk;
}

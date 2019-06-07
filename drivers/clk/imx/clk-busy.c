// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright 2012 Freescale Semiconductor, Inc.
 * Copyright 2012 Linaro Ltd.
 */

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/jiffies.h>
#include <linux/err.h>
#include "clk.h"

static int clk_busy_wait(void __iomem *reg, u8 shift)
{
	unsigned long timeout = jiffies + msecs_to_jiffies(10);

	while (readl_relaxed(reg) & (1 << shift))
		if (time_after(jiffies, timeout))
			return -ETIMEDOUT;

	return 0;
}

struct clk_busy_divider {
	struct clk_divider div;
	const struct clk_ops *div_ops;
	void __iomem *reg;
	u8 shift;
};

static inline struct clk_busy_divider *to_clk_busy_divider(struct clk_hw *hw)
{
	struct clk_divider *div = to_clk_divider(hw);

	return container_of(div, struct clk_busy_divider, div);
}

static unsigned long clk_busy_divider_recalc_rate(struct clk_hw *hw,
						  unsigned long parent_rate)
{
	struct clk_busy_divider *busy = to_clk_busy_divider(hw);

	return busy->div_ops->recalc_rate(&busy->div.hw, parent_rate);
}

static long clk_busy_divider_round_rate(struct clk_hw *hw, unsigned long rate,
					unsigned long *prate)
{
	struct clk_busy_divider *busy = to_clk_busy_divider(hw);

	return busy->div_ops->round_rate(&busy->div.hw, rate, prate);
}

static int clk_busy_divider_set_rate(struct clk_hw *hw, unsigned long rate,
		unsigned long parent_rate)
{
	struct clk_busy_divider *busy = to_clk_busy_divider(hw);
	int ret;

	ret = busy->div_ops->set_rate(&busy->div.hw, rate, parent_rate);
	if (!ret)
		ret = clk_busy_wait(busy->reg, busy->shift);

	return ret;
}

static const struct clk_ops clk_busy_divider_ops = {
	.recalc_rate = clk_busy_divider_recalc_rate,
	.round_rate = clk_busy_divider_round_rate,
	.set_rate = clk_busy_divider_set_rate,
};

struct clk *imx_clk_busy_divider(const char *name, const char *parent_name,
				 void __iomem *reg, u8 shift, u8 width,
				 void __iomem *busy_reg, u8 busy_shift)
{
	struct clk_busy_divider *busy;
	struct clk *clk;
	struct clk_init_data init;

	busy = kzalloc(sizeof(*busy), GFP_KERNEL);
	if (!busy)
		return ERR_PTR(-ENOMEM);

	busy->reg = busy_reg;
	busy->shift = busy_shift;

	busy->div.reg = reg;
	busy->div.shift = shift;
	busy->div.width = width;
	busy->div.lock = &imx_ccm_lock;
	busy->div_ops = &clk_divider_ops;

	init.name = name;
	init.ops = &clk_busy_divider_ops;
	init.flags = CLK_SET_RATE_PARENT | CLK_IS_CRITICAL;
	init.parent_names = &parent_name;
	init.num_parents = 1;

	busy->div.hw.init = &init;

	clk = clk_register(NULL, &busy->div.hw);
	if (IS_ERR(clk))
		kfree(busy);

	return clk;
}

struct clk_busy_mux {
	struct clk_mux mux;
	const struct clk_ops *mux_ops;
	void __iomem *reg;
	u8 shift;
};

static inline struct clk_busy_mux *to_clk_busy_mux(struct clk_hw *hw)
{
	struct clk_mux *mux = to_clk_mux(hw);

	return container_of(mux, struct clk_busy_mux, mux);
}

static u8 clk_busy_mux_get_parent(struct clk_hw *hw)
{
	struct clk_busy_mux *busy = to_clk_busy_mux(hw);

	return busy->mux_ops->get_parent(&busy->mux.hw);
}

static int clk_busy_mux_set_parent(struct clk_hw *hw, u8 index)
{
	struct clk_busy_mux *busy = to_clk_busy_mux(hw);
	int ret;

	ret = busy->mux_ops->set_parent(&busy->mux.hw, index);
	if (!ret)
		ret = clk_busy_wait(busy->reg, busy->shift);

	return ret;
}

static const struct clk_ops clk_busy_mux_ops = {
	.get_parent = clk_busy_mux_get_parent,
	.set_parent = clk_busy_mux_set_parent,
};

struct clk *imx_clk_busy_mux(const char *name, void __iomem *reg, u8 shift,
			     u8 width, void __iomem *busy_reg, u8 busy_shift,
			     const char * const *parent_names, int num_parents)
{
	struct clk_busy_mux *busy;
	struct clk *clk;
	struct clk_init_data init;

	busy = kzalloc(sizeof(*busy), GFP_KERNEL);
	if (!busy)
		return ERR_PTR(-ENOMEM);

	busy->reg = busy_reg;
	busy->shift = busy_shift;

	busy->mux.reg = reg;
	busy->mux.shift = shift;
	busy->mux.mask = BIT(width) - 1;
	busy->mux.lock = &imx_ccm_lock;
	busy->mux_ops = &clk_mux_ops;

	init.name = name;
	init.ops = &clk_busy_mux_ops;
	init.flags = CLK_IS_CRITICAL;
	init.parent_names = parent_names;
	init.num_parents = num_parents;

	busy->mux.hw.init = &init;

	clk = clk_register(NULL, &busy->mux.hw);
	if (IS_ERR(clk))
		kfree(busy);

	return clk;
}

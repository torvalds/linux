/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * Copyright (C) 2013 ARM Limited
 */

#include <linux/amba/sp810.h>
#include <linux/clkdev.h>
#include <linux/clk-provider.h>
#include <linux/err.h>
#include <linux/of.h>
#include <linux/of_address.h>

#define to_clk_sp810_timerclken(_hw) \
		container_of(_hw, struct clk_sp810_timerclken, hw)

struct clk_sp810;

struct clk_sp810_timerclken {
	struct clk_hw hw;
	struct clk *clk;
	struct clk_sp810 *sp810;
	int channel;
};

struct clk_sp810 {
	struct device_node *node;
	int refclk_index, timclk_index;
	void __iomem *base;
	spinlock_t lock;
	struct clk_sp810_timerclken timerclken[4];
	struct clk *refclk;
	struct clk *timclk;
};

static u8 clk_sp810_timerclken_get_parent(struct clk_hw *hw)
{
	struct clk_sp810_timerclken *timerclken = to_clk_sp810_timerclken(hw);
	u32 val = readl(timerclken->sp810->base + SCCTRL);

	return !!(val & (1 << SCCTRL_TIMERENnSEL_SHIFT(timerclken->channel)));
}

static int clk_sp810_timerclken_set_parent(struct clk_hw *hw, u8 index)
{
	struct clk_sp810_timerclken *timerclken = to_clk_sp810_timerclken(hw);
	struct clk_sp810 *sp810 = timerclken->sp810;
	u32 val, shift = SCCTRL_TIMERENnSEL_SHIFT(timerclken->channel);
	unsigned long flags = 0;

	if (WARN_ON(index > 1))
		return -EINVAL;

	spin_lock_irqsave(&sp810->lock, flags);

	val = readl(sp810->base + SCCTRL);
	val &= ~(1 << shift);
	val |= index << shift;
	writel(val, sp810->base + SCCTRL);

	spin_unlock_irqrestore(&sp810->lock, flags);

	return 0;
}

/*
 * FIXME - setting the parent every time .prepare is invoked is inefficient.
 * This is better handled by a dedicated clock tree configuration mechanism at
 * init-time.  Revisit this later when such a mechanism exists
 */
static int clk_sp810_timerclken_prepare(struct clk_hw *hw)
{
	struct clk_sp810_timerclken *timerclken = to_clk_sp810_timerclken(hw);
	struct clk_sp810 *sp810 = timerclken->sp810;
	struct clk *old_parent = __clk_get_parent(hw->clk);
	struct clk *new_parent;

	if (!sp810->refclk)
		sp810->refclk = of_clk_get(sp810->node, sp810->refclk_index);

	if (!sp810->timclk)
		sp810->timclk = of_clk_get(sp810->node, sp810->timclk_index);

	if (WARN_ON(IS_ERR(sp810->refclk) || IS_ERR(sp810->timclk)))
		return -ENOENT;

	/* Select fastest parent */
	if (clk_get_rate(sp810->refclk) > clk_get_rate(sp810->timclk))
		new_parent = sp810->refclk;
	else
		new_parent = sp810->timclk;

	/* Switch the parent if necessary */
	if (old_parent != new_parent) {
		clk_prepare(new_parent);
		clk_set_parent(hw->clk, new_parent);
		clk_unprepare(old_parent);
	}

	return 0;
}

static void clk_sp810_timerclken_unprepare(struct clk_hw *hw)
{
	struct clk_sp810_timerclken *timerclken = to_clk_sp810_timerclken(hw);
	struct clk_sp810 *sp810 = timerclken->sp810;

	clk_put(sp810->timclk);
	clk_put(sp810->refclk);
}

static const struct clk_ops clk_sp810_timerclken_ops = {
	.prepare = clk_sp810_timerclken_prepare,
	.unprepare = clk_sp810_timerclken_unprepare,
	.get_parent = clk_sp810_timerclken_get_parent,
	.set_parent = clk_sp810_timerclken_set_parent,
};

static struct clk *clk_sp810_timerclken_of_get(struct of_phandle_args *clkspec,
		void *data)
{
	struct clk_sp810 *sp810 = data;

	if (WARN_ON(clkspec->args_count != 1 || clkspec->args[0] >
			ARRAY_SIZE(sp810->timerclken)))
		return NULL;

	return sp810->timerclken[clkspec->args[0]].clk;
}

void __init clk_sp810_of_setup(struct device_node *node)
{
	struct clk_sp810 *sp810 = kzalloc(sizeof(*sp810), GFP_KERNEL);
	const char *parent_names[2];
	char name[12];
	struct clk_init_data init;
	int i;

	if (!sp810) {
		pr_err("Failed to allocate memory for SP810!\n");
		return;
	}

	sp810->refclk_index = of_property_match_string(node, "clock-names",
			"refclk");
	parent_names[0] = of_clk_get_parent_name(node, sp810->refclk_index);

	sp810->timclk_index = of_property_match_string(node, "clock-names",
			"timclk");
	parent_names[1] = of_clk_get_parent_name(node, sp810->timclk_index);

	if (parent_names[0] <= 0 || parent_names[1] <= 0) {
		pr_warn("Failed to obtain parent clocks for SP810!\n");
		return;
	}

	sp810->node = node;
	sp810->base = of_iomap(node, 0);
	spin_lock_init(&sp810->lock);

	init.name = name;
	init.ops = &clk_sp810_timerclken_ops;
	init.flags = CLK_IS_BASIC;
	init.parent_names = parent_names;
	init.num_parents = ARRAY_SIZE(parent_names);

	for (i = 0; i < ARRAY_SIZE(sp810->timerclken); i++) {
		snprintf(name, ARRAY_SIZE(name), "timerclken%d", i);

		sp810->timerclken[i].sp810 = sp810;
		sp810->timerclken[i].channel = i;
		sp810->timerclken[i].hw.init = &init;

		sp810->timerclken[i].clk = clk_register(NULL,
				&sp810->timerclken[i].hw);
		WARN_ON(IS_ERR(sp810->timerclken[i].clk));
	}

	of_clk_add_provider(node, clk_sp810_timerclken_of_get, sp810);
}
CLK_OF_DECLARE(sp810, "arm,sp810", clk_sp810_of_setup);

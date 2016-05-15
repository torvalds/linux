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
#include <linux/slab.h>
#include <linux/clk.h>
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
	void __iomem *base;
	spinlock_t lock;
	struct clk_sp810_timerclken timerclken[4];
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

static const struct clk_ops clk_sp810_timerclken_ops = {
	.get_parent = clk_sp810_timerclken_get_parent,
	.set_parent = clk_sp810_timerclken_set_parent,
};

static struct clk *clk_sp810_timerclken_of_get(struct of_phandle_args *clkspec,
		void *data)
{
	struct clk_sp810 *sp810 = data;

	if (WARN_ON(clkspec->args_count != 1 ||
		    clkspec->args[0] >=	ARRAY_SIZE(sp810->timerclken)))
		return NULL;

	return sp810->timerclken[clkspec->args[0]].clk;
}

static void __init clk_sp810_of_setup(struct device_node *node)
{
	struct clk_sp810 *sp810 = kzalloc(sizeof(*sp810), GFP_KERNEL);
	const char *parent_names[2];
	int num = ARRAY_SIZE(parent_names);
	char name[12];
	struct clk_init_data init;
	static int instance;
	int i;
	bool deprecated;

	if (!sp810)
		return;

	if (of_clk_parent_fill(node, parent_names, num) != num) {
		pr_warn("Failed to obtain parent clocks for SP810!\n");
		kfree(sp810);
		return;
	}

	sp810->node = node;
	sp810->base = of_iomap(node, 0);
	spin_lock_init(&sp810->lock);

	init.name = name;
	init.ops = &clk_sp810_timerclken_ops;
	init.flags = CLK_IS_BASIC;
	init.parent_names = parent_names;
	init.num_parents = num;

	deprecated = !of_find_property(node, "assigned-clock-parents", NULL);

	for (i = 0; i < ARRAY_SIZE(sp810->timerclken); i++) {
		snprintf(name, sizeof(name), "sp810_%d_%d", instance, i);

		sp810->timerclken[i].sp810 = sp810;
		sp810->timerclken[i].channel = i;
		sp810->timerclken[i].hw.init = &init;

		/*
		 * If DT isn't setting the parent, force it to be
		 * the 1 MHz clock without going through the framework.
		 * We do this before clk_register() so that it can determine
		 * the parent and setup the tree properly.
		 */
		if (deprecated)
			init.ops->set_parent(&sp810->timerclken[i].hw, 1);

		sp810->timerclken[i].clk = clk_register(NULL,
				&sp810->timerclken[i].hw);
		WARN_ON(IS_ERR(sp810->timerclken[i].clk));
	}

	of_clk_add_provider(node, clk_sp810_timerclken_of_get, sp810);
	instance++;
}
CLK_OF_DECLARE(sp810, "arm,sp810", clk_sp810_of_setup);

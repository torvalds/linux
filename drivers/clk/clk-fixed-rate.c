/*
 * Copyright (C) 2010-2011 Canonical Ltd <jeremy.kerr@canonical.com>
 * Copyright (C) 2011-2012 Mike Turquette, Linaro Ltd <mturquette@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Fixed rate clock implementation
 */

#include <linux/clk-provider.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/err.h>
#include <linux/of.h>

/*
 * DOC: basic fixed-rate clock that cannot gate
 *
 * Traits of this clock:
 * prepare - clk_(un)prepare only ensures parents are prepared
 * enable - clk_enable only ensures parents are enabled
 * rate - rate is always a fixed value.  No clk_set_rate support
 * parent - fixed parent.  No clk_set_parent support
 */

static unsigned long clk_fixed_rate_recalc_rate(struct clk_hw *hw,
		unsigned long parent_rate)
{
	return to_clk_fixed_rate(hw)->fixed_rate;
}

static unsigned long clk_fixed_rate_recalc_accuracy(struct clk_hw *hw,
		unsigned long parent_accuracy)
{
	return to_clk_fixed_rate(hw)->fixed_accuracy;
}

const struct clk_ops clk_fixed_rate_ops = {
	.recalc_rate = clk_fixed_rate_recalc_rate,
	.recalc_accuracy = clk_fixed_rate_recalc_accuracy,
};
EXPORT_SYMBOL_GPL(clk_fixed_rate_ops);

/**
 * clk_register_fixed_rate_with_accuracy - register fixed-rate clock with the
 *					   clock framework
 * @dev: device that is registering this clock
 * @name: name of this clock
 * @parent_name: name of clock's parent
 * @flags: framework-specific flags
 * @fixed_rate: non-adjustable clock rate
 * @fixed_accuracy: non-adjustable clock rate
 */
struct clk *clk_register_fixed_rate_with_accuracy(struct device *dev,
		const char *name, const char *parent_name, unsigned long flags,
		unsigned long fixed_rate, unsigned long fixed_accuracy)
{
	struct clk_fixed_rate *fixed;
	struct clk *clk;
	struct clk_init_data init;

	/* allocate fixed-rate clock */
	fixed = kzalloc(sizeof(*fixed), GFP_KERNEL);
	if (!fixed)
		return ERR_PTR(-ENOMEM);

	init.name = name;
	init.ops = &clk_fixed_rate_ops;
	init.flags = flags | CLK_IS_BASIC;
	init.parent_names = (parent_name ? &parent_name: NULL);
	init.num_parents = (parent_name ? 1 : 0);

	/* struct clk_fixed_rate assignments */
	fixed->fixed_rate = fixed_rate;
	fixed->fixed_accuracy = fixed_accuracy;
	fixed->hw.init = &init;

	/* register the clock */
	clk = clk_register(dev, &fixed->hw);
	if (IS_ERR(clk))
		kfree(fixed);

	return clk;
}
EXPORT_SYMBOL_GPL(clk_register_fixed_rate_with_accuracy);

/**
 * clk_register_fixed_rate - register fixed-rate clock with the clock framework
 * @dev: device that is registering this clock
 * @name: name of this clock
 * @parent_name: name of clock's parent
 * @flags: framework-specific flags
 * @fixed_rate: non-adjustable clock rate
 */
struct clk *clk_register_fixed_rate(struct device *dev, const char *name,
		const char *parent_name, unsigned long flags,
		unsigned long fixed_rate)
{
	return clk_register_fixed_rate_with_accuracy(dev, name, parent_name,
						     flags, fixed_rate, 0);
}
EXPORT_SYMBOL_GPL(clk_register_fixed_rate);

#ifdef CONFIG_OF
/**
 * of_fixed_clk_setup() - Setup function for simple fixed rate clock
 */
void of_fixed_clk_setup(struct device_node *node)
{
	struct clk *clk;
	const char *clk_name = node->name;
	u32 rate;
	u32 accuracy = 0;

	if (of_property_read_u32(node, "clock-frequency", &rate))
		return;

	of_property_read_u32(node, "clock-accuracy", &accuracy);

	of_property_read_string(node, "clock-output-names", &clk_name);

	clk = clk_register_fixed_rate_with_accuracy(NULL, clk_name, NULL,
						    CLK_IS_ROOT, rate,
						    accuracy);
	if (!IS_ERR(clk))
		of_clk_add_provider(node, of_clk_src_simple_get, clk);
}
EXPORT_SYMBOL_GPL(of_fixed_clk_setup);
CLK_OF_DECLARE(fixed_clk, "fixed-clock", of_fixed_clk_setup);
#endif

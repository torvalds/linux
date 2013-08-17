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

/*
 * DOC: basic fixed-rate clock that cannot gate
 *
 * Traits of this clock:
 * prepare - clk_(un)prepare only ensures parents are prepared
 * enable - clk_enable only ensures parents are enabled
 * rate - rate is always a fixed value.  No clk_set_rate support
 * parent - fixed parent.  No clk_set_parent support
 */

#define to_clk_fixed_rate(_hw) container_of(_hw, struct clk_fixed_rate, hw)

static unsigned long clk_fixed_rate_recalc_rate(struct clk_hw *hw,
		unsigned long parent_rate)
{
	return to_clk_fixed_rate(hw)->fixed_rate;
}
EXPORT_SYMBOL_GPL(clk_fixed_rate_recalc_rate);

struct clk_ops clk_fixed_rate_ops = {
	.recalc_rate = clk_fixed_rate_recalc_rate,
};
EXPORT_SYMBOL_GPL(clk_fixed_rate_ops);

struct clk *clk_register_fixed_rate(struct device *dev, const char *name,
		const char *parent_name, unsigned long flags,
		unsigned long fixed_rate)
{
	struct clk_fixed_rate *fixed;
	char **parent_names = NULL;
	u8 len;

	fixed = kzalloc(sizeof(struct clk_fixed_rate), GFP_KERNEL);

	if (!fixed) {
		pr_err("%s: could not allocate fixed clk\n", __func__);
		return ERR_PTR(-ENOMEM);
	}

	/* struct clk_fixed_rate assignments */
	fixed->fixed_rate = fixed_rate;

	if (parent_name) {
		parent_names = kmalloc(sizeof(char *), GFP_KERNEL);

		if (! parent_names)
			goto out;

		len = sizeof(char) * strlen(parent_name);

		parent_names[0] = kmalloc(len, GFP_KERNEL);

		if (!parent_names[0])
			goto out;

		strncpy(parent_names[0], parent_name, len);
	}

out:
	return clk_register(dev, name,
			&clk_fixed_rate_ops, &fixed->hw,
			parent_names,
			(parent_name ? 1 : 0),
			flags);
}

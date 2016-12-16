/*
 * Copyright 2016 Maxime Ripard
 *
 * Maxime Ripard <maxime.ripard@free-electrons.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/clk-provider.h>
#include <linux/iopoll.h>
#include <linux/slab.h>

#include "ccu_common.h"
#include "ccu_reset.h"

static DEFINE_SPINLOCK(ccu_lock);

void ccu_helper_wait_for_lock(struct ccu_common *common, u32 lock)
{
	u32 reg;

	if (!lock)
		return;

	WARN_ON(readl_relaxed_poll_timeout(common->base + common->reg, reg,
					   reg & lock, 100, 70000));
}

int sunxi_ccu_probe(struct device_node *node, void __iomem *reg,
		    const struct sunxi_ccu_desc *desc)
{
	struct ccu_reset *reset;
	int i, ret;

	for (i = 0; i < desc->num_ccu_clks; i++) {
		struct ccu_common *cclk = desc->ccu_clks[i];

		if (!cclk)
			continue;

		cclk->base = reg;
		cclk->lock = &ccu_lock;
	}

	for (i = 0; i < desc->hw_clks->num ; i++) {
		struct clk_hw *hw = desc->hw_clks->hws[i];

		if (!hw)
			continue;

		ret = clk_hw_register(NULL, hw);
		if (ret) {
			pr_err("Couldn't register clock %s\n",
			       clk_hw_get_name(hw));
			goto err_clk_unreg;
		}
	}

	ret = of_clk_add_hw_provider(node, of_clk_hw_onecell_get,
				     desc->hw_clks);
	if (ret)
		goto err_clk_unreg;

	reset = kzalloc(sizeof(*reset), GFP_KERNEL);
	reset->rcdev.of_node = node;
	reset->rcdev.ops = &ccu_reset_ops;
	reset->rcdev.owner = THIS_MODULE;
	reset->rcdev.nr_resets = desc->num_resets;
	reset->base = reg;
	reset->lock = &ccu_lock;
	reset->reset_map = desc->resets;

	ret = reset_controller_register(&reset->rcdev);
	if (ret)
		goto err_of_clk_unreg;

	return 0;

err_of_clk_unreg:
err_clk_unreg:
	return ret;
}

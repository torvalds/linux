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

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/iopoll.h>
#include <linux/slab.h>

#include "ccu_common.h"
#include "ccu_gate.h"
#include "ccu_reset.h"

static DEFINE_SPINLOCK(ccu_lock);

void ccu_helper_wait_for_lock(struct ccu_common *common, u32 lock)
{
	void __iomem *addr;
	u32 reg;

	if (!lock)
		return;

	if (common->features & CCU_FEATURE_LOCK_REG)
		addr = common->base + common->lock_reg;
	else
		addr = common->base + common->reg;

	WARN_ON(readl_relaxed_poll_timeout(addr, reg, reg & lock, 100, 70000));
}

/*
 * This clock notifier is called when the frequency of a PLL clock is
 * changed. In common PLL designs, changes to the dividers take effect
 * almost immediately, while changes to the multipliers (implemented
 * as dividers in the feedback loop) take a few cycles to work into
 * the feedback loop for the PLL to stablize.
 *
 * Sometimes when the PLL clock rate is changed, the decrease in the
 * divider is too much for the decrease in the multiplier to catch up.
 * The PLL clock rate will spike, and in some cases, might lock up
 * completely.
 *
 * This notifier callback will gate and then ungate the clock,
 * effectively resetting it, so it proceeds to work. Care must be
 * taken to reparent consumers to other temporary clocks during the
 * rate change, and that this notifier callback must be the first
 * to be registered.
 */
static int ccu_pll_notifier_cb(struct notifier_block *nb,
			       unsigned long event, void *data)
{
	struct ccu_pll_nb *pll = to_ccu_pll_nb(nb);
	int ret = 0;

	if (event != POST_RATE_CHANGE)
		goto out;

	ccu_gate_helper_disable(pll->common, pll->enable);

	ret = ccu_gate_helper_enable(pll->common, pll->enable);
	if (ret)
		goto out;

	ccu_helper_wait_for_lock(pll->common, pll->lock);

out:
	return notifier_from_errno(ret);
}

int ccu_pll_notifier_register(struct ccu_pll_nb *pll_nb)
{
	pll_nb->clk_nb.notifier_call = ccu_pll_notifier_cb;

	return clk_notifier_register(pll_nb->common->hw.clk,
				     &pll_nb->clk_nb);
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
	if (!reset) {
		ret = -ENOMEM;
		goto err_alloc_reset;
	}

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
	kfree(reset);
err_alloc_reset:
	of_clk_del_provider(node);
err_clk_unreg:
	while (--i >= 0) {
		struct clk_hw *hw = desc->hw_clks->hws[i];

		if (!hw)
			continue;
		clk_hw_unregister(hw);
	}
	return ret;
}

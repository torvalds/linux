/*
 * OMAP APLL clock support
 *
 * Copyright (C) 2013 Texas Instruments, Inc.
 *
 * J Keerthy <j-keerthy@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/err.h>
#include <linux/string.h>
#include <linux/log2.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/clk/ti.h>
#include <linux/delay.h>

#include "clock.h"

#define APLL_FORCE_LOCK 0x1
#define APLL_AUTO_IDLE	0x2
#define MAX_APLL_WAIT_TRIES		1000000

#undef pr_fmt
#define pr_fmt(fmt) "%s: " fmt, __func__

static int dra7_apll_enable(struct clk_hw *hw)
{
	struct clk_hw_omap *clk = to_clk_hw_omap(hw);
	int r = 0, i = 0;
	struct dpll_data *ad;
	const char *clk_name;
	u8 state = 1;
	u32 v;

	ad = clk->dpll_data;
	if (!ad)
		return -EINVAL;

	clk_name = clk_hw_get_name(&clk->hw);

	state <<= __ffs(ad->idlest_mask);

	/* Check is already locked */
	v = ti_clk_ll_ops->clk_readl(ad->idlest_reg);

	if ((v & ad->idlest_mask) == state)
		return r;

	v = ti_clk_ll_ops->clk_readl(ad->control_reg);
	v &= ~ad->enable_mask;
	v |= APLL_FORCE_LOCK << __ffs(ad->enable_mask);
	ti_clk_ll_ops->clk_writel(v, ad->control_reg);

	state <<= __ffs(ad->idlest_mask);

	while (1) {
		v = ti_clk_ll_ops->clk_readl(ad->idlest_reg);
		if ((v & ad->idlest_mask) == state)
			break;
		if (i > MAX_APLL_WAIT_TRIES)
			break;
		i++;
		udelay(1);
	}

	if (i == MAX_APLL_WAIT_TRIES) {
		pr_warn("clock: %s failed transition to '%s'\n",
			clk_name, (state) ? "locked" : "bypassed");
		r = -EBUSY;
	} else
		pr_debug("clock: %s transition to '%s' in %d loops\n",
			 clk_name, (state) ? "locked" : "bypassed", i);

	return r;
}

static void dra7_apll_disable(struct clk_hw *hw)
{
	struct clk_hw_omap *clk = to_clk_hw_omap(hw);
	struct dpll_data *ad;
	u8 state = 1;
	u32 v;

	ad = clk->dpll_data;

	state <<= __ffs(ad->idlest_mask);

	v = ti_clk_ll_ops->clk_readl(ad->control_reg);
	v &= ~ad->enable_mask;
	v |= APLL_AUTO_IDLE << __ffs(ad->enable_mask);
	ti_clk_ll_ops->clk_writel(v, ad->control_reg);
}

static int dra7_apll_is_enabled(struct clk_hw *hw)
{
	struct clk_hw_omap *clk = to_clk_hw_omap(hw);
	struct dpll_data *ad;
	u32 v;

	ad = clk->dpll_data;

	v = ti_clk_ll_ops->clk_readl(ad->control_reg);
	v &= ad->enable_mask;

	v >>= __ffs(ad->enable_mask);

	return v == APLL_AUTO_IDLE ? 0 : 1;
}

static u8 dra7_init_apll_parent(struct clk_hw *hw)
{
	return 0;
}

static const struct clk_ops apll_ck_ops = {
	.enable		= &dra7_apll_enable,
	.disable	= &dra7_apll_disable,
	.is_enabled	= &dra7_apll_is_enabled,
	.get_parent	= &dra7_init_apll_parent,
};

static void __init omap_clk_register_apll(struct clk_hw *hw,
					  struct device_node *node)
{
	struct clk_hw_omap *clk_hw = to_clk_hw_omap(hw);
	struct dpll_data *ad = clk_hw->dpll_data;
	struct clk *clk;

	ad->clk_ref = of_clk_get(node, 0);
	ad->clk_bypass = of_clk_get(node, 1);

	if (IS_ERR(ad->clk_ref) || IS_ERR(ad->clk_bypass)) {
		pr_debug("clk-ref or clk-bypass for %s not ready, retry\n",
			 node->name);
		if (!ti_clk_retry_init(node, hw, omap_clk_register_apll))
			return;

		goto cleanup;
	}

	clk = clk_register(NULL, &clk_hw->hw);
	if (!IS_ERR(clk)) {
		of_clk_add_provider(node, of_clk_src_simple_get, clk);
		kfree(clk_hw->hw.init->parent_names);
		kfree(clk_hw->hw.init);
		return;
	}

cleanup:
	kfree(clk_hw->dpll_data);
	kfree(clk_hw->hw.init->parent_names);
	kfree(clk_hw->hw.init);
	kfree(clk_hw);
}

static void __init of_dra7_apll_setup(struct device_node *node)
{
	struct dpll_data *ad = NULL;
	struct clk_hw_omap *clk_hw = NULL;
	struct clk_init_data *init = NULL;
	const char **parent_names = NULL;

	ad = kzalloc(sizeof(*ad), GFP_KERNEL);
	clk_hw = kzalloc(sizeof(*clk_hw), GFP_KERNEL);
	init = kzalloc(sizeof(*init), GFP_KERNEL);
	if (!ad || !clk_hw || !init)
		goto cleanup;

	clk_hw->dpll_data = ad;
	clk_hw->hw.init = init;
	clk_hw->flags = MEMMAP_ADDRESSING;

	init->name = node->name;
	init->ops = &apll_ck_ops;

	init->num_parents = of_clk_get_parent_count(node);
	if (init->num_parents < 1) {
		pr_err("dra7 apll %s must have parent(s)\n", node->name);
		goto cleanup;
	}

	parent_names = kzalloc(sizeof(char *) * init->num_parents, GFP_KERNEL);
	if (!parent_names)
		goto cleanup;

	of_clk_parent_fill(node, parent_names, init->num_parents);

	init->parent_names = parent_names;

	ad->control_reg = ti_clk_get_reg_addr(node, 0);
	ad->idlest_reg = ti_clk_get_reg_addr(node, 1);

	if (IS_ERR(ad->control_reg) || IS_ERR(ad->idlest_reg))
		goto cleanup;

	ad->idlest_mask = 0x1;
	ad->enable_mask = 0x3;

	omap_clk_register_apll(&clk_hw->hw, node);
	return;

cleanup:
	kfree(parent_names);
	kfree(ad);
	kfree(clk_hw);
	kfree(init);
}
CLK_OF_DECLARE(dra7_apll_clock, "ti,dra7-apll-clock", of_dra7_apll_setup);

#define OMAP2_EN_APLL_LOCKED	0x3
#define OMAP2_EN_APLL_STOPPED	0x0

static int omap2_apll_is_enabled(struct clk_hw *hw)
{
	struct clk_hw_omap *clk = to_clk_hw_omap(hw);
	struct dpll_data *ad = clk->dpll_data;
	u32 v;

	v = ti_clk_ll_ops->clk_readl(ad->control_reg);
	v &= ad->enable_mask;

	v >>= __ffs(ad->enable_mask);

	return v == OMAP2_EN_APLL_LOCKED ? 1 : 0;
}

static unsigned long omap2_apll_recalc(struct clk_hw *hw,
				       unsigned long parent_rate)
{
	struct clk_hw_omap *clk = to_clk_hw_omap(hw);

	if (omap2_apll_is_enabled(hw))
		return clk->fixed_rate;

	return 0;
}

static int omap2_apll_enable(struct clk_hw *hw)
{
	struct clk_hw_omap *clk = to_clk_hw_omap(hw);
	struct dpll_data *ad = clk->dpll_data;
	u32 v;
	int i = 0;

	v = ti_clk_ll_ops->clk_readl(ad->control_reg);
	v &= ~ad->enable_mask;
	v |= OMAP2_EN_APLL_LOCKED << __ffs(ad->enable_mask);
	ti_clk_ll_ops->clk_writel(v, ad->control_reg);

	while (1) {
		v = ti_clk_ll_ops->clk_readl(ad->idlest_reg);
		if (v & ad->idlest_mask)
			break;
		if (i > MAX_APLL_WAIT_TRIES)
			break;
		i++;
		udelay(1);
	}

	if (i == MAX_APLL_WAIT_TRIES) {
		pr_warn("%s failed to transition to locked\n",
			clk_hw_get_name(&clk->hw));
		return -EBUSY;
	}

	return 0;
}

static void omap2_apll_disable(struct clk_hw *hw)
{
	struct clk_hw_omap *clk = to_clk_hw_omap(hw);
	struct dpll_data *ad = clk->dpll_data;
	u32 v;

	v = ti_clk_ll_ops->clk_readl(ad->control_reg);
	v &= ~ad->enable_mask;
	v |= OMAP2_EN_APLL_STOPPED << __ffs(ad->enable_mask);
	ti_clk_ll_ops->clk_writel(v, ad->control_reg);
}

static struct clk_ops omap2_apll_ops = {
	.enable		= &omap2_apll_enable,
	.disable	= &omap2_apll_disable,
	.is_enabled	= &omap2_apll_is_enabled,
	.recalc_rate	= &omap2_apll_recalc,
};

static void omap2_apll_set_autoidle(struct clk_hw_omap *clk, u32 val)
{
	struct dpll_data *ad = clk->dpll_data;
	u32 v;

	v = ti_clk_ll_ops->clk_readl(ad->autoidle_reg);
	v &= ~ad->autoidle_mask;
	v |= val << __ffs(ad->autoidle_mask);
	ti_clk_ll_ops->clk_writel(v, ad->control_reg);
}

#define OMAP2_APLL_AUTOIDLE_LOW_POWER_STOP	0x3
#define OMAP2_APLL_AUTOIDLE_DISABLE		0x0

static void omap2_apll_allow_idle(struct clk_hw_omap *clk)
{
	omap2_apll_set_autoidle(clk, OMAP2_APLL_AUTOIDLE_LOW_POWER_STOP);
}

static void omap2_apll_deny_idle(struct clk_hw_omap *clk)
{
	omap2_apll_set_autoidle(clk, OMAP2_APLL_AUTOIDLE_DISABLE);
}

static const struct clk_hw_omap_ops omap2_apll_hwops = {
	.allow_idle	= &omap2_apll_allow_idle,
	.deny_idle	= &omap2_apll_deny_idle,
};

static void __init of_omap2_apll_setup(struct device_node *node)
{
	struct dpll_data *ad = NULL;
	struct clk_hw_omap *clk_hw = NULL;
	struct clk_init_data *init = NULL;
	struct clk *clk;
	const char *parent_name;
	u32 val;

	ad = kzalloc(sizeof(*ad), GFP_KERNEL);
	clk_hw = kzalloc(sizeof(*clk_hw), GFP_KERNEL);
	init = kzalloc(sizeof(*init), GFP_KERNEL);

	if (!ad || !clk_hw || !init)
		goto cleanup;

	clk_hw->dpll_data = ad;
	clk_hw->hw.init = init;
	init->ops = &omap2_apll_ops;
	init->name = node->name;
	clk_hw->ops = &omap2_apll_hwops;

	init->num_parents = of_clk_get_parent_count(node);
	if (init->num_parents != 1) {
		pr_err("%s must have one parent\n", node->name);
		goto cleanup;
	}

	parent_name = of_clk_get_parent_name(node, 0);
	init->parent_names = &parent_name;

	if (of_property_read_u32(node, "ti,clock-frequency", &val)) {
		pr_err("%s missing clock-frequency\n", node->name);
		goto cleanup;
	}
	clk_hw->fixed_rate = val;

	if (of_property_read_u32(node, "ti,bit-shift", &val)) {
		pr_err("%s missing bit-shift\n", node->name);
		goto cleanup;
	}

	clk_hw->enable_bit = val;
	ad->enable_mask = 0x3 << val;
	ad->autoidle_mask = 0x3 << val;

	if (of_property_read_u32(node, "ti,idlest-shift", &val)) {
		pr_err("%s missing idlest-shift\n", node->name);
		goto cleanup;
	}

	ad->idlest_mask = 1 << val;

	ad->control_reg = ti_clk_get_reg_addr(node, 0);
	ad->autoidle_reg = ti_clk_get_reg_addr(node, 1);
	ad->idlest_reg = ti_clk_get_reg_addr(node, 2);

	if (IS_ERR(ad->control_reg) || IS_ERR(ad->autoidle_reg) ||
	    IS_ERR(ad->idlest_reg))
		goto cleanup;

	clk = clk_register(NULL, &clk_hw->hw);
	if (!IS_ERR(clk)) {
		of_clk_add_provider(node, of_clk_src_simple_get, clk);
		kfree(init);
		return;
	}
cleanup:
	kfree(ad);
	kfree(clk_hw);
	kfree(init);
}
CLK_OF_DECLARE(omap2_apll_clock, "ti,omap2-apll-clock",
	       of_omap2_apll_setup);

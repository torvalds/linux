/*
 * OMAP clockdomain support
 *
 * Copyright (C) 2013 Texas Instruments, Inc.
 *
 * Tero Kristo <t-kristo@ti.com>
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
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/clk/ti.h>

#include "clock.h"

#undef pr_fmt
#define pr_fmt(fmt) "%s: " fmt, __func__

/**
 * omap2_clkops_enable_clkdm - increment usecount on clkdm of @hw
 * @hw: struct clk_hw * of the clock being enabled
 *
 * Increment the usecount of the clockdomain of the clock pointed to
 * by @hw; if the usecount is 1, the clockdomain will be "enabled."
 * Only needed for clocks that don't use omap2_dflt_clk_enable() as
 * their enable function pointer.  Passes along the return value of
 * clkdm_clk_enable(), -EINVAL if @hw is not associated with a
 * clockdomain, or 0 if clock framework-based clockdomain control is
 * not implemented.
 */
int omap2_clkops_enable_clkdm(struct clk_hw *hw)
{
	struct clk_hw_omap *clk;
	int ret = 0;

	clk = to_clk_hw_omap(hw);

	if (unlikely(!clk->clkdm)) {
		pr_err("%s: %s: no clkdm set ?!\n", __func__,
		       clk_hw_get_name(hw));
		return -EINVAL;
	}

	if (ti_clk_get_features()->flags & TI_CLK_DISABLE_CLKDM_CONTROL) {
		pr_err("%s: %s: clkfw-based clockdomain control disabled ?!\n",
		       __func__, clk_hw_get_name(hw));
		return 0;
	}

	ret = ti_clk_ll_ops->clkdm_clk_enable(clk->clkdm, hw->clk);
	WARN(ret, "%s: could not enable %s's clockdomain %s: %d\n",
	     __func__, clk_hw_get_name(hw), clk->clkdm_name, ret);

	return ret;
}

/**
 * omap2_clkops_disable_clkdm - decrement usecount on clkdm of @hw
 * @hw: struct clk_hw * of the clock being disabled
 *
 * Decrement the usecount of the clockdomain of the clock pointed to
 * by @hw; if the usecount is 0, the clockdomain will be "disabled."
 * Only needed for clocks that don't use omap2_dflt_clk_disable() as their
 * disable function pointer.  No return value.
 */
void omap2_clkops_disable_clkdm(struct clk_hw *hw)
{
	struct clk_hw_omap *clk;

	clk = to_clk_hw_omap(hw);

	if (unlikely(!clk->clkdm)) {
		pr_err("%s: %s: no clkdm set ?!\n", __func__,
		       clk_hw_get_name(hw));
		return;
	}

	if (ti_clk_get_features()->flags & TI_CLK_DISABLE_CLKDM_CONTROL) {
		pr_err("%s: %s: clkfw-based clockdomain control disabled ?!\n",
		       __func__, clk_hw_get_name(hw));
		return;
	}

	ti_clk_ll_ops->clkdm_clk_disable(clk->clkdm, hw->clk);
}

/**
 * omap2_init_clk_clkdm - look up a clockdomain name, store pointer in clk
 * @hw: Pointer to clk_hw_omap used to obtain OMAP clock struct ptr to use
 *
 * Convert a clockdomain name stored in a struct clk 'clk' into a
 * clockdomain pointer, and save it into the struct clk.  Intended to be
 * called during clk_register(). Returns 0 on success, -EERROR otherwise.
 */
int omap2_init_clk_clkdm(struct clk_hw *hw)
{
	struct clk_hw_omap *clk = to_clk_hw_omap(hw);
	struct clockdomain *clkdm;
	const char *clk_name;

	if (!clk->clkdm_name)
		return 0;

	clk_name = __clk_get_name(hw->clk);

	clkdm = ti_clk_ll_ops->clkdm_lookup(clk->clkdm_name);
	if (clkdm) {
		pr_debug("clock: associated clk %s to clkdm %s\n",
			 clk_name, clk->clkdm_name);
		clk->clkdm = clkdm;
	} else {
		pr_debug("clock: could not associate clk %s to clkdm %s\n",
			 clk_name, clk->clkdm_name);
	}

	return 0;
}

static void __init of_ti_clockdomain_setup(struct device_node *node)
{
	struct clk *clk;
	struct clk_hw *clk_hw;
	const char *clkdm_name = node->name;
	int i;
	unsigned int num_clks;

	num_clks = of_clk_get_parent_count(node);

	for (i = 0; i < num_clks; i++) {
		clk = of_clk_get(node, i);
		if (IS_ERR(clk)) {
			pr_err("%s: Failed get %pOF' clock nr %d (%ld)\n",
			       __func__, node, i, PTR_ERR(clk));
			continue;
		}
		clk_hw = __clk_get_hw(clk);
		if (!omap2_clk_is_hw_omap(clk_hw)) {
			pr_warn("can't setup clkdm for basic clk %s\n",
				__clk_get_name(clk));
			clk_put(clk);
			continue;
		}
		to_clk_hw_omap(clk_hw)->clkdm_name = clkdm_name;
		omap2_init_clk_clkdm(clk_hw);
		clk_put(clk);
	}
}

static const struct of_device_id ti_clkdm_match_table[] __initconst = {
	{ .compatible = "ti,clockdomain" },
	{ }
};

/**
 * ti_dt_clockdomains_setup - setup device tree clockdomains
 *
 * Initializes clockdomain nodes for a SoC. This parses through all the
 * nodes with compatible = "ti,clockdomain", and add the clockdomain
 * info for all the clocks listed under these. This function shall be
 * called after rest of the DT clock init has completed and all
 * clock nodes have been registered.
 */
void __init ti_dt_clockdomains_setup(void)
{
	struct device_node *np;
	for_each_matching_node(np, ti_clkdm_match_table) {
		of_ti_clockdomain_setup(np);
	}
}

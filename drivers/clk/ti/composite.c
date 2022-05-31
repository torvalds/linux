/*
 * TI composite clock support
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

#include <linux/clk-provider.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/clk/ti.h>
#include <linux/list.h>

#include "clock.h"

#undef pr_fmt
#define pr_fmt(fmt) "%s: " fmt, __func__

static unsigned long ti_composite_recalc_rate(struct clk_hw *hw,
					      unsigned long parent_rate)
{
	return ti_clk_divider_ops.recalc_rate(hw, parent_rate);
}

static long ti_composite_round_rate(struct clk_hw *hw, unsigned long rate,
				    unsigned long *prate)
{
	return -EINVAL;
}

static int ti_composite_set_rate(struct clk_hw *hw, unsigned long rate,
				 unsigned long parent_rate)
{
	return -EINVAL;
}

static const struct clk_ops ti_composite_divider_ops = {
	.recalc_rate	= &ti_composite_recalc_rate,
	.round_rate	= &ti_composite_round_rate,
	.set_rate	= &ti_composite_set_rate,
};

static const struct clk_ops ti_composite_gate_ops = {
	.enable		= &omap2_dflt_clk_enable,
	.disable	= &omap2_dflt_clk_disable,
	.is_enabled	= &omap2_dflt_clk_is_enabled,
};

struct component_clk {
	int num_parents;
	const char **parent_names;
	struct device_node *node;
	int type;
	struct clk_hw *hw;
	struct list_head link;
};

static const char * const component_clk_types[] __initconst = {
	"gate", "divider", "mux"
};

static LIST_HEAD(component_clks);

static struct device_node *_get_component_node(struct device_node *node, int i)
{
	int rc;
	struct of_phandle_args clkspec;

	rc = of_parse_phandle_with_args(node, "clocks", "#clock-cells", i,
					&clkspec);
	if (rc)
		return NULL;

	return clkspec.np;
}

static struct component_clk *_lookup_component(struct device_node *node)
{
	struct component_clk *comp;

	list_for_each_entry(comp, &component_clks, link) {
		if (comp->node == node)
			return comp;
	}
	return NULL;
}

struct clk_hw_omap_comp {
	struct clk_hw hw;
	struct device_node *comp_nodes[CLK_COMPONENT_TYPE_MAX];
	struct component_clk *comp_clks[CLK_COMPONENT_TYPE_MAX];
};

static inline struct clk_hw *_get_hw(struct clk_hw_omap_comp *clk, int idx)
{
	if (!clk)
		return NULL;

	if (!clk->comp_clks[idx])
		return NULL;

	return clk->comp_clks[idx]->hw;
}

#define to_clk_hw_comp(_hw) container_of(_hw, struct clk_hw_omap_comp, hw)

static void __init _register_composite(void *user,
				       struct device_node *node)
{
	struct clk_hw *hw = user;
	struct clk *clk;
	struct clk_hw_omap_comp *cclk = to_clk_hw_comp(hw);
	struct component_clk *comp;
	int num_parents = 0;
	const char **parent_names = NULL;
	const char *name;
	int i;
	int ret;

	/* Check for presence of each component clock */
	for (i = 0; i < CLK_COMPONENT_TYPE_MAX; i++) {
		if (!cclk->comp_nodes[i])
			continue;

		comp = _lookup_component(cclk->comp_nodes[i]);
		if (!comp) {
			pr_debug("component %s not ready for %pOFn, retry\n",
				 cclk->comp_nodes[i]->name, node);
			if (!ti_clk_retry_init(node, hw,
					       _register_composite))
				return;

			goto cleanup;
		}
		if (cclk->comp_clks[comp->type] != NULL) {
			pr_err("duplicate component types for %pOFn (%s)!\n",
			       node, component_clk_types[comp->type]);
			goto cleanup;
		}

		cclk->comp_clks[comp->type] = comp;

		/* Mark this node as found */
		cclk->comp_nodes[i] = NULL;
	}

	/* All components exists, proceed with registration */
	for (i = CLK_COMPONENT_TYPE_MAX - 1; i >= 0; i--) {
		comp = cclk->comp_clks[i];
		if (!comp)
			continue;
		if (comp->num_parents) {
			num_parents = comp->num_parents;
			parent_names = comp->parent_names;
			break;
		}
	}

	if (!num_parents) {
		pr_err("%s: no parents found for %pOFn!\n", __func__, node);
		goto cleanup;
	}

	name = ti_dt_clk_name(node);
	clk = clk_register_composite(NULL, name,
				     parent_names, num_parents,
				     _get_hw(cclk, CLK_COMPONENT_TYPE_MUX),
				     &ti_clk_mux_ops,
				     _get_hw(cclk, CLK_COMPONENT_TYPE_DIVIDER),
				     &ti_composite_divider_ops,
				     _get_hw(cclk, CLK_COMPONENT_TYPE_GATE),
				     &ti_composite_gate_ops, 0);

	if (!IS_ERR(clk)) {
		ret = ti_clk_add_alias(NULL, clk, name);
		if (ret) {
			clk_unregister(clk);
			goto cleanup;
		}
		of_clk_add_provider(node, of_clk_src_simple_get, clk);
	}

cleanup:
	/* Free component clock list entries */
	for (i = 0; i < CLK_COMPONENT_TYPE_MAX; i++) {
		if (!cclk->comp_clks[i])
			continue;
		list_del(&cclk->comp_clks[i]->link);
		kfree(cclk->comp_clks[i]->parent_names);
		kfree(cclk->comp_clks[i]);
	}

	kfree(cclk);
}

static void __init of_ti_composite_clk_setup(struct device_node *node)
{
	unsigned int num_clks;
	int i;
	struct clk_hw_omap_comp *cclk;

	/* Number of component clocks to be put inside this clock */
	num_clks = of_clk_get_parent_count(node);

	if (!num_clks) {
		pr_err("composite clk %pOFn must have component(s)\n", node);
		return;
	}

	cclk = kzalloc(sizeof(*cclk), GFP_KERNEL);
	if (!cclk)
		return;

	/* Get device node pointers for each component clock */
	for (i = 0; i < num_clks; i++)
		cclk->comp_nodes[i] = _get_component_node(node, i);

	_register_composite(&cclk->hw, node);
}
CLK_OF_DECLARE(ti_composite_clock, "ti,composite-clock",
	       of_ti_composite_clk_setup);

/**
 * ti_clk_add_component - add a component clock to the pool
 * @node: device node of the component clock
 * @hw: hardware clock definition for the component clock
 * @type: type of the component clock
 *
 * Adds a component clock to the list of available components, so that
 * it can be registered by a composite clock.
 */
int __init ti_clk_add_component(struct device_node *node, struct clk_hw *hw,
				int type)
{
	unsigned int num_parents;
	const char **parent_names;
	struct component_clk *clk;

	num_parents = of_clk_get_parent_count(node);

	if (!num_parents) {
		pr_err("component-clock %pOFn must have parent(s)\n", node);
		return -EINVAL;
	}

	parent_names = kzalloc((sizeof(char *) * num_parents), GFP_KERNEL);
	if (!parent_names)
		return -ENOMEM;

	of_clk_parent_fill(node, parent_names, num_parents);

	clk = kzalloc(sizeof(*clk), GFP_KERNEL);
	if (!clk) {
		kfree(parent_names);
		return -ENOMEM;
	}

	clk->num_parents = num_parents;
	clk->parent_names = parent_names;
	clk->hw = hw;
	clk->node = node;
	clk->type = type;
	list_add(&clk->link, &component_clks);

	return 0;
}

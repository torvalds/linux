/*
 * TI clock support
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
#include <linux/clkdev.h>
#include <linux/clk/ti.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/list.h>

#undef pr_fmt
#define pr_fmt(fmt) "%s: " fmt, __func__

struct ti_clk_ll_ops *ti_clk_ll_ops;
static struct device_node *clocks_node_ptr[CLK_MAX_MEMMAPS];

/**
 * ti_dt_clocks_register - register DT alias clocks during boot
 * @oclks: list of clocks to register
 *
 * Register alias or non-standard DT clock entries during boot. By
 * default, DT clocks are found based on their node name. If any
 * additional con-id / dev-id -> clock mapping is required, use this
 * function to list these.
 */
void __init ti_dt_clocks_register(struct ti_dt_clk oclks[])
{
	struct ti_dt_clk *c;
	struct device_node *node;
	struct clk *clk;
	struct of_phandle_args clkspec;

	for (c = oclks; c->node_name != NULL; c++) {
		node = of_find_node_by_name(NULL, c->node_name);
		clkspec.np = node;
		clk = of_clk_get_from_provider(&clkspec);

		if (!IS_ERR(clk)) {
			c->lk.clk = clk;
			clkdev_add(&c->lk);
		} else {
			pr_warn("failed to lookup clock node %s\n",
				c->node_name);
		}
	}
}

struct clk_init_item {
	struct device_node *node;
	struct clk_hw *hw;
	ti_of_clk_init_cb_t func;
	struct list_head link;
};

static LIST_HEAD(retry_list);

/**
 * ti_clk_retry_init - retries a failed clock init at later phase
 * @node: device not for the clock
 * @hw: partially initialized clk_hw struct for the clock
 * @func: init function to be called for the clock
 *
 * Adds a failed clock init to the retry list. The retry list is parsed
 * once all the other clocks have been initialized.
 */
int __init ti_clk_retry_init(struct device_node *node, struct clk_hw *hw,
			      ti_of_clk_init_cb_t func)
{
	struct clk_init_item *retry;

	pr_debug("%s: adding to retry list...\n", node->name);
	retry = kzalloc(sizeof(*retry), GFP_KERNEL);
	if (!retry)
		return -ENOMEM;

	retry->node = node;
	retry->func = func;
	retry->hw = hw;
	list_add(&retry->link, &retry_list);

	return 0;
}

/**
 * ti_clk_get_reg_addr - get register address for a clock register
 * @node: device node for the clock
 * @index: register index from the clock node
 *
 * Builds clock register address from device tree information. This
 * is a struct of type clk_omap_reg.
 */
void __iomem *ti_clk_get_reg_addr(struct device_node *node, int index)
{
	struct clk_omap_reg *reg;
	u32 val;
	u32 tmp;
	int i;

	reg = (struct clk_omap_reg *)&tmp;

	for (i = 0; i < CLK_MAX_MEMMAPS; i++) {
		if (clocks_node_ptr[i] == node->parent)
			break;
	}

	if (i == CLK_MAX_MEMMAPS) {
		pr_err("clk-provider not found for %s!\n", node->name);
		return NULL;
	}

	reg->index = i;

	if (of_property_read_u32_index(node, "reg", index, &val)) {
		pr_err("%s must have reg[%d]!\n", node->name, index);
		return NULL;
	}

	reg->offset = val;

	return (void __iomem *)tmp;
}

/**
 * ti_dt_clk_init_provider - init master clock provider
 * @parent: master node
 * @index: internal index for clk_reg_ops
 *
 * Initializes a master clock IP block. This basically sets up the
 * mapping from clocks node to the memory map index. All the clocks
 * are then initialized through the common of_clk_init call, and the
 * clocks will access their memory maps based on the node layout.
 */
void ti_dt_clk_init_provider(struct device_node *parent, int index)
{
	struct device_node *clocks;

	/* get clocks for this parent */
	clocks = of_get_child_by_name(parent, "clocks");
	if (!clocks) {
		pr_err("%s missing 'clocks' child node.\n", parent->name);
		return;
	}

	/* add clocks node info */
	clocks_node_ptr[index] = clocks;
}

/**
 * ti_dt_clk_init_retry_clks - init clocks from the retry list
 *
 * Initializes any clocks that have failed to initialize before,
 * reasons being missing parent node(s) during earlier init. This
 * typically happens only for DPLLs which need to have both of their
 * parent clocks ready during init.
 */
void ti_dt_clk_init_retry_clks(void)
{
	struct clk_init_item *retry;
	struct clk_init_item *tmp;
	int retries = 5;

	while (!list_empty(&retry_list) && retries) {
		list_for_each_entry_safe(retry, tmp, &retry_list, link) {
			pr_debug("retry-init: %s\n", retry->node->name);
			retry->func(retry->hw, retry->node);
			list_del(&retry->link);
			kfree(retry);
		}
		retries--;
	}
}

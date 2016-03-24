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

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/clkdev.h>
#include <linux/clk/ti.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/list.h>
#include <linux/regmap.h>
#include <linux/bootmem.h>

#include "clock.h"

#undef pr_fmt
#define pr_fmt(fmt) "%s: " fmt, __func__

struct ti_clk_ll_ops *ti_clk_ll_ops;
static struct device_node *clocks_node_ptr[CLK_MAX_MEMMAPS];

static struct ti_clk_features ti_clk_features;

struct clk_iomap {
	struct regmap *regmap;
	void __iomem *mem;
};

static struct clk_iomap *clk_memmaps[CLK_MAX_MEMMAPS];

static void clk_memmap_writel(u32 val, void __iomem *reg)
{
	struct clk_omap_reg *r = (struct clk_omap_reg *)&reg;
	struct clk_iomap *io = clk_memmaps[r->index];

	if (io->regmap)
		regmap_write(io->regmap, r->offset, val);
	else
		writel_relaxed(val, io->mem + r->offset);
}

static u32 clk_memmap_readl(void __iomem *reg)
{
	u32 val;
	struct clk_omap_reg *r = (struct clk_omap_reg *)&reg;
	struct clk_iomap *io = clk_memmaps[r->index];

	if (io->regmap)
		regmap_read(io->regmap, r->offset, &val);
	else
		val = readl_relaxed(io->mem + r->offset);

	return val;
}

/**
 * ti_clk_setup_ll_ops - setup low level clock operations
 * @ops: low level clock ops descriptor
 *
 * Sets up low level clock operations for TI clock driver. This is used
 * to provide various callbacks for the clock driver towards platform
 * specific code. Returns 0 on success, -EBUSY if ll_ops have been
 * registered already.
 */
int ti_clk_setup_ll_ops(struct ti_clk_ll_ops *ops)
{
	if (ti_clk_ll_ops) {
		pr_err("Attempt to register ll_ops multiple times.\n");
		return -EBUSY;
	}

	ti_clk_ll_ops = ops;
	ops->clk_readl = clk_memmap_readl;
	ops->clk_writel = clk_memmap_writel;

	return 0;
}

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
 * is a struct of type clk_omap_reg. Returns a pointer to the register
 * address, or a pointer error value in failure.
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
		return IOMEM_ERR_PTR(-ENOENT);
	}

	reg->index = i;

	if (of_property_read_u32_index(node, "reg", index, &val)) {
		pr_err("%s must have reg[%d]!\n", node->name, index);
		return IOMEM_ERR_PTR(-EINVAL);
	}

	reg->offset = val;

	return (__force void __iomem *)tmp;
}

/**
 * omap2_clk_provider_init - init master clock provider
 * @parent: master node
 * @index: internal index for clk_reg_ops
 * @syscon: syscon regmap pointer for accessing clock registers
 * @mem: iomem pointer for the clock provider memory area, only used if
 *       syscon is not provided
 *
 * Initializes a master clock IP block. This basically sets up the
 * mapping from clocks node to the memory map index. All the clocks
 * are then initialized through the common of_clk_init call, and the
 * clocks will access their memory maps based on the node layout.
 * Returns 0 in success.
 */
int __init omap2_clk_provider_init(struct device_node *parent, int index,
				   struct regmap *syscon, void __iomem *mem)
{
	struct device_node *clocks;
	struct clk_iomap *io;

	/* get clocks for this parent */
	clocks = of_get_child_by_name(parent, "clocks");
	if (!clocks) {
		pr_err("%s missing 'clocks' child node.\n", parent->name);
		return -EINVAL;
	}

	/* add clocks node info */
	clocks_node_ptr[index] = clocks;

	io = kzalloc(sizeof(*io), GFP_KERNEL);
	if (!io)
		return -ENOMEM;

	io->regmap = syscon;
	io->mem = mem;

	clk_memmaps[index] = io;

	return 0;
}

/**
 * omap2_clk_legacy_provider_init - initialize a legacy clock provider
 * @index: index for the clock provider
 * @mem: iomem pointer for the clock provider memory area
 *
 * Initializes a legacy clock provider memory mapping.
 */
void __init omap2_clk_legacy_provider_init(int index, void __iomem *mem)
{
	struct clk_iomap *io;

	io = memblock_virt_alloc(sizeof(*io), 0);

	io->mem = mem;

	clk_memmaps[index] = io;
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

#if defined(CONFIG_ARCH_OMAP3) && defined(CONFIG_ATAGS)
void __init ti_clk_patch_legacy_clks(struct ti_clk **patch)
{
	while (*patch) {
		memcpy((*patch)->patch, *patch, sizeof(**patch));
		patch++;
	}
}

struct clk __init *ti_clk_register_clk(struct ti_clk *setup)
{
	struct clk *clk;
	struct ti_clk_fixed *fixed;
	struct ti_clk_fixed_factor *fixed_factor;
	struct clk_hw *clk_hw;

	if (setup->clk)
		return setup->clk;

	switch (setup->type) {
	case TI_CLK_FIXED:
		fixed = setup->data;

		clk = clk_register_fixed_rate(NULL, setup->name, NULL, 0,
					      fixed->frequency);
		break;
	case TI_CLK_MUX:
		clk = ti_clk_register_mux(setup);
		break;
	case TI_CLK_DIVIDER:
		clk = ti_clk_register_divider(setup);
		break;
	case TI_CLK_COMPOSITE:
		clk = ti_clk_register_composite(setup);
		break;
	case TI_CLK_FIXED_FACTOR:
		fixed_factor = setup->data;

		clk = clk_register_fixed_factor(NULL, setup->name,
						fixed_factor->parent,
						0, fixed_factor->mult,
						fixed_factor->div);
		break;
	case TI_CLK_GATE:
		clk = ti_clk_register_gate(setup);
		break;
	case TI_CLK_DPLL:
		clk = ti_clk_register_dpll(setup);
		break;
	default:
		pr_err("bad type for %s!\n", setup->name);
		clk = ERR_PTR(-EINVAL);
	}

	if (!IS_ERR(clk)) {
		setup->clk = clk;
		if (setup->clkdm_name) {
			clk_hw = __clk_get_hw(clk);
			if (clk_hw_get_flags(clk_hw) & CLK_IS_BASIC) {
				pr_warn("can't setup clkdm for basic clk %s\n",
					setup->name);
			} else {
				to_clk_hw_omap(clk_hw)->clkdm_name =
					setup->clkdm_name;
				omap2_init_clk_clkdm(clk_hw);
			}
		}
	}

	return clk;
}

int __init ti_clk_register_legacy_clks(struct ti_clk_alias *clks)
{
	struct clk *clk;
	bool retry;
	struct ti_clk_alias *retry_clk;
	struct ti_clk_alias *tmp;

	while (clks->clk) {
		clk = ti_clk_register_clk(clks->clk);
		if (IS_ERR(clk)) {
			if (PTR_ERR(clk) == -EAGAIN) {
				list_add(&clks->link, &retry_list);
			} else {
				pr_err("register for %s failed: %ld\n",
				       clks->clk->name, PTR_ERR(clk));
				return PTR_ERR(clk);
			}
		} else {
			clks->lk.clk = clk;
			clkdev_add(&clks->lk);
		}
		clks++;
	}

	retry = true;

	while (!list_empty(&retry_list) && retry) {
		retry = false;
		list_for_each_entry_safe(retry_clk, tmp, &retry_list, link) {
			pr_debug("retry-init: %s\n", retry_clk->clk->name);
			clk = ti_clk_register_clk(retry_clk->clk);
			if (IS_ERR(clk)) {
				if (PTR_ERR(clk) == -EAGAIN) {
					continue;
				} else {
					pr_err("register for %s failed: %ld\n",
					       retry_clk->clk->name,
					       PTR_ERR(clk));
					return PTR_ERR(clk);
				}
			} else {
				retry = true;
				retry_clk->lk.clk = clk;
				clkdev_add(&retry_clk->lk);
				list_del(&retry_clk->link);
			}
		}
	}

	return 0;
}
#endif

/**
 * ti_clk_setup_features - setup clock features flags
 * @features: features definition to use
 *
 * Initializes the clock driver features flags based on platform
 * provided data. No return value.
 */
void __init ti_clk_setup_features(struct ti_clk_features *features)
{
	memcpy(&ti_clk_features, features, sizeof(*features));
}

/**
 * ti_clk_get_features - get clock driver features flags
 *
 * Get TI clock driver features description. Returns a pointer
 * to the current feature setup.
 */
const struct ti_clk_features *ti_clk_get_features(void)
{
	return &ti_clk_features;
}

/**
 * omap2_clk_enable_init_clocks - prepare & enable a list of clocks
 * @clk_names: ptr to an array of strings of clock names to enable
 * @num_clocks: number of clock names in @clk_names
 *
 * Prepare and enable a list of clocks, named by @clk_names.  No
 * return value. XXX Deprecated; only needed until these clocks are
 * properly claimed and enabled by the drivers or core code that uses
 * them.  XXX What code disables & calls clk_put on these clocks?
 */
void omap2_clk_enable_init_clocks(const char **clk_names, u8 num_clocks)
{
	struct clk *init_clk;
	int i;

	for (i = 0; i < num_clocks; i++) {
		init_clk = clk_get(NULL, clk_names[i]);
		if (WARN(IS_ERR(init_clk), "could not find init clock %s\n",
			 clk_names[i]))
			continue;
		clk_prepare_enable(init_clk);
	}
}

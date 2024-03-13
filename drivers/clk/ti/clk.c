// SPDX-License-Identifier: GPL-2.0-only
/*
 * TI clock support
 *
 * Copyright (C) 2013 Texas Instruments, Inc.
 *
 * Tero Kristo <t-kristo@ti.com>
 */

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/clkdev.h>
#include <linux/clk/ti.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/list.h>
#include <linux/regmap.h>
#include <linux/memblock.h>
#include <linux/device.h>

#include "clock.h"

#undef pr_fmt
#define pr_fmt(fmt) "%s: " fmt, __func__

static LIST_HEAD(clk_hw_omap_clocks);
struct ti_clk_ll_ops *ti_clk_ll_ops;
static struct device_node *clocks_node_ptr[CLK_MAX_MEMMAPS];

struct ti_clk_features ti_clk_features;

struct clk_iomap {
	struct regmap *regmap;
	void __iomem *mem;
};

static struct clk_iomap *clk_memmaps[CLK_MAX_MEMMAPS];

static void clk_memmap_writel(u32 val, const struct clk_omap_reg *reg)
{
	struct clk_iomap *io = clk_memmaps[reg->index];

	if (reg->ptr)
		writel_relaxed(val, reg->ptr);
	else if (io->regmap)
		regmap_write(io->regmap, reg->offset, val);
	else
		writel_relaxed(val, io->mem + reg->offset);
}

static void _clk_rmw(u32 val, u32 mask, void __iomem *ptr)
{
	u32 v;

	v = readl_relaxed(ptr);
	v &= ~mask;
	v |= val;
	writel_relaxed(v, ptr);
}

static void clk_memmap_rmw(u32 val, u32 mask, const struct clk_omap_reg *reg)
{
	struct clk_iomap *io = clk_memmaps[reg->index];

	if (reg->ptr) {
		_clk_rmw(val, mask, reg->ptr);
	} else if (io->regmap) {
		regmap_update_bits(io->regmap, reg->offset, mask, val);
	} else {
		_clk_rmw(val, mask, io->mem + reg->offset);
	}
}

static u32 clk_memmap_readl(const struct clk_omap_reg *reg)
{
	u32 val;
	struct clk_iomap *io = clk_memmaps[reg->index];

	if (reg->ptr)
		val = readl_relaxed(reg->ptr);
	else if (io->regmap)
		regmap_read(io->regmap, reg->offset, &val);
	else
		val = readl_relaxed(io->mem + reg->offset);

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
	ops->clk_rmw = clk_memmap_rmw;

	return 0;
}

/*
 * Eventually we could standardize to using '_' for clk-*.c files to follow the
 * TRM naming and leave out the tmp name here.
 */
static struct device_node *ti_find_clock_provider(struct device_node *from,
						  const char *name)
{
	struct device_node *np;
	bool found = false;
	const char *n;
	char *tmp;

	tmp = kstrdup(name, GFP_KERNEL);
	if (!tmp)
		return NULL;
	strreplace(tmp, '-', '_');

	/* Node named "clock" with "clock-output-names" */
	for_each_of_allnodes_from(from, np) {
		if (of_property_read_string_index(np, "clock-output-names",
						  0, &n))
			continue;

		if (!strncmp(n, tmp, strlen(tmp))) {
			of_node_get(np);
			found = true;
			break;
		}
	}
	kfree(tmp);

	if (found) {
		of_node_put(from);
		return np;
	}

	/* Fall back to using old node name base provider name */
	return of_find_node_by_name(from, name);
}

/**
 * ti_dt_clocks_register - register DT alias clocks during boot
 * @oclks: list of clocks to register
 *
 * Register alias or non-standard DT clock entries during boot. By
 * default, DT clocks are found based on their clock-output-names
 * property, or the clock node name for legacy cases. If any
 * additional con-id / dev-id -> clock mapping is required, use this
 * function to list these.
 */
void __init ti_dt_clocks_register(struct ti_dt_clk oclks[])
{
	struct ti_dt_clk *c;
	struct device_node *node, *parent, *child;
	struct clk *clk;
	struct of_phandle_args clkspec;
	char buf[64];
	char *ptr;
	char *tags[2];
	int i;
	int num_args;
	int ret;
	static bool clkctrl_nodes_missing;
	static bool has_clkctrl_data;
	static bool compat_mode;

	compat_mode = ti_clk_get_features()->flags & TI_CLK_CLKCTRL_COMPAT;

	for (c = oclks; c->node_name != NULL; c++) {
		strcpy(buf, c->node_name);
		ptr = buf;
		for (i = 0; i < 2; i++)
			tags[i] = NULL;
		num_args = 0;
		while (*ptr) {
			if (*ptr == ':') {
				if (num_args >= 2) {
					pr_warn("Bad number of tags on %s\n",
						c->node_name);
					return;
				}
				tags[num_args++] = ptr + 1;
				*ptr = 0;
			}
			ptr++;
		}

		if (num_args && clkctrl_nodes_missing)
			continue;

		node = ti_find_clock_provider(NULL, buf);
		if (num_args && compat_mode) {
			parent = node;
			child = of_get_child_by_name(parent, "clock");
			if (!child)
				child = of_get_child_by_name(parent, "clk");
			if (child) {
				of_node_put(parent);
				node = child;
			}
		}

		clkspec.np = node;
		clkspec.args_count = num_args;
		for (i = 0; i < num_args; i++) {
			ret = kstrtoint(tags[i], i ? 10 : 16, clkspec.args + i);
			if (ret) {
				pr_warn("Bad tag in %s at %d: %s\n",
					c->node_name, i, tags[i]);
				of_node_put(node);
				return;
			}
		}
		clk = of_clk_get_from_provider(&clkspec);
		of_node_put(node);
		if (!IS_ERR(clk)) {
			c->lk.clk = clk;
			clkdev_add(&c->lk);
		} else {
			if (num_args && !has_clkctrl_data) {
				struct device_node *np;

				np = of_find_compatible_node(NULL, NULL,
							     "ti,clkctrl");
				if (np) {
					has_clkctrl_data = true;
					of_node_put(np);
				} else {
					clkctrl_nodes_missing = true;

					pr_warn("missing clkctrl nodes, please update your dts.\n");
					continue;
				}
			}

			pr_warn("failed to lookup clock node %s, ret=%ld\n",
				c->node_name, PTR_ERR(clk));
		}
	}
}

struct clk_init_item {
	struct device_node *node;
	void *user;
	ti_of_clk_init_cb_t func;
	struct list_head link;
};

static LIST_HEAD(retry_list);

/**
 * ti_clk_retry_init - retries a failed clock init at later phase
 * @node: device not for the clock
 * @user: user data pointer
 * @func: init function to be called for the clock
 *
 * Adds a failed clock init to the retry list. The retry list is parsed
 * once all the other clocks have been initialized.
 */
int __init ti_clk_retry_init(struct device_node *node, void *user,
			     ti_of_clk_init_cb_t func)
{
	struct clk_init_item *retry;

	pr_debug("%pOFn: adding to retry list...\n", node);
	retry = kzalloc(sizeof(*retry), GFP_KERNEL);
	if (!retry)
		return -ENOMEM;

	retry->node = node;
	retry->func = func;
	retry->user = user;
	list_add(&retry->link, &retry_list);

	return 0;
}

/**
 * ti_clk_get_reg_addr - get register address for a clock register
 * @node: device node for the clock
 * @index: register index from the clock node
 * @reg: pointer to target register struct
 *
 * Builds clock register address from device tree information, and returns
 * the data via the provided output pointer @reg. Returns 0 on success,
 * negative error value on failure.
 */
int ti_clk_get_reg_addr(struct device_node *node, int index,
			struct clk_omap_reg *reg)
{
	u32 val;
	int i;

	for (i = 0; i < CLK_MAX_MEMMAPS; i++) {
		if (clocks_node_ptr[i] == node->parent)
			break;
		if (clocks_node_ptr[i] == node->parent->parent)
			break;
	}

	if (i == CLK_MAX_MEMMAPS) {
		pr_err("clk-provider not found for %pOFn!\n", node);
		return -ENOENT;
	}

	reg->index = i;

	if (of_property_read_u32_index(node, "reg", index, &val)) {
		if (of_property_read_u32_index(node->parent, "reg",
					       index, &val)) {
			pr_err("%pOFn or parent must have reg[%d]!\n",
			       node, index);
			return -EINVAL;
		}
	}

	reg->offset = val;
	reg->ptr = NULL;

	return 0;
}

void ti_clk_latch(struct clk_omap_reg *reg, s8 shift)
{
	u32 latch;

	if (shift < 0)
		return;

	latch = 1 << shift;

	ti_clk_ll_ops->clk_rmw(latch, latch, reg);
	ti_clk_ll_ops->clk_rmw(0, latch, reg);
	ti_clk_ll_ops->clk_readl(reg); /* OCP barrier */
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
		pr_err("%pOFn missing 'clocks' child node.\n", parent);
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

	io = memblock_alloc(sizeof(*io), SMP_CACHE_BYTES);
	if (!io)
		panic("%s: Failed to allocate %zu bytes\n", __func__,
		      sizeof(*io));

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
			pr_debug("retry-init: %pOFn\n", retry->node);
			retry->func(retry->user, retry->node);
			list_del(&retry->link);
			kfree(retry);
		}
		retries--;
	}
}

static const struct of_device_id simple_clk_match_table[] __initconst = {
	{ .compatible = "fixed-clock" },
	{ .compatible = "fixed-factor-clock" },
	{ }
};

/**
 * ti_dt_clk_name - init clock name from first output name or node name
 * @np: device node
 *
 * Use the first clock-output-name for the clock name if found. Fall back
 * to legacy naming based on node name.
 */
const char *ti_dt_clk_name(struct device_node *np)
{
	const char *name;

	if (!of_property_read_string_index(np, "clock-output-names", 0,
					   &name))
		return name;

	return np->name;
}

/**
 * ti_clk_add_aliases - setup clock aliases
 *
 * Sets up any missing clock aliases. No return value.
 */
void __init ti_clk_add_aliases(void)
{
	struct device_node *np;
	struct clk *clk;

	for_each_matching_node(np, simple_clk_match_table) {
		struct of_phandle_args clkspec;

		clkspec.np = np;
		clk = of_clk_get_from_provider(&clkspec);

		ti_clk_add_alias(clk, ti_dt_clk_name(np));
	}
}

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

/**
 * ti_clk_add_alias - add a clock alias for a TI clock
 * @clk: clock handle to create alias for
 * @con: connection ID for this clock
 *
 * Creates a clock alias for a TI clock. Allocates the clock lookup entry
 * and assigns the data to it. Returns 0 if successful, negative error
 * value otherwise.
 */
int ti_clk_add_alias(struct clk *clk, const char *con)
{
	struct clk_lookup *cl;

	if (!clk)
		return 0;

	if (IS_ERR(clk))
		return PTR_ERR(clk);

	cl = kzalloc(sizeof(*cl), GFP_KERNEL);
	if (!cl)
		return -ENOMEM;

	cl->con_id = con;
	cl->clk = clk;

	clkdev_add(cl);

	return 0;
}

/**
 * of_ti_clk_register - register a TI clock to the common clock framework
 * @node: device node for this clock
 * @hw: hardware clock handle
 * @con: connection ID for this clock
 *
 * Registers a TI clock to the common clock framework, and adds a clock
 * alias for it. Returns a handle to the registered clock if successful,
 * ERR_PTR value in failure.
 */
struct clk *of_ti_clk_register(struct device_node *node, struct clk_hw *hw,
			       const char *con)
{
	struct clk *clk;
	int ret;

	ret = of_clk_hw_register(node, hw);
	if (ret)
		return ERR_PTR(ret);

	clk = hw->clk;
	ret = ti_clk_add_alias(clk, con);
	if (ret) {
		clk_unregister(clk);
		return ERR_PTR(ret);
	}

	return clk;
}

/**
 * of_ti_clk_register_omap_hw - register a clk_hw_omap to the clock framework
 * @node: device node for this clock
 * @hw: hardware clock handle
 * @con: connection ID for this clock
 *
 * Registers a clk_hw_omap clock to the clock framewor, adds a clock alias
 * for it, and adds the list to the available clk_hw_omap type clocks.
 * Returns a handle to the registered clock if successful, ERR_PTR value
 * in failure.
 */
struct clk *of_ti_clk_register_omap_hw(struct device_node *node,
				       struct clk_hw *hw, const char *con)
{
	struct clk *clk;
	struct clk_hw_omap *oclk;

	clk = of_ti_clk_register(node, hw, con);
	if (IS_ERR(clk))
		return clk;

	oclk = to_clk_hw_omap(hw);

	list_add(&oclk->node, &clk_hw_omap_clocks);

	return clk;
}

/**
 * omap2_clk_for_each - call function for each registered clk_hw_omap
 * @fn: pointer to a callback function
 *
 * Call @fn for each registered clk_hw_omap, passing @hw to each
 * function.  @fn must return 0 for success or any other value for
 * failure.  If @fn returns non-zero, the iteration across clocks
 * will stop and the non-zero return value will be passed to the
 * caller of omap2_clk_for_each().
 */
int omap2_clk_for_each(int (*fn)(struct clk_hw_omap *hw))
{
	int ret;
	struct clk_hw_omap *hw;

	list_for_each_entry(hw, &clk_hw_omap_clocks, node) {
		ret = (*fn)(hw);
		if (ret)
			break;
	}

	return ret;
}

/**
 * omap2_clk_is_hw_omap - check if the provided clk_hw is OMAP clock
 * @hw: clk_hw to check if it is an omap clock or not
 *
 * Checks if the provided clk_hw is OMAP clock or not. Returns true if
 * it is, false otherwise.
 */
bool omap2_clk_is_hw_omap(struct clk_hw *hw)
{
	struct clk_hw_omap *oclk;

	list_for_each_entry(oclk, &clk_hw_omap_clocks, node) {
		if (&oclk->hw == hw)
			return true;
	}

	return false;
}

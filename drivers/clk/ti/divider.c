// SPDX-License-Identifier: GPL-2.0-only
/*
 * TI Divider Clock
 *
 * Copyright (C) 2013 Texas Instruments, Inc.
 *
 * Tero Kristo <t-kristo@ti.com>
 */

#include <linux/clk-provider.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/clk/ti.h>
#include "clock.h"

#undef pr_fmt
#define pr_fmt(fmt) "%s: " fmt, __func__

static unsigned int _get_table_div(const struct clk_div_table *table,
				   unsigned int val)
{
	const struct clk_div_table *clkt;

	for (clkt = table; clkt->div; clkt++)
		if (clkt->val == val)
			return clkt->div;
	return 0;
}

static void _setup_mask(struct clk_omap_divider *divider)
{
	u16 mask;
	u32 max_val;
	const struct clk_div_table *clkt;

	if (divider->table) {
		max_val = 0;

		for (clkt = divider->table; clkt->div; clkt++)
			if (clkt->val > max_val)
				max_val = clkt->val;
	} else {
		max_val = divider->max;

		if (!(divider->flags & CLK_DIVIDER_ONE_BASED) &&
		    !(divider->flags & CLK_DIVIDER_POWER_OF_TWO))
			max_val--;
	}

	if (divider->flags & CLK_DIVIDER_POWER_OF_TWO)
		mask = fls(max_val) - 1;
	else
		mask = max_val;

	divider->mask = (1 << fls(mask)) - 1;
}

static unsigned int _get_div(struct clk_omap_divider *divider, unsigned int val)
{
	if (divider->flags & CLK_DIVIDER_ONE_BASED)
		return val;
	if (divider->flags & CLK_DIVIDER_POWER_OF_TWO)
		return 1 << val;
	if (divider->table)
		return _get_table_div(divider->table, val);
	return val + 1;
}

static unsigned int _get_table_val(const struct clk_div_table *table,
				   unsigned int div)
{
	const struct clk_div_table *clkt;

	for (clkt = table; clkt->div; clkt++)
		if (clkt->div == div)
			return clkt->val;
	return 0;
}

static unsigned int _get_val(struct clk_omap_divider *divider, u8 div)
{
	if (divider->flags & CLK_DIVIDER_ONE_BASED)
		return div;
	if (divider->flags & CLK_DIVIDER_POWER_OF_TWO)
		return __ffs(div);
	if (divider->table)
		return  _get_table_val(divider->table, div);
	return div - 1;
}

static unsigned long ti_clk_divider_recalc_rate(struct clk_hw *hw,
						unsigned long parent_rate)
{
	struct clk_omap_divider *divider = to_clk_omap_divider(hw);
	unsigned int div, val;

	val = ti_clk_ll_ops->clk_readl(&divider->reg) >> divider->shift;
	val &= divider->mask;

	div = _get_div(divider, val);
	if (!div) {
		WARN(!(divider->flags & CLK_DIVIDER_ALLOW_ZERO),
		     "%s: Zero divisor and CLK_DIVIDER_ALLOW_ZERO not set\n",
		     clk_hw_get_name(hw));
		return parent_rate;
	}

	return DIV_ROUND_UP(parent_rate, div);
}

/*
 * The reverse of DIV_ROUND_UP: The maximum number which
 * divided by m is r
 */
#define MULT_ROUND_UP(r, m) ((r) * (m) + (m) - 1)

static bool _is_valid_table_div(const struct clk_div_table *table,
				unsigned int div)
{
	const struct clk_div_table *clkt;

	for (clkt = table; clkt->div; clkt++)
		if (clkt->div == div)
			return true;
	return false;
}

static bool _is_valid_div(struct clk_omap_divider *divider, unsigned int div)
{
	if (divider->flags & CLK_DIVIDER_POWER_OF_TWO)
		return is_power_of_2(div);
	if (divider->table)
		return _is_valid_table_div(divider->table, div);
	return true;
}

static int _div_round_up(const struct clk_div_table *table,
			 unsigned long parent_rate, unsigned long rate)
{
	const struct clk_div_table *clkt;
	int up = INT_MAX;
	int div = DIV_ROUND_UP_ULL((u64)parent_rate, rate);

	for (clkt = table; clkt->div; clkt++) {
		if (clkt->div == div)
			return clkt->div;
		else if (clkt->div < div)
			continue;

		if ((clkt->div - div) < (up - div))
			up = clkt->div;
	}

	return up;
}

static int _div_round(const struct clk_div_table *table,
		      unsigned long parent_rate, unsigned long rate)
{
	if (!table)
		return DIV_ROUND_UP(parent_rate, rate);

	return _div_round_up(table, parent_rate, rate);
}

static int ti_clk_divider_bestdiv(struct clk_hw *hw, unsigned long rate,
				  unsigned long *best_parent_rate)
{
	struct clk_omap_divider *divider = to_clk_omap_divider(hw);
	int i, bestdiv = 0;
	unsigned long parent_rate, best = 0, now, maxdiv;
	unsigned long parent_rate_saved = *best_parent_rate;

	if (!rate)
		rate = 1;

	maxdiv = divider->max;

	if (!(clk_hw_get_flags(hw) & CLK_SET_RATE_PARENT)) {
		parent_rate = *best_parent_rate;
		bestdiv = _div_round(divider->table, parent_rate, rate);
		bestdiv = bestdiv == 0 ? 1 : bestdiv;
		bestdiv = bestdiv > maxdiv ? maxdiv : bestdiv;
		return bestdiv;
	}

	/*
	 * The maximum divider we can use without overflowing
	 * unsigned long in rate * i below
	 */
	maxdiv = min(ULONG_MAX / rate, maxdiv);

	for (i = 1; i <= maxdiv; i++) {
		if (!_is_valid_div(divider, i))
			continue;
		if (rate * i == parent_rate_saved) {
			/*
			 * It's the most ideal case if the requested rate can be
			 * divided from parent clock without needing to change
			 * parent rate, so return the divider immediately.
			 */
			*best_parent_rate = parent_rate_saved;
			return i;
		}
		parent_rate = clk_hw_round_rate(clk_hw_get_parent(hw),
				MULT_ROUND_UP(rate, i));
		now = DIV_ROUND_UP(parent_rate, i);
		if (now <= rate && now > best) {
			bestdiv = i;
			best = now;
			*best_parent_rate = parent_rate;
		}
	}

	if (!bestdiv) {
		bestdiv = divider->max;
		*best_parent_rate =
			clk_hw_round_rate(clk_hw_get_parent(hw), 1);
	}

	return bestdiv;
}

static long ti_clk_divider_round_rate(struct clk_hw *hw, unsigned long rate,
				      unsigned long *prate)
{
	int div;
	div = ti_clk_divider_bestdiv(hw, rate, prate);

	return DIV_ROUND_UP(*prate, div);
}

static int ti_clk_divider_set_rate(struct clk_hw *hw, unsigned long rate,
				   unsigned long parent_rate)
{
	struct clk_omap_divider *divider;
	unsigned int div, value;
	u32 val;

	if (!hw || !rate)
		return -EINVAL;

	divider = to_clk_omap_divider(hw);

	div = DIV_ROUND_UP(parent_rate, rate);

	if (div > divider->max)
		div = divider->max;
	if (div < divider->min)
		div = divider->min;

	value = _get_val(divider, div);

	val = ti_clk_ll_ops->clk_readl(&divider->reg);
	val &= ~(divider->mask << divider->shift);
	val |= value << divider->shift;
	ti_clk_ll_ops->clk_writel(val, &divider->reg);

	ti_clk_latch(&divider->reg, divider->latch);

	return 0;
}

/**
 * clk_divider_save_context - Save the divider value
 * @hw: pointer  struct clk_hw
 *
 * Save the divider value
 */
static int clk_divider_save_context(struct clk_hw *hw)
{
	struct clk_omap_divider *divider = to_clk_omap_divider(hw);
	u32 val;

	val = ti_clk_ll_ops->clk_readl(&divider->reg) >> divider->shift;
	divider->context = val & divider->mask;

	return 0;
}

/**
 * clk_divider_restore_context - restore the saved the divider value
 * @hw: pointer  struct clk_hw
 *
 * Restore the saved the divider value
 */
static void clk_divider_restore_context(struct clk_hw *hw)
{
	struct clk_omap_divider *divider = to_clk_omap_divider(hw);
	u32 val;

	val = ti_clk_ll_ops->clk_readl(&divider->reg);
	val &= ~(divider->mask << divider->shift);
	val |= divider->context << divider->shift;
	ti_clk_ll_ops->clk_writel(val, &divider->reg);
}

const struct clk_ops ti_clk_divider_ops = {
	.recalc_rate = ti_clk_divider_recalc_rate,
	.round_rate = ti_clk_divider_round_rate,
	.set_rate = ti_clk_divider_set_rate,
	.save_context = clk_divider_save_context,
	.restore_context = clk_divider_restore_context,
};

static struct clk *_register_divider(struct device_node *node,
				     u32 flags,
				     struct clk_omap_divider *div)
{
	struct clk_init_data init;
	const char *parent_name;
	const char *name;

	parent_name = of_clk_get_parent_name(node, 0);

	name = ti_dt_clk_name(node);
	init.name = name;
	init.ops = &ti_clk_divider_ops;
	init.flags = flags;
	init.parent_names = (parent_name ? &parent_name : NULL);
	init.num_parents = (parent_name ? 1 : 0);

	div->hw.init = &init;

	/* register the clock */
	return of_ti_clk_register(node, &div->hw, name);
}

int ti_clk_parse_divider_data(int *div_table, int num_dividers, int max_div,
			      u8 flags, struct clk_omap_divider *divider)
{
	int valid_div = 0;
	int i;
	struct clk_div_table *tmp;
	u16 min_div = 0;

	if (!div_table) {
		divider->min = 1;
		divider->max = max_div;
		_setup_mask(divider);
		return 0;
	}

	i = 0;

	while (!num_dividers || i < num_dividers) {
		if (div_table[i] == -1)
			break;
		if (div_table[i])
			valid_div++;
		i++;
	}

	num_dividers = i;

	tmp = kcalloc(valid_div + 1, sizeof(*tmp), GFP_KERNEL);
	if (!tmp)
		return -ENOMEM;

	valid_div = 0;

	for (i = 0; i < num_dividers; i++)
		if (div_table[i] > 0) {
			tmp[valid_div].div = div_table[i];
			tmp[valid_div].val = i;
			valid_div++;
			if (div_table[i] > max_div)
				max_div = div_table[i];
			if (!min_div || div_table[i] < min_div)
				min_div = div_table[i];
		}

	divider->min = min_div;
	divider->max = max_div;
	divider->table = tmp;
	_setup_mask(divider);

	return 0;
}

static int __init ti_clk_get_div_table(struct device_node *node,
				       struct clk_omap_divider *div)
{
	struct clk_div_table *table;
	const __be32 *divspec;
	u32 val;
	u32 num_div;
	u32 valid_div;
	int i;

	divspec = of_get_property(node, "ti,dividers", &num_div);

	if (!divspec)
		return 0;

	num_div /= 4;

	valid_div = 0;

	/* Determine required size for divider table */
	for (i = 0; i < num_div; i++) {
		of_property_read_u32_index(node, "ti,dividers", i, &val);
		if (val)
			valid_div++;
	}

	if (!valid_div) {
		pr_err("no valid dividers for %pOFn table\n", node);
		return -EINVAL;
	}

	table = kcalloc(valid_div + 1, sizeof(*table), GFP_KERNEL);
	if (!table)
		return -ENOMEM;

	valid_div = 0;

	for (i = 0; i < num_div; i++) {
		of_property_read_u32_index(node, "ti,dividers", i, &val);
		if (val) {
			table[valid_div].div = val;
			table[valid_div].val = i;
			valid_div++;
		}
	}

	div->table = table;

	return 0;
}

static int _populate_divider_min_max(struct device_node *node,
				     struct clk_omap_divider *divider)
{
	u32 min_div = 0;
	u32 max_div = 0;
	u32 val;
	const struct clk_div_table *clkt;

	if (!divider->table) {
		/* Clk divider table not provided, determine min/max divs */
		if (of_property_read_u32(node, "ti,min-div", &min_div))
			min_div = 1;

		if (of_property_read_u32(node, "ti,max-div", &max_div)) {
			pr_err("no max-div for %pOFn!\n", node);
			return -EINVAL;
		}
	} else {

		for (clkt = divider->table; clkt->div; clkt++) {
			val = clkt->div;
			if (val > max_div)
				max_div = val;
			if (!min_div || val < min_div)
				min_div = val;
		}
	}

	divider->min = min_div;
	divider->max = max_div;
	_setup_mask(divider);

	return 0;
}

static int __init ti_clk_divider_populate(struct device_node *node,
					  struct clk_omap_divider *div,
					  u32 *flags)
{
	u32 val;
	int ret;

	ret = ti_clk_get_reg_addr(node, 0, &div->reg);
	if (ret)
		return ret;

	div->shift = div->reg.bit;

	if (!of_property_read_u32(node, "ti,latch-bit", &val))
		div->latch = val;
	else
		div->latch = -EINVAL;

	*flags = 0;
	div->flags = 0;

	if (of_property_read_bool(node, "ti,index-starts-at-one"))
		div->flags |= CLK_DIVIDER_ONE_BASED;

	if (of_property_read_bool(node, "ti,index-power-of-two"))
		div->flags |= CLK_DIVIDER_POWER_OF_TWO;

	if (of_property_read_bool(node, "ti,set-rate-parent"))
		*flags |= CLK_SET_RATE_PARENT;

	ret = ti_clk_get_div_table(node, div);
	if (ret)
		return ret;

	return _populate_divider_min_max(node, div);
}

/**
 * of_ti_divider_clk_setup - Setup function for simple div rate clock
 * @node: device node for this clock
 *
 * Sets up a basic divider clock.
 */
static void __init of_ti_divider_clk_setup(struct device_node *node)
{
	struct clk *clk;
	u32 flags = 0;
	struct clk_omap_divider *div;

	div = kzalloc(sizeof(*div), GFP_KERNEL);
	if (!div)
		return;

	if (ti_clk_divider_populate(node, div, &flags))
		goto cleanup;

	clk = _register_divider(node, flags, div);
	if (!IS_ERR(clk)) {
		of_clk_add_provider(node, of_clk_src_simple_get, clk);
		of_ti_clk_autoidle_setup(node);
		return;
	}

cleanup:
	kfree(div->table);
	kfree(div);
}
CLK_OF_DECLARE(divider_clk, "ti,divider-clock", of_ti_divider_clk_setup);

static void __init of_ti_composite_divider_clk_setup(struct device_node *node)
{
	struct clk_omap_divider *div;
	u32 tmp;

	div = kzalloc(sizeof(*div), GFP_KERNEL);
	if (!div)
		return;

	if (ti_clk_divider_populate(node, div, &tmp))
		goto cleanup;

	if (!ti_clk_add_component(node, &div->hw, CLK_COMPONENT_TYPE_DIVIDER))
		return;

cleanup:
	kfree(div->table);
	kfree(div);
}
CLK_OF_DECLARE(ti_composite_divider_clk, "ti,composite-divider-clock",
	       of_ti_composite_divider_clk_setup);

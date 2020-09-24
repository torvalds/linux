/*
 * r8a7790 Common Clock Framework support
 *
 * Copyright (C) 2013  Renesas Solutions Corp.
 *
 * Contact: Laurent Pinchart <laurent.pinchart@ideasonboard.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 */

#include <linux/clk-provider.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/notifier.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/pm.h>
#include <linux/slab.h>

#include "clk-div6.h"

#define CPG_DIV6_CKSTP		BIT(8)
#define CPG_DIV6_DIV(d)		((d) & 0x3f)
#define CPG_DIV6_DIV_MASK	0x3f

/**
 * struct div6_clock - CPG 6 bit divider clock
 * @hw: handle between common and hardware-specific interfaces
 * @reg: IO-remapped register
 * @div: divisor value (1-64)
 * @src_shift: Shift to access the register bits to select the parent clock
 * @src_width: Number of register bits to select the parent clock (may be 0)
 * @parents: Array to map from valid parent clocks indices to hardware indices
 * @nb: Notifier block to save/restore clock state for system resume
 */
struct div6_clock {
	struct clk_hw hw;
	void __iomem *reg;
	unsigned int div;
	u32 src_shift;
	u32 src_width;
	u8 *parents;
	struct notifier_block nb;
};

#define to_div6_clock(_hw) container_of(_hw, struct div6_clock, hw)

static int cpg_div6_clock_enable(struct clk_hw *hw)
{
	struct div6_clock *clock = to_div6_clock(hw);
	u32 val;

	val = (readl(clock->reg) & ~(CPG_DIV6_DIV_MASK | CPG_DIV6_CKSTP))
	    | CPG_DIV6_DIV(clock->div - 1);
	writel(val, clock->reg);

	return 0;
}

static void cpg_div6_clock_disable(struct clk_hw *hw)
{
	struct div6_clock *clock = to_div6_clock(hw);
	u32 val;

	val = readl(clock->reg);
	val |= CPG_DIV6_CKSTP;
	/*
	 * DIV6 clocks require the divisor field to be non-zero when stopping
	 * the clock. However, some clocks (e.g. ZB on sh73a0) fail to be
	 * re-enabled later if the divisor field is changed when stopping the
	 * clock
	 */
	if (!(val & CPG_DIV6_DIV_MASK))
		val |= CPG_DIV6_DIV_MASK;
	writel(val, clock->reg);
}

static int cpg_div6_clock_is_enabled(struct clk_hw *hw)
{
	struct div6_clock *clock = to_div6_clock(hw);

	return !(readl(clock->reg) & CPG_DIV6_CKSTP);
}

static unsigned long cpg_div6_clock_recalc_rate(struct clk_hw *hw,
						unsigned long parent_rate)
{
	struct div6_clock *clock = to_div6_clock(hw);

	return parent_rate / clock->div;
}

static unsigned int cpg_div6_clock_calc_div(unsigned long rate,
					    unsigned long parent_rate)
{
	unsigned int div;

	if (!rate)
		rate = 1;

	div = DIV_ROUND_CLOSEST(parent_rate, rate);
	return clamp_t(unsigned int, div, 1, 64);
}

static long cpg_div6_clock_round_rate(struct clk_hw *hw, unsigned long rate,
				      unsigned long *parent_rate)
{
	unsigned int div = cpg_div6_clock_calc_div(rate, *parent_rate);

	return *parent_rate / div;
}

static int cpg_div6_clock_set_rate(struct clk_hw *hw, unsigned long rate,
				   unsigned long parent_rate)
{
	struct div6_clock *clock = to_div6_clock(hw);
	unsigned int div = cpg_div6_clock_calc_div(rate, parent_rate);
	u32 val;

	clock->div = div;

	val = readl(clock->reg) & ~CPG_DIV6_DIV_MASK;
	/* Only program the new divisor if the clock isn't stopped. */
	if (!(val & CPG_DIV6_CKSTP))
		writel(val | CPG_DIV6_DIV(clock->div - 1), clock->reg);

	return 0;
}

static u8 cpg_div6_clock_get_parent(struct clk_hw *hw)
{
	struct div6_clock *clock = to_div6_clock(hw);
	unsigned int i;
	u8 hw_index;

	if (clock->src_width == 0)
		return 0;

	hw_index = (readl(clock->reg) >> clock->src_shift) &
		   (BIT(clock->src_width) - 1);
	for (i = 0; i < clk_hw_get_num_parents(hw); i++) {
		if (clock->parents[i] == hw_index)
			return i;
	}

	pr_err("%s: %s DIV6 clock set to invalid parent %u\n",
	       __func__, clk_hw_get_name(hw), hw_index);
	return 0;
}

static int cpg_div6_clock_set_parent(struct clk_hw *hw, u8 index)
{
	struct div6_clock *clock = to_div6_clock(hw);
	u8 hw_index;
	u32 mask;

	if (index >= clk_hw_get_num_parents(hw))
		return -EINVAL;

	mask = ~((BIT(clock->src_width) - 1) << clock->src_shift);
	hw_index = clock->parents[index];

	writel((readl(clock->reg) & mask) | (hw_index << clock->src_shift),
	       clock->reg);

	return 0;
}

static const struct clk_ops cpg_div6_clock_ops = {
	.enable = cpg_div6_clock_enable,
	.disable = cpg_div6_clock_disable,
	.is_enabled = cpg_div6_clock_is_enabled,
	.get_parent = cpg_div6_clock_get_parent,
	.set_parent = cpg_div6_clock_set_parent,
	.recalc_rate = cpg_div6_clock_recalc_rate,
	.round_rate = cpg_div6_clock_round_rate,
	.set_rate = cpg_div6_clock_set_rate,
};

static int cpg_div6_clock_notifier_call(struct notifier_block *nb,
					unsigned long action, void *data)
{
	struct div6_clock *clock = container_of(nb, struct div6_clock, nb);

	switch (action) {
	case PM_EVENT_RESUME:
		/*
		 * TODO: This does not yet support DIV6 clocks with multiple
		 * parents, as the parent selection bits are not restored.
		 * Fortunately so far such DIV6 clocks are found only on
		 * R/SH-Mobile SoCs, while the resume functionality is only
		 * needed on R-Car Gen3.
		 */
		if (__clk_get_enable_count(clock->hw.clk))
			cpg_div6_clock_enable(&clock->hw);
		else
			cpg_div6_clock_disable(&clock->hw);
		return NOTIFY_OK;
	}

	return NOTIFY_DONE;
}

/**
 * cpg_div6_register - Register a DIV6 clock
 * @name: Name of the DIV6 clock
 * @num_parents: Number of parent clocks of the DIV6 clock (1, 4, or 8)
 * @parent_names: Array containing the names of the parent clocks
 * @reg: Mapped register used to control the DIV6 clock
 * @notifiers: Optional notifier chain to save/restore state for system resume
 */
struct clk * __init cpg_div6_register(const char *name,
				      unsigned int num_parents,
				      const char **parent_names,
				      void __iomem *reg,
				      struct raw_notifier_head *notifiers)
{
	unsigned int valid_parents;
	struct clk_init_data init = {};
	struct div6_clock *clock;
	struct clk *clk;
	unsigned int i;

	clock = kzalloc(sizeof(*clock), GFP_KERNEL);
	if (!clock)
		return ERR_PTR(-ENOMEM);

	clock->parents = kmalloc_array(num_parents, sizeof(*clock->parents),
				       GFP_KERNEL);
	if (!clock->parents) {
		clk = ERR_PTR(-ENOMEM);
		goto free_clock;
	}

	clock->reg = reg;

	/*
	 * Read the divisor. Disabling the clock overwrites the divisor, so we
	 * need to cache its value for the enable operation.
	 */
	clock->div = (readl(clock->reg) & CPG_DIV6_DIV_MASK) + 1;

	switch (num_parents) {
	case 1:
		/* fixed parent clock */
		clock->src_shift = clock->src_width = 0;
		break;
	case 4:
		/* clock with EXSRC bits 6-7 */
		clock->src_shift = 6;
		clock->src_width = 2;
		break;
	case 8:
		/* VCLK with EXSRC bits 12-14 */
		clock->src_shift = 12;
		clock->src_width = 3;
		break;
	default:
		pr_err("%s: invalid number of parents for DIV6 clock %s\n",
		       __func__, name);
		clk = ERR_PTR(-EINVAL);
		goto free_parents;
	}

	/* Filter out invalid parents */
	for (i = 0, valid_parents = 0; i < num_parents; i++) {
		if (parent_names[i]) {
			parent_names[valid_parents] = parent_names[i];
			clock->parents[valid_parents] = i;
			valid_parents++;
		}
	}

	/* Register the clock. */
	init.name = name;
	init.ops = &cpg_div6_clock_ops;
	init.flags = CLK_IS_BASIC;
	init.parent_names = parent_names;
	init.num_parents = valid_parents;

	clock->hw.init = &init;

	clk = clk_register(NULL, &clock->hw);
	if (IS_ERR(clk))
		goto free_parents;

	if (notifiers) {
		clock->nb.notifier_call = cpg_div6_clock_notifier_call;
		raw_notifier_chain_register(notifiers, &clock->nb);
	}

	return clk;

free_parents:
	kfree(clock->parents);
free_clock:
	kfree(clock);
	return clk;
}

static void __init cpg_div6_clock_init(struct device_node *np)
{
	unsigned int num_parents;
	const char **parent_names;
	const char *clk_name = np->name;
	void __iomem *reg;
	struct clk *clk;
	unsigned int i;

	num_parents = of_clk_get_parent_count(np);
	if (num_parents < 1) {
		pr_err("%s: no parent found for %s DIV6 clock\n",
		       __func__, np->name);
		return;
	}

	parent_names = kmalloc_array(num_parents, sizeof(*parent_names),
				GFP_KERNEL);
	if (!parent_names)
		return;

	reg = of_iomap(np, 0);
	if (reg == NULL) {
		pr_err("%s: failed to map %s DIV6 clock register\n",
		       __func__, np->name);
		goto error;
	}

	/* Parse the DT properties. */
	of_property_read_string(np, "clock-output-names", &clk_name);

	for (i = 0; i < num_parents; i++)
		parent_names[i] = of_clk_get_parent_name(np, i);

	clk = cpg_div6_register(clk_name, num_parents, parent_names, reg, NULL);
	if (IS_ERR(clk)) {
		pr_err("%s: failed to register %s DIV6 clock (%ld)\n",
		       __func__, np->name, PTR_ERR(clk));
		goto error;
	}

	of_clk_add_provider(np, of_clk_src_simple_get, clk);

	kfree(parent_names);
	return;

error:
	if (reg)
		iounmap(reg);
	kfree(parent_names);
}
CLK_OF_DECLARE(cpg_div6_clk, "renesas,cpg-div6-clock", cpg_div6_clock_init);

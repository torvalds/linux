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
#include <linux/clkdev.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/of_address.h>

#define CPG_DIV6_CKSTP		BIT(8)
#define CPG_DIV6_DIV(d)		((d) & 0x3f)
#define CPG_DIV6_DIV_MASK	0x3f

/**
 * struct div6_clock - CPG 6 bit divider clock
 * @hw: handle between common and hardware-specific interfaces
 * @reg: IO-remapped register
 * @div: divisor value (1-64)
 */
struct div6_clock {
	struct clk_hw hw;
	void __iomem *reg;
	unsigned int div;
};

#define to_div6_clock(_hw) container_of(_hw, struct div6_clock, hw)

static int cpg_div6_clock_enable(struct clk_hw *hw)
{
	struct div6_clock *clock = to_div6_clock(hw);

	clk_writel(CPG_DIV6_DIV(clock->div - 1), clock->reg);

	return 0;
}

static void cpg_div6_clock_disable(struct clk_hw *hw)
{
	struct div6_clock *clock = to_div6_clock(hw);

	/* DIV6 clocks require the divisor field to be non-zero when stopping
	 * the clock.
	 */
	clk_writel(CPG_DIV6_CKSTP | CPG_DIV6_DIV(CPG_DIV6_DIV_MASK),
		   clock->reg);
}

static int cpg_div6_clock_is_enabled(struct clk_hw *hw)
{
	struct div6_clock *clock = to_div6_clock(hw);

	return !(clk_readl(clock->reg) & CPG_DIV6_CKSTP);
}

static unsigned long cpg_div6_clock_recalc_rate(struct clk_hw *hw,
						unsigned long parent_rate)
{
	struct div6_clock *clock = to_div6_clock(hw);
	unsigned int div = (clk_readl(clock->reg) & CPG_DIV6_DIV_MASK) + 1;

	return parent_rate / div;
}

static unsigned int cpg_div6_clock_calc_div(unsigned long rate,
					    unsigned long parent_rate)
{
	unsigned int div;

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

	clock->div = div;

	/* Only program the new divisor if the clock isn't stopped. */
	if (!(clk_readl(clock->reg) & CPG_DIV6_CKSTP))
		clk_writel(CPG_DIV6_DIV(clock->div - 1), clock->reg);

	return 0;
}

static const struct clk_ops cpg_div6_clock_ops = {
	.enable = cpg_div6_clock_enable,
	.disable = cpg_div6_clock_disable,
	.is_enabled = cpg_div6_clock_is_enabled,
	.recalc_rate = cpg_div6_clock_recalc_rate,
	.round_rate = cpg_div6_clock_round_rate,
	.set_rate = cpg_div6_clock_set_rate,
};

static void __init cpg_div6_clock_init(struct device_node *np)
{
	struct clk_init_data init;
	struct div6_clock *clock;
	const char *parent_name;
	const char *name;
	struct clk *clk;
	int ret;

	clock = kzalloc(sizeof(*clock), GFP_KERNEL);
	if (!clock) {
		pr_err("%s: failed to allocate %s DIV6 clock\n",
		       __func__, np->name);
		return;
	}

	/* Remap the clock register and read the divisor. Disabling the
	 * clock overwrites the divisor, so we need to cache its value for the
	 * enable operation.
	 */
	clock->reg = of_iomap(np, 0);
	if (clock->reg == NULL) {
		pr_err("%s: failed to map %s DIV6 clock register\n",
		       __func__, np->name);
		goto error;
	}

	clock->div = (clk_readl(clock->reg) & CPG_DIV6_DIV_MASK) + 1;

	/* Parse the DT properties. */
	ret = of_property_read_string(np, "clock-output-names", &name);
	if (ret < 0) {
		pr_err("%s: failed to get %s DIV6 clock output name\n",
		       __func__, np->name);
		goto error;
	}

	parent_name = of_clk_get_parent_name(np, 0);
	if (parent_name == NULL) {
		pr_err("%s: failed to get %s DIV6 clock parent name\n",
		       __func__, np->name);
		goto error;
	}

	/* Register the clock. */
	init.name = name;
	init.ops = &cpg_div6_clock_ops;
	init.flags = CLK_IS_BASIC;
	init.parent_names = &parent_name;
	init.num_parents = 1;

	clock->hw.init = &init;

	clk = clk_register(NULL, &clock->hw);
	if (IS_ERR(clk)) {
		pr_err("%s: failed to register %s DIV6 clock (%ld)\n",
		       __func__, np->name, PTR_ERR(clk));
		goto error;
	}

	of_clk_add_provider(np, of_clk_src_simple_get, clk);

	return;

error:
	if (clock->reg)
		iounmap(clock->reg);
	kfree(clock);
}
CLK_OF_DECLARE(cpg_div6_clk, "renesas,cpg-div6-clock", cpg_div6_clock_init);

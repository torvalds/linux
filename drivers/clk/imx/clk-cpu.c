/*
 * Copyright (c) 2014 Lucas Stach <l.stach@pengutronix.de>, Pengutronix
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/slab.h>

struct clk_cpu {
	struct clk_hw	hw;
	struct clk	*div;
	struct clk	*mux;
	struct clk	*pll;
	struct clk	*step;
};

static inline struct clk_cpu *to_clk_cpu(struct clk_hw *hw)
{
	return container_of(hw, struct clk_cpu, hw);
}

static unsigned long clk_cpu_recalc_rate(struct clk_hw *hw,
					 unsigned long parent_rate)
{
	struct clk_cpu *cpu = to_clk_cpu(hw);

	return clk_get_rate(cpu->div);
}

static long clk_cpu_round_rate(struct clk_hw *hw, unsigned long rate,
			       unsigned long *prate)
{
	struct clk_cpu *cpu = to_clk_cpu(hw);

	return clk_round_rate(cpu->pll, rate);
}

static int clk_cpu_set_rate(struct clk_hw *hw, unsigned long rate,
			    unsigned long parent_rate)
{
	struct clk_cpu *cpu = to_clk_cpu(hw);
	int ret;

	/* switch to PLL bypass clock */
	ret = clk_set_parent(cpu->mux, cpu->step);
	if (ret)
		return ret;

	/* reprogram PLL */
	ret = clk_set_rate(cpu->pll, rate);
	if (ret) {
		clk_set_parent(cpu->mux, cpu->pll);
		return ret;
	}
	/* switch back to PLL clock */
	clk_set_parent(cpu->mux, cpu->pll);

	/* Ensure the divider is what we expect */
	clk_set_rate(cpu->div, rate);

	return 0;
}

static const struct clk_ops clk_cpu_ops = {
	.recalc_rate	= clk_cpu_recalc_rate,
	.round_rate	= clk_cpu_round_rate,
	.set_rate	= clk_cpu_set_rate,
};

struct clk *imx_clk_cpu(const char *name, const char *parent_name,
		struct clk *div, struct clk *mux, struct clk *pll,
		struct clk *step)
{
	struct clk_cpu *cpu;
	struct clk *clk;
	struct clk_init_data init;

	cpu = kzalloc(sizeof(*cpu), GFP_KERNEL);
	if (!cpu)
		return ERR_PTR(-ENOMEM);

	cpu->div = div;
	cpu->mux = mux;
	cpu->pll = pll;
	cpu->step = step;

	init.name = name;
	init.ops = &clk_cpu_ops;
	init.flags = 0;
	init.parent_names = &parent_name;
	init.num_parents = 1;

	cpu->hw.init = &init;

	clk = clk_register(NULL, &cpu->hw);
	if (IS_ERR(clk))
		kfree(cpu);

	return clk;
}

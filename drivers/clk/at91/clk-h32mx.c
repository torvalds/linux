/*
 * clk-h32mx.c
 *
 *  Copyright (C) 2014 Atmel
 *
 * Alexandre Belloni <alexandre.belloni@free-electrons.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */

#include <linux/clk-provider.h>
#include <linux/clkdev.h>
#include <linux/clk/at91_pmc.h>
#include <linux/delay.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/sched.h>
#include <linux/wait.h>

#include "pmc.h"

#define H32MX_MAX_FREQ	90000000

struct clk_sama5d4_h32mx {
	struct clk_hw hw;
	struct at91_pmc *pmc;
};

#define to_clk_sama5d4_h32mx(hw) container_of(hw, struct clk_sama5d4_h32mx, hw)

static unsigned long clk_sama5d4_h32mx_recalc_rate(struct clk_hw *hw,
						 unsigned long parent_rate)
{
	struct clk_sama5d4_h32mx *h32mxclk = to_clk_sama5d4_h32mx(hw);

	if (pmc_read(h32mxclk->pmc, AT91_PMC_MCKR) & AT91_PMC_H32MXDIV)
		return parent_rate / 2;

	if (parent_rate > H32MX_MAX_FREQ)
		pr_warn("H32MX clock is too fast\n");
	return parent_rate;
}

static long clk_sama5d4_h32mx_round_rate(struct clk_hw *hw, unsigned long rate,
				       unsigned long *parent_rate)
{
	unsigned long div;

	if (rate > *parent_rate)
		return *parent_rate;
	div = *parent_rate / 2;
	if (rate < div)
		return div;

	if (rate - div < *parent_rate - rate)
		return div;

	return *parent_rate;
}

static int clk_sama5d4_h32mx_set_rate(struct clk_hw *hw, unsigned long rate,
				    unsigned long parent_rate)
{
	struct clk_sama5d4_h32mx *h32mxclk = to_clk_sama5d4_h32mx(hw);
	struct at91_pmc *pmc = h32mxclk->pmc;
	u32 tmp;

	if (parent_rate != rate && (parent_rate / 2) != rate)
		return -EINVAL;

	pmc_lock(pmc);
	tmp = pmc_read(pmc, AT91_PMC_MCKR) & ~AT91_PMC_H32MXDIV;
	if ((parent_rate / 2) == rate)
		tmp |= AT91_PMC_H32MXDIV;
	pmc_write(pmc, AT91_PMC_MCKR, tmp);
	pmc_unlock(pmc);

	return 0;
}

static const struct clk_ops h32mx_ops = {
	.recalc_rate = clk_sama5d4_h32mx_recalc_rate,
	.round_rate = clk_sama5d4_h32mx_round_rate,
	.set_rate = clk_sama5d4_h32mx_set_rate,
};

void __init of_sama5d4_clk_h32mx_setup(struct device_node *np,
				     struct at91_pmc *pmc)
{
	struct clk_sama5d4_h32mx *h32mxclk;
	struct clk_init_data init;
	const char *parent_name;
	struct clk *clk;

	h32mxclk = kzalloc(sizeof(*h32mxclk), GFP_KERNEL);
	if (!h32mxclk)
		return;

	parent_name = of_clk_get_parent_name(np, 0);

	init.name = np->name;
	init.ops = &h32mx_ops;
	init.parent_names = parent_name ? &parent_name : NULL;
	init.num_parents = parent_name ? 1 : 0;
	init.flags = CLK_SET_RATE_GATE;

	h32mxclk->hw.init = &init;
	h32mxclk->pmc = pmc;

	clk = clk_register(NULL, &h32mxclk->hw);
	if (!clk)
		return;

	of_clk_add_provider(np, of_clk_src_simple_get, clk);
}

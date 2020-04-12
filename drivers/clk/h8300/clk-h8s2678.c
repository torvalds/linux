// SPDX-License-Identifier: GPL-2.0
/*
 * H8S2678 clock driver
 *
 * Copyright 2015 Yoshinori Sato <ysato@users.sourceforge.jp>
 */

#include <linux/clk-provider.h>
#include <linux/err.h>
#include <linux/device.h>
#include <linux/of_address.h>
#include <linux/slab.h>

static DEFINE_SPINLOCK(clklock);

#define MAX_FREQ 33333333
#define MIN_FREQ  8000000

struct pll_clock {
	struct clk_hw hw;
	void __iomem *sckcr;
	void __iomem *pllcr;
};

#define to_pll_clock(_hw) container_of(_hw, struct pll_clock, hw)

static unsigned long pll_recalc_rate(struct clk_hw *hw,
		unsigned long parent_rate)
{
	struct pll_clock *pll_clock = to_pll_clock(hw);
	int mul = 1 << (readb(pll_clock->pllcr) & 3);

	return parent_rate * mul;
}

static long pll_round_rate(struct clk_hw *hw, unsigned long rate,
				unsigned long *prate)
{
	int i, m = -1;
	long offset[3];

	if (rate > MAX_FREQ)
		rate = MAX_FREQ;
	if (rate < MIN_FREQ)
		rate = MIN_FREQ;

	for (i = 0; i < 3; i++)
		offset[i] = abs(rate - (*prate * (1 << i)));
	for (i = 0; i < 3; i++)
		if (m < 0)
			m = i;
		else
			m = (offset[i] < offset[m])?i:m;

	return *prate * (1 << m);
}

static int pll_set_rate(struct clk_hw *hw, unsigned long rate,
			unsigned long parent_rate)
{
	int pll;
	unsigned char val;
	unsigned long flags;
	struct pll_clock *pll_clock = to_pll_clock(hw);

	pll = ((rate / parent_rate) / 2) & 0x03;
	spin_lock_irqsave(&clklock, flags);
	val = readb(pll_clock->sckcr);
	val |= 0x08;
	writeb(val, pll_clock->sckcr);
	val = readb(pll_clock->pllcr);
	val &= ~0x03;
	val |= pll;
	writeb(val, pll_clock->pllcr);
	spin_unlock_irqrestore(&clklock, flags);
	return 0;
}

static const struct clk_ops pll_ops = {
	.recalc_rate = pll_recalc_rate,
	.round_rate = pll_round_rate,
	.set_rate = pll_set_rate,
};

static void __init h8s2678_pll_clk_setup(struct device_node *node)
{
	unsigned int num_parents;
	const char *clk_name = node->name;
	const char *parent_name;
	struct pll_clock *pll_clock;
	struct clk_init_data init = {};
	int ret;

	num_parents = of_clk_get_parent_count(node);
	if (!num_parents) {
		pr_err("%s: no parent found\n", clk_name);
		return;
	}


	pll_clock = kzalloc(sizeof(*pll_clock), GFP_KERNEL);
	if (!pll_clock)
		return;

	pll_clock->sckcr = of_iomap(node, 0);
	if (pll_clock->sckcr == NULL) {
		pr_err("%s: failed to map divide register\n", clk_name);
		goto free_clock;
	}

	pll_clock->pllcr = of_iomap(node, 1);
	if (pll_clock->pllcr == NULL) {
		pr_err("%s: failed to map multiply register\n", clk_name);
		goto unmap_sckcr;
	}

	parent_name = of_clk_get_parent_name(node, 0);
	init.name = clk_name;
	init.ops = &pll_ops;
	init.flags = CLK_IS_BASIC;
	init.parent_names = &parent_name;
	init.num_parents = 1;
	pll_clock->hw.init = &init;

	ret = clk_hw_register(NULL, &pll_clock->hw);
	if (ret) {
		pr_err("%s: failed to register %s div clock (%d)\n",
		       __func__, clk_name, ret);
		goto unmap_pllcr;
	}

	of_clk_add_hw_provider(node, of_clk_hw_simple_get, &pll_clock->hw);
	return;

unmap_pllcr:
	iounmap(pll_clock->pllcr);
unmap_sckcr:
	iounmap(pll_clock->sckcr);
free_clock:
	kfree(pll_clock);
}

CLK_OF_DECLARE(h8s2678_div_clk, "renesas,h8s2678-pll-clock",
	       h8s2678_pll_clk_setup);

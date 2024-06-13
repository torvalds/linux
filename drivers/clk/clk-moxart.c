// SPDX-License-Identifier: GPL-2.0-only
/*
 * MOXA ART SoCs clock driver.
 *
 * Copyright (C) 2013 Jonas Jensen
 *
 * Jonas Jensen <jonas.jensen@gmail.com>
 */

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/io.h>
#include <linux/of_address.h>
#include <linux/clkdev.h>

static void __init moxart_of_pll_clk_init(struct device_node *node)
{
	void __iomem *base;
	struct clk_hw *hw;
	struct clk *ref_clk;
	unsigned int mul;
	const char *name = node->name;
	const char *parent_name;

	of_property_read_string(node, "clock-output-names", &name);
	parent_name = of_clk_get_parent_name(node, 0);

	base = of_iomap(node, 0);
	if (!base) {
		pr_err("%pOF: of_iomap failed\n", node);
		return;
	}

	mul = readl(base + 0x30) >> 3 & 0x3f;
	iounmap(base);

	ref_clk = of_clk_get(node, 0);
	if (IS_ERR(ref_clk)) {
		pr_err("%pOF: of_clk_get failed\n", node);
		return;
	}

	hw = clk_hw_register_fixed_factor(NULL, name, parent_name, 0, mul, 1);
	if (IS_ERR(hw)) {
		pr_err("%pOF: failed to register clock\n", node);
		return;
	}

	clk_hw_register_clkdev(hw, NULL, name);
	of_clk_add_hw_provider(node, of_clk_hw_simple_get, hw);
}
CLK_OF_DECLARE(moxart_pll_clock, "moxa,moxart-pll-clock",
	       moxart_of_pll_clk_init);

static void __init moxart_of_apb_clk_init(struct device_node *node)
{
	void __iomem *base;
	struct clk_hw *hw;
	struct clk *pll_clk;
	unsigned int div, val;
	unsigned int div_idx[] = { 2, 3, 4, 6, 8};
	const char *name = node->name;
	const char *parent_name;

	of_property_read_string(node, "clock-output-names", &name);
	parent_name = of_clk_get_parent_name(node, 0);

	base = of_iomap(node, 0);
	if (!base) {
		pr_err("%pOF: of_iomap failed\n", node);
		return;
	}

	val = readl(base + 0xc) >> 4 & 0x7;
	iounmap(base);

	if (val > 4)
		val = 0;
	div = div_idx[val] * 2;

	pll_clk = of_clk_get(node, 0);
	if (IS_ERR(pll_clk)) {
		pr_err("%pOF: of_clk_get failed\n", node);
		return;
	}

	hw = clk_hw_register_fixed_factor(NULL, name, parent_name, 0, 1, div);
	if (IS_ERR(hw)) {
		pr_err("%pOF: failed to register clock\n", node);
		return;
	}

	clk_hw_register_clkdev(hw, NULL, name);
	of_clk_add_hw_provider(node, of_clk_hw_simple_get, hw);
}
CLK_OF_DECLARE(moxart_apb_clock, "moxa,moxart-apb-clock",
	       moxart_of_apb_clk_init);

/*
 * MOXA ART SoCs clock driver.
 *
 * Copyright (C) 2013 Jonas Jensen
 *
 * Jonas Jensen <jonas.jensen@gmail.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/clk-provider.h>
#include <linux/io.h>
#include <linux/of_address.h>
#include <linux/clkdev.h>

static void __init moxart_of_pll_clk_init(struct device_node *node)
{
	static void __iomem *base;
	struct clk *clk, *ref_clk;
	unsigned int mul;
	const char *name = node->name;
	const char *parent_name;

	of_property_read_string(node, "clock-output-names", &name);
	parent_name = of_clk_get_parent_name(node, 0);

	base = of_iomap(node, 0);
	if (!base) {
		pr_err("%s: of_iomap failed\n", node->full_name);
		return;
	}

	mul = readl(base + 0x30) >> 3 & 0x3f;
	iounmap(base);

	ref_clk = of_clk_get(node, 0);
	if (IS_ERR(ref_clk)) {
		pr_err("%s: of_clk_get failed\n", node->full_name);
		return;
	}

	clk = clk_register_fixed_factor(NULL, name, parent_name, 0, mul, 1);
	if (IS_ERR(clk)) {
		pr_err("%s: failed to register clock\n", node->full_name);
		return;
	}

	clk_register_clkdev(clk, NULL, name);
	of_clk_add_provider(node, of_clk_src_simple_get, clk);
}
CLK_OF_DECLARE(moxart_pll_clock, "moxa,moxart-pll-clock",
	       moxart_of_pll_clk_init);

static void __init moxart_of_apb_clk_init(struct device_node *node)
{
	static void __iomem *base;
	struct clk *clk, *pll_clk;
	unsigned int div, val;
	unsigned int div_idx[] = { 2, 3, 4, 6, 8};
	const char *name = node->name;
	const char *parent_name;

	of_property_read_string(node, "clock-output-names", &name);
	parent_name = of_clk_get_parent_name(node, 0);

	base = of_iomap(node, 0);
	if (!base) {
		pr_err("%s: of_iomap failed\n", node->full_name);
		return;
	}

	val = readl(base + 0xc) >> 4 & 0x7;
	iounmap(base);

	if (val > 4)
		val = 0;
	div = div_idx[val] * 2;

	pll_clk = of_clk_get(node, 0);
	if (IS_ERR(pll_clk)) {
		pr_err("%s: of_clk_get failed\n", node->full_name);
		return;
	}

	clk = clk_register_fixed_factor(NULL, name, parent_name, 0, 1, div);
	if (IS_ERR(clk)) {
		pr_err("%s: failed to register clock\n", node->full_name);
		return;
	}

	clk_register_clkdev(clk, NULL, name);
	of_clk_add_provider(node, of_clk_src_simple_get, clk);
}
CLK_OF_DECLARE(moxart_apb_clock, "moxa,moxart-apb-clock",
	       moxart_of_apb_clk_init);

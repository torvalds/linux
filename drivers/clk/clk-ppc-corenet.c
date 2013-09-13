/*
 * Copyright 2013 Freescale Semiconductor, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * clock driver for Freescale PowerPC corenet SoCs.
 */
#include <linux/clk-provider.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/of.h>
#include <linux/slab.h>

struct cmux_clk {
	struct clk_hw hw;
	void __iomem *reg;
	u32 flags;
};

#define PLL_KILL			BIT(31)
#define	CLKSEL_SHIFT		27
#define CLKSEL_ADJUST		BIT(0)
#define to_cmux_clk(p)		container_of(p, struct cmux_clk, hw)

static void __iomem *base;
static unsigned int clocks_per_pll;

static int cmux_set_parent(struct clk_hw *hw, u8 idx)
{
	struct cmux_clk *clk = to_cmux_clk(hw);
	u32 clksel;

	clksel = ((idx / clocks_per_pll) << 2) + idx % clocks_per_pll;
	if (clk->flags & CLKSEL_ADJUST)
		clksel += 8;
	clksel = (clksel & 0xf) << CLKSEL_SHIFT;
	iowrite32be(clksel, clk->reg);

	return 0;
}

static u8 cmux_get_parent(struct clk_hw *hw)
{
	struct cmux_clk *clk = to_cmux_clk(hw);
	u32 clksel;

	clksel = ioread32be(clk->reg);
	clksel = (clksel >> CLKSEL_SHIFT) & 0xf;
	if (clk->flags & CLKSEL_ADJUST)
		clksel -= 8;
	clksel = (clksel >> 2) * clocks_per_pll + clksel % 4;

	return clksel;
}

const struct clk_ops cmux_ops = {
	.get_parent = cmux_get_parent,
	.set_parent = cmux_set_parent,
};

static void __init core_mux_init(struct device_node *np)
{
	struct clk *clk;
	struct clk_init_data init;
	struct cmux_clk *cmux_clk;
	struct device_node *node;
	int rc, count, i;
	u32	offset;
	const char *clk_name;
	const char **parent_names;

	rc = of_property_read_u32(np, "reg", &offset);
	if (rc) {
		pr_err("%s: could not get reg property\n", np->name);
		return;
	}

	/* get the input clock source count */
	count = of_property_count_strings(np, "clock-names");
	if (count < 0) {
		pr_err("%s: get clock count error\n", np->name);
		return;
	}
	parent_names = kzalloc((sizeof(char *) * count), GFP_KERNEL);
	if (!parent_names) {
		pr_err("%s: could not allocate parent_names\n", __func__);
		return;
	}

	for (i = 0; i < count; i++)
		parent_names[i] = of_clk_get_parent_name(np, i);

	cmux_clk = kzalloc(sizeof(struct cmux_clk), GFP_KERNEL);
	if (!cmux_clk) {
		pr_err("%s: could not allocate cmux_clk\n", __func__);
		goto err_name;
	}
	cmux_clk->reg = base + offset;

	node = of_find_compatible_node(NULL, NULL, "fsl,p4080-clockgen");
	if (node && (offset >= 0x80))
		cmux_clk->flags = CLKSEL_ADJUST;

	rc = of_property_read_string_index(np, "clock-output-names",
			0, &clk_name);
	if (rc) {
		pr_err("%s: read clock names error\n", np->name);
		goto err_clk;
	}

	init.name = clk_name;
	init.ops = &cmux_ops;
	init.parent_names = parent_names;
	init.num_parents = count;
	init.flags = 0;
	cmux_clk->hw.init = &init;

	clk = clk_register(NULL, &cmux_clk->hw);
	if (IS_ERR(clk)) {
		pr_err("%s: could not register clock\n", clk_name);
		goto err_clk;
	}

	rc = of_clk_add_provider(np, of_clk_src_simple_get, clk);
	if (rc) {
		pr_err("Could not register clock provider for node:%s\n",
			 np->name);
		goto err_clk;
	}
	goto err_name;

err_clk:
	kfree(cmux_clk);
err_name:
	/* free *_names because they are reallocated when registered */
	kfree(parent_names);
}

static void __init core_pll_init(struct device_node *np)
{
	u32 offset, mult;
	int i, rc, count;
	const char *clk_name, *parent_name;
	struct clk_onecell_data *onecell_data;
	struct clk      **subclks;

	rc = of_property_read_u32(np, "reg", &offset);
	if (rc) {
		pr_err("%s: could not get reg property\n", np->name);
		return;
	}

	/* get the multiple of PLL */
	mult = ioread32be(base + offset);

	/* check if this PLL is disabled */
	if (mult & PLL_KILL) {
		pr_debug("PLL:%s is disabled\n", np->name);
		return;
	}
	mult = (mult >> 1) & 0x3f;

	parent_name = of_clk_get_parent_name(np, 0);
	if (!parent_name) {
		pr_err("PLL: %s must have a parent\n", np->name);
		return;
	}

	count = of_property_count_strings(np, "clock-output-names");
	if (count < 0 || count > 4) {
		pr_err("%s: clock is not supported\n", np->name);
		return;
	}

	/* output clock number per PLL */
	clocks_per_pll = count;

	subclks = kzalloc(sizeof(struct clk *) * count, GFP_KERNEL);
	if (!subclks) {
		pr_err("%s: could not allocate subclks\n", __func__);
		return;
	}

	onecell_data = kzalloc(sizeof(struct clk_onecell_data), GFP_KERNEL);
	if (!onecell_data) {
		pr_err("%s: could not allocate onecell_data\n", __func__);
		goto err_clks;
	}

	for (i = 0; i < count; i++) {
		rc = of_property_read_string_index(np, "clock-output-names",
				i, &clk_name);
		if (rc) {
			pr_err("%s: could not get clock names\n", np->name);
			goto err_cell;
		}

		/*
		 * when count == 4, there are 4 output clocks:
		 * /1, /2, /3, /4 respectively
		 * when count < 4, there are at least 2 output clocks:
		 * /1, /2, (/4, if count == 3) respectively.
		 */
		if (count == 4)
			subclks[i] = clk_register_fixed_factor(NULL, clk_name,
					parent_name, 0, mult, 1 + i);
		else

			subclks[i] = clk_register_fixed_factor(NULL, clk_name,
					parent_name, 0, mult, 1 << i);

		if (IS_ERR(subclks[i])) {
			pr_err("%s: could not register clock\n", clk_name);
			goto err_cell;
		}
	}

	onecell_data->clks = subclks;
	onecell_data->clk_num = count;

	rc = of_clk_add_provider(np, of_clk_src_onecell_get, onecell_data);
	if (rc) {
		pr_err("Could not register clk provider for node:%s\n",
			 np->name);
		goto err_cell;
	}

	return;
err_cell:
	kfree(onecell_data);
err_clks:
	kfree(subclks);
}

static const struct of_device_id clk_match[] __initconst = {
	{ .compatible = "fixed-clock", .data = of_fixed_clk_setup, },
	{ .compatible = "fsl,core-pll-clock", .data = core_pll_init, },
	{ .compatible = "fsl,core-mux-clock", .data = core_mux_init, },
	{}
};

static int __init ppc_corenet_clk_probe(struct platform_device *pdev)
{
	struct device_node *np;

	np = pdev->dev.of_node;
	base = of_iomap(np, 0);
	if (!base) {
		dev_err(&pdev->dev, "iomap error\n");
		return -ENOMEM;
	}
	of_clk_init(clk_match);

	return 0;
}

static const struct of_device_id ppc_clk_ids[] __initconst = {
	{ .compatible = "fsl,qoriq-clockgen-1.0", },
	{ .compatible = "fsl,qoriq-clockgen-2.0", },
	{}
};

static struct platform_driver ppc_corenet_clk_driver = {
	.driver = {
		.name = "ppc_corenet_clock",
		.owner = THIS_MODULE,
		.of_match_table = ppc_clk_ids,
	},
	.probe = ppc_corenet_clk_probe,
};

static int __init ppc_corenet_clk_init(void)
{
	return platform_driver_register(&ppc_corenet_clk_driver);
}
subsys_initcall(ppc_corenet_clk_init);

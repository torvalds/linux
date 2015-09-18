/*
 * Copyright 2013 Freescale Semiconductor, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * clock driver for Freescale QorIQ SoCs.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/clk-provider.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/of.h>
#include <linux/slab.h>

struct cmux_clk {
	struct clk_hw hw;
	void __iomem *reg;
	unsigned int clk_per_pll;
	u32 flags;
};

#define PLL_KILL			BIT(31)
#define	CLKSEL_SHIFT		27
#define CLKSEL_ADJUST		BIT(0)
#define to_cmux_clk(p)		container_of(p, struct cmux_clk, hw)

static int cmux_set_parent(struct clk_hw *hw, u8 idx)
{
	struct cmux_clk *clk = to_cmux_clk(hw);
	u32 clksel;

	clksel = ((idx / clk->clk_per_pll) << 2) + idx % clk->clk_per_pll;
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
	clksel = (clksel >> 2) * clk->clk_per_pll + clksel % 4;

	return clksel;
}

static const struct clk_ops cmux_ops = {
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
	struct of_phandle_args clkspec;

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
	parent_names = kcalloc(count, sizeof(char *), GFP_KERNEL);
	if (!parent_names)
		return;

	for (i = 0; i < count; i++)
		parent_names[i] = of_clk_get_parent_name(np, i);

	cmux_clk = kzalloc(sizeof(*cmux_clk), GFP_KERNEL);
	if (!cmux_clk)
		goto err_name;

	cmux_clk->reg = of_iomap(np, 0);
	if (!cmux_clk->reg) {
		pr_err("%s: could not map register\n", __func__);
		goto err_clk;
	}

	rc = of_parse_phandle_with_args(np, "clocks", "#clock-cells", 0,
					&clkspec);
	if (rc) {
		pr_err("%s: parse clock node error\n", __func__);
		goto err_clk;
	}

	cmux_clk->clk_per_pll = of_property_count_strings(clkspec.np,
			"clock-output-names");
	of_node_put(clkspec.np);

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
	u32 mult;
	int i, rc, count;
	const char *clk_name, *parent_name;
	struct clk_onecell_data *onecell_data;
	struct clk      **subclks;
	void __iomem *base;

	base = of_iomap(np, 0);
	if (!base) {
		pr_err("iomap error\n");
		return;
	}

	/* get the multiple of PLL */
	mult = ioread32be(base);

	/* check if this PLL is disabled */
	if (mult & PLL_KILL) {
		pr_debug("PLL:%s is disabled\n", np->name);
		goto err_map;
	}
	mult = (mult >> 1) & 0x3f;

	parent_name = of_clk_get_parent_name(np, 0);
	if (!parent_name) {
		pr_err("PLL: %s must have a parent\n", np->name);
		goto err_map;
	}

	count = of_property_count_strings(np, "clock-output-names");
	if (count < 0 || count > 4) {
		pr_err("%s: clock is not supported\n", np->name);
		goto err_map;
	}

	subclks = kcalloc(count, sizeof(struct clk *), GFP_KERNEL);
	if (!subclks)
		goto err_map;

	onecell_data = kmalloc(sizeof(*onecell_data), GFP_KERNEL);
	if (!onecell_data)
		goto err_clks;

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

	iounmap(base);
	return;
err_cell:
	kfree(onecell_data);
err_clks:
	kfree(subclks);
err_map:
	iounmap(base);
}

static void __init sysclk_init(struct device_node *node)
{
	struct clk *clk;
	const char *clk_name = node->name;
	struct device_node *np = of_get_parent(node);
	u32 rate;

	if (!np) {
		pr_err("could not get parent node\n");
		return;
	}

	if (of_property_read_u32(np, "clock-frequency", &rate)) {
		of_node_put(node);
		return;
	}

	of_property_read_string(np, "clock-output-names", &clk_name);

	clk = clk_register_fixed_rate(NULL, clk_name, NULL, CLK_IS_ROOT, rate);
	if (!IS_ERR(clk))
		of_clk_add_provider(np, of_clk_src_simple_get, clk);
}

static void __init pltfrm_pll_init(struct device_node *np)
{
	void __iomem *base;
	uint32_t mult;
	const char *parent_name, *clk_name;
	int i, _errno;
	struct clk_onecell_data *cod;

	base = of_iomap(np, 0);
	if (!base) {
		pr_err("%s(): %s: of_iomap() failed\n", __func__, np->name);
		return;
	}

	/* Get the multiple of PLL */
	mult = ioread32be(base);

	iounmap(base);

	/* Check if this PLL is disabled */
	if (mult & PLL_KILL) {
		pr_debug("%s(): %s: Disabled\n", __func__, np->name);
		return;
	}
	mult = (mult & GENMASK(6, 1)) >> 1;

	parent_name = of_clk_get_parent_name(np, 0);
	if (!parent_name) {
		pr_err("%s(): %s: of_clk_get_parent_name() failed\n",
		       __func__, np->name);
		return;
	}

	i = of_property_count_strings(np, "clock-output-names");
	if (i < 0) {
		pr_err("%s(): %s: of_property_count_strings(clock-output-names) = %d\n",
		       __func__, np->name, i);
		return;
	}

	cod = kmalloc(sizeof(*cod) + i * sizeof(struct clk *), GFP_KERNEL);
	if (!cod)
		return;
	cod->clks = (struct clk **)(cod + 1);
	cod->clk_num = i;

	for (i = 0; i < cod->clk_num; i++) {
		_errno = of_property_read_string_index(np, "clock-output-names",
						       i, &clk_name);
		if (_errno < 0) {
			pr_err("%s(): %s: of_property_read_string_index(clock-output-names) = %d\n",
			       __func__, np->name, _errno);
			goto return_clk_unregister;
		}

		cod->clks[i] = clk_register_fixed_factor(NULL, clk_name,
					       parent_name, 0, mult, 1 + i);
		if (IS_ERR(cod->clks[i])) {
			pr_err("%s(): %s: clk_register_fixed_factor(%s) = %ld\n",
			       __func__, np->name,
			       clk_name, PTR_ERR(cod->clks[i]));
			goto return_clk_unregister;
		}
	}

	_errno = of_clk_add_provider(np, of_clk_src_onecell_get, cod);
	if (_errno < 0) {
		pr_err("%s(): %s: of_clk_add_provider() = %d\n",
		       __func__, np->name, _errno);
		goto return_clk_unregister;
	}

	return;

return_clk_unregister:
	while (--i >= 0)
		clk_unregister(cod->clks[i]);
	kfree(cod);
}

CLK_OF_DECLARE(qoriq_sysclk_1, "fsl,qoriq-sysclk-1.0", sysclk_init);
CLK_OF_DECLARE(qoriq_sysclk_2, "fsl,qoriq-sysclk-2.0", sysclk_init);
CLK_OF_DECLARE(qoriq_core_pll_1, "fsl,qoriq-core-pll-1.0", core_pll_init);
CLK_OF_DECLARE(qoriq_core_pll_2, "fsl,qoriq-core-pll-2.0", core_pll_init);
CLK_OF_DECLARE(qoriq_core_mux_1, "fsl,qoriq-core-mux-1.0", core_mux_init);
CLK_OF_DECLARE(qoriq_core_mux_2, "fsl,qoriq-core-mux-2.0", core_mux_init);
CLK_OF_DECLARE(qoriq_pltfrm_pll_1, "fsl,qoriq-platform-pll-1.0", pltfrm_pll_init);
CLK_OF_DECLARE(qoriq_pltfrm_pll_2, "fsl,qoriq-platform-pll-2.0", pltfrm_pll_init);

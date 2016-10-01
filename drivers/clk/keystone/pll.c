/*
 * PLL clock driver for Keystone devices
 *
 * Copyright (C) 2013 Texas Instruments Inc.
 *	Murali Karicheri <m-karicheri2@ti.com>
 *	Santosh Shilimkar <santosh.shilimkar@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <linux/clk-provider.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/of_address.h>
#include <linux/of.h>
#include <linux/module.h>

#define PLLM_LOW_MASK		0x3f
#define PLLM_HIGH_MASK		0x7ffc0
#define MAIN_PLLM_HIGH_MASK	0x7f000
#define PLLM_HIGH_SHIFT		6
#define PLLD_MASK		0x3f
#define CLKOD_MASK		0x780000
#define CLKOD_SHIFT		19

/**
 * struct clk_pll_data - pll data structure
 * @has_pllctrl: If set to non zero, lower 6 bits of multiplier is in pllm
 *	register of pll controller, else it is in the pll_ctrl0((bit 11-6)
 * @phy_pllm: Physical address of PLLM in pll controller. Used when
 *	has_pllctrl is non zero.
 * @phy_pll_ctl0: Physical address of PLL ctrl0. This could be that of
 *	Main PLL or any other PLLs in the device such as ARM PLL, DDR PLL
 *	or PA PLL available on keystone2. These PLLs are controlled by
 *	this register. Main PLL is controlled by a PLL controller.
 * @pllm: PLL register map address for multiplier bits
 * @pllod: PLL register map address for post divider bits
 * @pll_ctl0: PLL controller map address
 * @pllm_lower_mask: multiplier lower mask
 * @pllm_upper_mask: multiplier upper mask
 * @pllm_upper_shift: multiplier upper shift
 * @plld_mask: divider mask
 * @clkod_mask: output divider mask
 * @clkod_shift: output divider shift
 * @plld_mask: divider mask
 * @postdiv: Fixed post divider
 */
struct clk_pll_data {
	bool has_pllctrl;
	u32 phy_pllm;
	u32 phy_pll_ctl0;
	void __iomem *pllm;
	void __iomem *pllod;
	void __iomem *pll_ctl0;
	u32 pllm_lower_mask;
	u32 pllm_upper_mask;
	u32 pllm_upper_shift;
	u32 plld_mask;
	u32 clkod_mask;
	u32 clkod_shift;
	u32 postdiv;
};

/**
 * struct clk_pll - Main pll clock
 * @hw: clk_hw for the pll
 * @pll_data: PLL driver specific data
 */
struct clk_pll {
	struct clk_hw hw;
	struct clk_pll_data *pll_data;
};

#define to_clk_pll(_hw) container_of(_hw, struct clk_pll, hw)

static unsigned long clk_pllclk_recalc(struct clk_hw *hw,
					unsigned long parent_rate)
{
	struct clk_pll *pll = to_clk_pll(hw);
	struct clk_pll_data *pll_data = pll->pll_data;
	unsigned long rate = parent_rate;
	u32  mult = 0, prediv, postdiv, val;

	/*
	 * get bits 0-5 of multiplier from pllctrl PLLM register
	 * if has_pllctrl is non zero
	 */
	if (pll_data->has_pllctrl) {
		val = readl(pll_data->pllm);
		mult = (val & pll_data->pllm_lower_mask);
	}

	/* bit6-12 of PLLM is in Main PLL control register */
	val = readl(pll_data->pll_ctl0);
	mult |= ((val & pll_data->pllm_upper_mask)
			>> pll_data->pllm_upper_shift);
	prediv = (val & pll_data->plld_mask);

	if (!pll_data->has_pllctrl)
		/* read post divider from od bits*/
		postdiv = ((val & pll_data->clkod_mask) >>
				 pll_data->clkod_shift) + 1;
	else if (pll_data->pllod) {
		postdiv = readl(pll_data->pllod);
		postdiv = ((postdiv & pll_data->clkod_mask) >>
				pll_data->clkod_shift) + 1;
	} else
		postdiv = pll_data->postdiv;

	rate /= (prediv + 1);
	rate = (rate * (mult + 1));
	rate /= postdiv;

	return rate;
}

static const struct clk_ops clk_pll_ops = {
	.recalc_rate = clk_pllclk_recalc,
};

static struct clk *clk_register_pll(struct device *dev,
			const char *name,
			const char *parent_name,
			struct clk_pll_data *pll_data)
{
	struct clk_init_data init;
	struct clk_pll *pll;
	struct clk *clk;

	pll = kzalloc(sizeof(*pll), GFP_KERNEL);
	if (!pll)
		return ERR_PTR(-ENOMEM);

	init.name = name;
	init.ops = &clk_pll_ops;
	init.flags = 0;
	init.parent_names = (parent_name ? &parent_name : NULL);
	init.num_parents = (parent_name ? 1 : 0);

	pll->pll_data	= pll_data;
	pll->hw.init = &init;

	clk = clk_register(NULL, &pll->hw);
	if (IS_ERR(clk))
		goto out;

	return clk;
out:
	kfree(pll);
	return NULL;
}

/**
 * _of_pll_clk_init - PLL initialisation via DT
 * @node: device tree node for this clock
 * @pllctrl: If true, lower 6 bits of multiplier is in pllm register of
 *		pll controller, else it is in the control register0(bit 11-6)
 */
static void __init _of_pll_clk_init(struct device_node *node, bool pllctrl)
{
	struct clk_pll_data *pll_data;
	const char *parent_name;
	struct clk *clk;
	int i;

	pll_data = kzalloc(sizeof(*pll_data), GFP_KERNEL);
	if (!pll_data) {
		pr_err("%s: Out of memory\n", __func__);
		return;
	}

	parent_name = of_clk_get_parent_name(node, 0);
	if (of_property_read_u32(node, "fixed-postdiv",	&pll_data->postdiv)) {
		/* assume the PLL has output divider register bits */
		pll_data->clkod_mask = CLKOD_MASK;
		pll_data->clkod_shift = CLKOD_SHIFT;

		/*
		 * Check if there is an post-divider register. If not
		 * assume od bits are part of control register.
		 */
		i = of_property_match_string(node, "reg-names",
					     "post-divider");
		pll_data->pllod = of_iomap(node, i);
	}

	i = of_property_match_string(node, "reg-names", "control");
	pll_data->pll_ctl0 = of_iomap(node, i);
	if (!pll_data->pll_ctl0) {
		pr_err("%s: ioremap failed\n", __func__);
		iounmap(pll_data->pllod);
		goto out;
	}

	pll_data->pllm_lower_mask = PLLM_LOW_MASK;
	pll_data->pllm_upper_shift = PLLM_HIGH_SHIFT;
	pll_data->plld_mask = PLLD_MASK;
	pll_data->has_pllctrl = pllctrl;
	if (!pll_data->has_pllctrl) {
		pll_data->pllm_upper_mask = PLLM_HIGH_MASK;
	} else {
		pll_data->pllm_upper_mask = MAIN_PLLM_HIGH_MASK;
		i = of_property_match_string(node, "reg-names", "multiplier");
		pll_data->pllm = of_iomap(node, i);
		if (!pll_data->pllm) {
			iounmap(pll_data->pll_ctl0);
			iounmap(pll_data->pllod);
			goto out;
		}
	}

	clk = clk_register_pll(NULL, node->name, parent_name, pll_data);
	if (clk) {
		of_clk_add_provider(node, of_clk_src_simple_get, clk);
		return;
	}

out:
	pr_err("%s: error initializing pll %s\n", __func__, node->name);
	kfree(pll_data);
}

/**
 * of_keystone_pll_clk_init - PLL initialisation DT wrapper
 * @node: device tree node for this clock
 */
static void __init of_keystone_pll_clk_init(struct device_node *node)
{
	_of_pll_clk_init(node, false);
}
CLK_OF_DECLARE(keystone_pll_clock, "ti,keystone,pll-clock",
					of_keystone_pll_clk_init);

/**
 * of_keystone_main_pll_clk_init - Main PLL initialisation DT wrapper
 * @node: device tree node for this clock
 */
static void __init of_keystone_main_pll_clk_init(struct device_node *node)
{
	_of_pll_clk_init(node, true);
}
CLK_OF_DECLARE(keystone_main_pll_clock, "ti,keystone,main-pll-clock",
						of_keystone_main_pll_clk_init);

/**
 * of_pll_div_clk_init - PLL divider setup function
 * @node: device tree node for this clock
 */
static void __init of_pll_div_clk_init(struct device_node *node)
{
	const char *parent_name;
	void __iomem *reg;
	u32 shift, mask;
	struct clk *clk;
	const char *clk_name = node->name;

	of_property_read_string(node, "clock-output-names", &clk_name);
	reg = of_iomap(node, 0);
	if (!reg) {
		pr_err("%s: ioremap failed\n", __func__);
		return;
	}

	parent_name = of_clk_get_parent_name(node, 0);
	if (!parent_name) {
		pr_err("%s: missing parent clock\n", __func__);
		return;
	}

	if (of_property_read_u32(node, "bit-shift", &shift)) {
		pr_err("%s: missing 'shift' property\n", __func__);
		return;
	}

	if (of_property_read_u32(node, "bit-mask", &mask)) {
		pr_err("%s: missing 'bit-mask' property\n", __func__);
		return;
	}

	clk = clk_register_divider(NULL, clk_name, parent_name, 0, reg, shift,
				 mask, 0, NULL);
	if (clk)
		of_clk_add_provider(node, of_clk_src_simple_get, clk);
	else
		pr_err("%s: error registering divider %s\n", __func__, clk_name);
}
CLK_OF_DECLARE(pll_divider_clock, "ti,keystone,pll-divider-clock", of_pll_div_clk_init);

/**
 * of_pll_mux_clk_init - PLL mux setup function
 * @node: device tree node for this clock
 */
static void __init of_pll_mux_clk_init(struct device_node *node)
{
	void __iomem *reg;
	u32 shift, mask;
	struct clk *clk;
	const char *parents[2];
	const char *clk_name = node->name;

	of_property_read_string(node, "clock-output-names", &clk_name);
	reg = of_iomap(node, 0);
	if (!reg) {
		pr_err("%s: ioremap failed\n", __func__);
		return;
	}

	of_clk_parent_fill(node, parents, 2);
	if (!parents[0] || !parents[1]) {
		pr_err("%s: missing parent clocks\n", __func__);
		return;
	}

	if (of_property_read_u32(node, "bit-shift", &shift)) {
		pr_err("%s: missing 'shift' property\n", __func__);
		return;
	}

	if (of_property_read_u32(node, "bit-mask", &mask)) {
		pr_err("%s: missing 'bit-mask' property\n", __func__);
		return;
	}

	clk = clk_register_mux(NULL, clk_name, (const char **)&parents,
				ARRAY_SIZE(parents) , 0, reg, shift, mask,
				0, NULL);
	if (clk)
		of_clk_add_provider(node, of_clk_src_simple_get, clk);
	else
		pr_err("%s: error registering mux %s\n", __func__, clk_name);
}
CLK_OF_DECLARE(pll_mux_clock, "ti,keystone,pll-mux-clock", of_pll_mux_clk_init);

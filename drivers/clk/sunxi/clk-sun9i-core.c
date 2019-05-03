/*
 * Copyright 2014 Chen-Yu Tsai
 *
 * Chen-Yu Tsai <wens@csie.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/log2.h>

#include "clk-factors.h"


/**
 * sun9i_a80_get_pll4_factors() - calculates n, p, m factors for PLL4
 * PLL4 rate is calculated as follows
 * rate = (parent_rate * n >> p) / (m + 1);
 * parent_rate is always 24MHz
 *
 * p and m are named div1 and div2 in Allwinner's SDK
 */

static void sun9i_a80_get_pll4_factors(struct factors_request *req)
{
	int n;
	int m = 1;
	int p = 1;

	/* Normalize value to a 6 MHz multiple (24 MHz / 4) */
	n = DIV_ROUND_UP(req->rate, 6000000);

	/* If n is too large switch to steps of 12 MHz */
	if (n > 255) {
		m = 0;
		n = (n + 1) / 2;
	}

	/* If n is still too large switch to steps of 24 MHz */
	if (n > 255) {
		p = 0;
		n = (n + 1) / 2;
	}

	/* n must be between 12 and 255 */
	if (n > 255)
		n = 255;
	else if (n < 12)
		n = 12;

	req->rate = ((24000000 * n) >> p) / (m + 1);
	req->n = n;
	req->m = m;
	req->p = p;
}

static const struct clk_factors_config sun9i_a80_pll4_config = {
	.mshift = 18,
	.mwidth = 1,
	.nshift = 8,
	.nwidth = 8,
	.pshift = 16,
	.pwidth = 1,
};

static const struct factors_data sun9i_a80_pll4_data __initconst = {
	.enable = 31,
	.table = &sun9i_a80_pll4_config,
	.getter = sun9i_a80_get_pll4_factors,
};

static DEFINE_SPINLOCK(sun9i_a80_pll4_lock);

static void __init sun9i_a80_pll4_setup(struct device_node *node)
{
	void __iomem *reg;

	reg = of_io_request_and_map(node, 0, of_node_full_name(node));
	if (IS_ERR(reg)) {
		pr_err("Could not get registers for a80-pll4-clk: %pOFn\n",
		       node);
		return;
	}

	sunxi_factors_register(node, &sun9i_a80_pll4_data,
			       &sun9i_a80_pll4_lock, reg);
}
CLK_OF_DECLARE(sun9i_a80_pll4, "allwinner,sun9i-a80-pll4-clk", sun9i_a80_pll4_setup);


/**
 * sun9i_a80_get_gt_factors() - calculates m factor for GT
 * GT rate is calculated as follows
 * rate = parent_rate / (m + 1);
 */

static void sun9i_a80_get_gt_factors(struct factors_request *req)
{
	u32 div;

	if (req->parent_rate < req->rate)
		req->rate = req->parent_rate;

	div = DIV_ROUND_UP(req->parent_rate, req->rate);

	/* maximum divider is 4 */
	if (div > 4)
		div = 4;

	req->rate = req->parent_rate / div;
	req->m = div;
}

static const struct clk_factors_config sun9i_a80_gt_config = {
	.mshift = 0,
	.mwidth = 2,
};

static const struct factors_data sun9i_a80_gt_data __initconst = {
	.mux = 24,
	.muxmask = BIT(1) | BIT(0),
	.table = &sun9i_a80_gt_config,
	.getter = sun9i_a80_get_gt_factors,
};

static DEFINE_SPINLOCK(sun9i_a80_gt_lock);

static void __init sun9i_a80_gt_setup(struct device_node *node)
{
	void __iomem *reg;

	reg = of_io_request_and_map(node, 0, of_node_full_name(node));
	if (IS_ERR(reg)) {
		pr_err("Could not get registers for a80-gt-clk: %pOFn\n",
		       node);
		return;
	}

	/* The GT bus clock needs to be always enabled */
	sunxi_factors_register_critical(node, &sun9i_a80_gt_data,
					&sun9i_a80_gt_lock, reg);
}
CLK_OF_DECLARE(sun9i_a80_gt, "allwinner,sun9i-a80-gt-clk", sun9i_a80_gt_setup);


/**
 * sun9i_a80_get_ahb_factors() - calculates p factor for AHB0/1/2
 * AHB rate is calculated as follows
 * rate = parent_rate >> p;
 */

static void sun9i_a80_get_ahb_factors(struct factors_request *req)
{
	u32 _p;

	if (req->parent_rate < req->rate)
		req->rate = req->parent_rate;

	_p = order_base_2(DIV_ROUND_UP(req->parent_rate, req->rate));

	/* maximum p is 3 */
	if (_p > 3)
		_p = 3;

	req->rate = req->parent_rate >> _p;
	req->p = _p;
}

static const struct clk_factors_config sun9i_a80_ahb_config = {
	.pshift = 0,
	.pwidth = 2,
};

static const struct factors_data sun9i_a80_ahb_data __initconst = {
	.mux = 24,
	.muxmask = BIT(1) | BIT(0),
	.table = &sun9i_a80_ahb_config,
	.getter = sun9i_a80_get_ahb_factors,
};

static DEFINE_SPINLOCK(sun9i_a80_ahb_lock);

static void __init sun9i_a80_ahb_setup(struct device_node *node)
{
	void __iomem *reg;

	reg = of_io_request_and_map(node, 0, of_node_full_name(node));
	if (IS_ERR(reg)) {
		pr_err("Could not get registers for a80-ahb-clk: %pOFn\n",
		       node);
		return;
	}

	sunxi_factors_register(node, &sun9i_a80_ahb_data,
			       &sun9i_a80_ahb_lock, reg);
}
CLK_OF_DECLARE(sun9i_a80_ahb, "allwinner,sun9i-a80-ahb-clk", sun9i_a80_ahb_setup);


static const struct factors_data sun9i_a80_apb0_data __initconst = {
	.mux = 24,
	.muxmask = BIT(0),
	.table = &sun9i_a80_ahb_config,
	.getter = sun9i_a80_get_ahb_factors,
};

static DEFINE_SPINLOCK(sun9i_a80_apb0_lock);

static void __init sun9i_a80_apb0_setup(struct device_node *node)
{
	void __iomem *reg;

	reg = of_io_request_and_map(node, 0, of_node_full_name(node));
	if (IS_ERR(reg)) {
		pr_err("Could not get registers for a80-apb0-clk: %pOFn\n",
		       node);
		return;
	}

	sunxi_factors_register(node, &sun9i_a80_apb0_data,
			       &sun9i_a80_apb0_lock, reg);
}
CLK_OF_DECLARE(sun9i_a80_apb0, "allwinner,sun9i-a80-apb0-clk", sun9i_a80_apb0_setup);


/**
 * sun9i_a80_get_apb1_factors() - calculates m, p factors for APB1
 * APB1 rate is calculated as follows
 * rate = (parent_rate >> p) / (m + 1);
 */

static void sun9i_a80_get_apb1_factors(struct factors_request *req)
{
	u32 div;

	if (req->parent_rate < req->rate)
		req->rate = req->parent_rate;

	div = DIV_ROUND_UP(req->parent_rate, req->rate);

	/* Highest possible divider is 256 (p = 3, m = 31) */
	if (div > 256)
		div = 256;

	req->p = order_base_2(div);
	req->m = (req->parent_rate >> req->p) - 1;
	req->rate = (req->parent_rate >> req->p) / (req->m + 1);
}

static const struct clk_factors_config sun9i_a80_apb1_config = {
	.mshift = 0,
	.mwidth = 5,
	.pshift = 16,
	.pwidth = 2,
};

static const struct factors_data sun9i_a80_apb1_data __initconst = {
	.mux = 24,
	.muxmask = BIT(0),
	.table = &sun9i_a80_apb1_config,
	.getter = sun9i_a80_get_apb1_factors,
};

static DEFINE_SPINLOCK(sun9i_a80_apb1_lock);

static void __init sun9i_a80_apb1_setup(struct device_node *node)
{
	void __iomem *reg;

	reg = of_io_request_and_map(node, 0, of_node_full_name(node));
	if (IS_ERR(reg)) {
		pr_err("Could not get registers for a80-apb1-clk: %pOFn\n",
		       node);
		return;
	}

	sunxi_factors_register(node, &sun9i_a80_apb1_data,
			       &sun9i_a80_apb1_lock, reg);
}
CLK_OF_DECLARE(sun9i_a80_apb1, "allwinner,sun9i-a80-apb1-clk", sun9i_a80_apb1_setup);

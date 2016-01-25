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
#include <linux/of_address.h>

#include "clk-factors.h"

/**
 * sun8i_a23_get_mbus_factors() - calculates m factor for MBUS clocks
 * MBUS rate is calculated as follows
 * rate = parent_rate / (m + 1);
 */

static void sun8i_a23_get_mbus_factors(struct factors_request *req)
{
	u8 div;

	/*
	 * These clocks can only divide, so we will never be able to
	 * achieve frequencies higher than the parent frequency
	 */
	if (req->rate > req->parent_rate)
		req->rate = req->parent_rate;

	div = DIV_ROUND_UP(req->parent_rate, req->rate);

	if (div > 8)
		div = 8;

	req->rate = req->parent_rate / div;
	req->m = div - 1;
}

static struct clk_factors_config sun8i_a23_mbus_config = {
	.mshift = 0,
	.mwidth = 3,
};

static const struct factors_data sun8i_a23_mbus_data __initconst = {
	.enable = 31,
	.mux = 24,
	.muxmask = BIT(1) | BIT(0),
	.table = &sun8i_a23_mbus_config,
	.getter = sun8i_a23_get_mbus_factors,
};

static DEFINE_SPINLOCK(sun8i_a23_mbus_lock);

static void __init sun8i_a23_mbus_setup(struct device_node *node)
{
	struct clk *mbus;
	void __iomem *reg;

	reg = of_iomap(node, 0);
	if (!reg) {
		pr_err("Could not get registers for a23-mbus-clk\n");
		return;
	}

	mbus = sunxi_factors_register(node, &sun8i_a23_mbus_data,
				      &sun8i_a23_mbus_lock, reg);

	/* The MBUS clocks needs to be always enabled */
	__clk_get(mbus);
	clk_prepare_enable(mbus);
}
CLK_OF_DECLARE(sun8i_a23_mbus, "allwinner,sun8i-a23-mbus-clk", sun8i_a23_mbus_setup);

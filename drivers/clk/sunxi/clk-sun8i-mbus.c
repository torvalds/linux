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

#include <linux/clk-provider.h>
#include <linux/clkdev.h>
#include <linux/of_address.h>

#include "clk-factors.h"

/**
 * sun8i_a23_get_mbus_factors() - calculates m factor for MBUS clocks
 * MBUS rate is calculated as follows
 * rate = parent_rate / (m + 1);
 */

static void sun8i_a23_get_mbus_factors(u32 *freq, u32 parent_rate,
				       u8 *n, u8 *k, u8 *m, u8 *p)
{
	u8 div;

	/*
	 * These clocks can only divide, so we will never be able to
	 * achieve frequencies higher than the parent frequency
	 */
	if (*freq > parent_rate)
		*freq = parent_rate;

	div = DIV_ROUND_UP(parent_rate, *freq);

	if (div > 8)
		div = 8;

	*freq = parent_rate / div;

	/* we were called to round the frequency, we can now return */
	if (m == NULL)
		return;

	*m = div - 1;
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
	struct clk *mbus = sunxi_factors_register(node, &sun8i_a23_mbus_data,
						  &sun8i_a23_mbus_lock);

	/* The MBUS clocks needs to be always enabled */
	__clk_get(mbus);
	clk_prepare_enable(mbus);
}
CLK_OF_DECLARE(sun8i_a23_mbus, "allwinner,sun8i-a23-mbus-clk", sun8i_a23_mbus_setup);

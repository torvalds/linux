/*
 * Copyright 2013 Emilio López
 *
 * Emilio López <emilio@elopez.com.ar>
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

#include "clk-factors.h"

/**
 * sun4i_get_mod0_factors() - calculates m, n factors for MOD0-style clocks
 * MOD0 rate is calculated as follows
 * rate = (parent_rate >> p) / (m + 1);
 */

static void sun4i_a10_get_mod0_factors(u32 *freq, u32 parent_rate,
				       u8 *n, u8 *k, u8 *m, u8 *p)
{
	u8 div, calcm, calcp;

	/* These clocks can only divide, so we will never be able to achieve
	 * frequencies higher than the parent frequency */
	if (*freq > parent_rate)
		*freq = parent_rate;

	div = DIV_ROUND_UP(parent_rate, *freq);

	if (div < 16)
		calcp = 0;
	else if (div / 2 < 16)
		calcp = 1;
	else if (div / 4 < 16)
		calcp = 2;
	else
		calcp = 3;

	calcm = DIV_ROUND_UP(div, 1 << calcp);

	*freq = (parent_rate >> calcp) / calcm;

	/* we were called to round the frequency, we can now return */
	if (n == NULL)
		return;

	*m = calcm - 1;
	*p = calcp;
}

/* user manual says "n" but it's really "p" */
static struct clk_factors_config sun4i_a10_mod0_config = {
	.mshift = 0,
	.mwidth = 4,
	.pshift = 16,
	.pwidth = 2,
};

static const struct factors_data sun4i_a10_mod0_data __initconst = {
	.enable = 31,
	.mux = 24,
	.table = &sun4i_a10_mod0_config,
	.getter = sun4i_a10_get_mod0_factors,
};

static DEFINE_SPINLOCK(sun4i_a10_mod0_lock);

static void __init sun4i_a10_mod0_setup(struct device_node *node)
{
	sunxi_factors_register(node, &sun4i_a10_mod0_data, &sun4i_a10_mod0_lock);
}
CLK_OF_DECLARE(sun4i_a10_mod0, "allwinner,sun4i-a10-mod0-clk", sun4i_a10_mod0_setup);

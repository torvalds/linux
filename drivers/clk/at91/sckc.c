/*
 * drivers/clk/at91/sckc.c
 *
 *  Copyright (C) 2013 Boris BREZILLON <b.brezillon@overkiz.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */

#include <linux/clk-provider.h>
#include <linux/clkdev.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/io.h>

#include "sckc.h"

static const struct of_device_id sckc_clk_ids[] __initconst = {
	/* Slow clock */
	{
		.compatible = "atmel,at91sam9x5-clk-slow-osc",
		.data = of_at91sam9x5_clk_slow_osc_setup,
	},
	{
		.compatible = "atmel,at91sam9x5-clk-slow-rc-osc",
		.data = of_at91sam9x5_clk_slow_rc_osc_setup,
	},
	{
		.compatible = "atmel,at91sam9x5-clk-slow",
		.data = of_at91sam9x5_clk_slow_setup,
	},
	{ /*sentinel*/ }
};

static void __init of_at91sam9x5_sckc_setup(struct device_node *np)
{
	struct device_node *childnp;
	void (*clk_setup)(struct device_node *, void __iomem *);
	const struct of_device_id *clk_id;
	void __iomem *regbase = of_iomap(np, 0);

	if (!regbase)
		return;

	for_each_child_of_node(np, childnp) {
		clk_id = of_match_node(sckc_clk_ids, childnp);
		if (!clk_id)
			continue;
		clk_setup = clk_id->data;
		clk_setup(childnp, regbase);
	}
}
CLK_OF_DECLARE(at91sam9x5_clk_sckc, "atmel,at91sam9x5-sckc",
	       of_at91sam9x5_sckc_setup);

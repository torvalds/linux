// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Hisilicon HiP04 clock driver
 *
 * Copyright (c) 2013-2014 Hisilicon Limited.
 * Copyright (c) 2013-2014 Linaro Limited.
 *
 * Author: Haojian Zhuang <haojian.zhuang@linaro.org>
 */

#include <linux/kernel.h>
#include <linux/clk-provider.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/slab.h>

#include <dt-bindings/clock/hip04-clock.h>

#include "clk.h"

/* fixed rate clocks */
static struct hisi_fixed_rate_clock hip04_fixed_rate_clks[] __initdata = {
	{ HIP04_OSC50M,   "osc50m",   NULL, 0, 50000000, },
	{ HIP04_CLK_50M,  "clk50m",   NULL, 0, 50000000, },
	{ HIP04_CLK_168M, "clk168m",  NULL, 0, 168750000, },
};

static void __init hip04_clk_init(struct device_node *np)
{
	struct hisi_clock_data *clk_data;

	clk_data = hisi_clk_init(np, HIP04_NR_CLKS);
	if (!clk_data)
		return;

	hisi_clk_register_fixed_rate(hip04_fixed_rate_clks,
				     ARRAY_SIZE(hip04_fixed_rate_clks),
				     clk_data);
}
CLK_OF_DECLARE(hip04_clk, "hisilicon,hip04-clock", hip04_clk_init);

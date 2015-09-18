/*
 * Hisilicon HiP04 clock driver
 *
 * Copyright (c) 2013-2014 Hisilicon Limited.
 * Copyright (c) 2013-2014 Linaro Limited.
 *
 * Author: Haojian Zhuang <haojian.zhuang@linaro.org>
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
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
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
	{ HIP04_OSC50M,   "osc50m",   NULL, CLK_IS_ROOT, 50000000, },
	{ HIP04_CLK_50M,  "clk50m",   NULL, CLK_IS_ROOT, 50000000, },
	{ HIP04_CLK_168M, "clk168m",  NULL, CLK_IS_ROOT, 168750000, },
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

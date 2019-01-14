// SPDX-License-Identifier: MIT
/*
 * clock framework for AMD Stoney based clocks
 *
 * Copyright 2018 Advanced Micro Devices, Inc.
 */

#include <linux/clk.h>
#include <linux/clkdev.h>
#include <linux/clk-provider.h>
#include <linux/platform_data/clk-st.h>
#include <linux/platform_device.h>

/* Clock Driving Strength 2 register */
#define CLKDRVSTR2	0x28
/* Clock Control 1 register */
#define MISCCLKCNTL1	0x40
/* Auxiliary clock1 enable bit */
#define OSCCLKENB	2
/* 25Mhz auxiliary output clock freq bit */
#define OSCOUT1CLK25MHZ	16

#define ST_CLK_48M	0
#define ST_CLK_25M	1
#define ST_CLK_MUX	2
#define ST_CLK_GATE	3
#define ST_MAX_CLKS	4

static const char * const clk_oscout1_parents[] = { "clk48MHz", "clk25MHz" };
static struct clk_hw *hws[ST_MAX_CLKS];

static int st_clk_probe(struct platform_device *pdev)
{
	struct st_clk_data *st_data;

	st_data = dev_get_platdata(&pdev->dev);
	if (!st_data || !st_data->base)
		return -EINVAL;

	hws[ST_CLK_48M] = clk_hw_register_fixed_rate(NULL, "clk48MHz", NULL, 0,
						     48000000);
	hws[ST_CLK_25M] = clk_hw_register_fixed_rate(NULL, "clk25MHz", NULL, 0,
						     25000000);

	hws[ST_CLK_MUX] = clk_hw_register_mux(NULL, "oscout1_mux",
		clk_oscout1_parents, ARRAY_SIZE(clk_oscout1_parents),
		0, st_data->base + CLKDRVSTR2, OSCOUT1CLK25MHZ, 3, 0, NULL);

	clk_set_parent(hws[ST_CLK_MUX]->clk, hws[ST_CLK_48M]->clk);

	hws[ST_CLK_GATE] = clk_hw_register_gate(NULL, "oscout1", "oscout1_mux",
		0, st_data->base + MISCCLKCNTL1, OSCCLKENB,
		CLK_GATE_SET_TO_DISABLE, NULL);

	clk_hw_register_clkdev(hws[ST_CLK_GATE], "oscout1", NULL);

	return 0;
}

static int st_clk_remove(struct platform_device *pdev)
{
	int i;

	for (i = 0; i < ST_MAX_CLKS; i++)
		clk_hw_unregister(hws[i]);
	return 0;
}

static struct platform_driver st_clk_driver = {
	.driver = {
		.name = "clk-st",
		.suppress_bind_attrs = true,
	},
	.probe = st_clk_probe,
	.remove = st_clk_remove,
};
builtin_platform_driver(st_clk_driver);

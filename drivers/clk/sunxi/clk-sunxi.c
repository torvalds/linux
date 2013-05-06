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
#include <linux/clk/sunxi.h>
#include <linux/of.h>
#include <linux/of_address.h>

#include "clk-factors.h"

static DEFINE_SPINLOCK(clk_lock);

/**
 * sunxi_osc_clk_setup() - Setup function for gatable oscillator
 */

#define SUNXI_OSC24M_GATE	0

static void __init sunxi_osc_clk_setup(struct device_node *node)
{
	struct clk *clk;
	const char *clk_name = node->name;
	const char *parent;
	void *reg;

	reg = of_iomap(node, 0);

	parent = of_clk_get_parent_name(node, 0);

	clk = clk_register_gate(NULL, clk_name, parent, CLK_IGNORE_UNUSED,
				reg, SUNXI_OSC24M_GATE, 0, &clk_lock);

	if (clk) {
		of_clk_add_provider(node, of_clk_src_simple_get, clk);
		clk_register_clkdev(clk, clk_name, NULL);
	}
}



/**
 * sunxi_get_pll1_factors() - calculates n, k, m, p factors for PLL1
 * PLL1 rate is calculated as follows
 * rate = (parent_rate * n * (k + 1) >> p) / (m + 1);
 * parent_rate is always 24Mhz
 */

static void sunxi_get_pll1_factors(u32 *freq, u32 parent_rate,
				   u8 *n, u8 *k, u8 *m, u8 *p)
{
	u8 div;

	/* Normalize value to a 6M multiple */
	div = *freq / 6000000;
	*freq = 6000000 * div;

	/* we were called to round the frequency, we can now return */
	if (n == NULL)
		return;

	/* m is always zero for pll1 */
	*m = 0;

	/* k is 1 only on these cases */
	if (*freq >= 768000000 || *freq == 42000000 || *freq == 54000000)
		*k = 1;
	else
		*k = 0;

	/* p will be 3 for divs under 10 */
	if (div < 10)
		*p = 3;

	/* p will be 2 for divs between 10 - 20 and odd divs under 32 */
	else if (div < 20 || (div < 32 && (div & 1)))
		*p = 2;

	/* p will be 1 for even divs under 32, divs under 40 and odd pairs
	 * of divs between 40-62 */
	else if (div < 40 || (div < 64 && (div & 2)))
		*p = 1;

	/* any other entries have p = 0 */
	else
		*p = 0;

	/* calculate a suitable n based on k and p */
	div <<= *p;
	div /= (*k + 1);
	*n = div / 4;
}



/**
 * sunxi_get_apb1_factors() - calculates m, p factors for APB1
 * APB1 rate is calculated as follows
 * rate = (parent_rate >> p) / (m + 1);
 */

static void sunxi_get_apb1_factors(u32 *freq, u32 parent_rate,
				   u8 *n, u8 *k, u8 *m, u8 *p)
{
	u8 calcm, calcp;

	if (parent_rate < *freq)
		*freq = parent_rate;

	parent_rate = (parent_rate + (*freq - 1)) / *freq;

	/* Invalid rate! */
	if (parent_rate > 32)
		return;

	if (parent_rate <= 4)
		calcp = 0;
	else if (parent_rate <= 8)
		calcp = 1;
	else if (parent_rate <= 16)
		calcp = 2;
	else
		calcp = 3;

	calcm = (parent_rate >> calcp) - 1;

	*freq = (parent_rate >> calcp) / (calcm + 1);

	/* we were called to round the frequency, we can now return */
	if (n == NULL)
		return;

	*m = calcm;
	*p = calcp;
}



/**
 * sunxi_factors_clk_setup() - Setup function for factor clocks
 */

struct factors_data {
	struct clk_factors_config *table;
	void (*getter) (u32 *rate, u32 parent_rate, u8 *n, u8 *k, u8 *m, u8 *p);
};

static struct clk_factors_config pll1_config = {
	.nshift = 8,
	.nwidth = 5,
	.kshift = 4,
	.kwidth = 2,
	.mshift = 0,
	.mwidth = 2,
	.pshift = 16,
	.pwidth = 2,
};

static struct clk_factors_config apb1_config = {
	.mshift = 0,
	.mwidth = 5,
	.pshift = 16,
	.pwidth = 2,
};

static const __initconst struct factors_data pll1_data = {
	.table = &pll1_config,
	.getter = sunxi_get_pll1_factors,
};

static const __initconst struct factors_data apb1_data = {
	.table = &apb1_config,
	.getter = sunxi_get_apb1_factors,
};

static void __init sunxi_factors_clk_setup(struct device_node *node,
					   struct factors_data *data)
{
	struct clk *clk;
	const char *clk_name = node->name;
	const char *parent;
	void *reg;

	reg = of_iomap(node, 0);

	parent = of_clk_get_parent_name(node, 0);

	clk = clk_register_factors(NULL, clk_name, parent, CLK_IGNORE_UNUSED,
				   reg, data->table, data->getter, &clk_lock);

	if (clk) {
		of_clk_add_provider(node, of_clk_src_simple_get, clk);
		clk_register_clkdev(clk, clk_name, NULL);
	}
}



/**
 * sunxi_mux_clk_setup() - Setup function for muxes
 */

#define SUNXI_MUX_GATE_WIDTH	2

struct mux_data {
	u8 shift;
};

static const __initconst struct mux_data cpu_data = {
	.shift = 16,
};

static const __initconst struct mux_data apb1_mux_data = {
	.shift = 24,
};

static void __init sunxi_mux_clk_setup(struct device_node *node,
				       struct mux_data *data)
{
	struct clk *clk;
	const char *clk_name = node->name;
	const char **parents = kmalloc(sizeof(char *) * 5, GFP_KERNEL);
	void *reg;
	int i = 0;

	reg = of_iomap(node, 0);

	while (i < 5 && (parents[i] = of_clk_get_parent_name(node, i)) != NULL)
		i++;

	clk = clk_register_mux(NULL, clk_name, parents, i, 0, reg,
			       data->shift, SUNXI_MUX_GATE_WIDTH,
			       0, &clk_lock);

	if (clk) {
		of_clk_add_provider(node, of_clk_src_simple_get, clk);
		clk_register_clkdev(clk, clk_name, NULL);
	}
}



/**
 * sunxi_divider_clk_setup() - Setup function for simple divider clocks
 */

#define SUNXI_DIVISOR_WIDTH	2

struct div_data {
	u8 shift;
	u8 pow;
};

static const __initconst struct div_data axi_data = {
	.shift = 0,
	.pow = 0,
};

static const __initconst struct div_data ahb_data = {
	.shift = 4,
	.pow = 1,
};

static const __initconst struct div_data apb0_data = {
	.shift = 8,
	.pow = 1,
};

static void __init sunxi_divider_clk_setup(struct device_node *node,
					   struct div_data *data)
{
	struct clk *clk;
	const char *clk_name = node->name;
	const char *clk_parent;
	void *reg;

	reg = of_iomap(node, 0);

	clk_parent = of_clk_get_parent_name(node, 0);

	clk = clk_register_divider(NULL, clk_name, clk_parent, 0,
				   reg, data->shift, SUNXI_DIVISOR_WIDTH,
				   data->pow ? CLK_DIVIDER_POWER_OF_TWO : 0,
				   &clk_lock);
	if (clk) {
		of_clk_add_provider(node, of_clk_src_simple_get, clk);
		clk_register_clkdev(clk, clk_name, NULL);
	}
}


/* Matches for of_clk_init */
static const __initconst struct of_device_id clk_match[] = {
	{.compatible = "fixed-clock", .data = of_fixed_clk_setup,},
	{.compatible = "allwinner,sun4i-osc-clk", .data = sunxi_osc_clk_setup,},
	{}
};

/* Matches for factors clocks */
static const __initconst struct of_device_id clk_factors_match[] = {
	{.compatible = "allwinner,sun4i-pll1-clk", .data = &pll1_data,},
	{.compatible = "allwinner,sun4i-apb1-clk", .data = &apb1_data,},
	{}
};

/* Matches for divider clocks */
static const __initconst struct of_device_id clk_div_match[] = {
	{.compatible = "allwinner,sun4i-axi-clk", .data = &axi_data,},
	{.compatible = "allwinner,sun4i-ahb-clk", .data = &ahb_data,},
	{.compatible = "allwinner,sun4i-apb0-clk", .data = &apb0_data,},
	{}
};

/* Matches for mux clocks */
static const __initconst struct of_device_id clk_mux_match[] = {
	{.compatible = "allwinner,sun4i-cpu-clk", .data = &cpu_data,},
	{.compatible = "allwinner,sun4i-apb1-mux-clk", .data = &apb1_mux_data,},
	{}
};

static void __init of_sunxi_table_clock_setup(const struct of_device_id *clk_match,
					      void *function)
{
	struct device_node *np;
	const struct div_data *data;
	const struct of_device_id *match;
	void (*setup_function)(struct device_node *, const void *) = function;

	for_each_matching_node(np, clk_match) {
		match = of_match_node(clk_match, np);
		data = match->data;
		setup_function(np, data);
	}
}

void __init sunxi_init_clocks(void)
{
	/* Register all the simple sunxi clocks on DT */
	of_clk_init(clk_match);

	/* Register factor clocks */
	of_sunxi_table_clock_setup(clk_factors_match, sunxi_factors_clk_setup);

	/* Register divider clocks */
	of_sunxi_table_clock_setup(clk_div_match, sunxi_divider_clk_setup);

	/* Register mux clocks */
	of_sunxi_table_clock_setup(clk_mux_match, sunxi_mux_clk_setup);
}

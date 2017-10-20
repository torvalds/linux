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

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/clkdev.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/reset-controller.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/log2.h>

#include "clk-factors.h"

static DEFINE_SPINLOCK(clk_lock);

/* Maximum number of parents our clocks have */
#define SUNXI_MAX_PARENTS	5

/**
 * sun4i_get_pll1_factors() - calculates n, k, m, p factors for PLL1
 * PLL1 rate is calculated as follows
 * rate = (parent_rate * n * (k + 1) >> p) / (m + 1);
 * parent_rate is always 24Mhz
 */

static void sun4i_get_pll1_factors(struct factors_request *req)
{
	u8 div;

	/* Normalize value to a 6M multiple */
	div = req->rate / 6000000;
	req->rate = 6000000 * div;

	/* m is always zero for pll1 */
	req->m = 0;

	/* k is 1 only on these cases */
	if (req->rate >= 768000000 || req->rate == 42000000 ||
			req->rate == 54000000)
		req->k = 1;
	else
		req->k = 0;

	/* p will be 3 for divs under 10 */
	if (div < 10)
		req->p = 3;

	/* p will be 2 for divs between 10 - 20 and odd divs under 32 */
	else if (div < 20 || (div < 32 && (div & 1)))
		req->p = 2;

	/* p will be 1 for even divs under 32, divs under 40 and odd pairs
	 * of divs between 40-62 */
	else if (div < 40 || (div < 64 && (div & 2)))
		req->p = 1;

	/* any other entries have p = 0 */
	else
		req->p = 0;

	/* calculate a suitable n based on k and p */
	div <<= req->p;
	div /= (req->k + 1);
	req->n = div / 4;
}

/**
 * sun6i_a31_get_pll1_factors() - calculates n, k and m factors for PLL1
 * PLL1 rate is calculated as follows
 * rate = parent_rate * (n + 1) * (k + 1) / (m + 1);
 * parent_rate should always be 24MHz
 */
static void sun6i_a31_get_pll1_factors(struct factors_request *req)
{
	/*
	 * We can operate only on MHz, this will make our life easier
	 * later.
	 */
	u32 freq_mhz = req->rate / 1000000;
	u32 parent_freq_mhz = req->parent_rate / 1000000;

	/*
	 * Round down the frequency to the closest multiple of either
	 * 6 or 16
	 */
	u32 round_freq_6 = round_down(freq_mhz, 6);
	u32 round_freq_16 = round_down(freq_mhz, 16);

	if (round_freq_6 > round_freq_16)
		freq_mhz = round_freq_6;
	else
		freq_mhz = round_freq_16;

	req->rate = freq_mhz * 1000000;

	/* If the frequency is a multiple of 32 MHz, k is always 3 */
	if (!(freq_mhz % 32))
		req->k = 3;
	/* If the frequency is a multiple of 9 MHz, k is always 2 */
	else if (!(freq_mhz % 9))
		req->k = 2;
	/* If the frequency is a multiple of 8 MHz, k is always 1 */
	else if (!(freq_mhz % 8))
		req->k = 1;
	/* Otherwise, we don't use the k factor */
	else
		req->k = 0;

	/*
	 * If the frequency is a multiple of 2 but not a multiple of
	 * 3, m is 3. This is the first time we use 6 here, yet we
	 * will use it on several other places.
	 * We use this number because it's the lowest frequency we can
	 * generate (with n = 0, k = 0, m = 3), so every other frequency
	 * somehow relates to this frequency.
	 */
	if ((freq_mhz % 6) == 2 || (freq_mhz % 6) == 4)
		req->m = 2;
	/*
	 * If the frequency is a multiple of 6MHz, but the factor is
	 * odd, m will be 3
	 */
	else if ((freq_mhz / 6) & 1)
		req->m = 3;
	/* Otherwise, we end up with m = 1 */
	else
		req->m = 1;

	/* Calculate n thanks to the above factors we already got */
	req->n = freq_mhz * (req->m + 1) / ((req->k + 1) * parent_freq_mhz)
		 - 1;

	/*
	 * If n end up being outbound, and that we can still decrease
	 * m, do it.
	 */
	if ((req->n + 1) > 31 && (req->m + 1) > 1) {
		req->n = (req->n + 1) / 2 - 1;
		req->m = (req->m + 1) / 2 - 1;
	}
}

/**
 * sun8i_a23_get_pll1_factors() - calculates n, k, m, p factors for PLL1
 * PLL1 rate is calculated as follows
 * rate = (parent_rate * (n + 1) * (k + 1) >> p) / (m + 1);
 * parent_rate is always 24Mhz
 */

static void sun8i_a23_get_pll1_factors(struct factors_request *req)
{
	u8 div;

	/* Normalize value to a 6M multiple */
	div = req->rate / 6000000;
	req->rate = 6000000 * div;

	/* m is always zero for pll1 */
	req->m = 0;

	/* k is 1 only on these cases */
	if (req->rate >= 768000000 || req->rate == 42000000 ||
			req->rate == 54000000)
		req->k = 1;
	else
		req->k = 0;

	/* p will be 2 for divs under 20 and odd divs under 32 */
	if (div < 20 || (div < 32 && (div & 1)))
		req->p = 2;

	/* p will be 1 for even divs under 32, divs under 40 and odd pairs
	 * of divs between 40-62 */
	else if (div < 40 || (div < 64 && (div & 2)))
		req->p = 1;

	/* any other entries have p = 0 */
	else
		req->p = 0;

	/* calculate a suitable n based on k and p */
	div <<= req->p;
	div /= (req->k + 1);
	req->n = div / 4 - 1;
}

/**
 * sun4i_get_pll5_factors() - calculates n, k factors for PLL5
 * PLL5 rate is calculated as follows
 * rate = parent_rate * n * (k + 1)
 * parent_rate is always 24Mhz
 */

static void sun4i_get_pll5_factors(struct factors_request *req)
{
	u8 div;

	/* Normalize value to a parent_rate multiple (24M) */
	div = req->rate / req->parent_rate;
	req->rate = req->parent_rate * div;

	if (div < 31)
		req->k = 0;
	else if (div / 2 < 31)
		req->k = 1;
	else if (div / 3 < 31)
		req->k = 2;
	else
		req->k = 3;

	req->n = DIV_ROUND_UP(div, (req->k + 1));
}

/**
 * sun6i_a31_get_pll6_factors() - calculates n, k factors for A31 PLL6x2
 * PLL6x2 rate is calculated as follows
 * rate = parent_rate * (n + 1) * (k + 1)
 * parent_rate is always 24Mhz
 */

static void sun6i_a31_get_pll6_factors(struct factors_request *req)
{
	u8 div;

	/* Normalize value to a parent_rate multiple (24M) */
	div = req->rate / req->parent_rate;
	req->rate = req->parent_rate * div;

	req->k = div / 32;
	if (req->k > 3)
		req->k = 3;

	req->n = DIV_ROUND_UP(div, (req->k + 1)) - 1;
}

/**
 * sun5i_a13_get_ahb_factors() - calculates m, p factors for AHB
 * AHB rate is calculated as follows
 * rate = parent_rate >> p
 */

static void sun5i_a13_get_ahb_factors(struct factors_request *req)
{
	u32 div;

	/* divide only */
	if (req->parent_rate < req->rate)
		req->rate = req->parent_rate;

	/*
	 * user manual says valid speed is 8k ~ 276M, but tests show it
	 * can work at speeds up to 300M, just after reparenting to pll6
	 */
	if (req->rate < 8000)
		req->rate = 8000;
	if (req->rate > 300000000)
		req->rate = 300000000;

	div = order_base_2(DIV_ROUND_UP(req->parent_rate, req->rate));

	/* p = 0 ~ 3 */
	if (div > 3)
		div = 3;

	req->rate = req->parent_rate >> div;

	req->p = div;
}

#define SUN6I_AHB1_PARENT_PLL6	3

/**
 * sun6i_a31_get_ahb_factors() - calculates m, p factors for AHB
 * AHB rate is calculated as follows
 * rate = parent_rate >> p
 *
 * if parent is pll6, then
 * parent_rate = pll6 rate / (m + 1)
 */

static void sun6i_get_ahb1_factors(struct factors_request *req)
{
	u8 div, calcp, calcm = 1;

	/*
	 * clock can only divide, so we will never be able to achieve
	 * frequencies higher than the parent frequency
	 */
	if (req->parent_rate && req->rate > req->parent_rate)
		req->rate = req->parent_rate;

	div = DIV_ROUND_UP(req->parent_rate, req->rate);

	/* calculate pre-divider if parent is pll6 */
	if (req->parent_index == SUN6I_AHB1_PARENT_PLL6) {
		if (div < 4)
			calcp = 0;
		else if (div / 2 < 4)
			calcp = 1;
		else if (div / 4 < 4)
			calcp = 2;
		else
			calcp = 3;

		calcm = DIV_ROUND_UP(div, 1 << calcp);
	} else {
		calcp = __roundup_pow_of_two(div);
		calcp = calcp > 3 ? 3 : calcp;
	}

	req->rate = (req->parent_rate / calcm) >> calcp;
	req->p = calcp;
	req->m = calcm - 1;
}

/**
 * sun6i_ahb1_recalc() - calculates AHB clock rate from m, p factors and
 *			 parent index
 */
static void sun6i_ahb1_recalc(struct factors_request *req)
{
	req->rate = req->parent_rate;

	/* apply pre-divider first if parent is pll6 */
	if (req->parent_index == SUN6I_AHB1_PARENT_PLL6)
		req->rate /= req->m + 1;

	/* clk divider */
	req->rate >>= req->p;
}

/**
 * sun4i_get_apb1_factors() - calculates m, p factors for APB1
 * APB1 rate is calculated as follows
 * rate = (parent_rate >> p) / (m + 1);
 */

static void sun4i_get_apb1_factors(struct factors_request *req)
{
	u8 calcm, calcp;
	int div;

	if (req->parent_rate < req->rate)
		req->rate = req->parent_rate;

	div = DIV_ROUND_UP(req->parent_rate, req->rate);

	/* Invalid rate! */
	if (div > 32)
		return;

	if (div <= 4)
		calcp = 0;
	else if (div <= 8)
		calcp = 1;
	else if (div <= 16)
		calcp = 2;
	else
		calcp = 3;

	calcm = (div >> calcp) - 1;

	req->rate = (req->parent_rate >> calcp) / (calcm + 1);
	req->m = calcm;
	req->p = calcp;
}




/**
 * sun7i_a20_get_out_factors() - calculates m, p factors for CLK_OUT_A/B
 * CLK_OUT rate is calculated as follows
 * rate = (parent_rate >> p) / (m + 1);
 */

static void sun7i_a20_get_out_factors(struct factors_request *req)
{
	u8 div, calcm, calcp;

	/* These clocks can only divide, so we will never be able to achieve
	 * frequencies higher than the parent frequency */
	if (req->rate > req->parent_rate)
		req->rate = req->parent_rate;

	div = DIV_ROUND_UP(req->parent_rate, req->rate);

	if (div < 32)
		calcp = 0;
	else if (div / 2 < 32)
		calcp = 1;
	else if (div / 4 < 32)
		calcp = 2;
	else
		calcp = 3;

	calcm = DIV_ROUND_UP(div, 1 << calcp);

	req->rate = (req->parent_rate >> calcp) / calcm;
	req->m = calcm - 1;
	req->p = calcp;
}

/**
 * sunxi_factors_clk_setup() - Setup function for factor clocks
 */

static const struct clk_factors_config sun4i_pll1_config = {
	.nshift = 8,
	.nwidth = 5,
	.kshift = 4,
	.kwidth = 2,
	.mshift = 0,
	.mwidth = 2,
	.pshift = 16,
	.pwidth = 2,
};

static const struct clk_factors_config sun6i_a31_pll1_config = {
	.nshift	= 8,
	.nwidth = 5,
	.kshift = 4,
	.kwidth = 2,
	.mshift = 0,
	.mwidth = 2,
	.n_start = 1,
};

static const struct clk_factors_config sun8i_a23_pll1_config = {
	.nshift = 8,
	.nwidth = 5,
	.kshift = 4,
	.kwidth = 2,
	.mshift = 0,
	.mwidth = 2,
	.pshift = 16,
	.pwidth = 2,
	.n_start = 1,
};

static const struct clk_factors_config sun4i_pll5_config = {
	.nshift = 8,
	.nwidth = 5,
	.kshift = 4,
	.kwidth = 2,
};

static const struct clk_factors_config sun6i_a31_pll6_config = {
	.nshift	= 8,
	.nwidth = 5,
	.kshift = 4,
	.kwidth = 2,
	.n_start = 1,
};

static const struct clk_factors_config sun5i_a13_ahb_config = {
	.pshift = 4,
	.pwidth = 2,
};

static const struct clk_factors_config sun6i_ahb1_config = {
	.mshift = 6,
	.mwidth = 2,
	.pshift = 4,
	.pwidth = 2,
};

static const struct clk_factors_config sun4i_apb1_config = {
	.mshift = 0,
	.mwidth = 5,
	.pshift = 16,
	.pwidth = 2,
};

/* user manual says "n" but it's really "p" */
static const struct clk_factors_config sun7i_a20_out_config = {
	.mshift = 8,
	.mwidth = 5,
	.pshift = 20,
	.pwidth = 2,
};

static const struct factors_data sun4i_pll1_data __initconst = {
	.enable = 31,
	.table = &sun4i_pll1_config,
	.getter = sun4i_get_pll1_factors,
};

static const struct factors_data sun6i_a31_pll1_data __initconst = {
	.enable = 31,
	.table = &sun6i_a31_pll1_config,
	.getter = sun6i_a31_get_pll1_factors,
};

static const struct factors_data sun8i_a23_pll1_data __initconst = {
	.enable = 31,
	.table = &sun8i_a23_pll1_config,
	.getter = sun8i_a23_get_pll1_factors,
};

static const struct factors_data sun7i_a20_pll4_data __initconst = {
	.enable = 31,
	.table = &sun4i_pll5_config,
	.getter = sun4i_get_pll5_factors,
};

static const struct factors_data sun4i_pll5_data __initconst = {
	.enable = 31,
	.table = &sun4i_pll5_config,
	.getter = sun4i_get_pll5_factors,
};

static const struct factors_data sun6i_a31_pll6_data __initconst = {
	.enable = 31,
	.table = &sun6i_a31_pll6_config,
	.getter = sun6i_a31_get_pll6_factors,
};

static const struct factors_data sun5i_a13_ahb_data __initconst = {
	.mux = 6,
	.muxmask = BIT(1) | BIT(0),
	.table = &sun5i_a13_ahb_config,
	.getter = sun5i_a13_get_ahb_factors,
};

static const struct factors_data sun6i_ahb1_data __initconst = {
	.mux = 12,
	.muxmask = BIT(1) | BIT(0),
	.table = &sun6i_ahb1_config,
	.getter = sun6i_get_ahb1_factors,
	.recalc = sun6i_ahb1_recalc,
};

static const struct factors_data sun4i_apb1_data __initconst = {
	.mux = 24,
	.muxmask = BIT(1) | BIT(0),
	.table = &sun4i_apb1_config,
	.getter = sun4i_get_apb1_factors,
};

static const struct factors_data sun7i_a20_out_data __initconst = {
	.enable = 31,
	.mux = 24,
	.muxmask = BIT(1) | BIT(0),
	.table = &sun7i_a20_out_config,
	.getter = sun7i_a20_get_out_factors,
};

static struct clk * __init sunxi_factors_clk_setup(struct device_node *node,
						   const struct factors_data *data)
{
	void __iomem *reg;

	reg = of_iomap(node, 0);
	if (!reg) {
		pr_err("Could not get registers for factors-clk: %s\n",
		       node->name);
		return NULL;
	}

	return sunxi_factors_register(node, data, &clk_lock, reg);
}

static void __init sun4i_pll1_clk_setup(struct device_node *node)
{
	sunxi_factors_clk_setup(node, &sun4i_pll1_data);
}
CLK_OF_DECLARE(sun4i_pll1, "allwinner,sun4i-a10-pll1-clk",
	       sun4i_pll1_clk_setup);

static void __init sun6i_pll1_clk_setup(struct device_node *node)
{
	sunxi_factors_clk_setup(node, &sun6i_a31_pll1_data);
}
CLK_OF_DECLARE(sun6i_pll1, "allwinner,sun6i-a31-pll1-clk",
	       sun6i_pll1_clk_setup);

static void __init sun8i_pll1_clk_setup(struct device_node *node)
{
	sunxi_factors_clk_setup(node, &sun8i_a23_pll1_data);
}
CLK_OF_DECLARE(sun8i_pll1, "allwinner,sun8i-a23-pll1-clk",
	       sun8i_pll1_clk_setup);

static void __init sun7i_pll4_clk_setup(struct device_node *node)
{
	sunxi_factors_clk_setup(node, &sun7i_a20_pll4_data);
}
CLK_OF_DECLARE(sun7i_pll4, "allwinner,sun7i-a20-pll4-clk",
	       sun7i_pll4_clk_setup);

static void __init sun5i_ahb_clk_setup(struct device_node *node)
{
	sunxi_factors_clk_setup(node, &sun5i_a13_ahb_data);
}
CLK_OF_DECLARE(sun5i_ahb, "allwinner,sun5i-a13-ahb-clk",
	       sun5i_ahb_clk_setup);

static void __init sun6i_ahb1_clk_setup(struct device_node *node)
{
	sunxi_factors_clk_setup(node, &sun6i_ahb1_data);
}
CLK_OF_DECLARE(sun6i_a31_ahb1, "allwinner,sun6i-a31-ahb1-clk",
	       sun6i_ahb1_clk_setup);

static void __init sun4i_apb1_clk_setup(struct device_node *node)
{
	sunxi_factors_clk_setup(node, &sun4i_apb1_data);
}
CLK_OF_DECLARE(sun4i_apb1, "allwinner,sun4i-a10-apb1-clk",
	       sun4i_apb1_clk_setup);

static void __init sun7i_out_clk_setup(struct device_node *node)
{
	sunxi_factors_clk_setup(node, &sun7i_a20_out_data);
}
CLK_OF_DECLARE(sun7i_out, "allwinner,sun7i-a20-out-clk",
	       sun7i_out_clk_setup);


/**
 * sunxi_mux_clk_setup() - Setup function for muxes
 */

#define SUNXI_MUX_GATE_WIDTH	2

struct mux_data {
	u8 shift;
};

static const struct mux_data sun4i_cpu_mux_data __initconst = {
	.shift = 16,
};

static const struct mux_data sun6i_a31_ahb1_mux_data __initconst = {
	.shift = 12,
};

static const struct mux_data sun8i_h3_ahb2_mux_data __initconst = {
	.shift = 0,
};

static struct clk * __init sunxi_mux_clk_setup(struct device_node *node,
					       const struct mux_data *data)
{
	struct clk *clk;
	const char *clk_name = node->name;
	const char *parents[SUNXI_MAX_PARENTS];
	void __iomem *reg;
	int i;

	reg = of_iomap(node, 0);
	if (!reg) {
		pr_err("Could not map registers for mux-clk: %pOF\n", node);
		return NULL;
	}

	i = of_clk_parent_fill(node, parents, SUNXI_MAX_PARENTS);
	if (of_property_read_string(node, "clock-output-names", &clk_name)) {
		pr_err("%s: could not read clock-output-names from \"%pOF\"\n",
		       __func__, node);
		goto out_unmap;
	}

	clk = clk_register_mux(NULL, clk_name, parents, i,
			       CLK_SET_RATE_PARENT, reg,
			       data->shift, SUNXI_MUX_GATE_WIDTH,
			       0, &clk_lock);

	if (IS_ERR(clk)) {
		pr_err("%s: failed to register mux clock %s: %ld\n", __func__,
		       clk_name, PTR_ERR(clk));
		goto out_unmap;
	}

	if (of_clk_add_provider(node, of_clk_src_simple_get, clk)) {
		pr_err("%s: failed to add clock provider for %s\n",
		       __func__, clk_name);
		clk_unregister_divider(clk);
		goto out_unmap;
	}

	return clk;
out_unmap:
	iounmap(reg);
	return NULL;
}

static void __init sun4i_cpu_clk_setup(struct device_node *node)
{
	struct clk *clk;

	clk = sunxi_mux_clk_setup(node, &sun4i_cpu_mux_data);
	if (!clk)
		return;

	/* Protect CPU clock */
	__clk_get(clk);
	clk_prepare_enable(clk);
}
CLK_OF_DECLARE(sun4i_cpu, "allwinner,sun4i-a10-cpu-clk",
	       sun4i_cpu_clk_setup);

static void __init sun6i_ahb1_mux_clk_setup(struct device_node *node)
{
	sunxi_mux_clk_setup(node, &sun6i_a31_ahb1_mux_data);
}
CLK_OF_DECLARE(sun6i_ahb1_mux, "allwinner,sun6i-a31-ahb1-mux-clk",
	       sun6i_ahb1_mux_clk_setup);

static void __init sun8i_ahb2_clk_setup(struct device_node *node)
{
	sunxi_mux_clk_setup(node, &sun8i_h3_ahb2_mux_data);
}
CLK_OF_DECLARE(sun8i_ahb2, "allwinner,sun8i-h3-ahb2-clk",
	       sun8i_ahb2_clk_setup);


/**
 * sunxi_divider_clk_setup() - Setup function for simple divider clocks
 */

struct div_data {
	u8	shift;
	u8	pow;
	u8	width;
	const struct clk_div_table *table;
};

static const struct div_data sun4i_axi_data __initconst = {
	.shift	= 0,
	.pow	= 0,
	.width	= 2,
};

static const struct clk_div_table sun8i_a23_axi_table[] __initconst = {
	{ .val = 0, .div = 1 },
	{ .val = 1, .div = 2 },
	{ .val = 2, .div = 3 },
	{ .val = 3, .div = 4 },
	{ .val = 4, .div = 4 },
	{ .val = 5, .div = 4 },
	{ .val = 6, .div = 4 },
	{ .val = 7, .div = 4 },
	{ } /* sentinel */
};

static const struct div_data sun8i_a23_axi_data __initconst = {
	.width	= 3,
	.table	= sun8i_a23_axi_table,
};

static const struct div_data sun4i_ahb_data __initconst = {
	.shift	= 4,
	.pow	= 1,
	.width	= 2,
};

static const struct clk_div_table sun4i_apb0_table[] __initconst = {
	{ .val = 0, .div = 2 },
	{ .val = 1, .div = 2 },
	{ .val = 2, .div = 4 },
	{ .val = 3, .div = 8 },
	{ } /* sentinel */
};

static const struct div_data sun4i_apb0_data __initconst = {
	.shift	= 8,
	.pow	= 1,
	.width	= 2,
	.table	= sun4i_apb0_table,
};

static void __init sunxi_divider_clk_setup(struct device_node *node,
					   const struct div_data *data)
{
	struct clk *clk;
	const char *clk_name = node->name;
	const char *clk_parent;
	void __iomem *reg;

	reg = of_iomap(node, 0);
	if (!reg) {
		pr_err("Could not map registers for mux-clk: %pOF\n", node);
		return;
	}

	clk_parent = of_clk_get_parent_name(node, 0);

	if (of_property_read_string(node, "clock-output-names", &clk_name)) {
		pr_err("%s: could not read clock-output-names from \"%pOF\"\n",
		       __func__, node);
		goto out_unmap;
	}

	clk = clk_register_divider_table(NULL, clk_name, clk_parent, 0,
					 reg, data->shift, data->width,
					 data->pow ? CLK_DIVIDER_POWER_OF_TWO : 0,
					 data->table, &clk_lock);
	if (IS_ERR(clk)) {
		pr_err("%s: failed to register divider clock %s: %ld\n",
		       __func__, clk_name, PTR_ERR(clk));
		goto out_unmap;
	}

	if (of_clk_add_provider(node, of_clk_src_simple_get, clk)) {
		pr_err("%s: failed to add clock provider for %s\n",
		       __func__, clk_name);
		goto out_unregister;
	}

	if (clk_register_clkdev(clk, clk_name, NULL)) {
		of_clk_del_provider(node);
		goto out_unregister;
	}

	return;
out_unregister:
	clk_unregister_divider(clk);

out_unmap:
	iounmap(reg);
}

static void __init sun4i_ahb_clk_setup(struct device_node *node)
{
	sunxi_divider_clk_setup(node, &sun4i_ahb_data);
}
CLK_OF_DECLARE(sun4i_ahb, "allwinner,sun4i-a10-ahb-clk",
	       sun4i_ahb_clk_setup);

static void __init sun4i_apb0_clk_setup(struct device_node *node)
{
	sunxi_divider_clk_setup(node, &sun4i_apb0_data);
}
CLK_OF_DECLARE(sun4i_apb0, "allwinner,sun4i-a10-apb0-clk",
	       sun4i_apb0_clk_setup);

static void __init sun4i_axi_clk_setup(struct device_node *node)
{
	sunxi_divider_clk_setup(node, &sun4i_axi_data);
}
CLK_OF_DECLARE(sun4i_axi, "allwinner,sun4i-a10-axi-clk",
	       sun4i_axi_clk_setup);

static void __init sun8i_axi_clk_setup(struct device_node *node)
{
	sunxi_divider_clk_setup(node, &sun8i_a23_axi_data);
}
CLK_OF_DECLARE(sun8i_axi, "allwinner,sun8i-a23-axi-clk",
	       sun8i_axi_clk_setup);



/**
 * sunxi_gates_clk_setup() - Setup function for leaf gates on clocks
 */

#define SUNXI_GATES_MAX_SIZE	64

struct gates_data {
	DECLARE_BITMAP(mask, SUNXI_GATES_MAX_SIZE);
};

/**
 * sunxi_divs_clk_setup() helper data
 */

#define SUNXI_DIVS_MAX_QTY	4
#define SUNXI_DIVISOR_WIDTH	2

struct divs_data {
	const struct factors_data *factors; /* data for the factor clock */
	int ndivs; /* number of outputs */
	/*
	 * List of outputs. Refer to the diagram for sunxi_divs_clk_setup():
	 * self or base factor clock refers to the output from the pll
	 * itself. The remaining refer to fixed or configurable divider
	 * outputs.
	 */
	struct {
		u8 self; /* is it the base factor clock? (only one) */
		u8 fixed; /* is it a fixed divisor? if not... */
		struct clk_div_table *table; /* is it a table based divisor? */
		u8 shift; /* otherwise it's a normal divisor with this shift */
		u8 pow;   /* is it power-of-two based? */
		u8 gate;  /* is it independently gateable? */
	} div[SUNXI_DIVS_MAX_QTY];
};

static struct clk_div_table pll6_sata_tbl[] = {
	{ .val = 0, .div = 6, },
	{ .val = 1, .div = 12, },
	{ .val = 2, .div = 18, },
	{ .val = 3, .div = 24, },
	{ } /* sentinel */
};

static const struct divs_data pll5_divs_data __initconst = {
	.factors = &sun4i_pll5_data,
	.ndivs = 2,
	.div = {
		{ .shift = 0, .pow = 0, }, /* M, DDR */
		{ .shift = 16, .pow = 1, }, /* P, other */
		/* No output for the base factor clock */
	}
};

static const struct divs_data pll6_divs_data __initconst = {
	.factors = &sun4i_pll5_data,
	.ndivs = 4,
	.div = {
		{ .shift = 0, .table = pll6_sata_tbl, .gate = 14 }, /* M, SATA */
		{ .fixed = 2 }, /* P, other */
		{ .self = 1 }, /* base factor clock, 2x */
		{ .fixed = 4 }, /* pll6 / 4, used as ahb input */
	}
};

static const struct divs_data sun6i_a31_pll6_divs_data __initconst = {
	.factors = &sun6i_a31_pll6_data,
	.ndivs = 2,
	.div = {
		{ .fixed = 2 }, /* normal output */
		{ .self = 1 }, /* base factor clock, 2x */
	}
};

/**
 * sunxi_divs_clk_setup() - Setup function for leaf divisors on clocks
 *
 * These clocks look something like this
 *            ________________________
 *           |         ___divisor 1---|----> to consumer
 * parent >--|  pll___/___divisor 2---|----> to consumer
 *           |        \_______________|____> to consumer
 *           |________________________|
 */

static struct clk ** __init sunxi_divs_clk_setup(struct device_node *node,
						 const struct divs_data *data)
{
	struct clk_onecell_data *clk_data;
	const char *parent;
	const char *clk_name;
	struct clk **clks, *pclk;
	struct clk_hw *gate_hw, *rate_hw;
	const struct clk_ops *rate_ops;
	struct clk_gate *gate = NULL;
	struct clk_fixed_factor *fix_factor;
	struct clk_divider *divider;
	struct factors_data factors = *data->factors;
	char *derived_name = NULL;
	void __iomem *reg;
	int ndivs = SUNXI_DIVS_MAX_QTY, i = 0;
	int flags, clkflags;

	/* if number of children known, use it */
	if (data->ndivs)
		ndivs = data->ndivs;

	/* Try to find a name for base factor clock */
	for (i = 0; i < ndivs; i++) {
		if (data->div[i].self) {
			of_property_read_string_index(node, "clock-output-names",
						      i, &factors.name);
			break;
		}
	}
	/* If we don't have a .self clk use the first output-name up to '_' */
	if (factors.name == NULL) {
		char *endp;

		of_property_read_string_index(node, "clock-output-names",
						      0, &clk_name);
		endp = strchr(clk_name, '_');
		if (endp) {
			derived_name = kstrndup(clk_name, endp - clk_name,
						GFP_KERNEL);
			factors.name = derived_name;
		} else {
			factors.name = clk_name;
		}
	}

	/* Set up factor clock that we will be dividing */
	pclk = sunxi_factors_clk_setup(node, &factors);
	if (!pclk)
		return NULL;

	parent = __clk_get_name(pclk);
	kfree(derived_name);

	reg = of_iomap(node, 0);
	if (!reg) {
		pr_err("Could not map registers for divs-clk: %pOF\n", node);
		return NULL;
	}

	clk_data = kmalloc(sizeof(struct clk_onecell_data), GFP_KERNEL);
	if (!clk_data)
		goto out_unmap;

	clks = kcalloc(ndivs, sizeof(*clks), GFP_KERNEL);
	if (!clks)
		goto free_clkdata;

	clk_data->clks = clks;

	/* It's not a good idea to have automatic reparenting changing
	 * our RAM clock! */
	clkflags = !strcmp("pll5", parent) ? 0 : CLK_SET_RATE_PARENT;

	for (i = 0; i < ndivs; i++) {
		if (of_property_read_string_index(node, "clock-output-names",
						  i, &clk_name) != 0)
			break;

		/* If this is the base factor clock, only update clks */
		if (data->div[i].self) {
			clk_data->clks[i] = pclk;
			continue;
		}

		gate_hw = NULL;
		rate_hw = NULL;
		rate_ops = NULL;

		/* If this leaf clock can be gated, create a gate */
		if (data->div[i].gate) {
			gate = kzalloc(sizeof(*gate), GFP_KERNEL);
			if (!gate)
				goto free_clks;

			gate->reg = reg;
			gate->bit_idx = data->div[i].gate;
			gate->lock = &clk_lock;

			gate_hw = &gate->hw;
		}

		/* Leaves can be fixed or configurable divisors */
		if (data->div[i].fixed) {
			fix_factor = kzalloc(sizeof(*fix_factor), GFP_KERNEL);
			if (!fix_factor)
				goto free_gate;

			fix_factor->mult = 1;
			fix_factor->div = data->div[i].fixed;

			rate_hw = &fix_factor->hw;
			rate_ops = &clk_fixed_factor_ops;
		} else {
			divider = kzalloc(sizeof(*divider), GFP_KERNEL);
			if (!divider)
				goto free_gate;

			flags = data->div[i].pow ? CLK_DIVIDER_POWER_OF_TWO : 0;

			divider->reg = reg;
			divider->shift = data->div[i].shift;
			divider->width = SUNXI_DIVISOR_WIDTH;
			divider->flags = flags;
			divider->lock = &clk_lock;
			divider->table = data->div[i].table;

			rate_hw = &divider->hw;
			rate_ops = &clk_divider_ops;
		}

		/* Wrap the (potential) gate and the divisor on a composite
		 * clock to unify them */
		clks[i] = clk_register_composite(NULL, clk_name, &parent, 1,
						 NULL, NULL,
						 rate_hw, rate_ops,
						 gate_hw, &clk_gate_ops,
						 clkflags);

		WARN_ON(IS_ERR(clk_data->clks[i]));
	}

	/* Adjust to the real max */
	clk_data->clk_num = i;

	if (of_clk_add_provider(node, of_clk_src_onecell_get, clk_data)) {
		pr_err("%s: failed to add clock provider for %s\n",
		       __func__, clk_name);
		goto free_gate;
	}

	return clks;
free_gate:
	kfree(gate);
free_clks:
	kfree(clks);
free_clkdata:
	kfree(clk_data);
out_unmap:
	iounmap(reg);
	return NULL;
}

static void __init sun4i_pll5_clk_setup(struct device_node *node)
{
	struct clk **clks;

	clks = sunxi_divs_clk_setup(node, &pll5_divs_data);
	if (!clks)
		return;

	/* Protect PLL5_DDR */
	__clk_get(clks[0]);
	clk_prepare_enable(clks[0]);
}
CLK_OF_DECLARE(sun4i_pll5, "allwinner,sun4i-a10-pll5-clk",
	       sun4i_pll5_clk_setup);

static void __init sun4i_pll6_clk_setup(struct device_node *node)
{
	sunxi_divs_clk_setup(node, &pll6_divs_data);
}
CLK_OF_DECLARE(sun4i_pll6, "allwinner,sun4i-a10-pll6-clk",
	       sun4i_pll6_clk_setup);

static void __init sun6i_pll6_clk_setup(struct device_node *node)
{
	sunxi_divs_clk_setup(node, &sun6i_a31_pll6_divs_data);
}
CLK_OF_DECLARE(sun6i_pll6, "allwinner,sun6i-a31-pll6-clk",
	       sun6i_pll6_clk_setup);

/*
 * sun6i display
 *
 * rate = parent_rate / (m + 1);
 */
static void sun6i_display_factors(struct factors_request *req)
{
	u8 m;

	if (req->rate > req->parent_rate)
		req->rate = req->parent_rate;

	m = DIV_ROUND_UP(req->parent_rate, req->rate);

	req->rate = req->parent_rate / m;
	req->m = m - 1;
}

static const struct clk_factors_config sun6i_display_config = {
	.mshift = 0,
	.mwidth = 4,
};

static const struct factors_data sun6i_display_data __initconst = {
	.enable = 31,
	.mux = 24,
	.muxmask = BIT(2) | BIT(1) | BIT(0),
	.table = &sun6i_display_config,
	.getter = sun6i_display_factors,
};

static void __init sun6i_display_setup(struct device_node *node)
{
	sunxi_factors_clk_setup(node, &sun6i_display_data);
}
CLK_OF_DECLARE(sun6i_display, "allwinner,sun6i-a31-display-clk",
	       sun6i_display_setup);

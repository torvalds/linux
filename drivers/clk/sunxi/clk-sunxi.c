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
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/reset-controller.h>
#include <linux/spinlock.h>

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

static void sun4i_get_pll1_factors(u32 *freq, u32 parent_rate,
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
 * sun6i_a31_get_pll1_factors() - calculates n, k and m factors for PLL1
 * PLL1 rate is calculated as follows
 * rate = parent_rate * (n + 1) * (k + 1) / (m + 1);
 * parent_rate should always be 24MHz
 */
static void sun6i_a31_get_pll1_factors(u32 *freq, u32 parent_rate,
				       u8 *n, u8 *k, u8 *m, u8 *p)
{
	/*
	 * We can operate only on MHz, this will make our life easier
	 * later.
	 */
	u32 freq_mhz = *freq / 1000000;
	u32 parent_freq_mhz = parent_rate / 1000000;

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

	*freq = freq_mhz * 1000000;

	/*
	 * If the factors pointer are null, we were just called to
	 * round down the frequency.
	 * Exit.
	 */
	if (n == NULL)
		return;

	/* If the frequency is a multiple of 32 MHz, k is always 3 */
	if (!(freq_mhz % 32))
		*k = 3;
	/* If the frequency is a multiple of 9 MHz, k is always 2 */
	else if (!(freq_mhz % 9))
		*k = 2;
	/* If the frequency is a multiple of 8 MHz, k is always 1 */
	else if (!(freq_mhz % 8))
		*k = 1;
	/* Otherwise, we don't use the k factor */
	else
		*k = 0;

	/*
	 * If the frequency is a multiple of 2 but not a multiple of
	 * 3, m is 3. This is the first time we use 6 here, yet we
	 * will use it on several other places.
	 * We use this number because it's the lowest frequency we can
	 * generate (with n = 0, k = 0, m = 3), so every other frequency
	 * somehow relates to this frequency.
	 */
	if ((freq_mhz % 6) == 2 || (freq_mhz % 6) == 4)
		*m = 2;
	/*
	 * If the frequency is a multiple of 6MHz, but the factor is
	 * odd, m will be 3
	 */
	else if ((freq_mhz / 6) & 1)
		*m = 3;
	/* Otherwise, we end up with m = 1 */
	else
		*m = 1;

	/* Calculate n thanks to the above factors we already got */
	*n = freq_mhz * (*m + 1) / ((*k + 1) * parent_freq_mhz) - 1;

	/*
	 * If n end up being outbound, and that we can still decrease
	 * m, do it.
	 */
	if ((*n + 1) > 31 && (*m + 1) > 1) {
		*n = (*n + 1) / 2 - 1;
		*m = (*m + 1) / 2 - 1;
	}
}

/**
 * sun8i_a23_get_pll1_factors() - calculates n, k, m, p factors for PLL1
 * PLL1 rate is calculated as follows
 * rate = (parent_rate * (n + 1) * (k + 1) >> p) / (m + 1);
 * parent_rate is always 24Mhz
 */

static void sun8i_a23_get_pll1_factors(u32 *freq, u32 parent_rate,
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

	/* p will be 2 for divs under 20 and odd divs under 32 */
	if (div < 20 || (div < 32 && (div & 1)))
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
	*n = div / 4 - 1;
}

/**
 * sun4i_get_pll5_factors() - calculates n, k factors for PLL5
 * PLL5 rate is calculated as follows
 * rate = parent_rate * n * (k + 1)
 * parent_rate is always 24Mhz
 */

static void sun4i_get_pll5_factors(u32 *freq, u32 parent_rate,
				   u8 *n, u8 *k, u8 *m, u8 *p)
{
	u8 div;

	/* Normalize value to a parent_rate multiple (24M) */
	div = *freq / parent_rate;
	*freq = parent_rate * div;

	/* we were called to round the frequency, we can now return */
	if (n == NULL)
		return;

	if (div < 31)
		*k = 0;
	else if (div / 2 < 31)
		*k = 1;
	else if (div / 3 < 31)
		*k = 2;
	else
		*k = 3;

	*n = DIV_ROUND_UP(div, (*k+1));
}

/**
 * sun6i_a31_get_pll6_factors() - calculates n, k factors for A31 PLL6
 * PLL6 rate is calculated as follows
 * rate = parent_rate * n * (k + 1) / 2
 * parent_rate is always 24Mhz
 */

static void sun6i_a31_get_pll6_factors(u32 *freq, u32 parent_rate,
				       u8 *n, u8 *k, u8 *m, u8 *p)
{
	u8 div;

	/*
	 * We always have 24MHz / 2, so we can just say that our
	 * parent clock is 12MHz.
	 */
	parent_rate = parent_rate / 2;

	/* Normalize value to a parent_rate multiple (24M / 2) */
	div = *freq / parent_rate;
	*freq = parent_rate * div;

	/* we were called to round the frequency, we can now return */
	if (n == NULL)
		return;

	*k = div / 32;
	if (*k > 3)
		*k = 3;

	*n = DIV_ROUND_UP(div, (*k+1));
}

/**
 * sun4i_get_apb1_factors() - calculates m, p factors for APB1
 * APB1 rate is calculated as follows
 * rate = (parent_rate >> p) / (m + 1);
 */

static void sun4i_get_apb1_factors(u32 *freq, u32 parent_rate,
				   u8 *n, u8 *k, u8 *m, u8 *p)
{
	u8 calcm, calcp;

	if (parent_rate < *freq)
		*freq = parent_rate;

	parent_rate = DIV_ROUND_UP(parent_rate, *freq);

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
 * sun4i_get_mod0_factors() - calculates m, n factors for MOD0-style clocks
 * MOD0 rate is calculated as follows
 * rate = (parent_rate >> p) / (m + 1);
 */

static void sun4i_get_mod0_factors(u32 *freq, u32 parent_rate,
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



/**
 * sun7i_a20_get_out_factors() - calculates m, p factors for CLK_OUT_A/B
 * CLK_OUT rate is calculated as follows
 * rate = (parent_rate >> p) / (m + 1);
 */

static void sun7i_a20_get_out_factors(u32 *freq, u32 parent_rate,
				      u8 *n, u8 *k, u8 *m, u8 *p)
{
	u8 div, calcm, calcp;

	/* These clocks can only divide, so we will never be able to achieve
	 * frequencies higher than the parent frequency */
	if (*freq > parent_rate)
		*freq = parent_rate;

	div = DIV_ROUND_UP(parent_rate, *freq);

	if (div < 32)
		calcp = 0;
	else if (div / 2 < 32)
		calcp = 1;
	else if (div / 4 < 32)
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

/**
 * clk_sunxi_mmc_phase_control() - configures MMC clock phase control
 */

void clk_sunxi_mmc_phase_control(struct clk *clk, u8 sample, u8 output)
{
	#define to_clk_composite(_hw) container_of(_hw, struct clk_composite, hw)
	#define to_clk_factors(_hw) container_of(_hw, struct clk_factors, hw)

	struct clk_hw *hw = __clk_get_hw(clk);
	struct clk_composite *composite = to_clk_composite(hw);
	struct clk_hw *rate_hw = composite->rate_hw;
	struct clk_factors *factors = to_clk_factors(rate_hw);
	unsigned long flags = 0;
	u32 reg;

	if (factors->lock)
		spin_lock_irqsave(factors->lock, flags);

	reg = readl(factors->reg);

	/* set sample clock phase control */
	reg &= ~(0x7 << 20);
	reg |= ((sample & 0x7) << 20);

	/* set output clock phase control */
	reg &= ~(0x7 << 8);
	reg |= ((output & 0x7) << 8);

	writel(reg, factors->reg);

	if (factors->lock)
		spin_unlock_irqrestore(factors->lock, flags);
}
EXPORT_SYMBOL(clk_sunxi_mmc_phase_control);


/**
 * sunxi_factors_clk_setup() - Setup function for factor clocks
 */

static struct clk_factors_config sun4i_pll1_config = {
	.nshift = 8,
	.nwidth = 5,
	.kshift = 4,
	.kwidth = 2,
	.mshift = 0,
	.mwidth = 2,
	.pshift = 16,
	.pwidth = 2,
};

static struct clk_factors_config sun6i_a31_pll1_config = {
	.nshift	= 8,
	.nwidth = 5,
	.kshift = 4,
	.kwidth = 2,
	.mshift = 0,
	.mwidth = 2,
};

static struct clk_factors_config sun8i_a23_pll1_config = {
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

static struct clk_factors_config sun4i_pll5_config = {
	.nshift = 8,
	.nwidth = 5,
	.kshift = 4,
	.kwidth = 2,
};

static struct clk_factors_config sun6i_a31_pll6_config = {
	.nshift	= 8,
	.nwidth = 5,
	.kshift = 4,
	.kwidth = 2,
};

static struct clk_factors_config sun4i_apb1_config = {
	.mshift = 0,
	.mwidth = 5,
	.pshift = 16,
	.pwidth = 2,
};

/* user manual says "n" but it's really "p" */
static struct clk_factors_config sun4i_mod0_config = {
	.mshift = 0,
	.mwidth = 4,
	.pshift = 16,
	.pwidth = 2,
};

/* user manual says "n" but it's really "p" */
static struct clk_factors_config sun7i_a20_out_config = {
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
	.name = "pll5",
};

static const struct factors_data sun4i_pll6_data __initconst = {
	.enable = 31,
	.table = &sun4i_pll5_config,
	.getter = sun4i_get_pll5_factors,
	.name = "pll6",
};

static const struct factors_data sun6i_a31_pll6_data __initconst = {
	.enable = 31,
	.table = &sun6i_a31_pll6_config,
	.getter = sun6i_a31_get_pll6_factors,
};

static const struct factors_data sun4i_apb1_data __initconst = {
	.table = &sun4i_apb1_config,
	.getter = sun4i_get_apb1_factors,
};

static const struct factors_data sun4i_mod0_data __initconst = {
	.enable = 31,
	.mux = 24,
	.table = &sun4i_mod0_config,
	.getter = sun4i_get_mod0_factors,
};

static const struct factors_data sun7i_a20_out_data __initconst = {
	.enable = 31,
	.mux = 24,
	.table = &sun7i_a20_out_config,
	.getter = sun7i_a20_get_out_factors,
};

static struct clk * __init sunxi_factors_clk_setup(struct device_node *node,
						   const struct factors_data *data)
{
	return sunxi_factors_register(node, data, &clk_lock);
}



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

static const struct mux_data sun4i_apb1_mux_data __initconst = {
	.shift = 24,
};

static void __init sunxi_mux_clk_setup(struct device_node *node,
				       struct mux_data *data)
{
	struct clk *clk;
	const char *clk_name = node->name;
	const char *parents[SUNXI_MAX_PARENTS];
	void __iomem *reg;
	int i = 0;

	reg = of_iomap(node, 0);

	while (i < SUNXI_MAX_PARENTS &&
	       (parents[i] = of_clk_get_parent_name(node, i)) != NULL)
		i++;

	of_property_read_string(node, "clock-output-names", &clk_name);

	clk = clk_register_mux(NULL, clk_name, parents, i,
			       CLK_SET_RATE_NO_REPARENT, reg,
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

static const struct div_data sun6i_a31_apb2_div_data __initconst = {
	.shift	= 0,
	.pow	= 0,
	.width	= 4,
};

static void __init sunxi_divider_clk_setup(struct device_node *node,
					   struct div_data *data)
{
	struct clk *clk;
	const char *clk_name = node->name;
	const char *clk_parent;
	void __iomem *reg;

	reg = of_iomap(node, 0);

	clk_parent = of_clk_get_parent_name(node, 0);

	of_property_read_string(node, "clock-output-names", &clk_name);

	clk = clk_register_divider_table(NULL, clk_name, clk_parent, 0,
					 reg, data->shift, data->width,
					 data->pow ? CLK_DIVIDER_POWER_OF_TWO : 0,
					 data->table, &clk_lock);
	if (clk) {
		of_clk_add_provider(node, of_clk_src_simple_get, clk);
		clk_register_clkdev(clk, clk_name, NULL);
	}
}



/**
 * sunxi_gates_reset... - reset bits in leaf gate clk registers handling
 */

struct gates_reset_data {
	void __iomem			*reg;
	spinlock_t			*lock;
	struct reset_controller_dev	rcdev;
};

static int sunxi_gates_reset_assert(struct reset_controller_dev *rcdev,
			      unsigned long id)
{
	struct gates_reset_data *data = container_of(rcdev,
						     struct gates_reset_data,
						     rcdev);
	unsigned long flags;
	u32 reg;

	spin_lock_irqsave(data->lock, flags);

	reg = readl(data->reg);
	writel(reg & ~BIT(id), data->reg);

	spin_unlock_irqrestore(data->lock, flags);

	return 0;
}

static int sunxi_gates_reset_deassert(struct reset_controller_dev *rcdev,
				unsigned long id)
{
	struct gates_reset_data *data = container_of(rcdev,
						     struct gates_reset_data,
						     rcdev);
	unsigned long flags;
	u32 reg;

	spin_lock_irqsave(data->lock, flags);

	reg = readl(data->reg);
	writel(reg | BIT(id), data->reg);

	spin_unlock_irqrestore(data->lock, flags);

	return 0;
}

static struct reset_control_ops sunxi_gates_reset_ops = {
	.assert		= sunxi_gates_reset_assert,
	.deassert	= sunxi_gates_reset_deassert,
};

/**
 * sunxi_gates_clk_setup() - Setup function for leaf gates on clocks
 */

#define SUNXI_GATES_MAX_SIZE	64

struct gates_data {
	DECLARE_BITMAP(mask, SUNXI_GATES_MAX_SIZE);
	u32 reset_mask;
};

static const struct gates_data sun4i_axi_gates_data __initconst = {
	.mask = {1},
};

static const struct gates_data sun4i_ahb_gates_data __initconst = {
	.mask = {0x7F77FFF, 0x14FB3F},
};

static const struct gates_data sun5i_a10s_ahb_gates_data __initconst = {
	.mask = {0x147667e7, 0x185915},
};

static const struct gates_data sun5i_a13_ahb_gates_data __initconst = {
	.mask = {0x107067e7, 0x185111},
};

static const struct gates_data sun6i_a31_ahb1_gates_data __initconst = {
	.mask = {0xEDFE7F62, 0x794F931},
};

static const struct gates_data sun7i_a20_ahb_gates_data __initconst = {
	.mask = { 0x12f77fff, 0x16ff3f },
};

static const struct gates_data sun8i_a23_ahb1_gates_data __initconst = {
	.mask = {0x25386742, 0x2505111},
};

static const struct gates_data sun4i_apb0_gates_data __initconst = {
	.mask = {0x4EF},
};

static const struct gates_data sun5i_a10s_apb0_gates_data __initconst = {
	.mask = {0x469},
};

static const struct gates_data sun5i_a13_apb0_gates_data __initconst = {
	.mask = {0x61},
};

static const struct gates_data sun7i_a20_apb0_gates_data __initconst = {
	.mask = { 0x4ff },
};

static const struct gates_data sun4i_apb1_gates_data __initconst = {
	.mask = {0xFF00F7},
};

static const struct gates_data sun5i_a10s_apb1_gates_data __initconst = {
	.mask = {0xf0007},
};

static const struct gates_data sun5i_a13_apb1_gates_data __initconst = {
	.mask = {0xa0007},
};

static const struct gates_data sun6i_a31_apb1_gates_data __initconst = {
	.mask = {0x3031},
};

static const struct gates_data sun8i_a23_apb1_gates_data __initconst = {
	.mask = {0x3021},
};

static const struct gates_data sun6i_a31_apb2_gates_data __initconst = {
	.mask = {0x3F000F},
};

static const struct gates_data sun7i_a20_apb1_gates_data __initconst = {
	.mask = { 0xff80ff },
};

static const struct gates_data sun8i_a23_apb2_gates_data __initconst = {
	.mask = {0x1F0007},
};

static const struct gates_data sun4i_a10_usb_gates_data __initconst = {
	.mask = {0x1C0},
	.reset_mask = 0x07,
};

static const struct gates_data sun5i_a13_usb_gates_data __initconst = {
	.mask = {0x140},
	.reset_mask = 0x03,
};

static const struct gates_data sun6i_a31_usb_gates_data __initconst = {
	.mask = { BIT(18) | BIT(17) | BIT(16) | BIT(10) | BIT(9) | BIT(8) },
	.reset_mask = BIT(2) | BIT(1) | BIT(0),
};

static void __init sunxi_gates_clk_setup(struct device_node *node,
					 struct gates_data *data)
{
	struct clk_onecell_data *clk_data;
	struct gates_reset_data *reset_data;
	const char *clk_parent;
	const char *clk_name;
	void __iomem *reg;
	int qty;
	int i = 0;
	int j = 0;

	reg = of_iomap(node, 0);

	clk_parent = of_clk_get_parent_name(node, 0);

	/* Worst-case size approximation and memory allocation */
	qty = find_last_bit(data->mask, SUNXI_GATES_MAX_SIZE);
	clk_data = kmalloc(sizeof(struct clk_onecell_data), GFP_KERNEL);
	if (!clk_data)
		return;
	clk_data->clks = kzalloc((qty+1) * sizeof(struct clk *), GFP_KERNEL);
	if (!clk_data->clks) {
		kfree(clk_data);
		return;
	}

	for_each_set_bit(i, data->mask, SUNXI_GATES_MAX_SIZE) {
		of_property_read_string_index(node, "clock-output-names",
					      j, &clk_name);

		clk_data->clks[i] = clk_register_gate(NULL, clk_name,
						      clk_parent, 0,
						      reg + 4 * (i/32), i % 32,
						      0, &clk_lock);
		WARN_ON(IS_ERR(clk_data->clks[i]));
		clk_register_clkdev(clk_data->clks[i], clk_name, NULL);

		j++;
	}

	/* Adjust to the real max */
	clk_data->clk_num = i;

	of_clk_add_provider(node, of_clk_src_onecell_get, clk_data);

	/* Register a reset controler for gates with reset bits */
	if (data->reset_mask == 0)
		return;

	reset_data = kzalloc(sizeof(*reset_data), GFP_KERNEL);
	if (!reset_data)
		return;

	reset_data->reg = reg;
	reset_data->lock = &clk_lock;
	reset_data->rcdev.nr_resets = __fls(data->reset_mask) + 1;
	reset_data->rcdev.ops = &sunxi_gates_reset_ops;
	reset_data->rcdev.of_node = node;
	reset_controller_register(&reset_data->rcdev);
}



/**
 * sunxi_divs_clk_setup() helper data
 */

#define SUNXI_DIVS_MAX_QTY	2
#define SUNXI_DIVISOR_WIDTH	2

struct divs_data {
	const struct factors_data *factors; /* data for the factor clock */
	struct {
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
	.div = {
		{ .shift = 0, .pow = 0, }, /* M, DDR */
		{ .shift = 16, .pow = 1, }, /* P, other */
	}
};

static const struct divs_data pll6_divs_data __initconst = {
	.factors = &sun4i_pll6_data,
	.div = {
		{ .shift = 0, .table = pll6_sata_tbl, .gate = 14 }, /* M, SATA */
		{ .fixed = 2 }, /* P, other */
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

static void __init sunxi_divs_clk_setup(struct device_node *node,
					struct divs_data *data)
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
	void __iomem *reg;
	int i = 0;
	int flags, clkflags;

	/* Set up factor clock that we will be dividing */
	pclk = sunxi_factors_clk_setup(node, data->factors);
	parent = __clk_get_name(pclk);

	reg = of_iomap(node, 0);

	clk_data = kmalloc(sizeof(struct clk_onecell_data), GFP_KERNEL);
	if (!clk_data)
		return;

	clks = kzalloc((SUNXI_DIVS_MAX_QTY+1) * sizeof(*clks), GFP_KERNEL);
	if (!clks)
		goto free_clkdata;

	clk_data->clks = clks;

	/* It's not a good idea to have automatic reparenting changing
	 * our RAM clock! */
	clkflags = !strcmp("pll5", parent) ? 0 : CLK_SET_RATE_PARENT;

	for (i = 0; i < SUNXI_DIVS_MAX_QTY; i++) {
		if (of_property_read_string_index(node, "clock-output-names",
						  i, &clk_name) != 0)
			break;

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
		clk_register_clkdev(clks[i], clk_name, NULL);
	}

	/* The last clock available on the getter is the parent */
	clks[i++] = pclk;

	/* Adjust to the real max */
	clk_data->clk_num = i;

	of_clk_add_provider(node, of_clk_src_onecell_get, clk_data);

	return;

free_gate:
	kfree(gate);
free_clks:
	kfree(clks);
free_clkdata:
	kfree(clk_data);
}



/* Matches for factors clocks */
static const struct of_device_id clk_factors_match[] __initconst = {
	{.compatible = "allwinner,sun4i-a10-pll1-clk", .data = &sun4i_pll1_data,},
	{.compatible = "allwinner,sun6i-a31-pll1-clk", .data = &sun6i_a31_pll1_data,},
	{.compatible = "allwinner,sun8i-a23-pll1-clk", .data = &sun8i_a23_pll1_data,},
	{.compatible = "allwinner,sun7i-a20-pll4-clk", .data = &sun7i_a20_pll4_data,},
	{.compatible = "allwinner,sun6i-a31-pll6-clk", .data = &sun6i_a31_pll6_data,},
	{.compatible = "allwinner,sun4i-a10-apb1-clk", .data = &sun4i_apb1_data,},
	{.compatible = "allwinner,sun4i-a10-mod0-clk", .data = &sun4i_mod0_data,},
	{.compatible = "allwinner,sun7i-a20-out-clk", .data = &sun7i_a20_out_data,},
	{}
};

/* Matches for divider clocks */
static const struct of_device_id clk_div_match[] __initconst = {
	{.compatible = "allwinner,sun4i-a10-axi-clk", .data = &sun4i_axi_data,},
	{.compatible = "allwinner,sun8i-a23-axi-clk", .data = &sun8i_a23_axi_data,},
	{.compatible = "allwinner,sun4i-a10-ahb-clk", .data = &sun4i_ahb_data,},
	{.compatible = "allwinner,sun4i-a10-apb0-clk", .data = &sun4i_apb0_data,},
	{.compatible = "allwinner,sun6i-a31-apb2-div-clk", .data = &sun6i_a31_apb2_div_data,},
	{}
};

/* Matches for divided outputs */
static const struct of_device_id clk_divs_match[] __initconst = {
	{.compatible = "allwinner,sun4i-a10-pll5-clk", .data = &pll5_divs_data,},
	{.compatible = "allwinner,sun4i-a10-pll6-clk", .data = &pll6_divs_data,},
	{}
};

/* Matches for mux clocks */
static const struct of_device_id clk_mux_match[] __initconst = {
	{.compatible = "allwinner,sun4i-a10-cpu-clk", .data = &sun4i_cpu_mux_data,},
	{.compatible = "allwinner,sun4i-a10-apb1-mux-clk", .data = &sun4i_apb1_mux_data,},
	{.compatible = "allwinner,sun6i-a31-ahb1-mux-clk", .data = &sun6i_a31_ahb1_mux_data,},
	{}
};

/* Matches for gate clocks */
static const struct of_device_id clk_gates_match[] __initconst = {
	{.compatible = "allwinner,sun4i-a10-axi-gates-clk", .data = &sun4i_axi_gates_data,},
	{.compatible = "allwinner,sun4i-a10-ahb-gates-clk", .data = &sun4i_ahb_gates_data,},
	{.compatible = "allwinner,sun5i-a10s-ahb-gates-clk", .data = &sun5i_a10s_ahb_gates_data,},
	{.compatible = "allwinner,sun5i-a13-ahb-gates-clk", .data = &sun5i_a13_ahb_gates_data,},
	{.compatible = "allwinner,sun6i-a31-ahb1-gates-clk", .data = &sun6i_a31_ahb1_gates_data,},
	{.compatible = "allwinner,sun7i-a20-ahb-gates-clk", .data = &sun7i_a20_ahb_gates_data,},
	{.compatible = "allwinner,sun8i-a23-ahb1-gates-clk", .data = &sun8i_a23_ahb1_gates_data,},
	{.compatible = "allwinner,sun4i-a10-apb0-gates-clk", .data = &sun4i_apb0_gates_data,},
	{.compatible = "allwinner,sun5i-a10s-apb0-gates-clk", .data = &sun5i_a10s_apb0_gates_data,},
	{.compatible = "allwinner,sun5i-a13-apb0-gates-clk", .data = &sun5i_a13_apb0_gates_data,},
	{.compatible = "allwinner,sun7i-a20-apb0-gates-clk", .data = &sun7i_a20_apb0_gates_data,},
	{.compatible = "allwinner,sun4i-a10-apb1-gates-clk", .data = &sun4i_apb1_gates_data,},
	{.compatible = "allwinner,sun5i-a10s-apb1-gates-clk", .data = &sun5i_a10s_apb1_gates_data,},
	{.compatible = "allwinner,sun5i-a13-apb1-gates-clk", .data = &sun5i_a13_apb1_gates_data,},
	{.compatible = "allwinner,sun6i-a31-apb1-gates-clk", .data = &sun6i_a31_apb1_gates_data,},
	{.compatible = "allwinner,sun7i-a20-apb1-gates-clk", .data = &sun7i_a20_apb1_gates_data,},
	{.compatible = "allwinner,sun8i-a23-apb1-gates-clk", .data = &sun8i_a23_apb1_gates_data,},
	{.compatible = "allwinner,sun6i-a31-apb2-gates-clk", .data = &sun6i_a31_apb2_gates_data,},
	{.compatible = "allwinner,sun8i-a23-apb2-gates-clk", .data = &sun8i_a23_apb2_gates_data,},
	{.compatible = "allwinner,sun4i-a10-usb-clk", .data = &sun4i_a10_usb_gates_data,},
	{.compatible = "allwinner,sun5i-a13-usb-clk", .data = &sun5i_a13_usb_gates_data,},
	{.compatible = "allwinner,sun6i-a31-usb-clk", .data = &sun6i_a31_usb_gates_data,},
	{}
};

static void __init of_sunxi_table_clock_setup(const struct of_device_id *clk_match,
					      void *function)
{
	struct device_node *np;
	const struct div_data *data;
	const struct of_device_id *match;
	void (*setup_function)(struct device_node *, const void *) = function;

	for_each_matching_node_and_match(np, clk_match, &match) {
		data = match->data;
		setup_function(np, data);
	}
}

static void __init sunxi_init_clocks(const char *clocks[], int nclocks)
{
	unsigned int i;

	/* Register factor clocks */
	of_sunxi_table_clock_setup(clk_factors_match, sunxi_factors_clk_setup);

	/* Register divider clocks */
	of_sunxi_table_clock_setup(clk_div_match, sunxi_divider_clk_setup);

	/* Register divided output clocks */
	of_sunxi_table_clock_setup(clk_divs_match, sunxi_divs_clk_setup);

	/* Register mux clocks */
	of_sunxi_table_clock_setup(clk_mux_match, sunxi_mux_clk_setup);

	/* Register gate clocks */
	of_sunxi_table_clock_setup(clk_gates_match, sunxi_gates_clk_setup);

	/* Protect the clocks that needs to stay on */
	for (i = 0; i < nclocks; i++) {
		struct clk *clk = clk_get(NULL, clocks[i]);

		if (!IS_ERR(clk))
			clk_prepare_enable(clk);
	}
}

static const char *sun4i_a10_critical_clocks[] __initdata = {
	"pll5_ddr",
	"ahb_sdram",
};

static void __init sun4i_a10_init_clocks(struct device_node *node)
{
	sunxi_init_clocks(sun4i_a10_critical_clocks,
			  ARRAY_SIZE(sun4i_a10_critical_clocks));
}
CLK_OF_DECLARE(sun4i_a10_clk_init, "allwinner,sun4i-a10", sun4i_a10_init_clocks);

static const char *sun5i_critical_clocks[] __initdata = {
	"mbus",
	"pll5_ddr",
	"ahb_sdram",
};

static void __init sun5i_init_clocks(struct device_node *node)
{
	sunxi_init_clocks(sun5i_critical_clocks,
			  ARRAY_SIZE(sun5i_critical_clocks));
}
CLK_OF_DECLARE(sun5i_a10s_clk_init, "allwinner,sun5i-a10s", sun5i_init_clocks);
CLK_OF_DECLARE(sun5i_a13_clk_init, "allwinner,sun5i-a13", sun5i_init_clocks);
CLK_OF_DECLARE(sun7i_a20_clk_init, "allwinner,sun7i-a20", sun5i_init_clocks);

static const char *sun6i_critical_clocks[] __initdata = {
	"cpu",
	"ahb1_sdram",
};

static void __init sun6i_init_clocks(struct device_node *node)
{
	sunxi_init_clocks(sun6i_critical_clocks,
			  ARRAY_SIZE(sun6i_critical_clocks));
}
CLK_OF_DECLARE(sun6i_a31_clk_init, "allwinner,sun6i-a31", sun6i_init_clocks);
CLK_OF_DECLARE(sun8i_a23_clk_init, "allwinner,sun8i-a23", sun6i_init_clocks);

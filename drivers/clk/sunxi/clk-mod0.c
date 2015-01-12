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
#include <linux/of_address.h>

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
	.muxmask = BIT(1) | BIT(0),
	.table = &sun4i_a10_mod0_config,
	.getter = sun4i_a10_get_mod0_factors,
};

static DEFINE_SPINLOCK(sun4i_a10_mod0_lock);

static void __init sun4i_a10_mod0_setup(struct device_node *node)
{
	sunxi_factors_register(node, &sun4i_a10_mod0_data, &sun4i_a10_mod0_lock);
}
CLK_OF_DECLARE(sun4i_a10_mod0, "allwinner,sun4i-a10-mod0-clk", sun4i_a10_mod0_setup);

static DEFINE_SPINLOCK(sun5i_a13_mbus_lock);

static void __init sun5i_a13_mbus_setup(struct device_node *node)
{
	struct clk *mbus = sunxi_factors_register(node, &sun4i_a10_mod0_data, &sun5i_a13_mbus_lock);

	/* The MBUS clocks needs to be always enabled */
	__clk_get(mbus);
	clk_prepare_enable(mbus);
}
CLK_OF_DECLARE(sun5i_a13_mbus, "allwinner,sun5i-a13-mbus-clk", sun5i_a13_mbus_setup);

struct mmc_phase_data {
	u8	offset;
};

struct mmc_phase {
	struct clk_hw		hw;
	void __iomem		*reg;
	struct mmc_phase_data	*data;
	spinlock_t		*lock;
};

#define to_mmc_phase(_hw) container_of(_hw, struct mmc_phase, hw)

static int mmc_get_phase(struct clk_hw *hw)
{
	struct clk *mmc, *mmc_parent, *clk = hw->clk;
	struct mmc_phase *phase = to_mmc_phase(hw);
	unsigned int mmc_rate, mmc_parent_rate;
	u16 step, mmc_div;
	u32 value;
	u8 delay;

	value = readl(phase->reg);
	delay = (value >> phase->data->offset) & 0x3;

	if (!delay)
		return 180;

	/* Get the main MMC clock */
	mmc = clk_get_parent(clk);
	if (!mmc)
		return -EINVAL;

	/* And its rate */
	mmc_rate = clk_get_rate(mmc);
	if (!mmc_rate)
		return -EINVAL;

	/* Now, get the MMC parent (most likely some PLL) */
	mmc_parent = clk_get_parent(mmc);
	if (!mmc_parent)
		return -EINVAL;

	/* And its rate */
	mmc_parent_rate = clk_get_rate(mmc_parent);
	if (!mmc_parent_rate)
		return -EINVAL;

	/* Get MMC clock divider */
	mmc_div = mmc_parent_rate / mmc_rate;

	step = DIV_ROUND_CLOSEST(360, mmc_div);
	return delay * step;
}

static int mmc_set_phase(struct clk_hw *hw, int degrees)
{
	struct clk *mmc, *mmc_parent, *clk = hw->clk;
	struct mmc_phase *phase = to_mmc_phase(hw);
	unsigned int mmc_rate, mmc_parent_rate;
	unsigned long flags;
	u32 value;
	u8 delay;

	/* Get the main MMC clock */
	mmc = clk_get_parent(clk);
	if (!mmc)
		return -EINVAL;

	/* And its rate */
	mmc_rate = clk_get_rate(mmc);
	if (!mmc_rate)
		return -EINVAL;

	/* Now, get the MMC parent (most likely some PLL) */
	mmc_parent = clk_get_parent(mmc);
	if (!mmc_parent)
		return -EINVAL;

	/* And its rate */
	mmc_parent_rate = clk_get_rate(mmc_parent);
	if (!mmc_parent_rate)
		return -EINVAL;

	if (degrees != 180) {
		u16 step, mmc_div;

		/* Get MMC clock divider */
		mmc_div = mmc_parent_rate / mmc_rate;

		/*
		 * We can only outphase the clocks by multiple of the
		 * PLL's period.
		 *
		 * Since the MMC clock in only a divider, and the
		 * formula to get the outphasing in degrees is deg =
		 * 360 * delta / period
		 *
		 * If we simplify this formula, we can see that the
		 * only thing that we're concerned about is the number
		 * of period we want to outphase our clock from, and
		 * the divider set by the MMC clock.
		 */
		step = DIV_ROUND_CLOSEST(360, mmc_div);
		delay = DIV_ROUND_CLOSEST(degrees, step);
	} else {
		delay = 0;
	}

	spin_lock_irqsave(phase->lock, flags);
	value = readl(phase->reg);
	value &= ~GENMASK(phase->data->offset + 3, phase->data->offset);
	value |= delay << phase->data->offset;
	writel(value, phase->reg);
	spin_unlock_irqrestore(phase->lock, flags);

	return 0;
}

static const struct clk_ops mmc_clk_ops = {
	.get_phase	= mmc_get_phase,
	.set_phase	= mmc_set_phase,
};

static void __init sun4i_a10_mmc_phase_setup(struct device_node *node,
					     struct mmc_phase_data *data)
{
	const char *parent_names[1] = { of_clk_get_parent_name(node, 0) };
	struct clk_init_data init = {
		.num_parents	= 1,
		.parent_names	= parent_names,
		.ops		= &mmc_clk_ops,
	};

	struct mmc_phase *phase;
	struct clk *clk;

	phase = kmalloc(sizeof(*phase), GFP_KERNEL);
	if (!phase)
		return;

	phase->hw.init = &init;

	phase->reg = of_iomap(node, 0);
	if (!phase->reg)
		goto err_free;

	phase->data = data;
	phase->lock = &sun4i_a10_mod0_lock;

	if (of_property_read_string(node, "clock-output-names", &init.name))
		init.name = node->name;

	clk = clk_register(NULL, &phase->hw);
	if (IS_ERR(clk))
		goto err_unmap;

	of_clk_add_provider(node, of_clk_src_simple_get, clk);

	return;

err_unmap:
	iounmap(phase->reg);
err_free:
	kfree(phase);
}


static struct mmc_phase_data mmc_output_clk = {
	.offset	= 8,
};

static struct mmc_phase_data mmc_sample_clk = {
	.offset	= 20,
};

static void __init sun4i_a10_mmc_output_setup(struct device_node *node)
{
	sun4i_a10_mmc_phase_setup(node, &mmc_output_clk);
}
CLK_OF_DECLARE(sun4i_a10_mmc_output, "allwinner,sun4i-a10-mmc-output-clk", sun4i_a10_mmc_output_setup);

static void __init sun4i_a10_mmc_sample_setup(struct device_node *node)
{
	sun4i_a10_mmc_phase_setup(node, &mmc_sample_clk);
}
CLK_OF_DECLARE(sun4i_a10_mmc_sample, "allwinner,sun4i-a10-mmc-sample-clk", sun4i_a10_mmc_sample_setup);

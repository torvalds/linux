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
#include <linux/clkdev.h>
#include <linux/clk-provider.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include "clk-factors.h"

/**
 * sun4i_get_mod0_factors() - calculates m, n factors for MOD0-style clocks
 * MOD0 rate is calculated as follows
 * rate = (parent_rate >> p) / (m + 1);
 */

static void sun4i_a10_get_mod0_factors(struct factors_request *req)
{
	u8 div, calcm, calcp;

	/* These clocks can only divide, so we will never be able to achieve
	 * frequencies higher than the parent frequency */
	if (req->rate > req->parent_rate)
		req->rate = req->parent_rate;

	div = DIV_ROUND_UP(req->parent_rate, req->rate);

	if (div < 16)
		calcp = 0;
	else if (div / 2 < 16)
		calcp = 1;
	else if (div / 4 < 16)
		calcp = 2;
	else
		calcp = 3;

	calcm = DIV_ROUND_UP(div, 1 << calcp);

	req->rate = (req->parent_rate >> calcp) / calcm;
	req->m = calcm - 1;
	req->p = calcp;
}

/* user manual says "n" but it's really "p" */
static const struct clk_factors_config sun4i_a10_mod0_config = {
	.mshift = 0,
	.mwidth = 4,
	.pshift = 16,
	.pwidth = 2,
};

static const struct factors_data sun4i_a10_mod0_data = {
	.enable = 31,
	.mux = 24,
	.muxmask = BIT(1) | BIT(0),
	.table = &sun4i_a10_mod0_config,
	.getter = sun4i_a10_get_mod0_factors,
};

static DEFINE_SPINLOCK(sun4i_a10_mod0_lock);

static void __init sun4i_a10_mod0_setup(struct device_node *node)
{
	void __iomem *reg;

	reg = of_iomap(node, 0);
	if (!reg) {
		/*
		 * This happens with mod0 clk nodes instantiated through
		 * mfd, as those do not have their resources assigned at
		 * CLK_OF_DECLARE time yet, so do not print an error.
		 */
		return;
	}

	sunxi_factors_register(node, &sun4i_a10_mod0_data,
			       &sun4i_a10_mod0_lock, reg);
}
CLK_OF_DECLARE(sun4i_a10_mod0, "allwinner,sun4i-a10-mod0-clk", sun4i_a10_mod0_setup);

static int sun4i_a10_mod0_clk_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct resource *r;
	void __iomem *reg;

	if (!np)
		return -ENODEV;

	r = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	reg = devm_ioremap_resource(&pdev->dev, r);
	if (IS_ERR(reg))
		return PTR_ERR(reg);

	sunxi_factors_register(np, &sun4i_a10_mod0_data,
			       &sun4i_a10_mod0_lock, reg);
	return 0;
}

static const struct of_device_id sun4i_a10_mod0_clk_dt_ids[] = {
	{ .compatible = "allwinner,sun4i-a10-mod0-clk" },
	{ /* sentinel */ }
};

static struct platform_driver sun4i_a10_mod0_clk_driver = {
	.driver = {
		.name = "sun4i-a10-mod0-clk",
		.of_match_table = sun4i_a10_mod0_clk_dt_ids,
	},
	.probe = sun4i_a10_mod0_clk_probe,
};
builtin_platform_driver(sun4i_a10_mod0_clk_driver);

static const struct factors_data sun9i_a80_mod0_data __initconst = {
	.enable = 31,
	.mux = 24,
	.muxmask = BIT(3) | BIT(2) | BIT(1) | BIT(0),
	.table = &sun4i_a10_mod0_config,
	.getter = sun4i_a10_get_mod0_factors,
};

static void __init sun9i_a80_mod0_setup(struct device_node *node)
{
	void __iomem *reg;

	reg = of_io_request_and_map(node, 0, of_node_full_name(node));
	if (IS_ERR(reg)) {
		pr_err("Could not get registers for mod0-clk: %s\n",
		       node->name);
		return;
	}

	sunxi_factors_register(node, &sun9i_a80_mod0_data,
			       &sun4i_a10_mod0_lock, reg);
}
CLK_OF_DECLARE(sun9i_a80_mod0, "allwinner,sun9i-a80-mod0-clk", sun9i_a80_mod0_setup);

static DEFINE_SPINLOCK(sun5i_a13_mbus_lock);

static void __init sun5i_a13_mbus_setup(struct device_node *node)
{
	struct clk *mbus;
	void __iomem *reg;

	reg = of_iomap(node, 0);
	if (!reg) {
		pr_err("Could not get registers for a13-mbus-clk\n");
		return;
	}

	mbus = sunxi_factors_register(node, &sun4i_a10_mod0_data,
				      &sun5i_a13_mbus_lock, reg);

	/* The MBUS clocks needs to be always enabled */
	__clk_get(mbus);
	clk_prepare_enable(mbus);
}
CLK_OF_DECLARE(sun5i_a13_mbus, "allwinner,sun5i-a13-mbus-clk", sun5i_a13_mbus_setup);

struct mmc_phase {
	struct clk_hw		hw;
	u8			offset;
	void __iomem		*reg;
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
	delay = (value >> phase->offset) & 0x3;

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
	value &= ~GENMASK(phase->offset + 3, phase->offset);
	value |= delay << phase->offset;
	writel(value, phase->reg);
	spin_unlock_irqrestore(phase->lock, flags);

	return 0;
}

static const struct clk_ops mmc_clk_ops = {
	.get_phase	= mmc_get_phase,
	.set_phase	= mmc_set_phase,
};

/*
 * sunxi_mmc_setup - Common setup function for mmc module clocks
 *
 * The only difference between module clocks on different platforms is the
 * width of the mux register bits and the valid values, which are passed in
 * through struct factors_data. The phase clocks parts are identical.
 */
static void __init sunxi_mmc_setup(struct device_node *node,
				   const struct factors_data *data,
				   spinlock_t *lock)
{
	struct clk_onecell_data *clk_data;
	const char *parent;
	void __iomem *reg;
	int i;

	reg = of_io_request_and_map(node, 0, of_node_full_name(node));
	if (IS_ERR(reg)) {
		pr_err("Couldn't map the %s clock registers\n", node->name);
		return;
	}

	clk_data = kmalloc(sizeof(*clk_data), GFP_KERNEL);
	if (!clk_data)
		return;

	clk_data->clks = kcalloc(3, sizeof(*clk_data->clks), GFP_KERNEL);
	if (!clk_data->clks)
		goto err_free_data;

	clk_data->clk_num = 3;
	clk_data->clks[0] = sunxi_factors_register(node, data, lock, reg);
	if (!clk_data->clks[0])
		goto err_free_clks;

	parent = __clk_get_name(clk_data->clks[0]);

	for (i = 1; i < 3; i++) {
		struct clk_init_data init = {
			.num_parents	= 1,
			.parent_names	= &parent,
			.ops		= &mmc_clk_ops,
		};
		struct mmc_phase *phase;

		phase = kmalloc(sizeof(*phase), GFP_KERNEL);
		if (!phase)
			continue;

		phase->hw.init = &init;
		phase->reg = reg;
		phase->lock = lock;

		if (i == 1)
			phase->offset = 8;
		else
			phase->offset = 20;

		if (of_property_read_string_index(node, "clock-output-names",
						  i, &init.name))
			init.name = node->name;

		clk_data->clks[i] = clk_register(NULL, &phase->hw);
		if (IS_ERR(clk_data->clks[i])) {
			kfree(phase);
			continue;
		}
	}

	of_clk_add_provider(node, of_clk_src_onecell_get, clk_data);

	return;

err_free_clks:
	kfree(clk_data->clks);
err_free_data:
	kfree(clk_data);
}

static DEFINE_SPINLOCK(sun4i_a10_mmc_lock);

static void __init sun4i_a10_mmc_setup(struct device_node *node)
{
	sunxi_mmc_setup(node, &sun4i_a10_mod0_data, &sun4i_a10_mmc_lock);
}
CLK_OF_DECLARE(sun4i_a10_mmc, "allwinner,sun4i-a10-mmc-clk", sun4i_a10_mmc_setup);

static DEFINE_SPINLOCK(sun9i_a80_mmc_lock);

static void __init sun9i_a80_mmc_setup(struct device_node *node)
{
	sunxi_mmc_setup(node, &sun9i_a80_mod0_data, &sun9i_a80_mmc_lock);
}
CLK_OF_DECLARE(sun9i_a80_mmc, "allwinner,sun9i-a80-mmc-clk", sun9i_a80_mmc_setup);

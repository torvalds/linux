// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2018 ROHM Semiconductors

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/mfd/rohm-bd718x7.h>
#include <linux/clk-provider.h>
#include <linux/clkdev.h>
#include <linux/regmap.h>

struct bd718xx_clk {
	struct clk_hw hw;
	u8 reg;
	u8 mask;
	struct platform_device *pdev;
	struct rohm_regmap_dev *mfd;
};

static int bd71837_clk_set(struct clk_hw *hw, int status)
{
	struct bd718xx_clk *c = container_of(hw, struct bd718xx_clk, hw);

	return regmap_update_bits(c->mfd->regmap, c->reg, c->mask, status);
}

static void bd71837_clk_disable(struct clk_hw *hw)
{
	int rv;
	struct bd718xx_clk *c = container_of(hw, struct bd718xx_clk, hw);

	rv = bd71837_clk_set(hw, 0);
	if (rv)
		dev_dbg(&c->pdev->dev, "Failed to disable 32K clk (%d)\n", rv);
}

static int bd71837_clk_enable(struct clk_hw *hw)
{
	return bd71837_clk_set(hw, 1);
}

static int bd71837_clk_is_enabled(struct clk_hw *hw)
{
	int enabled;
	int rval;
	struct bd718xx_clk *c = container_of(hw, struct bd718xx_clk, hw);

	rval = regmap_read(c->mfd->regmap, c->reg, &enabled);

	if (rval)
		return rval;

	return enabled & c->mask;
}

static const struct clk_ops bd71837_clk_ops = {
	.prepare = &bd71837_clk_enable,
	.unprepare = &bd71837_clk_disable,
	.is_prepared = &bd71837_clk_is_enabled,
};

static int bd71837_clk_probe(struct platform_device *pdev)
{
	struct bd718xx_clk *c;
	int rval = -ENOMEM;
	const char *parent_clk;
	struct device *parent = pdev->dev.parent;
	struct rohm_regmap_dev *mfd = dev_get_drvdata(parent);
	struct clk_init_data init = {
		.name = "bd718xx-32k-out",
		.ops = &bd71837_clk_ops,
	};

	c = devm_kzalloc(&pdev->dev, sizeof(*c), GFP_KERNEL);
	if (!c)
		return -ENOMEM;

	init.num_parents = 1;
	parent_clk = of_clk_get_parent_name(parent->of_node, 0);

	init.parent_names = &parent_clk;
	if (!parent_clk) {
		dev_err(&pdev->dev, "No parent clk found\n");
		return -EINVAL;
	}

	c->reg = BD718XX_REG_OUT32K;
	c->mask = BD718XX_OUT32K_EN;
	c->mfd = mfd;
	c->pdev = pdev;
	c->hw.init = &init;

	of_property_read_string_index(parent->of_node,
				      "clock-output-names", 0, &init.name);

	rval = devm_clk_hw_register(&pdev->dev, &c->hw);
	if (rval) {
		dev_err(&pdev->dev, "failed to register 32K clk");
		return rval;
	}
	rval = devm_of_clk_add_hw_provider(&pdev->dev, of_clk_hw_simple_get,
					   &c->hw);
	if (rval)
		dev_err(&pdev->dev, "adding clk provider failed\n");

	return rval;
}

static struct platform_driver bd71837_clk = {
	.driver = {
		.name = "bd718xx-clk",
	},
	.probe = bd71837_clk_probe,
};

module_platform_driver(bd71837_clk);

MODULE_AUTHOR("Matti Vaittinen <matti.vaittinen@fi.rohmeurope.com>");
MODULE_DESCRIPTION("BD71837/BD71847 chip clk driver");
MODULE_LICENSE("GPL");

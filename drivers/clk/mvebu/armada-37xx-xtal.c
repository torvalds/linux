// SPDX-License-Identifier: GPL-2.0
/*
 * Marvell Armada 37xx SoC xtal clocks
 *
 * Copyright (C) 2016 Marvell
 *
 * Gregory CLEMENT <gregory.clement@free-electrons.com>
 *
 */

#include <linux/clk-provider.h>
#include <linux/mfd/syscon.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#define NB_GPIO1_LATCH	0x8
#define XTAL_MODE	    BIT(9)

static int armada_3700_xtal_clock_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	const char *xtal_name = "xtal";
	struct device_node *parent;
	struct regmap *regmap;
	struct clk_hw *xtal_hw;
	unsigned int rate;
	u32 reg;
	int ret;

	xtal_hw = devm_kzalloc(&pdev->dev, sizeof(*xtal_hw), GFP_KERNEL);
	if (!xtal_hw)
		return -ENOMEM;

	platform_set_drvdata(pdev, xtal_hw);

	parent = np->parent;
	if (!parent) {
		dev_err(&pdev->dev, "no parent\n");
		return -ENODEV;
	}

	regmap = syscon_node_to_regmap(parent);
	if (IS_ERR(regmap)) {
		dev_err(&pdev->dev, "cannot get regmap\n");
		return PTR_ERR(regmap);
	}

	ret = regmap_read(regmap, NB_GPIO1_LATCH, &reg);
	if (ret) {
		dev_err(&pdev->dev, "cannot read from regmap\n");
		return ret;
	}

	if (reg & XTAL_MODE)
		rate = 40000000;
	else
		rate = 25000000;

	of_property_read_string_index(np, "clock-output-names", 0, &xtal_name);
	xtal_hw = clk_hw_register_fixed_rate(NULL, xtal_name, NULL, 0, rate);
	if (IS_ERR(xtal_hw))
		return PTR_ERR(xtal_hw);
	ret = of_clk_add_hw_provider(np, of_clk_hw_simple_get, xtal_hw);

	return ret;
}

static void armada_3700_xtal_clock_remove(struct platform_device *pdev)
{
	of_clk_del_provider(pdev->dev.of_node);
}

static const struct of_device_id armada_3700_xtal_clock_of_match[] = {
	{ .compatible = "marvell,armada-3700-xtal-clock", },
	{ }
};

static struct platform_driver armada_3700_xtal_clock_driver = {
	.probe = armada_3700_xtal_clock_probe,
	.remove = armada_3700_xtal_clock_remove,
	.driver		= {
		.name	= "marvell-armada-3700-xtal-clock",
		.of_match_table = armada_3700_xtal_clock_of_match,
	},
};

builtin_platform_driver(armada_3700_xtal_clock_driver);

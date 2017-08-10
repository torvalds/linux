/*
 * RSB driver for the X-Powers' Power Management ICs
 *
 * AXP20x typically comprises an adaptive USB-Compatible PWM charger, BUCK DC-DC
 * converters, LDOs, multiple 12-bit ADCs of voltage, current and temperature
 * as well as configurable GPIOs.
 *
 * This driver supports the RSB variants.
 *
 * Copyright (C) 2015 Chen-Yu Tsai
 *
 * Author: Chen-Yu Tsai <wens@csie.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/acpi.h>
#include <linux/err.h>
#include <linux/mfd/axp20x.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/sunxi-rsb.h>

static int axp20x_rsb_probe(struct sunxi_rsb_device *rdev)
{
	struct axp20x_dev *axp20x;
	int ret;

	axp20x = devm_kzalloc(&rdev->dev, sizeof(*axp20x), GFP_KERNEL);
	if (!axp20x)
		return -ENOMEM;

	axp20x->dev = &rdev->dev;
	axp20x->irq = rdev->irq;
	dev_set_drvdata(&rdev->dev, axp20x);

	ret = axp20x_match_device(axp20x);
	if (ret)
		return ret;

	axp20x->regmap = devm_regmap_init_sunxi_rsb(rdev, axp20x->regmap_cfg);
	if (IS_ERR(axp20x->regmap)) {
		ret = PTR_ERR(axp20x->regmap);
		dev_err(&rdev->dev, "regmap init failed: %d\n", ret);
		return ret;
	}

	return axp20x_device_probe(axp20x);
}

static int axp20x_rsb_remove(struct sunxi_rsb_device *rdev)
{
	struct axp20x_dev *axp20x = sunxi_rsb_device_get_drvdata(rdev);

	return axp20x_device_remove(axp20x);
}

static const struct of_device_id axp20x_rsb_of_match[] = {
	{ .compatible = "x-powers,axp223", .data = (void *)AXP223_ID },
	{ .compatible = "x-powers,axp803", .data = (void *)AXP803_ID },
	{ .compatible = "x-powers,axp806", .data = (void *)AXP806_ID },
	{ .compatible = "x-powers,axp809", .data = (void *)AXP809_ID },
	{ },
};
MODULE_DEVICE_TABLE(of, axp20x_rsb_of_match);

static struct sunxi_rsb_driver axp20x_rsb_driver = {
	.driver = {
		.name	= "axp20x-rsb",
		.of_match_table	= of_match_ptr(axp20x_rsb_of_match),
	},
	.probe	= axp20x_rsb_probe,
	.remove	= axp20x_rsb_remove,
};
module_sunxi_rsb_driver(axp20x_rsb_driver);

MODULE_DESCRIPTION("PMIC MFD sunXi RSB driver for AXP20X");
MODULE_AUTHOR("Chen-Yu Tsai <wens@csie.org>");
MODULE_LICENSE("GPL v2");

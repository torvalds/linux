/*
 * clk-max77802.c - Clock driver for Maxim 77802
 *
 * Copyright (C) 2014 Google, Inc
 *
 * Copyright (C) 2012 Samsung Electornics
 * Jonghwa Lee <jonghwa3.lee@samsung.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * This driver is based on clk-max77686.c
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/mfd/max77686-private.h>
#include <linux/clk-provider.h>
#include <linux/mutex.h>
#include <linux/clkdev.h>

#include <dt-bindings/clock/maxim,max77802.h>
#include "clk-max-gen.h"

#define MAX77802_CLOCK_OPMODE_MASK	0x1
#define MAX77802_CLOCK_LOW_JITTER_SHIFT 0x3

static struct clk_init_data max77802_clks_init[MAX77802_CLKS_NUM] = {
	[MAX77802_CLK_32K_AP] = {
		.name = "32khz_ap",
		.ops = &max_gen_clk_ops,
	},
	[MAX77802_CLK_32K_CP] = {
		.name = "32khz_cp",
		.ops = &max_gen_clk_ops,
	},
};

static int max77802_clk_probe(struct platform_device *pdev)
{
	struct max77686_dev *iodev = dev_get_drvdata(pdev->dev.parent);
	int ret;

	ret = max_gen_clk_probe(pdev, iodev->regmap, MAX77802_REG_32KHZ,
				max77802_clks_init, MAX77802_CLKS_NUM);

	if (ret) {
		dev_err(&pdev->dev, "generic probe failed %d\n", ret);
		return ret;
	}

	/* Enable low-jitter mode on the 32khz clocks. */
	ret = regmap_update_bits(iodev->regmap, MAX77802_REG_32KHZ,
				 1 << MAX77802_CLOCK_LOW_JITTER_SHIFT,
				 1 << MAX77802_CLOCK_LOW_JITTER_SHIFT);
	if (ret < 0)
		dev_err(&pdev->dev, "failed to enable low-jitter mode\n");

	return ret;
}

static int max77802_clk_remove(struct platform_device *pdev)
{
	return max_gen_clk_remove(pdev, MAX77802_CLKS_NUM);
}

static const struct platform_device_id max77802_clk_id[] = {
	{ "max77802-clk", 0},
	{ },
};
MODULE_DEVICE_TABLE(platform, max77802_clk_id);

static struct platform_driver max77802_clk_driver = {
	.driver = {
		.name  = "max77802-clk",
	},
	.probe = max77802_clk_probe,
	.remove = max77802_clk_remove,
	.id_table = max77802_clk_id,
};

module_platform_driver(max77802_clk_driver);

MODULE_DESCRIPTION("MAXIM 77802 Clock Driver");
MODULE_AUTHOR("Javier Martinez Canillas <javier@osg.samsung.com");
MODULE_LICENSE("GPL");

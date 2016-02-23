/*
 * dummy.c
 *
 * Copyright 2010 Wolfson Microelectronics PLC.
 *
 * Author: Mark Brown <broonie@opensource.wolfsonmicro.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This is useful for systems with mixed controllable and
 * non-controllable regulators, as well as for allowing testing on
 * systems with no controllable regulators.
 */

#include <linux/err.h>
#include <linux/export.h>
#include <linux/platform_device.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>

#include "dummy.h"

struct regulator_dev *dummy_regulator_rdev;

static struct regulator_init_data dummy_initdata = {
	.constraints = {
		.always_on = 1,
	},
};

static struct regulator_ops dummy_ops;

static const struct regulator_desc dummy_desc = {
	.name = "regulator-dummy",
	.id = -1,
	.type = REGULATOR_VOLTAGE,
	.owner = THIS_MODULE,
	.ops = &dummy_ops,
};

static int dummy_regulator_probe(struct platform_device *pdev)
{
	struct regulator_config config = { };
	int ret;

	config.dev = &pdev->dev;
	config.init_data = &dummy_initdata;

	dummy_regulator_rdev = regulator_register(&dummy_desc, &config);
	if (IS_ERR(dummy_regulator_rdev)) {
		ret = PTR_ERR(dummy_regulator_rdev);
		pr_err("Failed to register regulator: %d\n", ret);
		return ret;
	}

	return 0;
}

static struct platform_driver dummy_regulator_driver = {
	.probe		= dummy_regulator_probe,
	.driver		= {
		.name		= "reg-dummy",
	},
};

static struct platform_device *dummy_pdev;

void __init regulator_dummy_init(void)
{
	int ret;

	dummy_pdev = platform_device_alloc("reg-dummy", -1);
	if (!dummy_pdev) {
		pr_err("Failed to allocate dummy regulator device\n");
		return;
	}

	ret = platform_device_add(dummy_pdev);
	if (ret != 0) {
		pr_err("Failed to register dummy regulator device: %d\n", ret);
		platform_device_put(dummy_pdev);
		return;
	}

	ret = platform_driver_register(&dummy_regulator_driver);
	if (ret != 0) {
		pr_err("Failed to register dummy regulator driver: %d\n", ret);
		platform_device_unregister(dummy_pdev);
	}
}

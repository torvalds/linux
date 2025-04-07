// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * dummy.c
 *
 * Copyright 2010 Wolfson Microelectronics PLC.
 *
 * Author: Mark Brown <broonie@opensource.wolfsonmicro.com>
 *
 * This is useful for systems with mixed controllable and
 * non-controllable regulators, as well as for allowing testing on
 * systems with no controllable regulators.
 */

#include <linux/err.h>
#include <linux/export.h>
#include <linux/device/faux.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>

#include "dummy.h"

struct regulator_dev *dummy_regulator_rdev;

static const struct regulator_init_data dummy_initdata = {
	.constraints = {
		.always_on = 1,
	},
};

static const struct regulator_ops dummy_ops;

static const struct regulator_desc dummy_desc = {
	.name = "regulator-dummy",
	.id = -1,
	.type = REGULATOR_VOLTAGE,
	.owner = THIS_MODULE,
	.ops = &dummy_ops,
};

static int dummy_regulator_probe(struct faux_device *fdev)
{
	struct regulator_config config = { };
	int ret;

	config.dev = &fdev->dev;
	config.init_data = &dummy_initdata;

	dummy_regulator_rdev = devm_regulator_register(&fdev->dev, &dummy_desc,
						       &config);
	if (IS_ERR(dummy_regulator_rdev)) {
		ret = PTR_ERR(dummy_regulator_rdev);
		pr_err("Failed to register regulator: %d\n", ret);
		return ret;
	}

	return 0;
}

struct faux_device_ops dummy_regulator_driver = {
	.probe = dummy_regulator_probe,
};

static struct faux_device *dummy_fdev;

void __init regulator_dummy_init(void)
{
	dummy_fdev = faux_device_create("reg-dummy", NULL, &dummy_regulator_driver);
	if (!dummy_fdev) {
		pr_err("Failed to allocate dummy regulator device\n");
		return;
	}
}

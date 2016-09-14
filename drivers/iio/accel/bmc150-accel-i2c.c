/*
 * 3-axis accelerometer driver supporting following I2C Bosch-Sensortec chips:
 *  - BMC150
 *  - BMI055
 *  - BMA255
 *  - BMA250E
 *  - BMA222E
 *  - BMA280
 *
 * Copyright (c) 2014, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#include <linux/device.h>
#include <linux/mod_devicetable.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/acpi.h>
#include <linux/regmap.h>

#include "bmc150-accel.h"

static int bmc150_accel_probe(struct i2c_client *client,
			      const struct i2c_device_id *id)
{
	struct regmap *regmap;
	const char *name = NULL;
	bool block_supported =
		i2c_check_functionality(client->adapter, I2C_FUNC_I2C) ||
		i2c_check_functionality(client->adapter,
					I2C_FUNC_SMBUS_READ_I2C_BLOCK);

	regmap = devm_regmap_init_i2c(client, &bmc150_regmap_conf);
	if (IS_ERR(regmap)) {
		dev_err(&client->dev, "Failed to initialize i2c regmap\n");
		return PTR_ERR(regmap);
	}

	if (id)
		name = id->name;

	return bmc150_accel_core_probe(&client->dev, regmap, client->irq, name,
				       block_supported);
}

static int bmc150_accel_remove(struct i2c_client *client)
{
	return bmc150_accel_core_remove(&client->dev);
}

static const struct acpi_device_id bmc150_accel_acpi_match[] = {
	{"BSBA0150",	bmc150},
	{"BMC150A",	bmc150},
	{"BMI055A",	bmi055},
	{"BMA0255",	bma255},
	{"BMA250E",	bma250e},
	{"BMA222E",	bma222e},
	{"BMA0280",	bma280},
	{ },
};
MODULE_DEVICE_TABLE(acpi, bmc150_accel_acpi_match);

static const struct i2c_device_id bmc150_accel_id[] = {
	{"bmc150_accel",	bmc150},
	{"bmi055_accel",	bmi055},
	{"bma255",		bma255},
	{"bma250e",		bma250e},
	{"bma222e",		bma222e},
	{"bma280",		bma280},
	{}
};

MODULE_DEVICE_TABLE(i2c, bmc150_accel_id);

static struct i2c_driver bmc150_accel_driver = {
	.driver = {
		.name	= "bmc150_accel_i2c",
		.acpi_match_table = ACPI_PTR(bmc150_accel_acpi_match),
		.pm	= &bmc150_accel_pm_ops,
	},
	.probe		= bmc150_accel_probe,
	.remove		= bmc150_accel_remove,
	.id_table	= bmc150_accel_id,
};
module_i2c_driver(bmc150_accel_driver);

MODULE_AUTHOR("Srinivas Pandruvada <srinivas.pandruvada@linux.intel.com>");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("BMC150 I2C accelerometer driver");

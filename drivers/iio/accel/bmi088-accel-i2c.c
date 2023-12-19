// SPDX-License-Identifier: GPL-2.0
/*
 * 3-axis accelerometer driver supporting following Bosch-Sensortec chips:
 *  - BMI088
 *  - BMI085
 *  - BMI090L
 *
 * Copyright 2023 Jun Yan <jerrysteve1101@gmail.com>
 */

#include <linux/i2c.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/slab.h>

#include "bmi088-accel.h"

static int bmi088_accel_probe(struct i2c_client *i2c)
{
	struct regmap *regmap;
	const struct i2c_device_id *id = i2c_client_get_device_id(i2c);

	regmap = devm_regmap_init_i2c(i2c, &bmi088_regmap_conf);
	if (IS_ERR(regmap)) {
		dev_err(&i2c->dev, "Failed to initialize i2c regmap\n");
		return PTR_ERR(regmap);
	}

	return bmi088_accel_core_probe(&i2c->dev, regmap, i2c->irq,
					id->driver_data);
}

static void bmi088_accel_remove(struct i2c_client *i2c)
{
	bmi088_accel_core_remove(&i2c->dev);
}

static const struct of_device_id bmi088_of_match[] = {
	{ .compatible = "bosch,bmi085-accel" },
	{ .compatible = "bosch,bmi088-accel" },
	{ .compatible = "bosch,bmi090l-accel" },
	{}
};
MODULE_DEVICE_TABLE(of, bmi088_of_match);

static const struct i2c_device_id bmi088_accel_id[] = {
	{ "bmi085-accel",  BOSCH_BMI085 },
	{ "bmi088-accel",  BOSCH_BMI088 },
	{ "bmi090l-accel", BOSCH_BMI090L },
	{}
};
MODULE_DEVICE_TABLE(i2c, bmi088_accel_id);

static struct i2c_driver bmi088_accel_driver = {
	.driver = {
		.name	= "bmi088_accel_i2c",
		.pm	= pm_ptr(&bmi088_accel_pm_ops),
		.of_match_table = bmi088_of_match,
	},
	.probe		= bmi088_accel_probe,
	.remove		= bmi088_accel_remove,
	.id_table	= bmi088_accel_id,
};
module_i2c_driver(bmi088_accel_driver);

MODULE_AUTHOR("Jun Yan <jerrysteve1101@gmail.com>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("BMI088 accelerometer driver (I2C)");
MODULE_IMPORT_NS(IIO_BMI088);

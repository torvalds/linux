// SPDX-License-Identifier: GPL-2.0-only
/*
 * 3-axis magnetometer driver supporting following I2C Bosch-Sensortec chips:
 *  - BMC150
 *  - BMC156
 *  - BMM150
 *
 * Copyright (c) 2016, Intel Corporation.
 */
#include <linux/device.h>
#include <linux/mod_devicetable.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/regmap.h>

#include "bmc150_magn.h"

static int bmc150_magn_i2c_probe(struct i2c_client *client)
{
	const struct i2c_device_id *id = i2c_client_get_device_id(client);
	struct regmap *regmap;
	const char *name = NULL;

	regmap = devm_regmap_init_i2c(client, &bmc150_magn_regmap_config);
	if (IS_ERR(regmap)) {
		dev_err(&client->dev, "Failed to initialize i2c regmap\n");
		return PTR_ERR(regmap);
	}

	if (id)
		name = id->name;

	return bmc150_magn_probe(&client->dev, regmap, client->irq, name);
}

static void bmc150_magn_i2c_remove(struct i2c_client *client)
{
	bmc150_magn_remove(&client->dev);
}

static const struct i2c_device_id bmc150_magn_i2c_id[] = {
	{ "bmc150_magn" },
	{ "bmc156_magn" },
	{ "bmm150_magn" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, bmc150_magn_i2c_id);

static const struct of_device_id bmc150_magn_of_match[] = {
	{ .compatible = "bosch,bmc150_magn" },
	{ .compatible = "bosch,bmc156_magn" },
	{ .compatible = "bosch,bmm150_magn" }, /* deprecated compatible */
	{ .compatible = "bosch,bmm150" },
	{ }
};
MODULE_DEVICE_TABLE(of, bmc150_magn_of_match);

static struct i2c_driver bmc150_magn_driver = {
	.driver = {
		.name	= "bmc150_magn_i2c",
		.of_match_table = bmc150_magn_of_match,
		.pm	= &bmc150_magn_pm_ops,
	},
	.probe		= bmc150_magn_i2c_probe,
	.remove		= bmc150_magn_i2c_remove,
	.id_table	= bmc150_magn_i2c_id,
};
module_i2c_driver(bmc150_magn_driver);

MODULE_AUTHOR("Daniel Baluta <daniel.baluta@intel.com");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("BMC150 I2C magnetometer driver");
MODULE_IMPORT_NS("IIO_BMC150_MAGN");

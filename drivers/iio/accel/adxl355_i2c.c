// SPDX-License-Identifier: GPL-2.0-only
/*
 * ADXL355 3-Axis Digital Accelerometer I2C driver
 *
 * Copyright (c) 2021 Puranjay Mohan <puranjay12@gmail.com>
 */

#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/regmap.h>

#include "adxl355.h"

static const struct regmap_config adxl355_i2c_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = 0x2F,
	.rd_table = &adxl355_readable_regs_tbl,
	.wr_table = &adxl355_writeable_regs_tbl,
};

static int adxl355_i2c_probe(struct i2c_client *client)
{
	struct regmap *regmap;

	regmap = devm_regmap_init_i2c(client, &adxl355_i2c_regmap_config);
	if (IS_ERR(regmap)) {
		dev_err(&client->dev, "Error initializing i2c regmap: %ld\n",
			PTR_ERR(regmap));

		return PTR_ERR(regmap);
	}

	return adxl355_core_probe(&client->dev, regmap, client->name);
}

static const struct i2c_device_id adxl355_i2c_id[] = {
	{ "adxl355", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, adxl355_i2c_id);

static const struct of_device_id adxl355_of_match[] = {
	{ .compatible = "adi,adxl355" },
	{ }
};
MODULE_DEVICE_TABLE(of, adxl355_of_match);

static struct i2c_driver adxl355_i2c_driver = {
	.driver = {
		.name	= "adxl355_i2c",
		.of_match_table = adxl355_of_match,
	},
	.probe_new	= adxl355_i2c_probe,
	.id_table	= adxl355_i2c_id,
};
module_i2c_driver(adxl355_i2c_driver);

MODULE_AUTHOR("Puranjay Mohan <puranjay12@gmail.com>");
MODULE_DESCRIPTION("ADXL355 3-Axis Digital Accelerometer I2C driver");
MODULE_LICENSE("GPL v2");

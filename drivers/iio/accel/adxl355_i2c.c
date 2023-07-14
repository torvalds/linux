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
	const struct adxl355_chip_info *chip_data;
	const struct i2c_device_id *adxl355;

	chip_data = device_get_match_data(&client->dev);
	if (!chip_data) {
		adxl355 = to_i2c_driver(client->dev.driver)->id_table;
		if (!adxl355)
			return -EINVAL;

		chip_data = (void *)i2c_match_id(adxl355, client)->driver_data;

		if (!chip_data)
			return -EINVAL;
	}

	regmap = devm_regmap_init_i2c(client, &adxl355_i2c_regmap_config);
	if (IS_ERR(regmap)) {
		dev_err(&client->dev, "Error initializing i2c regmap: %ld\n",
			PTR_ERR(regmap));

		return PTR_ERR(regmap);
	}

	return adxl355_core_probe(&client->dev, regmap, chip_data);
}

static const struct i2c_device_id adxl355_i2c_id[] = {
	{ "adxl355", (kernel_ulong_t)&adxl35x_chip_info[ADXL355] },
	{ "adxl359", (kernel_ulong_t)&adxl35x_chip_info[ADXL359] },
	{ }
};
MODULE_DEVICE_TABLE(i2c, adxl355_i2c_id);

static const struct of_device_id adxl355_of_match[] = {
	{ .compatible = "adi,adxl355", .data = &adxl35x_chip_info[ADXL355] },
	{ .compatible = "adi,adxl359", .data = &adxl35x_chip_info[ADXL359] },
	{ }
};
MODULE_DEVICE_TABLE(of, adxl355_of_match);

static struct i2c_driver adxl355_i2c_driver = {
	.driver = {
		.name	= "adxl355_i2c",
		.of_match_table = adxl355_of_match,
	},
	.probe		= adxl355_i2c_probe,
	.id_table	= adxl355_i2c_id,
};
module_i2c_driver(adxl355_i2c_driver);

MODULE_AUTHOR("Puranjay Mohan <puranjay12@gmail.com>");
MODULE_DESCRIPTION("ADXL355 3-Axis Digital Accelerometer I2C driver");
MODULE_LICENSE("GPL v2");
MODULE_IMPORT_NS(IIO_ADXL355);

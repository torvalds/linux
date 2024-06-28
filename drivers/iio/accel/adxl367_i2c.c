// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2021 Analog Devices, Inc.
 * Author: Cosmin Tanislav <cosmin.tanislav@analog.com>
 */

#include <linux/i2c.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/regmap.h>

#include "adxl367.h"

#define ADXL367_I2C_FIFO_DATA	0x18

struct adxl367_i2c_state {
	struct regmap *regmap;
};

static bool adxl367_readable_noinc_reg(struct device *dev, unsigned int reg)
{
	return reg == ADXL367_I2C_FIFO_DATA;
}

static int adxl367_i2c_read_fifo(void *context, __be16 *fifo_buf,
				 unsigned int fifo_entries)
{
	struct adxl367_i2c_state *st = context;

	return regmap_noinc_read(st->regmap, ADXL367_I2C_FIFO_DATA, fifo_buf,
				 fifo_entries * sizeof(*fifo_buf));
}

static const struct regmap_config adxl367_i2c_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.readable_noinc_reg = adxl367_readable_noinc_reg,
};

static const struct adxl367_ops adxl367_i2c_ops = {
	.read_fifo = adxl367_i2c_read_fifo,
};

static int adxl367_i2c_probe(struct i2c_client *client,
			     const struct i2c_device_id *id)
{
	struct adxl367_i2c_state *st;
	struct regmap *regmap;

	st = devm_kzalloc(&client->dev, sizeof(*st), GFP_KERNEL);
	if (!st)
		return -ENOMEM;

	regmap = devm_regmap_init_i2c(client, &adxl367_i2c_regmap_config);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	st->regmap = regmap;

	return adxl367_probe(&client->dev, &adxl367_i2c_ops, st, regmap,
			     client->irq);
}

static const struct i2c_device_id adxl367_i2c_id[] = {
	{ "adxl367", 0 },
	{ },
};
MODULE_DEVICE_TABLE(i2c, adxl367_i2c_id);

static const struct of_device_id adxl367_of_match[] = {
	{ .compatible = "adi,adxl367" },
	{ },
};
MODULE_DEVICE_TABLE(of, adxl367_of_match);

static struct i2c_driver adxl367_i2c_driver = {
	.driver = {
		.name = "adxl367_i2c",
		.of_match_table = adxl367_of_match,
	},
	.probe = adxl367_i2c_probe,
	.id_table = adxl367_i2c_id,
};

module_i2c_driver(adxl367_i2c_driver);

MODULE_IMPORT_NS(IIO_ADXL367);
MODULE_AUTHOR("Cosmin Tanislav <cosmin.tanislav@analog.com>");
MODULE_DESCRIPTION("Analog Devices ADXL367 3-axis accelerometer I2C driver");
MODULE_LICENSE("GPL");

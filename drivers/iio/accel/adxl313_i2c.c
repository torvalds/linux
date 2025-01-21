// SPDX-License-Identifier: GPL-2.0-only
/*
 * ADXL313 3-Axis Digital Accelerometer
 *
 * Copyright (c) 2021 Lucas Stankus <lucas.p.stankus@gmail.com>
 *
 * Datasheet: https://www.analog.com/media/en/technical-documentation/data-sheets/ADXL313.pdf
 */

#include <linux/i2c.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/regmap.h>

#include "adxl313.h"

static const struct regmap_config adxl31x_i2c_regmap_config[] = {
	[ADXL312] = {
		.reg_bits	= 8,
		.val_bits	= 8,
		.rd_table	= &adxl312_readable_regs_table,
		.wr_table	= &adxl312_writable_regs_table,
		.max_register	= 0x39,
	},
	[ADXL313] = {
		.reg_bits	= 8,
		.val_bits	= 8,
		.rd_table	= &adxl313_readable_regs_table,
		.wr_table	= &adxl313_writable_regs_table,
		.max_register	= 0x39,
	},
	[ADXL314] = {
		.reg_bits	= 8,
		.val_bits	= 8,
		.rd_table	= &adxl314_readable_regs_table,
		.wr_table	= &adxl314_writable_regs_table,
		.max_register	= 0x39,
	},
};

static const struct i2c_device_id adxl313_i2c_id[] = {
	{ .name = "adxl312", .driver_data = (kernel_ulong_t)&adxl31x_chip_info[ADXL312] },
	{ .name = "adxl313", .driver_data = (kernel_ulong_t)&adxl31x_chip_info[ADXL313] },
	{ .name = "adxl314", .driver_data = (kernel_ulong_t)&adxl31x_chip_info[ADXL314] },
	{ }
};

MODULE_DEVICE_TABLE(i2c, adxl313_i2c_id);

static const struct of_device_id adxl313_of_match[] = {
	{ .compatible = "adi,adxl312", .data = &adxl31x_chip_info[ADXL312] },
	{ .compatible = "adi,adxl313", .data = &adxl31x_chip_info[ADXL313] },
	{ .compatible = "adi,adxl314", .data = &adxl31x_chip_info[ADXL314] },
	{ }
};

MODULE_DEVICE_TABLE(of, adxl313_of_match);

static int adxl313_i2c_probe(struct i2c_client *client)
{
	const struct adxl313_chip_info *chip_data;
	struct regmap *regmap;

	/*
	 * Retrieves device specific data as a pointer to a
	 * adxl313_chip_info structure
	 */
	chip_data = i2c_get_match_data(client);

	regmap = devm_regmap_init_i2c(client,
				      &adxl31x_i2c_regmap_config[chip_data->type]);
	if (IS_ERR(regmap)) {
		dev_err(&client->dev, "Error initializing i2c regmap: %ld\n",
			PTR_ERR(regmap));
		return PTR_ERR(regmap);
	}

	return adxl313_core_probe(&client->dev, regmap, chip_data, NULL);
}

static struct i2c_driver adxl313_i2c_driver = {
	.driver = {
		.name	= "adxl313_i2c",
		.of_match_table = adxl313_of_match,
	},
	.probe		= adxl313_i2c_probe,
	.id_table	= adxl313_i2c_id,
};

module_i2c_driver(adxl313_i2c_driver);

MODULE_AUTHOR("Lucas Stankus <lucas.p.stankus@gmail.com>");
MODULE_DESCRIPTION("ADXL313 3-Axis Digital Accelerometer I2C driver");
MODULE_LICENSE("GPL v2");
MODULE_IMPORT_NS("IIO_ADXL313");

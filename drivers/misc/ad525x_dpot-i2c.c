// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Driver for the Analog Devices digital potentiometers (I2C bus)
 *
 * Copyright (C) 2010-2011 Michael Hennerich, Analog Devices Inc.
 */

#include <linux/i2c.h>
#include <linux/module.h>

#include "ad525x_dpot.h"

/* I2C bus functions */
static int write_d8(void *client, u8 val)
{
	return i2c_smbus_write_byte(client, val);
}

static int write_r8d8(void *client, u8 reg, u8 val)
{
	return i2c_smbus_write_byte_data(client, reg, val);
}

static int write_r8d16(void *client, u8 reg, u16 val)
{
	return i2c_smbus_write_word_data(client, reg, val);
}

static int read_d8(void *client)
{
	return i2c_smbus_read_byte(client);
}

static int read_r8d8(void *client, u8 reg)
{
	return i2c_smbus_read_byte_data(client, reg);
}

static int read_r8d16(void *client, u8 reg)
{
	return i2c_smbus_read_word_data(client, reg);
}

static const struct ad_dpot_bus_ops bops = {
	.read_d8	= read_d8,
	.read_r8d8	= read_r8d8,
	.read_r8d16	= read_r8d16,
	.write_d8	= write_d8,
	.write_r8d8	= write_r8d8,
	.write_r8d16	= write_r8d16,
};

static int ad_dpot_i2c_probe(struct i2c_client *client)
{
	const struct i2c_device_id *id = i2c_client_get_device_id(client);
	struct ad_dpot_bus_data bdata = {
		.client = client,
		.bops = &bops,
	};

	if (!i2c_check_functionality(client->adapter,
				     I2C_FUNC_SMBUS_WORD_DATA)) {
		dev_err(&client->dev, "SMBUS Word Data not Supported\n");
		return -EIO;
	}

	return ad_dpot_probe(&client->dev, &bdata, id->driver_data, id->name);
}

static void ad_dpot_i2c_remove(struct i2c_client *client)
{
	ad_dpot_remove(&client->dev);
}

static const struct i2c_device_id ad_dpot_id[] = {
	{ .name = "ad5258", .driver_data = AD5258_ID },
	{ .name = "ad5259", .driver_data = AD5259_ID },
	{ .name = "ad5251", .driver_data = AD5251_ID },
	{ .name = "ad5252", .driver_data = AD5252_ID },
	{ .name = "ad5253", .driver_data = AD5253_ID },
	{ .name = "ad5254", .driver_data = AD5254_ID },
	{ .name = "ad5255", .driver_data = AD5255_ID },
	{ .name = "ad5241", .driver_data = AD5241_ID },
	{ .name = "ad5242", .driver_data = AD5242_ID },
	{ .name = "ad5243", .driver_data = AD5243_ID },
	{ .name = "ad5245", .driver_data = AD5245_ID },
	{ .name = "ad5246", .driver_data = AD5246_ID },
	{ .name = "ad5247", .driver_data = AD5247_ID },
	{ .name = "ad5248", .driver_data = AD5248_ID },
	{ .name = "ad5280", .driver_data = AD5280_ID },
	{ .name = "ad5282", .driver_data = AD5282_ID },
	{ .name = "adn2860", .driver_data = ADN2860_ID },
	{ .name = "ad5273", .driver_data = AD5273_ID },
	{ .name = "ad5161", .driver_data = AD5161_ID },
	{ .name = "ad5171", .driver_data = AD5171_ID },
	{ .name = "ad5170", .driver_data = AD5170_ID },
	{ .name = "ad5172", .driver_data = AD5172_ID },
	{ .name = "ad5173", .driver_data = AD5173_ID },
	{ .name = "ad5272", .driver_data = AD5272_ID },
	{ .name = "ad5274", .driver_data = AD5274_ID },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ad_dpot_id);

static struct i2c_driver ad_dpot_i2c_driver = {
	.driver = {
		.name	= "ad_dpot",
		.dev_groups = ad_dpot_groups,
	},
	.probe		= ad_dpot_i2c_probe,
	.remove		= ad_dpot_i2c_remove,
	.id_table	= ad_dpot_id,
};

module_i2c_driver(ad_dpot_i2c_driver);

MODULE_AUTHOR("Michael Hennerich <michael.hennerich@analog.com>");
MODULE_DESCRIPTION("digital potentiometer I2C bus driver");
MODULE_LICENSE("GPL");

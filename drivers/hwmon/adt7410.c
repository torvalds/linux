// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * ADT7410/ADT7420 digital temperature sensor driver
 *
 * Copyright 2012-2013 Analog Devices Inc.
 *   Author: Lars-Peter Clausen <lars@metafoo.de>
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/i2c.h>

#include "adt7x10.h"

static int adt7410_i2c_read_word(struct device *dev, u8 reg)
{
	return i2c_smbus_read_word_swapped(to_i2c_client(dev), reg);
}

static int adt7410_i2c_write_word(struct device *dev, u8 reg, u16 data)
{
	return i2c_smbus_write_word_swapped(to_i2c_client(dev), reg, data);
}

static int adt7410_i2c_read_byte(struct device *dev, u8 reg)
{
	return i2c_smbus_read_byte_data(to_i2c_client(dev), reg);
}

static int adt7410_i2c_write_byte(struct device *dev, u8 reg, u8 data)
{
	return i2c_smbus_write_byte_data(to_i2c_client(dev), reg, data);
}

static const struct adt7x10_ops adt7410_i2c_ops = {
	.read_word = adt7410_i2c_read_word,
	.write_word = adt7410_i2c_write_word,
	.read_byte = adt7410_i2c_read_byte,
	.write_byte = adt7410_i2c_write_byte,
};

static int adt7410_i2c_probe(struct i2c_client *client,
	const struct i2c_device_id *id)
{
	if (!i2c_check_functionality(client->adapter,
			I2C_FUNC_SMBUS_BYTE_DATA | I2C_FUNC_SMBUS_WORD_DATA))
		return -ENODEV;

	return adt7x10_probe(&client->dev, NULL, client->irq, &adt7410_i2c_ops);
}

static int adt7410_i2c_remove(struct i2c_client *client)
{
	return adt7x10_remove(&client->dev, client->irq);
}

static const struct i2c_device_id adt7410_ids[] = {
	{ "adt7410", 0 },
	{ "adt7420", 0 },
	{}
};
MODULE_DEVICE_TABLE(i2c, adt7410_ids);

static struct i2c_driver adt7410_driver = {
	.class		= I2C_CLASS_HWMON,
	.driver = {
		.name	= "adt7410",
		.pm	= ADT7X10_DEV_PM_OPS,
	},
	.probe		= adt7410_i2c_probe,
	.remove		= adt7410_i2c_remove,
	.id_table	= adt7410_ids,
	.address_list	= I2C_ADDRS(0x48, 0x49, 0x4a, 0x4b),
};
module_i2c_driver(adt7410_driver);

MODULE_AUTHOR("Lars-Peter Clausen <lars@metafoo.de>");
MODULE_DESCRIPTION("ADT7410/AD7420 driver");
MODULE_LICENSE("GPL");

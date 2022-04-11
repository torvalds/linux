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
#include <linux/regmap.h>

#include "adt7x10.h"

static bool adt7410_regmap_is_volatile(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case ADT7X10_TEMPERATURE:
	case ADT7X10_STATUS:
		return true;
	default:
		return false;
	}
}

static int adt7410_reg_read(void *context, unsigned int reg, unsigned int *val)
{
	struct i2c_client *client = context;
	int regval;

	switch (reg) {
	case ADT7X10_TEMPERATURE:
	case ADT7X10_T_ALARM_HIGH:
	case ADT7X10_T_ALARM_LOW:
	case ADT7X10_T_CRIT:
		regval = i2c_smbus_read_word_swapped(client, reg);
		break;
	default:
		regval = i2c_smbus_read_byte_data(client, reg);
		break;
	}
	if (regval < 0)
		return regval;
	*val = regval;
	return 0;
}

static int adt7410_reg_write(void *context, unsigned int reg, unsigned int val)
{
	struct i2c_client *client = context;
	int ret;

	switch (reg) {
	case ADT7X10_TEMPERATURE:
	case ADT7X10_T_ALARM_HIGH:
	case ADT7X10_T_ALARM_LOW:
	case ADT7X10_T_CRIT:
		ret = i2c_smbus_write_word_swapped(client, reg, val);
		break;
	default:
		ret = i2c_smbus_write_byte_data(client, reg, val);
		break;
	}
	return ret;
}

static const struct regmap_config adt7410_regmap_config = {
	.reg_bits = 8,
	.val_bits = 16,
	.max_register = ADT7X10_ID,
	.cache_type = REGCACHE_RBTREE,
	.volatile_reg = adt7410_regmap_is_volatile,
	.reg_read = adt7410_reg_read,
	.reg_write = adt7410_reg_write,
};

static int adt7410_i2c_probe(struct i2c_client *client)
{
	struct regmap *regmap;

	regmap = devm_regmap_init(&client->dev, NULL, client,
				  &adt7410_regmap_config);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	return adt7x10_probe(&client->dev, client->name, client->irq, regmap);
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
	.probe_new	= adt7410_i2c_probe,
	.id_table	= adt7410_ids,
	.address_list	= I2C_ADDRS(0x48, 0x49, 0x4a, 0x4b),
};
module_i2c_driver(adt7410_driver);

MODULE_AUTHOR("Lars-Peter Clausen <lars@metafoo.de>");
MODULE_DESCRIPTION("ADT7410/AD7420 driver");
MODULE_LICENSE("GPL");

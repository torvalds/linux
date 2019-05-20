/*
 * Texas Instruments TMP103 SMBus temperature sensor driver
 * Copyright (C) 2014 Heiko Schocher <hs@denx.de>
 *
 * Based on:
 * Texas Instruments TMP102 SMBus temperature sensor driver
 *
 * Copyright (C) 2010 Steven King <sfking@fdwdc.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/err.h>
#include <linux/mutex.h>
#include <linux/device.h>
#include <linux/jiffies.h>
#include <linux/regmap.h>

#define TMP103_TEMP_REG		0x00
#define TMP103_CONF_REG		0x01
#define TMP103_TLOW_REG		0x02
#define TMP103_THIGH_REG	0x03

#define TMP103_CONF_M0		0x01
#define TMP103_CONF_M1		0x02
#define TMP103_CONF_LC		0x04
#define TMP103_CONF_FL		0x08
#define TMP103_CONF_FH		0x10
#define TMP103_CONF_CR0		0x20
#define TMP103_CONF_CR1		0x40
#define TMP103_CONF_ID		0x80
#define TMP103_CONF_SD		(TMP103_CONF_M1)
#define TMP103_CONF_SD_MASK	(TMP103_CONF_M0 | TMP103_CONF_M1)

#define TMP103_CONFIG		(TMP103_CONF_CR1 | TMP103_CONF_M1)
#define TMP103_CONFIG_MASK	(TMP103_CONF_CR0 | TMP103_CONF_CR1 | \
				 TMP103_CONF_M0 | TMP103_CONF_M1)

static inline int tmp103_reg_to_mc(s8 val)
{
	return val * 1000;
}

static inline u8 tmp103_mc_to_reg(int val)
{
	return DIV_ROUND_CLOSEST(val, 1000);
}

static ssize_t tmp103_temp_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct sensor_device_attribute *sda = to_sensor_dev_attr(attr);
	struct regmap *regmap = dev_get_drvdata(dev);
	unsigned int regval;
	int ret;

	ret = regmap_read(regmap, sda->index, &regval);
	if (ret < 0)
		return ret;

	return sprintf(buf, "%d\n", tmp103_reg_to_mc(regval));
}

static ssize_t tmp103_temp_store(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t count)
{
	struct sensor_device_attribute *sda = to_sensor_dev_attr(attr);
	struct regmap *regmap = dev_get_drvdata(dev);
	long val;
	int ret;

	if (kstrtol(buf, 10, &val) < 0)
		return -EINVAL;

	val = clamp_val(val, -55000, 127000);
	ret = regmap_write(regmap, sda->index, tmp103_mc_to_reg(val));
	return ret ? ret : count;
}

static SENSOR_DEVICE_ATTR_RO(temp1_input, tmp103_temp, TMP103_TEMP_REG);

static SENSOR_DEVICE_ATTR_RW(temp1_min, tmp103_temp, TMP103_TLOW_REG);

static SENSOR_DEVICE_ATTR_RW(temp1_max, tmp103_temp, TMP103_THIGH_REG);

static struct attribute *tmp103_attrs[] = {
	&sensor_dev_attr_temp1_input.dev_attr.attr,
	&sensor_dev_attr_temp1_min.dev_attr.attr,
	&sensor_dev_attr_temp1_max.dev_attr.attr,
	NULL
};
ATTRIBUTE_GROUPS(tmp103);

static bool tmp103_regmap_is_volatile(struct device *dev, unsigned int reg)
{
	return reg == TMP103_TEMP_REG;
}

static const struct regmap_config tmp103_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = TMP103_THIGH_REG,
	.volatile_reg = tmp103_regmap_is_volatile,
};

static int tmp103_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct device *hwmon_dev;
	struct regmap *regmap;
	int ret;

	regmap = devm_regmap_init_i2c(client, &tmp103_regmap_config);
	if (IS_ERR(regmap)) {
		dev_err(dev, "failed to allocate register map\n");
		return PTR_ERR(regmap);
	}

	ret = regmap_update_bits(regmap, TMP103_CONF_REG, TMP103_CONFIG_MASK,
				 TMP103_CONFIG);
	if (ret < 0) {
		dev_err(&client->dev, "error writing config register\n");
		return ret;
	}

	i2c_set_clientdata(client, regmap);
	hwmon_dev = devm_hwmon_device_register_with_groups(dev, client->name,
						      regmap, tmp103_groups);
	return PTR_ERR_OR_ZERO(hwmon_dev);
}

static int __maybe_unused tmp103_suspend(struct device *dev)
{
	struct regmap *regmap = dev_get_drvdata(dev);

	return regmap_update_bits(regmap, TMP103_CONF_REG,
				  TMP103_CONF_SD_MASK, 0);
}

static int __maybe_unused tmp103_resume(struct device *dev)
{
	struct regmap *regmap = dev_get_drvdata(dev);

	return regmap_update_bits(regmap, TMP103_CONF_REG,
				  TMP103_CONF_SD_MASK, TMP103_CONF_SD);
}

static SIMPLE_DEV_PM_OPS(tmp103_dev_pm_ops, tmp103_suspend, tmp103_resume);

static const struct i2c_device_id tmp103_id[] = {
	{ "tmp103", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, tmp103_id);

static const struct of_device_id __maybe_unused tmp103_of_match[] = {
	{ .compatible = "ti,tmp103" },
	{ },
};
MODULE_DEVICE_TABLE(of, tmp103_of_match);

static struct i2c_driver tmp103_driver = {
	.driver = {
		.name	= "tmp103",
		.of_match_table = of_match_ptr(tmp103_of_match),
		.pm	= &tmp103_dev_pm_ops,
	},
	.probe		= tmp103_probe,
	.id_table	= tmp103_id,
};

module_i2c_driver(tmp103_driver);

MODULE_AUTHOR("Heiko Schocher <hs@denx.de>");
MODULE_DESCRIPTION("Texas Instruments TMP103 temperature sensor driver");
MODULE_LICENSE("GPL");

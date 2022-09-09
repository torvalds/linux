// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * An hwmon driver for the Analog Devices AD7414
 *
 * Copyright 2006 Stefan Roese <sr at denx.de>, DENX Software Engineering
 *
 * Copyright (c) 2008 PIKA Technologies
 *   Sean MacLennan <smaclennan@pikatech.com>
 *
 * Copyright (c) 2008 Spansion Inc.
 *   Frank Edelhaeuser <frank.edelhaeuser at spansion.com>
 *   (converted to "new style" I2C driver model, removed checkpatch.pl warnings)
 *
 * Based on ad7418.c
 * Copyright 2006 Tower Technologies, Alessandro Zummo <a.zummo at towertech.it>
 */

#include <linux/module.h>
#include <linux/jiffies.h>
#include <linux/i2c.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/err.h>
#include <linux/mutex.h>
#include <linux/sysfs.h>
#include <linux/slab.h>


/* AD7414 registers */
#define AD7414_REG_TEMP		0x00
#define AD7414_REG_CONF		0x01
#define AD7414_REG_T_HIGH	0x02
#define AD7414_REG_T_LOW	0x03

static u8 AD7414_REG_LIMIT[] = { AD7414_REG_T_HIGH, AD7414_REG_T_LOW };

struct ad7414_data {
	struct i2c_client	*client;
	struct mutex		lock;	/* atomic read data updates */
	bool			valid;	/* true if following fields are valid */
	unsigned long		next_update;	/* In jiffies */
	s16			temp_input;	/* Register values */
	s8			temps[ARRAY_SIZE(AD7414_REG_LIMIT)];
};

/* REG: (0.25C/bit, two's complement) << 6 */
static inline int ad7414_temp_from_reg(s16 reg)
{
	/*
	 * use integer division instead of equivalent right shift to
	 * guarantee arithmetic shift and preserve the sign
	 */
	return ((int)reg / 64) * 250;
}

static inline int ad7414_read(struct i2c_client *client, u8 reg)
{
	if (reg == AD7414_REG_TEMP)
		return i2c_smbus_read_word_swapped(client, reg);
	else
		return i2c_smbus_read_byte_data(client, reg);
}

static inline int ad7414_write(struct i2c_client *client, u8 reg, u8 value)
{
	return i2c_smbus_write_byte_data(client, reg, value);
}

static struct ad7414_data *ad7414_update_device(struct device *dev)
{
	struct ad7414_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;

	mutex_lock(&data->lock);

	if (time_after(jiffies, data->next_update) || !data->valid) {
		int value, i;

		dev_dbg(&client->dev, "starting ad7414 update\n");

		value = ad7414_read(client, AD7414_REG_TEMP);
		if (value < 0)
			dev_dbg(&client->dev, "AD7414_REG_TEMP err %d\n",
				value);
		else
			data->temp_input = value;

		for (i = 0; i < ARRAY_SIZE(AD7414_REG_LIMIT); ++i) {
			value = ad7414_read(client, AD7414_REG_LIMIT[i]);
			if (value < 0)
				dev_dbg(&client->dev, "AD7414 reg %d err %d\n",
					AD7414_REG_LIMIT[i], value);
			else
				data->temps[i] = value;
		}

		data->next_update = jiffies + HZ + HZ / 2;
		data->valid = true;
	}

	mutex_unlock(&data->lock);

	return data;
}

static ssize_t temp_input_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct ad7414_data *data = ad7414_update_device(dev);
	return sprintf(buf, "%d\n", ad7414_temp_from_reg(data->temp_input));
}
static SENSOR_DEVICE_ATTR_RO(temp1_input, temp_input, 0);

static ssize_t max_min_show(struct device *dev, struct device_attribute *attr,
			    char *buf)
{
	int index = to_sensor_dev_attr(attr)->index;
	struct ad7414_data *data = ad7414_update_device(dev);
	return sprintf(buf, "%d\n", data->temps[index] * 1000);
}

static ssize_t max_min_store(struct device *dev,
			     struct device_attribute *attr, const char *buf,
			     size_t count)
{
	struct ad7414_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	int index = to_sensor_dev_attr(attr)->index;
	u8 reg = AD7414_REG_LIMIT[index];
	long temp;
	int ret = kstrtol(buf, 10, &temp);

	if (ret < 0)
		return ret;

	temp = clamp_val(temp, -40000, 85000);
	temp = (temp + (temp < 0 ? -500 : 500)) / 1000;

	mutex_lock(&data->lock);
	data->temps[index] = temp;
	ad7414_write(client, reg, temp);
	mutex_unlock(&data->lock);
	return count;
}

static SENSOR_DEVICE_ATTR_RW(temp1_max, max_min, 0);
static SENSOR_DEVICE_ATTR_RW(temp1_min, max_min, 1);

static ssize_t alarm_show(struct device *dev, struct device_attribute *attr,
			  char *buf)
{
	int bitnr = to_sensor_dev_attr(attr)->index;
	struct ad7414_data *data = ad7414_update_device(dev);
	int value = (data->temp_input >> bitnr) & 1;
	return sprintf(buf, "%d\n", value);
}

static SENSOR_DEVICE_ATTR_RO(temp1_min_alarm, alarm, 3);
static SENSOR_DEVICE_ATTR_RO(temp1_max_alarm, alarm, 4);

static struct attribute *ad7414_attrs[] = {
	&sensor_dev_attr_temp1_input.dev_attr.attr,
	&sensor_dev_attr_temp1_max.dev_attr.attr,
	&sensor_dev_attr_temp1_min.dev_attr.attr,
	&sensor_dev_attr_temp1_max_alarm.dev_attr.attr,
	&sensor_dev_attr_temp1_min_alarm.dev_attr.attr,
	NULL
};

ATTRIBUTE_GROUPS(ad7414);

static int ad7414_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct ad7414_data *data;
	struct device *hwmon_dev;
	int conf;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_BYTE_DATA |
				     I2C_FUNC_SMBUS_READ_WORD_DATA))
		return -EOPNOTSUPP;

	data = devm_kzalloc(dev, sizeof(struct ad7414_data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->client = client;
	mutex_init(&data->lock);

	dev_info(&client->dev, "chip found\n");

	/* Make sure the chip is powered up. */
	conf = i2c_smbus_read_byte_data(client, AD7414_REG_CONF);
	if (conf < 0)
		dev_warn(dev, "ad7414_probe unable to read config register.\n");
	else {
		conf &= ~(1 << 7);
		i2c_smbus_write_byte_data(client, AD7414_REG_CONF, conf);
	}

	hwmon_dev = devm_hwmon_device_register_with_groups(dev,
							   client->name,
							   data, ad7414_groups);
	return PTR_ERR_OR_ZERO(hwmon_dev);
}

static const struct i2c_device_id ad7414_id[] = {
	{ "ad7414", 0 },
	{}
};
MODULE_DEVICE_TABLE(i2c, ad7414_id);

static const struct of_device_id __maybe_unused ad7414_of_match[] = {
	{ .compatible = "ad,ad7414" },
	{ },
};
MODULE_DEVICE_TABLE(of, ad7414_of_match);

static struct i2c_driver ad7414_driver = {
	.driver = {
		.name	= "ad7414",
		.of_match_table = of_match_ptr(ad7414_of_match),
	},
	.probe_new = ad7414_probe,
	.id_table = ad7414_id,
};

module_i2c_driver(ad7414_driver);

MODULE_AUTHOR("Stefan Roese <sr at denx.de>, "
	      "Frank Edelhaeuser <frank.edelhaeuser at spansion.com>");

MODULE_DESCRIPTION("AD7414 driver");
MODULE_LICENSE("GPL");

/*
 * Measurement Specialties HTU21D humidity and temperature sensor driver
 *
 * Copyright (C) 2013 William Markezana <william.markezana@meas-spec.com>
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

/* HTU21 Commands */
#define HTU21_T_MEASUREMENT_HM	0xE3
#define HTU21_RH_MEASUREMENT_HM	0xE5

struct htu21 {
	struct i2c_client *client;
	struct mutex lock;
	bool valid;
	unsigned long last_update;
	int temperature;
	int humidity;
};

static inline int htu21_temp_ticks_to_millicelsius(int ticks)
{
	ticks &= ~0x0003; /* clear status bits */
	/*
	 * Formula T = -46.85 + 175.72 * ST / 2^16 from datasheet p14,
	 * optimized for integer fixed point (3 digits) arithmetic
	 */
	return ((21965 * ticks) >> 13) - 46850;
}

static inline int htu21_rh_ticks_to_per_cent_mille(int ticks)
{
	ticks &= ~0x0003; /* clear status bits */
	/*
	 * Formula RH = -6 + 125 * SRH / 2^16 from datasheet p14,
	 * optimized for integer fixed point (3 digits) arithmetic
	 */
	return ((15625 * ticks) >> 13) - 6000;
}

static int htu21_update_measurements(struct device *dev)
{
	struct htu21 *htu21 = dev_get_drvdata(dev);
	struct i2c_client *client = htu21->client;
	int ret = 0;

	mutex_lock(&htu21->lock);

	if (time_after(jiffies, htu21->last_update + HZ / 2) ||
	    !htu21->valid) {
		ret = i2c_smbus_read_word_swapped(client,
						  HTU21_T_MEASUREMENT_HM);
		if (ret < 0)
			goto out;
		htu21->temperature = htu21_temp_ticks_to_millicelsius(ret);
		ret = i2c_smbus_read_word_swapped(client,
						  HTU21_RH_MEASUREMENT_HM);
		if (ret < 0)
			goto out;
		htu21->humidity = htu21_rh_ticks_to_per_cent_mille(ret);
		htu21->last_update = jiffies;
		htu21->valid = true;
	}
out:
	mutex_unlock(&htu21->lock);

	return ret >= 0 ? 0 : ret;
}

static ssize_t htu21_show_temperature(struct device *dev,
				      struct device_attribute *attr, char *buf)
{
	struct htu21 *htu21 = dev_get_drvdata(dev);
	int ret;

	ret = htu21_update_measurements(dev);
	if (ret < 0)
		return ret;
	return sprintf(buf, "%d\n", htu21->temperature);
}

static ssize_t htu21_show_humidity(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct htu21 *htu21 = dev_get_drvdata(dev);
	int ret;

	ret = htu21_update_measurements(dev);
	if (ret < 0)
		return ret;
	return sprintf(buf, "%d\n", htu21->humidity);
}

static SENSOR_DEVICE_ATTR(temp1_input, S_IRUGO,
			  htu21_show_temperature, NULL, 0);
static SENSOR_DEVICE_ATTR(humidity1_input, S_IRUGO,
			  htu21_show_humidity, NULL, 0);

static struct attribute *htu21_attrs[] = {
	&sensor_dev_attr_temp1_input.dev_attr.attr,
	&sensor_dev_attr_humidity1_input.dev_attr.attr,
	NULL
};

ATTRIBUTE_GROUPS(htu21);

static int htu21_probe(struct i2c_client *client,
		       const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct htu21 *htu21;
	struct device *hwmon_dev;

	if (!i2c_check_functionality(client->adapter,
				     I2C_FUNC_SMBUS_READ_WORD_DATA)) {
		dev_err(&client->dev,
			"adapter does not support SMBus word transactions\n");
		return -ENODEV;
	}

	htu21 = devm_kzalloc(dev, sizeof(*htu21), GFP_KERNEL);
	if (!htu21)
		return -ENOMEM;

	htu21->client = client;
	mutex_init(&htu21->lock);

	hwmon_dev = devm_hwmon_device_register_with_groups(dev, client->name,
							   htu21,
							   htu21_groups);
	return PTR_ERR_OR_ZERO(hwmon_dev);
}

static const struct i2c_device_id htu21_id[] = {
	{ "htu21", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, htu21_id);

static struct i2c_driver htu21_driver = {
	.class = I2C_CLASS_HWMON,
	.driver = {
		.name	= "htu21",
	},
	.probe       = htu21_probe,
	.id_table    = htu21_id,
};

module_i2c_driver(htu21_driver);

MODULE_AUTHOR("William Markezana <william.markezana@meas-spec.com>");
MODULE_DESCRIPTION("MEAS HTU21D humidity and temperature sensor driver");
MODULE_LICENSE("GPL");

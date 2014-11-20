/*
 * Driver for Linear Technology LTC4260 I2C Positive Voltage Hot Swap Controller
 *
 * Copyright (c) 2014 Guenter Roeck
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
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/jiffies.h>
#include <linux/regmap.h>

/* chip registers */
#define LTC4260_CONTROL	0x00
#define LTC4260_ALERT	0x01
#define LTC4260_STATUS	0x02
#define LTC4260_FAULT	0x03
#define LTC4260_SENSE	0x04
#define LTC4260_SOURCE	0x05
#define LTC4260_ADIN	0x06

/*
 * Fault register bits
 */
#define FAULT_OV	(1 << 0)
#define FAULT_UV	(1 << 1)
#define FAULT_OC	(1 << 2)
#define FAULT_POWER_BAD	(1 << 3)
#define FAULT_FET_SHORT	(1 << 5)

/* Return the voltage from the given register in mV or mA */
static int ltc4260_get_value(struct device *dev, u8 reg)
{
	struct regmap *regmap = dev_get_drvdata(dev);
	unsigned int val;
	int ret;

	ret = regmap_read(regmap, reg, &val);
	if (ret < 0)
		return ret;

	switch (reg) {
	case LTC4260_ADIN:
		/* 10 mV resolution. Convert to mV. */
		val = val * 10;
		break;
	case LTC4260_SOURCE:
		/* 400 mV resolution. Convert to mV. */
		val = val * 400;
		break;
	case LTC4260_SENSE:
		/*
		 * 300 uV resolution. Convert to current as measured with
		 * an 1 mOhm sense resistor, in mA. If a different sense
		 * resistor is installed, calculate the actual current by
		 * dividing the reported current by the sense resistor value
		 * in mOhm.
		 */
		val = val * 300;
		break;
	default:
		return -EINVAL;
	}

	return val;
}

static ssize_t ltc4260_show_value(struct device *dev,
				  struct device_attribute *da, char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(da);
	int value;

	value = ltc4260_get_value(dev, attr->index);
	if (value < 0)
		return value;
	return snprintf(buf, PAGE_SIZE, "%d\n", value);
}

static ssize_t ltc4260_show_bool(struct device *dev,
				 struct device_attribute *da, char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(da);
	struct regmap *regmap = dev_get_drvdata(dev);
	unsigned int fault;
	int ret;

	ret = regmap_read(regmap, LTC4260_FAULT, &fault);
	if (ret < 0)
		return ret;

	fault &= attr->index;
	if (fault)		/* Clear reported faults in chip register */
		regmap_update_bits(regmap, LTC4260_FAULT, attr->index, 0);

	return snprintf(buf, PAGE_SIZE, "%d\n", !!fault);
}

/* Voltages */
static SENSOR_DEVICE_ATTR(in1_input, S_IRUGO, ltc4260_show_value, NULL,
			  LTC4260_SOURCE);
static SENSOR_DEVICE_ATTR(in2_input, S_IRUGO, ltc4260_show_value, NULL,
			  LTC4260_ADIN);

/*
 * Voltage alarms
 * UV/OV faults are associated with the input voltage, and the POWER BAD and
 * FET SHORT faults are associated with the output voltage.
 */
static SENSOR_DEVICE_ATTR(in1_min_alarm, S_IRUGO, ltc4260_show_bool, NULL,
			  FAULT_UV);
static SENSOR_DEVICE_ATTR(in1_max_alarm, S_IRUGO, ltc4260_show_bool, NULL,
			  FAULT_OV);
static SENSOR_DEVICE_ATTR(in2_alarm, S_IRUGO, ltc4260_show_bool, NULL,
			  FAULT_POWER_BAD | FAULT_FET_SHORT);

/* Current (via sense resistor) */
static SENSOR_DEVICE_ATTR(curr1_input, S_IRUGO, ltc4260_show_value, NULL,
			  LTC4260_SENSE);

/* Overcurrent alarm */
static SENSOR_DEVICE_ATTR(curr1_max_alarm, S_IRUGO, ltc4260_show_bool, NULL,
			  FAULT_OC);

static struct attribute *ltc4260_attrs[] = {
	&sensor_dev_attr_in1_input.dev_attr.attr,
	&sensor_dev_attr_in1_min_alarm.dev_attr.attr,
	&sensor_dev_attr_in1_max_alarm.dev_attr.attr,
	&sensor_dev_attr_in2_input.dev_attr.attr,
	&sensor_dev_attr_in2_alarm.dev_attr.attr,

	&sensor_dev_attr_curr1_input.dev_attr.attr,
	&sensor_dev_attr_curr1_max_alarm.dev_attr.attr,

	NULL,
};
ATTRIBUTE_GROUPS(ltc4260);

static const struct regmap_config ltc4260_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = LTC4260_ADIN,
};

static int ltc4260_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct device *hwmon_dev;
	struct regmap *regmap;

	regmap = devm_regmap_init_i2c(client, &ltc4260_regmap_config);
	if (IS_ERR(regmap)) {
		dev_err(dev, "failed to allocate register map\n");
		return PTR_ERR(regmap);
	}

	/* Clear faults */
	regmap_write(regmap, LTC4260_FAULT, 0x00);

	hwmon_dev = devm_hwmon_device_register_with_groups(dev, client->name,
							   regmap,
							   ltc4260_groups);
	return PTR_ERR_OR_ZERO(hwmon_dev);
}

static const struct i2c_device_id ltc4260_id[] = {
	{"ltc4260", 0},
	{ }
};

MODULE_DEVICE_TABLE(i2c, ltc4260_id);

static struct i2c_driver ltc4260_driver = {
	.driver = {
		   .name = "ltc4260",
		   },
	.probe = ltc4260_probe,
	.id_table = ltc4260_id,
};

module_i2c_driver(ltc4260_driver);

MODULE_AUTHOR("Guenter Roeck <linux@roeck-us.net>");
MODULE_DESCRIPTION("LTC4260 driver");
MODULE_LICENSE("GPL");

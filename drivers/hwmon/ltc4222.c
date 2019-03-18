/*
 * Driver for Linear Technology LTC4222 Dual Hot Swap controller
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
#include <linux/bitops.h>
#include <linux/i2c.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/jiffies.h>
#include <linux/regmap.h>

/* chip registers */

#define LTC4222_CONTROL1	0xd0
#define LTC4222_ALERT1		0xd1
#define LTC4222_STATUS1		0xd2
#define LTC4222_FAULT1		0xd3
#define LTC4222_CONTROL2	0xd4
#define LTC4222_ALERT2		0xd5
#define LTC4222_STATUS2		0xd6
#define LTC4222_FAULT2		0xd7
#define LTC4222_SOURCE1		0xd8
#define LTC4222_SOURCE2		0xda
#define LTC4222_ADIN1		0xdc
#define LTC4222_ADIN2		0xde
#define LTC4222_SENSE1		0xe0
#define LTC4222_SENSE2		0xe2
#define LTC4222_ADC_CONTROL	0xe4

/*
 * Fault register bits
 */
#define FAULT_OV	BIT(0)
#define FAULT_UV	BIT(1)
#define FAULT_OC	BIT(2)
#define FAULT_POWER_BAD	BIT(3)
#define FAULT_FET_BAD	BIT(5)

/* Return the voltage from the given register in mV or mA */
static int ltc4222_get_value(struct device *dev, u8 reg)
{
	struct regmap *regmap = dev_get_drvdata(dev);
	unsigned int val;
	u8 buf[2];
	int ret;

	ret = regmap_bulk_read(regmap, reg, buf, 2);
	if (ret < 0)
		return ret;

	val = ((buf[0] << 8) + buf[1]) >> 6;

	switch (reg) {
	case LTC4222_ADIN1:
	case LTC4222_ADIN2:
		/* 1.25 mV resolution. Convert to mV. */
		val = DIV_ROUND_CLOSEST(val * 5, 4);
		break;
	case LTC4222_SOURCE1:
	case LTC4222_SOURCE2:
		/* 31.25 mV resolution. Convert to mV. */
		val = DIV_ROUND_CLOSEST(val * 125, 4);
		break;
	case LTC4222_SENSE1:
	case LTC4222_SENSE2:
		/*
		 * 62.5 uV resolution. Convert to current as measured with
		 * an 1 mOhm sense resistor, in mA. If a different sense
		 * resistor is installed, calculate the actual current by
		 * dividing the reported current by the sense resistor value
		 * in mOhm.
		 */
		val = DIV_ROUND_CLOSEST(val * 125, 2);
		break;
	default:
		return -EINVAL;
	}
	return val;
}

static ssize_t ltc4222_value_show(struct device *dev,
				  struct device_attribute *da, char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(da);
	int value;

	value = ltc4222_get_value(dev, attr->index);
	if (value < 0)
		return value;
	return snprintf(buf, PAGE_SIZE, "%d\n", value);
}

static ssize_t ltc4222_bool_show(struct device *dev,
				 struct device_attribute *da, char *buf)
{
	struct sensor_device_attribute_2 *attr = to_sensor_dev_attr_2(da);
	struct regmap *regmap = dev_get_drvdata(dev);
	unsigned int fault;
	int ret;

	ret = regmap_read(regmap, attr->nr, &fault);
	if (ret < 0)
		return ret;
	fault &= attr->index;
	if (fault)		/* Clear reported faults in chip register */
		regmap_update_bits(regmap, attr->nr, attr->index, 0);

	return snprintf(buf, PAGE_SIZE, "%d\n", !!fault);
}

/* Voltages */
static SENSOR_DEVICE_ATTR_RO(in1_input, ltc4222_value, LTC4222_SOURCE1);
static SENSOR_DEVICE_ATTR_RO(in2_input, ltc4222_value, LTC4222_ADIN1);
static SENSOR_DEVICE_ATTR_RO(in3_input, ltc4222_value, LTC4222_SOURCE2);
static SENSOR_DEVICE_ATTR_RO(in4_input, ltc4222_value, LTC4222_ADIN2);

/*
 * Voltage alarms
 * UV/OV faults are associated with the input voltage, and power bad and fet
 * faults are associated with the output voltage.
 */
static SENSOR_DEVICE_ATTR_2_RO(in1_min_alarm, ltc4222_bool, LTC4222_FAULT1,
			       FAULT_UV);
static SENSOR_DEVICE_ATTR_2_RO(in1_max_alarm, ltc4222_bool, LTC4222_FAULT1,
			       FAULT_OV);
static SENSOR_DEVICE_ATTR_2_RO(in2_alarm, ltc4222_bool, LTC4222_FAULT1,
			       FAULT_POWER_BAD | FAULT_FET_BAD);

static SENSOR_DEVICE_ATTR_2_RO(in3_min_alarm, ltc4222_bool, LTC4222_FAULT2,
			       FAULT_UV);
static SENSOR_DEVICE_ATTR_2_RO(in3_max_alarm, ltc4222_bool, LTC4222_FAULT2,
			       FAULT_OV);
static SENSOR_DEVICE_ATTR_2_RO(in4_alarm, ltc4222_bool, LTC4222_FAULT2,
			       FAULT_POWER_BAD | FAULT_FET_BAD);

/* Current (via sense resistor) */
static SENSOR_DEVICE_ATTR_RO(curr1_input, ltc4222_value, LTC4222_SENSE1);
static SENSOR_DEVICE_ATTR_RO(curr2_input, ltc4222_value, LTC4222_SENSE2);

/* Overcurrent alarm */
static SENSOR_DEVICE_ATTR_2_RO(curr1_max_alarm, ltc4222_bool, LTC4222_FAULT1,
			       FAULT_OC);
static SENSOR_DEVICE_ATTR_2_RO(curr2_max_alarm, ltc4222_bool, LTC4222_FAULT2,
			       FAULT_OC);

static struct attribute *ltc4222_attrs[] = {
	&sensor_dev_attr_in1_input.dev_attr.attr,
	&sensor_dev_attr_in1_min_alarm.dev_attr.attr,
	&sensor_dev_attr_in1_max_alarm.dev_attr.attr,
	&sensor_dev_attr_in2_input.dev_attr.attr,
	&sensor_dev_attr_in2_alarm.dev_attr.attr,
	&sensor_dev_attr_in3_input.dev_attr.attr,
	&sensor_dev_attr_in3_min_alarm.dev_attr.attr,
	&sensor_dev_attr_in3_max_alarm.dev_attr.attr,
	&sensor_dev_attr_in4_input.dev_attr.attr,
	&sensor_dev_attr_in4_alarm.dev_attr.attr,

	&sensor_dev_attr_curr1_input.dev_attr.attr,
	&sensor_dev_attr_curr1_max_alarm.dev_attr.attr,
	&sensor_dev_attr_curr2_input.dev_attr.attr,
	&sensor_dev_attr_curr2_max_alarm.dev_attr.attr,

	NULL,
};
ATTRIBUTE_GROUPS(ltc4222);

static const struct regmap_config ltc4222_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = LTC4222_ADC_CONTROL,
};

static int ltc4222_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct device *hwmon_dev;
	struct regmap *regmap;

	regmap = devm_regmap_init_i2c(client, &ltc4222_regmap_config);
	if (IS_ERR(regmap)) {
		dev_err(dev, "failed to allocate register map\n");
		return PTR_ERR(regmap);
	}

	/* Clear faults */
	regmap_write(regmap, LTC4222_FAULT1, 0x00);
	regmap_write(regmap, LTC4222_FAULT2, 0x00);

	hwmon_dev = devm_hwmon_device_register_with_groups(dev, client->name,
							   regmap,
							   ltc4222_groups);
	return PTR_ERR_OR_ZERO(hwmon_dev);
}

static const struct i2c_device_id ltc4222_id[] = {
	{"ltc4222", 0},
	{ }
};

MODULE_DEVICE_TABLE(i2c, ltc4222_id);

static struct i2c_driver ltc4222_driver = {
	.driver = {
		   .name = "ltc4222",
		   },
	.probe = ltc4222_probe,
	.id_table = ltc4222_id,
};

module_i2c_driver(ltc4222_driver);

MODULE_AUTHOR("Guenter Roeck <linux@roeck-us.net>");
MODULE_DESCRIPTION("LTC4222 driver");
MODULE_LICENSE("GPL");

/*
 * Driver for Linear Technology LTC2990 power monitor
 *
 * Copyright (C) 2014 Topic Embedded Products
 * Author: Mike Looijmans <mike.looijmans@topic.nl>
 *
 * License: GPLv2
 *
 * This driver assumes the chip is wired as a dual current monitor, and
 * reports the voltage drop across two series resistors. It also reports
 * the chip's internal temperature and Vcc power supply voltage.
 */

#include <linux/err.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/module.h>

#define LTC2990_STATUS	0x00
#define LTC2990_CONTROL	0x01
#define LTC2990_TRIGGER	0x02
#define LTC2990_TINT_MSB	0x04
#define LTC2990_V1_MSB	0x06
#define LTC2990_V2_MSB	0x08
#define LTC2990_V3_MSB	0x0A
#define LTC2990_V4_MSB	0x0C
#define LTC2990_VCC_MSB	0x0E

#define LTC2990_CONTROL_KELVIN		BIT(7)
#define LTC2990_CONTROL_SINGLE		BIT(6)
#define LTC2990_CONTROL_MEASURE_ALL	(0x3 << 3)
#define LTC2990_CONTROL_MODE_CURRENT	0x06
#define LTC2990_CONTROL_MODE_VOLTAGE	0x07

/* convert raw register value to sign-extended integer in 16-bit range */
static int ltc2990_voltage_to_int(int raw)
{
	if (raw & BIT(14))
		return -(0x4000 - (raw & 0x3FFF)) << 2;
	else
		return (raw & 0x3FFF) << 2;
}

/* Return the converted value from the given register in uV or mC */
static int ltc2990_get_value(struct i2c_client *i2c, u8 reg, int *result)
{
	int val;

	val = i2c_smbus_read_word_swapped(i2c, reg);
	if (unlikely(val < 0))
		return val;

	switch (reg) {
	case LTC2990_TINT_MSB:
		/* internal temp, 0.0625 degrees/LSB, 13-bit  */
		val = (val & 0x1FFF) << 3;
		*result = (val * 1000) >> 7;
		break;
	case LTC2990_V1_MSB:
	case LTC2990_V3_MSB:
		 /* Vx-Vy, 19.42uV/LSB. Depends on mode. */
		*result = ltc2990_voltage_to_int(val) * 1942 / (4 * 100);
		break;
	case LTC2990_VCC_MSB:
		/* Vcc, 305.18μV/LSB, 2.5V offset */
		*result = (ltc2990_voltage_to_int(val) * 30518 /
			   (4 * 100 * 1000)) + 2500;
		break;
	default:
		return -EINVAL; /* won't happen, keep compiler happy */
	}

	return 0;
}

static ssize_t ltc2990_show_value(struct device *dev,
				  struct device_attribute *da, char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(da);
	int value;
	int ret;

	ret = ltc2990_get_value(dev_get_drvdata(dev), attr->index, &value);
	if (unlikely(ret < 0))
		return ret;

	return snprintf(buf, PAGE_SIZE, "%d\n", value);
}

static SENSOR_DEVICE_ATTR(temp1_input, S_IRUGO, ltc2990_show_value, NULL,
			  LTC2990_TINT_MSB);
static SENSOR_DEVICE_ATTR(curr1_input, S_IRUGO, ltc2990_show_value, NULL,
			  LTC2990_V1_MSB);
static SENSOR_DEVICE_ATTR(curr2_input, S_IRUGO, ltc2990_show_value, NULL,
			  LTC2990_V3_MSB);
static SENSOR_DEVICE_ATTR(in0_input, S_IRUGO, ltc2990_show_value, NULL,
			  LTC2990_VCC_MSB);

static struct attribute *ltc2990_attrs[] = {
	&sensor_dev_attr_temp1_input.dev_attr.attr,
	&sensor_dev_attr_curr1_input.dev_attr.attr,
	&sensor_dev_attr_curr2_input.dev_attr.attr,
	&sensor_dev_attr_in0_input.dev_attr.attr,
	NULL,
};
ATTRIBUTE_GROUPS(ltc2990);

static int ltc2990_i2c_probe(struct i2c_client *i2c,
			     const struct i2c_device_id *id)
{
	int ret;
	struct device *hwmon_dev;

	if (!i2c_check_functionality(i2c->adapter, I2C_FUNC_SMBUS_BYTE_DATA |
				     I2C_FUNC_SMBUS_WORD_DATA))
		return -ENODEV;

	/* Setup continuous mode, current monitor */
	ret = i2c_smbus_write_byte_data(i2c, LTC2990_CONTROL,
					LTC2990_CONTROL_MEASURE_ALL |
					LTC2990_CONTROL_MODE_CURRENT);
	if (ret < 0) {
		dev_err(&i2c->dev, "Error: Failed to set control mode.\n");
		return ret;
	}
	/* Trigger once to start continuous conversion */
	ret = i2c_smbus_write_byte_data(i2c, LTC2990_TRIGGER, 1);
	if (ret < 0) {
		dev_err(&i2c->dev, "Error: Failed to start acquisition.\n");
		return ret;
	}

	hwmon_dev = devm_hwmon_device_register_with_groups(&i2c->dev,
							   i2c->name,
							   i2c,
							   ltc2990_groups);

	return PTR_ERR_OR_ZERO(hwmon_dev);
}

static const struct i2c_device_id ltc2990_i2c_id[] = {
	{ "ltc2990", 0 },
	{}
};
MODULE_DEVICE_TABLE(i2c, ltc2990_i2c_id);

static struct i2c_driver ltc2990_i2c_driver = {
	.driver = {
		.name = "ltc2990",
	},
	.probe    = ltc2990_i2c_probe,
	.id_table = ltc2990_i2c_id,
};

module_i2c_driver(ltc2990_i2c_driver);

MODULE_DESCRIPTION("LTC2990 Sensor Driver");
MODULE_AUTHOR("Topic Embedded Products");
MODULE_LICENSE("GPL v2");

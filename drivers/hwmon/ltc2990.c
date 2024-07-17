// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver for Linear Technology LTC2990 power monitor
 *
 * Copyright (C) 2014 Topic Embedded Products
 * Author: Mike Looijmans <mike.looijmans@topic.nl>
 */

#include <linux/bitops.h>
#include <linux/err.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/property.h>

#define LTC2990_STATUS	0x00
#define LTC2990_CONTROL	0x01
#define LTC2990_TRIGGER	0x02
#define LTC2990_TINT_MSB	0x04
#define LTC2990_V1_MSB	0x06
#define LTC2990_V2_MSB	0x08
#define LTC2990_V3_MSB	0x0A
#define LTC2990_V4_MSB	0x0C
#define LTC2990_VCC_MSB	0x0E

#define LTC2990_IN0	BIT(0)
#define LTC2990_IN1	BIT(1)
#define LTC2990_IN2	BIT(2)
#define LTC2990_IN3	BIT(3)
#define LTC2990_IN4	BIT(4)
#define LTC2990_CURR1	BIT(5)
#define LTC2990_CURR2	BIT(6)
#define LTC2990_TEMP1	BIT(7)
#define LTC2990_TEMP2	BIT(8)
#define LTC2990_TEMP3	BIT(9)
#define LTC2990_NONE	0
#define LTC2990_ALL	GENMASK(9, 0)

#define LTC2990_MODE0_SHIFT	0
#define LTC2990_MODE0_MASK	GENMASK(2, 0)
#define LTC2990_MODE1_SHIFT	3
#define LTC2990_MODE1_MASK	GENMASK(1, 0)

/* Enabled measurements for mode bits 2..0 */
static const int ltc2990_attrs_ena_0[] = {
	LTC2990_IN1 | LTC2990_IN2 | LTC2990_TEMP3,
	LTC2990_CURR1 | LTC2990_TEMP3,
	LTC2990_CURR1 | LTC2990_IN3 | LTC2990_IN4,
	LTC2990_TEMP2 | LTC2990_IN3 | LTC2990_IN4,
	LTC2990_TEMP2 | LTC2990_CURR2,
	LTC2990_TEMP2 | LTC2990_TEMP3,
	LTC2990_CURR1 | LTC2990_CURR2,
	LTC2990_IN1 | LTC2990_IN2 | LTC2990_IN3 | LTC2990_IN4
};

/* Enabled measurements for mode bits 4..3 */
static const int ltc2990_attrs_ena_1[] = {
	LTC2990_NONE,
	LTC2990_TEMP2 | LTC2990_IN1 | LTC2990_CURR1,
	LTC2990_TEMP3 | LTC2990_IN3 | LTC2990_CURR2,
	LTC2990_ALL
};

struct ltc2990_data {
	struct i2c_client *i2c;
	u32 mode[2];
};

/* Return the converted value from the given register in uV or mC */
static int ltc2990_get_value(struct i2c_client *i2c, int index, int *result)
{
	int val;
	u8 reg;

	switch (index) {
	case LTC2990_IN0:
		reg = LTC2990_VCC_MSB;
		break;
	case LTC2990_IN1:
	case LTC2990_CURR1:
	case LTC2990_TEMP2:
		reg = LTC2990_V1_MSB;
		break;
	case LTC2990_IN2:
		reg = LTC2990_V2_MSB;
		break;
	case LTC2990_IN3:
	case LTC2990_CURR2:
	case LTC2990_TEMP3:
		reg = LTC2990_V3_MSB;
		break;
	case LTC2990_IN4:
		reg = LTC2990_V4_MSB;
		break;
	case LTC2990_TEMP1:
		reg = LTC2990_TINT_MSB;
		break;
	default:
		return -EINVAL;
	}

	val = i2c_smbus_read_word_swapped(i2c, reg);
	if (unlikely(val < 0))
		return val;

	switch (index) {
	case LTC2990_TEMP1:
	case LTC2990_TEMP2:
	case LTC2990_TEMP3:
		/* temp, 0.0625 degrees/LSB */
		*result = sign_extend32(val, 12) * 1000 / 16;
		break;
	case LTC2990_CURR1:
	case LTC2990_CURR2:
		 /* Vx-Vy, 19.42uV/LSB */
		*result = sign_extend32(val, 14) * 1942 / 100;
		break;
	case LTC2990_IN0:
		/* Vcc, 305.18uV/LSB, 2.5V offset */
		*result = sign_extend32(val, 14) * 30518 / (100 * 1000) + 2500;
		break;
	case LTC2990_IN1:
	case LTC2990_IN2:
	case LTC2990_IN3:
	case LTC2990_IN4:
		/* Vx, 305.18uV/LSB */
		*result = sign_extend32(val, 14) * 30518 / (100 * 1000);
		break;
	default:
		return -EINVAL; /* won't happen, keep compiler happy */
	}

	return 0;
}

static ssize_t ltc2990_value_show(struct device *dev,
				  struct device_attribute *da, char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(da);
	struct ltc2990_data *data = dev_get_drvdata(dev);
	int value;
	int ret;

	ret = ltc2990_get_value(data->i2c, attr->index, &value);
	if (unlikely(ret < 0))
		return ret;

	return sysfs_emit(buf, "%d\n", value);
}

static umode_t ltc2990_attrs_visible(struct kobject *kobj,
				     struct attribute *a, int n)
{
	struct device *dev = kobj_to_dev(kobj);
	struct ltc2990_data *data = dev_get_drvdata(dev);
	struct device_attribute *da =
			container_of(a, struct device_attribute, attr);
	struct sensor_device_attribute *attr = to_sensor_dev_attr(da);

	int attrs_mask = LTC2990_IN0 | LTC2990_TEMP1 |
			 (ltc2990_attrs_ena_0[data->mode[0]] &
			  ltc2990_attrs_ena_1[data->mode[1]]);

	if (attr->index & attrs_mask)
		return a->mode;

	return 0;
}

static SENSOR_DEVICE_ATTR_RO(temp1_input, ltc2990_value, LTC2990_TEMP1);
static SENSOR_DEVICE_ATTR_RO(temp2_input, ltc2990_value, LTC2990_TEMP2);
static SENSOR_DEVICE_ATTR_RO(temp3_input, ltc2990_value, LTC2990_TEMP3);
static SENSOR_DEVICE_ATTR_RO(curr1_input, ltc2990_value, LTC2990_CURR1);
static SENSOR_DEVICE_ATTR_RO(curr2_input, ltc2990_value, LTC2990_CURR2);
static SENSOR_DEVICE_ATTR_RO(in0_input, ltc2990_value, LTC2990_IN0);
static SENSOR_DEVICE_ATTR_RO(in1_input, ltc2990_value, LTC2990_IN1);
static SENSOR_DEVICE_ATTR_RO(in2_input, ltc2990_value, LTC2990_IN2);
static SENSOR_DEVICE_ATTR_RO(in3_input, ltc2990_value, LTC2990_IN3);
static SENSOR_DEVICE_ATTR_RO(in4_input, ltc2990_value, LTC2990_IN4);

static struct attribute *ltc2990_attrs[] = {
	&sensor_dev_attr_temp1_input.dev_attr.attr,
	&sensor_dev_attr_temp2_input.dev_attr.attr,
	&sensor_dev_attr_temp3_input.dev_attr.attr,
	&sensor_dev_attr_curr1_input.dev_attr.attr,
	&sensor_dev_attr_curr2_input.dev_attr.attr,
	&sensor_dev_attr_in0_input.dev_attr.attr,
	&sensor_dev_attr_in1_input.dev_attr.attr,
	&sensor_dev_attr_in2_input.dev_attr.attr,
	&sensor_dev_attr_in3_input.dev_attr.attr,
	&sensor_dev_attr_in4_input.dev_attr.attr,
	NULL,
};

static const struct attribute_group ltc2990_group = {
	.attrs = ltc2990_attrs,
	.is_visible = ltc2990_attrs_visible,
};
__ATTRIBUTE_GROUPS(ltc2990);

static int ltc2990_i2c_probe(struct i2c_client *i2c)
{
	int ret;
	struct device *hwmon_dev;
	struct ltc2990_data *data;

	if (!i2c_check_functionality(i2c->adapter, I2C_FUNC_SMBUS_BYTE_DATA |
				     I2C_FUNC_SMBUS_WORD_DATA))
		return -ENODEV;

	data = devm_kzalloc(&i2c->dev, sizeof(struct ltc2990_data), GFP_KERNEL);
	if (unlikely(!data))
		return -ENOMEM;

	data->i2c = i2c;

	if (dev_fwnode(&i2c->dev)) {
		ret = device_property_read_u32_array(&i2c->dev,
						     "lltc,meas-mode",
						     data->mode, 2);
		if (ret < 0)
			return ret;

		if (data->mode[0] & ~LTC2990_MODE0_MASK ||
		    data->mode[1] & ~LTC2990_MODE1_MASK)
			return -EINVAL;
	} else {
		ret = i2c_smbus_read_byte_data(i2c, LTC2990_CONTROL);
		if (ret < 0)
			return ret;

		data->mode[0] = ret >> LTC2990_MODE0_SHIFT & LTC2990_MODE0_MASK;
		data->mode[1] = ret >> LTC2990_MODE1_SHIFT & LTC2990_MODE1_MASK;
	}

	/* Setup continuous mode */
	ret = i2c_smbus_write_byte_data(i2c, LTC2990_CONTROL,
					data->mode[0] << LTC2990_MODE0_SHIFT |
					data->mode[1] << LTC2990_MODE1_SHIFT);
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
							   data,
							   ltc2990_groups);

	return PTR_ERR_OR_ZERO(hwmon_dev);
}

static const struct i2c_device_id ltc2990_i2c_id[] = {
	{ "ltc2990" },
	{}
};
MODULE_DEVICE_TABLE(i2c, ltc2990_i2c_id);

static struct i2c_driver ltc2990_i2c_driver = {
	.driver = {
		.name = "ltc2990",
	},
	.probe = ltc2990_i2c_probe,
	.id_table = ltc2990_i2c_id,
};

module_i2c_driver(ltc2990_i2c_driver);

MODULE_DESCRIPTION("LTC2990 Sensor Driver");
MODULE_AUTHOR("Topic Embedded Products");
MODULE_LICENSE("GPL v2");

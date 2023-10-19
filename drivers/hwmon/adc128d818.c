// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Driver for TI ADC128D818 System Monitor with Temperature Sensor
 *
 * Copyright (c) 2014 Guenter Roeck
 *
 * Derived from lm80.c
 * Copyright (C) 1998, 1999  Frodo Looijaard <frodol@dds.nl>
 *			     and Philip Edelbrock <phil@netroedge.com>
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/jiffies.h>
#include <linux/i2c.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/err.h>
#include <linux/regulator/consumer.h>
#include <linux/mutex.h>
#include <linux/bitops.h>
#include <linux/of.h>

/* Addresses to scan
 * The chip also supports addresses 0x35..0x37. Don't scan those addresses
 * since they are also used by some EEPROMs, which may result in false
 * positives.
 */
static const unsigned short normal_i2c[] = {
	0x1d, 0x1e, 0x1f, 0x2d, 0x2e, 0x2f, I2C_CLIENT_END };

/* registers */
#define ADC128_REG_IN_MAX(nr)		(0x2a + (nr) * 2)
#define ADC128_REG_IN_MIN(nr)		(0x2b + (nr) * 2)
#define ADC128_REG_IN(nr)		(0x20 + (nr))

#define ADC128_REG_TEMP			0x27
#define ADC128_REG_TEMP_MAX		0x38
#define ADC128_REG_TEMP_HYST		0x39

#define ADC128_REG_CONFIG		0x00
#define ADC128_REG_ALARM		0x01
#define ADC128_REG_MASK			0x03
#define ADC128_REG_CONV_RATE		0x07
#define ADC128_REG_ONESHOT		0x09
#define ADC128_REG_SHUTDOWN		0x0a
#define ADC128_REG_CONFIG_ADV		0x0b
#define ADC128_REG_BUSY_STATUS		0x0c

#define ADC128_REG_MAN_ID		0x3e
#define ADC128_REG_DEV_ID		0x3f

/* No. of voltage entries in adc128_attrs */
#define ADC128_ATTR_NUM_VOLT		(8 * 4)

/* Voltage inputs visible per operation mode */
static const u8 num_inputs[] = { 7, 8, 4, 6 };

struct adc128_data {
	struct i2c_client *client;
	struct regulator *regulator;
	int vref;		/* Reference voltage in mV */
	struct mutex update_lock;
	u8 mode;		/* Operation mode */
	bool valid;		/* true if following fields are valid */
	unsigned long last_updated;	/* In jiffies */

	u16 in[3][8];		/* Register value, normalized to 12 bit
				 * 0: input voltage
				 * 1: min limit
				 * 2: max limit
				 */
	s16 temp[3];		/* Register value, normalized to 9 bit
				 * 0: sensor 1: limit 2: hyst
				 */
	u8 alarms;		/* alarm register value */
};

static struct adc128_data *adc128_update_device(struct device *dev)
{
	struct adc128_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	struct adc128_data *ret = data;
	int i, rv;

	mutex_lock(&data->update_lock);

	if (time_after(jiffies, data->last_updated + HZ) || !data->valid) {
		for (i = 0; i < num_inputs[data->mode]; i++) {
			rv = i2c_smbus_read_word_swapped(client,
							 ADC128_REG_IN(i));
			if (rv < 0)
				goto abort;
			data->in[0][i] = rv >> 4;

			rv = i2c_smbus_read_byte_data(client,
						      ADC128_REG_IN_MIN(i));
			if (rv < 0)
				goto abort;
			data->in[1][i] = rv << 4;

			rv = i2c_smbus_read_byte_data(client,
						      ADC128_REG_IN_MAX(i));
			if (rv < 0)
				goto abort;
			data->in[2][i] = rv << 4;
		}

		if (data->mode != 1) {
			rv = i2c_smbus_read_word_swapped(client,
							 ADC128_REG_TEMP);
			if (rv < 0)
				goto abort;
			data->temp[0] = rv >> 7;

			rv = i2c_smbus_read_byte_data(client,
						      ADC128_REG_TEMP_MAX);
			if (rv < 0)
				goto abort;
			data->temp[1] = rv << 1;

			rv = i2c_smbus_read_byte_data(client,
						      ADC128_REG_TEMP_HYST);
			if (rv < 0)
				goto abort;
			data->temp[2] = rv << 1;
		}

		rv = i2c_smbus_read_byte_data(client, ADC128_REG_ALARM);
		if (rv < 0)
			goto abort;
		data->alarms |= rv;

		data->last_updated = jiffies;
		data->valid = true;
	}
	goto done;

abort:
	ret = ERR_PTR(rv);
	data->valid = false;
done:
	mutex_unlock(&data->update_lock);
	return ret;
}

static ssize_t adc128_in_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	struct adc128_data *data = adc128_update_device(dev);
	int index = to_sensor_dev_attr_2(attr)->index;
	int nr = to_sensor_dev_attr_2(attr)->nr;
	int val;

	if (IS_ERR(data))
		return PTR_ERR(data);

	val = DIV_ROUND_CLOSEST(data->in[index][nr] * data->vref, 4095);
	return sprintf(buf, "%d\n", val);
}

static ssize_t adc128_in_store(struct device *dev,
			       struct device_attribute *attr, const char *buf,
			       size_t count)
{
	struct adc128_data *data = dev_get_drvdata(dev);
	int index = to_sensor_dev_attr_2(attr)->index;
	int nr = to_sensor_dev_attr_2(attr)->nr;
	u8 reg, regval;
	long val;
	int err;

	err = kstrtol(buf, 10, &val);
	if (err < 0)
		return err;

	mutex_lock(&data->update_lock);
	/* 10 mV LSB on limit registers */
	regval = clamp_val(DIV_ROUND_CLOSEST(val, 10), 0, 255);
	data->in[index][nr] = regval << 4;
	reg = index == 1 ? ADC128_REG_IN_MIN(nr) : ADC128_REG_IN_MAX(nr);
	i2c_smbus_write_byte_data(data->client, reg, regval);
	mutex_unlock(&data->update_lock);

	return count;
}

static ssize_t adc128_temp_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct adc128_data *data = adc128_update_device(dev);
	int index = to_sensor_dev_attr(attr)->index;
	int temp;

	if (IS_ERR(data))
		return PTR_ERR(data);

	temp = sign_extend32(data->temp[index], 8);
	return sprintf(buf, "%d\n", temp * 500);/* 0.5 degrees C resolution */
}

static ssize_t adc128_temp_store(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t count)
{
	struct adc128_data *data = dev_get_drvdata(dev);
	int index = to_sensor_dev_attr(attr)->index;
	long val;
	int err;
	s8 regval;

	err = kstrtol(buf, 10, &val);
	if (err < 0)
		return err;

	mutex_lock(&data->update_lock);
	regval = clamp_val(DIV_ROUND_CLOSEST(val, 1000), -128, 127);
	data->temp[index] = regval << 1;
	i2c_smbus_write_byte_data(data->client,
				  index == 1 ? ADC128_REG_TEMP_MAX
					     : ADC128_REG_TEMP_HYST,
				  regval);
	mutex_unlock(&data->update_lock);

	return count;
}

static ssize_t adc128_alarm_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct adc128_data *data = adc128_update_device(dev);
	int mask = 1 << to_sensor_dev_attr(attr)->index;
	u8 alarms;

	if (IS_ERR(data))
		return PTR_ERR(data);

	/*
	 * Clear an alarm after reporting it to user space. If it is still
	 * active, the next update sequence will set the alarm bit again.
	 */
	alarms = data->alarms;
	data->alarms &= ~mask;

	return sprintf(buf, "%u\n", !!(alarms & mask));
}

static umode_t adc128_is_visible(struct kobject *kobj,
				 struct attribute *attr, int index)
{
	struct device *dev = kobj_to_dev(kobj);
	struct adc128_data *data = dev_get_drvdata(dev);

	if (index < ADC128_ATTR_NUM_VOLT) {
		/* Voltage, visible according to num_inputs[] */
		if (index >= num_inputs[data->mode] * 4)
			return 0;
	} else {
		/* Temperature, visible if not in mode 1 */
		if (data->mode == 1)
			return 0;
	}

	return attr->mode;
}

static SENSOR_DEVICE_ATTR_2_RO(in0_input, adc128_in, 0, 0);
static SENSOR_DEVICE_ATTR_2_RW(in0_min, adc128_in, 0, 1);
static SENSOR_DEVICE_ATTR_2_RW(in0_max, adc128_in, 0, 2);

static SENSOR_DEVICE_ATTR_2_RO(in1_input, adc128_in, 1, 0);
static SENSOR_DEVICE_ATTR_2_RW(in1_min, adc128_in, 1, 1);
static SENSOR_DEVICE_ATTR_2_RW(in1_max, adc128_in, 1, 2);

static SENSOR_DEVICE_ATTR_2_RO(in2_input, adc128_in, 2, 0);
static SENSOR_DEVICE_ATTR_2_RW(in2_min, adc128_in, 2, 1);
static SENSOR_DEVICE_ATTR_2_RW(in2_max, adc128_in, 2, 2);

static SENSOR_DEVICE_ATTR_2_RO(in3_input, adc128_in, 3, 0);
static SENSOR_DEVICE_ATTR_2_RW(in3_min, adc128_in, 3, 1);
static SENSOR_DEVICE_ATTR_2_RW(in3_max, adc128_in, 3, 2);

static SENSOR_DEVICE_ATTR_2_RO(in4_input, adc128_in, 4, 0);
static SENSOR_DEVICE_ATTR_2_RW(in4_min, adc128_in, 4, 1);
static SENSOR_DEVICE_ATTR_2_RW(in4_max, adc128_in, 4, 2);

static SENSOR_DEVICE_ATTR_2_RO(in5_input, adc128_in, 5, 0);
static SENSOR_DEVICE_ATTR_2_RW(in5_min, adc128_in, 5, 1);
static SENSOR_DEVICE_ATTR_2_RW(in5_max, adc128_in, 5, 2);

static SENSOR_DEVICE_ATTR_2_RO(in6_input, adc128_in, 6, 0);
static SENSOR_DEVICE_ATTR_2_RW(in6_min, adc128_in, 6, 1);
static SENSOR_DEVICE_ATTR_2_RW(in6_max, adc128_in, 6, 2);

static SENSOR_DEVICE_ATTR_2_RO(in7_input, adc128_in, 7, 0);
static SENSOR_DEVICE_ATTR_2_RW(in7_min, adc128_in, 7, 1);
static SENSOR_DEVICE_ATTR_2_RW(in7_max, adc128_in, 7, 2);

static SENSOR_DEVICE_ATTR_RO(temp1_input, adc128_temp, 0);
static SENSOR_DEVICE_ATTR_RW(temp1_max, adc128_temp, 1);
static SENSOR_DEVICE_ATTR_RW(temp1_max_hyst, adc128_temp, 2);

static SENSOR_DEVICE_ATTR_RO(in0_alarm, adc128_alarm, 0);
static SENSOR_DEVICE_ATTR_RO(in1_alarm, adc128_alarm, 1);
static SENSOR_DEVICE_ATTR_RO(in2_alarm, adc128_alarm, 2);
static SENSOR_DEVICE_ATTR_RO(in3_alarm, adc128_alarm, 3);
static SENSOR_DEVICE_ATTR_RO(in4_alarm, adc128_alarm, 4);
static SENSOR_DEVICE_ATTR_RO(in5_alarm, adc128_alarm, 5);
static SENSOR_DEVICE_ATTR_RO(in6_alarm, adc128_alarm, 6);
static SENSOR_DEVICE_ATTR_RO(in7_alarm, adc128_alarm, 7);
static SENSOR_DEVICE_ATTR_RO(temp1_max_alarm, adc128_alarm, 7);

static struct attribute *adc128_attrs[] = {
	&sensor_dev_attr_in0_alarm.dev_attr.attr,
	&sensor_dev_attr_in0_input.dev_attr.attr,
	&sensor_dev_attr_in0_max.dev_attr.attr,
	&sensor_dev_attr_in0_min.dev_attr.attr,
	&sensor_dev_attr_in1_alarm.dev_attr.attr,
	&sensor_dev_attr_in1_input.dev_attr.attr,
	&sensor_dev_attr_in1_max.dev_attr.attr,
	&sensor_dev_attr_in1_min.dev_attr.attr,
	&sensor_dev_attr_in2_alarm.dev_attr.attr,
	&sensor_dev_attr_in2_input.dev_attr.attr,
	&sensor_dev_attr_in2_max.dev_attr.attr,
	&sensor_dev_attr_in2_min.dev_attr.attr,
	&sensor_dev_attr_in3_alarm.dev_attr.attr,
	&sensor_dev_attr_in3_input.dev_attr.attr,
	&sensor_dev_attr_in3_max.dev_attr.attr,
	&sensor_dev_attr_in3_min.dev_attr.attr,
	&sensor_dev_attr_in4_alarm.dev_attr.attr,
	&sensor_dev_attr_in4_input.dev_attr.attr,
	&sensor_dev_attr_in4_max.dev_attr.attr,
	&sensor_dev_attr_in4_min.dev_attr.attr,
	&sensor_dev_attr_in5_alarm.dev_attr.attr,
	&sensor_dev_attr_in5_input.dev_attr.attr,
	&sensor_dev_attr_in5_max.dev_attr.attr,
	&sensor_dev_attr_in5_min.dev_attr.attr,
	&sensor_dev_attr_in6_alarm.dev_attr.attr,
	&sensor_dev_attr_in6_input.dev_attr.attr,
	&sensor_dev_attr_in6_max.dev_attr.attr,
	&sensor_dev_attr_in6_min.dev_attr.attr,
	&sensor_dev_attr_in7_alarm.dev_attr.attr,
	&sensor_dev_attr_in7_input.dev_attr.attr,
	&sensor_dev_attr_in7_max.dev_attr.attr,
	&sensor_dev_attr_in7_min.dev_attr.attr,
	&sensor_dev_attr_temp1_input.dev_attr.attr,
	&sensor_dev_attr_temp1_max.dev_attr.attr,
	&sensor_dev_attr_temp1_max_alarm.dev_attr.attr,
	&sensor_dev_attr_temp1_max_hyst.dev_attr.attr,
	NULL
};

static const struct attribute_group adc128_group = {
	.attrs = adc128_attrs,
	.is_visible = adc128_is_visible,
};
__ATTRIBUTE_GROUPS(adc128);

static int adc128_detect(struct i2c_client *client, struct i2c_board_info *info)
{
	int man_id, dev_id;

	if (!i2c_check_functionality(client->adapter,
				     I2C_FUNC_SMBUS_BYTE_DATA |
				     I2C_FUNC_SMBUS_WORD_DATA))
		return -ENODEV;

	man_id = i2c_smbus_read_byte_data(client, ADC128_REG_MAN_ID);
	dev_id = i2c_smbus_read_byte_data(client, ADC128_REG_DEV_ID);
	if (man_id != 0x01 || dev_id != 0x09)
		return -ENODEV;

	/* Check unused bits for confirmation */
	if (i2c_smbus_read_byte_data(client, ADC128_REG_CONFIG) & 0xf4)
		return -ENODEV;
	if (i2c_smbus_read_byte_data(client, ADC128_REG_CONV_RATE) & 0xfe)
		return -ENODEV;
	if (i2c_smbus_read_byte_data(client, ADC128_REG_ONESHOT) & 0xfe)
		return -ENODEV;
	if (i2c_smbus_read_byte_data(client, ADC128_REG_SHUTDOWN) & 0xfe)
		return -ENODEV;
	if (i2c_smbus_read_byte_data(client, ADC128_REG_CONFIG_ADV) & 0xf8)
		return -ENODEV;
	if (i2c_smbus_read_byte_data(client, ADC128_REG_BUSY_STATUS) & 0xfc)
		return -ENODEV;

	strscpy(info->type, "adc128d818", I2C_NAME_SIZE);

	return 0;
}

static int adc128_init_client(struct adc128_data *data)
{
	struct i2c_client *client = data->client;
	int err;
	u8 regval = 0x0;

	/*
	 * Reset chip to defaults.
	 * This makes most other initializations unnecessary.
	 */
	err = i2c_smbus_write_byte_data(client, ADC128_REG_CONFIG, 0x80);
	if (err)
		return err;

	/* Set operation mode, if non-default */
	if (data->mode != 0)
		regval |= data->mode << 1;

	/* If external vref is selected, configure the chip to use it */
	if (data->regulator)
		regval |= 0x01;

	/* Write advanced configuration register */
	if (regval != 0x0) {
		err = i2c_smbus_write_byte_data(client, ADC128_REG_CONFIG_ADV,
						regval);
		if (err)
			return err;
	}

	/* Start monitoring */
	err = i2c_smbus_write_byte_data(client, ADC128_REG_CONFIG, 0x01);
	if (err)
		return err;

	return 0;
}

static int adc128_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct regulator *regulator;
	struct device *hwmon_dev;
	struct adc128_data *data;
	int err, vref;

	data = devm_kzalloc(dev, sizeof(struct adc128_data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	/* vref is optional. If specified, is used as chip reference voltage */
	regulator = devm_regulator_get_optional(dev, "vref");
	if (!IS_ERR(regulator)) {
		data->regulator = regulator;
		err = regulator_enable(regulator);
		if (err < 0)
			return err;
		vref = regulator_get_voltage(regulator);
		if (vref < 0) {
			err = vref;
			goto error;
		}
		data->vref = DIV_ROUND_CLOSEST(vref, 1000);
	} else {
		data->vref = 2560;	/* 2.56V, in mV */
	}

	/* Operation mode is optional. If unspecified, keep current mode */
	if (of_property_read_u8(dev->of_node, "ti,mode", &data->mode) == 0) {
		if (data->mode > 3) {
			dev_err(dev, "invalid operation mode %d\n",
				data->mode);
			err = -EINVAL;
			goto error;
		}
	} else {
		err = i2c_smbus_read_byte_data(client, ADC128_REG_CONFIG_ADV);
		if (err < 0)
			goto error;
		data->mode = (err >> 1) & ADC128_REG_MASK;
	}

	data->client = client;
	i2c_set_clientdata(client, data);
	mutex_init(&data->update_lock);

	/* Initialize the chip */
	err = adc128_init_client(data);
	if (err < 0)
		goto error;

	hwmon_dev = devm_hwmon_device_register_with_groups(dev, client->name,
							   data, adc128_groups);
	if (IS_ERR(hwmon_dev)) {
		err = PTR_ERR(hwmon_dev);
		goto error;
	}

	return 0;

error:
	if (data->regulator)
		regulator_disable(data->regulator);
	return err;
}

static void adc128_remove(struct i2c_client *client)
{
	struct adc128_data *data = i2c_get_clientdata(client);

	if (data->regulator)
		regulator_disable(data->regulator);
}

static const struct i2c_device_id adc128_id[] = {
	{ "adc128d818", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, adc128_id);

static const struct of_device_id __maybe_unused adc128_of_match[] = {
	{ .compatible = "ti,adc128d818" },
	{ },
};
MODULE_DEVICE_TABLE(of, adc128_of_match);

static struct i2c_driver adc128_driver = {
	.class		= I2C_CLASS_HWMON,
	.driver = {
		.name	= "adc128d818",
		.of_match_table = of_match_ptr(adc128_of_match),
	},
	.probe_new	= adc128_probe,
	.remove		= adc128_remove,
	.id_table	= adc128_id,
	.detect		= adc128_detect,
	.address_list	= normal_i2c,
};

module_i2c_driver(adc128_driver);

MODULE_AUTHOR("Guenter Roeck");
MODULE_DESCRIPTION("Driver for ADC128D818");
MODULE_LICENSE("GPL");

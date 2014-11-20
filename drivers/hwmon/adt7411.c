/*
 *  Driver for the ADT7411 (I2C/SPI 8 channel 10 bit ADC & temperature-sensor)
 *
 *  Copyright (C) 2008, 2010 Pengutronix
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 *
 *  TODO: SPI, support for external temperature sensor
 *	  use power-down mode for suspend?, interrupt handling?
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/mutex.h>
#include <linux/jiffies.h>
#include <linux/i2c.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/slab.h>

#define ADT7411_REG_INT_TEMP_VDD_LSB		0x03
#define ADT7411_REG_EXT_TEMP_AIN14_LSB		0x04
#define ADT7411_REG_VDD_MSB			0x06
#define ADT7411_REG_INT_TEMP_MSB		0x07
#define ADT7411_REG_EXT_TEMP_AIN1_MSB		0x08

#define ADT7411_REG_CFG1			0x18
#define ADT7411_CFG1_START_MONITOR		(1 << 0)

#define ADT7411_REG_CFG2			0x19
#define ADT7411_CFG2_DISABLE_AVG		(1 << 5)

#define ADT7411_REG_CFG3			0x1a
#define ADT7411_CFG3_ADC_CLK_225		(1 << 0)
#define ADT7411_CFG3_REF_VDD			(1 << 4)

#define ADT7411_REG_DEVICE_ID			0x4d
#define ADT7411_REG_MANUFACTURER_ID		0x4e

#define ADT7411_DEVICE_ID			0x2
#define ADT7411_MANUFACTURER_ID			0x41

static const unsigned short normal_i2c[] = { 0x48, 0x4a, 0x4b, I2C_CLIENT_END };

struct adt7411_data {
	struct mutex device_lock;	/* for "atomic" device accesses */
	struct mutex update_lock;
	unsigned long next_update;
	int vref_cached;
	struct i2c_client *client;
};

/*
 * When reading a register containing (up to 4) lsb, all associated
 * msb-registers get locked by the hardware. After _one_ of those msb is read,
 * _all_ are unlocked. In order to use this locking correctly, reading lsb/msb
 * is protected here with a mutex, too.
 */
static int adt7411_read_10_bit(struct i2c_client *client, u8 lsb_reg,
				u8 msb_reg, u8 lsb_shift)
{
	struct adt7411_data *data = i2c_get_clientdata(client);
	int val, tmp;

	mutex_lock(&data->device_lock);

	val = i2c_smbus_read_byte_data(client, lsb_reg);
	if (val < 0)
		goto exit_unlock;

	tmp = (val >> lsb_shift) & 3;
	val = i2c_smbus_read_byte_data(client, msb_reg);

	if (val >= 0)
		val = (val << 2) | tmp;

 exit_unlock:
	mutex_unlock(&data->device_lock);

	return val;
}

static int adt7411_modify_bit(struct i2c_client *client, u8 reg, u8 bit,
				bool flag)
{
	struct adt7411_data *data = i2c_get_clientdata(client);
	int ret, val;

	mutex_lock(&data->device_lock);

	ret = i2c_smbus_read_byte_data(client, reg);
	if (ret < 0)
		goto exit_unlock;

	if (flag)
		val = ret | bit;
	else
		val = ret & ~bit;

	ret = i2c_smbus_write_byte_data(client, reg, val);

 exit_unlock:
	mutex_unlock(&data->device_lock);
	return ret;
}

static ssize_t adt7411_show_vdd(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct adt7411_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	int ret = adt7411_read_10_bit(client, ADT7411_REG_INT_TEMP_VDD_LSB,
			ADT7411_REG_VDD_MSB, 2);

	return ret < 0 ? ret : sprintf(buf, "%u\n", ret * 7000 / 1024);
}

static ssize_t adt7411_show_temp(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	struct adt7411_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	int val = adt7411_read_10_bit(client, ADT7411_REG_INT_TEMP_VDD_LSB,
			ADT7411_REG_INT_TEMP_MSB, 0);

	if (val < 0)
		return val;

	val = val & 0x200 ? val - 0x400 : val; /* 10 bit signed */

	return sprintf(buf, "%d\n", val * 250);
}

static ssize_t adt7411_show_input(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	int nr = to_sensor_dev_attr(attr)->index;
	struct adt7411_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	int val;
	u8 lsb_reg, lsb_shift;

	mutex_lock(&data->update_lock);
	if (time_after_eq(jiffies, data->next_update)) {
		val = i2c_smbus_read_byte_data(client, ADT7411_REG_CFG3);
		if (val < 0)
			goto exit_unlock;

		if (val & ADT7411_CFG3_REF_VDD) {
			val = adt7411_read_10_bit(client,
					ADT7411_REG_INT_TEMP_VDD_LSB,
					ADT7411_REG_VDD_MSB, 2);
			if (val < 0)
				goto exit_unlock;

			data->vref_cached = val * 7000 / 1024;
		} else {
			data->vref_cached = 2250;
		}

		data->next_update = jiffies + HZ;
	}

	lsb_reg = ADT7411_REG_EXT_TEMP_AIN14_LSB + (nr >> 2);
	lsb_shift = 2 * (nr & 0x03);
	val = adt7411_read_10_bit(client, lsb_reg,
			ADT7411_REG_EXT_TEMP_AIN1_MSB + nr, lsb_shift);
	if (val < 0)
		goto exit_unlock;

	val = sprintf(buf, "%u\n", val * data->vref_cached / 1024);
 exit_unlock:
	mutex_unlock(&data->update_lock);
	return val;
}

static ssize_t adt7411_show_bit(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct sensor_device_attribute_2 *attr2 = to_sensor_dev_attr_2(attr);
	struct adt7411_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	int ret = i2c_smbus_read_byte_data(client, attr2->index);

	return ret < 0 ? ret : sprintf(buf, "%u\n", !!(ret & attr2->nr));
}

static ssize_t adt7411_set_bit(struct device *dev,
			       struct device_attribute *attr, const char *buf,
			       size_t count)
{
	struct sensor_device_attribute_2 *s_attr2 = to_sensor_dev_attr_2(attr);
	struct adt7411_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	int ret;
	unsigned long flag;

	ret = kstrtoul(buf, 0, &flag);
	if (ret || flag > 1)
		return -EINVAL;

	ret = adt7411_modify_bit(client, s_attr2->index, s_attr2->nr, flag);

	/* force update */
	mutex_lock(&data->update_lock);
	data->next_update = jiffies;
	mutex_unlock(&data->update_lock);

	return ret < 0 ? ret : count;
}

#define ADT7411_BIT_ATTR(__name, __reg, __bit) \
	SENSOR_DEVICE_ATTR_2(__name, S_IRUGO | S_IWUSR, adt7411_show_bit, \
	adt7411_set_bit, __bit, __reg)

static DEVICE_ATTR(temp1_input, S_IRUGO, adt7411_show_temp, NULL);
static DEVICE_ATTR(in0_input, S_IRUGO, adt7411_show_vdd, NULL);
static SENSOR_DEVICE_ATTR(in1_input, S_IRUGO, adt7411_show_input, NULL, 0);
static SENSOR_DEVICE_ATTR(in2_input, S_IRUGO, adt7411_show_input, NULL, 1);
static SENSOR_DEVICE_ATTR(in3_input, S_IRUGO, adt7411_show_input, NULL, 2);
static SENSOR_DEVICE_ATTR(in4_input, S_IRUGO, adt7411_show_input, NULL, 3);
static SENSOR_DEVICE_ATTR(in5_input, S_IRUGO, adt7411_show_input, NULL, 4);
static SENSOR_DEVICE_ATTR(in6_input, S_IRUGO, adt7411_show_input, NULL, 5);
static SENSOR_DEVICE_ATTR(in7_input, S_IRUGO, adt7411_show_input, NULL, 6);
static SENSOR_DEVICE_ATTR(in8_input, S_IRUGO, adt7411_show_input, NULL, 7);
static ADT7411_BIT_ATTR(no_average, ADT7411_REG_CFG2, ADT7411_CFG2_DISABLE_AVG);
static ADT7411_BIT_ATTR(fast_sampling, ADT7411_REG_CFG3, ADT7411_CFG3_ADC_CLK_225);
static ADT7411_BIT_ATTR(adc_ref_vdd, ADT7411_REG_CFG3, ADT7411_CFG3_REF_VDD);

static struct attribute *adt7411_attrs[] = {
	&dev_attr_temp1_input.attr,
	&dev_attr_in0_input.attr,
	&sensor_dev_attr_in1_input.dev_attr.attr,
	&sensor_dev_attr_in2_input.dev_attr.attr,
	&sensor_dev_attr_in3_input.dev_attr.attr,
	&sensor_dev_attr_in4_input.dev_attr.attr,
	&sensor_dev_attr_in5_input.dev_attr.attr,
	&sensor_dev_attr_in6_input.dev_attr.attr,
	&sensor_dev_attr_in7_input.dev_attr.attr,
	&sensor_dev_attr_in8_input.dev_attr.attr,
	&sensor_dev_attr_no_average.dev_attr.attr,
	&sensor_dev_attr_fast_sampling.dev_attr.attr,
	&sensor_dev_attr_adc_ref_vdd.dev_attr.attr,
	NULL
};

ATTRIBUTE_GROUPS(adt7411);

static int adt7411_detect(struct i2c_client *client,
			  struct i2c_board_info *info)
{
	int val;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_BYTE_DATA))
		return -ENODEV;

	val = i2c_smbus_read_byte_data(client, ADT7411_REG_MANUFACTURER_ID);
	if (val < 0 || val != ADT7411_MANUFACTURER_ID) {
		dev_dbg(&client->dev,
			"Wrong manufacturer ID. Got %d, expected %d\n",
			val, ADT7411_MANUFACTURER_ID);
		return -ENODEV;
	}

	val = i2c_smbus_read_byte_data(client, ADT7411_REG_DEVICE_ID);
	if (val < 0 || val != ADT7411_DEVICE_ID) {
		dev_dbg(&client->dev,
			"Wrong device ID. Got %d, expected %d\n",
			val, ADT7411_DEVICE_ID);
		return -ENODEV;
	}

	strlcpy(info->type, "adt7411", I2C_NAME_SIZE);

	return 0;
}

static int adt7411_probe(struct i2c_client *client,
				   const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct adt7411_data *data;
	struct device *hwmon_dev;
	int ret;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	i2c_set_clientdata(client, data);
	data->client = client;
	mutex_init(&data->device_lock);
	mutex_init(&data->update_lock);

	ret = adt7411_modify_bit(client, ADT7411_REG_CFG1,
				 ADT7411_CFG1_START_MONITOR, 1);
	if (ret < 0)
		return ret;

	/* force update on first occasion */
	data->next_update = jiffies;

	hwmon_dev = devm_hwmon_device_register_with_groups(dev, client->name,
							   data,
							   adt7411_groups);
	return PTR_ERR_OR_ZERO(hwmon_dev);
}

static const struct i2c_device_id adt7411_id[] = {
	{ "adt7411", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, adt7411_id);

static struct i2c_driver adt7411_driver = {
	.driver		= {
		.name		= "adt7411",
	},
	.probe  = adt7411_probe,
	.id_table = adt7411_id,
	.detect = adt7411_detect,
	.address_list = normal_i2c,
	.class = I2C_CLASS_HWMON,
};

module_i2c_driver(adt7411_driver);

MODULE_AUTHOR("Sascha Hauer <s.hauer@pengutronix.de> and "
	"Wolfram Sang <w.sang@pengutronix.de>");
MODULE_DESCRIPTION("ADT7411 driver");
MODULE_LICENSE("GPL v2");

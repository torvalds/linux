// SPDX-License-Identifier: GPL-2.0-only
/*
 * An hwmon driver for the Analog Devices AD7416/17/18
 * Copyright (C) 2006-07 Tower Technologies
 *
 * Author: Alessandro Zummo <a.zummo@towertech.it>
 *
 * Based on lm75.c
 * Copyright (C) 1998-99 Frodo Looijaard <frodol@dds.nl>
 */

#include <linux/module.h>
#include <linux/jiffies.h>
#include <linux/i2c.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/err.h>
#include <linux/mutex.h>
#include <linux/of_device.h>
#include <linux/delay.h>
#include <linux/slab.h>

#include "lm75.h"

#define DRV_VERSION "0.4"

enum chips { ad7416, ad7417, ad7418 };

/* AD7418 registers */
#define AD7418_REG_TEMP_IN	0x00
#define AD7418_REG_CONF		0x01
#define AD7418_REG_TEMP_HYST	0x02
#define AD7418_REG_TEMP_OS	0x03
#define AD7418_REG_ADC		0x04
#define AD7418_REG_CONF2	0x05

#define AD7418_REG_ADC_CH(x)	((x) << 5)
#define AD7418_CH_TEMP		AD7418_REG_ADC_CH(0)

static const u8 AD7418_REG_TEMP[] = { AD7418_REG_TEMP_IN,
					AD7418_REG_TEMP_HYST,
					AD7418_REG_TEMP_OS };

struct ad7418_data {
	struct i2c_client	*client;
	enum chips		type;
	struct mutex		lock;
	int			adc_max;	/* number of ADC channels */
	bool			valid;
	unsigned long		last_updated;	/* In jiffies */
	s16			temp[3];	/* Register values */
	u16			in[4];
};

static int ad7418_update_device(struct device *dev)
{
	struct ad7418_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	s32 val;

	mutex_lock(&data->lock);

	if (time_after(jiffies, data->last_updated + HZ + HZ / 2)
		|| !data->valid) {
		u8 cfg;
		int i, ch;

		/* read config register and clear channel bits */
		val = i2c_smbus_read_byte_data(client, AD7418_REG_CONF);
		if (val < 0)
			goto abort;

		cfg = val;
		cfg &= 0x1F;

		val = i2c_smbus_write_byte_data(client, AD7418_REG_CONF,
						cfg | AD7418_CH_TEMP);
		if (val < 0)
			goto abort;

		udelay(30);

		for (i = 0; i < 3; i++) {
			val = i2c_smbus_read_word_swapped(client,
							  AD7418_REG_TEMP[i]);
			if (val < 0)
				goto abort;

			data->temp[i] = val;
		}

		for (i = 0, ch = 4; i < data->adc_max; i++, ch--) {
			val = i2c_smbus_write_byte_data(client, AD7418_REG_CONF,
					cfg | AD7418_REG_ADC_CH(ch));
			if (val < 0)
				goto abort;

			udelay(15);
			val = i2c_smbus_read_word_swapped(client,
							  AD7418_REG_ADC);
			if (val < 0)
				goto abort;

			data->in[data->adc_max - 1 - i] = val;
		}

		/* restore old configuration value */
		val = i2c_smbus_write_word_swapped(client, AD7418_REG_CONF,
						   cfg);
		if (val < 0)
			goto abort;

		data->last_updated = jiffies;
		data->valid = true;
	}

	mutex_unlock(&data->lock);
	return 0;

abort:
	data->valid = false;
	mutex_unlock(&data->lock);
	return val;
}

static ssize_t temp_show(struct device *dev, struct device_attribute *devattr,
			 char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct ad7418_data *data = dev_get_drvdata(dev);
	int ret;

	ret = ad7418_update_device(dev);
	if (ret < 0)
		return ret;

	return sprintf(buf, "%d\n",
		LM75_TEMP_FROM_REG(data->temp[attr->index]));
}

static ssize_t adc_show(struct device *dev, struct device_attribute *devattr,
			char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct ad7418_data *data = dev_get_drvdata(dev);
	int ret;

	ret = ad7418_update_device(dev);
	if (ret < 0)
		return ret;

	return sprintf(buf, "%d\n",
		((data->in[attr->index] >> 6) * 2500 + 512) / 1024);
}

static ssize_t temp_store(struct device *dev,
			  struct device_attribute *devattr, const char *buf,
			  size_t count)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct ad7418_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	long temp;
	int ret = kstrtol(buf, 10, &temp);

	if (ret < 0)
		return ret;

	mutex_lock(&data->lock);
	data->temp[attr->index] = LM75_TEMP_TO_REG(temp);
	i2c_smbus_write_word_swapped(client,
				     AD7418_REG_TEMP[attr->index],
				     data->temp[attr->index]);
	mutex_unlock(&data->lock);
	return count;
}

static SENSOR_DEVICE_ATTR_RO(temp1_input, temp, 0);
static SENSOR_DEVICE_ATTR_RW(temp1_max_hyst, temp, 1);
static SENSOR_DEVICE_ATTR_RW(temp1_max, temp, 2);

static SENSOR_DEVICE_ATTR_RO(in1_input, adc, 0);
static SENSOR_DEVICE_ATTR_RO(in2_input, adc, 1);
static SENSOR_DEVICE_ATTR_RO(in3_input, adc, 2);
static SENSOR_DEVICE_ATTR_RO(in4_input, adc, 3);

static struct attribute *ad7416_attrs[] = {
	&sensor_dev_attr_temp1_max.dev_attr.attr,
	&sensor_dev_attr_temp1_max_hyst.dev_attr.attr,
	&sensor_dev_attr_temp1_input.dev_attr.attr,
	NULL
};
ATTRIBUTE_GROUPS(ad7416);

static struct attribute *ad7417_attrs[] = {
	&sensor_dev_attr_temp1_max.dev_attr.attr,
	&sensor_dev_attr_temp1_max_hyst.dev_attr.attr,
	&sensor_dev_attr_temp1_input.dev_attr.attr,
	&sensor_dev_attr_in1_input.dev_attr.attr,
	&sensor_dev_attr_in2_input.dev_attr.attr,
	&sensor_dev_attr_in3_input.dev_attr.attr,
	&sensor_dev_attr_in4_input.dev_attr.attr,
	NULL
};
ATTRIBUTE_GROUPS(ad7417);

static struct attribute *ad7418_attrs[] = {
	&sensor_dev_attr_temp1_max.dev_attr.attr,
	&sensor_dev_attr_temp1_max_hyst.dev_attr.attr,
	&sensor_dev_attr_temp1_input.dev_attr.attr,
	&sensor_dev_attr_in1_input.dev_attr.attr,
	NULL
};
ATTRIBUTE_GROUPS(ad7418);

static void ad7418_init_client(struct i2c_client *client)
{
	struct ad7418_data *data = i2c_get_clientdata(client);

	int reg = i2c_smbus_read_byte_data(client, AD7418_REG_CONF);
	if (reg < 0) {
		dev_err(&client->dev, "cannot read configuration register\n");
	} else {
		dev_info(&client->dev, "configuring for mode 1\n");
		i2c_smbus_write_byte_data(client, AD7418_REG_CONF, reg & 0xfe);

		if (data->type == ad7417 || data->type == ad7418)
			i2c_smbus_write_byte_data(client,
						AD7418_REG_CONF2, 0x00);
	}
}

static const struct i2c_device_id ad7418_id[];

static int ad7418_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct i2c_adapter *adapter = client->adapter;
	struct ad7418_data *data;
	struct device *hwmon_dev;
	const struct attribute_group **attr_groups = NULL;

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA |
					I2C_FUNC_SMBUS_WORD_DATA))
		return -EOPNOTSUPP;

	data = devm_kzalloc(dev, sizeof(struct ad7418_data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	i2c_set_clientdata(client, data);

	mutex_init(&data->lock);
	data->client = client;
	if (dev->of_node)
		data->type = (enum chips)of_device_get_match_data(dev);
	else
		data->type = i2c_match_id(ad7418_id, client)->driver_data;

	switch (data->type) {
	case ad7416:
		data->adc_max = 0;
		attr_groups = ad7416_groups;
		break;

	case ad7417:
		data->adc_max = 4;
		attr_groups = ad7417_groups;
		break;

	case ad7418:
		data->adc_max = 1;
		attr_groups = ad7418_groups;
		break;
	}

	dev_info(dev, "%s chip found\n", client->name);

	/* Initialize the AD7418 chip */
	ad7418_init_client(client);

	hwmon_dev = devm_hwmon_device_register_with_groups(dev,
							   client->name,
							   data, attr_groups);
	return PTR_ERR_OR_ZERO(hwmon_dev);
}

static const struct i2c_device_id ad7418_id[] = {
	{ "ad7416", ad7416 },
	{ "ad7417", ad7417 },
	{ "ad7418", ad7418 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ad7418_id);

static const struct of_device_id ad7418_dt_ids[] = {
	{ .compatible = "adi,ad7416", .data = (void *)ad7416, },
	{ .compatible = "adi,ad7417", .data = (void *)ad7417, },
	{ .compatible = "adi,ad7418", .data = (void *)ad7418, },
	{ }
};
MODULE_DEVICE_TABLE(of, ad7418_dt_ids);

static struct i2c_driver ad7418_driver = {
	.driver = {
		.name	= "ad7418",
		.of_match_table = ad7418_dt_ids,
	},
	.probe		= ad7418_probe,
	.id_table	= ad7418_id,
};

module_i2c_driver(ad7418_driver);

MODULE_AUTHOR("Alessandro Zummo <a.zummo@towertech.it>");
MODULE_DESCRIPTION("AD7416/17/18 driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(DRV_VERSION);

/*
 * An hwmon driver for the Analog Devices AD7416/17/18
 * Copyright (C) 2006-07 Tower Technologies
 *
 * Author: Alessandro Zummo <a.zummo@towertech.it>
 *
 * Based on lm75.c
 * Copyright (C) 1998-99 Frodo Looijaard <frodol@dds.nl>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License,
 * as published by the Free Software Foundation - version 2.
 */

#include <linux/module.h>
#include <linux/jiffies.h>
#include <linux/i2c.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/err.h>
#include <linux/mutex.h>
#include <linux/delay.h>

#include "lm75.h"

#define DRV_VERSION "0.3"

/* Addresses to scan */
static unsigned short normal_i2c[] = { 0x28, I2C_CLIENT_END };
/* Insmod parameters */
I2C_CLIENT_INSMOD_3(ad7416, ad7417, ad7418);

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
	struct i2c_client	client;
	struct class_device	*class_dev;
	struct attribute_group	attrs;
	enum chips		type;
	struct mutex		lock;
	int			adc_max;	/* number of ADC channels */
	char			valid;
	unsigned long		last_updated;	/* In jiffies */
	s16			temp[3];	/* Register values */
	u16			in[4];
};

static int ad7418_attach_adapter(struct i2c_adapter *adapter);
static int ad7418_detect(struct i2c_adapter *adapter, int address, int kind);
static int ad7418_detach_client(struct i2c_client *client);

static struct i2c_driver ad7418_driver = {
	.driver = {
		.name	= "ad7418",
	},
	.attach_adapter	= ad7418_attach_adapter,
	.detach_client	= ad7418_detach_client,
};

/* All registers are word-sized, except for the configuration registers.
 * AD7418 uses a high-byte first convention. Do NOT use those functions to
 * access the configuration registers CONF and CONF2, as they are byte-sized.
 */
static inline int ad7418_read(struct i2c_client *client, u8 reg)
{
	return swab16(i2c_smbus_read_word_data(client, reg));
}

static inline int ad7418_write(struct i2c_client *client, u8 reg, u16 value)
{
	return i2c_smbus_write_word_data(client, reg, swab16(value));
}

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

static struct ad7418_data *ad7418_update_device(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct ad7418_data *data = i2c_get_clientdata(client);

	mutex_lock(&data->lock);

	if (time_after(jiffies, data->last_updated + HZ + HZ / 2)
		|| !data->valid) {
		u8 cfg;
		int i, ch;

		/* read config register and clear channel bits */
		cfg = i2c_smbus_read_byte_data(client, AD7418_REG_CONF);
		cfg &= 0x1F;

		i2c_smbus_write_byte_data(client, AD7418_REG_CONF,
						cfg | AD7418_CH_TEMP);
		udelay(30);

		for (i = 0; i < 3; i++) {
			data->temp[i] = ad7418_read(client, AD7418_REG_TEMP[i]);
		}

		for (i = 0, ch = 4; i < data->adc_max; i++, ch--) {
			i2c_smbus_write_byte_data(client,
					AD7418_REG_CONF,
					cfg | AD7418_REG_ADC_CH(ch));

			udelay(15);
			data->in[data->adc_max - 1 - i] =
				ad7418_read(client, AD7418_REG_ADC);
		}

		/* restore old configuration value */
		ad7418_write(client, AD7418_REG_CONF, cfg);

		data->last_updated = jiffies;
		data->valid = 1;
	}

	mutex_unlock(&data->lock);

	return data;
}

static ssize_t show_temp(struct device *dev, struct device_attribute *devattr,
			char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct ad7418_data *data = ad7418_update_device(dev);
	return sprintf(buf, "%d\n",
		LM75_TEMP_FROM_REG(data->temp[attr->index]));
}

static ssize_t show_adc(struct device *dev, struct device_attribute *devattr,
			char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct ad7418_data *data = ad7418_update_device(dev);

	return sprintf(buf, "%d\n",
		((data->in[attr->index] >> 6) * 2500 + 512) / 1024);
}

static ssize_t set_temp(struct device *dev, struct device_attribute *devattr,
			const char *buf, size_t count)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct i2c_client *client = to_i2c_client(dev);
	struct ad7418_data *data = i2c_get_clientdata(client);
	int temp = simple_strtol(buf, NULL, 10);

	mutex_lock(&data->lock);
	data->temp[attr->index] = LM75_TEMP_TO_REG(temp);
	ad7418_write(client, AD7418_REG_TEMP[attr->index], data->temp[attr->index]);
	mutex_unlock(&data->lock);
	return count;
}

static SENSOR_DEVICE_ATTR(temp1_input, S_IRUGO, show_temp, NULL, 0);
static SENSOR_DEVICE_ATTR(temp1_max_hyst, S_IWUSR | S_IRUGO,
				show_temp, set_temp, 1);
static SENSOR_DEVICE_ATTR(temp1_max, S_IWUSR | S_IRUGO,
				show_temp, set_temp, 2);

static SENSOR_DEVICE_ATTR(in1_input, S_IRUGO, show_adc, NULL, 0);
static SENSOR_DEVICE_ATTR(in2_input, S_IRUGO, show_adc, NULL, 1);
static SENSOR_DEVICE_ATTR(in3_input, S_IRUGO, show_adc, NULL, 2);
static SENSOR_DEVICE_ATTR(in4_input, S_IRUGO, show_adc, NULL, 3);

static int ad7418_attach_adapter(struct i2c_adapter *adapter)
{
	if (!(adapter->class & I2C_CLASS_HWMON))
		return 0;
	return i2c_probe(adapter, &addr_data, ad7418_detect);
}

static struct attribute *ad7416_attributes[] = {
	&sensor_dev_attr_temp1_max.dev_attr.attr,
	&sensor_dev_attr_temp1_max_hyst.dev_attr.attr,
	&sensor_dev_attr_temp1_input.dev_attr.attr,
	NULL
};

static struct attribute *ad7417_attributes[] = {
	&sensor_dev_attr_temp1_max.dev_attr.attr,
	&sensor_dev_attr_temp1_max_hyst.dev_attr.attr,
	&sensor_dev_attr_temp1_input.dev_attr.attr,
	&sensor_dev_attr_in1_input.dev_attr.attr,
	&sensor_dev_attr_in2_input.dev_attr.attr,
	&sensor_dev_attr_in3_input.dev_attr.attr,
	&sensor_dev_attr_in4_input.dev_attr.attr,
	NULL
};

static struct attribute *ad7418_attributes[] = {
	&sensor_dev_attr_temp1_max.dev_attr.attr,
	&sensor_dev_attr_temp1_max_hyst.dev_attr.attr,
	&sensor_dev_attr_temp1_input.dev_attr.attr,
	&sensor_dev_attr_in1_input.dev_attr.attr,
	NULL
};

static int ad7418_detect(struct i2c_adapter *adapter, int address, int kind)
{
	struct i2c_client *client;
	struct ad7418_data *data;
	int err = 0;

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA |
					I2C_FUNC_SMBUS_WORD_DATA))
		goto exit;

	if (!(data = kzalloc(sizeof(struct ad7418_data), GFP_KERNEL))) {
		err = -ENOMEM;
		goto exit;
	}

	client = &data->client;
	client->addr = address;
	client->adapter = adapter;
	client->driver = &ad7418_driver;

	i2c_set_clientdata(client, data);

	mutex_init(&data->lock);

	/* AD7418 has a curious behaviour on registers 6 and 7. They
	 * both always read 0xC071 and are not documented on the datasheet.
	 * We use them to detect the chip.
	 */
	if (kind <= 0) {
		int reg, reg6, reg7;

		/* the AD7416 lies within this address range, but I have
		 * no means to check.
		 */
		if (address >= 0x48 && address <= 0x4f) {
			/* XXX add tests for AD7416 here */
			/* data->type = ad7416; */
		}
		/* here we might have AD7417 or AD7418 */
		else if (address >= 0x28 && address <= 0x2f) {
			reg6 = i2c_smbus_read_word_data(client, 0x06);
			reg7 = i2c_smbus_read_word_data(client, 0x07);

			if (address == 0x28 && reg6 == 0xC071 && reg7 == 0xC071)
				data->type = ad7418;

			/* XXX add tests for AD7417 here */


			/* both AD7417 and AD7418 have bits 0-5 of
			 * the CONF2 register at 0
			 */
			reg = i2c_smbus_read_byte_data(client,
							AD7418_REG_CONF2);
			if (reg & 0x3F)
				data->type = any_chip; /* detection failed */
		}
	} else {
		dev_dbg(&adapter->dev, "detection forced\n");
	}

	if (kind > 0)
		data->type = kind;
	else if (kind < 0 && data->type == any_chip) {
		err = -ENODEV;
		goto exit_free;
	}

	switch (data->type) {
	case any_chip:
	case ad7416:
		data->adc_max = 0;
		data->attrs.attrs = ad7416_attributes;
		strlcpy(client->name, "ad7416", I2C_NAME_SIZE);
		break;

	case ad7417:
		data->adc_max = 4;
		data->attrs.attrs = ad7417_attributes;
		strlcpy(client->name, "ad7417", I2C_NAME_SIZE);
		break;

	case ad7418:
		data->adc_max = 1;
		data->attrs.attrs = ad7418_attributes;
		strlcpy(client->name, "ad7418", I2C_NAME_SIZE);
		break;
	}

	if ((err = i2c_attach_client(client)))
		goto exit_free;

	dev_info(&client->dev, "%s chip found\n", client->name);

	/* Initialize the AD7418 chip */
	ad7418_init_client(client);

	/* Register sysfs hooks */
	if ((err = sysfs_create_group(&client->dev.kobj, &data->attrs)))
		goto exit_detach;

	data->class_dev = hwmon_device_register(&client->dev);
	if (IS_ERR(data->class_dev)) {
		err = PTR_ERR(data->class_dev);
		goto exit_remove;
	}

	return 0;

exit_remove:
	sysfs_remove_group(&client->dev.kobj, &data->attrs);
exit_detach:
	i2c_detach_client(client);
exit_free:
	kfree(data);
exit:
	return err;
}

static int ad7418_detach_client(struct i2c_client *client)
{
	struct ad7418_data *data = i2c_get_clientdata(client);
	hwmon_device_unregister(data->class_dev);
	sysfs_remove_group(&client->dev.kobj, &data->attrs);
	i2c_detach_client(client);
	kfree(data);
	return 0;
}

static int __init ad7418_init(void)
{
	return i2c_add_driver(&ad7418_driver);
}

static void __exit ad7418_exit(void)
{
	i2c_del_driver(&ad7418_driver);
}

MODULE_AUTHOR("Alessandro Zummo <a.zummo@towertech.it>");
MODULE_DESCRIPTION("AD7416/17/18 driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(DRV_VERSION);

module_init(ad7418_init);
module_exit(ad7418_exit);

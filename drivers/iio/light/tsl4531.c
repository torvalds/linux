// SPDX-License-Identifier: GPL-2.0-only
/*
 * tsl4531.c - Support for TAOS TSL4531 ambient light sensor
 *
 * Copyright 2013 Peter Meerwald <pmeerw@pmeerw.net>
 *
 * IIO driver for the TSL4531x family
 *   TSL45311/TSL45313: 7-bit I2C slave address 0x39
 *   TSL45315/TSL45317: 7-bit I2C slave address 0x29
 *
 * TODO: single cycle measurement
 */

#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/err.h>
#include <linux/delay.h>

#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>

#define TSL4531_DRV_NAME "tsl4531"

#define TSL4531_COMMAND BIT(7)

#define TSL4531_CONTROL (TSL4531_COMMAND | 0x00)
#define TSL4531_CONFIG (TSL4531_COMMAND | 0x01)
#define TSL4531_DATA (TSL4531_COMMAND | 0x04)
#define TSL4531_ID (TSL4531_COMMAND | 0x0a)

/* operating modes in control register */
#define TSL4531_MODE_POWERDOWN 0x00
#define TSL4531_MODE_SINGLE_ADC 0x02
#define TSL4531_MODE_NORMAL 0x03

/* integration time control in config register */
#define TSL4531_TCNTRL_400MS 0x00
#define TSL4531_TCNTRL_200MS 0x01
#define TSL4531_TCNTRL_100MS 0x02

/* part number in id register */
#define TSL45311_ID 0x8
#define TSL45313_ID 0x9
#define TSL45315_ID 0xa
#define TSL45317_ID 0xb
#define TSL4531_ID_SHIFT 4

struct tsl4531_data {
	struct i2c_client *client;
	struct mutex lock;
	int int_time;
};

static IIO_CONST_ATTR_INT_TIME_AVAIL("0.1 0.2 0.4");

static struct attribute *tsl4531_attributes[] = {
	&iio_const_attr_integration_time_available.dev_attr.attr,
	NULL
};

static const struct attribute_group tsl4531_attribute_group = {
	.attrs = tsl4531_attributes,
};

static const struct iio_chan_spec tsl4531_channels[] = {
	{
		.type = IIO_LIGHT,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
			BIT(IIO_CHAN_INFO_SCALE) |
			BIT(IIO_CHAN_INFO_INT_TIME)
	}
};

static int tsl4531_read_raw(struct iio_dev *indio_dev,
				struct iio_chan_spec const *chan,
				int *val, int *val2, long mask)
{
	struct tsl4531_data *data = iio_priv(indio_dev);
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		ret = i2c_smbus_read_word_data(data->client,
			TSL4531_DATA);
		if (ret < 0)
			return ret;
		*val = ret;
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		/* 0.. 1x, 1 .. 2x, 2 .. 4x */
		*val = 1 << data->int_time;
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_INT_TIME:
		if (data->int_time == 0)
			*val2 = 400000;
		else if (data->int_time == 1)
			*val2 = 200000;
		else if (data->int_time == 2)
			*val2 = 100000;
		else
			return -EINVAL;
		*val = 0;
		return IIO_VAL_INT_PLUS_MICRO;
	default:
		return -EINVAL;
	}
}

static int tsl4531_write_raw(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *chan,
			     int val, int val2, long mask)
{
	struct tsl4531_data *data = iio_priv(indio_dev);
	int int_time, ret;

	switch (mask) {
	case IIO_CHAN_INFO_INT_TIME:
		if (val != 0)
			return -EINVAL;
		if (val2 == 400000)
			int_time = 0;
		else if (val2 == 200000)
			int_time = 1;
		else if (val2 == 100000)
			int_time = 2;
		else
			return -EINVAL;
		mutex_lock(&data->lock);
		ret = i2c_smbus_write_byte_data(data->client,
			TSL4531_CONFIG, int_time);
		if (ret >= 0)
			data->int_time = int_time;
		mutex_unlock(&data->lock);
		return ret;
	default:
		return -EINVAL;
	}
}

static const struct iio_info tsl4531_info = {
	.read_raw = tsl4531_read_raw,
	.write_raw = tsl4531_write_raw,
	.attrs = &tsl4531_attribute_group,
};

static int tsl4531_check_id(struct i2c_client *client)
{
	int ret = i2c_smbus_read_byte_data(client, TSL4531_ID);
	if (ret < 0)
		return ret;

	switch (ret >> TSL4531_ID_SHIFT) {
	case TSL45311_ID:
	case TSL45313_ID:
	case TSL45315_ID:
	case TSL45317_ID:
		return 0;
	default:
		return -ENODEV;
	}
}

static int tsl4531_probe(struct i2c_client *client)
{
	struct tsl4531_data *data;
	struct iio_dev *indio_dev;
	int ret;

	indio_dev = devm_iio_device_alloc(&client->dev, sizeof(*data));
	if (!indio_dev)
		return -ENOMEM;

	data = iio_priv(indio_dev);
	i2c_set_clientdata(client, indio_dev);
	data->client = client;
	mutex_init(&data->lock);

	ret = tsl4531_check_id(client);
	if (ret) {
		dev_err(&client->dev, "no TSL4531 sensor\n");
		return ret;
	}

	ret = i2c_smbus_write_byte_data(data->client, TSL4531_CONTROL,
		TSL4531_MODE_NORMAL);
	if (ret < 0)
		return ret;

	ret = i2c_smbus_write_byte_data(data->client, TSL4531_CONFIG,
		TSL4531_TCNTRL_400MS);
	if (ret < 0)
		return ret;

	indio_dev->info = &tsl4531_info;
	indio_dev->channels = tsl4531_channels;
	indio_dev->num_channels = ARRAY_SIZE(tsl4531_channels);
	indio_dev->name = TSL4531_DRV_NAME;
	indio_dev->modes = INDIO_DIRECT_MODE;

	return iio_device_register(indio_dev);
}

static int tsl4531_powerdown(struct i2c_client *client)
{
	return i2c_smbus_write_byte_data(client, TSL4531_CONTROL,
		TSL4531_MODE_POWERDOWN);
}

static void tsl4531_remove(struct i2c_client *client)
{
	iio_device_unregister(i2c_get_clientdata(client));
	tsl4531_powerdown(client);
}

static int tsl4531_suspend(struct device *dev)
{
	return tsl4531_powerdown(to_i2c_client(dev));
}

static int tsl4531_resume(struct device *dev)
{
	return i2c_smbus_write_byte_data(to_i2c_client(dev), TSL4531_CONTROL,
		TSL4531_MODE_NORMAL);
}

static DEFINE_SIMPLE_DEV_PM_OPS(tsl4531_pm_ops, tsl4531_suspend,
				tsl4531_resume);

static const struct i2c_device_id tsl4531_id[] = {
	{ "tsl4531" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, tsl4531_id);

static struct i2c_driver tsl4531_driver = {
	.driver = {
		.name   = TSL4531_DRV_NAME,
		.pm	= pm_sleep_ptr(&tsl4531_pm_ops),
	},
	.probe = tsl4531_probe,
	.remove = tsl4531_remove,
	.id_table = tsl4531_id,
};

module_i2c_driver(tsl4531_driver);

MODULE_AUTHOR("Peter Meerwald <pmeerw@pmeerw.net>");
MODULE_DESCRIPTION("TAOS TSL4531 ambient light sensors driver");
MODULE_LICENSE("GPL");

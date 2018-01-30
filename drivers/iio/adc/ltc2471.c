/*
 * Driver for Linear Technology LTC2471 and LTC2473 voltage monitors
 * The LTC2473 is identical to the 2471, but reports a differential signal.
 *
 * Copyright (C) 2017 Topic Embedded Products
 * Author: Mike Looijmans <mike.looijmans@topic.nl>
 *
 * License: GPLv2
 */

#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>

enum ltc2471_chips {
	ltc2471,
	ltc2473,
};

struct ltc2471_data {
	struct i2c_client *client;
};

/* Reference voltage is 1.25V */
#define LTC2471_VREF 1250

/* Read two bytes from the I2C bus to obtain the ADC result */
static int ltc2471_get_value(struct i2c_client *client)
{
	int ret;
	__be16 buf;

	ret = i2c_master_recv(client, (char *)&buf, sizeof(buf));
	if (ret < 0)
		return ret;
	if (ret != sizeof(buf))
		return -EIO;

	/* MSB first */
	return be16_to_cpu(buf);
}

static int ltc2471_read_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan,
			    int *val, int *val2, long info)
{
	struct ltc2471_data *data = iio_priv(indio_dev);
	int ret;

	switch (info) {
	case IIO_CHAN_INFO_RAW:
		ret = ltc2471_get_value(data->client);
		if (ret < 0)
			return ret;
		*val = ret;
		return IIO_VAL_INT;

	case IIO_CHAN_INFO_SCALE:
		if (chan->differential)
			/* Output ranges from -VREF to +VREF */
			*val = 2 * LTC2471_VREF;
		else
			/* Output ranges from 0 to VREF */
			*val = LTC2471_VREF;
		*val2 = 16;	/* 16 data bits */
		return IIO_VAL_FRACTIONAL_LOG2;

	case IIO_CHAN_INFO_OFFSET:
		/* Only differential chip has this property */
		*val = -LTC2471_VREF;
		return IIO_VAL_INT;

	default:
		return -EINVAL;
	}
}

static const struct iio_chan_spec ltc2471_channel[] = {
	{
		.type = IIO_VOLTAGE,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE),
	},
};

static const struct iio_chan_spec ltc2473_channel[] = {
	{
		.type = IIO_VOLTAGE,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE) |
					    BIT(IIO_CHAN_INFO_OFFSET),
		.differential = 1,
	},
};

static const struct iio_info ltc2471_info = {
	.read_raw = ltc2471_read_raw,
};

static int ltc2471_i2c_probe(struct i2c_client *client,
			     const struct i2c_device_id *id)
{
	struct iio_dev *indio_dev;
	struct ltc2471_data *data;
	int ret;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C))
		return -EOPNOTSUPP;

	indio_dev = devm_iio_device_alloc(&client->dev, sizeof(*data));
	if (!indio_dev)
		return -ENOMEM;

	data = iio_priv(indio_dev);
	data->client = client;

	indio_dev->dev.parent = &client->dev;
	indio_dev->name = id->name;
	indio_dev->info = &ltc2471_info;
	indio_dev->modes = INDIO_DIRECT_MODE;
	if (id->driver_data == ltc2473)
		indio_dev->channels = ltc2473_channel;
	else
		indio_dev->channels = ltc2471_channel;
	indio_dev->num_channels = 1;

	/* Trigger once to start conversion and check if chip is there */
	ret = ltc2471_get_value(client);
	if (ret < 0) {
		dev_err(&client->dev, "Cannot read from device.\n");
		return ret;
	}

	return devm_iio_device_register(&client->dev, indio_dev);
}

static const struct i2c_device_id ltc2471_i2c_id[] = {
	{ "ltc2471", ltc2471 },
	{ "ltc2473", ltc2473 },
	{}
};
MODULE_DEVICE_TABLE(i2c, ltc2471_i2c_id);

static struct i2c_driver ltc2471_i2c_driver = {
	.driver = {
		.name = "ltc2471",
	},
	.probe    = ltc2471_i2c_probe,
	.id_table = ltc2471_i2c_id,
};

module_i2c_driver(ltc2471_i2c_driver);

MODULE_DESCRIPTION("LTC2471/LTC2473 ADC driver");
MODULE_AUTHOR("Topic Embedded Products");
MODULE_LICENSE("GPL v2");

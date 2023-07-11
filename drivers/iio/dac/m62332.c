// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  m62332.c - Support for Mitsubishi m62332 DAC
 *
 *  Copyright (c) 2014 Dmitry Eremin-Solenikov
 *
 *  Based on max517 driver:
 *  Copyright (C) 2010, 2011 Roland Stigge <stigge@antcom.de>
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/err.h>

#include <linux/iio/iio.h>
#include <linux/iio/driver.h>

#include <linux/regulator/consumer.h>

#define M62332_CHANNELS 2

struct m62332_data {
	struct i2c_client	*client;
	struct regulator	*vcc;
	struct mutex		mutex;
	u8			raw[M62332_CHANNELS];
	u8			save[M62332_CHANNELS];
};

static int m62332_set_value(struct iio_dev *indio_dev, u8 val, int channel)
{
	struct m62332_data *data = iio_priv(indio_dev);
	struct i2c_client *client = data->client;
	u8 outbuf[2];
	int res;

	if (val == data->raw[channel])
		return 0;

	outbuf[0] = channel;
	outbuf[1] = val;

	mutex_lock(&data->mutex);

	if (val) {
		res = regulator_enable(data->vcc);
		if (res)
			goto out;
	}

	res = i2c_master_send(client, outbuf, ARRAY_SIZE(outbuf));
	if (res >= 0 && res != ARRAY_SIZE(outbuf))
		res = -EIO;
	if (res < 0)
		goto out;

	data->raw[channel] = val;

	if (!val)
		regulator_disable(data->vcc);

	mutex_unlock(&data->mutex);

	return 0;

out:
	mutex_unlock(&data->mutex);

	return res;
}

static int m62332_read_raw(struct iio_dev *indio_dev,
			   struct iio_chan_spec const *chan,
			   int *val,
			   int *val2,
			   long mask)
{
	struct m62332_data *data = iio_priv(indio_dev);
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_SCALE:
		/* Corresponds to Vref / 2^(bits) */
		ret = regulator_get_voltage(data->vcc);
		if (ret < 0)
			return ret;

		*val = ret / 1000; /* mV */
		*val2 = 8;

		return IIO_VAL_FRACTIONAL_LOG2;
	case IIO_CHAN_INFO_RAW:
		*val = data->raw[chan->channel];

		return IIO_VAL_INT;
	case IIO_CHAN_INFO_OFFSET:
		*val = 1;

		return IIO_VAL_INT;
	default:
		break;
	}

	return -EINVAL;
}

static int m62332_write_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan, int val, int val2,
			    long mask)
{
	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		if (val < 0 || val > 255)
			return -EINVAL;

		return m62332_set_value(indio_dev, val, chan->channel);
	default:
		break;
	}

	return -EINVAL;
}

static int m62332_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct iio_dev *indio_dev = i2c_get_clientdata(client);
	struct m62332_data *data = iio_priv(indio_dev);
	int ret;

	data->save[0] = data->raw[0];
	data->save[1] = data->raw[1];

	ret = m62332_set_value(indio_dev, 0, 0);
	if (ret < 0)
		return ret;

	return m62332_set_value(indio_dev, 0, 1);
}

static int m62332_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct iio_dev *indio_dev = i2c_get_clientdata(client);
	struct m62332_data *data = iio_priv(indio_dev);
	int ret;

	ret = m62332_set_value(indio_dev, data->save[0], 0);
	if (ret < 0)
		return ret;

	return m62332_set_value(indio_dev, data->save[1], 1);
}

static DEFINE_SIMPLE_DEV_PM_OPS(m62332_pm_ops, m62332_suspend, m62332_resume);

static const struct iio_info m62332_info = {
	.read_raw = m62332_read_raw,
	.write_raw = m62332_write_raw,
};

#define M62332_CHANNEL(chan) {					\
	.type = IIO_VOLTAGE,					\
	.indexed = 1,						\
	.output = 1,						\
	.channel = (chan),					\
	.datasheet_name = "CH" #chan,				\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),		\
	.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE) |	\
				    BIT(IIO_CHAN_INFO_OFFSET),	\
}

static const struct iio_chan_spec m62332_channels[M62332_CHANNELS] = {
	M62332_CHANNEL(0),
	M62332_CHANNEL(1)
};

static int m62332_probe(struct i2c_client *client)
{
	struct m62332_data *data;
	struct iio_dev *indio_dev;
	int ret;

	indio_dev = devm_iio_device_alloc(&client->dev, sizeof(*data));
	if (!indio_dev)
		return -ENOMEM;

	data = iio_priv(indio_dev);
	i2c_set_clientdata(client, indio_dev);
	data->client = client;

	mutex_init(&data->mutex);

	data->vcc = devm_regulator_get(&client->dev, "VCC");
	if (IS_ERR(data->vcc))
		return PTR_ERR(data->vcc);

	indio_dev->num_channels = ARRAY_SIZE(m62332_channels);
	indio_dev->channels = m62332_channels;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->info = &m62332_info;

	ret = iio_map_array_register(indio_dev, client->dev.platform_data);
	if (ret < 0)
		return ret;

	ret = iio_device_register(indio_dev);
	if (ret < 0)
		goto err;

	return 0;

err:
	iio_map_array_unregister(indio_dev);

	return ret;
}

static void m62332_remove(struct i2c_client *client)
{
	struct iio_dev *indio_dev = i2c_get_clientdata(client);

	iio_device_unregister(indio_dev);
	iio_map_array_unregister(indio_dev);
	m62332_set_value(indio_dev, 0, 0);
	m62332_set_value(indio_dev, 0, 1);
}

static const struct i2c_device_id m62332_id[] = {
	{ "m62332", },
	{ }
};
MODULE_DEVICE_TABLE(i2c, m62332_id);

static struct i2c_driver m62332_driver = {
	.driver = {
		.name	= "m62332",
		.pm	= pm_sleep_ptr(&m62332_pm_ops),
	},
	.probe		= m62332_probe,
	.remove		= m62332_remove,
	.id_table	= m62332_id,
};
module_i2c_driver(m62332_driver);

MODULE_AUTHOR("Dmitry Eremin-Solenikov");
MODULE_DESCRIPTION("M62332 8-bit DAC");
MODULE_LICENSE("GPL v2");

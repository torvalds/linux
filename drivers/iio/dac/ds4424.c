/*
 * Maxim Integrated
 * 7-bit, Multi-Channel Sink/Source Current DAC Driver
 * Copyright (C) 2017 Maxim Integrated
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/regulator/consumer.h>
#include <linux/err.h>
#include <linux/delay.h>
#include <linux/iio/iio.h>
#include <linux/iio/driver.h>
#include <linux/iio/machine.h>
#include <linux/iio/consumer.h>

#define DS4422_MAX_DAC_CHANNELS		2
#define DS4424_MAX_DAC_CHANNELS		4

#define DS4424_DAC_ADDR(chan)   ((chan) + 0xf8)
#define DS4424_SOURCE_I		1
#define DS4424_SINK_I		0

#define DS4424_CHANNEL(chan) { \
	.type = IIO_CURRENT, \
	.indexed = 1, \
	.output = 1, \
	.channel = chan, \
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW), \
}

/*
 * DS4424 DAC control register 8 bits
 * [7]		0: to sink; 1: to source
 * [6:0]	steps to sink/source
 * bit[7] looks like a sign bit, but the value of the register is
 * not a two's complement code considering the bit[6:0] is a absolute
 * distance from the zero point.
 */
union ds4424_raw_data {
	struct {
		u8 dx:7;
		u8 source_bit:1;
	};
	u8 bits;
};

enum ds4424_device_ids {
	ID_DS4422,
	ID_DS4424,
};

struct ds4424_data {
	struct i2c_client *client;
	struct mutex lock;
	uint8_t save[DS4424_MAX_DAC_CHANNELS];
	struct regulator *vcc_reg;
	uint8_t raw[DS4424_MAX_DAC_CHANNELS];
};

static const struct iio_chan_spec ds4424_channels[] = {
	DS4424_CHANNEL(0),
	DS4424_CHANNEL(1),
	DS4424_CHANNEL(2),
	DS4424_CHANNEL(3),
};

static int ds4424_get_value(struct iio_dev *indio_dev,
			     int *val, int channel)
{
	struct ds4424_data *data = iio_priv(indio_dev);
	int ret;

	mutex_lock(&data->lock);
	ret = i2c_smbus_read_byte_data(data->client, DS4424_DAC_ADDR(channel));
	if (ret < 0)
		goto fail;

	*val = ret;

fail:
	mutex_unlock(&data->lock);
	return ret;
}

static int ds4424_set_value(struct iio_dev *indio_dev,
			     int val, struct iio_chan_spec const *chan)
{
	struct ds4424_data *data = iio_priv(indio_dev);
	int ret;

	mutex_lock(&data->lock);
	ret = i2c_smbus_write_byte_data(data->client,
			DS4424_DAC_ADDR(chan->channel), val);
	if (ret < 0)
		goto fail;

	data->raw[chan->channel] = val;

fail:
	mutex_unlock(&data->lock);
	return ret;
}

static int ds4424_read_raw(struct iio_dev *indio_dev,
			   struct iio_chan_spec const *chan,
			   int *val, int *val2, long mask)
{
	union ds4424_raw_data raw;
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		ret = ds4424_get_value(indio_dev, val, chan->channel);
		if (ret < 0) {
			pr_err("%s : ds4424_get_value returned %d\n",
							__func__, ret);
			return ret;
		}
		raw.bits = *val;
		*val = raw.dx;
		if (raw.source_bit == DS4424_SINK_I)
			*val = -*val;
		return IIO_VAL_INT;

	default:
		return -EINVAL;
	}
}

static int ds4424_write_raw(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *chan,
			     int val, int val2, long mask)
{
	union ds4424_raw_data raw;

	if (val2 != 0)
		return -EINVAL;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		if (val < S8_MIN || val > S8_MAX)
			return -EINVAL;

		if (val > 0) {
			raw.source_bit = DS4424_SOURCE_I;
			raw.dx = val;
		} else {
			raw.source_bit = DS4424_SINK_I;
			raw.dx = -val;
		}

		return ds4424_set_value(indio_dev, raw.bits, chan);

	default:
		return -EINVAL;
	}
}

static int ds4424_verify_chip(struct iio_dev *indio_dev)
{
	int ret, val;

	ret = ds4424_get_value(indio_dev, &val, 0);
	if (ret < 0)
		dev_err(&indio_dev->dev,
				"%s failed. ret: %d\n", __func__, ret);

	return ret;
}

static int __maybe_unused ds4424_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct iio_dev *indio_dev = i2c_get_clientdata(client);
	struct ds4424_data *data = iio_priv(indio_dev);
	int ret = 0;
	int i;

	for (i = 0; i < indio_dev->num_channels; i++) {
		data->save[i] = data->raw[i];
		ret = ds4424_set_value(indio_dev, 0,
				&indio_dev->channels[i]);
		if (ret < 0)
			return ret;
	}
	return ret;
}

static int __maybe_unused ds4424_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct iio_dev *indio_dev = i2c_get_clientdata(client);
	struct ds4424_data *data = iio_priv(indio_dev);
	int ret = 0;
	int i;

	for (i = 0; i < indio_dev->num_channels; i++) {
		ret = ds4424_set_value(indio_dev, data->save[i],
				&indio_dev->channels[i]);
		if (ret < 0)
			return ret;
	}
	return ret;
}

static SIMPLE_DEV_PM_OPS(ds4424_pm_ops, ds4424_suspend, ds4424_resume);

static const struct iio_info ds4424_info = {
	.read_raw = ds4424_read_raw,
	.write_raw = ds4424_write_raw,
};

static int ds4424_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct ds4424_data *data;
	struct iio_dev *indio_dev;
	int ret;

	indio_dev = devm_iio_device_alloc(&client->dev, sizeof(*data));
	if (!indio_dev) {
		dev_err(&client->dev, "iio dev alloc failed.\n");
		return -ENOMEM;
	}

	data = iio_priv(indio_dev);
	i2c_set_clientdata(client, indio_dev);
	data->client = client;
	indio_dev->name = id->name;
	indio_dev->dev.of_node = client->dev.of_node;
	indio_dev->dev.parent = &client->dev;

	if (!client->dev.of_node) {
		dev_err(&client->dev,
				"Not found DT.\n");
		return -ENODEV;
	}

	data->vcc_reg = devm_regulator_get(&client->dev, "vcc");
	if (IS_ERR(data->vcc_reg)) {
		dev_err(&client->dev,
			"Failed to get vcc-supply regulator. err: %ld\n",
				PTR_ERR(data->vcc_reg));
		return PTR_ERR(data->vcc_reg);
	}

	mutex_init(&data->lock);
	ret = regulator_enable(data->vcc_reg);
	if (ret < 0) {
		dev_err(&client->dev,
				"Unable to enable the regulator.\n");
		return ret;
	}

	usleep_range(1000, 1200);
	ret = ds4424_verify_chip(indio_dev);
	if (ret < 0)
		goto fail;

	switch (id->driver_data) {
	case ID_DS4422:
		indio_dev->num_channels = DS4422_MAX_DAC_CHANNELS;
		break;
	case ID_DS4424:
		indio_dev->num_channels = DS4424_MAX_DAC_CHANNELS;
		break;
	default:
		dev_err(&client->dev,
				"ds4424: Invalid chip id.\n");
		ret = -ENXIO;
		goto fail;
	}

	indio_dev->channels = ds4424_channels;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->info = &ds4424_info;

	ret = iio_device_register(indio_dev);
	if (ret < 0) {
		dev_err(&client->dev,
				"iio_device_register failed. ret: %d\n", ret);
		goto fail;
	}

	return ret;

fail:
	regulator_disable(data->vcc_reg);
	return ret;
}

static int ds4424_remove(struct i2c_client *client)
{
	struct iio_dev *indio_dev = i2c_get_clientdata(client);
	struct ds4424_data *data = iio_priv(indio_dev);

	iio_device_unregister(indio_dev);
	regulator_disable(data->vcc_reg);

	return 0;
}

static const struct i2c_device_id ds4424_id[] = {
	{ "ds4422", ID_DS4422 },
	{ "ds4424", ID_DS4424 },
	{ }
};

MODULE_DEVICE_TABLE(i2c, ds4424_id);

static const struct of_device_id ds4424_of_match[] = {
	{ .compatible = "maxim,ds4422" },
	{ .compatible = "maxim,ds4424" },
	{ },
};

MODULE_DEVICE_TABLE(of, ds4424_of_match);

static struct i2c_driver ds4424_driver = {
	.driver = {
		.name	= "ds4424",
		.of_match_table = ds4424_of_match,
		.pm     = &ds4424_pm_ops,
	},
	.probe		= ds4424_probe,
	.remove		= ds4424_remove,
	.id_table	= ds4424_id,
};
module_i2c_driver(ds4424_driver);

MODULE_DESCRIPTION("Maxim DS4424 DAC Driver");
MODULE_AUTHOR("Ismail H. Kose <ismail.kose@maximintegrated.com>");
MODULE_AUTHOR("Vishal Sood <vishal.sood@maximintegrated.com>");
MODULE_AUTHOR("David Jung <david.jung@maximintegrated.com>");
MODULE_LICENSE("GPL v2");

// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  max517.c - Support for Maxim MAX517, MAX518 and MAX519
 *
 *  Copyright (C) 2010, 2011 Roland Stigge <stigge@antcom.de>
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/jiffies.h>
#include <linux/i2c.h>
#include <linux/err.h>

#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/dac/max517.h>

#define MAX517_DRV_NAME	"max517"

/* Commands */
#define COMMAND_CHANNEL0	0x00
#define COMMAND_CHANNEL1	0x01 /* for MAX518 and MAX519 */
#define COMMAND_PD		0x08 /* Power Down */

enum max517_device_ids {
	ID_MAX517,
	ID_MAX518,
	ID_MAX519,
	ID_MAX520,
	ID_MAX521,
};

struct max517_data {
	struct i2c_client	*client;
	unsigned short		vref_mv[8];
};

/*
 * channel: bit 0: channel 1
 *          bit 1: channel 2
 * (this way, it's possible to set both channels at once)
 */
static int max517_set_value(struct iio_dev *indio_dev,
	long val, int channel)
{
	struct max517_data *data = iio_priv(indio_dev);
	struct i2c_client *client = data->client;
	u8 outbuf[2];
	int res;

	if (val < 0 || val > 255)
		return -EINVAL;

	outbuf[0] = channel;
	outbuf[1] = val;

	res = i2c_master_send(client, outbuf, 2);
	if (res < 0)
		return res;
	else if (res != 2)
		return -EIO;
	else
		return 0;
}

static int max517_read_raw(struct iio_dev *indio_dev,
			   struct iio_chan_spec const *chan,
			   int *val,
			   int *val2,
			   long m)
{
	struct max517_data *data = iio_priv(indio_dev);

	switch (m) {
	case IIO_CHAN_INFO_SCALE:
		/* Corresponds to Vref / 2^(bits) */
		*val = data->vref_mv[chan->channel];
		*val2 = 8;
		return IIO_VAL_FRACTIONAL_LOG2;
	default:
		break;
	}
	return -EINVAL;
}

static int max517_write_raw(struct iio_dev *indio_dev,
	struct iio_chan_spec const *chan, int val, int val2, long mask)
{
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		ret = max517_set_value(indio_dev, val, chan->channel);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int __maybe_unused max517_suspend(struct device *dev)
{
	u8 outbuf = COMMAND_PD;

	return i2c_master_send(to_i2c_client(dev), &outbuf, 1);
}

static int __maybe_unused max517_resume(struct device *dev)
{
	u8 outbuf = 0;

	return i2c_master_send(to_i2c_client(dev), &outbuf, 1);
}

static SIMPLE_DEV_PM_OPS(max517_pm_ops, max517_suspend, max517_resume);

static const struct iio_info max517_info = {
	.read_raw = max517_read_raw,
	.write_raw = max517_write_raw,
};

#define MAX517_CHANNEL(chan) {				\
	.type = IIO_VOLTAGE,				\
	.indexed = 1,					\
	.output = 1,					\
	.channel = (chan),				\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |	\
	BIT(IIO_CHAN_INFO_SCALE),			\
}

static const struct iio_chan_spec max517_channels[] = {
	MAX517_CHANNEL(0),
	MAX517_CHANNEL(1),
	MAX517_CHANNEL(2),
	MAX517_CHANNEL(3),
	MAX517_CHANNEL(4),
	MAX517_CHANNEL(5),
	MAX517_CHANNEL(6),
	MAX517_CHANNEL(7),
};

static int max517_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct max517_data *data;
	struct iio_dev *indio_dev;
	struct max517_platform_data *platform_data = client->dev.platform_data;
	int chan;

	indio_dev = devm_iio_device_alloc(&client->dev, sizeof(*data));
	if (!indio_dev)
		return -ENOMEM;
	data = iio_priv(indio_dev);
	i2c_set_clientdata(client, indio_dev);
	data->client = client;

	switch (id->driver_data) {
	case ID_MAX521:
		indio_dev->num_channels = 8;
		break;
	case ID_MAX520:
		indio_dev->num_channels = 4;
		break;
	case ID_MAX519:
	case ID_MAX518:
		indio_dev->num_channels = 2;
		break;
	default:  /* single channel for MAX517 */
		indio_dev->num_channels = 1;
		break;
	}
	indio_dev->channels = max517_channels;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->info = &max517_info;

	/*
	 * Reference voltage on MAX518 and default is 5V, else take vref_mv
	 * from platform_data
	 */
	for (chan = 0; chan < indio_dev->num_channels; chan++) {
		if (id->driver_data == ID_MAX518 || !platform_data)
			data->vref_mv[chan] = 5000; /* mV */
		else
			data->vref_mv[chan] = platform_data->vref_mv[chan];
	}

	return iio_device_register(indio_dev);
}

static int max517_remove(struct i2c_client *client)
{
	iio_device_unregister(i2c_get_clientdata(client));
	return 0;
}

static const struct i2c_device_id max517_id[] = {
	{ "max517", ID_MAX517 },
	{ "max518", ID_MAX518 },
	{ "max519", ID_MAX519 },
	{ "max520", ID_MAX520 },
	{ "max521", ID_MAX521 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, max517_id);

static struct i2c_driver max517_driver = {
	.driver = {
		.name	= MAX517_DRV_NAME,
		.pm	= &max517_pm_ops,
	},
	.probe		= max517_probe,
	.remove		= max517_remove,
	.id_table	= max517_id,
};
module_i2c_driver(max517_driver);

MODULE_AUTHOR("Roland Stigge <stigge@antcom.de>");
MODULE_DESCRIPTION("MAX517/518/519/520/521 8-bit DAC");
MODULE_LICENSE("GPL");

// SPDX-License-Identifier: GPL-2.0-only
/*
 * adjd_s311.c - Support for ADJD-S311-CR999 digital color sensor
 *
 * Copyright (C) 2012 Peter Meerwald <pmeerw@pmeerw.net>
 *
 * driver for ADJD-S311-CR999 digital color sensor (10-bit channels for
 * red, green, blue, clear); 7-bit I2C slave address 0x74
 *
 * limitations: no calibration, no offset mode, no sleep mode
 */

#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/bitmap.h>
#include <linux/err.h>
#include <linux/irq.h>

#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/iio/buffer.h>
#include <linux/iio/triggered_buffer.h>

#define ADJD_S311_DRV_NAME "adjd_s311"

#define ADJD_S311_CTRL		0x00
#define ADJD_S311_CONFIG	0x01
#define ADJD_S311_CAP_RED	0x06
#define ADJD_S311_CAP_GREEN	0x07
#define ADJD_S311_CAP_BLUE	0x08
#define ADJD_S311_CAP_CLEAR	0x09
#define ADJD_S311_INT_RED	0x0a
#define ADJD_S311_INT_GREEN	0x0c
#define ADJD_S311_INT_BLUE	0x0e
#define ADJD_S311_INT_CLEAR	0x10
#define ADJD_S311_DATA_RED	0x40
#define ADJD_S311_DATA_GREEN	0x42
#define ADJD_S311_DATA_BLUE	0x44
#define ADJD_S311_DATA_CLEAR	0x46
#define ADJD_S311_OFFSET_RED	0x48
#define ADJD_S311_OFFSET_GREEN	0x49
#define ADJD_S311_OFFSET_BLUE	0x4a
#define ADJD_S311_OFFSET_CLEAR	0x4b

#define ADJD_S311_CTRL_GOFS	0x02
#define ADJD_S311_CTRL_GSSR	0x01
#define ADJD_S311_CAP_MASK	0x0f
#define ADJD_S311_INT_MASK	0x0fff
#define ADJD_S311_DATA_MASK	0x03ff

struct adjd_s311_data {
	struct i2c_client *client;
	u16 *buffer;
};

enum adjd_s311_channel_idx {
	IDX_RED, IDX_GREEN, IDX_BLUE, IDX_CLEAR
};

#define ADJD_S311_DATA_REG(chan) (ADJD_S311_DATA_RED + (chan) * 2)
#define ADJD_S311_INT_REG(chan) (ADJD_S311_INT_RED + (chan) * 2)
#define ADJD_S311_CAP_REG(chan) (ADJD_S311_CAP_RED + (chan))

static int adjd_s311_req_data(struct iio_dev *indio_dev)
{
	struct adjd_s311_data *data = iio_priv(indio_dev);
	int tries = 10;

	int ret = i2c_smbus_write_byte_data(data->client, ADJD_S311_CTRL,
		ADJD_S311_CTRL_GSSR);
	if (ret < 0)
		return ret;

	while (tries--) {
		ret = i2c_smbus_read_byte_data(data->client, ADJD_S311_CTRL);
		if (ret < 0)
			return ret;
		if (!(ret & ADJD_S311_CTRL_GSSR))
			break;
		msleep(20);
	}

	if (tries < 0) {
		dev_err(&data->client->dev,
			"adjd_s311_req_data() failed, data not ready\n");
		return -EIO;
	}

	return 0;
}

static int adjd_s311_read_data(struct iio_dev *indio_dev, u8 reg, int *val)
{
	struct adjd_s311_data *data = iio_priv(indio_dev);

	int ret = adjd_s311_req_data(indio_dev);
	if (ret < 0)
		return ret;

	ret = i2c_smbus_read_word_data(data->client, reg);
	if (ret < 0)
		return ret;

	*val = ret & ADJD_S311_DATA_MASK;

	return 0;
}

static irqreturn_t adjd_s311_trigger_handler(int irq, void *p)
{
	struct iio_poll_func *pf = p;
	struct iio_dev *indio_dev = pf->indio_dev;
	struct adjd_s311_data *data = iio_priv(indio_dev);
	s64 time_ns = iio_get_time_ns(indio_dev);
	int i, j = 0;

	int ret = adjd_s311_req_data(indio_dev);
	if (ret < 0)
		goto done;

	for_each_set_bit(i, indio_dev->active_scan_mask,
		indio_dev->masklength) {
		ret = i2c_smbus_read_word_data(data->client,
			ADJD_S311_DATA_REG(i));
		if (ret < 0)
			goto done;

		data->buffer[j++] = ret & ADJD_S311_DATA_MASK;
	}

	iio_push_to_buffers_with_timestamp(indio_dev, data->buffer, time_ns);

done:
	iio_trigger_notify_done(indio_dev->trig);

	return IRQ_HANDLED;
}

#define ADJD_S311_CHANNEL(_color, _scan_idx) { \
	.type = IIO_INTENSITY, \
	.modified = 1, \
	.address = (IDX_##_color), \
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) | \
		BIT(IIO_CHAN_INFO_HARDWAREGAIN) | \
		BIT(IIO_CHAN_INFO_INT_TIME), \
	.channel2 = (IIO_MOD_LIGHT_##_color), \
	.scan_index = (_scan_idx), \
	.scan_type = { \
		.sign = 'u', \
		.realbits = 10, \
		.storagebits = 16, \
		.endianness = IIO_CPU, \
	}, \
}

static const struct iio_chan_spec adjd_s311_channels[] = {
	ADJD_S311_CHANNEL(RED, 0),
	ADJD_S311_CHANNEL(GREEN, 1),
	ADJD_S311_CHANNEL(BLUE, 2),
	ADJD_S311_CHANNEL(CLEAR, 3),
	IIO_CHAN_SOFT_TIMESTAMP(4),
};

static int adjd_s311_read_raw(struct iio_dev *indio_dev,
			   struct iio_chan_spec const *chan,
			   int *val, int *val2, long mask)
{
	struct adjd_s311_data *data = iio_priv(indio_dev);
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		ret = adjd_s311_read_data(indio_dev,
			ADJD_S311_DATA_REG(chan->address), val);
		if (ret < 0)
			return ret;
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_HARDWAREGAIN:
		ret = i2c_smbus_read_byte_data(data->client,
			ADJD_S311_CAP_REG(chan->address));
		if (ret < 0)
			return ret;
		*val = ret & ADJD_S311_CAP_MASK;
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_INT_TIME:
		ret = i2c_smbus_read_word_data(data->client,
			ADJD_S311_INT_REG(chan->address));
		if (ret < 0)
			return ret;
		*val = 0;
		/*
		 * not documented, based on measurement:
		 * 4095 LSBs correspond to roughly 4 ms
		 */
		*val2 = ret & ADJD_S311_INT_MASK;
		return IIO_VAL_INT_PLUS_MICRO;
	}
	return -EINVAL;
}

static int adjd_s311_write_raw(struct iio_dev *indio_dev,
			       struct iio_chan_spec const *chan,
			       int val, int val2, long mask)
{
	struct adjd_s311_data *data = iio_priv(indio_dev);

	switch (mask) {
	case IIO_CHAN_INFO_HARDWAREGAIN:
		if (val < 0 || val > ADJD_S311_CAP_MASK)
			return -EINVAL;

		return i2c_smbus_write_byte_data(data->client,
			ADJD_S311_CAP_REG(chan->address), val);
	case IIO_CHAN_INFO_INT_TIME:
		if (val != 0 || val2 < 0 || val2 > ADJD_S311_INT_MASK)
			return -EINVAL;

		return i2c_smbus_write_word_data(data->client,
			ADJD_S311_INT_REG(chan->address), val2);
	}
	return -EINVAL;
}

static int adjd_s311_update_scan_mode(struct iio_dev *indio_dev,
	const unsigned long *scan_mask)
{
	struct adjd_s311_data *data = iio_priv(indio_dev);

	kfree(data->buffer);
	data->buffer = kmalloc(indio_dev->scan_bytes, GFP_KERNEL);
	if (data->buffer == NULL)
		return -ENOMEM;

	return 0;
}

static const struct iio_info adjd_s311_info = {
	.read_raw = adjd_s311_read_raw,
	.write_raw = adjd_s311_write_raw,
	.update_scan_mode = adjd_s311_update_scan_mode,
};

static int adjd_s311_probe(struct i2c_client *client,
			   const struct i2c_device_id *id)
{
	struct adjd_s311_data *data;
	struct iio_dev *indio_dev;
	int err;

	indio_dev = devm_iio_device_alloc(&client->dev, sizeof(*data));
	if (indio_dev == NULL)
		return -ENOMEM;

	data = iio_priv(indio_dev);
	i2c_set_clientdata(client, indio_dev);
	data->client = client;

	indio_dev->info = &adjd_s311_info;
	indio_dev->name = ADJD_S311_DRV_NAME;
	indio_dev->channels = adjd_s311_channels;
	indio_dev->num_channels = ARRAY_SIZE(adjd_s311_channels);
	indio_dev->modes = INDIO_DIRECT_MODE;

	err = iio_triggered_buffer_setup(indio_dev, NULL,
		adjd_s311_trigger_handler, NULL);
	if (err < 0)
		return err;

	err = iio_device_register(indio_dev);
	if (err)
		goto exit_unreg_buffer;

	dev_info(&client->dev, "ADJD-S311 color sensor registered\n");

	return 0;

exit_unreg_buffer:
	iio_triggered_buffer_cleanup(indio_dev);
	return err;
}

static int adjd_s311_remove(struct i2c_client *client)
{
	struct iio_dev *indio_dev = i2c_get_clientdata(client);
	struct adjd_s311_data *data = iio_priv(indio_dev);

	iio_device_unregister(indio_dev);
	iio_triggered_buffer_cleanup(indio_dev);
	kfree(data->buffer);

	return 0;
}

static const struct i2c_device_id adjd_s311_id[] = {
	{ "adjd_s311", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, adjd_s311_id);

static struct i2c_driver adjd_s311_driver = {
	.driver = {
		.name	= ADJD_S311_DRV_NAME,
	},
	.probe		= adjd_s311_probe,
	.remove		= adjd_s311_remove,
	.id_table	= adjd_s311_id,
};
module_i2c_driver(adjd_s311_driver);

MODULE_AUTHOR("Peter Meerwald <pmeerw@pmeerw.net>");
MODULE_DESCRIPTION("ADJD-S311 color sensor");
MODULE_LICENSE("GPL");

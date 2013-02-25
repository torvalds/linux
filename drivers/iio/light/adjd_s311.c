/*
 * adjd_s311.c - Support for ADJD-S311-CR999 digital color sensor
 *
 * Copyright (C) 2012 Peter Meerwald <pmeerw@pmeerw.net>
 *
 * This file is subject to the terms and conditions of version 2 of
 * the GNU General Public License.  See the file COPYING in the main
 * directory of this archive for more details.
 *
 * driver for ADJD-S311-CR999 digital color sensor (10-bit channels for
 * red, green, blue, clear); 7-bit I2C slave address 0x74
 *
 * limitations: no calibration, no offset mode, no sleep mode
 */

#include <linux/module.h>
#include <linux/init.h>
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
#define ADJD_S311_INT_RED_LO	0x0a
#define ADJD_S311_INT_RED_HI	0x0b
#define ADJD_S311_INT_GREEN_LO	0x0c
#define ADJD_S311_INT_GREEN_HI	0x0d
#define ADJD_S311_INT_BLUE_LO	0x0e
#define ADJD_S311_INT_BLUE_HI	0x0f
#define ADJD_S311_INT_CLEAR_LO	0x10
#define ADJD_S311_INT_CLEAR_HI	0x11
#define ADJD_S311_DATA_RED_LO	0x40
#define ADJD_S311_DATA_RED_HI	0x41
#define ADJD_S311_DATA_GREEN_LO	0x42
#define ADJD_S311_DATA_GREEN_HI	0x43
#define ADJD_S311_DATA_BLUE_LO	0x44
#define ADJD_S311_DATA_BLUE_HI	0x45
#define ADJD_S311_DATA_CLEAR_LO	0x46
#define ADJD_S311_DATA_CLEAR_HI	0x47
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

#define ADJD_S311_DATA_REG(chan) (ADJD_S311_DATA_RED_LO + (chan) * 2)
#define ADJD_S311_INT_REG(chan) (ADJD_S311_INT_RED_LO + (chan) * 2)
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

static ssize_t adjd_s311_read_int_time(struct iio_dev *indio_dev,
	uintptr_t private, const struct iio_chan_spec *chan, char *buf)
{
	struct adjd_s311_data *data = iio_priv(indio_dev);
	s32 ret;

	ret = i2c_smbus_read_word_data(data->client,
		ADJD_S311_INT_REG(chan->address));
	if (ret < 0)
		return ret;

	return sprintf(buf, "%d\n", ret & ADJD_S311_INT_MASK);
}

static ssize_t adjd_s311_write_int_time(struct iio_dev *indio_dev,
	 uintptr_t private, const struct iio_chan_spec *chan, const char *buf,
	 size_t len)
{
	struct adjd_s311_data *data = iio_priv(indio_dev);
	unsigned long int_time;
	int ret;

	ret = kstrtoul(buf, 10, &int_time);
	if (ret)
		return ret;

	if (int_time > ADJD_S311_INT_MASK)
		return -EINVAL;

	ret = i2c_smbus_write_word_data(data->client,
		ADJD_S311_INT_REG(chan->address), int_time);
	if (ret < 0)
		return ret;

	return len;
}

static irqreturn_t adjd_s311_trigger_handler(int irq, void *p)
{
	struct iio_poll_func *pf = p;
	struct iio_dev *indio_dev = pf->indio_dev;
	struct adjd_s311_data *data = iio_priv(indio_dev);
	s64 time_ns = iio_get_time_ns();
	int len = 0;
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
		len += 2;
	}

	if (indio_dev->scan_timestamp)
		*(s64 *)((u8 *)data->buffer + ALIGN(len, sizeof(s64)))
			= time_ns;
	iio_push_to_buffers(indio_dev, (u8 *)data->buffer);

done:
	iio_trigger_notify_done(indio_dev->trig);

	return IRQ_HANDLED;
}

static const struct iio_chan_spec_ext_info adjd_s311_ext_info[] = {
	{
		.name = "integration_time",
		.read = adjd_s311_read_int_time,
		.write = adjd_s311_write_int_time,
	},
	{ }
};

#define ADJD_S311_CHANNEL(_color, _scan_idx) { \
	.type = IIO_INTENSITY, \
	.modified = 1, \
	.address = (IDX_##_color), \
	.info_mask = IIO_CHAN_INFO_RAW_SEPARATE_BIT | \
		IIO_CHAN_INFO_HARDWAREGAIN_SEPARATE_BIT, \
	.channel2 = (IIO_MOD_LIGHT_##_color), \
	.scan_index = (_scan_idx), \
	.scan_type = IIO_ST('u', 10, 16, 0), \
	.ext_info = adjd_s311_ext_info, \
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
		ret = adjd_s311_read_data(indio_dev, chan->address, val);
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
	}
	return -EINVAL;
}

static int adjd_s311_write_raw(struct iio_dev *indio_dev,
			       struct iio_chan_spec const *chan,
			       int val, int val2, long mask)
{
	struct adjd_s311_data *data = iio_priv(indio_dev);
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_HARDWAREGAIN:
		if (val < 0 || val > ADJD_S311_CAP_MASK)
			return -EINVAL;

		ret = i2c_smbus_write_byte_data(data->client,
			ADJD_S311_CAP_REG(chan->address), val);
		return ret;
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
	.driver_module = THIS_MODULE,
};

static int __devinit adjd_s311_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct adjd_s311_data *data;
	struct iio_dev *indio_dev;
	int err;

	indio_dev = iio_device_alloc(sizeof(*data));
	if (indio_dev == NULL) {
		err = -ENOMEM;
		goto exit;
	}
	data = iio_priv(indio_dev);
	i2c_set_clientdata(client, indio_dev);
	data->client = client;

	indio_dev->dev.parent = &client->dev;
	indio_dev->info = &adjd_s311_info;
	indio_dev->name = ADJD_S311_DRV_NAME;
	indio_dev->channels = adjd_s311_channels;
	indio_dev->num_channels = ARRAY_SIZE(adjd_s311_channels);
	indio_dev->modes = INDIO_DIRECT_MODE;

	err = iio_triggered_buffer_setup(indio_dev, NULL,
		adjd_s311_trigger_handler, NULL);
	if (err < 0)
		goto exit_free_device;

	err = iio_device_register(indio_dev);
	if (err)
		goto exit_unreg_buffer;

	dev_info(&client->dev, "ADJD-S311 color sensor registered\n");

	return 0;

exit_unreg_buffer:
	iio_triggered_buffer_cleanup(indio_dev);
exit_free_device:
	iio_device_free(indio_dev);
exit:
	return err;
}

static int __devexit adjd_s311_remove(struct i2c_client *client)
{
	struct iio_dev *indio_dev = i2c_get_clientdata(client);
	struct adjd_s311_data *data = iio_priv(indio_dev);

	iio_device_unregister(indio_dev);
	iio_triggered_buffer_cleanup(indio_dev);
	kfree(data->buffer);
	iio_device_free(indio_dev);

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
	.remove		= __devexit_p(adjd_s311_remove),
	.id_table	= adjd_s311_id,
};
module_i2c_driver(adjd_s311_driver);

MODULE_AUTHOR("Peter Meerwald <pmeerw@pmeerw.net>");
MODULE_DESCRIPTION("ADJD-S311 color sensor");
MODULE_LICENSE("GPL");

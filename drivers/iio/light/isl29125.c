// SPDX-License-Identifier: GPL-2.0-only
/*
 * isl29125.c - Support for Intersil ISL29125 RGB light sensor
 *
 * Copyright (c) 2014 Peter Meerwald <pmeerw@pmeerw.net>
 *
 * RGB light sensor with 16-bit channels for red, green, blue);
 * 7-bit I2C slave address 0x44
 *
 * TODO: interrupt support, IR compensation, thresholds, 12bit
 */

#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/pm.h>

#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/iio/buffer.h>
#include <linux/iio/triggered_buffer.h>

#define ISL29125_DRV_NAME "isl29125"

#define ISL29125_DEVICE_ID 0x00
#define ISL29125_CONF1 0x01
#define ISL29125_CONF2 0x02
#define ISL29125_CONF3 0x03
#define ISL29125_STATUS 0x08
#define ISL29125_GREEN_DATA 0x09
#define ISL29125_RED_DATA 0x0b
#define ISL29125_BLUE_DATA 0x0d

#define ISL29125_ID 0x7d

#define ISL29125_MODE_MASK GENMASK(2, 0)
#define ISL29125_MODE_PD 0x0
#define ISL29125_MODE_G 0x1
#define ISL29125_MODE_R 0x2
#define ISL29125_MODE_B 0x3
#define ISL29125_MODE_RGB 0x5

#define ISL29125_SENSING_RANGE_0 5722   /* 375 lux full range */
#define ISL29125_SENSING_RANGE_1 152590 /* 10k lux full range */

#define ISL29125_MODE_RANGE BIT(3)

#define ISL29125_STATUS_CONV BIT(1)

struct isl29125_data {
	struct i2c_client *client;
	u8 conf1;
	/* Ensure timestamp is naturally aligned */
	struct {
		u16 chans[3];
		s64 timestamp __aligned(8);
	} scan;
};

#define ISL29125_CHANNEL(_color, _si) { \
	.type = IIO_INTENSITY, \
	.modified = 1, \
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW), \
	.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE), \
	.channel2 = IIO_MOD_LIGHT_##_color, \
	.scan_index = _si, \
	.scan_type = { \
		.sign = 'u', \
		.realbits = 16, \
		.storagebits = 16, \
		.endianness = IIO_CPU, \
	}, \
}

static const struct iio_chan_spec isl29125_channels[] = {
	ISL29125_CHANNEL(GREEN, 0),
	ISL29125_CHANNEL(RED, 1),
	ISL29125_CHANNEL(BLUE, 2),
	IIO_CHAN_SOFT_TIMESTAMP(3),
};

static const struct {
	u8 mode, data;
} isl29125_regs[] = {
	{ISL29125_MODE_G, ISL29125_GREEN_DATA},
	{ISL29125_MODE_R, ISL29125_RED_DATA},
	{ISL29125_MODE_B, ISL29125_BLUE_DATA},
};

static int isl29125_read_data(struct isl29125_data *data, int si)
{
	int tries = 5;
	int ret;

	ret = i2c_smbus_write_byte_data(data->client, ISL29125_CONF1,
		data->conf1 | isl29125_regs[si].mode);
	if (ret < 0)
		return ret;

	msleep(101);

	while (tries--) {
		ret = i2c_smbus_read_byte_data(data->client, ISL29125_STATUS);
		if (ret < 0)
			goto fail;
		if (ret & ISL29125_STATUS_CONV)
			break;
		msleep(20);
	}

	if (tries < 0) {
		dev_err(&data->client->dev, "data not ready\n");
		ret = -EIO;
		goto fail;
	}

	ret = i2c_smbus_read_word_data(data->client, isl29125_regs[si].data);

fail:
	i2c_smbus_write_byte_data(data->client, ISL29125_CONF1, data->conf1);
	return ret;
}

static int isl29125_read_raw(struct iio_dev *indio_dev,
			   struct iio_chan_spec const *chan,
			   int *val, int *val2, long mask)
{
	struct isl29125_data *data = iio_priv(indio_dev);
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		ret = iio_device_claim_direct_mode(indio_dev);
		if (ret)
			return ret;
		ret = isl29125_read_data(data, chan->scan_index);
		iio_device_release_direct_mode(indio_dev);
		if (ret < 0)
			return ret;
		*val = ret;
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		*val = 0;
		if (data->conf1 & ISL29125_MODE_RANGE)
			*val2 = ISL29125_SENSING_RANGE_1; /*10k lux full range*/
		else
			*val2 = ISL29125_SENSING_RANGE_0; /*375 lux full range*/
		return IIO_VAL_INT_PLUS_MICRO;
	}
	return -EINVAL;
}

static int isl29125_write_raw(struct iio_dev *indio_dev,
			       struct iio_chan_spec const *chan,
			       int val, int val2, long mask)
{
	struct isl29125_data *data = iio_priv(indio_dev);

	switch (mask) {
	case IIO_CHAN_INFO_SCALE:
		if (val != 0)
			return -EINVAL;
		if (val2 == ISL29125_SENSING_RANGE_1)
			data->conf1 |= ISL29125_MODE_RANGE;
		else if (val2 == ISL29125_SENSING_RANGE_0)
			data->conf1 &= ~ISL29125_MODE_RANGE;
		else
			return -EINVAL;
		return i2c_smbus_write_byte_data(data->client, ISL29125_CONF1,
			data->conf1);
	default:
		return -EINVAL;
	}
}

static irqreturn_t isl29125_trigger_handler(int irq, void *p)
{
	struct iio_poll_func *pf = p;
	struct iio_dev *indio_dev = pf->indio_dev;
	struct isl29125_data *data = iio_priv(indio_dev);
	int i, j = 0;

	for_each_set_bit(i, indio_dev->active_scan_mask,
		indio_dev->masklength) {
		int ret = i2c_smbus_read_word_data(data->client,
			isl29125_regs[i].data);
		if (ret < 0)
			goto done;

		data->scan.chans[j++] = ret;
	}

	iio_push_to_buffers_with_timestamp(indio_dev, &data->scan,
		iio_get_time_ns(indio_dev));

done:
	iio_trigger_notify_done(indio_dev->trig);

	return IRQ_HANDLED;
}

static IIO_CONST_ATTR(scale_available, "0.005722 0.152590");

static struct attribute *isl29125_attributes[] = {
	&iio_const_attr_scale_available.dev_attr.attr,
	NULL
};

static const struct attribute_group isl29125_attribute_group = {
	.attrs = isl29125_attributes,
};

static const struct iio_info isl29125_info = {
	.read_raw = isl29125_read_raw,
	.write_raw = isl29125_write_raw,
	.attrs = &isl29125_attribute_group,
};

static int isl29125_buffer_postenable(struct iio_dev *indio_dev)
{
	struct isl29125_data *data = iio_priv(indio_dev);

	data->conf1 |= ISL29125_MODE_RGB;
	return i2c_smbus_write_byte_data(data->client, ISL29125_CONF1,
		data->conf1);
}

static int isl29125_buffer_predisable(struct iio_dev *indio_dev)
{
	struct isl29125_data *data = iio_priv(indio_dev);

	data->conf1 &= ~ISL29125_MODE_MASK;
	data->conf1 |= ISL29125_MODE_PD;
	return i2c_smbus_write_byte_data(data->client, ISL29125_CONF1,
		data->conf1);
}

static const struct iio_buffer_setup_ops isl29125_buffer_setup_ops = {
	.postenable = isl29125_buffer_postenable,
	.predisable = isl29125_buffer_predisable,
};

static int isl29125_probe(struct i2c_client *client)
{
	struct isl29125_data *data;
	struct iio_dev *indio_dev;
	int ret;

	indio_dev = devm_iio_device_alloc(&client->dev, sizeof(*data));
	if (indio_dev == NULL)
		return -ENOMEM;

	data = iio_priv(indio_dev);
	i2c_set_clientdata(client, indio_dev);
	data->client = client;

	indio_dev->info = &isl29125_info;
	indio_dev->name = ISL29125_DRV_NAME;
	indio_dev->channels = isl29125_channels;
	indio_dev->num_channels = ARRAY_SIZE(isl29125_channels);
	indio_dev->modes = INDIO_DIRECT_MODE;

	ret = i2c_smbus_read_byte_data(data->client, ISL29125_DEVICE_ID);
	if (ret < 0)
		return ret;
	if (ret != ISL29125_ID)
		return -ENODEV;

	data->conf1 = ISL29125_MODE_PD | ISL29125_MODE_RANGE;
	ret = i2c_smbus_write_byte_data(data->client, ISL29125_CONF1,
		data->conf1);
	if (ret < 0)
		return ret;

	ret = i2c_smbus_write_byte_data(data->client, ISL29125_STATUS, 0);
	if (ret < 0)
		return ret;

	ret = iio_triggered_buffer_setup(indio_dev, NULL,
		isl29125_trigger_handler, &isl29125_buffer_setup_ops);
	if (ret < 0)
		return ret;

	ret = iio_device_register(indio_dev);
	if (ret < 0)
		goto buffer_cleanup;

	return 0;

buffer_cleanup:
	iio_triggered_buffer_cleanup(indio_dev);
	return ret;
}

static int isl29125_powerdown(struct isl29125_data *data)
{
	return i2c_smbus_write_byte_data(data->client, ISL29125_CONF1,
		(data->conf1 & ~ISL29125_MODE_MASK) | ISL29125_MODE_PD);
}

static void isl29125_remove(struct i2c_client *client)
{
	struct iio_dev *indio_dev = i2c_get_clientdata(client);

	iio_device_unregister(indio_dev);
	iio_triggered_buffer_cleanup(indio_dev);
	isl29125_powerdown(iio_priv(indio_dev));
}

static int isl29125_suspend(struct device *dev)
{
	struct isl29125_data *data = iio_priv(i2c_get_clientdata(
		to_i2c_client(dev)));
	return isl29125_powerdown(data);
}

static int isl29125_resume(struct device *dev)
{
	struct isl29125_data *data = iio_priv(i2c_get_clientdata(
		to_i2c_client(dev)));
	return i2c_smbus_write_byte_data(data->client, ISL29125_CONF1,
		data->conf1);
}

static DEFINE_SIMPLE_DEV_PM_OPS(isl29125_pm_ops, isl29125_suspend,
				isl29125_resume);

static const struct i2c_device_id isl29125_id[] = {
	{ "isl29125" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, isl29125_id);

static struct i2c_driver isl29125_driver = {
	.driver = {
		.name	= ISL29125_DRV_NAME,
		.pm	= pm_sleep_ptr(&isl29125_pm_ops),
	},
	.probe		= isl29125_probe,
	.remove		= isl29125_remove,
	.id_table	= isl29125_id,
};
module_i2c_driver(isl29125_driver);

MODULE_AUTHOR("Peter Meerwald <pmeerw@pmeerw.net>");
MODULE_DESCRIPTION("ISL29125 RGB light sensor driver");
MODULE_LICENSE("GPL");

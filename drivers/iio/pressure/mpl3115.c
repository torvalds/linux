// SPDX-License-Identifier: GPL-2.0-only
/*
 * mpl3115.c - Support for Freescale MPL3115A2 pressure/temperature sensor
 *
 * Copyright (c) 2013 Peter Meerwald <pmeerw@pmeerw.net>
 *
 * (7-bit I2C slave address 0x60)
 *
 * TODO: FIFO buffer, altimeter mode, oversampling, continuous mode,
 * interrupts, user offset correction, raw mode
 */

#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/iio/buffer.h>
#include <linux/iio/triggered_buffer.h>
#include <linux/delay.h>

#define MPL3115_STATUS 0x00
#define MPL3115_OUT_PRESS 0x01 /* MSB first, 20 bit */
#define MPL3115_OUT_TEMP 0x04 /* MSB first, 12 bit */
#define MPL3115_WHO_AM_I 0x0c
#define MPL3115_CTRL_REG1 0x26

#define MPL3115_DEVICE_ID 0xc4

#define MPL3115_STATUS_PRESS_RDY BIT(2)
#define MPL3115_STATUS_TEMP_RDY BIT(1)

#define MPL3115_CTRL_RESET BIT(2) /* software reset */
#define MPL3115_CTRL_OST BIT(1) /* initiate measurement */
#define MPL3115_CTRL_ACTIVE BIT(0) /* continuous measurement */
#define MPL3115_CTRL_OS_258MS (BIT(5) | BIT(4)) /* 64x oversampling */

struct mpl3115_data {
	struct i2c_client *client;
	struct mutex lock;
	u8 ctrl_reg1;
};

static int mpl3115_request(struct mpl3115_data *data)
{
	int ret, tries = 15;

	/* trigger measurement */
	ret = i2c_smbus_write_byte_data(data->client, MPL3115_CTRL_REG1,
		data->ctrl_reg1 | MPL3115_CTRL_OST);
	if (ret < 0)
		return ret;

	while (tries-- > 0) {
		ret = i2c_smbus_read_byte_data(data->client, MPL3115_CTRL_REG1);
		if (ret < 0)
			return ret;
		/* wait for data ready, i.e. OST cleared */
		if (!(ret & MPL3115_CTRL_OST))
			break;
		msleep(20);
	}

	if (tries < 0) {
		dev_err(&data->client->dev, "data not ready\n");
		return -EIO;
	}

	return 0;
}

static int mpl3115_read_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan,
			    int *val, int *val2, long mask)
{
	struct mpl3115_data *data = iio_priv(indio_dev);
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		ret = iio_device_claim_direct_mode(indio_dev);
		if (ret)
			return ret;

		switch (chan->type) {
		case IIO_PRESSURE: { /* in 0.25 pascal / LSB */
			__be32 tmp = 0;

			mutex_lock(&data->lock);
			ret = mpl3115_request(data);
			if (ret < 0) {
				mutex_unlock(&data->lock);
				break;
			}
			ret = i2c_smbus_read_i2c_block_data(data->client,
				MPL3115_OUT_PRESS, 3, (u8 *) &tmp);
			mutex_unlock(&data->lock);
			if (ret < 0)
				break;
			*val = be32_to_cpu(tmp) >> chan->scan_type.shift;
			ret = IIO_VAL_INT;
			break;
		}
		case IIO_TEMP: { /* in 0.0625 celsius / LSB */
			__be16 tmp;

			mutex_lock(&data->lock);
			ret = mpl3115_request(data);
			if (ret < 0) {
				mutex_unlock(&data->lock);
				break;
			}
			ret = i2c_smbus_read_i2c_block_data(data->client,
				MPL3115_OUT_TEMP, 2, (u8 *) &tmp);
			mutex_unlock(&data->lock);
			if (ret < 0)
				break;
			*val = sign_extend32(be16_to_cpu(tmp) >> chan->scan_type.shift,
					     chan->scan_type.realbits - 1);
			ret = IIO_VAL_INT;
			break;
		}
		default:
			ret = -EINVAL;
			break;
		}

		iio_device_release_direct_mode(indio_dev);
		return ret;

	case IIO_CHAN_INFO_SCALE:
		switch (chan->type) {
		case IIO_PRESSURE:
			*val = 0;
			*val2 = 250; /* want kilopascal */
			return IIO_VAL_INT_PLUS_MICRO;
		case IIO_TEMP:
			*val = 0;
			*val2 = 62500;
			return IIO_VAL_INT_PLUS_MICRO;
		default:
			return -EINVAL;
		}
	}
	return -EINVAL;
}

static irqreturn_t mpl3115_trigger_handler(int irq, void *p)
{
	struct iio_poll_func *pf = p;
	struct iio_dev *indio_dev = pf->indio_dev;
	struct mpl3115_data *data = iio_priv(indio_dev);
	/*
	 * 32-bit channel + 16-bit channel + padding + ts
	 * Note that it is possible for only one of the first 2
	 * channels to be enabled. If that happens, the first element
	 * of the buffer may be either 16 or 32-bits.  As such we cannot
	 * use a simple structure definition to express this data layout.
	 */
	u8 buffer[16] __aligned(8);
	int ret, pos = 0;

	mutex_lock(&data->lock);
	ret = mpl3115_request(data);
	if (ret < 0) {
		mutex_unlock(&data->lock);
		goto done;
	}

	memset(buffer, 0, sizeof(buffer));
	if (test_bit(0, indio_dev->active_scan_mask)) {
		ret = i2c_smbus_read_i2c_block_data(data->client,
			MPL3115_OUT_PRESS, 3, &buffer[pos]);
		if (ret < 0) {
			mutex_unlock(&data->lock);
			goto done;
		}
		pos += 4;
	}

	if (test_bit(1, indio_dev->active_scan_mask)) {
		ret = i2c_smbus_read_i2c_block_data(data->client,
			MPL3115_OUT_TEMP, 2, &buffer[pos]);
		if (ret < 0) {
			mutex_unlock(&data->lock);
			goto done;
		}
	}
	mutex_unlock(&data->lock);

	iio_push_to_buffers_with_timestamp(indio_dev, buffer,
		iio_get_time_ns(indio_dev));

done:
	iio_trigger_notify_done(indio_dev->trig);
	return IRQ_HANDLED;
}

static const struct iio_chan_spec mpl3115_channels[] = {
	{
		.type = IIO_PRESSURE,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE),
		.scan_index = 0,
		.scan_type = {
			.sign = 'u',
			.realbits = 20,
			.storagebits = 32,
			.shift = 12,
			.endianness = IIO_BE,
		}
	},
	{
		.type = IIO_TEMP,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE),
		.scan_index = 1,
		.scan_type = {
			.sign = 's',
			.realbits = 12,
			.storagebits = 16,
			.shift = 4,
			.endianness = IIO_BE,
		}
	},
	IIO_CHAN_SOFT_TIMESTAMP(2),
};

static const struct iio_info mpl3115_info = {
	.read_raw = &mpl3115_read_raw,
};

static int mpl3115_probe(struct i2c_client *client)
{
	const struct i2c_device_id *id = i2c_client_get_device_id(client);
	struct mpl3115_data *data;
	struct iio_dev *indio_dev;
	int ret;

	ret = i2c_smbus_read_byte_data(client, MPL3115_WHO_AM_I);
	if (ret < 0)
		return ret;
	if (ret != MPL3115_DEVICE_ID)
		return -ENODEV;

	indio_dev = devm_iio_device_alloc(&client->dev, sizeof(*data));
	if (!indio_dev)
		return -ENOMEM;

	data = iio_priv(indio_dev);
	data->client = client;
	mutex_init(&data->lock);

	i2c_set_clientdata(client, indio_dev);
	indio_dev->info = &mpl3115_info;
	indio_dev->name = id->name;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = mpl3115_channels;
	indio_dev->num_channels = ARRAY_SIZE(mpl3115_channels);

	/* software reset, I2C transfer is aborted (fails) */
	i2c_smbus_write_byte_data(client, MPL3115_CTRL_REG1,
		MPL3115_CTRL_RESET);
	msleep(50);

	data->ctrl_reg1 = MPL3115_CTRL_OS_258MS;
	ret = i2c_smbus_write_byte_data(client, MPL3115_CTRL_REG1,
		data->ctrl_reg1);
	if (ret < 0)
		return ret;

	ret = iio_triggered_buffer_setup(indio_dev, NULL,
		mpl3115_trigger_handler, NULL);
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

static int mpl3115_standby(struct mpl3115_data *data)
{
	return i2c_smbus_write_byte_data(data->client, MPL3115_CTRL_REG1,
		data->ctrl_reg1 & ~MPL3115_CTRL_ACTIVE);
}

static void mpl3115_remove(struct i2c_client *client)
{
	struct iio_dev *indio_dev = i2c_get_clientdata(client);

	iio_device_unregister(indio_dev);
	iio_triggered_buffer_cleanup(indio_dev);
	mpl3115_standby(iio_priv(indio_dev));
}

static int mpl3115_suspend(struct device *dev)
{
	return mpl3115_standby(iio_priv(i2c_get_clientdata(
		to_i2c_client(dev))));
}

static int mpl3115_resume(struct device *dev)
{
	struct mpl3115_data *data = iio_priv(i2c_get_clientdata(
		to_i2c_client(dev)));

	return i2c_smbus_write_byte_data(data->client, MPL3115_CTRL_REG1,
		data->ctrl_reg1);
}

static DEFINE_SIMPLE_DEV_PM_OPS(mpl3115_pm_ops, mpl3115_suspend,
				mpl3115_resume);

static const struct i2c_device_id mpl3115_id[] = {
	{ "mpl3115", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, mpl3115_id);

static const struct of_device_id mpl3115_of_match[] = {
	{ .compatible = "fsl,mpl3115" },
	{ }
};
MODULE_DEVICE_TABLE(of, mpl3115_of_match);

static struct i2c_driver mpl3115_driver = {
	.driver = {
		.name	= "mpl3115",
		.of_match_table = mpl3115_of_match,
		.pm	= pm_sleep_ptr(&mpl3115_pm_ops),
	},
	.probe = mpl3115_probe,
	.remove = mpl3115_remove,
	.id_table = mpl3115_id,
};
module_i2c_driver(mpl3115_driver);

MODULE_AUTHOR("Peter Meerwald <pmeerw@pmeerw.net>");
MODULE_DESCRIPTION("Freescale MPL3115 pressure/temperature driver");
MODULE_LICENSE("GPL");

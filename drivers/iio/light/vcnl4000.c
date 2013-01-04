/*
 * vcnl4000.c - Support for Vishay VCNL4000 combined ambient light and
 * proximity sensor
 *
 * Copyright 2012 Peter Meerwald <pmeerw@pmeerw.net>
 *
 * This file is subject to the terms and conditions of version 2 of
 * the GNU General Public License.  See the file COPYING in the main
 * directory of this archive for more details.
 *
 * IIO driver for VCNL4000 (7-bit I2C slave address 0x13)
 *
 * TODO:
 *   allow to adjust IR current
 *   proximity threshold and event handling
 */

#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/err.h>
#include <linux/delay.h>

#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>

#define VCNL4000_DRV_NAME "vcnl4000"

#define VCNL4000_COMMAND	0x80 /* Command register */
#define VCNL4000_PROD_REV	0x81 /* Product ID and Revision ID */
#define VCNL4000_LED_CURRENT	0x83 /* IR LED current for proximity mode */
#define VCNL4000_AL_PARAM	0x84 /* Ambient light parameter register */
#define VCNL4000_AL_RESULT_HI	0x85 /* Ambient light result register, MSB */
#define VCNL4000_AL_RESULT_LO	0x86 /* Ambient light result register, LSB */
#define VCNL4000_PS_RESULT_HI	0x87 /* Proximity result register, MSB */
#define VCNL4000_PS_RESULT_LO	0x88 /* Proximity result register, LSB */
#define VCNL4000_PS_MEAS_FREQ	0x89 /* Proximity test signal frequency */
#define VCNL4000_PS_MOD_ADJ	0x8a /* Proximity modulator timing adjustment */

/* Bit masks for COMMAND register */
#define VCNL4000_AL_RDY		0x40 /* ALS data ready? */
#define VCNL4000_PS_RDY		0x20 /* proximity data ready? */
#define VCNL4000_AL_OD		0x10 /* start on-demand ALS measurement */
#define VCNL4000_PS_OD		0x08 /* start on-demand proximity measurement */

struct vcnl4000_data {
	struct i2c_client *client;
};

static const struct i2c_device_id vcnl4000_id[] = {
	{ "vcnl4000", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, vcnl4000_id);

static int vcnl4000_measure(struct vcnl4000_data *data, u8 req_mask,
				u8 rdy_mask, u8 data_reg, int *val)
{
	int tries = 20;
	u16 buf;
	int ret;

	ret = i2c_smbus_write_byte_data(data->client, VCNL4000_COMMAND,
					req_mask);
	if (ret < 0)
		return ret;

	/* wait for data to become ready */
	while (tries--) {
		ret = i2c_smbus_read_byte_data(data->client, VCNL4000_COMMAND);
		if (ret < 0)
			return ret;
		if (ret & rdy_mask)
			break;
		msleep(20); /* measurement takes up to 100 ms */
	}

	if (tries < 0) {
		dev_err(&data->client->dev,
			"vcnl4000_measure() failed, data not ready\n");
		return -EIO;
	}

	ret = i2c_smbus_read_i2c_block_data(data->client,
		data_reg, sizeof(buf), (u8 *) &buf);
	if (ret < 0)
		return ret;

	*val = be16_to_cpu(buf);

	return 0;
}

static const struct iio_chan_spec vcnl4000_channels[] = {
	{
		.type = IIO_LIGHT,
		.info_mask = IIO_CHAN_INFO_RAW_SEPARATE_BIT |
			IIO_CHAN_INFO_SCALE_SEPARATE_BIT,
	}, {
		.type = IIO_PROXIMITY,
		.info_mask = IIO_CHAN_INFO_RAW_SEPARATE_BIT,
	}
};

static int vcnl4000_read_raw(struct iio_dev *indio_dev,
				struct iio_chan_spec const *chan,
				int *val, int *val2, long mask)
{
	int ret = -EINVAL;
	struct vcnl4000_data *data = iio_priv(indio_dev);

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		switch (chan->type) {
		case IIO_LIGHT:
			ret = vcnl4000_measure(data,
				VCNL4000_AL_OD, VCNL4000_AL_RDY,
				VCNL4000_AL_RESULT_HI, val);
			if (ret < 0)
				return ret;
			ret = IIO_VAL_INT;
			break;
		case IIO_PROXIMITY:
			ret = vcnl4000_measure(data,
				VCNL4000_PS_OD, VCNL4000_PS_RDY,
				VCNL4000_PS_RESULT_HI, val);
			if (ret < 0)
				return ret;
			ret = IIO_VAL_INT;
			break;
		default:
			break;
		}
		break;
	case IIO_CHAN_INFO_SCALE:
		if (chan->type == IIO_LIGHT) {
			*val = 0;
			*val2 = 250000;
			ret = IIO_VAL_INT_PLUS_MICRO;
		}
		break;
	default:
		break;
	}

	return ret;
}

static const struct iio_info vcnl4000_info = {
	.read_raw = vcnl4000_read_raw,
	.driver_module = THIS_MODULE,
};

static int vcnl4000_probe(struct i2c_client *client,
			  const struct i2c_device_id *id)
{
	struct vcnl4000_data *data;
	struct iio_dev *indio_dev;
	int ret;

	indio_dev = iio_device_alloc(sizeof(*data));
	if (!indio_dev)
		return -ENOMEM;

	data = iio_priv(indio_dev);
	i2c_set_clientdata(client, indio_dev);
	data->client = client;

	ret = i2c_smbus_read_byte_data(data->client, VCNL4000_PROD_REV);
	if (ret < 0)
		goto error_free_dev;

	dev_info(&client->dev, "VCNL4000 Ambient light/proximity sensor, Prod %02x, Rev: %02x\n",
		ret >> 4, ret & 0xf);

	indio_dev->dev.parent = &client->dev;
	indio_dev->info = &vcnl4000_info;
	indio_dev->channels = vcnl4000_channels;
	indio_dev->num_channels = ARRAY_SIZE(vcnl4000_channels);
	indio_dev->name = VCNL4000_DRV_NAME;
	indio_dev->modes = INDIO_DIRECT_MODE;

	ret = iio_device_register(indio_dev);
	if (ret < 0)
		goto error_free_dev;

	return 0;

error_free_dev:
	iio_device_free(indio_dev);
	return ret;
}

static int vcnl4000_remove(struct i2c_client *client)
{
	struct iio_dev *indio_dev = i2c_get_clientdata(client);

	iio_device_unregister(indio_dev);
	iio_device_free(indio_dev);

	return 0;
}

static struct i2c_driver vcnl4000_driver = {
	.driver = {
		.name   = VCNL4000_DRV_NAME,
		.owner  = THIS_MODULE,
	},
	.probe  = vcnl4000_probe,
	.remove = vcnl4000_remove,
	.id_table = vcnl4000_id,
};

module_i2c_driver(vcnl4000_driver);

MODULE_AUTHOR("Peter Meerwald <pmeerw@pmeerw.net>");
MODULE_DESCRIPTION("Vishay VCNL4000 proximity/ambient light sensor driver");
MODULE_LICENSE("GPL");

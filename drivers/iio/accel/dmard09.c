// SPDX-License-Identifier: GPL-2.0-only
/*
 * IIO driver for the 3-axis accelerometer Domintech DMARD09.
 *
 * Copyright (c) 2016, Jelle van der Waa <jelle@vdwaa.nl>
 */

#include <asm/unaligned.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/iio/iio.h>

#define DMARD09_DRV_NAME	"dmard09"

#define DMARD09_REG_CHIPID      0x18
#define DMARD09_REG_STAT	0x0A
#define DMARD09_REG_X		0x0C
#define DMARD09_REG_Y		0x0E
#define DMARD09_REG_Z		0x10
#define DMARD09_CHIPID		0x95

#define DMARD09_BUF_LEN 8
#define DMARD09_AXIS_X 0
#define DMARD09_AXIS_Y 1
#define DMARD09_AXIS_Z 2
#define DMARD09_AXIS_X_OFFSET ((DMARD09_AXIS_X + 1) * 2)
#define DMARD09_AXIS_Y_OFFSET ((DMARD09_AXIS_Y + 1 )* 2)
#define DMARD09_AXIS_Z_OFFSET ((DMARD09_AXIS_Z + 1) * 2)

struct dmard09_data {
	struct i2c_client *client;
};

#define DMARD09_CHANNEL(_axis, offset) {			\
	.type = IIO_ACCEL,					\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),		\
	.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE),	\
	.modified = 1,						\
	.address = offset,					\
	.channel2 = IIO_MOD_##_axis,				\
}

static const struct iio_chan_spec dmard09_channels[] = {
	DMARD09_CHANNEL(X, DMARD09_AXIS_X_OFFSET),
	DMARD09_CHANNEL(Y, DMARD09_AXIS_Y_OFFSET),
	DMARD09_CHANNEL(Z, DMARD09_AXIS_Z_OFFSET),
};

static int dmard09_read_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan,
			    int *val, int *val2, long mask)
{
	struct dmard09_data *data = iio_priv(indio_dev);
	u8 buf[DMARD09_BUF_LEN];
	int ret;
	s16 accel;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		/*
		 * Read from the DMAR09_REG_STAT register, since the chip
		 * caches reads from the individual X, Y, Z registers.
		 */
		ret = i2c_smbus_read_i2c_block_data(data->client,
						    DMARD09_REG_STAT,
						    DMARD09_BUF_LEN, buf);
		if (ret < 0) {
			dev_err(&data->client->dev, "Error reading reg %d\n",
				DMARD09_REG_STAT);
			return ret;
		}

		accel = get_unaligned_le16(&buf[chan->address]);

		/* Remove lower 3 bits and sign extend */
		accel <<= 4;
		accel >>= 7;

		*val = accel;

		return IIO_VAL_INT;
	default:
		return -EINVAL;
	}
}

static const struct iio_info dmard09_info = {
	.read_raw	= dmard09_read_raw,
};

static int dmard09_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	int ret;
	struct iio_dev *indio_dev;
	struct dmard09_data *data;

	indio_dev = devm_iio_device_alloc(&client->dev, sizeof(*data));
	if (!indio_dev) {
		dev_err(&client->dev, "iio allocation failed\n");
		return -ENOMEM;
	}

	data = iio_priv(indio_dev);
	data->client = client;

	ret = i2c_smbus_read_byte_data(data->client, DMARD09_REG_CHIPID);
	if (ret < 0) {
		dev_err(&client->dev, "Error reading chip id %d\n", ret);
		return ret;
	}

	if (ret != DMARD09_CHIPID) {
		dev_err(&client->dev, "Invalid chip id %d\n", ret);
		return -ENODEV;
	}

	i2c_set_clientdata(client, indio_dev);
	indio_dev->name = DMARD09_DRV_NAME;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = dmard09_channels;
	indio_dev->num_channels = ARRAY_SIZE(dmard09_channels);
	indio_dev->info = &dmard09_info;

	return devm_iio_device_register(&client->dev, indio_dev);
}

static const struct i2c_device_id dmard09_id[] = {
	{ "dmard09", 0},
	{ },
};

MODULE_DEVICE_TABLE(i2c, dmard09_id);

static struct i2c_driver dmard09_driver = {
	.driver = {
		.name = DMARD09_DRV_NAME
	},
	.probe = dmard09_probe,
	.id_table = dmard09_id,
};

module_i2c_driver(dmard09_driver);

MODULE_AUTHOR("Jelle van der Waa <jelle@vdwaa.nl>");
MODULE_DESCRIPTION("DMARD09 3-axis accelerometer driver");
MODULE_LICENSE("GPL");

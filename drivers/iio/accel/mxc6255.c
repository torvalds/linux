// SPDX-License-Identifier: GPL-2.0-only
/*
 * MXC6255 - MEMSIC orientation sensing accelerometer
 *
 * Copyright (c) 2015, Intel Corporation.
 *
 * IIO driver for MXC6255 (7-bit I2C slave address 0x15).
 */

#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/iio/iio.h>
#include <linux/delay.h>
#include <linux/acpi.h>
#include <linux/regmap.h>
#include <linux/iio/sysfs.h>

#define MXC6255_DRV_NAME		"mxc6255"
#define MXC6255_REGMAP_NAME		"mxc6255_regmap"

#define MXC6255_REG_XOUT		0x00
#define MXC6255_REG_YOUT		0x01
#define MXC6255_REG_CHIP_ID		0x08

#define MXC6255_CHIP_ID			0x05

/*
 * MXC6255 has only one measurement range: +/- 2G.
 * The acceleration output is an 8-bit value.
 *
 * Scale is calculated as follows:
 * (2 + 2) * 9.80665 / (2^8 - 1) = 0.153829
 *
 * Scale value for +/- 2G measurement range
 */
#define MXC6255_SCALE			153829

enum mxc6255_axis {
	AXIS_X,
	AXIS_Y,
};

struct mxc6255_data {
	struct i2c_client *client;
	struct regmap *regmap;
};

static int mxc6255_read_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan,
			    int *val, int *val2, long mask)
{
	struct mxc6255_data *data = iio_priv(indio_dev);
	unsigned int reg;
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		ret = regmap_read(data->regmap, chan->address, &reg);
		if (ret < 0) {
			dev_err(&data->client->dev,
				"Error reading reg %lu\n", chan->address);
			return ret;
		}

		*val = sign_extend32(reg, 7);
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		*val = 0;
		*val2 = MXC6255_SCALE;
		return IIO_VAL_INT_PLUS_MICRO;
	default:
		return -EINVAL;
	}
}

static const struct iio_info mxc6255_info = {
	.read_raw	= mxc6255_read_raw,
};

#define MXC6255_CHANNEL(_axis, reg) {				\
	.type = IIO_ACCEL,					\
	.modified = 1,						\
	.channel2 = IIO_MOD_##_axis,				\
	.address = reg,						\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),		\
	.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE),	\
}

static const struct iio_chan_spec mxc6255_channels[] = {
	MXC6255_CHANNEL(X, MXC6255_REG_XOUT),
	MXC6255_CHANNEL(Y, MXC6255_REG_YOUT),
};

static bool mxc6255_is_readable_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case MXC6255_REG_XOUT:
	case MXC6255_REG_YOUT:
	case MXC6255_REG_CHIP_ID:
		return true;
	default:
		return false;
	}
}

static const struct regmap_config mxc6255_regmap_config = {
	.name = MXC6255_REGMAP_NAME,

	.reg_bits = 8,
	.val_bits = 8,

	.readable_reg = mxc6255_is_readable_reg,
};

static int mxc6255_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct mxc6255_data *data;
	struct iio_dev *indio_dev;
	struct regmap *regmap;
	unsigned int chip_id;
	int ret;

	indio_dev = devm_iio_device_alloc(&client->dev, sizeof(*data));
	if (!indio_dev)
		return -ENOMEM;

	regmap = devm_regmap_init_i2c(client, &mxc6255_regmap_config);
	if (IS_ERR(regmap)) {
		dev_err(&client->dev, "Error initializing regmap\n");
		return PTR_ERR(regmap);
	}

	data = iio_priv(indio_dev);
	i2c_set_clientdata(client, indio_dev);
	data->client = client;
	data->regmap = regmap;

	indio_dev->name = MXC6255_DRV_NAME;
	indio_dev->dev.parent = &client->dev;
	indio_dev->channels = mxc6255_channels;
	indio_dev->num_channels = ARRAY_SIZE(mxc6255_channels);
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->info = &mxc6255_info;

	ret = regmap_read(data->regmap, MXC6255_REG_CHIP_ID, &chip_id);
	if (ret < 0) {
		dev_err(&client->dev, "Error reading chip id %d\n", ret);
		return ret;
	}

	if ((chip_id & 0x1f) != MXC6255_CHIP_ID) {
		dev_err(&client->dev, "Invalid chip id %x\n", chip_id);
		return -ENODEV;
	}

	dev_dbg(&client->dev, "Chip id %x\n", chip_id);

	ret = devm_iio_device_register(&client->dev, indio_dev);
	if (ret < 0) {
		dev_err(&client->dev, "Could not register IIO device\n");
		return ret;
	}

	return 0;
}

static const struct acpi_device_id mxc6255_acpi_match[] = {
	{"MXC6225",	0},
	{"MXC6255",	0},
	{ }
};
MODULE_DEVICE_TABLE(acpi, mxc6255_acpi_match);

static const struct i2c_device_id mxc6255_id[] = {
	{"mxc6225",	0},
	{"mxc6255",	0},
	{ }
};
MODULE_DEVICE_TABLE(i2c, mxc6255_id);

static struct i2c_driver mxc6255_driver = {
	.driver = {
		.name = MXC6255_DRV_NAME,
		.acpi_match_table = ACPI_PTR(mxc6255_acpi_match),
	},
	.probe		= mxc6255_probe,
	.id_table	= mxc6255_id,
};

module_i2c_driver(mxc6255_driver);

MODULE_AUTHOR("Teodora Baluta <teodora.baluta@intel.com>");
MODULE_DESCRIPTION("MEMSIC MXC6255 orientation sensing accelerometer driver");
MODULE_LICENSE("GPL v2");

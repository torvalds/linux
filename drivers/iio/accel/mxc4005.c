/*
 * 3-axis accelerometer driver for MXC4005XC Memsic sensor
 *
 * Copyright (c) 2014, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/iio/iio.h>
#include <linux/acpi.h>
#include <linux/regmap.h>
#include <linux/iio/sysfs.h>

#define MXC4005_DRV_NAME		"mxc4005"
#define MXC4005_REGMAP_NAME		"mxc4005_regmap"

#define MXC4005_REG_XOUT_UPPER		0x03
#define MXC4005_REG_XOUT_LOWER		0x04
#define MXC4005_REG_YOUT_UPPER		0x05
#define MXC4005_REG_YOUT_LOWER		0x06
#define MXC4005_REG_ZOUT_UPPER		0x07
#define MXC4005_REG_ZOUT_LOWER		0x08

#define MXC4005_REG_CONTROL		0x0D
#define MXC4005_REG_CONTROL_MASK_FSR	GENMASK(6, 5)
#define MXC4005_CONTROL_FSR_SHIFT	5

#define MXC4005_REG_DEVICE_ID		0x0E

enum mxc4005_axis {
	AXIS_X,
	AXIS_Y,
	AXIS_Z,
};

enum mxc4005_range {
	MXC4005_RANGE_2G,
	MXC4005_RANGE_4G,
	MXC4005_RANGE_8G,
};

struct mxc4005_data {
	struct device *dev;
	struct mutex mutex;
	struct regmap *regmap;
};

/*
 * MXC4005 can operate in the following ranges:
 * +/- 2G, 4G, 8G (the default +/-2G)
 *
 * (2 + 2) * 9.81 / (2^12 - 1) = 0.009582
 * (4 + 4) * 9.81 / (2^12 - 1) = 0.019164
 * (8 + 8) * 9.81 / (2^12 - 1) = 0.038329
 */
static const struct {
	u8 range;
	int scale;
} mxc4005_scale_table[] = {
	{MXC4005_RANGE_2G, 9582},
	{MXC4005_RANGE_4G, 19164},
	{MXC4005_RANGE_8G, 38329},
};


static IIO_CONST_ATTR(in_accel_scale_available, "0.009582 0.019164 0.038329");

static struct attribute *mxc4005_attributes[] = {
	&iio_const_attr_in_accel_scale_available.dev_attr.attr,
	NULL,
};

static const struct attribute_group mxc4005_attrs_group = {
	.attrs = mxc4005_attributes,
};

static bool mxc4005_is_readable_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case MXC4005_REG_XOUT_UPPER:
	case MXC4005_REG_XOUT_LOWER:
	case MXC4005_REG_YOUT_UPPER:
	case MXC4005_REG_YOUT_LOWER:
	case MXC4005_REG_ZOUT_UPPER:
	case MXC4005_REG_ZOUT_LOWER:
	case MXC4005_REG_DEVICE_ID:
	case MXC4005_REG_CONTROL:
		return true;
	default:
		return false;
	}
}

static bool mxc4005_is_writeable_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case MXC4005_REG_CONTROL:
		return true;
	default:
		return false;
	}
}

static const struct regmap_config mxc4005_regmap_config = {
	.name = MXC4005_REGMAP_NAME,

	.reg_bits = 8,
	.val_bits = 8,

	.max_register = MXC4005_REG_DEVICE_ID,

	.readable_reg = mxc4005_is_readable_reg,
	.writeable_reg = mxc4005_is_writeable_reg,
};

static int mxc4005_read_axis(struct mxc4005_data *data,
			     unsigned int addr)
{
	__be16 reg;
	int ret;

	ret = regmap_bulk_read(data->regmap, addr, (u8 *) &reg, sizeof(reg));
	if (ret < 0) {
		dev_err(data->dev, "failed to read reg %02x\n", addr);
		return ret;
	}

	return be16_to_cpu(reg);
}

static int mxc4005_read_scale(struct mxc4005_data *data)
{
	unsigned int reg;
	int ret;
	int i;

	ret = regmap_read(data->regmap, MXC4005_REG_CONTROL, &reg);
	if (ret < 0) {
		dev_err(data->dev, "failed to read reg_control\n");
		return ret;
	}

	i = reg >> MXC4005_CONTROL_FSR_SHIFT;

	if (i < 0 || i >= ARRAY_SIZE(mxc4005_scale_table))
		return -EINVAL;

	return mxc4005_scale_table[i].scale;
}

static int mxc4005_set_scale(struct mxc4005_data *data, int val)
{
	unsigned int reg;
	int i;
	int ret;

	for (i = 0; i < ARRAY_SIZE(mxc4005_scale_table); i++) {
		if (mxc4005_scale_table[i].scale == val) {
			reg = i << MXC4005_CONTROL_FSR_SHIFT;
			ret = regmap_update_bits(data->regmap,
						 MXC4005_REG_CONTROL,
						 MXC4005_REG_CONTROL_MASK_FSR,
						 reg);
			if (ret < 0)
				dev_err(data->dev,
					"failed to write reg_control\n");
			return ret;
		}
	}

	return -EINVAL;
}

static int mxc4005_read_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan,
			    int *val, int *val2, long mask)
{
	struct mxc4005_data *data = iio_priv(indio_dev);
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		switch (chan->type) {
		case IIO_ACCEL:
			if (iio_buffer_enabled(indio_dev))
				return -EBUSY;

			ret = mxc4005_read_axis(data, chan->address);
			if (ret < 0)
				return ret;
			*val = sign_extend32(ret >> 4, 11);
			return IIO_VAL_INT;
		default:
			return -EINVAL;
		}
	case IIO_CHAN_INFO_SCALE:
		ret = mxc4005_read_scale(data);
		if (ret < 0)
			return ret;

		*val = 0;
		*val2 = ret;
		return IIO_VAL_INT_PLUS_MICRO;
	default:
		return -EINVAL;
	}
}

static int mxc4005_write_raw(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *chan,
			     int val, int val2, long mask)
{
	struct mxc4005_data *data = iio_priv(indio_dev);

	switch (mask) {
	case IIO_CHAN_INFO_SCALE:
		if (val != 0)
			return -EINVAL;

		return mxc4005_set_scale(data, val2);
	default:
		return -EINVAL;
	}
}

static const struct iio_info mxc4005_info = {
	.driver_module	= THIS_MODULE,
	.read_raw	= mxc4005_read_raw,
	.write_raw	= mxc4005_write_raw,
	.attrs		= &mxc4005_attrs_group,
};

#define MXC4005_CHANNEL(_axis, _addr) {				\
	.type = IIO_ACCEL,					\
	.modified = 1,						\
	.channel2 = IIO_MOD_##_axis,				\
	.address = _addr,					\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),		\
	.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE),	\
}

static const struct iio_chan_spec mxc4005_channels[] = {
	MXC4005_CHANNEL(X, MXC4005_REG_XOUT_UPPER),
	MXC4005_CHANNEL(Y, MXC4005_REG_YOUT_UPPER),
	MXC4005_CHANNEL(Z, MXC4005_REG_ZOUT_UPPER),
};

static int mxc4005_chip_init(struct mxc4005_data *data)
{
	int ret;
	unsigned int reg;

	ret = regmap_read(data->regmap, MXC4005_REG_DEVICE_ID, &reg);
	if (ret < 0) {
		dev_err(data->dev, "failed to read chip id\n");
		return ret;
	}

	dev_dbg(data->dev, "MXC4005 chip id %02x\n", reg);

	return 0;
}

static int mxc4005_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct mxc4005_data *data;
	struct iio_dev *indio_dev;
	struct regmap *regmap;
	int ret;

	indio_dev = devm_iio_device_alloc(&client->dev, sizeof(*data));
	if (!indio_dev)
		return -ENOMEM;

	regmap = devm_regmap_init_i2c(client, &mxc4005_regmap_config);
	if (IS_ERR(regmap)) {
		dev_err(&client->dev, "failed to initialize regmap\n");
		return PTR_ERR(regmap);
	}

	data = iio_priv(indio_dev);
	i2c_set_clientdata(client, indio_dev);
	data->dev = &client->dev;
	data->regmap = regmap;

	ret = mxc4005_chip_init(data);
	if (ret < 0) {
		dev_err(&client->dev, "failed to initialize chip\n");
		return ret;
	}

	mutex_init(&data->mutex);

	indio_dev->dev.parent = &client->dev;
	indio_dev->channels = mxc4005_channels;
	indio_dev->num_channels = ARRAY_SIZE(mxc4005_channels);
	indio_dev->name = MXC4005_DRV_NAME;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->info = &mxc4005_info;

	ret = iio_device_register(indio_dev);
	if (ret < 0) {
		dev_err(&client->dev,
			"unable to register iio device %d\n", ret);
		return ret;
	}

	return 0;
}

static int mxc4005_remove(struct i2c_client *client)
{
	iio_device_unregister(i2c_get_clientdata(client));

	return 0;
}

static const struct acpi_device_id mxc4005_acpi_match[] = {
	{"MXC4005",	0},
	{ },
};
MODULE_DEVICE_TABLE(acpi, mxc4005_acpi_match);

static const struct i2c_device_id mxc4005_id[] = {
	{"mxc4005",	0},
	{ },
};
MODULE_DEVICE_TABLE(i2c, mxc4005_id);

static struct i2c_driver mxc4005_driver = {
	.driver = {
		.name = MXC4005_DRV_NAME,
		.acpi_match_table = ACPI_PTR(mxc4005_acpi_match),
	},
	.probe		= mxc4005_probe,
	.remove		= mxc4005_remove,
	.id_table	= mxc4005_id,
};

module_i2c_driver(mxc4005_driver);

MODULE_AUTHOR("Teodora Baluta <teodora.baluta@intel.com>");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("MXC4005 3-axis accelerometer driver");

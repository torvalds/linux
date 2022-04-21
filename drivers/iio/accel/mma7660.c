// SPDX-License-Identifier: GPL-2.0-only
/*
 * Freescale MMA7660FC 3-Axis Accelerometer
 *
 * Copyright (c) 2016, Intel Corporation.
 *
 * IIO driver for Freescale MMA7660FC; 7-bit I2C address: 0x4c.
 */

#include <linux/acpi.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>

#define MMA7660_DRIVER_NAME	"mma7660"

#define MMA7660_REG_XOUT	0x00
#define MMA7660_REG_YOUT	0x01
#define MMA7660_REG_ZOUT	0x02
#define MMA7660_REG_OUT_BIT_ALERT	BIT(6)

#define MMA7660_REG_MODE	0x07
#define MMA7660_REG_MODE_BIT_MODE	BIT(0)
#define MMA7660_REG_MODE_BIT_TON	BIT(2)

#define MMA7660_I2C_READ_RETRIES	5

/*
 * The accelerometer has one measurement range:
 *
 * -1.5g - +1.5g (6-bit, signed)
 *
 * scale = (1.5 + 1.5) * 9.81 / (2^6 - 1)	= 0.467142857
 */

#define MMA7660_SCALE_AVAIL	"0.467142857"

static const int mma7660_nscale = 467142857;

#define MMA7660_CHANNEL(reg, axis) {	\
	.type = IIO_ACCEL,	\
	.address = reg,	\
	.modified = 1,	\
	.channel2 = IIO_MOD_##axis,	\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),	\
	.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE),	\
}

static const struct iio_chan_spec mma7660_channels[] = {
	MMA7660_CHANNEL(MMA7660_REG_XOUT, X),
	MMA7660_CHANNEL(MMA7660_REG_YOUT, Y),
	MMA7660_CHANNEL(MMA7660_REG_ZOUT, Z),
};

enum mma7660_mode {
	MMA7660_MODE_STANDBY,
	MMA7660_MODE_ACTIVE
};

struct mma7660_data {
	struct i2c_client *client;
	struct mutex lock;
	enum mma7660_mode mode;
};

static IIO_CONST_ATTR(in_accel_scale_available, MMA7660_SCALE_AVAIL);

static struct attribute *mma7660_attributes[] = {
	&iio_const_attr_in_accel_scale_available.dev_attr.attr,
	NULL,
};

static const struct attribute_group mma7660_attribute_group = {
	.attrs = mma7660_attributes
};

static int mma7660_set_mode(struct mma7660_data *data,
				enum mma7660_mode mode)
{
	int ret;
	struct i2c_client *client = data->client;

	if (mode == data->mode)
		return 0;

	ret = i2c_smbus_read_byte_data(client, MMA7660_REG_MODE);
	if (ret < 0) {
		dev_err(&client->dev, "failed to read sensor mode\n");
		return ret;
	}

	if (mode == MMA7660_MODE_ACTIVE) {
		ret &= ~MMA7660_REG_MODE_BIT_TON;
		ret |= MMA7660_REG_MODE_BIT_MODE;
	} else {
		ret &= ~MMA7660_REG_MODE_BIT_TON;
		ret &= ~MMA7660_REG_MODE_BIT_MODE;
	}

	ret = i2c_smbus_write_byte_data(client, MMA7660_REG_MODE, ret);
	if (ret < 0) {
		dev_err(&client->dev, "failed to change sensor mode\n");
		return ret;
	}

	data->mode = mode;

	return ret;
}

static int mma7660_read_accel(struct mma7660_data *data, u8 address)
{
	int ret, retries = MMA7660_I2C_READ_RETRIES;
	struct i2c_client *client = data->client;

	/*
	 * Read data. If the Alert bit is set, the register was read at
	 * the same time as the device was attempting to update the content.
	 * The solution is to read the register again. Do this only
	 * MMA7660_I2C_READ_RETRIES times to avoid spending too much time
	 * in the kernel.
	 */
	do {
		ret = i2c_smbus_read_byte_data(client, address);
		if (ret < 0) {
			dev_err(&client->dev, "register read failed\n");
			return ret;
		}
	} while (retries-- > 0 && ret & MMA7660_REG_OUT_BIT_ALERT);

	if (ret & MMA7660_REG_OUT_BIT_ALERT) {
		dev_err(&client->dev, "all register read retries failed\n");
		return -ETIMEDOUT;
	}

	return ret;
}

static int mma7660_read_raw(struct iio_dev *indio_dev,
				struct iio_chan_spec const *chan,
				int *val, int *val2, long mask)
{
	struct mma7660_data *data = iio_priv(indio_dev);
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		mutex_lock(&data->lock);
		ret = mma7660_read_accel(data, chan->address);
		mutex_unlock(&data->lock);
		if (ret < 0)
			return ret;
		*val = sign_extend32(ret, 5);
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		*val = 0;
		*val2 = mma7660_nscale;
		return IIO_VAL_INT_PLUS_NANO;
	default:
		return -EINVAL;
	}

	return -EINVAL;
}

static const struct iio_info mma7660_info = {
	.read_raw		= mma7660_read_raw,
	.attrs			= &mma7660_attribute_group,
};

static int mma7660_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	int ret;
	struct iio_dev *indio_dev;
	struct mma7660_data *data;

	indio_dev = devm_iio_device_alloc(&client->dev, sizeof(*data));
	if (!indio_dev) {
		dev_err(&client->dev, "iio allocation failed!\n");
		return -ENOMEM;
	}

	data = iio_priv(indio_dev);
	data->client = client;
	i2c_set_clientdata(client, indio_dev);
	mutex_init(&data->lock);
	data->mode = MMA7660_MODE_STANDBY;

	indio_dev->info = &mma7660_info;
	indio_dev->name = MMA7660_DRIVER_NAME;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = mma7660_channels;
	indio_dev->num_channels = ARRAY_SIZE(mma7660_channels);

	ret = mma7660_set_mode(data, MMA7660_MODE_ACTIVE);
	if (ret < 0)
		return ret;

	ret = iio_device_register(indio_dev);
	if (ret < 0) {
		dev_err(&client->dev, "device_register failed\n");
		mma7660_set_mode(data, MMA7660_MODE_STANDBY);
	}

	return ret;
}

static int mma7660_remove(struct i2c_client *client)
{
	struct iio_dev *indio_dev = i2c_get_clientdata(client);
	int ret;

	iio_device_unregister(indio_dev);

	ret = mma7660_set_mode(iio_priv(indio_dev), MMA7660_MODE_STANDBY);
	if (ret)
		dev_warn(&client->dev, "Failed to put device in stand-by mode (%pe), ignoring\n",
			 ERR_PTR(ret));

	return 0;
}

static int mma7660_suspend(struct device *dev)
{
	struct mma7660_data *data;

	data = iio_priv(i2c_get_clientdata(to_i2c_client(dev)));

	return mma7660_set_mode(data, MMA7660_MODE_STANDBY);
}

static int mma7660_resume(struct device *dev)
{
	struct mma7660_data *data;

	data = iio_priv(i2c_get_clientdata(to_i2c_client(dev)));

	return mma7660_set_mode(data, MMA7660_MODE_ACTIVE);
}

static DEFINE_SIMPLE_DEV_PM_OPS(mma7660_pm_ops, mma7660_suspend,
				mma7660_resume);

static const struct i2c_device_id mma7660_i2c_id[] = {
	{"mma7660", 0},
	{}
};
MODULE_DEVICE_TABLE(i2c, mma7660_i2c_id);

static const struct of_device_id mma7660_of_match[] = {
	{ .compatible = "fsl,mma7660" },
	{ }
};
MODULE_DEVICE_TABLE(of, mma7660_of_match);

static const struct acpi_device_id __maybe_unused mma7660_acpi_id[] = {
	{"MMA7660", 0},
	{}
};

MODULE_DEVICE_TABLE(acpi, mma7660_acpi_id);

static struct i2c_driver mma7660_driver = {
	.driver = {
		.name = "mma7660",
		.pm = pm_sleep_ptr(&mma7660_pm_ops),
		.of_match_table = mma7660_of_match,
		.acpi_match_table = ACPI_PTR(mma7660_acpi_id),
	},
	.probe		= mma7660_probe,
	.remove		= mma7660_remove,
	.id_table	= mma7660_i2c_id,
};

module_i2c_driver(mma7660_driver);

MODULE_AUTHOR("Constantin Musca <constantin.musca@intel.com>");
MODULE_DESCRIPTION("Freescale MMA7660FC 3-Axis Accelerometer driver");
MODULE_LICENSE("GPL v2");

// SPDX-License-Identifier: GPL-2.0-or-later
/**
 * mCube MC3230 3-Axis Accelerometer
 *
 * Copyright (c) 2016 Hans de Goede <hdegoede@redhat.com>
 *
 * IIO driver for mCube MC3230; 7-bit I2C address: 0x4c.
 */

#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>

#define MC3230_REG_XOUT			0x00
#define MC3230_REG_YOUT			0x01
#define MC3230_REG_ZOUT			0x02

#define MC3230_REG_MODE			0x07
#define MC3230_MODE_OPCON_MASK		0x03
#define MC3230_MODE_OPCON_WAKE		0x01
#define MC3230_MODE_OPCON_STANDBY	0x03

#define MC3230_REG_CHIP_ID		0x18
#define MC3230_CHIP_ID			0x01

#define MC3230_REG_PRODUCT_CODE		0x3b
#define MC3230_PRODUCT_CODE		0x19

/*
 * The accelerometer has one measurement range:
 *
 * -1.5g - +1.5g (8-bit, signed)
 *
 * scale = (1.5 + 1.5) * 9.81 / (2^8 - 1)	= 0.115411765
 */

static const int mc3230_nscale = 115411765;

#define MC3230_CHANNEL(reg, axis) {	\
	.type = IIO_ACCEL,	\
	.address = reg,	\
	.modified = 1,	\
	.channel2 = IIO_MOD_##axis,	\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),	\
	.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE),	\
}

static const struct iio_chan_spec mc3230_channels[] = {
	MC3230_CHANNEL(MC3230_REG_XOUT, X),
	MC3230_CHANNEL(MC3230_REG_YOUT, Y),
	MC3230_CHANNEL(MC3230_REG_ZOUT, Z),
};

struct mc3230_data {
	struct i2c_client *client;
};

static int mc3230_set_opcon(struct mc3230_data *data, int opcon)
{
	int ret;
	struct i2c_client *client = data->client;

	ret = i2c_smbus_read_byte_data(client, MC3230_REG_MODE);
	if (ret < 0) {
		dev_err(&client->dev, "failed to read mode reg: %d\n", ret);
		return ret;
	}

	ret &= ~MC3230_MODE_OPCON_MASK;
	ret |= opcon;

	ret = i2c_smbus_write_byte_data(client, MC3230_REG_MODE, ret);
	if (ret < 0) {
		dev_err(&client->dev, "failed to write mode reg: %d\n", ret);
		return ret;
	}

	return 0;
}

static int mc3230_read_raw(struct iio_dev *indio_dev,
				struct iio_chan_spec const *chan,
				int *val, int *val2, long mask)
{
	struct mc3230_data *data = iio_priv(indio_dev);
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		ret = i2c_smbus_read_byte_data(data->client, chan->address);
		if (ret < 0)
			return ret;
		*val = sign_extend32(ret, 7);
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		*val = 0;
		*val2 = mc3230_nscale;
		return IIO_VAL_INT_PLUS_NANO;
	default:
		return -EINVAL;
	}
}

static const struct iio_info mc3230_info = {
	.read_raw	= mc3230_read_raw,
};

static int mc3230_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	int ret;
	struct iio_dev *indio_dev;
	struct mc3230_data *data;

	/* First check chip-id and product-id */
	ret = i2c_smbus_read_byte_data(client, MC3230_REG_CHIP_ID);
	if (ret != MC3230_CHIP_ID)
		return (ret < 0) ? ret : -ENODEV;

	ret = i2c_smbus_read_byte_data(client, MC3230_REG_PRODUCT_CODE);
	if (ret != MC3230_PRODUCT_CODE)
		return (ret < 0) ? ret : -ENODEV;

	indio_dev = devm_iio_device_alloc(&client->dev, sizeof(*data));
	if (!indio_dev) {
		dev_err(&client->dev, "iio allocation failed!\n");
		return -ENOMEM;
	}

	data = iio_priv(indio_dev);
	data->client = client;
	i2c_set_clientdata(client, indio_dev);

	indio_dev->dev.parent = &client->dev;
	indio_dev->info = &mc3230_info;
	indio_dev->name = "mc3230";
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = mc3230_channels;
	indio_dev->num_channels = ARRAY_SIZE(mc3230_channels);

	ret = mc3230_set_opcon(data, MC3230_MODE_OPCON_WAKE);
	if (ret < 0)
		return ret;

	ret = iio_device_register(indio_dev);
	if (ret < 0) {
		dev_err(&client->dev, "device_register failed\n");
		mc3230_set_opcon(data, MC3230_MODE_OPCON_STANDBY);
	}

	return ret;
}

static int mc3230_remove(struct i2c_client *client)
{
	struct iio_dev *indio_dev = i2c_get_clientdata(client);

	iio_device_unregister(indio_dev);

	return mc3230_set_opcon(iio_priv(indio_dev), MC3230_MODE_OPCON_STANDBY);
}

#ifdef CONFIG_PM_SLEEP
static int mc3230_suspend(struct device *dev)
{
	struct mc3230_data *data;

	data = iio_priv(i2c_get_clientdata(to_i2c_client(dev)));

	return mc3230_set_opcon(data, MC3230_MODE_OPCON_STANDBY);
}

static int mc3230_resume(struct device *dev)
{
	struct mc3230_data *data;

	data = iio_priv(i2c_get_clientdata(to_i2c_client(dev)));

	return mc3230_set_opcon(data, MC3230_MODE_OPCON_WAKE);
}
#endif

static SIMPLE_DEV_PM_OPS(mc3230_pm_ops, mc3230_suspend, mc3230_resume);

static const struct i2c_device_id mc3230_i2c_id[] = {
	{"mc3230", 0},
	{}
};
MODULE_DEVICE_TABLE(i2c, mc3230_i2c_id);

static struct i2c_driver mc3230_driver = {
	.driver = {
		.name = "mc3230",
		.pm = &mc3230_pm_ops,
	},
	.probe		= mc3230_probe,
	.remove		= mc3230_remove,
	.id_table	= mc3230_i2c_id,
};

module_i2c_driver(mc3230_driver);

MODULE_AUTHOR("Hans de Goede <hdegoede@redhat.com>");
MODULE_DESCRIPTION("mCube MC3230 3-Axis Accelerometer driver");
MODULE_LICENSE("GPL v2");

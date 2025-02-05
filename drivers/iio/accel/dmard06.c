// SPDX-License-Identifier: GPL-2.0-only
/*
 * IIO driver for Domintech DMARD06 accelerometer
 *
 * Copyright (C) 2016 Aleksei Mamlin <mamlinav@gmail.com>
 */

#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/i2c.h>
#include <linux/iio/iio.h>

#define DMARD06_DRV_NAME		"dmard06"

/* Device data registers */
#define DMARD06_CHIP_ID_REG		0x0f
#define DMARD06_TOUT_REG		0x40
#define DMARD06_XOUT_REG		0x41
#define DMARD06_YOUT_REG		0x42
#define DMARD06_ZOUT_REG		0x43
#define DMARD06_CTRL1_REG		0x44

/* Device ID value */
#define DMARD05_CHIP_ID			0x05
#define DMARD06_CHIP_ID			0x06
#define DMARD07_CHIP_ID			0x07

/* Device values */
#define DMARD05_AXIS_SCALE_VAL		15625
#define DMARD06_AXIS_SCALE_VAL		31250
#define DMARD06_TEMP_CENTER_VAL		25
#define DMARD06_SIGN_BIT		7

/* Device power modes */
#define DMARD06_MODE_NORMAL		0x27
#define DMARD06_MODE_POWERDOWN		0x00

/* Device channels */
#define DMARD06_ACCEL_CHANNEL(_axis, _reg) {			\
	.type = IIO_ACCEL,					\
	.address = _reg,					\
	.channel2 = IIO_MOD_##_axis,				\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),		\
	.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE),	\
	.modified = 1,						\
}

#define DMARD06_TEMP_CHANNEL(_reg) {				\
	.type = IIO_TEMP,					\
	.address = _reg,					\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |		\
			      BIT(IIO_CHAN_INFO_OFFSET),	\
}

struct dmard06_data {
	struct i2c_client *client;
	u8 chip_id;
};

static const struct iio_chan_spec dmard06_channels[] = {
	DMARD06_ACCEL_CHANNEL(X, DMARD06_XOUT_REG),
	DMARD06_ACCEL_CHANNEL(Y, DMARD06_YOUT_REG),
	DMARD06_ACCEL_CHANNEL(Z, DMARD06_ZOUT_REG),
	DMARD06_TEMP_CHANNEL(DMARD06_TOUT_REG),
};

static int dmard06_read_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan,
			    int *val, int *val2, long mask)
{
	struct dmard06_data *dmard06 = iio_priv(indio_dev);
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		ret = i2c_smbus_read_byte_data(dmard06->client,
					       chan->address);
		if (ret < 0) {
			dev_err(&dmard06->client->dev,
				"Error reading data: %d\n", ret);
			return ret;
		}

		*val = sign_extend32(ret, DMARD06_SIGN_BIT);

		if (dmard06->chip_id == DMARD06_CHIP_ID)
			*val = *val >> 1;

		switch (chan->type) {
		case IIO_ACCEL:
			return IIO_VAL_INT;
		case IIO_TEMP:
			if (dmard06->chip_id != DMARD06_CHIP_ID)
				*val = *val / 2;
			return IIO_VAL_INT;
		default:
			return -EINVAL;
		}
	case IIO_CHAN_INFO_OFFSET:
		switch (chan->type) {
		case IIO_TEMP:
			*val = DMARD06_TEMP_CENTER_VAL;
			return IIO_VAL_INT;
		default:
			return -EINVAL;
		}
	case IIO_CHAN_INFO_SCALE:
		switch (chan->type) {
		case IIO_ACCEL:
			*val = 0;
			if (dmard06->chip_id == DMARD06_CHIP_ID)
				*val2 = DMARD06_AXIS_SCALE_VAL;
			else
				*val2 = DMARD05_AXIS_SCALE_VAL;
			return IIO_VAL_INT_PLUS_MICRO;
		default:
			return -EINVAL;
		}
	default:
		return -EINVAL;
	}
}

static const struct iio_info dmard06_info = {
	.read_raw	= dmard06_read_raw,
};

static int dmard06_probe(struct i2c_client *client)
{
	int ret;
	struct iio_dev *indio_dev;
	struct dmard06_data *dmard06;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		dev_err(&client->dev, "I2C check functionality failed\n");
		return -ENXIO;
	}

	indio_dev = devm_iio_device_alloc(&client->dev, sizeof(*dmard06));
	if (!indio_dev) {
		dev_err(&client->dev, "Failed to allocate iio device\n");
		return -ENOMEM;
	}

	dmard06 = iio_priv(indio_dev);
	dmard06->client = client;

	ret = i2c_smbus_read_byte_data(dmard06->client, DMARD06_CHIP_ID_REG);
	if (ret < 0) {
		dev_err(&client->dev, "Error reading chip id: %d\n", ret);
		return ret;
	}

	if (ret != DMARD05_CHIP_ID && ret != DMARD06_CHIP_ID &&
	    ret != DMARD07_CHIP_ID) {
		dev_err(&client->dev, "Invalid chip id: %02d\n", ret);
		return -ENODEV;
	}

	dmard06->chip_id = ret;

	i2c_set_clientdata(client, indio_dev);
	indio_dev->name = DMARD06_DRV_NAME;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = dmard06_channels;
	indio_dev->num_channels = ARRAY_SIZE(dmard06_channels);
	indio_dev->info = &dmard06_info;

	return devm_iio_device_register(&client->dev, indio_dev);
}

static int dmard06_suspend(struct device *dev)
{
	struct iio_dev *indio_dev = i2c_get_clientdata(to_i2c_client(dev));
	struct dmard06_data *dmard06 = iio_priv(indio_dev);
	int ret;

	ret = i2c_smbus_write_byte_data(dmard06->client, DMARD06_CTRL1_REG,
					DMARD06_MODE_POWERDOWN);
	if (ret < 0)
		return ret;

	return 0;
}

static int dmard06_resume(struct device *dev)
{
	struct iio_dev *indio_dev = i2c_get_clientdata(to_i2c_client(dev));
	struct dmard06_data *dmard06 = iio_priv(indio_dev);
	int ret;

	ret = i2c_smbus_write_byte_data(dmard06->client, DMARD06_CTRL1_REG,
					DMARD06_MODE_NORMAL);
	if (ret < 0)
		return ret;

	return 0;
}

static DEFINE_SIMPLE_DEV_PM_OPS(dmard06_pm_ops, dmard06_suspend,
				dmard06_resume);

static const struct i2c_device_id dmard06_id[] = {
	{ "dmard05" },
	{ "dmard06" },
	{ "dmard07" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, dmard06_id);

static const struct of_device_id dmard06_of_match[] = {
	{ .compatible = "domintech,dmard05" },
	{ .compatible = "domintech,dmard06" },
	{ .compatible = "domintech,dmard07" },
	{ }
};
MODULE_DEVICE_TABLE(of, dmard06_of_match);

static struct i2c_driver dmard06_driver = {
	.probe = dmard06_probe,
	.id_table = dmard06_id,
	.driver = {
		.name = DMARD06_DRV_NAME,
		.of_match_table = dmard06_of_match,
		.pm = pm_sleep_ptr(&dmard06_pm_ops),
	},
};
module_i2c_driver(dmard06_driver);

MODULE_AUTHOR("Aleksei Mamlin <mamlinav@gmail.com>");
MODULE_DESCRIPTION("Domintech DMARD06 accelerometer driver");
MODULE_LICENSE("GPL v2");

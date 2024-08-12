// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (c) Rajat Khandelwal <rajat.khandelwal@linux.intel.com>
 *
 * Maxim MAX30208 digital temperature sensor with 0.1°C accuracy
 * (7-bit I2C slave address (0x50 - 0x53))
 */

#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/iio/iio.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/types.h>

#define MAX30208_STATUS			0x00
#define MAX30208_STATUS_TEMP_RDY	BIT(0)
#define MAX30208_INT_ENABLE		0x01
#define MAX30208_INT_ENABLE_TEMP_RDY	BIT(0)

#define MAX30208_FIFO_OVF_CNTR		0x06
#define MAX30208_FIFO_DATA_CNTR		0x07
#define MAX30208_FIFO_DATA		0x08

#define MAX30208_FIFO_CONFIG		0x0a
#define MAX30208_FIFO_CONFIG_RO		BIT(1)

#define MAX30208_SYSTEM_CTRL		0x0c
#define MAX30208_SYSTEM_CTRL_RESET	0x01

#define MAX30208_TEMP_SENSOR_SETUP	0x14
#define MAX30208_TEMP_SENSOR_SETUP_CONV	BIT(0)

struct max30208_data {
	struct i2c_client *client;
	struct mutex lock; /* Lock to prevent concurrent reads of temperature readings */
};

static const struct iio_chan_spec max30208_channels[] = {
	{
		.type = IIO_TEMP,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) | BIT(IIO_CHAN_INFO_SCALE),
	},
};

/**
 * max30208_request() - Request a reading
 * @data: Struct comprising member elements of the device
 *
 * Requests a reading from the device and waits until the conversion is ready.
 */
static int max30208_request(struct max30208_data *data)
{
	/*
	 * Sensor can take up to 500 ms to respond so execute a total of
	 * 10 retries to give the device sufficient time.
	 */
	int retries = 10;
	u8 regval;
	int ret;

	ret = i2c_smbus_read_byte_data(data->client, MAX30208_TEMP_SENSOR_SETUP);
	if (ret < 0)
		return ret;

	regval = ret | MAX30208_TEMP_SENSOR_SETUP_CONV;

	ret = i2c_smbus_write_byte_data(data->client, MAX30208_TEMP_SENSOR_SETUP, regval);
	if (ret)
		return ret;

	while (retries--) {
		ret = i2c_smbus_read_byte_data(data->client, MAX30208_STATUS);
		if (ret < 0)
			return ret;

		if (ret & MAX30208_STATUS_TEMP_RDY)
			return 0;

		msleep(50);
	}
	dev_err(&data->client->dev, "Temperature conversion failed\n");

	return -ETIMEDOUT;
}

static int max30208_update_temp(struct max30208_data *data)
{
	u8 data_count;
	int ret;

	mutex_lock(&data->lock);

	ret = max30208_request(data);
	if (ret)
		goto unlock;

	ret = i2c_smbus_read_byte_data(data->client, MAX30208_FIFO_OVF_CNTR);
	if (ret < 0)
		goto unlock;
	else if (!ret) {
		ret = i2c_smbus_read_byte_data(data->client, MAX30208_FIFO_DATA_CNTR);
		if (ret < 0)
			goto unlock;

		data_count = ret;
	} else
		data_count = 1;

	while (data_count) {
		ret = i2c_smbus_read_word_swapped(data->client, MAX30208_FIFO_DATA);
		if (ret < 0)
			goto unlock;

		data_count--;
	}

unlock:
	mutex_unlock(&data->lock);
	return ret;
}

/**
 * max30208_config_setup() - Set up FIFO configuration register
 * @data: Struct comprising member elements of the device
 *
 * Sets the rollover bit to '1' to enable overwriting FIFO during overflow.
 */
static int max30208_config_setup(struct max30208_data *data)
{
	u8 regval;
	int ret;

	ret = i2c_smbus_read_byte_data(data->client, MAX30208_FIFO_CONFIG);
	if (ret < 0)
		return ret;

	regval = ret | MAX30208_FIFO_CONFIG_RO;

	ret = i2c_smbus_write_byte_data(data->client, MAX30208_FIFO_CONFIG, regval);
	if (ret)
		return ret;

	return 0;
}

static int max30208_read(struct iio_dev *indio_dev,
			 struct iio_chan_spec const *chan,
			 int *val, int *val2, long mask)
{
	struct max30208_data *data = iio_priv(indio_dev);
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		ret = max30208_update_temp(data);
		if (ret < 0)
			return ret;

		*val = sign_extend32(ret, 15);
		return IIO_VAL_INT;

	case IIO_CHAN_INFO_SCALE:
		*val = 5;
		return IIO_VAL_INT;

	default:
		return -EINVAL;
	}
}

static const struct iio_info max30208_info = {
	.read_raw = max30208_read,
};

static int max30208_probe(struct i2c_client *i2c)
{
	struct device *dev = &i2c->dev;
	struct max30208_data *data;
	struct iio_dev *indio_dev;
	int ret;

	indio_dev = devm_iio_device_alloc(dev, sizeof(*data));
	if (!indio_dev)
		return -ENOMEM;

	data = iio_priv(indio_dev);
	data->client = i2c;
	mutex_init(&data->lock);

	indio_dev->name = "max30208";
	indio_dev->channels = max30208_channels;
	indio_dev->num_channels = ARRAY_SIZE(max30208_channels);
	indio_dev->info = &max30208_info;
	indio_dev->modes = INDIO_DIRECT_MODE;

	ret = i2c_smbus_write_byte_data(data->client, MAX30208_SYSTEM_CTRL,
					MAX30208_SYSTEM_CTRL_RESET);
	if (ret) {
		dev_err(dev, "Failure in performing reset\n");
		return ret;
	}

	msleep(50);

	ret = max30208_config_setup(data);
	if (ret)
		return ret;

	ret = devm_iio_device_register(dev, indio_dev);
	if (ret) {
		dev_err(dev, "Failed to register IIO device\n");
		return ret;
	}

	return 0;
}

static const struct i2c_device_id max30208_id_table[] = {
	{ "max30208" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, max30208_id_table);

static const struct acpi_device_id max30208_acpi_match[] = {
	{ "MAX30208" },
	{ }
};
MODULE_DEVICE_TABLE(acpi, max30208_acpi_match);

static const struct of_device_id max30208_of_match[] = {
	{ .compatible = "maxim,max30208" },
	{ }
};
MODULE_DEVICE_TABLE(of, max30208_of_match);

static struct i2c_driver max30208_driver = {
	.driver = {
		.name = "max30208",
		.of_match_table = max30208_of_match,
		.acpi_match_table = max30208_acpi_match,
	},
	.probe = max30208_probe,
	.id_table = max30208_id_table,
};
module_i2c_driver(max30208_driver);

MODULE_AUTHOR("Rajat Khandelwal <rajat.khandelwal@linux.intel.com>");
MODULE_DESCRIPTION("Maxim MAX30208 digital temperature sensor");
MODULE_LICENSE("GPL");

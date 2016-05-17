/*
 * ms5637.c - Support for Measurement-Specialties ms5637 and ms8607
 *            pressure & temperature sensor
 *
 * Copyright (c) 2015 Measurement-Specialties
 *
 * Licensed under the GPL-2.
 *
 * (7-bit I2C slave address 0x76)
 *
 * Datasheet:
 *  http://www.meas-spec.com/downloads/MS5637-02BA03.pdf
 * Datasheet:
 *  http://www.meas-spec.com/downloads/MS8607-02BA01.pdf
 */

#include <linux/init.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/stat.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/mutex.h>

#include "../common/ms_sensors/ms_sensors_i2c.h"

static const int ms5637_samp_freq[6] = { 960, 480, 240, 120, 60, 30 };
/* String copy of the above const for readability purpose */
static const char ms5637_show_samp_freq[] = "960 480 240 120 60 30";

static int ms5637_read_raw(struct iio_dev *indio_dev,
			   struct iio_chan_spec const *channel, int *val,
			   int *val2, long mask)
{
	int ret;
	int temperature;
	unsigned int pressure;
	struct ms_tp_dev *dev_data = iio_priv(indio_dev);

	switch (mask) {
	case IIO_CHAN_INFO_PROCESSED:
		ret = ms_sensors_read_temp_and_pressure(dev_data,
							&temperature,
							&pressure);
		if (ret)
			return ret;

		switch (channel->type) {
		case IIO_TEMP:	/* in milli °C */
			*val = temperature;

			return IIO_VAL_INT;
		case IIO_PRESSURE:	/* in kPa */
			*val = pressure / 1000;
			*val2 = (pressure % 1000) * 1000;

			return IIO_VAL_INT_PLUS_MICRO;
		default:
			return -EINVAL;
		}
	case IIO_CHAN_INFO_SAMP_FREQ:
		*val = ms5637_samp_freq[dev_data->res_index];

		return IIO_VAL_INT;
	default:
		return -EINVAL;
	}
}

static int ms5637_write_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan,
			    int val, int val2, long mask)
{
	struct ms_tp_dev *dev_data = iio_priv(indio_dev);
	int i;

	switch (mask) {
	case IIO_CHAN_INFO_SAMP_FREQ:
		i = ARRAY_SIZE(ms5637_samp_freq);
		while (i-- > 0)
			if (val == ms5637_samp_freq[i])
				break;
		if (i < 0)
			return -EINVAL;
		dev_data->res_index = i;

		return 0;
	default:
		return -EINVAL;
	}
}

static const struct iio_chan_spec ms5637_channels[] = {
	{
		.type = IIO_TEMP,
		.info_mask_separate = BIT(IIO_CHAN_INFO_PROCESSED),
		.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_SAMP_FREQ),
	},
	{
		.type = IIO_PRESSURE,
		.info_mask_separate = BIT(IIO_CHAN_INFO_PROCESSED),
		.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_SAMP_FREQ),
	}
};

static IIO_CONST_ATTR_SAMP_FREQ_AVAIL(ms5637_show_samp_freq);

static struct attribute *ms5637_attributes[] = {
	&iio_const_attr_sampling_frequency_available.dev_attr.attr,
	NULL,
};

static const struct attribute_group ms5637_attribute_group = {
	.attrs = ms5637_attributes,
};

static const struct iio_info ms5637_info = {
	.read_raw = ms5637_read_raw,
	.write_raw = ms5637_write_raw,
	.attrs = &ms5637_attribute_group,
	.driver_module = THIS_MODULE,
};

static int ms5637_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct ms_tp_dev *dev_data;
	struct iio_dev *indio_dev;
	int ret;

	if (!i2c_check_functionality(client->adapter,
				     I2C_FUNC_SMBUS_READ_WORD_DATA |
				     I2C_FUNC_SMBUS_WRITE_BYTE |
				     I2C_FUNC_SMBUS_READ_I2C_BLOCK)) {
		dev_err(&client->dev,
			"Adapter does not support some i2c transaction\n");
		return -EOPNOTSUPP;
	}

	indio_dev = devm_iio_device_alloc(&client->dev, sizeof(*dev_data));
	if (!indio_dev)
		return -ENOMEM;

	dev_data = iio_priv(indio_dev);
	dev_data->client = client;
	dev_data->res_index = 5;
	mutex_init(&dev_data->lock);

	indio_dev->info = &ms5637_info;
	indio_dev->name = id->name;
	indio_dev->dev.parent = &client->dev;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = ms5637_channels;
	indio_dev->num_channels = ARRAY_SIZE(ms5637_channels);

	i2c_set_clientdata(client, indio_dev);

	ret = ms_sensors_reset(client, 0x1E, 3000);
	if (ret)
		return ret;

	ret = ms_sensors_tp_read_prom(dev_data);
	if (ret)
		return ret;

	return devm_iio_device_register(&client->dev, indio_dev);
}

static const struct i2c_device_id ms5637_id[] = {
	{"ms5637", 0},
	{"ms8607-temppressure", 1},
	{}
};
MODULE_DEVICE_TABLE(i2c, ms5637_id);

static struct i2c_driver ms5637_driver = {
	.probe = ms5637_probe,
	.id_table = ms5637_id,
	.driver = {
		   .name = "ms5637"
		   },
};

module_i2c_driver(ms5637_driver);

MODULE_DESCRIPTION("Measurement-Specialties ms5637 temperature & pressure driver");
MODULE_AUTHOR("William Markezana <william.markezana@meas-spec.com>");
MODULE_AUTHOR("Ludovic Tancerel <ludovic.tancerel@maplehightech.com>");
MODULE_LICENSE("GPL v2");

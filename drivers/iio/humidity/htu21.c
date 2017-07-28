/*
 * htu21.c - Support for Measurement-Specialties
 *           htu21 temperature & humidity sensor
 *	     and humidity part of MS8607 sensor
 *
 * Copyright (c) 2014 Measurement-Specialties
 *
 * Licensed under the GPL-2.
 *
 * (7-bit I2C slave address 0x40)
 *
 * Datasheet:
 *  http://www.meas-spec.com/downloads/HTU21D.pdf
 * Datasheet:
 *  http://www.meas-spec.com/downloads/MS8607-02BA01.pdf
 */

#include <linux/init.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/stat.h>
#include <linux/module.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>

#include "../common/ms_sensors/ms_sensors_i2c.h"

#define HTU21_RESET				0xFE

enum {
	HTU21,
	MS8607
};

static const int htu21_samp_freq[4] = { 20, 40, 70, 120 };
/* String copy of the above const for readability purpose */
static const char htu21_show_samp_freq[] = "20 40 70 120";

static int htu21_read_raw(struct iio_dev *indio_dev,
			  struct iio_chan_spec const *channel, int *val,
			  int *val2, long mask)
{
	int ret, temperature;
	unsigned int humidity;
	struct ms_ht_dev *dev_data = iio_priv(indio_dev);

	switch (mask) {
	case IIO_CHAN_INFO_PROCESSED:
		switch (channel->type) {
		case IIO_TEMP:	/* in milli °C */
			ret = ms_sensors_ht_read_temperature(dev_data,
							     &temperature);
			if (ret)
				return ret;
			*val = temperature;

			return IIO_VAL_INT;
		case IIO_HUMIDITYRELATIVE:	/* in milli %RH */
			ret = ms_sensors_ht_read_humidity(dev_data,
							  &humidity);
			if (ret)
				return ret;
			*val = humidity;

			return IIO_VAL_INT;
		default:
			return -EINVAL;
		}
	case IIO_CHAN_INFO_SAMP_FREQ:
		*val = htu21_samp_freq[dev_data->res_index];

		return IIO_VAL_INT;
	default:
		return -EINVAL;
	}
}

static int htu21_write_raw(struct iio_dev *indio_dev,
			   struct iio_chan_spec const *chan,
			   int val, int val2, long mask)
{
	struct ms_ht_dev *dev_data = iio_priv(indio_dev);
	int i, ret;

	switch (mask) {
	case IIO_CHAN_INFO_SAMP_FREQ:
		i = ARRAY_SIZE(htu21_samp_freq);
		while (i-- > 0)
			if (val == htu21_samp_freq[i])
				break;
		if (i < 0)
			return -EINVAL;
		mutex_lock(&dev_data->lock);
		dev_data->res_index = i;
		ret = ms_sensors_write_resolution(dev_data, i);
		mutex_unlock(&dev_data->lock);

		return ret;
	default:
		return -EINVAL;
	}
}

static const struct iio_chan_spec htu21_channels[] = {
	{
		.type = IIO_TEMP,
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_PROCESSED),
		.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_SAMP_FREQ),
	 },
	{
		.type = IIO_HUMIDITYRELATIVE,
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_PROCESSED),
		.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_SAMP_FREQ),
	 }
};

/*
 * Meas Spec recommendation is to not read temperature
 * on this driver part for MS8607
 */
static const struct iio_chan_spec ms8607_channels[] = {
	{
		.type = IIO_HUMIDITYRELATIVE,
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_PROCESSED),
		.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_SAMP_FREQ),
	 }
};

static ssize_t htu21_show_battery_low(struct device *dev,
				      struct device_attribute *attr, char *buf)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct ms_ht_dev *dev_data = iio_priv(indio_dev);

	return ms_sensors_show_battery_low(dev_data, buf);
}

static ssize_t htu21_show_heater(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct ms_ht_dev *dev_data = iio_priv(indio_dev);

	return ms_sensors_show_heater(dev_data, buf);
}

static ssize_t htu21_write_heater(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t len)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct ms_ht_dev *dev_data = iio_priv(indio_dev);

	return ms_sensors_write_heater(dev_data, buf, len);
}

static IIO_CONST_ATTR_SAMP_FREQ_AVAIL(htu21_show_samp_freq);
static IIO_DEVICE_ATTR(battery_low, S_IRUGO,
		       htu21_show_battery_low, NULL, 0);
static IIO_DEVICE_ATTR(heater_enable, S_IRUGO | S_IWUSR,
		       htu21_show_heater, htu21_write_heater, 0);

static struct attribute *htu21_attributes[] = {
	&iio_const_attr_sampling_frequency_available.dev_attr.attr,
	&iio_dev_attr_battery_low.dev_attr.attr,
	&iio_dev_attr_heater_enable.dev_attr.attr,
	NULL,
};

static const struct attribute_group htu21_attribute_group = {
	.attrs = htu21_attributes,
};

static const struct iio_info htu21_info = {
	.read_raw = htu21_read_raw,
	.write_raw = htu21_write_raw,
	.attrs = &htu21_attribute_group,
	.driver_module = THIS_MODULE,
};

static int htu21_probe(struct i2c_client *client,
		       const struct i2c_device_id *id)
{
	struct ms_ht_dev *dev_data;
	struct iio_dev *indio_dev;
	int ret;
	u64 serial_number;

	if (!i2c_check_functionality(client->adapter,
				     I2C_FUNC_SMBUS_WRITE_BYTE_DATA |
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
	dev_data->res_index = 0;
	mutex_init(&dev_data->lock);

	indio_dev->info = &htu21_info;
	indio_dev->name = id->name;
	indio_dev->dev.parent = &client->dev;
	indio_dev->modes = INDIO_DIRECT_MODE;

	if (id->driver_data == MS8607) {
		indio_dev->channels = ms8607_channels;
		indio_dev->num_channels = ARRAY_SIZE(ms8607_channels);
	} else {
		indio_dev->channels = htu21_channels;
		indio_dev->num_channels = ARRAY_SIZE(htu21_channels);
	}

	i2c_set_clientdata(client, indio_dev);

	ret = ms_sensors_reset(client, HTU21_RESET, 15000);
	if (ret)
		return ret;

	ret = ms_sensors_read_serial(client, &serial_number);
	if (ret)
		return ret;
	dev_info(&client->dev, "Serial number : %llx", serial_number);

	return devm_iio_device_register(&client->dev, indio_dev);
}

static const struct i2c_device_id htu21_id[] = {
	{"htu21", HTU21},
	{"ms8607-humidity", MS8607},
	{}
};
MODULE_DEVICE_TABLE(i2c, htu21_id);

static const struct of_device_id htu21_of_match[] = {
	{ .compatible = "meas,htu21", },
	{ .compatible = "meas,ms8607-humidity", },
	{ },
};
MODULE_DEVICE_TABLE(of, htu21_of_match);

static struct i2c_driver htu21_driver = {
	.probe = htu21_probe,
	.id_table = htu21_id,
	.driver = {
		   .name = "htu21",
		   .of_match_table = of_match_ptr(htu21_of_match),
		   },
};

module_i2c_driver(htu21_driver);

MODULE_DESCRIPTION("Measurement-Specialties htu21 temperature and humidity driver");
MODULE_AUTHOR("William Markezana <william.markezana@meas-spec.com>");
MODULE_AUTHOR("Ludovic Tancerel <ludovic.tancerel@maplehightech.com>");
MODULE_LICENSE("GPL v2");

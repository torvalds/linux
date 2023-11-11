// SPDX-License-Identifier: GPL-2.0-only
/*
 * tsys01.c - Support for Measurement-Specialties tsys01 temperature sensor
 *
 * Copyright (c) 2015 Measurement-Specialties
 *
 * Datasheet:
 *  http://www.meas-spec.com/downloads/TSYS01_Digital_Temperature_Sensor.pdf
 */

#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/device.h>
#include <linux/mutex.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/stat.h>
#include "../common/ms_sensors/ms_sensors_i2c.h"

/* TSYS01 Commands */
#define TSYS01_RESET				0x1E
#define TSYS01_CONVERSION_START			0x48
#define TSYS01_ADC_READ				0x00
#define TSYS01_PROM_READ			0xA0

#define TSYS01_PROM_WORDS_NB			8

struct tsys01_dev {
	void *client;
	struct mutex lock; /* lock during conversion */

	int (*reset)(void *cli, u8 cmd, unsigned int delay);
	int (*convert_and_read)(void *cli, u8 conv, u8 rd,
				unsigned int delay, u32 *adc);
	int (*read_prom_word)(void *cli, int cmd, u16 *word);

	u16 prom[TSYS01_PROM_WORDS_NB];
};

/* Multiplication coefficients for temperature computation */
static const int coeff_mul[] = { -1500000, 1000000, -2000000,
				 4000000, -2000000 };

static int tsys01_read_temperature(struct iio_dev *indio_dev,
				   s32 *temperature)
{
	int ret, i;
	u32 adc;
	s64 temp = 0;
	struct tsys01_dev *dev_data = iio_priv(indio_dev);

	mutex_lock(&dev_data->lock);
	ret = dev_data->convert_and_read(dev_data->client,
					 TSYS01_CONVERSION_START,
					 TSYS01_ADC_READ, 9000, &adc);
	mutex_unlock(&dev_data->lock);
	if (ret)
		return ret;

	adc >>= 8;

	/* Temperature algorithm */
	for (i = 4; i > 0; i--) {
		temp += coeff_mul[i] *
			(s64)dev_data->prom[5 - i];
		temp *= (s64)adc;
		temp = div64_s64(temp, 100000);
	}
	temp *= 10;
	temp += coeff_mul[0] * (s64)dev_data->prom[5];
	temp = div64_s64(temp, 100000);

	*temperature = temp;

	return 0;
}

static int tsys01_read_raw(struct iio_dev *indio_dev,
			   struct iio_chan_spec const *channel, int *val,
			   int *val2, long mask)
{
	int ret;
	s32 temperature;

	switch (mask) {
	case IIO_CHAN_INFO_PROCESSED:
		switch (channel->type) {
		case IIO_TEMP:	/* in milli °C */
			ret = tsys01_read_temperature(indio_dev, &temperature);
			if (ret)
				return ret;
			*val = temperature;

			return IIO_VAL_INT;
		default:
			return -EINVAL;
		}
	default:
		return -EINVAL;
	}
}

static const struct iio_chan_spec tsys01_channels[] = {
	{
		.type = IIO_TEMP,
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_PROCESSED),
	}
};

static const struct iio_info tsys01_info = {
	.read_raw = tsys01_read_raw,
};

static bool tsys01_crc_valid(u16 *n_prom)
{
	u8 cnt;
	u8 sum = 0;

	for (cnt = 0; cnt < TSYS01_PROM_WORDS_NB; cnt++)
		sum += ((n_prom[0] >> 8) + (n_prom[0] & 0xFF));

	return (sum == 0);
}

static int tsys01_read_prom(struct iio_dev *indio_dev)
{
	int i, ret;
	struct tsys01_dev *dev_data = iio_priv(indio_dev);
	char buf[7 * TSYS01_PROM_WORDS_NB + 1];
	char *ptr = buf;

	for (i = 0; i < TSYS01_PROM_WORDS_NB; i++) {
		ret = dev_data->read_prom_word(dev_data->client,
					       TSYS01_PROM_READ + (i << 1),
					       &dev_data->prom[i]);
		if (ret)
			return ret;

		ret = sprintf(ptr, "0x%04x ", dev_data->prom[i]);
		ptr += ret;
	}

	if (!tsys01_crc_valid(dev_data->prom)) {
		dev_err(&indio_dev->dev, "prom crc check error\n");
		return -ENODEV;
	}
	*ptr = 0;
	dev_info(&indio_dev->dev, "PROM coefficients : %s\n", buf);

	return 0;
}

static int tsys01_probe(struct iio_dev *indio_dev, struct device *dev)
{
	int ret;
	struct tsys01_dev *dev_data = iio_priv(indio_dev);

	mutex_init(&dev_data->lock);

	indio_dev->info = &tsys01_info;
	indio_dev->name = dev->driver->name;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = tsys01_channels;
	indio_dev->num_channels = ARRAY_SIZE(tsys01_channels);

	ret = dev_data->reset(dev_data->client, TSYS01_RESET, 3000);
	if (ret)
		return ret;

	ret = tsys01_read_prom(indio_dev);
	if (ret)
		return ret;

	return devm_iio_device_register(dev, indio_dev);
}

static int tsys01_i2c_probe(struct i2c_client *client,
			    const struct i2c_device_id *id)
{
	struct tsys01_dev *dev_data;
	struct iio_dev *indio_dev;

	if (!i2c_check_functionality(client->adapter,
				     I2C_FUNC_SMBUS_WORD_DATA |
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
	dev_data->reset = ms_sensors_reset;
	dev_data->read_prom_word = ms_sensors_read_prom_word;
	dev_data->convert_and_read = ms_sensors_convert_and_read;

	i2c_set_clientdata(client, indio_dev);

	return tsys01_probe(indio_dev, &client->dev);
}

static const struct i2c_device_id tsys01_id[] = {
	{"tsys01", 0},
	{}
};
MODULE_DEVICE_TABLE(i2c, tsys01_id);

static const struct of_device_id tsys01_of_match[] = {
	{ .compatible = "meas,tsys01", },
	{ },
};
MODULE_DEVICE_TABLE(of, tsys01_of_match);

static struct i2c_driver tsys01_driver = {
	.probe = tsys01_i2c_probe,
	.id_table = tsys01_id,
	.driver = {
		   .name = "tsys01",
		   .of_match_table = tsys01_of_match,
		   },
};

module_i2c_driver(tsys01_driver);

MODULE_DESCRIPTION("Measurement-Specialties tsys01 temperature driver");
MODULE_AUTHOR("William Markezana <william.markezana@meas-spec.com>");
MODULE_AUTHOR("Ludovic Tancerel <ludovic.tancerel@maplehightech.com>");
MODULE_LICENSE("GPL v2");
MODULE_IMPORT_NS(IIO_MEAS_SPEC_SENSORS);

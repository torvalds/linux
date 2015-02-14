/*
 * si7020.c - Silicon Labs Si7013/20/21 Relative Humidity and Temp Sensors
 * Copyright (c) 2013,2014  Uplogix, Inc.
 * David Barksdale <dbarksdale@uplogix.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

/*
 * The Silicon Labs Si7013/20/21 Relative Humidity and Temperature Sensors
 * are i2c devices which have an identical programming interface for
 * measuring relative humidity and temperature. The Si7013 has an additional
 * temperature input which this driver does not support.
 *
 * Data Sheets:
 *   Si7013: http://www.silabs.com/Support%20Documents/TechnicalDocs/Si7013.pdf
 *   Si7020: http://www.silabs.com/Support%20Documents/TechnicalDocs/Si7020.pdf
 *   Si7021: http://www.silabs.com/Support%20Documents/TechnicalDocs/Si7021.pdf
 */

#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/sysfs.h>

#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>

/* Measure Relative Humidity, Hold Master Mode */
#define SI7020CMD_RH_HOLD	0xE5
/* Measure Temperature, Hold Master Mode */
#define SI7020CMD_TEMP_HOLD	0xE3
/* Software Reset */
#define SI7020CMD_RESET		0xFE

static int si7020_read_raw(struct iio_dev *indio_dev,
			   struct iio_chan_spec const *chan, int *val,
			   int *val2, long mask)
{
	struct i2c_client **client = iio_priv(indio_dev);
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		ret = i2c_smbus_read_word_data(*client,
					       chan->type == IIO_TEMP ?
					       SI7020CMD_TEMP_HOLD :
					       SI7020CMD_RH_HOLD);
		if (ret < 0)
			return ret;
		*val = ret >> 2;
		if (chan->type == IIO_HUMIDITYRELATIVE)
			*val &= GENMASK(11, 0);
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		if (chan->type == IIO_TEMP)
			*val = 175720; /* = 175.72 * 1000 */
		else
			*val = 125 * 1000;
		*val2 = 65536 >> 2;
		return IIO_VAL_FRACTIONAL;
	case IIO_CHAN_INFO_OFFSET:
		/*
		 * Since iio_convert_raw_to_processed_unlocked assumes offset
		 * is an integer we have to round these values and lose
		 * accuracy.
		 * Relative humidity will be 0.0032959% too high and
		 * temperature will be 0.00277344 degrees too high.
		 * This is no big deal because it's within the accuracy of the
		 * sensor.
		 */
		if (chan->type == IIO_TEMP)
			*val = -4368; /* = -46.85 * (65536 >> 2) / 175.72 */
		else
			*val = -786; /* = -6 * (65536 >> 2) / 125 */
		return IIO_VAL_INT;
	default:
		break;
	}

	return -EINVAL;
}

static const struct iio_chan_spec si7020_channels[] = {
	{
		.type = IIO_HUMIDITYRELATIVE,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
			BIT(IIO_CHAN_INFO_SCALE) | BIT(IIO_CHAN_INFO_OFFSET),
	},
	{
		.type = IIO_TEMP,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
			BIT(IIO_CHAN_INFO_SCALE) | BIT(IIO_CHAN_INFO_OFFSET),
	}
};

static const struct iio_info si7020_info = {
	.read_raw = si7020_read_raw,
	.driver_module = THIS_MODULE,
};

static int si7020_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct iio_dev *indio_dev;
	struct i2c_client **data;
	int ret;

	if (!i2c_check_functionality(client->adapter,
				     I2C_FUNC_SMBUS_WRITE_BYTE |
				     I2C_FUNC_SMBUS_READ_WORD_DATA))
		return -ENODEV;

	/* Reset device, loads default settings. */
	ret = i2c_smbus_write_byte(client, SI7020CMD_RESET);
	if (ret < 0)
		return ret;
	/* Wait the maximum power-up time after software reset. */
	msleep(15);

	indio_dev = devm_iio_device_alloc(&client->dev, sizeof(*data));
	if (!indio_dev)
		return -ENOMEM;

	data = iio_priv(indio_dev);
	*data = client;

	indio_dev->dev.parent = &client->dev;
	indio_dev->name = dev_name(&client->dev);
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->info = &si7020_info;
	indio_dev->channels = si7020_channels;
	indio_dev->num_channels = ARRAY_SIZE(si7020_channels);

	return devm_iio_device_register(&client->dev, indio_dev);
}

static const struct i2c_device_id si7020_id[] = {
	{ "si7020", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, si7020_id);

static struct i2c_driver si7020_driver = {
	.driver.name	= "si7020",
	.probe		= si7020_probe,
	.id_table	= si7020_id,
};

module_i2c_driver(si7020_driver);
MODULE_DESCRIPTION("Silicon Labs Si7013/20/21 Relative Humidity and Temperature Sensors");
MODULE_AUTHOR("David Barksdale <dbarksdale@uplogix.com>");
MODULE_LICENSE("GPL");

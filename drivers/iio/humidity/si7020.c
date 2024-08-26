// SPDX-License-Identifier: GPL-2.0-only
/*
 * si7020.c - Silicon Labs Si7013/20/21 Relative Humidity and Temp Sensors
 * Copyright (c) 2013,2014  Uplogix, Inc.
 * David Barksdale <dbarksdale@uplogix.com>
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
#include <linux/mod_devicetable.h>
#include <linux/slab.h>
#include <linux/sysfs.h>
#include <linux/stat.h>

#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>

/* Measure Relative Humidity, Hold Master Mode */
#define SI7020CMD_RH_HOLD	0xE5
/* Measure Temperature, Hold Master Mode */
#define SI7020CMD_TEMP_HOLD	0xE3
/* Software Reset */
#define SI7020CMD_RESET		0xFE
#define SI7020CMD_USR_WRITE	0xE6
/* "Heater Enabled" bit in the User Register */
#define SI7020_USR_HEATER_EN	BIT(2)
#define SI7020CMD_HEATER_WRITE	0x51
/* Heater current configuration bits */
#define SI7020_HEATER_VAL	GENMASK(3, 0)

struct si7020_data {
	struct i2c_client *client;
	/* Lock for cached register values */
	struct mutex lock;
	u8 user_reg;
	u8 heater_reg;
};

static const int si7020_heater_vals[] = { 0, 1, 0xF };

static int si7020_read_raw(struct iio_dev *indio_dev,
			   struct iio_chan_spec const *chan, int *val,
			   int *val2, long mask)
{
	struct si7020_data *data = iio_priv(indio_dev);
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		if (chan->type == IIO_CURRENT) {
			*val = data->heater_reg;
			return IIO_VAL_INT;
		}

		ret = i2c_smbus_read_word_swapped(data->client,
						  chan->type == IIO_TEMP ?
						  SI7020CMD_TEMP_HOLD :
						  SI7020CMD_RH_HOLD);
		if (ret < 0)
			return ret;
		*val = ret >> 2;
		/*
		 * Humidity values can slightly exceed the 0-100%RH
		 * range and should be corrected by software
		 */
		if (chan->type == IIO_HUMIDITYRELATIVE)
			*val = clamp_val(*val, 786, 13893);
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
	},
	{
		.type = IIO_CURRENT,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
		.info_mask_separate_available = BIT(IIO_CHAN_INFO_RAW),
		.extend_name = "heater",
	}
};

static int si7020_update_reg(struct si7020_data *data,
				u8 *reg, u8 cmd, u8 mask, u8 val)
{
	u8 new = (*reg & ~mask) | val;
	int ret;

	ret = i2c_smbus_write_byte_data(data->client, cmd, new);
	if (ret)
		return ret;

	*reg = new;

	return 0;
}

static int si7020_write_raw(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *chan,
			     int val, int val2, long mask)
{
	struct si7020_data *data = iio_priv(indio_dev);
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		if (chan->type != IIO_CURRENT || val2 != 0 ||
			val < si7020_heater_vals[0] || val > si7020_heater_vals[2])
			return -EINVAL;

		scoped_guard(mutex, &data->lock)
			ret = si7020_update_reg(data, &data->heater_reg,
					SI7020CMD_HEATER_WRITE, SI7020_HEATER_VAL, val);
		return ret;
	default:
		return -EINVAL;
	}
}

static int si7020_read_available(struct iio_dev *indio_dev,
				  struct iio_chan_spec const *chan,
				  const int **vals,
				  int *type, int *length, long mask)
{
	if (mask != IIO_CHAN_INFO_RAW || chan->type != IIO_CURRENT)
		return -EINVAL;

	*vals = si7020_heater_vals;
	*type = IIO_VAL_INT;

	return IIO_AVAIL_RANGE;
}

static ssize_t si7020_show_heater_en(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct si7020_data *data = iio_priv(indio_dev);

	return sysfs_emit(buf, "%d\n", !!(data->user_reg & SI7020_USR_HEATER_EN));
}

static ssize_t si7020_store_heater_en(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t len)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct si7020_data *data = iio_priv(indio_dev);
	int ret;
	bool val;

	ret = kstrtobool(buf, &val);
	if (ret)
		return ret;

	scoped_guard(mutex, &data->lock)
		ret = si7020_update_reg(data, &data->user_reg, SI7020CMD_USR_WRITE,
				SI7020_USR_HEATER_EN, val ? SI7020_USR_HEATER_EN : 0);

	return ret < 0 ? ret : len;
}

static IIO_DEVICE_ATTR(heater_enable, 0644,
		       si7020_show_heater_en, si7020_store_heater_en, 0);

static struct attribute *si7020_attributes[] = {
	&iio_dev_attr_heater_enable.dev_attr.attr,
	NULL
};

static const struct attribute_group si7020_attribute_group = {
	.attrs = si7020_attributes,
};

static const struct iio_info si7020_info = {
	.read_raw = si7020_read_raw,
	.write_raw = si7020_write_raw,
	.read_avail = si7020_read_available,
	.attrs = &si7020_attribute_group,
};

static int si7020_probe(struct i2c_client *client)
{
	struct iio_dev *indio_dev;
	struct si7020_data *data;
	int ret;

	if (!i2c_check_functionality(client->adapter,
				     I2C_FUNC_SMBUS_WRITE_BYTE |
				     I2C_FUNC_SMBUS_READ_WORD_DATA))
		return -EOPNOTSUPP;

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
	i2c_set_clientdata(client, indio_dev);
	data->client = client;
	mutex_init(&data->lock);

	indio_dev->name = dev_name(&client->dev);
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->info = &si7020_info;
	indio_dev->channels = si7020_channels;
	indio_dev->num_channels = ARRAY_SIZE(si7020_channels);

	/* All the "reserved" bits in the User Register are 1s by default */
	data->user_reg = 0x3A;
	data->heater_reg = 0x0;

	return devm_iio_device_register(&client->dev, indio_dev);
}

static const struct i2c_device_id si7020_id[] = {
	{ "si7020" },
	{ "th06" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, si7020_id);

static const struct of_device_id si7020_dt_ids[] = {
	{ .compatible = "silabs,si7020" },
	{ }
};
MODULE_DEVICE_TABLE(of, si7020_dt_ids);

static struct i2c_driver si7020_driver = {
	.driver = {
		.name = "si7020",
		.of_match_table = si7020_dt_ids,
	},
	.probe		= si7020_probe,
	.id_table	= si7020_id,
};

module_i2c_driver(si7020_driver);
MODULE_DESCRIPTION("Silicon Labs Si7013/20/21 Relative Humidity and Temperature Sensors");
MODULE_AUTHOR("David Barksdale <dbarksdale@uplogix.com>");
MODULE_LICENSE("GPL");

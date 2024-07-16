// SPDX-License-Identifier: GPL-2.0+
/*
 * hdc2010.c - Support for the TI HDC2010 and HDC2080
 * temperature + relative humidity sensors
 *
 * Copyright (C) 2020 Norphonic AS
 * Author: Eugene Zaikonnikov <ez@norphonic.com>
 *
 * Datasheet: https://www.ti.com/product/HDC2010/datasheet
 * Datasheet: https://www.ti.com/product/HDC2080/datasheet
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/bitops.h>

#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>

#define HDC2010_REG_TEMP_LOW			0x00
#define HDC2010_REG_TEMP_HIGH			0x01
#define HDC2010_REG_HUMIDITY_LOW		0x02
#define HDC2010_REG_HUMIDITY_HIGH		0x03
#define HDC2010_REG_INTERRUPT_DRDY		0x04
#define HDC2010_REG_TEMP_MAX			0x05
#define HDC2010_REG_HUMIDITY_MAX		0x06
#define HDC2010_REG_INTERRUPT_EN		0x07
#define HDC2010_REG_TEMP_OFFSET_ADJ		0x08
#define HDC2010_REG_HUMIDITY_OFFSET_ADJ		0x09
#define HDC2010_REG_TEMP_THR_L			0x0a
#define HDC2010_REG_TEMP_THR_H			0x0b
#define HDC2010_REG_RH_THR_L			0x0c
#define HDC2010_REG_RH_THR_H			0x0d
#define HDC2010_REG_RESET_DRDY_INT_CONF		0x0e
#define HDC2010_REG_MEASUREMENT_CONF		0x0f

#define HDC2010_MEAS_CONF			GENMASK(2, 1)
#define HDC2010_MEAS_TRIG			BIT(0)
#define HDC2010_HEATER_EN			BIT(3)
#define HDC2010_AMM				GENMASK(6, 4)

struct hdc2010_data {
	struct i2c_client *client;
	struct mutex lock;
	u8 measurement_config;
	u8 interrupt_config;
	u8 drdy_config;
};

enum hdc2010_addr_groups {
	HDC2010_GROUP_TEMP = 0,
	HDC2010_GROUP_HUMIDITY,
};

struct hdc2010_reg_record {
	unsigned long primary;
	unsigned long peak;
};

static const struct hdc2010_reg_record hdc2010_reg_translation[] = {
	[HDC2010_GROUP_TEMP] = {
		.primary = HDC2010_REG_TEMP_LOW,
		.peak = HDC2010_REG_TEMP_MAX,
	},
	[HDC2010_GROUP_HUMIDITY] = {
		.primary = HDC2010_REG_HUMIDITY_LOW,
		.peak = HDC2010_REG_HUMIDITY_MAX,
	},
};

static IIO_CONST_ATTR(out_current_heater_raw_available, "0 1");

static struct attribute *hdc2010_attributes[] = {
	&iio_const_attr_out_current_heater_raw_available.dev_attr.attr,
	NULL
};

static const struct attribute_group hdc2010_attribute_group = {
	.attrs = hdc2010_attributes,
};

static const struct iio_chan_spec hdc2010_channels[] = {
	{
		.type = IIO_TEMP,
		.address = HDC2010_GROUP_TEMP,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
			BIT(IIO_CHAN_INFO_PEAK) |
			BIT(IIO_CHAN_INFO_OFFSET) |
			BIT(IIO_CHAN_INFO_SCALE),
	},
	{
		.type = IIO_HUMIDITYRELATIVE,
		.address = HDC2010_GROUP_HUMIDITY,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
			BIT(IIO_CHAN_INFO_PEAK) |
			BIT(IIO_CHAN_INFO_SCALE),
	},
	{
		.type = IIO_CURRENT,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
		.extend_name = "heater",
		.output = 1,
	},
};

static int hdc2010_update_drdy_config(struct hdc2010_data *data,
					     char mask, char val)
{
	u8 tmp = (~mask & data->drdy_config) | val;
	int ret;

	ret = i2c_smbus_write_byte_data(data->client,
					HDC2010_REG_RESET_DRDY_INT_CONF, tmp);
	if (ret)
		return ret;

	data->drdy_config = tmp;

	return 0;
}

static int hdc2010_get_prim_measurement_word(struct hdc2010_data *data,
					     struct iio_chan_spec const *chan)
{
	struct i2c_client *client = data->client;
	s32 ret;

	ret = i2c_smbus_read_word_data(client,
			hdc2010_reg_translation[chan->address].primary);

	if (ret < 0)
		dev_err(&client->dev, "Could not read sensor measurement word\n");

	return ret;
}

static int hdc2010_get_peak_measurement_byte(struct hdc2010_data *data,
					     struct iio_chan_spec const *chan)
{
	struct i2c_client *client = data->client;
	s32 ret;

	ret = i2c_smbus_read_byte_data(client,
			hdc2010_reg_translation[chan->address].peak);

	if (ret < 0)
		dev_err(&client->dev, "Could not read sensor measurement byte\n");

	return ret;
}

static int hdc2010_get_heater_status(struct hdc2010_data *data)
{
	return !!(data->drdy_config & HDC2010_HEATER_EN);
}

static int hdc2010_read_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan, int *val,
			    int *val2, long mask)
{
	struct hdc2010_data *data = iio_priv(indio_dev);

	switch (mask) {
	case IIO_CHAN_INFO_RAW: {
		int ret;

		if (chan->type == IIO_CURRENT) {
			*val = hdc2010_get_heater_status(data);
			return IIO_VAL_INT;
		}
		ret = iio_device_claim_direct_mode(indio_dev);
		if (ret)
			return ret;
		mutex_lock(&data->lock);
		ret = hdc2010_get_prim_measurement_word(data, chan);
		mutex_unlock(&data->lock);
		iio_device_release_direct_mode(indio_dev);
		if (ret < 0)
			return ret;
		*val = ret;
		return IIO_VAL_INT;
	}
	case IIO_CHAN_INFO_PEAK: {
		int ret;

		ret = iio_device_claim_direct_mode(indio_dev);
		if (ret)
			return ret;
		mutex_lock(&data->lock);
		ret = hdc2010_get_peak_measurement_byte(data, chan);
		mutex_unlock(&data->lock);
		iio_device_release_direct_mode(indio_dev);
		if (ret < 0)
			return ret;
		/* Scaling up the value so we can use same offset as RAW */
		*val = ret * 256;
		return IIO_VAL_INT;
	}
	case IIO_CHAN_INFO_SCALE:
		*val2 = 65536;
		if (chan->type == IIO_TEMP)
			*val = 165000;
		else
			*val = 100000;
		return IIO_VAL_FRACTIONAL;
	case IIO_CHAN_INFO_OFFSET:
		*val = -15887;
		*val2 = 515151;
		return IIO_VAL_INT_PLUS_MICRO;
	default:
		return -EINVAL;
	}
}

static int hdc2010_write_raw(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *chan,
			     int val, int val2, long mask)
{
	struct hdc2010_data *data = iio_priv(indio_dev);
	int new, ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		if (chan->type != IIO_CURRENT || val2 != 0)
			return -EINVAL;

		switch (val) {
		case 1:
			new = HDC2010_HEATER_EN;
			break;
		case 0:
			new = 0;
			break;
		default:
			return -EINVAL;
		}

		mutex_lock(&data->lock);
		ret = hdc2010_update_drdy_config(data, HDC2010_HEATER_EN, new);
		mutex_unlock(&data->lock);
		return ret;
	default:
		return -EINVAL;
	}
}

static const struct iio_info hdc2010_info = {
	.read_raw = hdc2010_read_raw,
	.write_raw = hdc2010_write_raw,
	.attrs = &hdc2010_attribute_group,
};

static int hdc2010_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct iio_dev *indio_dev;
	struct hdc2010_data *data;
	u8 tmp;
	int ret;

	if (!i2c_check_functionality(client->adapter,
	    I2C_FUNC_SMBUS_WORD_DATA | I2C_FUNC_SMBUS_BYTE | I2C_FUNC_I2C))
		return -EOPNOTSUPP;

	indio_dev = devm_iio_device_alloc(&client->dev, sizeof(*data));
	if (!indio_dev)
		return -ENOMEM;

	data = iio_priv(indio_dev);
	i2c_set_clientdata(client, indio_dev);
	data->client = client;
	mutex_init(&data->lock);

	/*
	 * As DEVICE ID register does not differentiate between
	 * HDC2010 and HDC2080, we have the name hardcoded
	 */
	indio_dev->name = "hdc2010";
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->info = &hdc2010_info;

	indio_dev->channels = hdc2010_channels;
	indio_dev->num_channels = ARRAY_SIZE(hdc2010_channels);

	/* Enable Automatic Measurement Mode at 5Hz */
	ret = hdc2010_update_drdy_config(data, HDC2010_AMM, HDC2010_AMM);
	if (ret)
		return ret;

	/*
	 * We enable both temp and humidity measurement.
	 * However the measurement won't start even in AMM until triggered.
	 */
	tmp = (data->measurement_config & ~HDC2010_MEAS_CONF) |
		HDC2010_MEAS_TRIG;

	ret = i2c_smbus_write_byte_data(client, HDC2010_REG_MEASUREMENT_CONF, tmp);
	if (ret) {
		dev_warn(&client->dev, "Unable to set up measurement\n");
		if (hdc2010_update_drdy_config(data, HDC2010_AMM, 0))
			dev_warn(&client->dev, "Unable to restore default AMM\n");
		return ret;
	}

	data->measurement_config = tmp;

	return iio_device_register(indio_dev);
}

static void hdc2010_remove(struct i2c_client *client)
{
	struct iio_dev *indio_dev = i2c_get_clientdata(client);
	struct hdc2010_data *data = iio_priv(indio_dev);

	iio_device_unregister(indio_dev);

	/* Disable Automatic Measurement Mode */
	if (hdc2010_update_drdy_config(data, HDC2010_AMM, 0))
		dev_warn(&client->dev, "Unable to restore default AMM\n");
}

static const struct i2c_device_id hdc2010_id[] = {
	{ "hdc2010" },
	{ "hdc2080" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, hdc2010_id);

static const struct of_device_id hdc2010_dt_ids[] = {
	{ .compatible = "ti,hdc2010" },
	{ .compatible = "ti,hdc2080" },
	{ }
};
MODULE_DEVICE_TABLE(of, hdc2010_dt_ids);

static struct i2c_driver hdc2010_driver = {
	.driver = {
		.name	= "hdc2010",
		.of_match_table = hdc2010_dt_ids,
	},
	.probe = hdc2010_probe,
	.remove = hdc2010_remove,
	.id_table = hdc2010_id,
};
module_i2c_driver(hdc2010_driver);

MODULE_AUTHOR("Eugene Zaikonnikov <ez@norphonic.com>");
MODULE_DESCRIPTION("TI HDC2010 humidity and temperature sensor driver");
MODULE_LICENSE("GPL");

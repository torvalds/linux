// SPDX-License-Identifier: GPL-2.0+
/*
 * hdc100x.c - Support for the TI HDC100x temperature + humidity sensors
 *
 * Copyright (C) 2015, 2018
 * Author: Matt Ranostay <matt.ranostay@konsulko.com>
 *
 * Datasheets:
 * https://www.ti.com/product/HDC1000/datasheet
 * https://www.ti.com/product/HDC1008/datasheet
 * https://www.ti.com/product/HDC1010/datasheet
 * https://www.ti.com/product/HDC1050/datasheet
 * https://www.ti.com/product/HDC1080/datasheet
 */

#include <linux/delay.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/init.h>
#include <linux/i2c.h>

#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/buffer.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/iio/triggered_buffer.h>

#include <linux/time.h>

#define HDC100X_REG_TEMP			0x00
#define HDC100X_REG_HUMIDITY			0x01

#define HDC100X_REG_CONFIG			0x02
#define HDC100X_REG_CONFIG_ACQ_MODE		BIT(12)
#define HDC100X_REG_CONFIG_HEATER_EN		BIT(13)

struct hdc100x_data {
	struct i2c_client *client;
	struct mutex lock;
	u16 config;

	/* integration time of the sensor */
	int adc_int_us[2];
	/* Ensure natural alignment of timestamp */
	struct {
		__be16 channels[2];
		aligned_s64 ts;
	} scan;
};

/* integration time in us */
static const int hdc100x_int_time[][3] = {
	{ 6350, 3650, 0 },	/* IIO_TEMP channel*/
	{ 6500, 3850, 2500 },	/* IIO_HUMIDITYRELATIVE channel */
};

/* HDC100X_REG_CONFIG shift and mask values */
static const struct {
	int shift;
	int mask;
} hdc100x_resolution_shift[2] = {
	{ /* IIO_TEMP channel */
		.shift = 10,
		.mask = 1
	},
	{ /* IIO_HUMIDITYRELATIVE channel */
		.shift = 8,
		.mask = 3,
	},
};

static IIO_CONST_ATTR(temp_integration_time_available,
		"0.00365 0.00635");

static IIO_CONST_ATTR(humidityrelative_integration_time_available,
		"0.0025 0.00385 0.0065");

static IIO_CONST_ATTR(out_current_heater_raw_available,
		"0 1");

static struct attribute *hdc100x_attributes[] = {
	&iio_const_attr_temp_integration_time_available.dev_attr.attr,
	&iio_const_attr_humidityrelative_integration_time_available.dev_attr.attr,
	&iio_const_attr_out_current_heater_raw_available.dev_attr.attr,
	NULL
};

static const struct attribute_group hdc100x_attribute_group = {
	.attrs = hdc100x_attributes,
};

static const struct iio_chan_spec hdc100x_channels[] = {
	{
		.type = IIO_TEMP,
		.address = HDC100X_REG_TEMP,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
			BIT(IIO_CHAN_INFO_SCALE) |
			BIT(IIO_CHAN_INFO_INT_TIME) |
			BIT(IIO_CHAN_INFO_OFFSET),
		.scan_index = 0,
		.scan_type = {
			.sign = 's',
			.realbits = 16,
			.storagebits = 16,
			.endianness = IIO_BE,
		},
	},
	{
		.type = IIO_HUMIDITYRELATIVE,
		.address = HDC100X_REG_HUMIDITY,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
			BIT(IIO_CHAN_INFO_SCALE) |
			BIT(IIO_CHAN_INFO_INT_TIME),
		.scan_index = 1,
		.scan_type = {
			.sign = 'u',
			.realbits = 16,
			.storagebits = 16,
			.endianness = IIO_BE,
		},
	},
	{
		.type = IIO_CURRENT,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
		.extend_name = "heater",
		.output = 1,
		.scan_index = -1,
	},
	IIO_CHAN_SOFT_TIMESTAMP(2),
};

static const unsigned long hdc100x_scan_masks[] = {0x3, 0};

static int hdc100x_update_config(struct hdc100x_data *data, int mask, int val)
{
	int tmp = (~mask & data->config) | val;
	int ret;

	ret = i2c_smbus_write_word_swapped(data->client,
						HDC100X_REG_CONFIG, tmp);
	if (!ret)
		data->config = tmp;

	return ret;
}

static int hdc100x_set_it_time(struct hdc100x_data *data, int chan, int val2)
{
	int shift = hdc100x_resolution_shift[chan].shift;
	int ret = -EINVAL;
	int i;

	for (i = 0; i < ARRAY_SIZE(hdc100x_int_time[chan]); i++) {
		if (val2 && val2 == hdc100x_int_time[chan][i]) {
			ret = hdc100x_update_config(data,
				hdc100x_resolution_shift[chan].mask << shift,
				i << shift);
			if (!ret)
				data->adc_int_us[chan] = val2;
			break;
		}
	}

	return ret;
}

static int hdc100x_get_measurement(struct hdc100x_data *data,
				   struct iio_chan_spec const *chan)
{
	struct i2c_client *client = data->client;
	int delay = data->adc_int_us[chan->address] + 1*USEC_PER_MSEC;
	int ret;
	__be16 val;

	/* start measurement */
	ret = i2c_smbus_write_byte(client, chan->address);
	if (ret < 0) {
		dev_err(&client->dev, "cannot start measurement");
		return ret;
	}

	/* wait for integration time to pass */
	usleep_range(delay, delay + 1000);

	/* read measurement */
	ret = i2c_master_recv(data->client, (char *)&val, sizeof(val));
	if (ret < 0) {
		dev_err(&client->dev, "cannot read sensor data\n");
		return ret;
	}
	return be16_to_cpu(val);
}

static int hdc100x_get_heater_status(struct hdc100x_data *data)
{
	return !!(data->config & HDC100X_REG_CONFIG_HEATER_EN);
}

static int hdc100x_read_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan, int *val,
			    int *val2, long mask)
{
	struct hdc100x_data *data = iio_priv(indio_dev);

	switch (mask) {
	case IIO_CHAN_INFO_RAW: {
		int ret;

		mutex_lock(&data->lock);
		if (chan->type == IIO_CURRENT) {
			*val = hdc100x_get_heater_status(data);
			ret = IIO_VAL_INT;
		} else {
			ret = iio_device_claim_direct_mode(indio_dev);
			if (ret) {
				mutex_unlock(&data->lock);
				return ret;
			}

			ret = hdc100x_get_measurement(data, chan);
			iio_device_release_direct_mode(indio_dev);
			if (ret >= 0) {
				*val = ret;
				ret = IIO_VAL_INT;
			}
		}
		mutex_unlock(&data->lock);
		return ret;
	}
	case IIO_CHAN_INFO_INT_TIME:
		*val = 0;
		*val2 = data->adc_int_us[chan->address];
		return IIO_VAL_INT_PLUS_MICRO;
	case IIO_CHAN_INFO_SCALE:
		if (chan->type == IIO_TEMP) {
			*val = 165000;
			*val2 = 65536;
			return IIO_VAL_FRACTIONAL;
		} else {
			*val = 100000;
			*val2 = 65536;
			return IIO_VAL_FRACTIONAL;
		}
		break;
	case IIO_CHAN_INFO_OFFSET:
		*val = -15887;
		*val2 = 515151;
		return IIO_VAL_INT_PLUS_MICRO;
	default:
		return -EINVAL;
	}
}

static int hdc100x_write_raw(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *chan,
			     int val, int val2, long mask)
{
	struct hdc100x_data *data = iio_priv(indio_dev);
	int ret = -EINVAL;

	switch (mask) {
	case IIO_CHAN_INFO_INT_TIME:
		if (val != 0)
			return -EINVAL;

		mutex_lock(&data->lock);
		ret = hdc100x_set_it_time(data, chan->address, val2);
		mutex_unlock(&data->lock);
		return ret;
	case IIO_CHAN_INFO_RAW:
		if (chan->type != IIO_CURRENT || val2 != 0)
			return -EINVAL;

		mutex_lock(&data->lock);
		ret = hdc100x_update_config(data, HDC100X_REG_CONFIG_HEATER_EN,
					val ? HDC100X_REG_CONFIG_HEATER_EN : 0);
		mutex_unlock(&data->lock);
		return ret;
	default:
		return -EINVAL;
	}
}

static int hdc100x_buffer_postenable(struct iio_dev *indio_dev)
{
	struct hdc100x_data *data = iio_priv(indio_dev);
	int ret;

	/* Buffer is enabled. First set ACQ Mode, then attach poll func */
	mutex_lock(&data->lock);
	ret = hdc100x_update_config(data, HDC100X_REG_CONFIG_ACQ_MODE,
				    HDC100X_REG_CONFIG_ACQ_MODE);
	mutex_unlock(&data->lock);

	return ret;
}

static int hdc100x_buffer_predisable(struct iio_dev *indio_dev)
{
	struct hdc100x_data *data = iio_priv(indio_dev);
	int ret;

	mutex_lock(&data->lock);
	ret = hdc100x_update_config(data, HDC100X_REG_CONFIG_ACQ_MODE, 0);
	mutex_unlock(&data->lock);

	return ret;
}

static const struct iio_buffer_setup_ops hdc_buffer_setup_ops = {
	.postenable  = hdc100x_buffer_postenable,
	.predisable  = hdc100x_buffer_predisable,
};

static irqreturn_t hdc100x_trigger_handler(int irq, void *p)
{
	struct iio_poll_func *pf = p;
	struct iio_dev *indio_dev = pf->indio_dev;
	struct hdc100x_data *data = iio_priv(indio_dev);
	struct i2c_client *client = data->client;
	int delay = data->adc_int_us[0] + data->adc_int_us[1] + 2*USEC_PER_MSEC;
	int ret;

	/* dual read starts at temp register */
	mutex_lock(&data->lock);
	ret = i2c_smbus_write_byte(client, HDC100X_REG_TEMP);
	if (ret < 0) {
		dev_err(&client->dev, "cannot start measurement\n");
		goto err;
	}
	usleep_range(delay, delay + 1000);

	ret = i2c_master_recv(client, (u8 *)data->scan.channels, 4);
	if (ret < 0) {
		dev_err(&client->dev, "cannot read sensor data\n");
		goto err;
	}

	iio_push_to_buffers_with_timestamp(indio_dev, &data->scan,
					   iio_get_time_ns(indio_dev));
err:
	mutex_unlock(&data->lock);
	iio_trigger_notify_done(indio_dev->trig);

	return IRQ_HANDLED;
}

static const struct iio_info hdc100x_info = {
	.read_raw = hdc100x_read_raw,
	.write_raw = hdc100x_write_raw,
	.attrs = &hdc100x_attribute_group,
};

static int hdc100x_probe(struct i2c_client *client)
{
	struct iio_dev *indio_dev;
	struct hdc100x_data *data;
	int ret;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_WORD_DATA |
				     I2C_FUNC_SMBUS_BYTE | I2C_FUNC_I2C))
		return -EOPNOTSUPP;

	indio_dev = devm_iio_device_alloc(&client->dev, sizeof(*data));
	if (!indio_dev)
		return -ENOMEM;

	data = iio_priv(indio_dev);
	i2c_set_clientdata(client, indio_dev);
	data->client = client;
	mutex_init(&data->lock);

	indio_dev->name = dev_name(&client->dev);
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->info = &hdc100x_info;

	indio_dev->channels = hdc100x_channels;
	indio_dev->num_channels = ARRAY_SIZE(hdc100x_channels);
	indio_dev->available_scan_masks = hdc100x_scan_masks;

	/* be sure we are in a known state */
	hdc100x_set_it_time(data, 0, hdc100x_int_time[0][0]);
	hdc100x_set_it_time(data, 1, hdc100x_int_time[1][0]);
	hdc100x_update_config(data, HDC100X_REG_CONFIG_ACQ_MODE, 0);

	ret = devm_iio_triggered_buffer_setup(&client->dev,
					 indio_dev, NULL,
					 hdc100x_trigger_handler,
					 &hdc_buffer_setup_ops);
	if (ret < 0) {
		dev_err(&client->dev, "iio triggered buffer setup failed\n");
		return ret;
	}

	return devm_iio_device_register(&client->dev, indio_dev);
}

static const struct i2c_device_id hdc100x_id[] = {
	{ "hdc100x" },
	{ "hdc1000" },
	{ "hdc1008" },
	{ "hdc1010" },
	{ "hdc1050" },
	{ "hdc1080" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, hdc100x_id);

static const struct of_device_id hdc100x_dt_ids[] = {
	{ .compatible = "ti,hdc1000" },
	{ .compatible = "ti,hdc1008" },
	{ .compatible = "ti,hdc1010" },
	{ .compatible = "ti,hdc1050" },
	{ .compatible = "ti,hdc1080" },
	{ }
};
MODULE_DEVICE_TABLE(of, hdc100x_dt_ids);

static const struct acpi_device_id hdc100x_acpi_match[] = {
	{ "TXNW1010" },
	{ }
};
MODULE_DEVICE_TABLE(acpi, hdc100x_acpi_match);

static struct i2c_driver hdc100x_driver = {
	.driver = {
		.name	= "hdc100x",
		.of_match_table = hdc100x_dt_ids,
		.acpi_match_table = hdc100x_acpi_match,
	},
	.probe = hdc100x_probe,
	.id_table = hdc100x_id,
};
module_i2c_driver(hdc100x_driver);

MODULE_AUTHOR("Matt Ranostay <matt.ranostay@konsulko.com>");
MODULE_DESCRIPTION("TI HDC100x humidity and temperature sensor driver");
MODULE_LICENSE("GPL");

/*
 * tcs3414.c - Support for TAOS TCS3414 digital color sensor
 *
 * Copyright (c) 2014 Peter Meerwald <pmeerw@pmeerw.net>
 *
 * This file is subject to the terms and conditions of version 2 of
 * the GNU General Public License.  See the file COPYING in the main
 * directory of this archive for more details.
 *
 * Digital color sensor with 16-bit channels for red, green, blue, clear);
 * 7-bit I2C slave address 0x39 (TCS3414) or 0x29, 0x49, 0x59 (TCS3413,
 * TCS3415, TCS3416, resp.)
 *
 * TODO: sync, interrupt support, thresholds, prescaler
 */

#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/pm.h>

#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/iio/buffer.h>
#include <linux/iio/triggered_buffer.h>

#define TCS3414_DRV_NAME "tcs3414"

#define TCS3414_COMMAND BIT(7)
#define TCS3414_COMMAND_WORD (TCS3414_COMMAND | BIT(5))

#define TCS3414_CONTROL (TCS3414_COMMAND | 0x00)
#define TCS3414_TIMING (TCS3414_COMMAND | 0x01)
#define TCS3414_ID (TCS3414_COMMAND | 0x04)
#define TCS3414_GAIN (TCS3414_COMMAND | 0x07)
#define TCS3414_DATA_GREEN (TCS3414_COMMAND_WORD | 0x10)
#define TCS3414_DATA_RED (TCS3414_COMMAND_WORD | 0x12)
#define TCS3414_DATA_BLUE (TCS3414_COMMAND_WORD | 0x14)
#define TCS3414_DATA_CLEAR (TCS3414_COMMAND_WORD | 0x16)

#define TCS3414_CONTROL_ADC_VALID BIT(4)
#define TCS3414_CONTROL_ADC_EN BIT(1)
#define TCS3414_CONTROL_POWER BIT(0)

#define TCS3414_INTEG_MASK GENMASK(1, 0)
#define TCS3414_INTEG_12MS 0x0
#define TCS3414_INTEG_100MS 0x1
#define TCS3414_INTEG_400MS 0x2

#define TCS3414_GAIN_MASK GENMASK(5, 4)
#define TCS3414_GAIN_SHIFT 4

struct tcs3414_data {
	struct i2c_client *client;
	u8 control;
	u8 gain;
	u8 timing;
	u16 buffer[8]; /* 4x 16-bit + 8 bytes timestamp */
};

#define TCS3414_CHANNEL(_color, _si, _addr) { \
	.type = IIO_INTENSITY, \
	.modified = 1, \
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW), \
	.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE) | \
		BIT(IIO_CHAN_INFO_INT_TIME), \
	.channel2 = IIO_MOD_LIGHT_##_color, \
	.address = _addr, \
	.scan_index = _si, \
	.scan_type = { \
		.sign = 'u', \
		.realbits = 16, \
		.storagebits = 16, \
		.endianness = IIO_CPU, \
	}, \
}

/* scale factors: 1/gain */
static const int tcs3414_scales[][2] = {
	{1, 0}, {0, 250000}, {0, 62500}, {0, 15625}
};

/* integration time in ms */
static const int tcs3414_times[] = { 12, 100, 400 };

static const struct iio_chan_spec tcs3414_channels[] = {
	TCS3414_CHANNEL(GREEN, 0, TCS3414_DATA_GREEN),
	TCS3414_CHANNEL(RED, 1, TCS3414_DATA_RED),
	TCS3414_CHANNEL(BLUE, 2, TCS3414_DATA_BLUE),
	TCS3414_CHANNEL(CLEAR, 3, TCS3414_DATA_CLEAR),
	IIO_CHAN_SOFT_TIMESTAMP(4),
};

static int tcs3414_req_data(struct tcs3414_data *data)
{
	int tries = 25;
	int ret;

	ret = i2c_smbus_write_byte_data(data->client, TCS3414_CONTROL,
		data->control | TCS3414_CONTROL_ADC_EN);
	if (ret < 0)
		return ret;

	while (tries--) {
		ret = i2c_smbus_read_byte_data(data->client, TCS3414_CONTROL);
		if (ret < 0)
			return ret;
		if (ret & TCS3414_CONTROL_ADC_VALID)
			break;
		msleep(20);
	}

	ret = i2c_smbus_write_byte_data(data->client, TCS3414_CONTROL,
		data->control);
	if (ret < 0)
		return ret;

	if (tries < 0) {
		dev_err(&data->client->dev, "data not ready\n");
		return -EIO;
	}

	return 0;
}

static int tcs3414_read_raw(struct iio_dev *indio_dev,
			   struct iio_chan_spec const *chan,
			   int *val, int *val2, long mask)
{
	struct tcs3414_data *data = iio_priv(indio_dev);
	int i, ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		ret = iio_device_claim_direct_mode(indio_dev);
		if (ret)
			return ret;
		ret = tcs3414_req_data(data);
		if (ret < 0) {
			iio_device_release_direct_mode(indio_dev);
			return ret;
		}
		ret = i2c_smbus_read_word_data(data->client, chan->address);
		iio_device_release_direct_mode(indio_dev);
		if (ret < 0)
			return ret;
		*val = ret;
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		i = (data->gain & TCS3414_GAIN_MASK) >> TCS3414_GAIN_SHIFT;
		*val = tcs3414_scales[i][0];
		*val2 = tcs3414_scales[i][1];
		return IIO_VAL_INT_PLUS_MICRO;
	case IIO_CHAN_INFO_INT_TIME:
		*val = 0;
		*val2 = tcs3414_times[data->timing & TCS3414_INTEG_MASK] * 1000;
		return IIO_VAL_INT_PLUS_MICRO;
	}
	return -EINVAL;
}

static int tcs3414_write_raw(struct iio_dev *indio_dev,
			       struct iio_chan_spec const *chan,
			       int val, int val2, long mask)
{
	struct tcs3414_data *data = iio_priv(indio_dev);
	int i;

	switch (mask) {
	case IIO_CHAN_INFO_SCALE:
		for (i = 0; i < ARRAY_SIZE(tcs3414_scales); i++) {
			if (val == tcs3414_scales[i][0] &&
				val2 == tcs3414_scales[i][1]) {
				data->gain &= ~TCS3414_GAIN_MASK;
				data->gain |= i << TCS3414_GAIN_SHIFT;
				return i2c_smbus_write_byte_data(
					data->client, TCS3414_GAIN,
					data->gain);
			}
		}
		return -EINVAL;
	case IIO_CHAN_INFO_INT_TIME:
		if (val != 0)
			return -EINVAL;
		for (i = 0; i < ARRAY_SIZE(tcs3414_times); i++) {
			if (val2 == tcs3414_times[i] * 1000) {
				data->timing &= ~TCS3414_INTEG_MASK;
				data->timing |= i;
				return i2c_smbus_write_byte_data(
					data->client, TCS3414_TIMING,
					data->timing);
			}
		}
		return -EINVAL;
	default:
		return -EINVAL;
	}
}

static irqreturn_t tcs3414_trigger_handler(int irq, void *p)
{
	struct iio_poll_func *pf = p;
	struct iio_dev *indio_dev = pf->indio_dev;
	struct tcs3414_data *data = iio_priv(indio_dev);
	int i, j = 0;

	for_each_set_bit(i, indio_dev->active_scan_mask,
		indio_dev->masklength) {
		int ret = i2c_smbus_read_word_data(data->client,
			TCS3414_DATA_GREEN + 2*i);
		if (ret < 0)
			goto done;

		data->buffer[j++] = ret;
	}

	iio_push_to_buffers_with_timestamp(indio_dev, data->buffer,
		iio_get_time_ns(indio_dev));

done:
	iio_trigger_notify_done(indio_dev->trig);

	return IRQ_HANDLED;
}

static IIO_CONST_ATTR(scale_available, "1 0.25 0.0625 0.015625");
static IIO_CONST_ATTR_INT_TIME_AVAIL("0.012 0.1 0.4");

static struct attribute *tcs3414_attributes[] = {
	&iio_const_attr_scale_available.dev_attr.attr,
	&iio_const_attr_integration_time_available.dev_attr.attr,
	NULL
};

static const struct attribute_group tcs3414_attribute_group = {
	.attrs = tcs3414_attributes,
};

static const struct iio_info tcs3414_info = {
	.read_raw = tcs3414_read_raw,
	.write_raw = tcs3414_write_raw,
	.attrs = &tcs3414_attribute_group,
	.driver_module = THIS_MODULE,
};

static int tcs3414_buffer_preenable(struct iio_dev *indio_dev)
{
	struct tcs3414_data *data = iio_priv(indio_dev);

	data->control |= TCS3414_CONTROL_ADC_EN;
	return i2c_smbus_write_byte_data(data->client, TCS3414_CONTROL,
		data->control);
}

static int tcs3414_buffer_predisable(struct iio_dev *indio_dev)
{
	struct tcs3414_data *data = iio_priv(indio_dev);
	int ret;

	ret = iio_triggered_buffer_predisable(indio_dev);
	if (ret < 0)
		return ret;

	data->control &= ~TCS3414_CONTROL_ADC_EN;
	return i2c_smbus_write_byte_data(data->client, TCS3414_CONTROL,
		data->control);
}

static const struct iio_buffer_setup_ops tcs3414_buffer_setup_ops = {
	.preenable = tcs3414_buffer_preenable,
	.postenable = &iio_triggered_buffer_postenable,
	.predisable = tcs3414_buffer_predisable,
};

static int tcs3414_probe(struct i2c_client *client,
			   const struct i2c_device_id *id)
{
	struct tcs3414_data *data;
	struct iio_dev *indio_dev;
	int ret;

	indio_dev = devm_iio_device_alloc(&client->dev, sizeof(*data));
	if (indio_dev == NULL)
		return -ENOMEM;

	data = iio_priv(indio_dev);
	i2c_set_clientdata(client, indio_dev);
	data->client = client;

	indio_dev->dev.parent = &client->dev;
	indio_dev->info = &tcs3414_info;
	indio_dev->name = TCS3414_DRV_NAME;
	indio_dev->channels = tcs3414_channels;
	indio_dev->num_channels = ARRAY_SIZE(tcs3414_channels);
	indio_dev->modes = INDIO_DIRECT_MODE;

	ret = i2c_smbus_read_byte_data(data->client, TCS3414_ID);
	if (ret < 0)
		return ret;

	switch (ret & 0xf0) {
	case 0x00:
		dev_info(&client->dev, "TCS3404 found\n");
		break;
	case 0x10:
		dev_info(&client->dev, "TCS3413/14/15/16 found\n");
		break;
	default:
		return -ENODEV;
	}

	data->control = TCS3414_CONTROL_POWER;
	ret = i2c_smbus_write_byte_data(data->client, TCS3414_CONTROL,
		data->control);
	if (ret < 0)
		return ret;

	data->timing = TCS3414_INTEG_12MS; /* free running */
	ret = i2c_smbus_write_byte_data(data->client, TCS3414_TIMING,
		data->timing);
	if (ret < 0)
		return ret;

	ret = i2c_smbus_read_byte_data(data->client, TCS3414_GAIN);
	if (ret < 0)
		return ret;
	data->gain = ret;

	ret = iio_triggered_buffer_setup(indio_dev, NULL,
		tcs3414_trigger_handler, &tcs3414_buffer_setup_ops);
	if (ret < 0)
		return ret;

	ret = iio_device_register(indio_dev);
	if (ret < 0)
		goto buffer_cleanup;

	return 0;

buffer_cleanup:
	iio_triggered_buffer_cleanup(indio_dev);
	return ret;
}

static int tcs3414_powerdown(struct tcs3414_data *data)
{
	return i2c_smbus_write_byte_data(data->client, TCS3414_CONTROL,
		data->control & ~(TCS3414_CONTROL_POWER |
		TCS3414_CONTROL_ADC_EN));
}

static int tcs3414_remove(struct i2c_client *client)
{
	struct iio_dev *indio_dev = i2c_get_clientdata(client);

	iio_device_unregister(indio_dev);
	iio_triggered_buffer_cleanup(indio_dev);
	tcs3414_powerdown(iio_priv(indio_dev));

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int tcs3414_suspend(struct device *dev)
{
	struct tcs3414_data *data = iio_priv(i2c_get_clientdata(
		to_i2c_client(dev)));
	return tcs3414_powerdown(data);
}

static int tcs3414_resume(struct device *dev)
{
	struct tcs3414_data *data = iio_priv(i2c_get_clientdata(
		to_i2c_client(dev)));
	return i2c_smbus_write_byte_data(data->client, TCS3414_CONTROL,
		data->control);
}
#endif

static SIMPLE_DEV_PM_OPS(tcs3414_pm_ops, tcs3414_suspend, tcs3414_resume);

static const struct i2c_device_id tcs3414_id[] = {
	{ "tcs3414", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, tcs3414_id);

static struct i2c_driver tcs3414_driver = {
	.driver = {
		.name	= TCS3414_DRV_NAME,
		.pm	= &tcs3414_pm_ops,
	},
	.probe		= tcs3414_probe,
	.remove		= tcs3414_remove,
	.id_table	= tcs3414_id,
};
module_i2c_driver(tcs3414_driver);

MODULE_AUTHOR("Peter Meerwald <pmeerw@pmeerw.net>");
MODULE_DESCRIPTION("TCS3414 digital color sensors driver");
MODULE_LICENSE("GPL");

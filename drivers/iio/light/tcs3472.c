// SPDX-License-Identifier: GPL-2.0-only
/*
 * tcs3472.c - Support for TAOS TCS3472 color light-to-digital converter
 *
 * Copyright (c) 2013 Peter Meerwald <pmeerw@pmeerw.net>
 *
 * Color light sensor with 16-bit channels for red, green, blue, clear);
 * 7-bit I2C slave address 0x39 (TCS34721, TCS34723) or 0x29 (TCS34725,
 * TCS34727)
 *
 * Datasheet: http://ams.com/eng/content/download/319364/1117183/file/TCS3472_Datasheet_EN_v2.pdf
 *
 * TODO: wait time
 */

#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/pm.h>

#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/events.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/iio/buffer.h>
#include <linux/iio/triggered_buffer.h>

#define TCS3472_DRV_NAME "tcs3472"

#define TCS3472_COMMAND BIT(7)
#define TCS3472_AUTO_INCR BIT(5)
#define TCS3472_SPECIAL_FUNC (BIT(5) | BIT(6))

#define TCS3472_INTR_CLEAR (TCS3472_COMMAND | TCS3472_SPECIAL_FUNC | 0x06)

#define TCS3472_ENABLE (TCS3472_COMMAND | 0x00)
#define TCS3472_ATIME (TCS3472_COMMAND | 0x01)
#define TCS3472_WTIME (TCS3472_COMMAND | 0x03)
#define TCS3472_AILT (TCS3472_COMMAND | TCS3472_AUTO_INCR | 0x04)
#define TCS3472_AIHT (TCS3472_COMMAND | TCS3472_AUTO_INCR | 0x06)
#define TCS3472_PERS (TCS3472_COMMAND | 0x0c)
#define TCS3472_CONFIG (TCS3472_COMMAND | 0x0d)
#define TCS3472_CONTROL (TCS3472_COMMAND | 0x0f)
#define TCS3472_ID (TCS3472_COMMAND | 0x12)
#define TCS3472_STATUS (TCS3472_COMMAND | 0x13)
#define TCS3472_CDATA (TCS3472_COMMAND | TCS3472_AUTO_INCR | 0x14)
#define TCS3472_RDATA (TCS3472_COMMAND | TCS3472_AUTO_INCR | 0x16)
#define TCS3472_GDATA (TCS3472_COMMAND | TCS3472_AUTO_INCR | 0x18)
#define TCS3472_BDATA (TCS3472_COMMAND | TCS3472_AUTO_INCR | 0x1a)

#define TCS3472_STATUS_AINT BIT(4)
#define TCS3472_STATUS_AVALID BIT(0)
#define TCS3472_ENABLE_AIEN BIT(4)
#define TCS3472_ENABLE_AEN BIT(1)
#define TCS3472_ENABLE_PON BIT(0)
#define TCS3472_CONTROL_AGAIN_MASK (BIT(0) | BIT(1))

struct tcs3472_data {
	struct i2c_client *client;
	struct mutex lock;
	u16 low_thresh;
	u16 high_thresh;
	u8 enable;
	u8 control;
	u8 atime;
	u8 apers;
	u16 buffer[8]; /* 4 16-bit channels + 64-bit timestamp */
};

static const struct iio_event_spec tcs3472_events[] = {
	{
		.type = IIO_EV_TYPE_THRESH,
		.dir = IIO_EV_DIR_RISING,
		.mask_separate = BIT(IIO_EV_INFO_VALUE),
	}, {
		.type = IIO_EV_TYPE_THRESH,
		.dir = IIO_EV_DIR_FALLING,
		.mask_separate = BIT(IIO_EV_INFO_VALUE),
	}, {
		.type = IIO_EV_TYPE_THRESH,
		.dir = IIO_EV_DIR_EITHER,
		.mask_separate = BIT(IIO_EV_INFO_ENABLE) |
				 BIT(IIO_EV_INFO_PERIOD),
	},
};

#define TCS3472_CHANNEL(_color, _si, _addr) { \
	.type = IIO_INTENSITY, \
	.modified = 1, \
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW), \
	.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_CALIBSCALE) | \
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
	.event_spec = _si ? NULL : tcs3472_events, \
	.num_event_specs = _si ? 0 : ARRAY_SIZE(tcs3472_events), \
}

static const int tcs3472_agains[] = { 1, 4, 16, 60 };

static const struct iio_chan_spec tcs3472_channels[] = {
	TCS3472_CHANNEL(CLEAR, 0, TCS3472_CDATA),
	TCS3472_CHANNEL(RED, 1, TCS3472_RDATA),
	TCS3472_CHANNEL(GREEN, 2, TCS3472_GDATA),
	TCS3472_CHANNEL(BLUE, 3, TCS3472_BDATA),
	IIO_CHAN_SOFT_TIMESTAMP(4),
};

static int tcs3472_req_data(struct tcs3472_data *data)
{
	int tries = 50;
	int ret;

	while (tries--) {
		ret = i2c_smbus_read_byte_data(data->client, TCS3472_STATUS);
		if (ret < 0)
			return ret;
		if (ret & TCS3472_STATUS_AVALID)
			break;
		msleep(20);
	}

	if (tries < 0) {
		dev_err(&data->client->dev, "data not ready\n");
		return -EIO;
	}

	return 0;
}

static int tcs3472_read_raw(struct iio_dev *indio_dev,
			   struct iio_chan_spec const *chan,
			   int *val, int *val2, long mask)
{
	struct tcs3472_data *data = iio_priv(indio_dev);
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		ret = iio_device_claim_direct_mode(indio_dev);
		if (ret)
			return ret;
		ret = tcs3472_req_data(data);
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
	case IIO_CHAN_INFO_CALIBSCALE:
		*val = tcs3472_agains[data->control &
			TCS3472_CONTROL_AGAIN_MASK];
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_INT_TIME:
		*val = 0;
		*val2 = (256 - data->atime) * 2400;
		return IIO_VAL_INT_PLUS_MICRO;
	}
	return -EINVAL;
}

static int tcs3472_write_raw(struct iio_dev *indio_dev,
			       struct iio_chan_spec const *chan,
			       int val, int val2, long mask)
{
	struct tcs3472_data *data = iio_priv(indio_dev);
	int i;

	switch (mask) {
	case IIO_CHAN_INFO_CALIBSCALE:
		if (val2 != 0)
			return -EINVAL;
		for (i = 0; i < ARRAY_SIZE(tcs3472_agains); i++) {
			if (val == tcs3472_agains[i]) {
				data->control &= ~TCS3472_CONTROL_AGAIN_MASK;
				data->control |= i;
				return i2c_smbus_write_byte_data(
					data->client, TCS3472_CONTROL,
					data->control);
			}
		}
		return -EINVAL;
	case IIO_CHAN_INFO_INT_TIME:
		if (val != 0)
			return -EINVAL;
		for (i = 0; i < 256; i++) {
			if (val2 == (256 - i) * 2400) {
				data->atime = i;
				return i2c_smbus_write_byte_data(
					data->client, TCS3472_ATIME,
					data->atime);
			}

		}
		return -EINVAL;
	}
	return -EINVAL;
}

/*
 * Translation from APERS field value to the number of consecutive out-of-range
 * clear channel values before an interrupt is generated
 */
static const int tcs3472_intr_pers[] = {
	0, 1, 2, 3, 5, 10, 15, 20, 25, 30, 35, 40, 45, 50, 55, 60
};

static int tcs3472_read_event(struct iio_dev *indio_dev,
	const struct iio_chan_spec *chan, enum iio_event_type type,
	enum iio_event_direction dir, enum iio_event_info info, int *val,
	int *val2)
{
	struct tcs3472_data *data = iio_priv(indio_dev);
	int ret;
	unsigned int period;

	mutex_lock(&data->lock);

	switch (info) {
	case IIO_EV_INFO_VALUE:
		*val = (dir == IIO_EV_DIR_RISING) ?
			data->high_thresh : data->low_thresh;
		ret = IIO_VAL_INT;
		break;
	case IIO_EV_INFO_PERIOD:
		period = (256 - data->atime) * 2400 *
			tcs3472_intr_pers[data->apers];
		*val = period / USEC_PER_SEC;
		*val2 = period % USEC_PER_SEC;
		ret = IIO_VAL_INT_PLUS_MICRO;
		break;
	default:
		ret = -EINVAL;
		break;
	}

	mutex_unlock(&data->lock);

	return ret;
}

static int tcs3472_write_event(struct iio_dev *indio_dev,
	const struct iio_chan_spec *chan, enum iio_event_type type,
	enum iio_event_direction dir, enum iio_event_info info, int val,
	int val2)
{
	struct tcs3472_data *data = iio_priv(indio_dev);
	int ret;
	u8 command;
	int period;
	int i;

	mutex_lock(&data->lock);
	switch (info) {
	case IIO_EV_INFO_VALUE:
		switch (dir) {
		case IIO_EV_DIR_RISING:
			command = TCS3472_AIHT;
			break;
		case IIO_EV_DIR_FALLING:
			command = TCS3472_AILT;
			break;
		default:
			ret = -EINVAL;
			goto error;
		}
		ret = i2c_smbus_write_word_data(data->client, command, val);
		if (ret)
			goto error;

		if (dir == IIO_EV_DIR_RISING)
			data->high_thresh = val;
		else
			data->low_thresh = val;
		break;
	case IIO_EV_INFO_PERIOD:
		period = val * USEC_PER_SEC + val2;
		for (i = 1; i < ARRAY_SIZE(tcs3472_intr_pers) - 1; i++) {
			if (period <= (256 - data->atime) * 2400 *
					tcs3472_intr_pers[i])
				break;
		}
		ret = i2c_smbus_write_byte_data(data->client, TCS3472_PERS, i);
		if (ret)
			goto error;

		data->apers = i;
		break;
	default:
		ret = -EINVAL;
		break;
	}
error:
	mutex_unlock(&data->lock);

	return ret;
}

static int tcs3472_read_event_config(struct iio_dev *indio_dev,
	const struct iio_chan_spec *chan, enum iio_event_type type,
	enum iio_event_direction dir)
{
	struct tcs3472_data *data = iio_priv(indio_dev);
	int ret;

	mutex_lock(&data->lock);
	ret = !!(data->enable & TCS3472_ENABLE_AIEN);
	mutex_unlock(&data->lock);

	return ret;
}

static int tcs3472_write_event_config(struct iio_dev *indio_dev,
	const struct iio_chan_spec *chan, enum iio_event_type type,
	enum iio_event_direction dir, int state)
{
	struct tcs3472_data *data = iio_priv(indio_dev);
	int ret = 0;
	u8 enable_old;

	mutex_lock(&data->lock);

	enable_old = data->enable;

	if (state)
		data->enable |= TCS3472_ENABLE_AIEN;
	else
		data->enable &= ~TCS3472_ENABLE_AIEN;

	if (enable_old != data->enable) {
		ret = i2c_smbus_write_byte_data(data->client, TCS3472_ENABLE,
						data->enable);
		if (ret)
			data->enable = enable_old;
	}
	mutex_unlock(&data->lock);

	return ret;
}

static irqreturn_t tcs3472_event_handler(int irq, void *priv)
{
	struct iio_dev *indio_dev = priv;
	struct tcs3472_data *data = iio_priv(indio_dev);
	int ret;

	ret = i2c_smbus_read_byte_data(data->client, TCS3472_STATUS);
	if (ret >= 0 && (ret & TCS3472_STATUS_AINT)) {
		iio_push_event(indio_dev, IIO_UNMOD_EVENT_CODE(IIO_INTENSITY, 0,
						IIO_EV_TYPE_THRESH,
						IIO_EV_DIR_EITHER),
				iio_get_time_ns(indio_dev));

		i2c_smbus_read_byte_data(data->client, TCS3472_INTR_CLEAR);
	}

	return IRQ_HANDLED;
}

static irqreturn_t tcs3472_trigger_handler(int irq, void *p)
{
	struct iio_poll_func *pf = p;
	struct iio_dev *indio_dev = pf->indio_dev;
	struct tcs3472_data *data = iio_priv(indio_dev);
	int i, j = 0;

	int ret = tcs3472_req_data(data);
	if (ret < 0)
		goto done;

	for_each_set_bit(i, indio_dev->active_scan_mask,
		indio_dev->masklength) {
		ret = i2c_smbus_read_word_data(data->client,
			TCS3472_CDATA + 2*i);
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

static ssize_t tcs3472_show_int_time_available(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	size_t len = 0;
	int i;

	for (i = 1; i <= 256; i++)
		len += scnprintf(buf + len, PAGE_SIZE - len, "0.%06d ",
			2400 * i);

	/* replace trailing space by newline */
	buf[len - 1] = '\n';

	return len;
}

static IIO_CONST_ATTR(calibscale_available, "1 4 16 60");
static IIO_DEV_ATTR_INT_TIME_AVAIL(tcs3472_show_int_time_available);

static struct attribute *tcs3472_attributes[] = {
	&iio_const_attr_calibscale_available.dev_attr.attr,
	&iio_dev_attr_integration_time_available.dev_attr.attr,
	NULL
};

static const struct attribute_group tcs3472_attribute_group = {
	.attrs = tcs3472_attributes,
};

static const struct iio_info tcs3472_info = {
	.read_raw = tcs3472_read_raw,
	.write_raw = tcs3472_write_raw,
	.read_event_value = tcs3472_read_event,
	.write_event_value = tcs3472_write_event,
	.read_event_config = tcs3472_read_event_config,
	.write_event_config = tcs3472_write_event_config,
	.attrs = &tcs3472_attribute_group,
};

static int tcs3472_probe(struct i2c_client *client,
			   const struct i2c_device_id *id)
{
	struct tcs3472_data *data;
	struct iio_dev *indio_dev;
	int ret;

	indio_dev = devm_iio_device_alloc(&client->dev, sizeof(*data));
	if (indio_dev == NULL)
		return -ENOMEM;

	data = iio_priv(indio_dev);
	i2c_set_clientdata(client, indio_dev);
	data->client = client;
	mutex_init(&data->lock);

	indio_dev->dev.parent = &client->dev;
	indio_dev->info = &tcs3472_info;
	indio_dev->name = TCS3472_DRV_NAME;
	indio_dev->channels = tcs3472_channels;
	indio_dev->num_channels = ARRAY_SIZE(tcs3472_channels);
	indio_dev->modes = INDIO_DIRECT_MODE;

	ret = i2c_smbus_read_byte_data(data->client, TCS3472_ID);
	if (ret < 0)
		return ret;

	if (ret == 0x44)
		dev_info(&client->dev, "TCS34721/34725 found\n");
	else if (ret == 0x4d)
		dev_info(&client->dev, "TCS34723/34727 found\n");
	else
		return -ENODEV;

	ret = i2c_smbus_read_byte_data(data->client, TCS3472_CONTROL);
	if (ret < 0)
		return ret;
	data->control = ret;

	ret = i2c_smbus_read_byte_data(data->client, TCS3472_ATIME);
	if (ret < 0)
		return ret;
	data->atime = ret;

	ret = i2c_smbus_read_word_data(data->client, TCS3472_AILT);
	if (ret < 0)
		return ret;
	data->low_thresh = ret;

	ret = i2c_smbus_read_word_data(data->client, TCS3472_AIHT);
	if (ret < 0)
		return ret;
	data->high_thresh = ret;

	data->apers = 1;
	ret = i2c_smbus_write_byte_data(data->client, TCS3472_PERS,
					data->apers);
	if (ret < 0)
		return ret;

	ret = i2c_smbus_read_byte_data(data->client, TCS3472_ENABLE);
	if (ret < 0)
		return ret;

	/* enable device */
	data->enable = ret | TCS3472_ENABLE_PON | TCS3472_ENABLE_AEN;
	data->enable &= ~TCS3472_ENABLE_AIEN;
	ret = i2c_smbus_write_byte_data(data->client, TCS3472_ENABLE,
		data->enable);
	if (ret < 0)
		return ret;

	ret = iio_triggered_buffer_setup(indio_dev, NULL,
		tcs3472_trigger_handler, NULL);
	if (ret < 0)
		return ret;

	if (client->irq) {
		ret = request_threaded_irq(client->irq, NULL,
					   tcs3472_event_handler,
					   IRQF_TRIGGER_FALLING | IRQF_SHARED |
					   IRQF_ONESHOT,
					   client->name, indio_dev);
		if (ret)
			goto buffer_cleanup;
	}

	ret = iio_device_register(indio_dev);
	if (ret < 0)
		goto free_irq;

	return 0;

free_irq:
	free_irq(client->irq, indio_dev);
buffer_cleanup:
	iio_triggered_buffer_cleanup(indio_dev);
	return ret;
}

static int tcs3472_powerdown(struct tcs3472_data *data)
{
	int ret;
	u8 enable_mask = TCS3472_ENABLE_AEN | TCS3472_ENABLE_PON;

	mutex_lock(&data->lock);

	ret = i2c_smbus_write_byte_data(data->client, TCS3472_ENABLE,
		data->enable & ~enable_mask);
	if (!ret)
		data->enable &= ~enable_mask;

	mutex_unlock(&data->lock);

	return ret;
}

static int tcs3472_remove(struct i2c_client *client)
{
	struct iio_dev *indio_dev = i2c_get_clientdata(client);

	iio_device_unregister(indio_dev);
	free_irq(client->irq, indio_dev);
	iio_triggered_buffer_cleanup(indio_dev);
	tcs3472_powerdown(iio_priv(indio_dev));

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int tcs3472_suspend(struct device *dev)
{
	struct tcs3472_data *data = iio_priv(i2c_get_clientdata(
		to_i2c_client(dev)));
	return tcs3472_powerdown(data);
}

static int tcs3472_resume(struct device *dev)
{
	struct tcs3472_data *data = iio_priv(i2c_get_clientdata(
		to_i2c_client(dev)));
	int ret;
	u8 enable_mask = TCS3472_ENABLE_AEN | TCS3472_ENABLE_PON;

	mutex_lock(&data->lock);

	ret = i2c_smbus_write_byte_data(data->client, TCS3472_ENABLE,
		data->enable | enable_mask);
	if (!ret)
		data->enable |= enable_mask;

	mutex_unlock(&data->lock);

	return ret;
}
#endif

static SIMPLE_DEV_PM_OPS(tcs3472_pm_ops, tcs3472_suspend, tcs3472_resume);

static const struct i2c_device_id tcs3472_id[] = {
	{ "tcs3472", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, tcs3472_id);

static struct i2c_driver tcs3472_driver = {
	.driver = {
		.name	= TCS3472_DRV_NAME,
		.pm	= &tcs3472_pm_ops,
	},
	.probe		= tcs3472_probe,
	.remove		= tcs3472_remove,
	.id_table	= tcs3472_id,
};
module_i2c_driver(tcs3472_driver);

MODULE_AUTHOR("Peter Meerwald <pmeerw@pmeerw.net>");
MODULE_DESCRIPTION("TCS3472 color light sensors driver");
MODULE_LICENSE("GPL");

/*
 * ltr501.c - Support for Lite-On LTR501 ambient light and proximity sensor
 *
 * Copyright 2014 Peter Meerwald <pmeerw@pmeerw.net>
 *
 * This file is subject to the terms and conditions of version 2 of
 * the GNU General Public License.  See the file COPYING in the main
 * directory of this archive for more details.
 *
 * 7-bit I2C slave address 0x23
 *
 * TODO: interrupt, threshold, measurement rate, IR LED characteristics
 */

#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/err.h>
#include <linux/delay.h>

#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/iio/buffer.h>
#include <linux/iio/triggered_buffer.h>

#define LTR501_DRV_NAME "ltr501"

#define LTR501_ALS_CONTR 0x80 /* ALS operation mode, SW reset */
#define LTR501_PS_CONTR 0x81 /* PS operation mode */
#define LTR501_PART_ID 0x86
#define LTR501_MANUFAC_ID 0x87
#define LTR501_ALS_DATA1 0x88 /* 16-bit, little endian */
#define LTR501_ALS_DATA0 0x8a /* 16-bit, little endian */
#define LTR501_ALS_PS_STATUS 0x8c
#define LTR501_PS_DATA 0x8d /* 16-bit, little endian */

#define LTR501_ALS_CONTR_SW_RESET BIT(2)
#define LTR501_CONTR_PS_GAIN_MASK (BIT(3) | BIT(2))
#define LTR501_CONTR_PS_GAIN_SHIFT 2
#define LTR501_CONTR_ALS_GAIN_MASK BIT(3)
#define LTR501_CONTR_ACTIVE BIT(1)

#define LTR501_STATUS_ALS_RDY BIT(2)
#define LTR501_STATUS_PS_RDY BIT(0)

#define LTR501_PS_DATA_MASK 0x7ff

struct ltr501_data {
	struct i2c_client *client;
	struct mutex lock_als, lock_ps;
	u8 als_contr, ps_contr;
};

static int ltr501_drdy(struct ltr501_data *data, u8 drdy_mask)
{
	int tries = 100;
	int ret;

	while (tries--) {
		ret = i2c_smbus_read_byte_data(data->client,
			LTR501_ALS_PS_STATUS);
		if (ret < 0)
			return ret;
		if ((ret & drdy_mask) == drdy_mask)
			return 0;
		msleep(25);
	}

	dev_err(&data->client->dev, "ltr501_drdy() failed, data not ready\n");
	return -EIO;
}

static int ltr501_read_als(struct ltr501_data *data, __le16 buf[2])
{
	int ret = ltr501_drdy(data, LTR501_STATUS_ALS_RDY);
	if (ret < 0)
		return ret;
	/* always read both ALS channels in given order */
	return i2c_smbus_read_i2c_block_data(data->client,
		LTR501_ALS_DATA1, 2 * sizeof(__le16), (u8 *) buf);
}

static int ltr501_read_ps(struct ltr501_data *data)
{
	int ret = ltr501_drdy(data, LTR501_STATUS_PS_RDY);
	if (ret < 0)
		return ret;
	return i2c_smbus_read_word_data(data->client, LTR501_PS_DATA);
}

#define LTR501_INTENSITY_CHANNEL(_idx, _addr, _mod, _shared) { \
	.type = IIO_INTENSITY, \
	.modified = 1, \
	.address = (_addr), \
	.channel2 = (_mod), \
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW), \
	.info_mask_shared_by_type = (_shared), \
	.scan_index = (_idx), \
	.scan_type = { \
		.sign = 'u', \
		.realbits = 16, \
		.storagebits = 16, \
		.endianness = IIO_CPU, \
	} \
}

static const struct iio_chan_spec ltr501_channels[] = {
	LTR501_INTENSITY_CHANNEL(0, LTR501_ALS_DATA0, IIO_MOD_LIGHT_BOTH, 0),
	LTR501_INTENSITY_CHANNEL(1, LTR501_ALS_DATA1, IIO_MOD_LIGHT_IR,
		BIT(IIO_CHAN_INFO_SCALE)),
	{
		.type = IIO_PROXIMITY,
		.address = LTR501_PS_DATA,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
			BIT(IIO_CHAN_INFO_SCALE),
		.scan_index = 2,
		.scan_type = {
			.sign = 'u',
			.realbits = 11,
			.storagebits = 16,
			.endianness = IIO_CPU,
		},
	},
	IIO_CHAN_SOFT_TIMESTAMP(3),
};

static const int ltr501_ps_gain[4][2] = {
	{1, 0}, {0, 250000}, {0, 125000}, {0, 62500}
};

static int ltr501_read_raw(struct iio_dev *indio_dev,
				struct iio_chan_spec const *chan,
				int *val, int *val2, long mask)
{
	struct ltr501_data *data = iio_priv(indio_dev);
	__le16 buf[2];
	int ret, i;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		if (iio_buffer_enabled(indio_dev))
			return -EBUSY;

		switch (chan->type) {
		case IIO_INTENSITY:
			mutex_lock(&data->lock_als);
			ret = ltr501_read_als(data, buf);
			mutex_unlock(&data->lock_als);
			if (ret < 0)
				return ret;
			*val = le16_to_cpu(chan->address == LTR501_ALS_DATA1 ?
				buf[0] : buf[1]);
			return IIO_VAL_INT;
		case IIO_PROXIMITY:
			mutex_lock(&data->lock_ps);
			ret = ltr501_read_ps(data);
			mutex_unlock(&data->lock_ps);
			if (ret < 0)
				return ret;
			*val = ret & LTR501_PS_DATA_MASK;
			return IIO_VAL_INT;
		default:
			return -EINVAL;
		}
	case IIO_CHAN_INFO_SCALE:
		switch (chan->type) {
		case IIO_INTENSITY:
			if (data->als_contr & LTR501_CONTR_ALS_GAIN_MASK) {
				*val = 0;
				*val2 = 5000;
				return IIO_VAL_INT_PLUS_MICRO;
			} else {
				*val = 1;
				*val2 = 0;
				return IIO_VAL_INT;
			}
		case IIO_PROXIMITY:
			i = (data->ps_contr & LTR501_CONTR_PS_GAIN_MASK) >>
				LTR501_CONTR_PS_GAIN_SHIFT;
			*val = ltr501_ps_gain[i][0];
			*val2 = ltr501_ps_gain[i][1];
			return IIO_VAL_INT_PLUS_MICRO;
		default:
			return -EINVAL;
		}
	}
	return -EINVAL;
}

static int ltr501_get_ps_gain_index(int val, int val2)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(ltr501_ps_gain); i++)
		if (val == ltr501_ps_gain[i][0] && val2 == ltr501_ps_gain[i][1])
			return i;

	return -1;
}

static int ltr501_write_raw(struct iio_dev *indio_dev,
			       struct iio_chan_spec const *chan,
			       int val, int val2, long mask)
{
	struct ltr501_data *data = iio_priv(indio_dev);
	int i;

	if (iio_buffer_enabled(indio_dev))
		return -EBUSY;

	switch (mask) {
	case IIO_CHAN_INFO_SCALE:
		switch (chan->type) {
		case IIO_INTENSITY:
			if (val == 0 && val2 == 5000)
				data->als_contr |= LTR501_CONTR_ALS_GAIN_MASK;
			else if (val == 1 && val2 == 0)
				data->als_contr &= ~LTR501_CONTR_ALS_GAIN_MASK;
			else
				return -EINVAL;
			return i2c_smbus_write_byte_data(data->client,
				LTR501_ALS_CONTR, data->als_contr);
		case IIO_PROXIMITY:
			i = ltr501_get_ps_gain_index(val, val2);
			if (i < 0)
				return -EINVAL;
			data->ps_contr &= ~LTR501_CONTR_PS_GAIN_MASK;
			data->ps_contr |= i << LTR501_CONTR_PS_GAIN_SHIFT;
			return i2c_smbus_write_byte_data(data->client,
				LTR501_PS_CONTR, data->ps_contr);
		default:
			return -EINVAL;
		}
	}
	return -EINVAL;
}

static IIO_CONST_ATTR(in_proximity_scale_available, "1 0.25 0.125 0.0625");
static IIO_CONST_ATTR(in_intensity_scale_available, "1 0.005");

static struct attribute *ltr501_attributes[] = {
	&iio_const_attr_in_proximity_scale_available.dev_attr.attr,
	&iio_const_attr_in_intensity_scale_available.dev_attr.attr,
	NULL
};

static const struct attribute_group ltr501_attribute_group = {
	.attrs = ltr501_attributes,
};

static const struct iio_info ltr501_info = {
	.read_raw = ltr501_read_raw,
	.write_raw = ltr501_write_raw,
	.attrs = &ltr501_attribute_group,
	.driver_module = THIS_MODULE,
};

static int ltr501_write_contr(struct i2c_client *client, u8 als_val, u8 ps_val)
{
	int ret = i2c_smbus_write_byte_data(client, LTR501_ALS_CONTR, als_val);
	if (ret < 0)
		return ret;

	return i2c_smbus_write_byte_data(client, LTR501_PS_CONTR, ps_val);
}

static irqreturn_t ltr501_trigger_handler(int irq, void *p)
{
	struct iio_poll_func *pf = p;
	struct iio_dev *indio_dev = pf->indio_dev;
	struct ltr501_data *data = iio_priv(indio_dev);
	u16 buf[8];
	__le16 als_buf[2];
	u8 mask = 0;
	int j = 0;
	int ret;

	memset(buf, 0, sizeof(buf));

	/* figure out which data needs to be ready */
	if (test_bit(0, indio_dev->active_scan_mask) ||
		test_bit(1, indio_dev->active_scan_mask))
		mask |= LTR501_STATUS_ALS_RDY;
	if (test_bit(2, indio_dev->active_scan_mask))
		mask |= LTR501_STATUS_PS_RDY;

	ret = ltr501_drdy(data, mask);
	if (ret < 0)
		goto done;

	if (mask & LTR501_STATUS_ALS_RDY) {
		ret = i2c_smbus_read_i2c_block_data(data->client,
			LTR501_ALS_DATA1, sizeof(als_buf), (u8 *) als_buf);
		if (ret < 0)
			return ret;
		if (test_bit(0, indio_dev->active_scan_mask))
			buf[j++] = le16_to_cpu(als_buf[1]);
		if (test_bit(1, indio_dev->active_scan_mask))
			buf[j++] = le16_to_cpu(als_buf[0]);
	}

	if (mask & LTR501_STATUS_PS_RDY) {
		ret = i2c_smbus_read_word_data(data->client, LTR501_PS_DATA);
		if (ret < 0)
			goto done;
		buf[j++] = ret & LTR501_PS_DATA_MASK;
	}

	iio_push_to_buffers_with_timestamp(indio_dev, buf,
		iio_get_time_ns());

done:
	iio_trigger_notify_done(indio_dev->trig);

	return IRQ_HANDLED;
}

static int ltr501_init(struct ltr501_data *data)
{
	int ret;

	ret = i2c_smbus_read_byte_data(data->client, LTR501_ALS_CONTR);
	if (ret < 0)
		return ret;
	data->als_contr = ret | LTR501_CONTR_ACTIVE;

	ret = i2c_smbus_read_byte_data(data->client, LTR501_PS_CONTR);
	if (ret < 0)
		return ret;
	data->ps_contr = ret | LTR501_CONTR_ACTIVE;

	return ltr501_write_contr(data->client, data->als_contr,
		data->ps_contr);
}

static int ltr501_powerdown(struct ltr501_data *data)
{
	return ltr501_write_contr(data->client,
				  data->als_contr & ~LTR501_CONTR_ACTIVE,
				  data->ps_contr & ~LTR501_CONTR_ACTIVE);
}

static int ltr501_probe(struct i2c_client *client,
			  const struct i2c_device_id *id)
{
	struct ltr501_data *data;
	struct iio_dev *indio_dev;
	int ret;

	indio_dev = devm_iio_device_alloc(&client->dev, sizeof(*data));
	if (!indio_dev)
		return -ENOMEM;

	data = iio_priv(indio_dev);
	i2c_set_clientdata(client, indio_dev);
	data->client = client;
	mutex_init(&data->lock_als);
	mutex_init(&data->lock_ps);

	ret = i2c_smbus_read_byte_data(data->client, LTR501_PART_ID);
	if (ret < 0)
		return ret;
	if ((ret >> 4) != 0x8)
		return -ENODEV;

	indio_dev->dev.parent = &client->dev;
	indio_dev->info = &ltr501_info;
	indio_dev->channels = ltr501_channels;
	indio_dev->num_channels = ARRAY_SIZE(ltr501_channels);
	indio_dev->name = LTR501_DRV_NAME;
	indio_dev->modes = INDIO_DIRECT_MODE;

	ret = ltr501_init(data);
	if (ret < 0)
		return ret;

	ret = iio_triggered_buffer_setup(indio_dev, NULL,
		ltr501_trigger_handler, NULL);
	if (ret)
		goto powerdown_on_error;

	ret = iio_device_register(indio_dev);
	if (ret)
		goto error_unreg_buffer;

	return 0;

error_unreg_buffer:
	iio_triggered_buffer_cleanup(indio_dev);
powerdown_on_error:
	ltr501_powerdown(data);
	return ret;
}

static int ltr501_remove(struct i2c_client *client)
{
	struct iio_dev *indio_dev = i2c_get_clientdata(client);

	iio_device_unregister(indio_dev);
	iio_triggered_buffer_cleanup(indio_dev);
	ltr501_powerdown(iio_priv(indio_dev));

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int ltr501_suspend(struct device *dev)
{
	struct ltr501_data *data = iio_priv(i2c_get_clientdata(
		to_i2c_client(dev)));
	return ltr501_powerdown(data);
}

static int ltr501_resume(struct device *dev)
{
	struct ltr501_data *data = iio_priv(i2c_get_clientdata(
		to_i2c_client(dev)));

	return ltr501_write_contr(data->client, data->als_contr,
		data->ps_contr);
}
#endif

static SIMPLE_DEV_PM_OPS(ltr501_pm_ops, ltr501_suspend, ltr501_resume);

static const struct i2c_device_id ltr501_id[] = {
	{ "ltr501", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ltr501_id);

static struct i2c_driver ltr501_driver = {
	.driver = {
		.name   = LTR501_DRV_NAME,
		.pm	= &ltr501_pm_ops,
		.owner  = THIS_MODULE,
	},
	.probe  = ltr501_probe,
	.remove	= ltr501_remove,
	.id_table = ltr501_id,
};

module_i2c_driver(ltr501_driver);

MODULE_AUTHOR("Peter Meerwald <pmeerw@pmeerw.net>");
MODULE_DESCRIPTION("Lite-On LTR501 ambient light and proximity sensor driver");
MODULE_LICENSE("GPL");

// SPDX-License-Identifier: GPL-2.0
/*
 * Sensirion SPS30 particulate matter sensor driver
 *
 * Copyright (c) Tomasz Duszynski <tduszyns@gmail.com>
 */

#include <linux/crc8.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/iio/buffer.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/iio/triggered_buffer.h>
#include <linux/kernel.h>
#include <linux/module.h>

#include "sps30.h"

/* sensor measures reliably up to 3000 ug / m3 */
#define SPS30_MAX_PM 3000
/* minimum and maximum self cleaning periods in seconds */
#define SPS30_AUTO_CLEANING_PERIOD_MIN 0
#define SPS30_AUTO_CLEANING_PERIOD_MAX 604800

enum {
	PM1,
	PM2P5,
	PM4,
	PM10,
};

enum {
	RESET,
	MEASURING,
};

static s32 sps30_float_to_int_clamped(__be32 *fp)
{
	int val = be32_to_cpup(fp);
	int mantissa = val & GENMASK(22, 0);
	/* this is fine since passed float is always non-negative */
	int exp = val >> 23;
	int fraction, shift;

	/* special case 0 */
	if (!exp && !mantissa)
		return 0;

	exp -= 127;
	if (exp < 0) {
		/* return values ranging from 1 to 99 */
		return ((((1 << 23) + mantissa) * 100) >> 23) >> (-exp);
	}

	/* return values ranging from 100 to 300000 */
	shift = 23 - exp;
	val = (1 << exp) + (mantissa >> shift);
	if (val >= SPS30_MAX_PM)
		return SPS30_MAX_PM * 100;

	fraction = mantissa & GENMASK(shift - 1, 0);

	return val * 100 + ((fraction * 100) >> shift);
}

static int sps30_do_meas(struct sps30_state *state, s32 *data, int size)
{
	int i, ret;

	if (state->state == RESET) {
		ret = state->ops->start_meas(state);
		if (ret)
			return ret;

		state->state = MEASURING;
	}

	ret = state->ops->read_meas(state, (__be32 *)data, size);
	if (ret)
		return ret;

	for (i = 0; i < size; i++)
		data[i] = sps30_float_to_int_clamped((__be32 *)&data[i]);

	return 0;
}

static int sps30_do_reset(struct sps30_state *state)
{
	int ret;

	ret = state->ops->reset(state);
	if (ret)
		return ret;

	state->state = RESET;

	return 0;
}

static irqreturn_t sps30_trigger_handler(int irq, void *p)
{
	struct iio_poll_func *pf = p;
	struct iio_dev *indio_dev = pf->indio_dev;
	struct sps30_state *state = iio_priv(indio_dev);
	int ret;
	struct {
		s32 data[4]; /* PM1, PM2P5, PM4, PM10 */
		s64 ts;
	} scan;

	mutex_lock(&state->lock);
	ret = sps30_do_meas(state, scan.data, ARRAY_SIZE(scan.data));
	mutex_unlock(&state->lock);
	if (ret)
		goto err;

	iio_push_to_buffers_with_timestamp(indio_dev, &scan,
					   iio_get_time_ns(indio_dev));
err:
	iio_trigger_notify_done(indio_dev->trig);

	return IRQ_HANDLED;
}

static int sps30_read_raw(struct iio_dev *indio_dev,
			  struct iio_chan_spec const *chan,
			  int *val, int *val2, long mask)
{
	struct sps30_state *state = iio_priv(indio_dev);
	int data[4], ret = -EINVAL;

	switch (mask) {
	case IIO_CHAN_INFO_PROCESSED:
		switch (chan->type) {
		case IIO_MASSCONCENTRATION:
			mutex_lock(&state->lock);
			/* read up to the number of bytes actually needed */
			switch (chan->channel2) {
			case IIO_MOD_PM1:
				ret = sps30_do_meas(state, data, 1);
				break;
			case IIO_MOD_PM2P5:
				ret = sps30_do_meas(state, data, 2);
				break;
			case IIO_MOD_PM4:
				ret = sps30_do_meas(state, data, 3);
				break;
			case IIO_MOD_PM10:
				ret = sps30_do_meas(state, data, 4);
				break;
			}
			mutex_unlock(&state->lock);
			if (ret)
				return ret;

			*val = data[chan->address] / 100;
			*val2 = (data[chan->address] % 100) * 10000;

			return IIO_VAL_INT_PLUS_MICRO;
		default:
			return -EINVAL;
		}
	case IIO_CHAN_INFO_SCALE:
		switch (chan->type) {
		case IIO_MASSCONCENTRATION:
			switch (chan->channel2) {
			case IIO_MOD_PM1:
			case IIO_MOD_PM2P5:
			case IIO_MOD_PM4:
			case IIO_MOD_PM10:
				*val = 0;
				*val2 = 10000;

				return IIO_VAL_INT_PLUS_MICRO;
			default:
				return -EINVAL;
			}
		default:
			return -EINVAL;
		}
	}

	return -EINVAL;
}

static ssize_t start_cleaning_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t len)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct sps30_state *state = iio_priv(indio_dev);
	int val, ret;

	if (kstrtoint(buf, 0, &val) || val != 1)
		return -EINVAL;

	mutex_lock(&state->lock);
	ret = state->ops->clean_fan(state);
	mutex_unlock(&state->lock);
	if (ret)
		return ret;

	return len;
}

static ssize_t cleaning_period_show(struct device *dev,
				    struct device_attribute *attr,
				    char *buf)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct sps30_state *state = iio_priv(indio_dev);
	__be32 val;
	int ret;

	mutex_lock(&state->lock);
	ret = state->ops->read_cleaning_period(state, &val);
	mutex_unlock(&state->lock);
	if (ret)
		return ret;

	return sprintf(buf, "%d\n", be32_to_cpu(val));
}

static ssize_t cleaning_period_store(struct device *dev, struct device_attribute *attr,
				     const char *buf, size_t len)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct sps30_state *state = iio_priv(indio_dev);
	int val, ret;

	if (kstrtoint(buf, 0, &val))
		return -EINVAL;

	if ((val < SPS30_AUTO_CLEANING_PERIOD_MIN) ||
	    (val > SPS30_AUTO_CLEANING_PERIOD_MAX))
		return -EINVAL;

	mutex_lock(&state->lock);
	ret = state->ops->write_cleaning_period(state, cpu_to_be32(val));
	if (ret) {
		mutex_unlock(&state->lock);
		return ret;
	}

	msleep(20);

	/*
	 * sensor requires reset in order to return up to date self cleaning
	 * period
	 */
	ret = sps30_do_reset(state);
	if (ret)
		dev_warn(dev,
			 "period changed but reads will return the old value\n");

	mutex_unlock(&state->lock);

	return len;
}

static ssize_t cleaning_period_available_show(struct device *dev,
					      struct device_attribute *attr,
					      char *buf)
{
	return sysfs_emit(buf, "[%d %d %d]\n",
			  SPS30_AUTO_CLEANING_PERIOD_MIN, 1,
			  SPS30_AUTO_CLEANING_PERIOD_MAX);
}

static IIO_DEVICE_ATTR_WO(start_cleaning, 0);
static IIO_DEVICE_ATTR_RW(cleaning_period, 0);
static IIO_DEVICE_ATTR_RO(cleaning_period_available, 0);

static struct attribute *sps30_attrs[] = {
	&iio_dev_attr_start_cleaning.dev_attr.attr,
	&iio_dev_attr_cleaning_period.dev_attr.attr,
	&iio_dev_attr_cleaning_period_available.dev_attr.attr,
	NULL
};

static const struct attribute_group sps30_attr_group = {
	.attrs = sps30_attrs,
};

static const struct iio_info sps30_info = {
	.attrs = &sps30_attr_group,
	.read_raw = sps30_read_raw,
};

#define SPS30_CHAN(_index, _mod) { \
	.type = IIO_MASSCONCENTRATION, \
	.modified = 1, \
	.channel2 = IIO_MOD_ ## _mod, \
	.info_mask_separate = BIT(IIO_CHAN_INFO_PROCESSED), \
	.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE), \
	.address = _mod, \
	.scan_index = _index, \
	.scan_type = { \
		.sign = 'u', \
		.realbits = 19, \
		.storagebits = 32, \
		.endianness = IIO_CPU, \
	}, \
}

static const struct iio_chan_spec sps30_channels[] = {
	SPS30_CHAN(0, PM1),
	SPS30_CHAN(1, PM2P5),
	SPS30_CHAN(2, PM4),
	SPS30_CHAN(3, PM10),
	IIO_CHAN_SOFT_TIMESTAMP(4),
};

static void sps30_devm_stop_meas(void *data)
{
	struct sps30_state *state = data;

	if (state->state == MEASURING)
		state->ops->stop_meas(state);
}

static const unsigned long sps30_scan_masks[] = { 0x0f, 0x00 };

int sps30_probe(struct device *dev, const char *name, void *priv, const struct sps30_ops *ops)
{
	struct iio_dev *indio_dev;
	struct sps30_state *state;
	int ret;

	indio_dev = devm_iio_device_alloc(dev, sizeof(*state));
	if (!indio_dev)
		return -ENOMEM;

	dev_set_drvdata(dev, indio_dev);

	state = iio_priv(indio_dev);
	state->dev = dev;
	state->priv = priv;
	state->ops = ops;
	mutex_init(&state->lock);

	indio_dev->info = &sps30_info;
	indio_dev->name = name;
	indio_dev->channels = sps30_channels;
	indio_dev->num_channels = ARRAY_SIZE(sps30_channels);
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->available_scan_masks = sps30_scan_masks;

	ret = sps30_do_reset(state);
	if (ret) {
		dev_err(dev, "failed to reset device\n");
		return ret;
	}

	ret = state->ops->show_info(state);
	if (ret) {
		dev_err(dev, "failed to read device info\n");
		return ret;
	}

	ret = devm_add_action_or_reset(dev, sps30_devm_stop_meas, state);
	if (ret)
		return ret;

	ret = devm_iio_triggered_buffer_setup(dev, indio_dev, NULL,
					      sps30_trigger_handler, NULL);
	if (ret)
		return ret;

	return devm_iio_device_register(dev, indio_dev);
}
EXPORT_SYMBOL_GPL(sps30_probe);

MODULE_AUTHOR("Tomasz Duszynski <tduszyns@gmail.com>");
MODULE_DESCRIPTION("Sensirion SPS30 particulate matter sensor driver");
MODULE_LICENSE("GPL v2");

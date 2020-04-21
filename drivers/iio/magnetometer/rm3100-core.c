// SPDX-License-Identifier: GPL-2.0
/*
 * PNI RM3100 3-axis geomagnetic sensor driver core.
 *
 * Copyright (C) 2018 Song Qiang <songqiang1304521@gmail.com>
 *
 * User Manual available at
 * <https://www.pnicorp.com/download/rm3100-user-manual/>
 *
 * TODO: event generation, pm.
 */

#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/slab.h>

#include <linux/iio/buffer.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/trigger.h>
#include <linux/iio/triggered_buffer.h>
#include <linux/iio/trigger_consumer.h>

#include <asm/unaligned.h>

#include "rm3100.h"

/* Cycle Count Registers. */
#define RM3100_REG_CC_X			0x05
#define RM3100_REG_CC_Y			0x07
#define RM3100_REG_CC_Z			0x09

/* Poll Measurement Mode register. */
#define RM3100_REG_POLL			0x00
#define		RM3100_POLL_X		BIT(4)
#define		RM3100_POLL_Y		BIT(5)
#define		RM3100_POLL_Z		BIT(6)

/* Continuous Measurement Mode register. */
#define RM3100_REG_CMM			0x01
#define		RM3100_CMM_START	BIT(0)
#define		RM3100_CMM_X		BIT(4)
#define		RM3100_CMM_Y		BIT(5)
#define		RM3100_CMM_Z		BIT(6)

/* TiMe Rate Configuration register. */
#define RM3100_REG_TMRC			0x0B
#define RM3100_TMRC_OFFSET		0x92

/* Result Status register. */
#define RM3100_REG_STATUS		0x34
#define		RM3100_STATUS_DRDY	BIT(7)

/* Measurement result registers. */
#define RM3100_REG_MX2			0x24
#define RM3100_REG_MY2			0x27
#define RM3100_REG_MZ2			0x2a

#define RM3100_W_REG_START		RM3100_REG_POLL
#define RM3100_W_REG_END		RM3100_REG_TMRC
#define RM3100_R_REG_START		RM3100_REG_POLL
#define RM3100_R_REG_END		RM3100_REG_STATUS
#define RM3100_V_REG_START		RM3100_REG_POLL
#define RM3100_V_REG_END		RM3100_REG_STATUS

/*
 * This is computed by hand, is the sum of channel storage bits and padding
 * bits, which is 4+4+4+12=24 in here.
 */
#define RM3100_SCAN_BYTES		24

#define RM3100_CMM_AXIS_SHIFT		4

struct rm3100_data {
	struct regmap *regmap;
	struct completion measuring_done;
	bool use_interrupt;
	int conversion_time;
	int scale;
	u8 buffer[RM3100_SCAN_BYTES];
	struct iio_trigger *drdy_trig;

	/*
	 * This lock is for protecting the consistency of series of i2c
	 * operations, that is, to make sure a measurement process will
	 * not be interrupted by a set frequency operation, which should
	 * be taken where a series of i2c operation starts, released where
	 * the operation ends.
	 */
	struct mutex lock;
};

static const struct regmap_range rm3100_readable_ranges[] = {
	regmap_reg_range(RM3100_R_REG_START, RM3100_R_REG_END),
};

const struct regmap_access_table rm3100_readable_table = {
	.yes_ranges = rm3100_readable_ranges,
	.n_yes_ranges = ARRAY_SIZE(rm3100_readable_ranges),
};
EXPORT_SYMBOL_GPL(rm3100_readable_table);

static const struct regmap_range rm3100_writable_ranges[] = {
	regmap_reg_range(RM3100_W_REG_START, RM3100_W_REG_END),
};

const struct regmap_access_table rm3100_writable_table = {
	.yes_ranges = rm3100_writable_ranges,
	.n_yes_ranges = ARRAY_SIZE(rm3100_writable_ranges),
};
EXPORT_SYMBOL_GPL(rm3100_writable_table);

static const struct regmap_range rm3100_volatile_ranges[] = {
	regmap_reg_range(RM3100_V_REG_START, RM3100_V_REG_END),
};

const struct regmap_access_table rm3100_volatile_table = {
	.yes_ranges = rm3100_volatile_ranges,
	.n_yes_ranges = ARRAY_SIZE(rm3100_volatile_ranges),
};
EXPORT_SYMBOL_GPL(rm3100_volatile_table);

static irqreturn_t rm3100_thread_fn(int irq, void *d)
{
	struct iio_dev *indio_dev = d;
	struct rm3100_data *data = iio_priv(indio_dev);

	/*
	 * Write operation to any register or read operation
	 * to first byte of results will clear the interrupt.
	 */
	regmap_write(data->regmap, RM3100_REG_POLL, 0);

	return IRQ_HANDLED;
}

static irqreturn_t rm3100_irq_handler(int irq, void *d)
{
	struct iio_dev *indio_dev = d;
	struct rm3100_data *data = iio_priv(indio_dev);

	switch (indio_dev->currentmode) {
	case INDIO_DIRECT_MODE:
		complete(&data->measuring_done);
		break;
	case INDIO_BUFFER_TRIGGERED:
		iio_trigger_poll(data->drdy_trig);
		break;
	default:
		dev_err(indio_dev->dev.parent,
			"device mode out of control, current mode: %d",
			indio_dev->currentmode);
	}

	return IRQ_WAKE_THREAD;
}

static int rm3100_wait_measurement(struct rm3100_data *data)
{
	struct regmap *regmap = data->regmap;
	unsigned int val;
	int tries = 20;
	int ret;

	/*
	 * A read cycle of 400kbits i2c bus is about 20us, plus the time
	 * used for scheduling, a read cycle of fast mode of this device
	 * can reach 1.7ms, it may be possible for data to arrive just
	 * after we check the RM3100_REG_STATUS. In this case, irq_handler is
	 * called before measuring_done is reinitialized, it will wait
	 * forever for data that has already been ready.
	 * Reinitialize measuring_done before looking up makes sure we
	 * will always capture interrupt no matter when it happens.
	 */
	if (data->use_interrupt)
		reinit_completion(&data->measuring_done);

	ret = regmap_read(regmap, RM3100_REG_STATUS, &val);
	if (ret < 0)
		return ret;

	if ((val & RM3100_STATUS_DRDY) != RM3100_STATUS_DRDY) {
		if (data->use_interrupt) {
			ret = wait_for_completion_timeout(&data->measuring_done,
				msecs_to_jiffies(data->conversion_time));
			if (!ret)
				return -ETIMEDOUT;
		} else {
			do {
				usleep_range(1000, 5000);

				ret = regmap_read(regmap, RM3100_REG_STATUS,
						  &val);
				if (ret < 0)
					return ret;

				if (val & RM3100_STATUS_DRDY)
					break;
			} while (--tries);
			if (!tries)
				return -ETIMEDOUT;
		}
	}
	return 0;
}

static int rm3100_read_mag(struct rm3100_data *data, int idx, int *val)
{
	struct regmap *regmap = data->regmap;
	u8 buffer[3];
	int ret;

	mutex_lock(&data->lock);
	ret = regmap_write(regmap, RM3100_REG_POLL, BIT(4 + idx));
	if (ret < 0)
		goto unlock_return;

	ret = rm3100_wait_measurement(data);
	if (ret < 0)
		goto unlock_return;

	ret = regmap_bulk_read(regmap, RM3100_REG_MX2 + 3 * idx, buffer, 3);
	if (ret < 0)
		goto unlock_return;
	mutex_unlock(&data->lock);

	*val = sign_extend32(get_unaligned_be24(&buffer[0]), 23);

	return IIO_VAL_INT;

unlock_return:
	mutex_unlock(&data->lock);
	return ret;
}

#define RM3100_CHANNEL(axis, idx)					\
	{								\
		.type = IIO_MAGN,					\
		.modified = 1,						\
		.channel2 = IIO_MOD_##axis,				\
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),		\
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE) |	\
			BIT(IIO_CHAN_INFO_SAMP_FREQ),			\
		.scan_index = idx,					\
		.scan_type = {						\
			.sign = 's',					\
			.realbits = 24,					\
			.storagebits = 32,				\
			.shift = 8,					\
			.endianness = IIO_BE,				\
		},							\
	}

static const struct iio_chan_spec rm3100_channels[] = {
	RM3100_CHANNEL(X, 0),
	RM3100_CHANNEL(Y, 1),
	RM3100_CHANNEL(Z, 2),
	IIO_CHAN_SOFT_TIMESTAMP(3),
};

static IIO_CONST_ATTR_SAMP_FREQ_AVAIL(
	"600 300 150 75 37 18 9 4.5 2.3 1.2 0.6 0.3 0.015 0.075"
);

static struct attribute *rm3100_attributes[] = {
	&iio_const_attr_sampling_frequency_available.dev_attr.attr,
	NULL,
};

static const struct attribute_group rm3100_attribute_group = {
	.attrs = rm3100_attributes,
};

#define RM3100_SAMP_NUM			14

/*
 * Frequency : rm3100_samp_rates[][0].rm3100_samp_rates[][1]Hz.
 * Time between reading: rm3100_sam_rates[][2]ms.
 * The first one is actually 1.7ms.
 */
static const int rm3100_samp_rates[RM3100_SAMP_NUM][3] = {
	{600, 0, 2}, {300, 0, 3}, {150, 0, 7}, {75, 0, 13}, {37, 0, 27},
	{18, 0, 55}, {9, 0, 110}, {4, 500000, 220}, {2, 300000, 440},
	{1, 200000, 800}, {0, 600000, 1600}, {0, 300000, 3300},
	{0, 15000, 6700},  {0, 75000, 13000}
};

static int rm3100_get_samp_freq(struct rm3100_data *data, int *val, int *val2)
{
	unsigned int tmp;
	int ret;

	mutex_lock(&data->lock);
	ret = regmap_read(data->regmap, RM3100_REG_TMRC, &tmp);
	mutex_unlock(&data->lock);
	if (ret < 0)
		return ret;
	*val = rm3100_samp_rates[tmp - RM3100_TMRC_OFFSET][0];
	*val2 = rm3100_samp_rates[tmp - RM3100_TMRC_OFFSET][1];

	return IIO_VAL_INT_PLUS_MICRO;
}

static int rm3100_set_cycle_count(struct rm3100_data *data, int val)
{
	int ret;
	u8 i;

	for (i = 0; i < 3; i++) {
		ret = regmap_write(data->regmap, RM3100_REG_CC_X + 2 * i, val);
		if (ret < 0)
			return ret;
	}

	/*
	 * The scale of this sensor depends on the cycle count value, these
	 * three values are corresponding to the cycle count value 50, 100,
	 * 200. scale = output / gain * 10^4.
	 */
	switch (val) {
	case 50:
		data->scale = 500;
		break;
	case 100:
		data->scale = 263;
		break;
	/*
	 * case 200:
	 * This function will never be called by users' code, so here we
	 * assume that it will never get a wrong parameter.
	 */
	default:
		data->scale = 133;
	}

	return 0;
}

static int rm3100_set_samp_freq(struct iio_dev *indio_dev, int val, int val2)
{
	struct rm3100_data *data = iio_priv(indio_dev);
	struct regmap *regmap = data->regmap;
	unsigned int cycle_count;
	int ret;
	int i;

	mutex_lock(&data->lock);
	/* All cycle count registers use the same value. */
	ret = regmap_read(regmap, RM3100_REG_CC_X, &cycle_count);
	if (ret < 0)
		goto unlock_return;

	for (i = 0; i < RM3100_SAMP_NUM; i++) {
		if (val == rm3100_samp_rates[i][0] &&
		    val2 == rm3100_samp_rates[i][1])
			break;
	}
	if (i == RM3100_SAMP_NUM) {
		ret = -EINVAL;
		goto unlock_return;
	}

	ret = regmap_write(regmap, RM3100_REG_TMRC, i + RM3100_TMRC_OFFSET);
	if (ret < 0)
		goto unlock_return;

	/* Checking if cycle count registers need changing. */
	if (val == 600 && cycle_count == 200) {
		ret = rm3100_set_cycle_count(data, 100);
		if (ret < 0)
			goto unlock_return;
	} else if (val != 600 && cycle_count == 100) {
		ret = rm3100_set_cycle_count(data, 200);
		if (ret < 0)
			goto unlock_return;
	}

	if (indio_dev->currentmode == INDIO_BUFFER_TRIGGERED) {
		/* Writing TMRC registers requires CMM reset. */
		ret = regmap_write(regmap, RM3100_REG_CMM, 0);
		if (ret < 0)
			goto unlock_return;
		ret = regmap_write(data->regmap, RM3100_REG_CMM,
			(*indio_dev->active_scan_mask & 0x7) <<
			RM3100_CMM_AXIS_SHIFT | RM3100_CMM_START);
		if (ret < 0)
			goto unlock_return;
	}
	mutex_unlock(&data->lock);

	data->conversion_time = rm3100_samp_rates[i][2] * 2;
	return 0;

unlock_return:
	mutex_unlock(&data->lock);
	return ret;
}

static int rm3100_read_raw(struct iio_dev *indio_dev,
			   const struct iio_chan_spec *chan,
			   int *val, int *val2, long mask)
{
	struct rm3100_data *data = iio_priv(indio_dev);
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		ret = iio_device_claim_direct_mode(indio_dev);
		if (ret < 0)
			return ret;

		ret = rm3100_read_mag(data, chan->scan_index, val);
		iio_device_release_direct_mode(indio_dev);

		return ret;
	case IIO_CHAN_INFO_SCALE:
		*val = 0;
		*val2 = data->scale;

		return IIO_VAL_INT_PLUS_MICRO;
	case IIO_CHAN_INFO_SAMP_FREQ:
		return rm3100_get_samp_freq(data, val, val2);
	default:
		return -EINVAL;
	}
}

static int rm3100_write_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan,
			    int val, int val2, long mask)
{
	switch (mask) {
	case IIO_CHAN_INFO_SAMP_FREQ:
		return rm3100_set_samp_freq(indio_dev, val, val2);
	default:
		return -EINVAL;
	}
}

static const struct iio_info rm3100_info = {
	.attrs = &rm3100_attribute_group,
	.read_raw = rm3100_read_raw,
	.write_raw = rm3100_write_raw,
};

static int rm3100_buffer_preenable(struct iio_dev *indio_dev)
{
	struct rm3100_data *data = iio_priv(indio_dev);

	/* Starting channels enabled. */
	return regmap_write(data->regmap, RM3100_REG_CMM,
		(*indio_dev->active_scan_mask & 0x7) << RM3100_CMM_AXIS_SHIFT |
		RM3100_CMM_START);
}

static int rm3100_buffer_postdisable(struct iio_dev *indio_dev)
{
	struct rm3100_data *data = iio_priv(indio_dev);

	return regmap_write(data->regmap, RM3100_REG_CMM, 0);
}

static const struct iio_buffer_setup_ops rm3100_buffer_ops = {
	.preenable = rm3100_buffer_preenable,
	.postenable = iio_triggered_buffer_postenable,
	.predisable = iio_triggered_buffer_predisable,
	.postdisable = rm3100_buffer_postdisable,
};

static irqreturn_t rm3100_trigger_handler(int irq, void *p)
{
	struct iio_poll_func *pf = p;
	struct iio_dev *indio_dev = pf->indio_dev;
	unsigned long scan_mask = *indio_dev->active_scan_mask;
	unsigned int mask_len = indio_dev->masklength;
	struct rm3100_data *data = iio_priv(indio_dev);
	struct regmap *regmap = data->regmap;
	int ret, i, bit;

	mutex_lock(&data->lock);
	switch (scan_mask) {
	case BIT(0) | BIT(1) | BIT(2):
		ret = regmap_bulk_read(regmap, RM3100_REG_MX2, data->buffer, 9);
		mutex_unlock(&data->lock);
		if (ret < 0)
			goto done;
		/* Convert XXXYYYZZZxxx to XXXxYYYxZZZx. x for paddings. */
		for (i = 2; i > 0; i--)
			memmove(data->buffer + i * 4, data->buffer + i * 3, 3);
		break;
	case BIT(0) | BIT(1):
		ret = regmap_bulk_read(regmap, RM3100_REG_MX2, data->buffer, 6);
		mutex_unlock(&data->lock);
		if (ret < 0)
			goto done;
		memmove(data->buffer + 4, data->buffer + 3, 3);
		break;
	case BIT(1) | BIT(2):
		ret = regmap_bulk_read(regmap, RM3100_REG_MY2, data->buffer, 6);
		mutex_unlock(&data->lock);
		if (ret < 0)
			goto done;
		memmove(data->buffer + 4, data->buffer + 3, 3);
		break;
	case BIT(0) | BIT(2):
		ret = regmap_bulk_read(regmap, RM3100_REG_MX2, data->buffer, 9);
		mutex_unlock(&data->lock);
		if (ret < 0)
			goto done;
		memmove(data->buffer + 4, data->buffer + 6, 3);
		break;
	default:
		for_each_set_bit(bit, &scan_mask, mask_len) {
			ret = regmap_bulk_read(regmap, RM3100_REG_MX2 + 3 * bit,
					       data->buffer, 3);
			if (ret < 0) {
				mutex_unlock(&data->lock);
				goto done;
			}
		}
		mutex_unlock(&data->lock);
	}
	/*
	 * Always using the same buffer so that we wouldn't need to set the
	 * paddings to 0 in case of leaking any data.
	 */
	iio_push_to_buffers_with_timestamp(indio_dev, data->buffer,
					   pf->timestamp);
done:
	iio_trigger_notify_done(indio_dev->trig);

	return IRQ_HANDLED;
}

int rm3100_common_probe(struct device *dev, struct regmap *regmap, int irq)
{
	struct iio_dev *indio_dev;
	struct rm3100_data *data;
	unsigned int tmp;
	int ret;

	indio_dev = devm_iio_device_alloc(dev, sizeof(*data));
	if (!indio_dev)
		return -ENOMEM;

	data = iio_priv(indio_dev);
	data->regmap = regmap;

	mutex_init(&data->lock);

	indio_dev->dev.parent = dev;
	indio_dev->name = "rm3100";
	indio_dev->info = &rm3100_info;
	indio_dev->channels = rm3100_channels;
	indio_dev->num_channels = ARRAY_SIZE(rm3100_channels);
	indio_dev->modes = INDIO_DIRECT_MODE | INDIO_BUFFER_TRIGGERED;
	indio_dev->currentmode = INDIO_DIRECT_MODE;

	if (!irq)
		data->use_interrupt = false;
	else {
		data->use_interrupt = true;

		init_completion(&data->measuring_done);
		ret = devm_request_threaded_irq(dev,
						irq,
						rm3100_irq_handler,
						rm3100_thread_fn,
						IRQF_TRIGGER_HIGH |
						IRQF_ONESHOT,
						indio_dev->name,
						indio_dev);
		if (ret < 0) {
			dev_err(dev, "request irq line failed.\n");
			return ret;
		}

		data->drdy_trig = devm_iio_trigger_alloc(dev, "%s-drdy%d",
							 indio_dev->name,
							 indio_dev->id);
		if (!data->drdy_trig)
			return -ENOMEM;

		data->drdy_trig->dev.parent = dev;
		ret = devm_iio_trigger_register(dev, data->drdy_trig);
		if (ret < 0)
			return ret;
	}

	ret = devm_iio_triggered_buffer_setup(dev, indio_dev,
					      &iio_pollfunc_store_time,
					      rm3100_trigger_handler,
					      &rm3100_buffer_ops);
	if (ret < 0)
		return ret;

	ret = regmap_read(regmap, RM3100_REG_TMRC, &tmp);
	if (ret < 0)
		return ret;
	/* Initializing max wait time, which is double conversion time. */
	data->conversion_time = rm3100_samp_rates[tmp - RM3100_TMRC_OFFSET][2]
				* 2;

	/* Cycle count values may not be what we want. */
	if ((tmp - RM3100_TMRC_OFFSET) == 0)
		rm3100_set_cycle_count(data, 100);
	else
		rm3100_set_cycle_count(data, 200);

	return devm_iio_device_register(dev, indio_dev);
}
EXPORT_SYMBOL_GPL(rm3100_common_probe);

MODULE_AUTHOR("Song Qiang <songqiang1304521@gmail.com>");
MODULE_DESCRIPTION("PNI RM3100 3-axis magnetometer i2c driver");
MODULE_LICENSE("GPL v2");

/*
 * Copyright (c) 2014 Intel Corporation
 *
 * Driver for Semtech's SX9500 capacitive proximity/button solution.
 * Datasheet available at
 * <http://www.semtech.com/images/datasheet/sx9500.pdf>.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/irq.h>
#include <linux/acpi.h>
#include <linux/gpio/consumer.h>
#include <linux/regmap.h>
#include <linux/pm.h>
#include <linux/delay.h>

#include <linux/iio/iio.h>
#include <linux/iio/buffer.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/events.h>
#include <linux/iio/trigger.h>
#include <linux/iio/triggered_buffer.h>
#include <linux/iio/trigger_consumer.h>

#define SX9500_DRIVER_NAME		"sx9500"
#define SX9500_IRQ_NAME			"sx9500_event"

#define SX9500_GPIO_INT			"interrupt"
#define SX9500_GPIO_RESET		"reset"

/* Register definitions. */
#define SX9500_REG_IRQ_SRC		0x00
#define SX9500_REG_STAT			0x01
#define SX9500_REG_IRQ_MSK		0x03

#define SX9500_REG_PROX_CTRL0		0x06
#define SX9500_REG_PROX_CTRL1		0x07
#define SX9500_REG_PROX_CTRL2		0x08
#define SX9500_REG_PROX_CTRL3		0x09
#define SX9500_REG_PROX_CTRL4		0x0a
#define SX9500_REG_PROX_CTRL5		0x0b
#define SX9500_REG_PROX_CTRL6		0x0c
#define SX9500_REG_PROX_CTRL7		0x0d
#define SX9500_REG_PROX_CTRL8		0x0e

#define SX9500_REG_SENSOR_SEL		0x20
#define SX9500_REG_USE_MSB		0x21
#define SX9500_REG_USE_LSB		0x22
#define SX9500_REG_AVG_MSB		0x23
#define SX9500_REG_AVG_LSB		0x24
#define SX9500_REG_DIFF_MSB		0x25
#define SX9500_REG_DIFF_LSB		0x26
#define SX9500_REG_OFFSET_MSB		0x27
#define SX9500_REG_OFFSET_LSB		0x28

#define SX9500_REG_RESET		0x7f

/* Write this to REG_RESET to do a soft reset. */
#define SX9500_SOFT_RESET		0xde

#define SX9500_SCAN_PERIOD_MASK		GENMASK(6, 4)
#define SX9500_SCAN_PERIOD_SHIFT	4

/*
 * These serve for identifying IRQ source in the IRQ_SRC register, and
 * also for masking the IRQs in the IRQ_MSK register.
 */
#define SX9500_CLOSE_IRQ		BIT(6)
#define SX9500_FAR_IRQ			BIT(5)
#define SX9500_CONVDONE_IRQ		BIT(3)

#define SX9500_PROXSTAT_SHIFT		4
#define SX9500_COMPSTAT_MASK		GENMASK(3, 0)

#define SX9500_NUM_CHANNELS		4
#define SX9500_CHAN_MASK		GENMASK(SX9500_NUM_CHANNELS - 1, 0)

struct sx9500_data {
	struct mutex mutex;
	struct i2c_client *client;
	struct iio_trigger *trig;
	struct regmap *regmap;
	struct gpio_desc *gpiod_rst;
	/*
	 * Last reading of the proximity status for each channel.  We
	 * only send an event to user space when this changes.
	 */
	bool prox_stat[SX9500_NUM_CHANNELS];
	bool event_enabled[SX9500_NUM_CHANNELS];
	bool trigger_enabled;
	u16 *buffer;
	/* Remember enabled channels and sample rate during suspend. */
	unsigned int suspend_ctrl0;
	struct completion completion;
	int data_rdy_users, close_far_users;
	int channel_users[SX9500_NUM_CHANNELS];
};

static const struct iio_event_spec sx9500_events[] = {
	{
		.type = IIO_EV_TYPE_THRESH,
		.dir = IIO_EV_DIR_EITHER,
		.mask_separate = BIT(IIO_EV_INFO_ENABLE),
	},
};

#define SX9500_CHANNEL(idx)					\
	{							\
		.type = IIO_PROXIMITY,				\
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),	\
		.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_SAMP_FREQ), \
		.indexed = 1,					\
		.channel = idx,					\
		.event_spec = sx9500_events,			\
		.num_event_specs = ARRAY_SIZE(sx9500_events),	\
		.scan_index = idx,				\
		.scan_type = {					\
			.sign = 'u',				\
			.realbits = 16,				\
			.storagebits = 16,			\
			.shift = 0,				\
		},						\
	}

static const struct iio_chan_spec sx9500_channels[] = {
	SX9500_CHANNEL(0),
	SX9500_CHANNEL(1),
	SX9500_CHANNEL(2),
	SX9500_CHANNEL(3),
	IIO_CHAN_SOFT_TIMESTAMP(4),
};

static const struct {
	int val;
	int val2;
} sx9500_samp_freq_table[] = {
	{33, 333333},
	{16, 666666},
	{11, 111111},
	{8, 333333},
	{6, 666666},
	{5, 0},
	{3, 333333},
	{2, 500000},
};

static const unsigned int sx9500_scan_period_table[] = {
	30, 60, 90, 120, 150, 200, 300, 400,
};

static const struct regmap_range sx9500_writable_reg_ranges[] = {
	regmap_reg_range(SX9500_REG_IRQ_MSK, SX9500_REG_IRQ_MSK),
	regmap_reg_range(SX9500_REG_PROX_CTRL0, SX9500_REG_PROX_CTRL8),
	regmap_reg_range(SX9500_REG_SENSOR_SEL, SX9500_REG_SENSOR_SEL),
	regmap_reg_range(SX9500_REG_OFFSET_MSB, SX9500_REG_OFFSET_LSB),
	regmap_reg_range(SX9500_REG_RESET, SX9500_REG_RESET),
};

static const struct regmap_access_table sx9500_writeable_regs = {
	.yes_ranges = sx9500_writable_reg_ranges,
	.n_yes_ranges = ARRAY_SIZE(sx9500_writable_reg_ranges),
};

/*
 * All allocated registers are readable, so we just list unallocated
 * ones.
 */
static const struct regmap_range sx9500_non_readable_reg_ranges[] = {
	regmap_reg_range(SX9500_REG_STAT + 1, SX9500_REG_STAT + 1),
	regmap_reg_range(SX9500_REG_IRQ_MSK + 1, SX9500_REG_PROX_CTRL0 - 1),
	regmap_reg_range(SX9500_REG_PROX_CTRL8 + 1, SX9500_REG_SENSOR_SEL - 1),
	regmap_reg_range(SX9500_REG_OFFSET_LSB + 1, SX9500_REG_RESET - 1),
};

static const struct regmap_access_table sx9500_readable_regs = {
	.no_ranges = sx9500_non_readable_reg_ranges,
	.n_no_ranges = ARRAY_SIZE(sx9500_non_readable_reg_ranges),
};

static const struct regmap_range sx9500_volatile_reg_ranges[] = {
	regmap_reg_range(SX9500_REG_IRQ_SRC, SX9500_REG_STAT),
	regmap_reg_range(SX9500_REG_USE_MSB, SX9500_REG_OFFSET_LSB),
	regmap_reg_range(SX9500_REG_RESET, SX9500_REG_RESET),
};

static const struct regmap_access_table sx9500_volatile_regs = {
	.yes_ranges = sx9500_volatile_reg_ranges,
	.n_yes_ranges = ARRAY_SIZE(sx9500_volatile_reg_ranges),
};

static const struct regmap_config sx9500_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,

	.max_register = SX9500_REG_RESET,
	.cache_type = REGCACHE_RBTREE,

	.wr_table = &sx9500_writeable_regs,
	.rd_table = &sx9500_readable_regs,
	.volatile_table = &sx9500_volatile_regs,
};

static int sx9500_inc_users(struct sx9500_data *data, int *counter,
			    unsigned int reg, unsigned int bitmask)
{
	(*counter)++;
	if (*counter != 1)
		/* Bit is already active, nothing to do. */
		return 0;

	return regmap_update_bits(data->regmap, reg, bitmask, bitmask);
}

static int sx9500_dec_users(struct sx9500_data *data, int *counter,
			    unsigned int reg, unsigned int bitmask)
{
	(*counter)--;
	if (*counter != 0)
		/* There are more users, do not deactivate. */
		return 0;

	return regmap_update_bits(data->regmap, reg, bitmask, 0);
}

static int sx9500_inc_chan_users(struct sx9500_data *data, int chan)
{
	return sx9500_inc_users(data, &data->channel_users[chan],
				SX9500_REG_PROX_CTRL0, BIT(chan));
}

static int sx9500_dec_chan_users(struct sx9500_data *data, int chan)
{
	return sx9500_dec_users(data, &data->channel_users[chan],
				SX9500_REG_PROX_CTRL0, BIT(chan));
}

static int sx9500_inc_data_rdy_users(struct sx9500_data *data)
{
	return sx9500_inc_users(data, &data->data_rdy_users,
				SX9500_REG_IRQ_MSK, SX9500_CONVDONE_IRQ);
}

static int sx9500_dec_data_rdy_users(struct sx9500_data *data)
{
	return sx9500_dec_users(data, &data->data_rdy_users,
				SX9500_REG_IRQ_MSK, SX9500_CONVDONE_IRQ);
}

static int sx9500_inc_close_far_users(struct sx9500_data *data)
{
	return sx9500_inc_users(data, &data->close_far_users,
				SX9500_REG_IRQ_MSK,
				SX9500_CLOSE_IRQ | SX9500_FAR_IRQ);
}

static int sx9500_dec_close_far_users(struct sx9500_data *data)
{
	return sx9500_dec_users(data, &data->close_far_users,
				SX9500_REG_IRQ_MSK,
				SX9500_CLOSE_IRQ | SX9500_FAR_IRQ);
}

static int sx9500_read_prox_data(struct sx9500_data *data,
				 const struct iio_chan_spec *chan,
				 int *val)
{
	int ret;
	__be16 regval;

	ret = regmap_write(data->regmap, SX9500_REG_SENSOR_SEL, chan->channel);
	if (ret < 0)
		return ret;

	ret = regmap_bulk_read(data->regmap, SX9500_REG_USE_MSB, &regval, 2);
	if (ret < 0)
		return ret;

	*val = be16_to_cpu(regval);

	return IIO_VAL_INT;
}

/*
 * If we have no interrupt support, we have to wait for a scan period
 * after enabling a channel to get a result.
 */
static int sx9500_wait_for_sample(struct sx9500_data *data)
{
	int ret;
	unsigned int val;

	ret = regmap_read(data->regmap, SX9500_REG_PROX_CTRL0, &val);
	if (ret < 0)
		return ret;

	val = (val & SX9500_SCAN_PERIOD_MASK) >> SX9500_SCAN_PERIOD_SHIFT;

	msleep(sx9500_scan_period_table[val]);

	return 0;
}

static int sx9500_read_proximity(struct sx9500_data *data,
				 const struct iio_chan_spec *chan,
				 int *val)
{
	int ret;

	mutex_lock(&data->mutex);

	ret = sx9500_inc_chan_users(data, chan->channel);
	if (ret < 0)
		goto out;

	ret = sx9500_inc_data_rdy_users(data);
	if (ret < 0)
		goto out_dec_chan;

	mutex_unlock(&data->mutex);

	if (data->client->irq > 0)
		ret = wait_for_completion_interruptible(&data->completion);
	else
		ret = sx9500_wait_for_sample(data);

	mutex_lock(&data->mutex);

	if (ret < 0)
		goto out_dec_data_rdy;

	ret = sx9500_read_prox_data(data, chan, val);
	if (ret < 0)
		goto out_dec_data_rdy;

	ret = sx9500_dec_data_rdy_users(data);
	if (ret < 0)
		goto out_dec_chan;

	ret = sx9500_dec_chan_users(data, chan->channel);
	if (ret < 0)
		goto out;

	ret = IIO_VAL_INT;

	goto out;

out_dec_data_rdy:
	sx9500_dec_data_rdy_users(data);
out_dec_chan:
	sx9500_dec_chan_users(data, chan->channel);
out:
	mutex_unlock(&data->mutex);
	reinit_completion(&data->completion);

	return ret;
}

static int sx9500_read_samp_freq(struct sx9500_data *data,
				 int *val, int *val2)
{
	int ret;
	unsigned int regval;

	mutex_lock(&data->mutex);
	ret = regmap_read(data->regmap, SX9500_REG_PROX_CTRL0, &regval);
	mutex_unlock(&data->mutex);

	if (ret < 0)
		return ret;

	regval = (regval & SX9500_SCAN_PERIOD_MASK) >> SX9500_SCAN_PERIOD_SHIFT;
	*val = sx9500_samp_freq_table[regval].val;
	*val2 = sx9500_samp_freq_table[regval].val2;

	return IIO_VAL_INT_PLUS_MICRO;
}

static int sx9500_read_raw(struct iio_dev *indio_dev,
			   const struct iio_chan_spec *chan,
			   int *val, int *val2, long mask)
{
	struct sx9500_data *data = iio_priv(indio_dev);
	int ret;

	switch (chan->type) {
	case IIO_PROXIMITY:
		switch (mask) {
		case IIO_CHAN_INFO_RAW:
			ret = iio_device_claim_direct_mode(indio_dev);
			if (ret)
				return ret;
			ret = sx9500_read_proximity(data, chan, val);
			iio_device_release_direct_mode(indio_dev);
			return ret;
		case IIO_CHAN_INFO_SAMP_FREQ:
			return sx9500_read_samp_freq(data, val, val2);
		default:
			return -EINVAL;
		}
	default:
		return -EINVAL;
	}
}

static int sx9500_set_samp_freq(struct sx9500_data *data,
				int val, int val2)
{
	int i, ret;

	for (i = 0; i < ARRAY_SIZE(sx9500_samp_freq_table); i++)
		if (val == sx9500_samp_freq_table[i].val &&
		    val2 == sx9500_samp_freq_table[i].val2)
			break;

	if (i == ARRAY_SIZE(sx9500_samp_freq_table))
		return -EINVAL;

	mutex_lock(&data->mutex);

	ret = regmap_update_bits(data->regmap, SX9500_REG_PROX_CTRL0,
				 SX9500_SCAN_PERIOD_MASK,
				 i << SX9500_SCAN_PERIOD_SHIFT);

	mutex_unlock(&data->mutex);

	return ret;
}

static int sx9500_write_raw(struct iio_dev *indio_dev,
			    const struct iio_chan_spec *chan,
			    int val, int val2, long mask)
{
	struct sx9500_data *data = iio_priv(indio_dev);

	switch (chan->type) {
	case IIO_PROXIMITY:
		switch (mask) {
		case IIO_CHAN_INFO_SAMP_FREQ:
			return sx9500_set_samp_freq(data, val, val2);
		default:
			return -EINVAL;
		}
	default:
		return -EINVAL;
	}
}

static irqreturn_t sx9500_irq_handler(int irq, void *private)
{
	struct iio_dev *indio_dev = private;
	struct sx9500_data *data = iio_priv(indio_dev);

	if (data->trigger_enabled)
		iio_trigger_poll(data->trig);

	/*
	 * Even if no event is enabled, we need to wake the thread to
	 * clear the interrupt state by reading SX9500_REG_IRQ_SRC.  It
	 * is not possible to do that here because regmap_read takes a
	 * mutex.
	 */
	return IRQ_WAKE_THREAD;
}

static void sx9500_push_events(struct iio_dev *indio_dev)
{
	int ret;
	unsigned int val, chan;
	struct sx9500_data *data = iio_priv(indio_dev);

	ret = regmap_read(data->regmap, SX9500_REG_STAT, &val);
	if (ret < 0) {
		dev_err(&data->client->dev, "i2c transfer error in irq\n");
		return;
	}

	val >>= SX9500_PROXSTAT_SHIFT;
	for (chan = 0; chan < SX9500_NUM_CHANNELS; chan++) {
		int dir;
		u64 ev;
		bool new_prox = val & BIT(chan);

		if (!data->event_enabled[chan])
			continue;
		if (new_prox == data->prox_stat[chan])
			/* No change on this channel. */
			continue;

		dir = new_prox ? IIO_EV_DIR_FALLING : IIO_EV_DIR_RISING;
		ev = IIO_UNMOD_EVENT_CODE(IIO_PROXIMITY, chan,
					  IIO_EV_TYPE_THRESH, dir);
		iio_push_event(indio_dev, ev, iio_get_time_ns(indio_dev));
		data->prox_stat[chan] = new_prox;
	}
}

static irqreturn_t sx9500_irq_thread_handler(int irq, void *private)
{
	struct iio_dev *indio_dev = private;
	struct sx9500_data *data = iio_priv(indio_dev);
	int ret;
	unsigned int val;

	mutex_lock(&data->mutex);

	ret = regmap_read(data->regmap, SX9500_REG_IRQ_SRC, &val);
	if (ret < 0) {
		dev_err(&data->client->dev, "i2c transfer error in irq\n");
		goto out;
	}

	if (val & (SX9500_CLOSE_IRQ | SX9500_FAR_IRQ))
		sx9500_push_events(indio_dev);

	if (val & SX9500_CONVDONE_IRQ)
		complete(&data->completion);

out:
	mutex_unlock(&data->mutex);

	return IRQ_HANDLED;
}

static int sx9500_read_event_config(struct iio_dev *indio_dev,
				    const struct iio_chan_spec *chan,
				    enum iio_event_type type,
				    enum iio_event_direction dir)
{
	struct sx9500_data *data = iio_priv(indio_dev);

	if (chan->type != IIO_PROXIMITY || type != IIO_EV_TYPE_THRESH ||
	    dir != IIO_EV_DIR_EITHER)
		return -EINVAL;

	return data->event_enabled[chan->channel];
}

static int sx9500_write_event_config(struct iio_dev *indio_dev,
				     const struct iio_chan_spec *chan,
				     enum iio_event_type type,
				     enum iio_event_direction dir,
				     int state)
{
	struct sx9500_data *data = iio_priv(indio_dev);
	int ret;

	if (chan->type != IIO_PROXIMITY || type != IIO_EV_TYPE_THRESH ||
	    dir != IIO_EV_DIR_EITHER)
		return -EINVAL;

	mutex_lock(&data->mutex);

	if (state == 1) {
		ret = sx9500_inc_chan_users(data, chan->channel);
		if (ret < 0)
			goto out_unlock;
		ret = sx9500_inc_close_far_users(data);
		if (ret < 0)
			goto out_undo_chan;
	} else {
		ret = sx9500_dec_chan_users(data, chan->channel);
		if (ret < 0)
			goto out_unlock;
		ret = sx9500_dec_close_far_users(data);
		if (ret < 0)
			goto out_undo_chan;
	}

	data->event_enabled[chan->channel] = state;
	goto out_unlock;

out_undo_chan:
	if (state == 1)
		sx9500_dec_chan_users(data, chan->channel);
	else
		sx9500_inc_chan_users(data, chan->channel);
out_unlock:
	mutex_unlock(&data->mutex);
	return ret;
}

static int sx9500_update_scan_mode(struct iio_dev *indio_dev,
				   const unsigned long *scan_mask)
{
	struct sx9500_data *data = iio_priv(indio_dev);

	mutex_lock(&data->mutex);
	kfree(data->buffer);
	data->buffer = kzalloc(indio_dev->scan_bytes, GFP_KERNEL);
	mutex_unlock(&data->mutex);

	if (data->buffer == NULL)
		return -ENOMEM;

	return 0;
}

static IIO_CONST_ATTR_SAMP_FREQ_AVAIL(
	"2.500000 3.333333 5 6.666666 8.333333 11.111111 16.666666 33.333333");

static struct attribute *sx9500_attributes[] = {
	&iio_const_attr_sampling_frequency_available.dev_attr.attr,
	NULL,
};

static const struct attribute_group sx9500_attribute_group = {
	.attrs = sx9500_attributes,
};

static const struct iio_info sx9500_info = {
	.driver_module = THIS_MODULE,
	.attrs = &sx9500_attribute_group,
	.read_raw = &sx9500_read_raw,
	.write_raw = &sx9500_write_raw,
	.read_event_config = &sx9500_read_event_config,
	.write_event_config = &sx9500_write_event_config,
	.update_scan_mode = &sx9500_update_scan_mode,
};

static int sx9500_set_trigger_state(struct iio_trigger *trig,
				    bool state)
{
	struct iio_dev *indio_dev = iio_trigger_get_drvdata(trig);
	struct sx9500_data *data = iio_priv(indio_dev);
	int ret;

	mutex_lock(&data->mutex);

	if (state)
		ret = sx9500_inc_data_rdy_users(data);
	else
		ret = sx9500_dec_data_rdy_users(data);
	if (ret < 0)
		goto out;

	data->trigger_enabled = state;

out:
	mutex_unlock(&data->mutex);

	return ret;
}

static const struct iio_trigger_ops sx9500_trigger_ops = {
	.set_trigger_state = sx9500_set_trigger_state,
	.owner = THIS_MODULE,
};

static irqreturn_t sx9500_trigger_handler(int irq, void *private)
{
	struct iio_poll_func *pf = private;
	struct iio_dev *indio_dev = pf->indio_dev;
	struct sx9500_data *data = iio_priv(indio_dev);
	int val, bit, ret, i = 0;

	mutex_lock(&data->mutex);

	for_each_set_bit(bit, indio_dev->active_scan_mask,
			 indio_dev->masklength) {
		ret = sx9500_read_prox_data(data, &indio_dev->channels[bit],
					    &val);
		if (ret < 0)
			goto out;

		data->buffer[i++] = val;
	}

	iio_push_to_buffers_with_timestamp(indio_dev, data->buffer,
					   iio_get_time_ns(indio_dev));

out:
	mutex_unlock(&data->mutex);

	iio_trigger_notify_done(indio_dev->trig);

	return IRQ_HANDLED;
}

static int sx9500_buffer_preenable(struct iio_dev *indio_dev)
{
	struct sx9500_data *data = iio_priv(indio_dev);
	int ret = 0, i;

	mutex_lock(&data->mutex);

	for (i = 0; i < SX9500_NUM_CHANNELS; i++)
		if (test_bit(i, indio_dev->active_scan_mask)) {
			ret = sx9500_inc_chan_users(data, i);
			if (ret)
				break;
		}

	if (ret)
		for (i = i - 1; i >= 0; i--)
			if (test_bit(i, indio_dev->active_scan_mask))
				sx9500_dec_chan_users(data, i);

	mutex_unlock(&data->mutex);

	return ret;
}

static int sx9500_buffer_predisable(struct iio_dev *indio_dev)
{
	struct sx9500_data *data = iio_priv(indio_dev);
	int ret = 0, i;

	iio_triggered_buffer_predisable(indio_dev);

	mutex_lock(&data->mutex);

	for (i = 0; i < SX9500_NUM_CHANNELS; i++)
		if (test_bit(i, indio_dev->active_scan_mask)) {
			ret = sx9500_dec_chan_users(data, i);
			if (ret)
				break;
		}

	if (ret)
		for (i = i - 1; i >= 0; i--)
			if (test_bit(i, indio_dev->active_scan_mask))
				sx9500_inc_chan_users(data, i);

	mutex_unlock(&data->mutex);

	return ret;
}

static const struct iio_buffer_setup_ops sx9500_buffer_setup_ops = {
	.preenable = sx9500_buffer_preenable,
	.postenable = iio_triggered_buffer_postenable,
	.predisable = sx9500_buffer_predisable,
};

struct sx9500_reg_default {
	u8 reg;
	u8 def;
};

static const struct sx9500_reg_default sx9500_default_regs[] = {
	{
		.reg = SX9500_REG_PROX_CTRL1,
		/* Shield enabled, small range. */
		.def = 0x43,
	},
	{
		.reg = SX9500_REG_PROX_CTRL2,
		/* x8 gain, 167kHz frequency, finest resolution. */
		.def = 0x77,
	},
	{
		.reg = SX9500_REG_PROX_CTRL3,
		/* Doze enabled, 2x scan period doze, no raw filter. */
		.def = 0x40,
	},
	{
		.reg = SX9500_REG_PROX_CTRL4,
		/* Average threshold. */
		.def = 0x30,
	},
	{
		.reg = SX9500_REG_PROX_CTRL5,
		/*
		 * Debouncer off, lowest average negative filter,
		 * highest average postive filter.
		 */
		.def = 0x0f,
	},
	{
		.reg = SX9500_REG_PROX_CTRL6,
		/* Proximity detection threshold: 280 */
		.def = 0x0e,
	},
	{
		.reg = SX9500_REG_PROX_CTRL7,
		/*
		 * No automatic compensation, compensate each pin
		 * independently, proximity hysteresis: 32, close
		 * debouncer off, far debouncer off.
		 */
		.def = 0x00,
	},
	{
		.reg = SX9500_REG_PROX_CTRL8,
		/* No stuck timeout, no periodic compensation. */
		.def = 0x00,
	},
	{
		.reg = SX9500_REG_PROX_CTRL0,
		/* Scan period: 30ms, all sensors disabled. */
		.def = 0x00,
	},
};

/* Activate all channels and perform an initial compensation. */
static int sx9500_init_compensation(struct iio_dev *indio_dev)
{
	struct sx9500_data *data = iio_priv(indio_dev);
	int i, ret;
	unsigned int val;

	ret = regmap_update_bits(data->regmap, SX9500_REG_PROX_CTRL0,
				 SX9500_CHAN_MASK, SX9500_CHAN_MASK);
	if (ret < 0)
		return ret;

	for (i = 10; i >= 0; i--) {
		usleep_range(10000, 20000);
		ret = regmap_read(data->regmap, SX9500_REG_STAT, &val);
		if (ret < 0)
			goto out;
		if (!(val & SX9500_COMPSTAT_MASK))
			break;
	}

	if (i < 0) {
		dev_err(&data->client->dev, "initial compensation timed out");
		ret = -ETIMEDOUT;
	}

out:
	regmap_update_bits(data->regmap, SX9500_REG_PROX_CTRL0,
			   SX9500_CHAN_MASK, 0);
	return ret;
}

static int sx9500_init_device(struct iio_dev *indio_dev)
{
	struct sx9500_data *data = iio_priv(indio_dev);
	int ret, i;
	unsigned int val;

	if (data->gpiod_rst) {
		gpiod_set_value_cansleep(data->gpiod_rst, 0);
		usleep_range(1000, 2000);
		gpiod_set_value_cansleep(data->gpiod_rst, 1);
		usleep_range(1000, 2000);
	}

	ret = regmap_write(data->regmap, SX9500_REG_IRQ_MSK, 0);
	if (ret < 0)
		return ret;

	ret = regmap_write(data->regmap, SX9500_REG_RESET,
			   SX9500_SOFT_RESET);
	if (ret < 0)
		return ret;

	ret = regmap_read(data->regmap, SX9500_REG_IRQ_SRC, &val);
	if (ret < 0)
		return ret;

	for (i = 0; i < ARRAY_SIZE(sx9500_default_regs); i++) {
		ret = regmap_write(data->regmap,
				   sx9500_default_regs[i].reg,
				   sx9500_default_regs[i].def);
		if (ret < 0)
			return ret;
	}

	return sx9500_init_compensation(indio_dev);
}

static void sx9500_gpio_probe(struct i2c_client *client,
			      struct sx9500_data *data)
{
	struct device *dev;

	if (!client)
		return;

	dev = &client->dev;

	data->gpiod_rst = devm_gpiod_get(dev, SX9500_GPIO_RESET, GPIOD_OUT_HIGH);
	if (IS_ERR(data->gpiod_rst)) {
		dev_warn(dev, "gpio get reset pin failed\n");
		data->gpiod_rst = NULL;
	}
}

static int sx9500_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	int ret;
	struct iio_dev *indio_dev;
	struct sx9500_data *data;

	indio_dev = devm_iio_device_alloc(&client->dev, sizeof(*data));
	if (indio_dev == NULL)
		return -ENOMEM;

	data = iio_priv(indio_dev);
	data->client = client;
	mutex_init(&data->mutex);
	init_completion(&data->completion);
	data->trigger_enabled = false;

	data->regmap = devm_regmap_init_i2c(client, &sx9500_regmap_config);
	if (IS_ERR(data->regmap))
		return PTR_ERR(data->regmap);

	indio_dev->dev.parent = &client->dev;
	indio_dev->name = SX9500_DRIVER_NAME;
	indio_dev->channels = sx9500_channels;
	indio_dev->num_channels = ARRAY_SIZE(sx9500_channels);
	indio_dev->info = &sx9500_info;
	indio_dev->modes = INDIO_DIRECT_MODE;
	i2c_set_clientdata(client, indio_dev);

	sx9500_gpio_probe(client, data);

	ret = sx9500_init_device(indio_dev);
	if (ret < 0)
		return ret;

	if (client->irq <= 0)
		dev_warn(&client->dev, "no valid irq found\n");
	else {
		ret = devm_request_threaded_irq(&client->dev, client->irq,
				sx9500_irq_handler, sx9500_irq_thread_handler,
				IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
				SX9500_IRQ_NAME, indio_dev);
		if (ret < 0)
			return ret;

		data->trig = devm_iio_trigger_alloc(&client->dev,
				"%s-dev%d", indio_dev->name, indio_dev->id);
		if (!data->trig)
			return -ENOMEM;

		data->trig->dev.parent = &client->dev;
		data->trig->ops = &sx9500_trigger_ops;
		iio_trigger_set_drvdata(data->trig, indio_dev);

		ret = iio_trigger_register(data->trig);
		if (ret)
			return ret;
	}

	ret = iio_triggered_buffer_setup(indio_dev, NULL,
					 sx9500_trigger_handler,
					 &sx9500_buffer_setup_ops);
	if (ret < 0)
		goto out_trigger_unregister;

	ret = iio_device_register(indio_dev);
	if (ret < 0)
		goto out_buffer_cleanup;

	return 0;

out_buffer_cleanup:
	iio_triggered_buffer_cleanup(indio_dev);
out_trigger_unregister:
	if (client->irq > 0)
		iio_trigger_unregister(data->trig);

	return ret;
}

static int sx9500_remove(struct i2c_client *client)
{
	struct iio_dev *indio_dev = i2c_get_clientdata(client);
	struct sx9500_data *data = iio_priv(indio_dev);

	iio_device_unregister(indio_dev);
	iio_triggered_buffer_cleanup(indio_dev);
	if (client->irq > 0)
		iio_trigger_unregister(data->trig);
	kfree(data->buffer);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int sx9500_suspend(struct device *dev)
{
	struct iio_dev *indio_dev = i2c_get_clientdata(to_i2c_client(dev));
	struct sx9500_data *data = iio_priv(indio_dev);
	int ret;

	mutex_lock(&data->mutex);
	ret = regmap_read(data->regmap, SX9500_REG_PROX_CTRL0,
			  &data->suspend_ctrl0);
	if (ret < 0)
		goto out;

	/*
	 * Scan period doesn't matter because when all the sensors are
	 * deactivated the device is in sleep mode.
	 */
	ret = regmap_write(data->regmap, SX9500_REG_PROX_CTRL0, 0);

out:
	mutex_unlock(&data->mutex);
	return ret;
}

static int sx9500_resume(struct device *dev)
{
	struct iio_dev *indio_dev = i2c_get_clientdata(to_i2c_client(dev));
	struct sx9500_data *data = iio_priv(indio_dev);
	int ret;

	mutex_lock(&data->mutex);
	ret = regmap_write(data->regmap, SX9500_REG_PROX_CTRL0,
			   data->suspend_ctrl0);
	mutex_unlock(&data->mutex);

	return ret;
}
#endif /* CONFIG_PM_SLEEP */

static const struct dev_pm_ops sx9500_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(sx9500_suspend, sx9500_resume)
};

static const struct acpi_device_id sx9500_acpi_match[] = {
	{"SSX9500", 0},
	{ },
};
MODULE_DEVICE_TABLE(acpi, sx9500_acpi_match);

static const struct of_device_id sx9500_of_match[] = {
	{ .compatible = "semtech,sx9500", },
	{ }
};
MODULE_DEVICE_TABLE(of, sx9500_of_match);

static const struct i2c_device_id sx9500_id[] = {
	{"sx9500", 0},
	{ },
};
MODULE_DEVICE_TABLE(i2c, sx9500_id);

static struct i2c_driver sx9500_driver = {
	.driver = {
		.name	= SX9500_DRIVER_NAME,
		.acpi_match_table = ACPI_PTR(sx9500_acpi_match),
		.of_match_table = of_match_ptr(sx9500_of_match),
		.pm = &sx9500_pm_ops,
	},
	.probe		= sx9500_probe,
	.remove		= sx9500_remove,
	.id_table	= sx9500_id,
};
module_i2c_driver(sx9500_driver);

MODULE_AUTHOR("Vlad Dogaru <vlad.dogaru@intel.com>");
MODULE_DESCRIPTION("Driver for Semtech SX9500 proximity sensor");
MODULE_LICENSE("GPL v2");

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

#include <linux/iio/iio.h>
#include <linux/iio/buffer.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/events.h>
#include <linux/iio/trigger.h>
#include <linux/iio/triggered_buffer.h>
#include <linux/iio/trigger_consumer.h>

#define SX9500_DRIVER_NAME		"sx9500"
#define SX9500_IRQ_NAME			"sx9500_event"
#define SX9500_GPIO_NAME		"sx9500_gpio"

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

#define SX9500_NUM_CHANNELS		4

struct sx9500_data {
	struct mutex mutex;
	struct i2c_client *client;
	struct iio_trigger *trig;
	struct regmap *regmap;
	/*
	 * Last reading of the proximity status for each channel.  We
	 * only send an event to user space when this changes.
	 */
	bool prox_stat[SX9500_NUM_CHANNELS];
	bool event_enabled[SX9500_NUM_CHANNELS];
	bool trigger_enabled;
	u16 *buffer;
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

static int sx9500_read_proximity(struct sx9500_data *data,
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
			if (iio_buffer_enabled(indio_dev))
				return -EBUSY;
			mutex_lock(&data->mutex);
			ret = sx9500_read_proximity(data, chan, val);
			mutex_unlock(&data->mutex);
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

static irqreturn_t sx9500_irq_thread_handler(int irq, void *private)
{
	struct iio_dev *indio_dev = private;
	struct sx9500_data *data = iio_priv(indio_dev);
	int ret;
	unsigned int val, chan;

	mutex_lock(&data->mutex);

	ret = regmap_read(data->regmap, SX9500_REG_IRQ_SRC, &val);
	if (ret < 0) {
		dev_err(&data->client->dev, "i2c transfer error in irq\n");
		goto out;
	}

	if (!(val & (SX9500_CLOSE_IRQ | SX9500_FAR_IRQ)))
		goto out;

	ret = regmap_read(data->regmap, SX9500_REG_STAT, &val);
	if (ret < 0) {
		dev_err(&data->client->dev, "i2c transfer error in irq\n");
		goto out;
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

		dir = new_prox ? IIO_EV_DIR_FALLING :
			IIO_EV_DIR_RISING;
		ev = IIO_UNMOD_EVENT_CODE(IIO_PROXIMITY,
					  chan,
					  IIO_EV_TYPE_THRESH,
					  dir);
		iio_push_event(indio_dev, ev, iio_get_time_ns());
		data->prox_stat[chan] = new_prox;
	}

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
	int ret, i;
	bool any_active = false;
	unsigned int irqmask;

	if (chan->type != IIO_PROXIMITY || type != IIO_EV_TYPE_THRESH ||
	    dir != IIO_EV_DIR_EITHER)
		return -EINVAL;

	mutex_lock(&data->mutex);

	data->event_enabled[chan->channel] = state;

	for (i = 0; i < SX9500_NUM_CHANNELS; i++)
		if (data->event_enabled[i]) {
			any_active = true;
			break;
		}

	irqmask = SX9500_CLOSE_IRQ | SX9500_FAR_IRQ;
	if (any_active)
		ret = regmap_update_bits(data->regmap, SX9500_REG_IRQ_MSK,
					 irqmask, irqmask);
	else
		ret = regmap_update_bits(data->regmap, SX9500_REG_IRQ_MSK,
					 irqmask, 0);

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

	ret = regmap_update_bits(data->regmap, SX9500_REG_IRQ_MSK,
				 SX9500_CONVDONE_IRQ,
				 state ? SX9500_CONVDONE_IRQ : 0);
	if (ret == 0)
		data->trigger_enabled = state;

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
		ret = sx9500_read_proximity(data, &indio_dev->channels[bit],
					    &val);
		if (ret < 0)
			goto out;

		data->buffer[i++] = val;
	}

	iio_push_to_buffers_with_timestamp(indio_dev, data->buffer,
					   iio_get_time_ns());

out:
	mutex_unlock(&data->mutex);

	iio_trigger_notify_done(indio_dev->trig);

	return IRQ_HANDLED;
}

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
		/* Scan period: 30ms, all sensors enabled. */
		.def = 0x0f,
	},
};

static int sx9500_init_device(struct iio_dev *indio_dev)
{
	struct sx9500_data *data = iio_priv(indio_dev);
	int ret, i;
	unsigned int val;

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

	return 0;
}

static int sx9500_gpio_probe(struct i2c_client *client,
			     struct sx9500_data *data)
{
	struct device *dev;
	struct gpio_desc *gpio;
	int ret;

	if (!client)
		return -EINVAL;

	dev = &client->dev;

	/* data ready gpio interrupt pin */
	gpio = devm_gpiod_get_index(dev, SX9500_GPIO_NAME, 0, GPIOD_IN);
	if (IS_ERR(gpio)) {
		dev_err(dev, "acpi gpio get index failed\n");
		return PTR_ERR(gpio);
	}

	ret = gpiod_to_irq(gpio);

	dev_dbg(dev, "GPIO resource, no:%d irq:%d\n", desc_to_gpio(gpio), ret);

	return ret;
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
	data->trigger_enabled = false;

	data->regmap = devm_regmap_init_i2c(client, &sx9500_regmap_config);
	if (IS_ERR(data->regmap))
		return PTR_ERR(data->regmap);

	sx9500_init_device(indio_dev);

	indio_dev->dev.parent = &client->dev;
	indio_dev->name = SX9500_DRIVER_NAME;
	indio_dev->channels = sx9500_channels;
	indio_dev->num_channels = ARRAY_SIZE(sx9500_channels);
	indio_dev->info = &sx9500_info;
	indio_dev->modes = INDIO_DIRECT_MODE;
	i2c_set_clientdata(client, indio_dev);

	if (client->irq <= 0)
		client->irq = sx9500_gpio_probe(client, data);

	if (client->irq > 0) {
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
					 sx9500_trigger_handler, NULL);
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

static const struct acpi_device_id sx9500_acpi_match[] = {
	{"SSX9500", 0},
	{ },
};
MODULE_DEVICE_TABLE(acpi, sx9500_acpi_match);

static const struct i2c_device_id sx9500_id[] = {
	{"sx9500", 0},
	{}
};
MODULE_DEVICE_TABLE(i2c, sx9500_id);

static struct i2c_driver sx9500_driver = {
	.driver = {
		.name	= SX9500_DRIVER_NAME,
		.acpi_match_table = ACPI_PTR(sx9500_acpi_match),
	},
	.probe		= sx9500_probe,
	.remove		= sx9500_remove,
	.id_table	= sx9500_id,
};
module_i2c_driver(sx9500_driver);

MODULE_AUTHOR("Vlad Dogaru <vlad.dogaru@intel.com>");
MODULE_DESCRIPTION("Driver for Semtech SX9500 proximity sensor");
MODULE_LICENSE("GPL v2");

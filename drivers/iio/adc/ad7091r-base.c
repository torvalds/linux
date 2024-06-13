// SPDX-License-Identifier: GPL-2.0
/*
 * AD7091RX Analog to Digital converter driver
 *
 * Copyright 2014-2019 Analog Devices Inc.
 */

#include <linux/bitops.h>
#include <linux/bitfield.h>
#include <linux/iio/events.h>
#include <linux/iio/iio.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>

#include "ad7091r-base.h"

#define AD7091R_REG_RESULT  0
#define AD7091R_REG_CHANNEL 1
#define AD7091R_REG_CONF    2
#define AD7091R_REG_ALERT   3
#define AD7091R_REG_CH_LOW_LIMIT(ch) ((ch) * 3 + 4)
#define AD7091R_REG_CH_HIGH_LIMIT(ch) ((ch) * 3 + 5)
#define AD7091R_REG_CH_HYSTERESIS(ch) ((ch) * 3 + 6)

/* AD7091R_REG_RESULT */
#define AD7091R_REG_RESULT_CH_ID(x)	    (((x) >> 13) & 0x3)
#define AD7091R_REG_RESULT_CONV_RESULT(x)   ((x) & 0xfff)

/* AD7091R_REG_CONF */
#define AD7091R_REG_CONF_ALERT_EN   BIT(4)
#define AD7091R_REG_CONF_AUTO   BIT(8)
#define AD7091R_REG_CONF_CMD    BIT(10)

#define AD7091R_REG_CONF_MODE_MASK  \
	(AD7091R_REG_CONF_AUTO | AD7091R_REG_CONF_CMD)

enum ad7091r_mode {
	AD7091R_MODE_SAMPLE,
	AD7091R_MODE_COMMAND,
	AD7091R_MODE_AUTOCYCLE,
};

struct ad7091r_state {
	struct device *dev;
	struct regmap *map;
	struct regulator *vref;
	const struct ad7091r_chip_info *chip_info;
	enum ad7091r_mode mode;
	struct mutex lock; /*lock to prevent concurent reads */
};

const struct iio_event_spec ad7091r_events[] = {
	{
		.type = IIO_EV_TYPE_THRESH,
		.dir = IIO_EV_DIR_RISING,
		.mask_separate = BIT(IIO_EV_INFO_VALUE) |
				 BIT(IIO_EV_INFO_ENABLE),
	},
	{
		.type = IIO_EV_TYPE_THRESH,
		.dir = IIO_EV_DIR_FALLING,
		.mask_separate = BIT(IIO_EV_INFO_VALUE) |
				 BIT(IIO_EV_INFO_ENABLE),
	},
	{
		.type = IIO_EV_TYPE_THRESH,
		.dir = IIO_EV_DIR_EITHER,
		.mask_separate = BIT(IIO_EV_INFO_HYSTERESIS),
	},
};
EXPORT_SYMBOL_NS_GPL(ad7091r_events, IIO_AD7091R);

static int ad7091r_set_mode(struct ad7091r_state *st, enum ad7091r_mode mode)
{
	int ret, conf;

	switch (mode) {
	case AD7091R_MODE_SAMPLE:
		conf = 0;
		break;
	case AD7091R_MODE_COMMAND:
		conf = AD7091R_REG_CONF_CMD;
		break;
	case AD7091R_MODE_AUTOCYCLE:
		conf = AD7091R_REG_CONF_AUTO;
		break;
	default:
		return -EINVAL;
	}

	ret = regmap_update_bits(st->map, AD7091R_REG_CONF,
				 AD7091R_REG_CONF_MODE_MASK, conf);
	if (ret)
		return ret;

	st->mode = mode;

	return 0;
}

static int ad7091r_set_channel(struct ad7091r_state *st, unsigned int channel)
{
	unsigned int dummy;
	int ret;

	/* AD7091R_REG_CHANNEL specified which channels to be converted */
	ret = regmap_write(st->map, AD7091R_REG_CHANNEL,
			BIT(channel) | (BIT(channel) << 8));
	if (ret)
		return ret;

	/*
	 * There is a latency of one conversion before the channel conversion
	 * sequence is updated
	 */
	return regmap_read(st->map, AD7091R_REG_RESULT, &dummy);
}

static int ad7091r_read_one(struct iio_dev *iio_dev,
		unsigned int channel, unsigned int *read_val)
{
	struct ad7091r_state *st = iio_priv(iio_dev);
	unsigned int val;
	int ret;

	ret = ad7091r_set_channel(st, channel);
	if (ret)
		return ret;

	ret = regmap_read(st->map, AD7091R_REG_RESULT, &val);
	if (ret)
		return ret;

	if (AD7091R_REG_RESULT_CH_ID(val) != channel)
		return -EIO;

	*read_val = AD7091R_REG_RESULT_CONV_RESULT(val);

	return 0;
}

static int ad7091r_read_raw(struct iio_dev *iio_dev,
			   struct iio_chan_spec const *chan,
			   int *val, int *val2, long m)
{
	struct ad7091r_state *st = iio_priv(iio_dev);
	unsigned int read_val;
	int ret;

	mutex_lock(&st->lock);

	switch (m) {
	case IIO_CHAN_INFO_RAW:
		if (st->mode != AD7091R_MODE_COMMAND) {
			ret = -EBUSY;
			goto unlock;
		}

		ret = ad7091r_read_one(iio_dev, chan->channel, &read_val);
		if (ret)
			goto unlock;

		*val = read_val;
		ret = IIO_VAL_INT;
		break;

	case IIO_CHAN_INFO_SCALE:
		if (st->vref) {
			ret = regulator_get_voltage(st->vref);
			if (ret < 0)
				goto unlock;

			*val = ret / 1000;
		} else {
			*val = st->chip_info->vref_mV;
		}

		*val2 = chan->scan_type.realbits;
		ret = IIO_VAL_FRACTIONAL_LOG2;
		break;

	default:
		ret = -EINVAL;
		break;
	}

unlock:
	mutex_unlock(&st->lock);
	return ret;
}

static int ad7091r_read_event_config(struct iio_dev *indio_dev,
				     const struct iio_chan_spec *chan,
				     enum iio_event_type type,
				     enum iio_event_direction dir)
{
	struct ad7091r_state *st = iio_priv(indio_dev);
	int val, ret;

	switch (dir) {
	case IIO_EV_DIR_RISING:
		ret = regmap_read(st->map,
				  AD7091R_REG_CH_HIGH_LIMIT(chan->channel),
				  &val);
		if (ret)
			return ret;
		return val != AD7091R_HIGH_LIMIT;
	case IIO_EV_DIR_FALLING:
		ret = regmap_read(st->map,
				  AD7091R_REG_CH_LOW_LIMIT(chan->channel),
				  &val);
		if (ret)
			return ret;
		return val != AD7091R_LOW_LIMIT;
	default:
		return -EINVAL;
	}
}

static int ad7091r_write_event_config(struct iio_dev *indio_dev,
				      const struct iio_chan_spec *chan,
				      enum iio_event_type type,
				      enum iio_event_direction dir, int state)
{
	struct ad7091r_state *st = iio_priv(indio_dev);

	if (state) {
		return regmap_set_bits(st->map, AD7091R_REG_CONF,
				       AD7091R_REG_CONF_ALERT_EN);
	} else {
		/*
		 * Set thresholds either to 0 or to 2^12 - 1 as appropriate to
		 * prevent alerts and thus disable event generation.
		 */
		switch (dir) {
		case IIO_EV_DIR_RISING:
			return regmap_write(st->map,
					    AD7091R_REG_CH_HIGH_LIMIT(chan->channel),
					    AD7091R_HIGH_LIMIT);
		case IIO_EV_DIR_FALLING:
			return regmap_write(st->map,
					    AD7091R_REG_CH_LOW_LIMIT(chan->channel),
					    AD7091R_LOW_LIMIT);
		default:
			return -EINVAL;
		}
	}
}

static int ad7091r_read_event_value(struct iio_dev *indio_dev,
				    const struct iio_chan_spec *chan,
				    enum iio_event_type type,
				    enum iio_event_direction dir,
				    enum iio_event_info info, int *val, int *val2)
{
	struct ad7091r_state *st = iio_priv(indio_dev);
	int ret;

	switch (info) {
	case IIO_EV_INFO_VALUE:
		switch (dir) {
		case IIO_EV_DIR_RISING:
			ret = regmap_read(st->map,
					  AD7091R_REG_CH_HIGH_LIMIT(chan->channel),
					  val);
			if (ret)
				return ret;
			return IIO_VAL_INT;
		case IIO_EV_DIR_FALLING:
			ret = regmap_read(st->map,
					  AD7091R_REG_CH_LOW_LIMIT(chan->channel),
					  val);
			if (ret)
				return ret;
			return IIO_VAL_INT;
		default:
			return -EINVAL;
		}
	case IIO_EV_INFO_HYSTERESIS:
		ret = regmap_read(st->map,
				  AD7091R_REG_CH_HYSTERESIS(chan->channel),
				  val);
		if (ret)
			return ret;
		return IIO_VAL_INT;
	default:
		return -EINVAL;
	}
}

static int ad7091r_write_event_value(struct iio_dev *indio_dev,
				     const struct iio_chan_spec *chan,
				     enum iio_event_type type,
				     enum iio_event_direction dir,
				     enum iio_event_info info, int val, int val2)
{
	struct ad7091r_state *st = iio_priv(indio_dev);

	switch (info) {
	case IIO_EV_INFO_VALUE:
		switch (dir) {
		case IIO_EV_DIR_RISING:
			return regmap_write(st->map,
					    AD7091R_REG_CH_HIGH_LIMIT(chan->channel),
					    val);
		case IIO_EV_DIR_FALLING:
			return regmap_write(st->map,
					    AD7091R_REG_CH_LOW_LIMIT(chan->channel),
					    val);
		default:
			return -EINVAL;
		}
	case IIO_EV_INFO_HYSTERESIS:
		return regmap_write(st->map,
				    AD7091R_REG_CH_HYSTERESIS(chan->channel),
				    val);
	default:
		return -EINVAL;
	}
}

static const struct iio_info ad7091r_info = {
	.read_raw = ad7091r_read_raw,
	.read_event_config = &ad7091r_read_event_config,
	.write_event_config = &ad7091r_write_event_config,
	.read_event_value = &ad7091r_read_event_value,
	.write_event_value = &ad7091r_write_event_value,
};

static irqreturn_t ad7091r_event_handler(int irq, void *private)
{
	struct iio_dev *iio_dev = private;
	struct ad7091r_state *st = iio_priv(iio_dev);
	unsigned int i, read_val;
	int ret;
	s64 timestamp = iio_get_time_ns(iio_dev);

	ret = regmap_read(st->map, AD7091R_REG_ALERT, &read_val);
	if (ret)
		return IRQ_HANDLED;

	for (i = 0; i < st->chip_info->num_channels; i++) {
		if (read_val & BIT(i * 2))
			iio_push_event(iio_dev,
					IIO_UNMOD_EVENT_CODE(IIO_VOLTAGE, i,
						IIO_EV_TYPE_THRESH,
						IIO_EV_DIR_RISING), timestamp);
		if (read_val & BIT(i * 2 + 1))
			iio_push_event(iio_dev,
					IIO_UNMOD_EVENT_CODE(IIO_VOLTAGE, i,
						IIO_EV_TYPE_THRESH,
						IIO_EV_DIR_FALLING), timestamp);
	}

	return IRQ_HANDLED;
}

static void ad7091r_remove(void *data)
{
	struct ad7091r_state *st = data;

	regulator_disable(st->vref);
}

int ad7091r_probe(struct device *dev, const char *name,
		const struct ad7091r_chip_info *chip_info,
		struct regmap *map, int irq)
{
	struct iio_dev *iio_dev;
	struct ad7091r_state *st;
	int ret;

	iio_dev = devm_iio_device_alloc(dev, sizeof(*st));
	if (!iio_dev)
		return -ENOMEM;

	st = iio_priv(iio_dev);
	st->dev = dev;
	st->chip_info = chip_info;
	st->map = map;

	iio_dev->name = name;
	iio_dev->info = &ad7091r_info;
	iio_dev->modes = INDIO_DIRECT_MODE;

	iio_dev->num_channels = chip_info->num_channels;
	iio_dev->channels = chip_info->channels;

	if (irq) {
		ret = regmap_update_bits(st->map, AD7091R_REG_CONF,
					 AD7091R_REG_CONF_ALERT_EN, BIT(4));
		if (ret)
			return ret;

		ret = devm_request_threaded_irq(dev, irq, NULL,
				ad7091r_event_handler,
				IRQF_TRIGGER_FALLING | IRQF_ONESHOT, name, iio_dev);
		if (ret)
			return ret;
	}

	st->vref = devm_regulator_get_optional(dev, "vref");
	if (IS_ERR(st->vref)) {
		if (PTR_ERR(st->vref) == -EPROBE_DEFER)
			return -EPROBE_DEFER;

		st->vref = NULL;
		/* Enable internal vref */
		ret = regmap_set_bits(st->map, AD7091R_REG_CONF,
				      AD7091R_REG_CONF_INT_VREF);
		if (ret)
			return dev_err_probe(st->dev, ret,
					     "Error on enable internal reference\n");
	} else {
		ret = regulator_enable(st->vref);
		if (ret)
			return ret;
		ret = devm_add_action_or_reset(dev, ad7091r_remove, st);
		if (ret)
			return ret;
	}

	/* Use command mode by default to convert only desired channels*/
	ret = ad7091r_set_mode(st, AD7091R_MODE_COMMAND);
	if (ret)
		return ret;

	return devm_iio_device_register(dev, iio_dev);
}
EXPORT_SYMBOL_NS_GPL(ad7091r_probe, IIO_AD7091R);

static bool ad7091r_writeable_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case AD7091R_REG_RESULT:
	case AD7091R_REG_ALERT:
		return false;
	default:
		return true;
	}
}

static bool ad7091r_volatile_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case AD7091R_REG_RESULT:
	case AD7091R_REG_ALERT:
		return true;
	default:
		return false;
	}
}

const struct regmap_config ad7091r_regmap_config = {
	.reg_bits = 8,
	.val_bits = 16,
	.writeable_reg = ad7091r_writeable_reg,
	.volatile_reg = ad7091r_volatile_reg,
};
EXPORT_SYMBOL_NS_GPL(ad7091r_regmap_config, IIO_AD7091R);

MODULE_AUTHOR("Beniamin Bia <beniamin.bia@analog.com>");
MODULE_DESCRIPTION("Analog Devices AD7091Rx multi-channel converters");
MODULE_LICENSE("GPL v2");

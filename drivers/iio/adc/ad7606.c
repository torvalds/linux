// SPDX-License-Identifier: GPL-2.0
/*
 * AD7606 SPI ADC driver
 *
 * Copyright 2011 Analog Devices Inc.
 */

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/gpio/consumer.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/regulator/consumer.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/sysfs.h>
#include <linux/util_macros.h>

#include <linux/iio/iio.h>
#include <linux/iio/buffer.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/trigger.h>
#include <linux/iio/triggered_buffer.h>
#include <linux/iio/trigger_consumer.h>

#include "ad7606.h"

/*
 * Scales are computed as 5000/32768 and 10000/32768 respectively,
 * so that when applied to the raw values they provide mV values
 */
static const unsigned int ad7606_scale_avail[2] = {
	152588, 305176
};


static const unsigned int ad7616_sw_scale_avail[3] = {
	76293, 152588, 305176
};

static const unsigned int ad7606_oversampling_avail[7] = {
	1, 2, 4, 8, 16, 32, 64,
};

static const unsigned int ad7616_oversampling_avail[8] = {
	1, 2, 4, 8, 16, 32, 64, 128,
};

static int ad7606_reset(struct ad7606_state *st)
{
	if (st->gpio_reset) {
		gpiod_set_value(st->gpio_reset, 1);
		ndelay(100); /* t_reset >= 100ns */
		gpiod_set_value(st->gpio_reset, 0);
		return 0;
	}

	return -ENODEV;
}

static int ad7606_reg_access(struct iio_dev *indio_dev,
			     unsigned int reg,
			     unsigned int writeval,
			     unsigned int *readval)
{
	struct ad7606_state *st = iio_priv(indio_dev);
	int ret;

	mutex_lock(&st->lock);
	if (readval) {
		ret = st->bops->reg_read(st, reg);
		if (ret < 0)
			goto err_unlock;
		*readval = ret;
		ret = 0;
	} else {
		ret = st->bops->reg_write(st, reg, writeval);
	}
err_unlock:
	mutex_unlock(&st->lock);
	return ret;
}

static int ad7606_read_samples(struct ad7606_state *st)
{
	unsigned int num = st->chip_info->num_channels - 1;
	u16 *data = st->data;
	int ret;

	/*
	 * The frstdata signal is set to high while and after reading the sample
	 * of the first channel and low for all other channels. This can be used
	 * to check that the incoming data is correctly aligned. During normal
	 * operation the data should never become unaligned, but some glitch or
	 * electrostatic discharge might cause an extra read or clock cycle.
	 * Monitoring the frstdata signal allows to recover from such failure
	 * situations.
	 */

	if (st->gpio_frstdata) {
		ret = st->bops->read_block(st->dev, 1, data);
		if (ret)
			return ret;

		if (!gpiod_get_value(st->gpio_frstdata)) {
			ad7606_reset(st);
			return -EIO;
		}

		data++;
		num--;
	}

	return st->bops->read_block(st->dev, num, data);
}

static irqreturn_t ad7606_trigger_handler(int irq, void *p)
{
	struct iio_poll_func *pf = p;
	struct iio_dev *indio_dev = pf->indio_dev;
	struct ad7606_state *st = iio_priv(indio_dev);
	int ret;

	mutex_lock(&st->lock);

	ret = ad7606_read_samples(st);
	if (ret == 0)
		iio_push_to_buffers_with_timestamp(indio_dev, st->data,
						   iio_get_time_ns(indio_dev));

	iio_trigger_notify_done(indio_dev->trig);
	/* The rising edge of the CONVST signal starts a new conversion. */
	gpiod_set_value(st->gpio_convst, 1);

	mutex_unlock(&st->lock);

	return IRQ_HANDLED;
}

static int ad7606_scan_direct(struct iio_dev *indio_dev, unsigned int ch)
{
	struct ad7606_state *st = iio_priv(indio_dev);
	int ret;

	gpiod_set_value(st->gpio_convst, 1);
	ret = wait_for_completion_timeout(&st->completion,
					  msecs_to_jiffies(1000));
	if (!ret) {
		ret = -ETIMEDOUT;
		goto error_ret;
	}

	ret = ad7606_read_samples(st);
	if (ret == 0)
		ret = st->data[ch];

error_ret:
	gpiod_set_value(st->gpio_convst, 0);

	return ret;
}

static int ad7606_read_raw(struct iio_dev *indio_dev,
			   struct iio_chan_spec const *chan,
			   int *val,
			   int *val2,
			   long m)
{
	int ret, ch = 0;
	struct ad7606_state *st = iio_priv(indio_dev);

	switch (m) {
	case IIO_CHAN_INFO_RAW:
		ret = iio_device_claim_direct_mode(indio_dev);
		if (ret)
			return ret;

		ret = ad7606_scan_direct(indio_dev, chan->address);
		iio_device_release_direct_mode(indio_dev);

		if (ret < 0)
			return ret;
		*val = (short)ret;
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		if (st->sw_mode_en)
			ch = chan->address;
		*val = 0;
		*val2 = st->scale_avail[st->range[ch]];
		return IIO_VAL_INT_PLUS_MICRO;
	case IIO_CHAN_INFO_OVERSAMPLING_RATIO:
		*val = st->oversampling;
		return IIO_VAL_INT;
	}
	return -EINVAL;
}

static ssize_t ad7606_show_avail(char *buf, const unsigned int *vals,
				 unsigned int n, bool micros)
{
	size_t len = 0;
	int i;

	for (i = 0; i < n; i++) {
		len += scnprintf(buf + len, PAGE_SIZE - len,
			micros ? "0.%06u " : "%u ", vals[i]);
	}
	buf[len - 1] = '\n';

	return len;
}

static ssize_t in_voltage_scale_available_show(struct device *dev,
					       struct device_attribute *attr,
					       char *buf)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct ad7606_state *st = iio_priv(indio_dev);

	return ad7606_show_avail(buf, st->scale_avail, st->num_scales, true);
}

static IIO_DEVICE_ATTR_RO(in_voltage_scale_available, 0);

static int ad7606_write_scale_hw(struct iio_dev *indio_dev, int ch, int val)
{
	struct ad7606_state *st = iio_priv(indio_dev);

	gpiod_set_value(st->gpio_range, val);

	return 0;
}

static int ad7606_write_os_hw(struct iio_dev *indio_dev, int val)
{
	struct ad7606_state *st = iio_priv(indio_dev);
	DECLARE_BITMAP(values, 3);

	values[0] = val;

	gpiod_set_array_value(ARRAY_SIZE(values), st->gpio_os->desc,
			      st->gpio_os->info, values);

	/* AD7616 requires a reset to update value */
	if (st->chip_info->os_req_reset)
		ad7606_reset(st);

	return 0;
}

static int ad7606_write_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan,
			    int val,
			    int val2,
			    long mask)
{
	struct ad7606_state *st = iio_priv(indio_dev);
	int i, ret, ch = 0;

	switch (mask) {
	case IIO_CHAN_INFO_SCALE:
		mutex_lock(&st->lock);
		i = find_closest(val2, st->scale_avail, st->num_scales);
		if (st->sw_mode_en)
			ch = chan->address;
		ret = st->write_scale(indio_dev, ch, i);
		if (ret < 0) {
			mutex_unlock(&st->lock);
			return ret;
		}
		st->range[ch] = i;
		mutex_unlock(&st->lock);

		return 0;
	case IIO_CHAN_INFO_OVERSAMPLING_RATIO:
		if (val2)
			return -EINVAL;
		i = find_closest(val, st->oversampling_avail,
				 st->num_os_ratios);
		mutex_lock(&st->lock);
		ret = st->write_os(indio_dev, i);
		if (ret < 0) {
			mutex_unlock(&st->lock);
			return ret;
		}
		st->oversampling = st->oversampling_avail[i];
		mutex_unlock(&st->lock);

		return 0;
	default:
		return -EINVAL;
	}
}

static ssize_t ad7606_oversampling_ratio_avail(struct device *dev,
					       struct device_attribute *attr,
					       char *buf)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct ad7606_state *st = iio_priv(indio_dev);

	return ad7606_show_avail(buf, st->oversampling_avail,
				 st->num_os_ratios, false);
}

static IIO_DEVICE_ATTR(oversampling_ratio_available, 0444,
		       ad7606_oversampling_ratio_avail, NULL, 0);

static struct attribute *ad7606_attributes_os_and_range[] = {
	&iio_dev_attr_in_voltage_scale_available.dev_attr.attr,
	&iio_dev_attr_oversampling_ratio_available.dev_attr.attr,
	NULL,
};

static const struct attribute_group ad7606_attribute_group_os_and_range = {
	.attrs = ad7606_attributes_os_and_range,
};

static struct attribute *ad7606_attributes_os[] = {
	&iio_dev_attr_oversampling_ratio_available.dev_attr.attr,
	NULL,
};

static const struct attribute_group ad7606_attribute_group_os = {
	.attrs = ad7606_attributes_os,
};

static struct attribute *ad7606_attributes_range[] = {
	&iio_dev_attr_in_voltage_scale_available.dev_attr.attr,
	NULL,
};

static const struct attribute_group ad7606_attribute_group_range = {
	.attrs = ad7606_attributes_range,
};

static const struct iio_chan_spec ad7605_channels[] = {
	IIO_CHAN_SOFT_TIMESTAMP(4),
	AD7605_CHANNEL(0),
	AD7605_CHANNEL(1),
	AD7605_CHANNEL(2),
	AD7605_CHANNEL(3),
};

static const struct iio_chan_spec ad7606_channels[] = {
	IIO_CHAN_SOFT_TIMESTAMP(8),
	AD7606_CHANNEL(0),
	AD7606_CHANNEL(1),
	AD7606_CHANNEL(2),
	AD7606_CHANNEL(3),
	AD7606_CHANNEL(4),
	AD7606_CHANNEL(5),
	AD7606_CHANNEL(6),
	AD7606_CHANNEL(7),
};

/*
 * The current assumption that this driver makes for AD7616, is that it's
 * working in Hardware Mode with Serial, Burst and Sequencer modes activated.
 * To activate them, following pins must be pulled high:
 *	-SER/PAR
 *	-SEQEN
 * And following pins must be pulled low:
 *	-WR/BURST
 *	-DB4/SER1W
 */
static const struct iio_chan_spec ad7616_channels[] = {
	IIO_CHAN_SOFT_TIMESTAMP(16),
	AD7606_CHANNEL(0),
	AD7606_CHANNEL(1),
	AD7606_CHANNEL(2),
	AD7606_CHANNEL(3),
	AD7606_CHANNEL(4),
	AD7606_CHANNEL(5),
	AD7606_CHANNEL(6),
	AD7606_CHANNEL(7),
	AD7606_CHANNEL(8),
	AD7606_CHANNEL(9),
	AD7606_CHANNEL(10),
	AD7606_CHANNEL(11),
	AD7606_CHANNEL(12),
	AD7606_CHANNEL(13),
	AD7606_CHANNEL(14),
	AD7606_CHANNEL(15),
};

static const struct ad7606_chip_info ad7606_chip_info_tbl[] = {
	/* More devices added in future */
	[ID_AD7605_4] = {
		.channels = ad7605_channels,
		.num_channels = 5,
	},
	[ID_AD7606_8] = {
		.channels = ad7606_channels,
		.num_channels = 9,
		.oversampling_avail = ad7606_oversampling_avail,
		.oversampling_num = ARRAY_SIZE(ad7606_oversampling_avail),
	},
	[ID_AD7606_6] = {
		.channels = ad7606_channels,
		.num_channels = 7,
		.oversampling_avail = ad7606_oversampling_avail,
		.oversampling_num = ARRAY_SIZE(ad7606_oversampling_avail),
	},
	[ID_AD7606_4] = {
		.channels = ad7606_channels,
		.num_channels = 5,
		.oversampling_avail = ad7606_oversampling_avail,
		.oversampling_num = ARRAY_SIZE(ad7606_oversampling_avail),
	},
	[ID_AD7606B] = {
		.channels = ad7606_channels,
		.num_channels = 9,
		.oversampling_avail = ad7606_oversampling_avail,
		.oversampling_num = ARRAY_SIZE(ad7606_oversampling_avail),
	},
	[ID_AD7616] = {
		.channels = ad7616_channels,
		.num_channels = 17,
		.oversampling_avail = ad7616_oversampling_avail,
		.oversampling_num = ARRAY_SIZE(ad7616_oversampling_avail),
		.os_req_reset = true,
		.init_delay_ms = 15,
	},
};

static int ad7606_request_gpios(struct ad7606_state *st)
{
	struct device *dev = st->dev;

	st->gpio_convst = devm_gpiod_get(dev, "adi,conversion-start",
					 GPIOD_OUT_LOW);
	if (IS_ERR(st->gpio_convst))
		return PTR_ERR(st->gpio_convst);

	st->gpio_reset = devm_gpiod_get_optional(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(st->gpio_reset))
		return PTR_ERR(st->gpio_reset);

	st->gpio_range = devm_gpiod_get_optional(dev, "adi,range",
						 GPIOD_OUT_LOW);
	if (IS_ERR(st->gpio_range))
		return PTR_ERR(st->gpio_range);

	st->gpio_standby = devm_gpiod_get_optional(dev, "standby",
						   GPIOD_OUT_HIGH);
	if (IS_ERR(st->gpio_standby))
		return PTR_ERR(st->gpio_standby);

	st->gpio_frstdata = devm_gpiod_get_optional(dev, "adi,first-data",
						    GPIOD_IN);
	if (IS_ERR(st->gpio_frstdata))
		return PTR_ERR(st->gpio_frstdata);

	if (!st->chip_info->oversampling_num)
		return 0;

	st->gpio_os = devm_gpiod_get_array_optional(dev,
						    "adi,oversampling-ratio",
						    GPIOD_OUT_LOW);
	return PTR_ERR_OR_ZERO(st->gpio_os);
}

/*
 * The BUSY signal indicates when conversions are in progress, so when a rising
 * edge of CONVST is applied, BUSY goes logic high and transitions low at the
 * end of the entire conversion process. The falling edge of the BUSY signal
 * triggers this interrupt.
 */
static irqreturn_t ad7606_interrupt(int irq, void *dev_id)
{
	struct iio_dev *indio_dev = dev_id;
	struct ad7606_state *st = iio_priv(indio_dev);

	if (iio_buffer_enabled(indio_dev)) {
		gpiod_set_value(st->gpio_convst, 0);
		iio_trigger_poll_chained(st->trig);
	} else {
		complete(&st->completion);
	}

	return IRQ_HANDLED;
};

static int ad7606_validate_trigger(struct iio_dev *indio_dev,
				   struct iio_trigger *trig)
{
	struct ad7606_state *st = iio_priv(indio_dev);

	if (st->trig != trig)
		return -EINVAL;

	return 0;
}

static int ad7606_buffer_postenable(struct iio_dev *indio_dev)
{
	struct ad7606_state *st = iio_priv(indio_dev);

	gpiod_set_value(st->gpio_convst, 1);

	return 0;
}

static int ad7606_buffer_predisable(struct iio_dev *indio_dev)
{
	struct ad7606_state *st = iio_priv(indio_dev);

	gpiod_set_value(st->gpio_convst, 0);

	return 0;
}

static const struct iio_buffer_setup_ops ad7606_buffer_ops = {
	.postenable = &ad7606_buffer_postenable,
	.predisable = &ad7606_buffer_predisable,
};

static const struct iio_info ad7606_info_no_os_or_range = {
	.read_raw = &ad7606_read_raw,
	.validate_trigger = &ad7606_validate_trigger,
};

static const struct iio_info ad7606_info_os_and_range = {
	.read_raw = &ad7606_read_raw,
	.write_raw = &ad7606_write_raw,
	.attrs = &ad7606_attribute_group_os_and_range,
	.validate_trigger = &ad7606_validate_trigger,
};

static const struct iio_info ad7606_info_os_range_and_debug = {
	.read_raw = &ad7606_read_raw,
	.write_raw = &ad7606_write_raw,
	.debugfs_reg_access = &ad7606_reg_access,
	.attrs = &ad7606_attribute_group_os_and_range,
	.validate_trigger = &ad7606_validate_trigger,
};

static const struct iio_info ad7606_info_os = {
	.read_raw = &ad7606_read_raw,
	.write_raw = &ad7606_write_raw,
	.attrs = &ad7606_attribute_group_os,
	.validate_trigger = &ad7606_validate_trigger,
};

static const struct iio_info ad7606_info_range = {
	.read_raw = &ad7606_read_raw,
	.write_raw = &ad7606_write_raw,
	.attrs = &ad7606_attribute_group_range,
	.validate_trigger = &ad7606_validate_trigger,
};

static const struct iio_trigger_ops ad7606_trigger_ops = {
	.validate_device = iio_trigger_validate_own_device,
};

static void ad7606_regulator_disable(void *data)
{
	struct ad7606_state *st = data;

	regulator_disable(st->reg);
}

int ad7606_probe(struct device *dev, int irq, void __iomem *base_address,
		 const char *name, unsigned int id,
		 const struct ad7606_bus_ops *bops)
{
	struct ad7606_state *st;
	int ret;
	struct iio_dev *indio_dev;

	indio_dev = devm_iio_device_alloc(dev, sizeof(*st));
	if (!indio_dev)
		return -ENOMEM;

	st = iio_priv(indio_dev);
	dev_set_drvdata(dev, indio_dev);

	st->dev = dev;
	mutex_init(&st->lock);
	st->bops = bops;
	st->base_address = base_address;
	/* tied to logic low, analog input range is +/- 5V */
	st->range[0] = 0;
	st->oversampling = 1;
	st->scale_avail = ad7606_scale_avail;
	st->num_scales = ARRAY_SIZE(ad7606_scale_avail);

	st->reg = devm_regulator_get(dev, "avcc");
	if (IS_ERR(st->reg))
		return PTR_ERR(st->reg);

	ret = regulator_enable(st->reg);
	if (ret) {
		dev_err(dev, "Failed to enable specified AVcc supply\n");
		return ret;
	}

	ret = devm_add_action_or_reset(dev, ad7606_regulator_disable, st);
	if (ret)
		return ret;

	st->chip_info = &ad7606_chip_info_tbl[id];

	if (st->chip_info->oversampling_num) {
		st->oversampling_avail = st->chip_info->oversampling_avail;
		st->num_os_ratios = st->chip_info->oversampling_num;
	}

	ret = ad7606_request_gpios(st);
	if (ret)
		return ret;

	if (st->gpio_os) {
		if (st->gpio_range)
			indio_dev->info = &ad7606_info_os_and_range;
		else
			indio_dev->info = &ad7606_info_os;
	} else {
		if (st->gpio_range)
			indio_dev->info = &ad7606_info_range;
		else
			indio_dev->info = &ad7606_info_no_os_or_range;
	}
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->name = name;
	indio_dev->channels = st->chip_info->channels;
	indio_dev->num_channels = st->chip_info->num_channels;

	init_completion(&st->completion);

	ret = ad7606_reset(st);
	if (ret)
		dev_warn(st->dev, "failed to RESET: no RESET GPIO specified\n");

	/* AD7616 requires al least 15ms to reconfigure after a reset */
	if (st->chip_info->init_delay_ms) {
		if (msleep_interruptible(st->chip_info->init_delay_ms))
			return -ERESTARTSYS;
	}

	st->write_scale = ad7606_write_scale_hw;
	st->write_os = ad7606_write_os_hw;

	if (st->bops->sw_mode_config)
		st->sw_mode_en = device_property_present(st->dev,
							 "adi,sw-mode");

	if (st->sw_mode_en) {
		/* Scale of 0.076293 is only available in sw mode */
		st->scale_avail = ad7616_sw_scale_avail;
		st->num_scales = ARRAY_SIZE(ad7616_sw_scale_avail);

		/* After reset, in software mode, ±10 V is set by default */
		memset32(st->range, 2, ARRAY_SIZE(st->range));
		indio_dev->info = &ad7606_info_os_range_and_debug;

		ret = st->bops->sw_mode_config(indio_dev);
		if (ret < 0)
			return ret;
	}

	st->trig = devm_iio_trigger_alloc(dev, "%s-dev%d",
					  indio_dev->name,
					  iio_device_id(indio_dev));
	if (!st->trig)
		return -ENOMEM;

	st->trig->ops = &ad7606_trigger_ops;
	iio_trigger_set_drvdata(st->trig, indio_dev);
	ret = devm_iio_trigger_register(dev, st->trig);
	if (ret)
		return ret;

	indio_dev->trig = iio_trigger_get(st->trig);

	ret = devm_request_threaded_irq(dev, irq,
					NULL,
					&ad7606_interrupt,
					IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
					name, indio_dev);
	if (ret)
		return ret;

	ret = devm_iio_triggered_buffer_setup(dev, indio_dev,
					      &iio_pollfunc_store_time,
					      &ad7606_trigger_handler,
					      &ad7606_buffer_ops);
	if (ret)
		return ret;

	return devm_iio_device_register(dev, indio_dev);
}
EXPORT_SYMBOL_NS_GPL(ad7606_probe, IIO_AD7606);

#ifdef CONFIG_PM_SLEEP

static int ad7606_suspend(struct device *dev)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct ad7606_state *st = iio_priv(indio_dev);

	if (st->gpio_standby) {
		gpiod_set_value(st->gpio_range, 1);
		gpiod_set_value(st->gpio_standby, 0);
	}

	return 0;
}

static int ad7606_resume(struct device *dev)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct ad7606_state *st = iio_priv(indio_dev);

	if (st->gpio_standby) {
		gpiod_set_value(st->gpio_range, st->range[0]);
		gpiod_set_value(st->gpio_standby, 1);
		ad7606_reset(st);
	}

	return 0;
}

SIMPLE_DEV_PM_OPS(ad7606_pm_ops, ad7606_suspend, ad7606_resume);
EXPORT_SYMBOL_NS_GPL(ad7606_pm_ops, IIO_AD7606);

#endif

MODULE_AUTHOR("Michael Hennerich <michael.hennerich@analog.com>");
MODULE_DESCRIPTION("Analog Devices AD7606 ADC");
MODULE_LICENSE("GPL v2");

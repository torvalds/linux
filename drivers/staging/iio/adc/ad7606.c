/*
 * AD7606 SPI ADC driver
 *
 * Copyright 2011 Analog Devices Inc.
 *
 * Licensed under the GPL-2.
 */

#include <linux/interrupt.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/sysfs.h>
#include <linux/regulator/consumer.h>
#include <linux/err.h>
#include <linux/gpio/consumer.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/module.h>

#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/buffer.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/iio/triggered_buffer.h>

#include "ad7606.h"

/*
 * Scales are computed as 5000/32768 and 10000/32768 respectively,
 * so that when applied to the raw values they provide mV values
 */
static const unsigned int scale_avail[2][2] = {
	{0, 152588}, {0, 305176}
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

static int ad7606_read_samples(struct ad7606_state *st)
{
	unsigned int num = st->chip_info->num_channels;
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
	struct ad7606_state *st = iio_priv(pf->indio_dev);

	gpiod_set_value(st->gpio_convst, 1);

	return IRQ_HANDLED;
}

/**
 * ad7606_poll_bh_to_ring() bh of trigger launched polling to ring buffer
 * @work_s:	the work struct through which this was scheduled
 *
 * Currently there is no option in this driver to disable the saving of
 * timestamps within the ring.
 * I think the one copy of this at a time was to avoid problems if the
 * trigger was set far too high and the reads then locked up the computer.
 **/
static void ad7606_poll_bh_to_ring(struct work_struct *work_s)
{
	struct ad7606_state *st = container_of(work_s, struct ad7606_state,
						poll_work);
	struct iio_dev *indio_dev = iio_priv_to_dev(st);
	int ret;

	ret = ad7606_read_samples(st);
	if (ret == 0)
		iio_push_to_buffers_with_timestamp(indio_dev, st->data,
						   iio_get_time_ns(indio_dev));

	gpiod_set_value(st->gpio_convst, 0);
	iio_trigger_notify_done(indio_dev->trig);
}

static int ad7606_scan_direct(struct iio_dev *indio_dev, unsigned int ch)
{
	struct ad7606_state *st = iio_priv(indio_dev);
	int ret;

	st->done = false;
	gpiod_set_value(st->gpio_convst, 1);

	ret = wait_event_interruptible(st->wq_data_avail, st->done);
	if (ret)
		goto error_ret;

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
	int ret;
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
		*val = scale_avail[st->range][0];
		*val2 = scale_avail[st->range][1];
		return IIO_VAL_INT_PLUS_MICRO;
	case IIO_CHAN_INFO_OVERSAMPLING_RATIO:
		*val = st->oversampling;
		return IIO_VAL_INT;
	}
	return -EINVAL;
}

static ssize_t in_voltage_scale_available_show(struct device *dev,
					       struct device_attribute *attr,
					       char *buf)
{
	int i, len = 0;

	for (i = 0; i < ARRAY_SIZE(scale_avail); i++)
		len += scnprintf(buf + len, PAGE_SIZE - len, "%d.%06u ",
				 scale_avail[i][0], scale_avail[i][1]);

	buf[len - 1] = '\n';

	return len;
}

static IIO_DEVICE_ATTR_RO(in_voltage_scale_available, 0);

static int ad7606_oversampling_get_index(unsigned int val)
{
	unsigned char supported[] = {1, 2, 4, 8, 16, 32, 64};
	int i;

	for (i = 0; i < ARRAY_SIZE(supported); i++)
		if (val == supported[i])
			return i;

	return -EINVAL;
}

static int ad7606_write_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan,
			    int val,
			    int val2,
			    long mask)
{
	struct ad7606_state *st = iio_priv(indio_dev);
	DECLARE_BITMAP(values, 3);
	int ret, i;

	switch (mask) {
	case IIO_CHAN_INFO_SCALE:
		ret = -EINVAL;
		mutex_lock(&st->lock);
		for (i = 0; i < ARRAY_SIZE(scale_avail); i++)
			if (val2 == scale_avail[i][1]) {
				gpiod_set_value(st->gpio_range, i);
				st->range = i;

				ret = 0;
				break;
			}
		mutex_unlock(&st->lock);

		return ret;
	case IIO_CHAN_INFO_OVERSAMPLING_RATIO:
		if (val2)
			return -EINVAL;
		ret = ad7606_oversampling_get_index(val);
		if (ret < 0)
			return ret;

		values[0] = ret;

		mutex_lock(&st->lock);
		gpiod_set_array_value(3, st->gpio_os->desc, st->gpio_os->info,
				      values);
		st->oversampling = val;
		mutex_unlock(&st->lock);

		return 0;
	default:
		return -EINVAL;
	}
}

static IIO_CONST_ATTR(oversampling_ratio_available, "1 2 4 8 16 32 64");

static struct attribute *ad7606_attributes_os_and_range[] = {
	&iio_dev_attr_in_voltage_scale_available.dev_attr.attr,
	&iio_const_attr_oversampling_ratio_available.dev_attr.attr,
	NULL,
};

static const struct attribute_group ad7606_attribute_group_os_and_range = {
	.attrs = ad7606_attributes_os_and_range,
};

static struct attribute *ad7606_attributes_os[] = {
	&iio_const_attr_oversampling_ratio_available.dev_attr.attr,
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

#define AD760X_CHANNEL(num, mask)				\
	{							\
		.type = IIO_VOLTAGE,				\
		.indexed = 1,					\
		.channel = num,					\
		.address = num,					\
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),	\
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE),\
		.info_mask_shared_by_all = mask,		\
		.scan_index = num,				\
		.scan_type = {					\
			.sign = 's',				\
			.realbits = 16,				\
			.storagebits = 16,			\
			.endianness = IIO_CPU,			\
		},						\
	}

#define AD7605_CHANNEL(num)	\
	AD760X_CHANNEL(num, 0)

#define AD7606_CHANNEL(num)	\
	AD760X_CHANNEL(num, BIT(IIO_CHAN_INFO_OVERSAMPLING_RATIO))

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

static const struct ad7606_chip_info ad7606_chip_info_tbl[] = {
	/*
	 * More devices added in future
	 */
	[ID_AD7605_4] = {
		.channels = ad7605_channels,
		.num_channels = 5,
	},
	[ID_AD7606_8] = {
		.channels = ad7606_channels,
		.num_channels = 9,
		.has_oversampling = true,
	},
	[ID_AD7606_6] = {
		.channels = ad7606_channels,
		.num_channels = 7,
		.has_oversampling = true,
	},
	[ID_AD7606_4] = {
		.channels = ad7606_channels,
		.num_channels = 5,
		.has_oversampling = true,
	},
};

static int ad7606_request_gpios(struct ad7606_state *st)
{
	struct device *dev = st->dev;

	st->gpio_convst = devm_gpiod_get(dev, "conversion-start",
					 GPIOD_OUT_LOW);
	if (IS_ERR(st->gpio_convst))
		return PTR_ERR(st->gpio_convst);

	st->gpio_reset = devm_gpiod_get_optional(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(st->gpio_reset))
		return PTR_ERR(st->gpio_reset);

	st->gpio_range = devm_gpiod_get_optional(dev, "range", GPIOD_OUT_LOW);
	if (IS_ERR(st->gpio_range))
		return PTR_ERR(st->gpio_range);

	st->gpio_standby = devm_gpiod_get_optional(dev, "standby",
						   GPIOD_OUT_HIGH);
	if (IS_ERR(st->gpio_standby))
		return PTR_ERR(st->gpio_standby);

	st->gpio_frstdata = devm_gpiod_get_optional(dev, "first-data",
						    GPIOD_IN);
	if (IS_ERR(st->gpio_frstdata))
		return PTR_ERR(st->gpio_frstdata);

	if (!st->chip_info->has_oversampling)
		return 0;

	st->gpio_os = devm_gpiod_get_array_optional(dev, "oversampling-ratio",
						    GPIOD_OUT_LOW);
	return PTR_ERR_OR_ZERO(st->gpio_os);
}

/**
 *  Interrupt handler
 */
static irqreturn_t ad7606_interrupt(int irq, void *dev_id)
{
	struct iio_dev *indio_dev = dev_id;
	struct ad7606_state *st = iio_priv(indio_dev);

	if (iio_buffer_enabled(indio_dev)) {
		schedule_work(&st->poll_work);
	} else {
		st->done = true;
		wake_up_interruptible(&st->wq_data_avail);
	}

	return IRQ_HANDLED;
};

static const struct iio_info ad7606_info_no_os_or_range = {
	.read_raw = &ad7606_read_raw,
};

static const struct iio_info ad7606_info_os_and_range = {
	.read_raw = &ad7606_read_raw,
	.write_raw = &ad7606_write_raw,
	.attrs = &ad7606_attribute_group_os_and_range,
};

static const struct iio_info ad7606_info_os = {
	.read_raw = &ad7606_read_raw,
	.write_raw = &ad7606_write_raw,
	.attrs = &ad7606_attribute_group_os,
};

static const struct iio_info ad7606_info_range = {
	.read_raw = &ad7606_read_raw,
	.write_raw = &ad7606_write_raw,
	.attrs = &ad7606_attribute_group_range,
};

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

	st->dev = dev;
	mutex_init(&st->lock);
	st->bops = bops;
	st->base_address = base_address;
	/* tied to logic low, analog input range is +/- 5V */
	st->range = 0;
	st->oversampling = 1;
	INIT_WORK(&st->poll_work, &ad7606_poll_bh_to_ring);

	st->reg = devm_regulator_get(dev, "avcc");
	if (IS_ERR(st->reg))
		return PTR_ERR(st->reg);

	ret = regulator_enable(st->reg);
	if (ret) {
		dev_err(dev, "Failed to enable specified AVcc supply\n");
		return ret;
	}

	st->chip_info = &ad7606_chip_info_tbl[id];

	ret = ad7606_request_gpios(st);
	if (ret)
		goto error_disable_reg;

	indio_dev->dev.parent = dev;
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

	init_waitqueue_head(&st->wq_data_avail);

	ret = ad7606_reset(st);
	if (ret)
		dev_warn(st->dev, "failed to RESET: no RESET GPIO specified\n");

	ret = request_irq(irq, ad7606_interrupt, IRQF_TRIGGER_FALLING, name,
			  indio_dev);
	if (ret)
		goto error_disable_reg;

	ret = iio_triggered_buffer_setup(indio_dev, &ad7606_trigger_handler,
					 NULL, NULL);
	if (ret)
		goto error_free_irq;

	ret = iio_device_register(indio_dev);
	if (ret)
		goto error_unregister_ring;

	dev_set_drvdata(dev, indio_dev);

	return 0;
error_unregister_ring:
	iio_triggered_buffer_cleanup(indio_dev);

error_free_irq:
	free_irq(irq, indio_dev);

error_disable_reg:
	regulator_disable(st->reg);
	return ret;
}
EXPORT_SYMBOL_GPL(ad7606_probe);

int ad7606_remove(struct device *dev, int irq)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct ad7606_state *st = iio_priv(indio_dev);

	iio_device_unregister(indio_dev);
	iio_triggered_buffer_cleanup(indio_dev);

	free_irq(irq, indio_dev);
	regulator_disable(st->reg);

	return 0;
}
EXPORT_SYMBOL_GPL(ad7606_remove);

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
		gpiod_set_value(st->gpio_range, st->range);
		gpiod_set_value(st->gpio_standby, 1);
		ad7606_reset(st);
	}

	return 0;
}

SIMPLE_DEV_PM_OPS(ad7606_pm_ops, ad7606_suspend, ad7606_resume);
EXPORT_SYMBOL_GPL(ad7606_pm_ops);

#endif

MODULE_AUTHOR("Michael Hennerich <michael.hennerich@analog.com>");
MODULE_DESCRIPTION("Analog Devices AD7606 ADC");
MODULE_LICENSE("GPL v2");

// SPDX-License-Identifier: GPL-2.0+
/*
 * as3935.c - Support for AS3935 Franklin lightning sensor
 *
 * Copyright (C) 2014, 2017-2018
 * Author: Matt Ranostay <matt.ranostay@konsulko.com>
 */

#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/workqueue.h>
#include <linux/devm-helpers.h>
#include <linux/mutex.h>
#include <linux/err.h>
#include <linux/irq.h>
#include <linux/spi/spi.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/trigger.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/iio/buffer.h>
#include <linux/iio/triggered_buffer.h>

#define AS3935_AFE_GAIN		0x00
#define AS3935_AFE_MASK		0x3F
#define AS3935_AFE_GAIN_MAX	0x1F
#define AS3935_AFE_PWR_BIT	BIT(0)

#define AS3935_NFLWDTH		0x01
#define AS3935_NFLWDTH_MASK	0x7f

#define AS3935_INT		0x03
#define AS3935_INT_MASK		0x0f
#define AS3935_DISTURB_INT	BIT(2)
#define AS3935_EVENT_INT	BIT(3)
#define AS3935_NOISE_INT	BIT(0)

#define AS3935_DATA		0x07
#define AS3935_DATA_MASK	0x3F

#define AS3935_TUNE_CAP		0x08
#define AS3935_DEFAULTS		0x3C
#define AS3935_CALIBRATE	0x3D

#define AS3935_READ_DATA	BIT(14)
#define AS3935_ADDRESS(x)	((x) << 8)

#define MAX_PF_CAP		120
#define TUNE_CAP_DIV		8

struct as3935_state {
	struct spi_device *spi;
	struct iio_trigger *trig;
	struct mutex lock;
	struct delayed_work work;

	unsigned long noise_tripped;
	u32 tune_cap;
	u32 nflwdth_reg;
	/* Ensure timestamp is naturally aligned */
	struct {
		u8 chan;
		s64 timestamp __aligned(8);
	} scan;
	u8 buf[2] __aligned(IIO_DMA_MINALIGN);
};

static const struct iio_chan_spec as3935_channels[] = {
	{
		.type           = IIO_PROXIMITY,
		.info_mask_separate =
			BIT(IIO_CHAN_INFO_RAW) |
			BIT(IIO_CHAN_INFO_PROCESSED) |
			BIT(IIO_CHAN_INFO_SCALE),
		.scan_index     = 0,
		.scan_type = {
			.sign           = 'u',
			.realbits       = 6,
			.storagebits    = 8,
		},
	},
	IIO_CHAN_SOFT_TIMESTAMP(1),
};

static int as3935_read(struct as3935_state *st, unsigned int reg, int *val)
{
	u8 cmd;
	int ret;

	cmd = (AS3935_READ_DATA | AS3935_ADDRESS(reg)) >> 8;
	ret = spi_w8r8(st->spi, cmd);
	if (ret < 0)
		return ret;
	*val = ret;

	return 0;
}

static int as3935_write(struct as3935_state *st,
				unsigned int reg,
				unsigned int val)
{
	u8 *buf = st->buf;

	buf[0] = AS3935_ADDRESS(reg) >> 8;
	buf[1] = val;

	return spi_write(st->spi, buf, 2);
}

static ssize_t as3935_sensor_sensitivity_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct as3935_state *st = iio_priv(dev_to_iio_dev(dev));
	int val, ret;

	ret = as3935_read(st, AS3935_AFE_GAIN, &val);
	if (ret)
		return ret;
	val = (val & AS3935_AFE_MASK) >> 1;

	return sysfs_emit(buf, "%d\n", val);
}

static ssize_t as3935_sensor_sensitivity_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t len)
{
	struct as3935_state *st = iio_priv(dev_to_iio_dev(dev));
	unsigned long val;
	int ret;

	ret = kstrtoul(buf, 10, &val);
	if (ret)
		return -EINVAL;

	if (val > AS3935_AFE_GAIN_MAX)
		return -EINVAL;

	as3935_write(st, AS3935_AFE_GAIN, val << 1);

	return len;
}

static ssize_t as3935_noise_level_tripped_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct as3935_state *st = iio_priv(dev_to_iio_dev(dev));
	int ret;

	mutex_lock(&st->lock);
	ret = sysfs_emit(buf, "%d\n", !time_after(jiffies, st->noise_tripped + HZ));
	mutex_unlock(&st->lock);

	return ret;
}

static IIO_DEVICE_ATTR(sensor_sensitivity, S_IRUGO | S_IWUSR,
	as3935_sensor_sensitivity_show, as3935_sensor_sensitivity_store, 0);

static IIO_DEVICE_ATTR(noise_level_tripped, S_IRUGO,
	as3935_noise_level_tripped_show, NULL, 0);

static struct attribute *as3935_attributes[] = {
	&iio_dev_attr_sensor_sensitivity.dev_attr.attr,
	&iio_dev_attr_noise_level_tripped.dev_attr.attr,
	NULL,
};

static const struct attribute_group as3935_attribute_group = {
	.attrs = as3935_attributes,
};

static int as3935_read_raw(struct iio_dev *indio_dev,
			   struct iio_chan_spec const *chan,
			   int *val,
			   int *val2,
			   long m)
{
	struct as3935_state *st = iio_priv(indio_dev);
	int ret;


	switch (m) {
	case IIO_CHAN_INFO_PROCESSED:
	case IIO_CHAN_INFO_RAW:
		*val2 = 0;
		ret = as3935_read(st, AS3935_DATA, val);
		if (ret)
			return ret;

		/* storm out of range */
		if (*val == AS3935_DATA_MASK)
			return -EINVAL;

		if (m == IIO_CHAN_INFO_RAW)
			return IIO_VAL_INT;

		if (m == IIO_CHAN_INFO_PROCESSED)
			*val *= 1000;
		break;
	case IIO_CHAN_INFO_SCALE:
		*val = 1000;
		break;
	default:
		return -EINVAL;
	}

	return IIO_VAL_INT;
}

static const struct iio_info as3935_info = {
	.attrs = &as3935_attribute_group,
	.read_raw = &as3935_read_raw,
};

static irqreturn_t as3935_trigger_handler(int irq, void *private)
{
	struct iio_poll_func *pf = private;
	struct iio_dev *indio_dev = pf->indio_dev;
	struct as3935_state *st = iio_priv(indio_dev);
	int val, ret;

	ret = as3935_read(st, AS3935_DATA, &val);
	if (ret)
		goto err_read;

	st->scan.chan = val & AS3935_DATA_MASK;
	iio_push_to_buffers_with_timestamp(indio_dev, &st->scan,
					   iio_get_time_ns(indio_dev));
err_read:
	iio_trigger_notify_done(indio_dev->trig);

	return IRQ_HANDLED;
}

static void as3935_event_work(struct work_struct *work)
{
	struct as3935_state *st;
	int val;
	int ret;

	st = container_of(work, struct as3935_state, work.work);

	ret = as3935_read(st, AS3935_INT, &val);
	if (ret) {
		dev_warn(&st->spi->dev, "read error\n");
		return;
	}

	val &= AS3935_INT_MASK;

	switch (val) {
	case AS3935_EVENT_INT:
		iio_trigger_poll_nested(st->trig);
		break;
	case AS3935_DISTURB_INT:
	case AS3935_NOISE_INT:
		mutex_lock(&st->lock);
		st->noise_tripped = jiffies;
		mutex_unlock(&st->lock);
		dev_warn(&st->spi->dev, "noise level is too high\n");
		break;
	}
}

static irqreturn_t as3935_interrupt_handler(int irq, void *private)
{
	struct iio_dev *indio_dev = private;
	struct as3935_state *st = iio_priv(indio_dev);

	/*
	 * Delay work for >2 milliseconds after an interrupt to allow
	 * estimated distance to recalculated.
	 */

	schedule_delayed_work(&st->work, msecs_to_jiffies(3));

	return IRQ_HANDLED;
}

static void calibrate_as3935(struct as3935_state *st)
{
	as3935_write(st, AS3935_DEFAULTS, 0x96);
	as3935_write(st, AS3935_CALIBRATE, 0x96);
	as3935_write(st, AS3935_TUNE_CAP,
		BIT(5) | (st->tune_cap / TUNE_CAP_DIV));

	mdelay(2);
	as3935_write(st, AS3935_TUNE_CAP, (st->tune_cap / TUNE_CAP_DIV));
	as3935_write(st, AS3935_NFLWDTH, st->nflwdth_reg);
}

static int as3935_suspend(struct device *dev)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct as3935_state *st = iio_priv(indio_dev);
	int val, ret;

	mutex_lock(&st->lock);
	ret = as3935_read(st, AS3935_AFE_GAIN, &val);
	if (ret)
		goto err_suspend;
	val |= AS3935_AFE_PWR_BIT;

	ret = as3935_write(st, AS3935_AFE_GAIN, val);

err_suspend:
	mutex_unlock(&st->lock);

	return ret;
}

static int as3935_resume(struct device *dev)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct as3935_state *st = iio_priv(indio_dev);
	int val, ret;

	mutex_lock(&st->lock);
	ret = as3935_read(st, AS3935_AFE_GAIN, &val);
	if (ret)
		goto err_resume;
	val &= ~AS3935_AFE_PWR_BIT;
	ret = as3935_write(st, AS3935_AFE_GAIN, val);

	calibrate_as3935(st);

err_resume:
	mutex_unlock(&st->lock);

	return ret;
}

static DEFINE_SIMPLE_DEV_PM_OPS(as3935_pm_ops, as3935_suspend, as3935_resume);

static int as3935_probe(struct spi_device *spi)
{
	struct device *dev = &spi->dev;
	struct iio_dev *indio_dev;
	struct iio_trigger *trig;
	struct as3935_state *st;
	int ret;

	/* Be sure lightning event interrupt is specified */
	if (!spi->irq) {
		dev_err(dev, "unable to get event interrupt\n");
		return -EINVAL;
	}

	indio_dev = devm_iio_device_alloc(dev, sizeof(*st));
	if (!indio_dev)
		return -ENOMEM;

	st = iio_priv(indio_dev);
	st->spi = spi;

	spi_set_drvdata(spi, indio_dev);
	mutex_init(&st->lock);

	ret = device_property_read_u32(dev,
			"ams,tuning-capacitor-pf", &st->tune_cap);
	if (ret) {
		st->tune_cap = 0;
		dev_warn(dev, "no tuning-capacitor-pf set, defaulting to %d",
			st->tune_cap);
	}

	if (st->tune_cap > MAX_PF_CAP) {
		dev_err(dev, "wrong tuning-capacitor-pf setting of %d\n",
			st->tune_cap);
		return -EINVAL;
	}

	ret = device_property_read_u32(dev,
			"ams,nflwdth", &st->nflwdth_reg);
	if (!ret && st->nflwdth_reg > AS3935_NFLWDTH_MASK) {
		dev_err(dev, "invalid nflwdth setting of %d\n",
			st->nflwdth_reg);
		return -EINVAL;
	}

	indio_dev->name = spi_get_device_id(spi)->name;
	indio_dev->channels = as3935_channels;
	indio_dev->num_channels = ARRAY_SIZE(as3935_channels);
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->info = &as3935_info;

	trig = devm_iio_trigger_alloc(dev, "%s-dev%d",
				      indio_dev->name,
				      iio_device_id(indio_dev));

	if (!trig)
		return -ENOMEM;

	st->trig = trig;
	st->noise_tripped = jiffies - HZ;
	iio_trigger_set_drvdata(trig, indio_dev);

	ret = devm_iio_trigger_register(dev, trig);
	if (ret) {
		dev_err(dev, "failed to register trigger\n");
		return ret;
	}

	ret = devm_iio_triggered_buffer_setup(dev, indio_dev,
					      iio_pollfunc_store_time,
					      as3935_trigger_handler, NULL);

	if (ret) {
		dev_err(dev, "cannot setup iio trigger\n");
		return ret;
	}

	calibrate_as3935(st);

	ret = devm_delayed_work_autocancel(dev, &st->work, as3935_event_work);
	if (ret)
		return ret;

	ret = devm_request_irq(dev, spi->irq,
				&as3935_interrupt_handler,
				IRQF_TRIGGER_RISING,
				dev_name(dev),
				indio_dev);

	if (ret) {
		dev_err(dev, "unable to request irq\n");
		return ret;
	}

	ret = devm_iio_device_register(dev, indio_dev);
	if (ret < 0) {
		dev_err(dev, "unable to register device\n");
		return ret;
	}
	return 0;
}

static const struct of_device_id as3935_of_match[] = {
	{ .compatible = "ams,as3935", },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, as3935_of_match);

static const struct spi_device_id as3935_id[] = {
	{"as3935", 0},
	{},
};
MODULE_DEVICE_TABLE(spi, as3935_id);

static struct spi_driver as3935_driver = {
	.driver = {
		.name	= "as3935",
		.of_match_table = as3935_of_match,
		.pm	= pm_sleep_ptr(&as3935_pm_ops),
	},
	.probe		= as3935_probe,
	.id_table	= as3935_id,
};
module_spi_driver(as3935_driver);

MODULE_AUTHOR("Matt Ranostay <matt.ranostay@konsulko.com>");
MODULE_DESCRIPTION("AS3935 lightning sensor");
MODULE_LICENSE("GPL");

/*
 * as3935.c - Support for AS3935 Franklin lightning sensor
 *
 * Copyright (C) 2014 Matt Ranostay <mranostay@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/workqueue.h>
#include <linux/mutex.h>
#include <linux/err.h>
#include <linux/irq.h>
#include <linux/gpio.h>
#include <linux/spi/spi.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/trigger.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/iio/buffer.h>
#include <linux/iio/triggered_buffer.h>
#include <linux/of_gpio.h>


#define AS3935_AFE_GAIN		0x00
#define AS3935_AFE_MASK		0x3F
#define AS3935_AFE_GAIN_MAX	0x1F
#define AS3935_AFE_PWR_BIT	BIT(0)

#define AS3935_INT		0x03
#define AS3935_INT_MASK		0x07
#define AS3935_EVENT_INT	BIT(3)
#define AS3935_NOISE_INT	BIT(1)

#define AS3935_DATA		0x07
#define AS3935_DATA_MASK	0x3F

#define AS3935_TUNE_CAP		0x08
#define AS3935_CALIBRATE	0x3D

#define AS3935_WRITE_DATA	BIT(15)
#define AS3935_READ_DATA	BIT(14)
#define AS3935_ADDRESS(x)	((x) << 8)

#define MAX_PF_CAP		120
#define TUNE_CAP_DIV		8

struct as3935_state {
	struct spi_device *spi;
	struct iio_trigger *trig;
	struct mutex lock;
	struct delayed_work work;

	u32 tune_cap;
	u8 buffer[16]; /* 8-bit data + 56-bit padding + 64-bit timestamp */
	u8 buf[2] ____cacheline_aligned;
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

	buf[0] = (AS3935_WRITE_DATA | AS3935_ADDRESS(reg)) >> 8;
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

	return sprintf(buf, "%d\n", val);
}

static ssize_t as3935_sensor_sensitivity_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t len)
{
	struct as3935_state *st = iio_priv(dev_to_iio_dev(dev));
	unsigned long val;
	int ret;

	ret = kstrtoul((const char *) buf, 10, &val);
	if (ret)
		return -EINVAL;

	if (val > AS3935_AFE_GAIN_MAX)
		return -EINVAL;

	as3935_write(st, AS3935_AFE_GAIN, val << 1);

	return len;
}

static IIO_DEVICE_ATTR(sensor_sensitivity, S_IRUGO | S_IWUSR,
	as3935_sensor_sensitivity_show, as3935_sensor_sensitivity_store, 0);


static struct attribute *as3935_attributes[] = {
	&iio_dev_attr_sensor_sensitivity.dev_attr.attr,
	NULL,
};

static struct attribute_group as3935_attribute_group = {
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

		if (m == IIO_CHAN_INFO_RAW)
			return IIO_VAL_INT;

		/* storm out of range */
		if (*val == AS3935_DATA_MASK)
			return -EINVAL;

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
	.driver_module = THIS_MODULE,
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

	st->buffer[0] = val & AS3935_DATA_MASK;
	iio_push_to_buffers_with_timestamp(indio_dev, &st->buffer,
					   pf->timestamp);
err_read:
	iio_trigger_notify_done(indio_dev->trig);

	return IRQ_HANDLED;
}

static const struct iio_trigger_ops iio_interrupt_trigger_ops = {
	.owner = THIS_MODULE,
};

static void as3935_event_work(struct work_struct *work)
{
	struct as3935_state *st;
	int val;

	st = container_of(work, struct as3935_state, work.work);

	as3935_read(st, AS3935_INT, &val);
	val &= AS3935_INT_MASK;

	switch (val) {
	case AS3935_EVENT_INT:
		iio_trigger_poll(st->trig);
		break;
	case AS3935_NOISE_INT:
		dev_warn(&st->spi->dev, "noise level is too high");
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
	mutex_lock(&st->lock);

	/* mask disturber interrupt bit */
	as3935_write(st, AS3935_INT, BIT(5));

	as3935_write(st, AS3935_CALIBRATE, 0x96);
	as3935_write(st, AS3935_TUNE_CAP,
		BIT(5) | (st->tune_cap / TUNE_CAP_DIV));

	mdelay(2);
	as3935_write(st, AS3935_TUNE_CAP, (st->tune_cap / TUNE_CAP_DIV));

	mutex_unlock(&st->lock);
}

#ifdef CONFIG_PM_SLEEP
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

err_resume:
	mutex_unlock(&st->lock);

	return ret;
}

static SIMPLE_DEV_PM_OPS(as3935_pm_ops, as3935_suspend, as3935_resume);
#define AS3935_PM_OPS (&as3935_pm_ops)

#else
#define AS3935_PM_OPS NULL
#endif

static int as3935_probe(struct spi_device *spi)
{
	struct iio_dev *indio_dev;
	struct iio_trigger *trig;
	struct as3935_state *st;
	struct device_node *np = spi->dev.of_node;
	int ret;

	/* Be sure lightning event interrupt is specified */
	if (!spi->irq) {
		dev_err(&spi->dev, "unable to get event interrupt\n");
		return -EINVAL;
	}

	indio_dev = devm_iio_device_alloc(&spi->dev, sizeof(*st));
	if (!indio_dev)
		return -ENOMEM;

	st = iio_priv(indio_dev);
	st->spi = spi;
	st->tune_cap = 0;

	spi_set_drvdata(spi, indio_dev);
	mutex_init(&st->lock);
	INIT_DELAYED_WORK(&st->work, as3935_event_work);

	ret = of_property_read_u32(np,
			"ams,tuning-capacitor-pf", &st->tune_cap);
	if (ret) {
		st->tune_cap = 0;
		dev_warn(&spi->dev,
			"no tuning-capacitor-pf set, defaulting to %d",
			st->tune_cap);
	}

	if (st->tune_cap > MAX_PF_CAP) {
		dev_err(&spi->dev,
			"wrong tuning-capacitor-pf setting of %d\n",
			st->tune_cap);
		return -EINVAL;
	}

	indio_dev->dev.parent = &spi->dev;
	indio_dev->name = spi_get_device_id(spi)->name;
	indio_dev->channels = as3935_channels;
	indio_dev->num_channels = ARRAY_SIZE(as3935_channels);
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->info = &as3935_info;

	trig = devm_iio_trigger_alloc(&spi->dev, "%s-dev%d",
				      indio_dev->name, indio_dev->id);

	if (!trig)
		return -ENOMEM;

	st->trig = trig;
	trig->dev.parent = indio_dev->dev.parent;
	iio_trigger_set_drvdata(trig, indio_dev);
	trig->ops = &iio_interrupt_trigger_ops;

	ret = iio_trigger_register(trig);
	if (ret) {
		dev_err(&spi->dev, "failed to register trigger\n");
		return ret;
	}

	ret = iio_triggered_buffer_setup(indio_dev, iio_pollfunc_store_time,
		&as3935_trigger_handler, NULL);

	if (ret) {
		dev_err(&spi->dev, "cannot setup iio trigger\n");
		goto unregister_trigger;
	}

	calibrate_as3935(st);

	ret = devm_request_irq(&spi->dev, spi->irq,
				&as3935_interrupt_handler,
				IRQF_TRIGGER_RISING,
				dev_name(&spi->dev),
				indio_dev);

	if (ret) {
		dev_err(&spi->dev, "unable to request irq\n");
		goto unregister_buffer;
	}

	ret = iio_device_register(indio_dev);
	if (ret < 0) {
		dev_err(&spi->dev, "unable to register device\n");
		goto unregister_buffer;
	}
	return 0;

unregister_buffer:
	iio_triggered_buffer_cleanup(indio_dev);

unregister_trigger:
	iio_trigger_unregister(st->trig);

	return ret;
}

static int as3935_remove(struct spi_device *spi)
{
	struct iio_dev *indio_dev = spi_get_drvdata(spi);
	struct as3935_state *st = iio_priv(indio_dev);

	iio_device_unregister(indio_dev);
	iio_triggered_buffer_cleanup(indio_dev);
	iio_trigger_unregister(st->trig);

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
		.of_match_table = of_match_ptr(as3935_of_match),
		.pm	= AS3935_PM_OPS,
	},
	.probe		= as3935_probe,
	.remove		= as3935_remove,
	.id_table	= as3935_id,
};
module_spi_driver(as3935_driver);

MODULE_AUTHOR("Matt Ranostay <mranostay@gmail.com>");
MODULE_DESCRIPTION("AS3935 lightning sensor");
MODULE_LICENSE("GPL");
MODULE_ALIAS("spi:as3935");

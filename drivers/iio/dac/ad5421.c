/*
 * AD5421 Digital to analog converters  driver
 *
 * Copyright 2011 Analog Devices Inc.
 *
 * Licensed under the GPL-2.
 */

#include <linux/device.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/spi/spi.h>
#include <linux/slab.h>
#include <linux/sysfs.h>

#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/events.h>
#include <linux/iio/dac/ad5421.h>


#define AD5421_REG_DAC_DATA		0x1
#define AD5421_REG_CTRL			0x2
#define AD5421_REG_OFFSET		0x3
#define AD5421_REG_GAIN			0x4
/* load dac and fault shared the same register number. Writing to it will cause
 * a dac load command, reading from it will return the fault status register */
#define AD5421_REG_LOAD_DAC		0x5
#define AD5421_REG_FAULT		0x5
#define AD5421_REG_FORCE_ALARM_CURRENT	0x6
#define AD5421_REG_RESET		0x7
#define AD5421_REG_START_CONVERSION	0x8
#define AD5421_REG_NOOP			0x9

#define AD5421_CTRL_WATCHDOG_DISABLE	BIT(12)
#define AD5421_CTRL_AUTO_FAULT_READBACK	BIT(11)
#define AD5421_CTRL_MIN_CURRENT		BIT(9)
#define AD5421_CTRL_ADC_SOURCE_TEMP	BIT(8)
#define AD5421_CTRL_ADC_ENABLE		BIT(7)
#define AD5421_CTRL_PWR_DOWN_INT_VREF	BIT(6)

#define AD5421_FAULT_SPI			BIT(15)
#define AD5421_FAULT_PEC			BIT(14)
#define AD5421_FAULT_OVER_CURRENT		BIT(13)
#define AD5421_FAULT_UNDER_CURRENT		BIT(12)
#define AD5421_FAULT_TEMP_OVER_140		BIT(11)
#define AD5421_FAULT_TEMP_OVER_100		BIT(10)
#define AD5421_FAULT_UNDER_VOLTAGE_6V		BIT(9)
#define AD5421_FAULT_UNDER_VOLTAGE_12V		BIT(8)

/* These bits will cause the fault pin to go high */
#define AD5421_FAULT_TRIGGER_IRQ \
	(AD5421_FAULT_SPI | AD5421_FAULT_PEC | AD5421_FAULT_OVER_CURRENT | \
	AD5421_FAULT_UNDER_CURRENT | AD5421_FAULT_TEMP_OVER_140)

/**
 * struct ad5421_state - driver instance specific data
 * @spi:		spi_device
 * @ctrl:		control register cache
 * @current_range:	current range which the device is configured for
 * @data:		spi transfer buffers
 * @fault_mask:		software masking of events
 */
struct ad5421_state {
	struct spi_device		*spi;
	unsigned int			ctrl;
	enum ad5421_current_range	current_range;
	unsigned int			fault_mask;

	/*
	 * DMA (thus cache coherency maintenance) requires the
	 * transfer buffers to live in their own cache lines.
	 */
	union {
		u32 d32;
		u8 d8[4];
	} data[2] ____cacheline_aligned;
};

static const struct iio_event_spec ad5421_current_event[] = {
	{
		.type = IIO_EV_TYPE_THRESH,
		.dir = IIO_EV_DIR_RISING,
		.mask_separate = BIT(IIO_EV_INFO_VALUE) |
			BIT(IIO_EV_INFO_ENABLE),
	}, {
		.type = IIO_EV_TYPE_THRESH,
		.dir = IIO_EV_DIR_FALLING,
		.mask_separate = BIT(IIO_EV_INFO_VALUE) |
			BIT(IIO_EV_INFO_ENABLE),
	},
};

static const struct iio_event_spec ad5421_temp_event[] = {
	{
		.type = IIO_EV_TYPE_THRESH,
		.dir = IIO_EV_DIR_RISING,
		.mask_separate = BIT(IIO_EV_INFO_VALUE) |
			BIT(IIO_EV_INFO_ENABLE),
	},
};

static const struct iio_chan_spec ad5421_channels[] = {
	{
		.type = IIO_CURRENT,
		.indexed = 1,
		.output = 1,
		.channel = 0,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
			BIT(IIO_CHAN_INFO_CALIBSCALE) |
			BIT(IIO_CHAN_INFO_CALIBBIAS),
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE) |
			BIT(IIO_CHAN_INFO_OFFSET),
		.scan_type = IIO_ST('u', 16, 16, 0),
		.event_spec = ad5421_current_event,
		.num_event_specs = ARRAY_SIZE(ad5421_current_event),
	},
	{
		.type = IIO_TEMP,
		.channel = -1,
		.event_spec = ad5421_temp_event,
		.num_event_specs = ARRAY_SIZE(ad5421_temp_event),
	},
};

static int ad5421_write_unlocked(struct iio_dev *indio_dev,
	unsigned int reg, unsigned int val)
{
	struct ad5421_state *st = iio_priv(indio_dev);

	st->data[0].d32 = cpu_to_be32((reg << 16) | val);

	return spi_write(st->spi, &st->data[0].d8[1], 3);
}

static int ad5421_write(struct iio_dev *indio_dev, unsigned int reg,
	unsigned int val)
{
	int ret;

	mutex_lock(&indio_dev->mlock);
	ret = ad5421_write_unlocked(indio_dev, reg, val);
	mutex_unlock(&indio_dev->mlock);

	return ret;
}

static int ad5421_read(struct iio_dev *indio_dev, unsigned int reg)
{
	struct ad5421_state *st = iio_priv(indio_dev);
	int ret;
	struct spi_transfer t[] = {
		{
			.tx_buf = &st->data[0].d8[1],
			.len = 3,
			.cs_change = 1,
		}, {
			.rx_buf = &st->data[1].d8[1],
			.len = 3,
		},
	};

	mutex_lock(&indio_dev->mlock);

	st->data[0].d32 = cpu_to_be32((1 << 23) | (reg << 16));

	ret = spi_sync_transfer(st->spi, t, ARRAY_SIZE(t));
	if (ret >= 0)
		ret = be32_to_cpu(st->data[1].d32) & 0xffff;

	mutex_unlock(&indio_dev->mlock);

	return ret;
}

static int ad5421_update_ctrl(struct iio_dev *indio_dev, unsigned int set,
	unsigned int clr)
{
	struct ad5421_state *st = iio_priv(indio_dev);
	unsigned int ret;

	mutex_lock(&indio_dev->mlock);

	st->ctrl &= ~clr;
	st->ctrl |= set;

	ret = ad5421_write_unlocked(indio_dev, AD5421_REG_CTRL, st->ctrl);

	mutex_unlock(&indio_dev->mlock);

	return ret;
}

static irqreturn_t ad5421_fault_handler(int irq, void *data)
{
	struct iio_dev *indio_dev = data;
	struct ad5421_state *st = iio_priv(indio_dev);
	unsigned int fault;
	unsigned int old_fault = 0;
	unsigned int events;

	fault = ad5421_read(indio_dev, AD5421_REG_FAULT);
	if (!fault)
		return IRQ_NONE;

	/* If we had a fault, this might mean that the DAC has lost its state
	 * and has been reset. Make sure that the control register actually
	 * contains what we expect it to contain. Otherwise the watchdog might
	 * be enabled and we get watchdog timeout faults, which will render the
	 * DAC unusable. */
	ad5421_update_ctrl(indio_dev, 0, 0);


	/* The fault pin stays high as long as a fault condition is present and
	 * it is not possible to mask fault conditions. For certain fault
	 * conditions for example like over-temperature it takes some time
	 * until the fault condition disappears. If we would exit the interrupt
	 * handler immediately after handling the event it would be entered
	 * again instantly. Thus we fall back to polling in case we detect that
	 * a interrupt condition is still present.
	 */
	do {
		/* 0xffff is a invalid value for the register and will only be
		 * read if there has been a communication error */
		if (fault == 0xffff)
			fault = 0;

		/* we are only interested in new events */
		events = (old_fault ^ fault) & fault;
		events &= st->fault_mask;

		if (events & AD5421_FAULT_OVER_CURRENT) {
			iio_push_event(indio_dev,
				IIO_UNMOD_EVENT_CODE(IIO_CURRENT,
					0,
					IIO_EV_TYPE_THRESH,
					IIO_EV_DIR_RISING),
			iio_get_time_ns());
		}

		if (events & AD5421_FAULT_UNDER_CURRENT) {
			iio_push_event(indio_dev,
				IIO_UNMOD_EVENT_CODE(IIO_CURRENT,
					0,
					IIO_EV_TYPE_THRESH,
					IIO_EV_DIR_FALLING),
				iio_get_time_ns());
		}

		if (events & AD5421_FAULT_TEMP_OVER_140) {
			iio_push_event(indio_dev,
				IIO_UNMOD_EVENT_CODE(IIO_TEMP,
					0,
					IIO_EV_TYPE_MAG,
					IIO_EV_DIR_RISING),
				iio_get_time_ns());
		}

		old_fault = fault;
		fault = ad5421_read(indio_dev, AD5421_REG_FAULT);

		/* still active? go to sleep for some time */
		if (fault & AD5421_FAULT_TRIGGER_IRQ)
			msleep(1000);

	} while (fault & AD5421_FAULT_TRIGGER_IRQ);


	return IRQ_HANDLED;
}

static void ad5421_get_current_min_max(struct ad5421_state *st,
	unsigned int *min, unsigned int *max)
{
	/* The current range is configured using external pins, which are
	 * usually hard-wired and not run-time switchable. */
	switch (st->current_range) {
	case AD5421_CURRENT_RANGE_4mA_20mA:
		*min = 4000;
		*max = 20000;
		break;
	case AD5421_CURRENT_RANGE_3mA8_21mA:
		*min = 3800;
		*max = 21000;
		break;
	case AD5421_CURRENT_RANGE_3mA2_24mA:
		*min = 3200;
		*max = 24000;
		break;
	default:
		*min = 0;
		*max = 1;
		break;
	}
}

static inline unsigned int ad5421_get_offset(struct ad5421_state *st)
{
	unsigned int min, max;

	ad5421_get_current_min_max(st, &min, &max);
	return (min * (1 << 16)) / (max - min);
}

static int ad5421_read_raw(struct iio_dev *indio_dev,
	struct iio_chan_spec const *chan, int *val, int *val2, long m)
{
	struct ad5421_state *st = iio_priv(indio_dev);
	unsigned int min, max;
	int ret;

	if (chan->type != IIO_CURRENT)
		return -EINVAL;

	switch (m) {
	case IIO_CHAN_INFO_RAW:
		ret = ad5421_read(indio_dev, AD5421_REG_DAC_DATA);
		if (ret < 0)
			return ret;
		*val = ret;
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		ad5421_get_current_min_max(st, &min, &max);
		*val = max - min;
		*val2 = (1 << 16) * 1000;
		return IIO_VAL_FRACTIONAL;
	case IIO_CHAN_INFO_OFFSET:
		*val = ad5421_get_offset(st);
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_CALIBBIAS:
		ret = ad5421_read(indio_dev, AD5421_REG_OFFSET);
		if (ret < 0)
			return ret;
		*val = ret - 32768;
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_CALIBSCALE:
		ret = ad5421_read(indio_dev, AD5421_REG_GAIN);
		if (ret < 0)
			return ret;
		*val = ret;
		return IIO_VAL_INT;
	}

	return -EINVAL;
}

static int ad5421_write_raw(struct iio_dev *indio_dev,
	struct iio_chan_spec const *chan, int val, int val2, long mask)
{
	const unsigned int max_val = 1 << 16;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		if (val >= max_val || val < 0)
			return -EINVAL;

		return ad5421_write(indio_dev, AD5421_REG_DAC_DATA, val);
	case IIO_CHAN_INFO_CALIBBIAS:
		val += 32768;
		if (val >= max_val || val < 0)
			return -EINVAL;

		return ad5421_write(indio_dev, AD5421_REG_OFFSET, val);
	case IIO_CHAN_INFO_CALIBSCALE:
		if (val >= max_val || val < 0)
			return -EINVAL;

		return ad5421_write(indio_dev, AD5421_REG_GAIN, val);
	default:
		break;
	}

	return -EINVAL;
}

static int ad5421_write_event_config(struct iio_dev *indio_dev,
	const struct iio_chan_spec *chan, enum iio_event_type type,
	enum iio_event_direction dir, int state)
{
	struct ad5421_state *st = iio_priv(indio_dev);
	unsigned int mask;

	switch (chan->type) {
	case IIO_CURRENT:
		if (dir == IIO_EV_DIR_RISING)
			mask = AD5421_FAULT_OVER_CURRENT;
		else
			mask = AD5421_FAULT_UNDER_CURRENT;
		break;
	case IIO_TEMP:
		mask = AD5421_FAULT_TEMP_OVER_140;
		break;
	default:
		return -EINVAL;
	}

	mutex_lock(&indio_dev->mlock);
	if (state)
		st->fault_mask |= mask;
	else
		st->fault_mask &= ~mask;
	mutex_unlock(&indio_dev->mlock);

	return 0;
}

static int ad5421_read_event_config(struct iio_dev *indio_dev,
	const struct iio_chan_spec *chan, enum iio_event_type type,
	enum iio_event_direction dir)
{
	struct ad5421_state *st = iio_priv(indio_dev);
	unsigned int mask;

	switch (chan->type) {
	case IIO_CURRENT:
		if (dir == IIO_EV_DIR_RISING)
			mask = AD5421_FAULT_OVER_CURRENT;
		else
			mask = AD5421_FAULT_UNDER_CURRENT;
		break;
	case IIO_TEMP:
		mask = AD5421_FAULT_TEMP_OVER_140;
		break;
	default:
		return -EINVAL;
	}

	return (bool)(st->fault_mask & mask);
}

static int ad5421_read_event_value(struct iio_dev *indio_dev,
	const struct iio_chan_spec *chan, enum iio_event_type type,
	enum iio_event_direction dir, enum iio_event_info info, int *val,
	int *val2)
{
	int ret;

	switch (chan->type) {
	case IIO_CURRENT:
		ret = ad5421_read(indio_dev, AD5421_REG_DAC_DATA);
		if (ret < 0)
			return ret;
		*val = ret;
		break;
	case IIO_TEMP:
		*val = 140000;
		break;
	default:
		return -EINVAL;
	}

	return IIO_VAL_INT;
}

static const struct iio_info ad5421_info = {
	.read_raw =		ad5421_read_raw,
	.write_raw =		ad5421_write_raw,
	.read_event_config_new = ad5421_read_event_config,
	.write_event_config_new = ad5421_write_event_config,
	.read_event_value_new =	ad5421_read_event_value,
	.driver_module =	THIS_MODULE,
};

static int ad5421_probe(struct spi_device *spi)
{
	struct ad5421_platform_data *pdata = dev_get_platdata(&spi->dev);
	struct iio_dev *indio_dev;
	struct ad5421_state *st;
	int ret;

	indio_dev = devm_iio_device_alloc(&spi->dev, sizeof(*st));
	if (indio_dev == NULL) {
		dev_err(&spi->dev, "Failed to allocate iio device\n");
		return  -ENOMEM;
	}

	st = iio_priv(indio_dev);
	spi_set_drvdata(spi, indio_dev);

	st->spi = spi;

	indio_dev->dev.parent = &spi->dev;
	indio_dev->name = "ad5421";
	indio_dev->info = &ad5421_info;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = ad5421_channels;
	indio_dev->num_channels = ARRAY_SIZE(ad5421_channels);

	st->ctrl = AD5421_CTRL_WATCHDOG_DISABLE |
			AD5421_CTRL_AUTO_FAULT_READBACK;

	if (pdata) {
		st->current_range = pdata->current_range;
		if (pdata->external_vref)
			st->ctrl |= AD5421_CTRL_PWR_DOWN_INT_VREF;
	} else {
		st->current_range = AD5421_CURRENT_RANGE_4mA_20mA;
	}

	/* write initial ctrl register value */
	ad5421_update_ctrl(indio_dev, 0, 0);

	if (spi->irq) {
		ret = devm_request_threaded_irq(&spi->dev, spi->irq,
					   NULL,
					   ad5421_fault_handler,
					   IRQF_TRIGGER_HIGH | IRQF_ONESHOT,
					   "ad5421 fault",
					   indio_dev);
		if (ret)
			return ret;
	}

	return iio_device_register(indio_dev);
}

static int ad5421_remove(struct spi_device *spi)
{
	struct iio_dev *indio_dev = spi_get_drvdata(spi);

	iio_device_unregister(indio_dev);

	return 0;
}

static struct spi_driver ad5421_driver = {
	.driver = {
		   .name = "ad5421",
		   .owner = THIS_MODULE,
	},
	.probe = ad5421_probe,
	.remove = ad5421_remove,
};
module_spi_driver(ad5421_driver);

MODULE_AUTHOR("Lars-Peter Clausen <lars@metafoo.de>");
MODULE_DESCRIPTION("Analog Devices AD5421 DAC");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("spi:ad5421");

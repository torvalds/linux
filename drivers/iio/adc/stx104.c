/*
 * IIO driver for the Apex Embedded Systems STX104
 * Copyright (C) 2016 William Breathitt Gray
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */
#include <linux/bitops.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/gpio/driver.h>
#include <linux/iio/iio.h>
#include <linux/iio/types.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/isa.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/spinlock.h>

#define STX104_OUT_CHAN(chan) {				\
	.type = IIO_VOLTAGE,				\
	.channel = chan,				\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),	\
	.indexed = 1,					\
	.output = 1					\
}
#define STX104_IN_CHAN(chan, diff) {					\
	.type = IIO_VOLTAGE,						\
	.channel = chan,						\
	.channel2 = chan,						\
	.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_HARDWAREGAIN) |	\
		BIT(IIO_CHAN_INFO_OFFSET) | BIT(IIO_CHAN_INFO_SCALE),	\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),			\
	.indexed = 1,							\
	.differential = diff						\
}

#define STX104_NUM_OUT_CHAN 2

#define STX104_EXTENT 16

static unsigned int base[max_num_isa_dev(STX104_EXTENT)];
static unsigned int num_stx104;
module_param_array(base, uint, &num_stx104, 0);
MODULE_PARM_DESC(base, "Apex Embedded Systems STX104 base addresses");

/**
 * struct stx104_iio - IIO device private data structure
 * @chan_out_states:	channels' output states
 * @base:		base port address of the IIO device
 */
struct stx104_iio {
	unsigned int chan_out_states[STX104_NUM_OUT_CHAN];
	unsigned int base;
};

/**
 * struct stx104_gpio - GPIO device private data structure
 * @chip:	instance of the gpio_chip
 * @lock:	synchronization lock to prevent I/O race conditions
 * @base:	base port address of the GPIO device
 * @out_state:	output bits state
 */
struct stx104_gpio {
	struct gpio_chip chip;
	spinlock_t lock;
	unsigned int base;
	unsigned int out_state;
};

static int stx104_read_raw(struct iio_dev *indio_dev,
	struct iio_chan_spec const *chan, int *val, int *val2, long mask)
{
	struct stx104_iio *const priv = iio_priv(indio_dev);
	unsigned int adc_config;
	int adbu;
	int gain;

	switch (mask) {
	case IIO_CHAN_INFO_HARDWAREGAIN:
		/* get gain configuration */
		adc_config = inb(priv->base + 11);
		gain = adc_config & 0x3;

		*val = 1 << gain;
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_RAW:
		if (chan->output) {
			*val = priv->chan_out_states[chan->channel];
			return IIO_VAL_INT;
		}

		/* select ADC channel */
		outb(chan->channel | (chan->channel << 4), priv->base + 2);

		/* trigger ADC sample capture and wait for completion */
		outb(0, priv->base);
		while (inb(priv->base + 8) & BIT(7));

		*val = inw(priv->base);
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_OFFSET:
		/* get ADC bipolar/unipolar configuration */
		adc_config = inb(priv->base + 11);
		adbu = !(adc_config & BIT(2));

		*val = -32768 * adbu;
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		/* get ADC bipolar/unipolar and gain configuration */
		adc_config = inb(priv->base + 11);
		adbu = !(adc_config & BIT(2));
		gain = adc_config & 0x3;

		*val = 5;
		*val2 = 15 - adbu + gain;
		return IIO_VAL_FRACTIONAL_LOG2;
	}

	return -EINVAL;
}

static int stx104_write_raw(struct iio_dev *indio_dev,
	struct iio_chan_spec const *chan, int val, int val2, long mask)
{
	struct stx104_iio *const priv = iio_priv(indio_dev);

	switch (mask) {
	case IIO_CHAN_INFO_HARDWAREGAIN:
		/* Only four gain states (x1, x2, x4, x8) */
		switch (val) {
		case 1:
			outb(0, priv->base + 11);
			break;
		case 2:
			outb(1, priv->base + 11);
			break;
		case 4:
			outb(2, priv->base + 11);
			break;
		case 8:
			outb(3, priv->base + 11);
			break;
		default:
			return -EINVAL;
		}

		return 0;
	case IIO_CHAN_INFO_RAW:
		if (chan->output) {
			/* DAC can only accept up to a 16-bit value */
			if ((unsigned int)val > 65535)
				return -EINVAL;

			priv->chan_out_states[chan->channel] = val;
			outw(val, priv->base + 4 + 2 * chan->channel);

			return 0;
		}
		return -EINVAL;
	}

	return -EINVAL;
}

static const struct iio_info stx104_info = {
	.driver_module = THIS_MODULE,
	.read_raw = stx104_read_raw,
	.write_raw = stx104_write_raw
};

/* single-ended input channels configuration */
static const struct iio_chan_spec stx104_channels_sing[] = {
	STX104_OUT_CHAN(0), STX104_OUT_CHAN(1),
	STX104_IN_CHAN(0, 0), STX104_IN_CHAN(1, 0), STX104_IN_CHAN(2, 0),
	STX104_IN_CHAN(3, 0), STX104_IN_CHAN(4, 0), STX104_IN_CHAN(5, 0),
	STX104_IN_CHAN(6, 0), STX104_IN_CHAN(7, 0), STX104_IN_CHAN(8, 0),
	STX104_IN_CHAN(9, 0), STX104_IN_CHAN(10, 0), STX104_IN_CHAN(11, 0),
	STX104_IN_CHAN(12, 0), STX104_IN_CHAN(13, 0), STX104_IN_CHAN(14, 0),
	STX104_IN_CHAN(15, 0)
};
/* differential input channels configuration */
static const struct iio_chan_spec stx104_channels_diff[] = {
	STX104_OUT_CHAN(0), STX104_OUT_CHAN(1),
	STX104_IN_CHAN(0, 1), STX104_IN_CHAN(1, 1), STX104_IN_CHAN(2, 1),
	STX104_IN_CHAN(3, 1), STX104_IN_CHAN(4, 1), STX104_IN_CHAN(5, 1),
	STX104_IN_CHAN(6, 1), STX104_IN_CHAN(7, 1)
};

static int stx104_gpio_get_direction(struct gpio_chip *chip,
	unsigned int offset)
{
	/* GPIO 0-3 are input only, while the rest are output only */
	if (offset < 4)
		return 1;

	return 0;
}

static int stx104_gpio_direction_input(struct gpio_chip *chip,
	unsigned int offset)
{
	if (offset >= 4)
		return -EINVAL;

	return 0;
}

static int stx104_gpio_direction_output(struct gpio_chip *chip,
	unsigned int offset, int value)
{
	if (offset < 4)
		return -EINVAL;

	chip->set(chip, offset, value);
	return 0;
}

static int stx104_gpio_get(struct gpio_chip *chip, unsigned int offset)
{
	struct stx104_gpio *const stx104gpio = gpiochip_get_data(chip);

	if (offset >= 4)
		return -EINVAL;

	return !!(inb(stx104gpio->base) & BIT(offset));
}

static void stx104_gpio_set(struct gpio_chip *chip, unsigned int offset,
	int value)
{
	struct stx104_gpio *const stx104gpio = gpiochip_get_data(chip);
	const unsigned int mask = BIT(offset) >> 4;
	unsigned long flags;

	if (offset < 4)
		return;

	spin_lock_irqsave(&stx104gpio->lock, flags);

	if (value)
		stx104gpio->out_state |= mask;
	else
		stx104gpio->out_state &= ~mask;

	outb(stx104gpio->out_state, stx104gpio->base);

	spin_unlock_irqrestore(&stx104gpio->lock, flags);
}

#define STX104_NGPIO 8
static const char *stx104_names[STX104_NGPIO] = {
	"DIN0", "DIN1", "DIN2", "DIN3", "DOUT0", "DOUT1", "DOUT2", "DOUT3"
};

static void stx104_gpio_set_multiple(struct gpio_chip *chip,
	unsigned long *mask, unsigned long *bits)
{
	struct stx104_gpio *const stx104gpio = gpiochip_get_data(chip);
	unsigned long flags;

	/* verify masked GPIO are output */
	if (!(*mask & 0xF0))
		return;

	*mask >>= 4;
	*bits >>= 4;

	spin_lock_irqsave(&stx104gpio->lock, flags);

	stx104gpio->out_state &= ~*mask;
	stx104gpio->out_state |= *mask & *bits;
	outb(stx104gpio->out_state, stx104gpio->base);

	spin_unlock_irqrestore(&stx104gpio->lock, flags);
}

static int stx104_probe(struct device *dev, unsigned int id)
{
	struct iio_dev *indio_dev;
	struct stx104_iio *priv;
	struct stx104_gpio *stx104gpio;
	int err;

	indio_dev = devm_iio_device_alloc(dev, sizeof(*priv));
	if (!indio_dev)
		return -ENOMEM;

	stx104gpio = devm_kzalloc(dev, sizeof(*stx104gpio), GFP_KERNEL);
	if (!stx104gpio)
		return -ENOMEM;

	if (!devm_request_region(dev, base[id], STX104_EXTENT,
		dev_name(dev))) {
		dev_err(dev, "Unable to lock port addresses (0x%X-0x%X)\n",
			base[id], base[id] + STX104_EXTENT);
		return -EBUSY;
	}

	indio_dev->info = &stx104_info;
	indio_dev->modes = INDIO_DIRECT_MODE;

	/* determine if differential inputs */
	if (inb(base[id] + 8) & BIT(5)) {
		indio_dev->num_channels = ARRAY_SIZE(stx104_channels_diff);
		indio_dev->channels = stx104_channels_diff;
	} else {
		indio_dev->num_channels = ARRAY_SIZE(stx104_channels_sing);
		indio_dev->channels = stx104_channels_sing;
	}

	indio_dev->name = dev_name(dev);
	indio_dev->dev.parent = dev;

	priv = iio_priv(indio_dev);
	priv->base = base[id];

	/* configure device for software trigger operation */
	outb(0, base[id] + 9);

	/* initialize gain setting to x1 */
	outb(0, base[id] + 11);

	/* initialize DAC output to 0V */
	outw(0, base[id] + 4);
	outw(0, base[id] + 6);

	stx104gpio->chip.label = dev_name(dev);
	stx104gpio->chip.parent = dev;
	stx104gpio->chip.owner = THIS_MODULE;
	stx104gpio->chip.base = -1;
	stx104gpio->chip.ngpio = STX104_NGPIO;
	stx104gpio->chip.names = stx104_names;
	stx104gpio->chip.get_direction = stx104_gpio_get_direction;
	stx104gpio->chip.direction_input = stx104_gpio_direction_input;
	stx104gpio->chip.direction_output = stx104_gpio_direction_output;
	stx104gpio->chip.get = stx104_gpio_get;
	stx104gpio->chip.set = stx104_gpio_set;
	stx104gpio->chip.set_multiple = stx104_gpio_set_multiple;
	stx104gpio->base = base[id] + 3;
	stx104gpio->out_state = 0x0;

	spin_lock_init(&stx104gpio->lock);

	err = devm_gpiochip_add_data(dev, &stx104gpio->chip, stx104gpio);
	if (err) {
		dev_err(dev, "GPIO registering failed (%d)\n", err);
		return err;
	}

	return devm_iio_device_register(dev, indio_dev);
}

static struct isa_driver stx104_driver = {
	.probe = stx104_probe,
	.driver = {
		.name = "stx104"
	},
};

module_isa_driver(stx104_driver, num_stx104);

MODULE_AUTHOR("William Breathitt Gray <vilhelm.gray@gmail.com>");
MODULE_DESCRIPTION("Apex Embedded Systems STX104 IIO driver");
MODULE_LICENSE("GPL v2");

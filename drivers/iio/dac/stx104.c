/*
 * DAC driver for the Apex Embedded Systems STX104
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
#include <linux/iio/iio.h>
#include <linux/iio/types.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/isa.h>
#include <linux/module.h>
#include <linux/moduleparam.h>

#define STX104_NUM_CHAN 2

#define STX104_CHAN(chan) {				\
	.type = IIO_VOLTAGE,				\
	.channel = chan,				\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),	\
	.indexed = 1,					\
	.output = 1					\
}

#define STX104_EXTENT 16
/**
 * The highest base address possible for an ISA device is 0x3FF; this results in
 * 1024 possible base addresses. Dividing the number of possible base addresses
 * by the address extent taken by each device results in the maximum number of
 * devices on a system.
 */
#define MAX_NUM_STX104 (1024 / STX104_EXTENT)

static unsigned base[MAX_NUM_STX104];
static unsigned num_stx104;
module_param_array(base, uint, &num_stx104, 0);
MODULE_PARM_DESC(base, "Apex Embedded Systems STX104 base addresses");

/**
 * struct stx104_iio - IIO device private data structure
 * @chan_out_states:	channels' output states
 * @base:		base port address of the IIO device
 */
struct stx104_iio {
	unsigned chan_out_states[STX104_NUM_CHAN];
	unsigned base;
};

static int stx104_read_raw(struct iio_dev *indio_dev,
	struct iio_chan_spec const *chan, int *val, int *val2, long mask)
{
	struct stx104_iio *const priv = iio_priv(indio_dev);

	if (mask != IIO_CHAN_INFO_RAW)
		return -EINVAL;

	*val = priv->chan_out_states[chan->channel];

	return IIO_VAL_INT;
}

static int stx104_write_raw(struct iio_dev *indio_dev,
	struct iio_chan_spec const *chan, int val, int val2, long mask)
{
	struct stx104_iio *const priv = iio_priv(indio_dev);
	const unsigned chan_addr_offset = 2 * chan->channel;

	if (mask != IIO_CHAN_INFO_RAW)
		return -EINVAL;

	priv->chan_out_states[chan->channel] = val;
	outw(val, priv->base + 4 + chan_addr_offset);

	return 0;
}

static const struct iio_info stx104_info = {
	.driver_module = THIS_MODULE,
	.read_raw = stx104_read_raw,
	.write_raw = stx104_write_raw
};

static const struct iio_chan_spec stx104_channels[STX104_NUM_CHAN] = {
	STX104_CHAN(0),
	STX104_CHAN(1)
};

static int stx104_probe(struct device *dev, unsigned int id)
{
	struct iio_dev *indio_dev;
	struct stx104_iio *priv;

	indio_dev = devm_iio_device_alloc(dev, sizeof(*priv));
	if (!indio_dev)
		return -ENOMEM;

	if (!devm_request_region(dev, base[id], STX104_EXTENT,
		dev_name(dev))) {
		dev_err(dev, "Unable to lock port addresses (0x%X-0x%X)\n",
			base[id], base[id] + STX104_EXTENT);
		return -EBUSY;
	}

	indio_dev->info = &stx104_info;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = stx104_channels;
	indio_dev->num_channels = STX104_NUM_CHAN;
	indio_dev->name = dev_name(dev);

	priv = iio_priv(indio_dev);
	priv->base = base[id];

	/* initialize DAC output to 0V */
	outw(0, base[id] + 4);
	outw(0, base[id] + 6);

	return devm_iio_device_register(dev, indio_dev);
}

static struct isa_driver stx104_driver = {
	.probe = stx104_probe,
	.driver = {
		.name = "stx104"
	}
};

static void __exit stx104_exit(void)
{
	isa_unregister_driver(&stx104_driver);
}

static int __init stx104_init(void)
{
	return isa_register_driver(&stx104_driver, num_stx104);
}

module_init(stx104_init);
module_exit(stx104_exit);

MODULE_AUTHOR("William Breathitt Gray <vilhelm.gray@gmail.com>");
MODULE_DESCRIPTION("Apex Embedded Systems STX104 DAC driver");
MODULE_LICENSE("GPL v2");

// SPDX-License-Identifier: GPL-2.0-only
/*
 * IIO driver for the Measurement Computing CIO-DAC
 * Copyright (C) 2016 William Breathitt Gray
 *
 * This driver supports the following Measurement Computing devices: CIO-DAC16,
 * CIO-DAC06, and PC104-DAC06.
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

#define CIO_DAC_NUM_CHAN 16

#define CIO_DAC_CHAN(chan) {				\
	.type = IIO_VOLTAGE,				\
	.channel = chan,				\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),	\
	.indexed = 1,					\
	.output = 1					\
}

#define CIO_DAC_EXTENT 32

static unsigned int base[max_num_isa_dev(CIO_DAC_EXTENT)];
static unsigned int num_cio_dac;
module_param_hw_array(base, uint, ioport, &num_cio_dac, 0);
MODULE_PARM_DESC(base, "Measurement Computing CIO-DAC base addresses");

/**
 * struct cio_dac_iio - IIO device private data structure
 * @chan_out_states:	channels' output states
 * @base:		base port address of the IIO device
 */
struct cio_dac_iio {
	int chan_out_states[CIO_DAC_NUM_CHAN];
	unsigned int base;
};

static int cio_dac_read_raw(struct iio_dev *indio_dev,
	struct iio_chan_spec const *chan, int *val, int *val2, long mask)
{
	struct cio_dac_iio *const priv = iio_priv(indio_dev);

	if (mask != IIO_CHAN_INFO_RAW)
		return -EINVAL;

	*val = priv->chan_out_states[chan->channel];

	return IIO_VAL_INT;
}

static int cio_dac_write_raw(struct iio_dev *indio_dev,
	struct iio_chan_spec const *chan, int val, int val2, long mask)
{
	struct cio_dac_iio *const priv = iio_priv(indio_dev);
	const unsigned int chan_addr_offset = 2 * chan->channel;

	if (mask != IIO_CHAN_INFO_RAW)
		return -EINVAL;

	/* DAC can only accept up to a 12-bit value */
	if ((unsigned int)val > 4095)
		return -EINVAL;

	priv->chan_out_states[chan->channel] = val;
	outw(val, priv->base + chan_addr_offset);

	return 0;
}

static const struct iio_info cio_dac_info = {
	.read_raw = cio_dac_read_raw,
	.write_raw = cio_dac_write_raw
};

static const struct iio_chan_spec cio_dac_channels[CIO_DAC_NUM_CHAN] = {
	CIO_DAC_CHAN(0), CIO_DAC_CHAN(1), CIO_DAC_CHAN(2), CIO_DAC_CHAN(3),
	CIO_DAC_CHAN(4), CIO_DAC_CHAN(5), CIO_DAC_CHAN(6), CIO_DAC_CHAN(7),
	CIO_DAC_CHAN(8), CIO_DAC_CHAN(9), CIO_DAC_CHAN(10), CIO_DAC_CHAN(11),
	CIO_DAC_CHAN(12), CIO_DAC_CHAN(13), CIO_DAC_CHAN(14), CIO_DAC_CHAN(15)
};

static int cio_dac_probe(struct device *dev, unsigned int id)
{
	struct iio_dev *indio_dev;
	struct cio_dac_iio *priv;
	unsigned int i;

	indio_dev = devm_iio_device_alloc(dev, sizeof(*priv));
	if (!indio_dev)
		return -ENOMEM;

	if (!devm_request_region(dev, base[id], CIO_DAC_EXTENT,
		dev_name(dev))) {
		dev_err(dev, "Unable to request port addresses (0x%X-0x%X)\n",
			base[id], base[id] + CIO_DAC_EXTENT);
		return -EBUSY;
	}

	indio_dev->info = &cio_dac_info;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = cio_dac_channels;
	indio_dev->num_channels = CIO_DAC_NUM_CHAN;
	indio_dev->name = dev_name(dev);

	priv = iio_priv(indio_dev);
	priv->base = base[id];

	/* initialize DAC outputs to 0V */
	for (i = 0; i < 32; i += 2)
		outw(0, base[id] + i);

	return devm_iio_device_register(dev, indio_dev);
}

static struct isa_driver cio_dac_driver = {
	.probe = cio_dac_probe,
	.driver = {
		.name = "cio-dac"
	}
};

module_isa_driver(cio_dac_driver, num_cio_dac);

MODULE_AUTHOR("William Breathitt Gray <vilhelm.gray@gmail.com>");
MODULE_DESCRIPTION("Measurement Computing CIO-DAC IIO driver");
MODULE_LICENSE("GPL v2");

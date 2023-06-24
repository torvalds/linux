// SPDX-License-Identifier: GPL-2.0-only
/*
 * AD7904/AD7914/AD7923/AD7924/AD7908/AD7918/AD7928 SPI ADC driver
 *
 * Copyright 2011 Analog Devices Inc (from AD7923 Driver)
 * Copyright 2012 CS Systemes d'Information
 */

#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/property.h>
#include <linux/slab.h>
#include <linux/sysfs.h>
#include <linux/spi/spi.h>
#include <linux/regulator/consumer.h>
#include <linux/err.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/interrupt.h>

#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/buffer.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/iio/triggered_buffer.h>

#define AD7923_WRITE_CR		BIT(11)		/* write control register */
#define AD7923_RANGE		BIT(1)		/* range to REFin */
#define AD7923_CODING		BIT(0)		/* coding is straight binary */
#define AD7923_PM_MODE_AS	(1)		/* auto shutdown */
#define AD7923_PM_MODE_FS	(2)		/* full shutdown */
#define AD7923_PM_MODE_OPS	(3)		/* normal operation */
#define AD7923_SEQUENCE_OFF	(0)		/* no sequence fonction */
#define AD7923_SEQUENCE_PROTECT	(2)		/* no interrupt write cycle */
#define AD7923_SEQUENCE_ON	(3)		/* continuous sequence */


#define AD7923_PM_MODE_WRITE(mode)	((mode) << 4)	 /* write mode */
#define AD7923_CHANNEL_WRITE(channel)	((channel) << 6) /* write channel */
#define AD7923_SEQUENCE_WRITE(sequence)	((((sequence) & 1) << 3) \
					+ (((sequence) & 2) << 9))
						/* write sequence fonction */
/* left shift for CR : bit 11 transmit in first */
#define AD7923_SHIFT_REGISTER	4

/* val = value, dec = left shift, bits = number of bits of the mask */
#define EXTRACT(val, dec, bits)		(((val) >> (dec)) & ((1 << (bits)) - 1))

struct ad7923_state {
	struct spi_device		*spi;
	struct spi_transfer		ring_xfer[5];
	struct spi_transfer		scan_single_xfer[2];
	struct spi_message		ring_msg;
	struct spi_message		scan_single_msg;

	struct regulator		*reg;

	unsigned int			settings;

	/*
	 * DMA (thus cache coherency maintenance) may require the
	 * transfer buffers to live in their own cache lines.
	 * Ensure rx_buf can be directly used in iio_push_to_buffers_with_timetamp
	 * Length = 8 channels + 4 extra for 8 byte timestamp
	 */
	__be16				rx_buf[12] __aligned(IIO_DMA_MINALIGN);
	__be16				tx_buf[4];
};

struct ad7923_chip_info {
	const struct iio_chan_spec *channels;
	unsigned int num_channels;
};

enum ad7923_id {
	AD7904,
	AD7914,
	AD7924,
	AD7908,
	AD7918,
	AD7928
};

#define AD7923_V_CHAN(index, bits)					\
	{								\
		.type = IIO_VOLTAGE,					\
		.indexed = 1,						\
		.channel = index,					\
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),		\
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE),	\
		.address = index,					\
		.scan_index = index,					\
		.scan_type = {						\
			.sign = 'u',					\
			.realbits = (bits),				\
			.storagebits = 16,				\
			.shift = 12 - (bits),				\
			.endianness = IIO_BE,				\
		},							\
	}

#define DECLARE_AD7923_CHANNELS(name, bits) \
const struct iio_chan_spec name ## _channels[] = { \
	AD7923_V_CHAN(0, bits), \
	AD7923_V_CHAN(1, bits), \
	AD7923_V_CHAN(2, bits), \
	AD7923_V_CHAN(3, bits), \
	IIO_CHAN_SOFT_TIMESTAMP(4), \
}

#define DECLARE_AD7908_CHANNELS(name, bits) \
const struct iio_chan_spec name ## _channels[] = { \
	AD7923_V_CHAN(0, bits), \
	AD7923_V_CHAN(1, bits), \
	AD7923_V_CHAN(2, bits), \
	AD7923_V_CHAN(3, bits), \
	AD7923_V_CHAN(4, bits), \
	AD7923_V_CHAN(5, bits), \
	AD7923_V_CHAN(6, bits), \
	AD7923_V_CHAN(7, bits), \
	IIO_CHAN_SOFT_TIMESTAMP(8), \
}

static DECLARE_AD7923_CHANNELS(ad7904, 8);
static DECLARE_AD7923_CHANNELS(ad7914, 10);
static DECLARE_AD7923_CHANNELS(ad7924, 12);
static DECLARE_AD7908_CHANNELS(ad7908, 8);
static DECLARE_AD7908_CHANNELS(ad7918, 10);
static DECLARE_AD7908_CHANNELS(ad7928, 12);

static const struct ad7923_chip_info ad7923_chip_info[] = {
	[AD7904] = {
		.channels = ad7904_channels,
		.num_channels = ARRAY_SIZE(ad7904_channels),
	},
	[AD7914] = {
		.channels = ad7914_channels,
		.num_channels = ARRAY_SIZE(ad7914_channels),
	},
	[AD7924] = {
		.channels = ad7924_channels,
		.num_channels = ARRAY_SIZE(ad7924_channels),
	},
	[AD7908] = {
		.channels = ad7908_channels,
		.num_channels = ARRAY_SIZE(ad7908_channels),
	},
	[AD7918] = {
		.channels = ad7918_channels,
		.num_channels = ARRAY_SIZE(ad7918_channels),
	},
	[AD7928] = {
		.channels = ad7928_channels,
		.num_channels = ARRAY_SIZE(ad7928_channels),
	},
};

/*
 * ad7923_update_scan_mode() setup the spi transfer buffer for the new scan mask
 */
static int ad7923_update_scan_mode(struct iio_dev *indio_dev,
				   const unsigned long *active_scan_mask)
{
	struct ad7923_state *st = iio_priv(indio_dev);
	int i, cmd, len;

	len = 0;
	/*
	 * For this driver the last channel is always the software timestamp so
	 * skip that one.
	 */
	for_each_set_bit(i, active_scan_mask, indio_dev->num_channels - 1) {
		cmd = AD7923_WRITE_CR | AD7923_CHANNEL_WRITE(i) |
			AD7923_SEQUENCE_WRITE(AD7923_SEQUENCE_OFF) |
			st->settings;
		cmd <<= AD7923_SHIFT_REGISTER;
		st->tx_buf[len++] = cpu_to_be16(cmd);
	}
	/* build spi ring message */
	st->ring_xfer[0].tx_buf = &st->tx_buf[0];
	st->ring_xfer[0].len = len;
	st->ring_xfer[0].cs_change = 1;

	spi_message_init(&st->ring_msg);
	spi_message_add_tail(&st->ring_xfer[0], &st->ring_msg);

	for (i = 0; i < len; i++) {
		st->ring_xfer[i + 1].rx_buf = &st->rx_buf[i];
		st->ring_xfer[i + 1].len = 2;
		st->ring_xfer[i + 1].cs_change = 1;
		spi_message_add_tail(&st->ring_xfer[i + 1], &st->ring_msg);
	}
	/* make sure last transfer cs_change is not set */
	st->ring_xfer[i + 1].cs_change = 0;

	return 0;
}

static irqreturn_t ad7923_trigger_handler(int irq, void *p)
{
	struct iio_poll_func *pf = p;
	struct iio_dev *indio_dev = pf->indio_dev;
	struct ad7923_state *st = iio_priv(indio_dev);
	int b_sent;

	b_sent = spi_sync(st->spi, &st->ring_msg);
	if (b_sent)
		goto done;

	iio_push_to_buffers_with_timestamp(indio_dev, st->rx_buf,
					   iio_get_time_ns(indio_dev));

done:
	iio_trigger_notify_done(indio_dev->trig);

	return IRQ_HANDLED;
}

static int ad7923_scan_direct(struct ad7923_state *st, unsigned int ch)
{
	int ret, cmd;

	cmd = AD7923_WRITE_CR | AD7923_CHANNEL_WRITE(ch) |
		AD7923_SEQUENCE_WRITE(AD7923_SEQUENCE_OFF) |
		st->settings;
	cmd <<= AD7923_SHIFT_REGISTER;
	st->tx_buf[0] = cpu_to_be16(cmd);

	ret = spi_sync(st->spi, &st->scan_single_msg);
	if (ret)
		return ret;

	return be16_to_cpu(st->rx_buf[0]);
}

static int ad7923_get_range(struct ad7923_state *st)
{
	int vref;

	vref = regulator_get_voltage(st->reg);
	if (vref < 0)
		return vref;

	vref /= 1000;

	if (!(st->settings & AD7923_RANGE))
		vref *= 2;

	return vref;
}

static int ad7923_read_raw(struct iio_dev *indio_dev,
			   struct iio_chan_spec const *chan,
			   int *val,
			   int *val2,
			   long m)
{
	int ret;
	struct ad7923_state *st = iio_priv(indio_dev);

	switch (m) {
	case IIO_CHAN_INFO_RAW:
		ret = iio_device_claim_direct_mode(indio_dev);
		if (ret)
			return ret;
		ret = ad7923_scan_direct(st, chan->address);
		iio_device_release_direct_mode(indio_dev);

		if (ret < 0)
			return ret;

		if (chan->address == EXTRACT(ret, 12, 4))
			*val = EXTRACT(ret, chan->scan_type.shift,
				       chan->scan_type.realbits);
		else
			return -EIO;

		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		ret = ad7923_get_range(st);
		if (ret < 0)
			return ret;
		*val = ret;
		*val2 = chan->scan_type.realbits;
		return IIO_VAL_FRACTIONAL_LOG2;
	}
	return -EINVAL;
}

static const struct iio_info ad7923_info = {
	.read_raw = &ad7923_read_raw,
	.update_scan_mode = ad7923_update_scan_mode,
};

static void ad7923_regulator_disable(void *data)
{
	struct ad7923_state *st = data;

	regulator_disable(st->reg);
}

static int ad7923_probe(struct spi_device *spi)
{
	u32 ad7923_range = AD7923_RANGE;
	struct ad7923_state *st;
	struct iio_dev *indio_dev;
	const struct ad7923_chip_info *info;
	int ret;

	indio_dev = devm_iio_device_alloc(&spi->dev, sizeof(*st));
	if (!indio_dev)
		return -ENOMEM;

	st = iio_priv(indio_dev);

	if (device_property_read_bool(&spi->dev, "adi,range-double"))
		ad7923_range = 0;

	st->spi = spi;
	st->settings = AD7923_CODING | ad7923_range |
			AD7923_PM_MODE_WRITE(AD7923_PM_MODE_OPS);

	info = &ad7923_chip_info[spi_get_device_id(spi)->driver_data];

	indio_dev->name = spi_get_device_id(spi)->name;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = info->channels;
	indio_dev->num_channels = info->num_channels;
	indio_dev->info = &ad7923_info;

	/* Setup default message */

	st->scan_single_xfer[0].tx_buf = &st->tx_buf[0];
	st->scan_single_xfer[0].len = 2;
	st->scan_single_xfer[0].cs_change = 1;
	st->scan_single_xfer[1].rx_buf = &st->rx_buf[0];
	st->scan_single_xfer[1].len = 2;

	spi_message_init(&st->scan_single_msg);
	spi_message_add_tail(&st->scan_single_xfer[0], &st->scan_single_msg);
	spi_message_add_tail(&st->scan_single_xfer[1], &st->scan_single_msg);

	st->reg = devm_regulator_get(&spi->dev, "refin");
	if (IS_ERR(st->reg))
		return PTR_ERR(st->reg);

	ret = regulator_enable(st->reg);
	if (ret)
		return ret;

	ret = devm_add_action_or_reset(&spi->dev, ad7923_regulator_disable, st);
	if (ret)
		return ret;

	ret = devm_iio_triggered_buffer_setup(&spi->dev, indio_dev, NULL,
					      &ad7923_trigger_handler, NULL);
	if (ret)
		return ret;

	return devm_iio_device_register(&spi->dev, indio_dev);
}

static const struct spi_device_id ad7923_id[] = {
	{"ad7904", AD7904},
	{"ad7914", AD7914},
	{"ad7923", AD7924},
	{"ad7924", AD7924},
	{"ad7908", AD7908},
	{"ad7918", AD7918},
	{"ad7928", AD7928},
	{}
};
MODULE_DEVICE_TABLE(spi, ad7923_id);

static const struct of_device_id ad7923_of_match[] = {
	{ .compatible = "adi,ad7904", },
	{ .compatible = "adi,ad7914", },
	{ .compatible = "adi,ad7923", },
	{ .compatible = "adi,ad7924", },
	{ .compatible = "adi,ad7908", },
	{ .compatible = "adi,ad7918", },
	{ .compatible = "adi,ad7928", },
	{ },
};
MODULE_DEVICE_TABLE(of, ad7923_of_match);

static struct spi_driver ad7923_driver = {
	.driver = {
		.name	= "ad7923",
		.of_match_table = ad7923_of_match,
	},
	.probe		= ad7923_probe,
	.id_table	= ad7923_id,
};
module_spi_driver(ad7923_driver);

MODULE_AUTHOR("Michael Hennerich <michael.hennerich@analog.com>");
MODULE_AUTHOR("Patrick Vasseur <patrick.vasseur@c-s.fr>");
MODULE_DESCRIPTION("Analog Devices AD7923 and similar ADC");
MODULE_LICENSE("GPL v2");

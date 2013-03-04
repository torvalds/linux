/*
 * AD7923 SPI ADC driver
 *
 * Copyright 2011 Analog Devices Inc (from AD7923 Driver)
 * Copyright 2012 CS Systemes d'Information
 *
 * Licensed under the GPL-2.
 */

#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/sysfs.h>
#include <linux/spi/spi.h>
#include <linux/err.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/interrupt.h>

#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/buffer.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/iio/triggered_buffer.h>

#define AD7923_WRITE_CR		(1 << 11)	/* write control register */
#define AD7923_RANGE		(1 << 1)	/* range to REFin */
#define AD7923_CODING		(1 << 0)	/* coding is straight binary */
#define AD7923_PM_MODE_AS	(1)		/* auto shutdown */
#define AD7923_PM_MODE_FS	(2)		/* full shutdown */
#define AD7923_PM_MODE_OPS	(3)		/* normal operation */
#define AD7923_CHANNEL_0	(0)		/* analog input 0 */
#define AD7923_CHANNEL_1	(1)		/* analog input 1 */
#define AD7923_CHANNEL_2	(2)		/* analog input 2 */
#define AD7923_CHANNEL_3	(3)		/* analog input 3 */
#define AD7923_SEQUENCE_OFF	(0)		/* no sequence fonction */
#define AD7923_SEQUENCE_PROTECT	(2)		/* no interrupt write cycle */
#define AD7923_SEQUENCE_ON	(3)		/* continuous sequence */

#define AD7923_MAX_CHAN		4

#define AD7923_PM_MODE_WRITE(mode)	(mode << 4)	/* write mode */
#define AD7923_CHANNEL_WRITE(channel)	(channel << 6)	/* write channel */
#define AD7923_SEQUENCE_WRITE(sequence)	(((sequence & 1) << 3) \
					+ ((sequence & 2) << 9))
						/* write sequence fonction */
/* left shift for CR : bit 11 transmit in first */
#define AD7923_SHIFT_REGISTER	4

/* val = value, dec = left shift, bits = number of bits of the mask */
#define EXTRACT(val, dec, bits)		((val >> dec) & ((1 << bits) - 1))

struct ad7923_state {
	struct spi_device		*spi;
	struct spi_transfer		ring_xfer[5];
	struct spi_transfer		scan_single_xfer[2];
	struct spi_message		ring_msg;
	struct spi_message		scan_single_msg;
	/*
	 * DMA (thus cache coherency maintenance) requires the
	 * transfer buffers to live in their own cache lines.
	 */
	__be16				rx_buf[4] ____cacheline_aligned;
	__be16				tx_buf[4];
};

#define AD7923_V_CHAN(index)						\
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
			.realbits = 12,					\
			.storagebits = 16,				\
			.endianness = IIO_BE,				\
		},							\
	}

static const struct iio_chan_spec ad7923_channels[] = {
	AD7923_V_CHAN(0),
	AD7923_V_CHAN(1),
	AD7923_V_CHAN(2),
	AD7923_V_CHAN(3),
	IIO_CHAN_SOFT_TIMESTAMP(4),
};

/**
 * ad7923_update_scan_mode() setup the spi transfer buffer for the new scan mask
 **/
static int ad7923_update_scan_mode(struct iio_dev *indio_dev,
	const unsigned long *active_scan_mask)
{
	struct ad7923_state *st = iio_priv(indio_dev);
	int i, cmd, len;

	len = 0;
	for_each_set_bit(i, active_scan_mask, AD7923_MAX_CHAN) {
		cmd = AD7923_WRITE_CR | AD7923_CODING | AD7923_RANGE |
			AD7923_PM_MODE_WRITE(AD7923_PM_MODE_OPS) |
			AD7923_SEQUENCE_WRITE(AD7923_SEQUENCE_OFF) |
			AD7923_CHANNEL_WRITE(i);
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

/**
 * ad7923_trigger_handler() bh of trigger launched polling to ring buffer
 *
 * Currently there is no option in this driver to disable the saving of
 * timestamps within the ring.
 **/
static irqreturn_t ad7923_trigger_handler(int irq, void *p)
{
	struct iio_poll_func *pf = p;
	struct iio_dev *indio_dev = pf->indio_dev;
	struct ad7923_state *st = iio_priv(indio_dev);
	s64 time_ns = 0;
	int b_sent;

	b_sent = spi_sync(st->spi, &st->ring_msg);
	if (b_sent)
		goto done;

	if (indio_dev->scan_timestamp) {
		time_ns = iio_get_time_ns();
		memcpy((u8 *)st->rx_buf + indio_dev->scan_bytes - sizeof(s64),
			&time_ns, sizeof(time_ns));
	}

	iio_push_to_buffers(indio_dev, (u8 *)st->rx_buf);

done:
	iio_trigger_notify_done(indio_dev->trig);

	return IRQ_HANDLED;
}

static int ad7923_scan_direct(struct ad7923_state *st, unsigned ch)
{
	int ret, cmd;

	cmd = AD7923_WRITE_CR | AD7923_PM_MODE_WRITE(AD7923_PM_MODE_OPS) |
		AD7923_SEQUENCE_WRITE(AD7923_SEQUENCE_OFF) | AD7923_CODING |
		AD7923_CHANNEL_WRITE(ch) | AD7923_RANGE;
	cmd <<= AD7923_SHIFT_REGISTER;
	st->tx_buf[0] = cpu_to_be16(cmd);

	ret = spi_sync(st->spi, &st->scan_single_msg);
	if (ret)
		return ret;

	return be16_to_cpu(st->rx_buf[0]);
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
		mutex_lock(&indio_dev->mlock);
		if (iio_buffer_enabled(indio_dev))
			ret = -EBUSY;
		else
			ret = ad7923_scan_direct(st, chan->address);
		mutex_unlock(&indio_dev->mlock);

		if (ret < 0)
			return ret;

		if (chan->address == EXTRACT(ret, 12, 4))
			*val = EXTRACT(ret, 0, 12);
		else
			return -EIO;

		return IIO_VAL_INT;
	}
	return -EINVAL;
}

static const struct iio_info ad7923_info = {
	.read_raw = &ad7923_read_raw,
	.update_scan_mode = ad7923_update_scan_mode,
	.driver_module = THIS_MODULE,
};

static int ad7923_probe(struct spi_device *spi)
{
	struct ad7923_state *st;
	struct iio_dev *indio_dev = iio_device_alloc(sizeof(*st));
	int ret;

	if (indio_dev == NULL)
		return -ENOMEM;

	st = iio_priv(indio_dev);

	spi_set_drvdata(spi, indio_dev);

	st->spi = spi;

	indio_dev->name = spi_get_device_id(spi)->name;
	indio_dev->dev.parent = &spi->dev;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = ad7923_channels;
	indio_dev->num_channels = ARRAY_SIZE(ad7923_channels);
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

	ret = iio_triggered_buffer_setup(indio_dev, NULL,
			&ad7923_trigger_handler, NULL);
	if (ret)
		goto error_free;

	ret = iio_device_register(indio_dev);
	if (ret)
		goto error_cleanup_ring;

	return 0;

error_cleanup_ring:
	iio_triggered_buffer_cleanup(indio_dev);
error_free:
	iio_device_free(indio_dev);

	return ret;
}

static int ad7923_remove(struct spi_device *spi)
{
	struct iio_dev *indio_dev = spi_get_drvdata(spi);

	iio_device_unregister(indio_dev);
	iio_triggered_buffer_cleanup(indio_dev);
	iio_device_free(indio_dev);

	return 0;
}

static const struct spi_device_id ad7923_id[] = {
	{"ad7923", 0},
	{}
};
MODULE_DEVICE_TABLE(spi, ad7923_id);

static struct spi_driver ad7923_driver = {
	.driver = {
		.name	= "ad7923",
		.owner	= THIS_MODULE,
	},
	.probe		= ad7923_probe,
	.remove		= ad7923_remove,
	.id_table	= ad7923_id,
};
module_spi_driver(ad7923_driver);

MODULE_AUTHOR("Michael Hennerich <hennerich@blackfin.uclinux.org>");
MODULE_AUTHOR("Patrick Vasseur <patrick.vasseur@c-s.fr>");
MODULE_DESCRIPTION("Analog Devices AD7923 ADC");
MODULE_LICENSE("GPL v2");

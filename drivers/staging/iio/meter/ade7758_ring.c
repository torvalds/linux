/*
 * ADE7758 Poly Phase Multifunction Energy Metering IC driver
 *
 * Copyright 2010-2011 Analog Devices Inc.
 *
 * Licensed under the GPL-2.
 */
#include <linux/export.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/spi/spi.h>
#include <linux/slab.h>
#include <asm/unaligned.h>

#include <linux/iio/iio.h>
#include <linux/iio/kfifo_buf.h>
#include <linux/iio/trigger_consumer.h>
#include "ade7758.h"

/**
 * ade7758_spi_read_burst() - read data registers
 * @indio_dev: the IIO device
 **/
static int ade7758_spi_read_burst(struct iio_dev *indio_dev)
{
	struct ade7758_state *st = iio_priv(indio_dev);
	int ret;

	ret = spi_sync(st->us, &st->ring_msg);
	if (ret)
		dev_err(&st->us->dev, "problem when reading WFORM value\n");

	return ret;
}

static int ade7758_write_waveform_type(struct device *dev, unsigned type)
{
	int ret;
	u8 reg;

	ret = ade7758_spi_read_reg_8(dev,
			ADE7758_WAVMODE,
			&reg);
	if (ret)
		goto out;

	reg &= ~0x1F;
	reg |= type & 0x1F;

	ret = ade7758_spi_write_reg_8(dev,
			ADE7758_WAVMODE,
			reg);
out:
	return ret;
}

/* Whilst this makes a lot of calls to iio_sw_ring functions - it is too device
 * specific to be rolled into the core.
 */
static irqreturn_t ade7758_trigger_handler(int irq, void *p)
{
	struct iio_poll_func *pf = p;
	struct iio_dev *indio_dev = pf->indio_dev;
	struct ade7758_state *st = iio_priv(indio_dev);
	s64 dat64[2];
	u32 *dat32 = (u32 *)dat64;

	if (!bitmap_empty(indio_dev->active_scan_mask, indio_dev->masklength))
		if (ade7758_spi_read_burst(indio_dev) >= 0)
			*dat32 = get_unaligned_be32(&st->rx_buf[5]) & 0xFFFFFF;

	/* Guaranteed to be aligned with 8 byte boundary */
	if (indio_dev->scan_timestamp)
		dat64[1] = pf->timestamp;

	iio_push_to_buffers(indio_dev, (u8 *)dat64);

	iio_trigger_notify_done(indio_dev->trig);

	return IRQ_HANDLED;
}

/**
 * ade7758_ring_preenable() setup the parameters of the ring before enabling
 *
 * The complex nature of the setting of the number of bytes per datum is due
 * to this driver currently ensuring that the timestamp is stored at an 8
 * byte boundary.
 **/
static int ade7758_ring_preenable(struct iio_dev *indio_dev)
{
	struct ade7758_state *st = iio_priv(indio_dev);
	unsigned channel;
	int ret;

	if (!bitmap_empty(indio_dev->active_scan_mask, indio_dev->masklength))
		return -EINVAL;

	ret = iio_sw_buffer_preenable(indio_dev);
	if (ret < 0)
		return ret;

	channel = find_first_bit(indio_dev->active_scan_mask,
				 indio_dev->masklength);

	ade7758_write_waveform_type(&indio_dev->dev,
		st->ade7758_ring_channels[channel].address);

	return 0;
}

static const struct iio_buffer_setup_ops ade7758_ring_setup_ops = {
	.preenable = &ade7758_ring_preenable,
	.postenable = &iio_triggered_buffer_postenable,
	.predisable = &iio_triggered_buffer_predisable,
	.validate_scan_mask = &iio_validate_scan_mask_onehot,
};

void ade7758_unconfigure_ring(struct iio_dev *indio_dev)
{
	iio_dealloc_pollfunc(indio_dev->pollfunc);
	iio_kfifo_free(indio_dev->buffer);
}

int ade7758_configure_ring(struct iio_dev *indio_dev)
{
	struct ade7758_state *st = iio_priv(indio_dev);
	int ret = 0;

	indio_dev->buffer = iio_kfifo_allocate(indio_dev);
	if (!indio_dev->buffer) {
		ret = -ENOMEM;
		return ret;
	}

	indio_dev->setup_ops = &ade7758_ring_setup_ops;

	indio_dev->pollfunc = iio_alloc_pollfunc(&iio_pollfunc_store_time,
						 &ade7758_trigger_handler,
						 0,
						 indio_dev,
						 "ade7759_consumer%d",
						 indio_dev->id);
	if (indio_dev->pollfunc == NULL) {
		ret = -ENOMEM;
		goto error_iio_kfifo_free;
	}

	indio_dev->modes |= INDIO_BUFFER_TRIGGERED;

	st->tx_buf[0] = ADE7758_READ_REG(ADE7758_RSTATUS);
	st->tx_buf[1] = 0;
	st->tx_buf[2] = 0;
	st->tx_buf[3] = 0;
	st->tx_buf[4] = ADE7758_READ_REG(ADE7758_WFORM);
	st->tx_buf[5] = 0;
	st->tx_buf[6] = 0;
	st->tx_buf[7] = 0;

	/* build spi ring message */
	st->ring_xfer[0].tx_buf = &st->tx_buf[0];
	st->ring_xfer[0].len = 1;
	st->ring_xfer[0].bits_per_word = 8;
	st->ring_xfer[0].delay_usecs = 4;
	st->ring_xfer[1].rx_buf = &st->rx_buf[1];
	st->ring_xfer[1].len = 3;
	st->ring_xfer[1].bits_per_word = 8;
	st->ring_xfer[1].cs_change = 1;

	st->ring_xfer[2].tx_buf = &st->tx_buf[4];
	st->ring_xfer[2].len = 1;
	st->ring_xfer[2].bits_per_word = 8;
	st->ring_xfer[2].delay_usecs = 1;
	st->ring_xfer[3].rx_buf = &st->rx_buf[5];
	st->ring_xfer[3].len = 3;
	st->ring_xfer[3].bits_per_word = 8;

	spi_message_init(&st->ring_msg);
	spi_message_add_tail(&st->ring_xfer[0], &st->ring_msg);
	spi_message_add_tail(&st->ring_xfer[1], &st->ring_msg);
	spi_message_add_tail(&st->ring_xfer[2], &st->ring_msg);
	spi_message_add_tail(&st->ring_xfer[3], &st->ring_msg);

	return 0;

error_iio_kfifo_free:
	iio_kfifo_free(indio_dev->buffer);
	return ret;
}

void ade7758_uninitialize_ring(struct iio_dev *indio_dev)
{
	iio_buffer_unregister(indio_dev);
}

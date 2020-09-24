// SPDX-License-Identifier: GPL-2.0
#include <linux/interrupt.h>
#include <linux/mutex.h>
#include <linux/kernel.h>
#include <linux/spi/spi.h>
#include <linux/slab.h>
#include <linux/bitops.h>
#include <linux/export.h>

#include <linux/iio/iio.h>
#include <linux/iio/buffer.h>
#include <linux/iio/triggered_buffer.h>
#include <linux/iio/trigger_consumer.h>

#include "adis16400.h"

int adis16400_update_scan_mode(struct iio_dev *indio_dev,
	const unsigned long *scan_mask)
{
	struct adis16400_state *st = iio_priv(indio_dev);
	struct adis *adis = &st->adis;
	unsigned int burst_length;
	u8 *tx;

	if (st->variant->flags & ADIS16400_NO_BURST)
		return adis_update_scan_mode(indio_dev, scan_mask);

	kfree(adis->xfer);
	kfree(adis->buffer);

	/* All but the timestamp channel */
	burst_length = (indio_dev->num_channels - 1) * sizeof(u16);
	if (st->variant->flags & ADIS16400_BURST_DIAG_STAT)
		burst_length += sizeof(u16);

	adis->xfer = kcalloc(2, sizeof(*adis->xfer), GFP_KERNEL);
	if (!adis->xfer)
		return -ENOMEM;

	adis->buffer = kzalloc(burst_length + sizeof(u16), GFP_KERNEL);
	if (!adis->buffer) {
		kfree(adis->xfer);
		adis->xfer = NULL;
		return -ENOMEM;
	}

	tx = adis->buffer + burst_length;
	tx[0] = ADIS_READ_REG(ADIS16400_GLOB_CMD);
	tx[1] = 0;

	adis->xfer[0].tx_buf = tx;
	adis->xfer[0].bits_per_word = 8;
	adis->xfer[0].len = 2;
	adis->xfer[1].rx_buf = adis->buffer;
	adis->xfer[1].bits_per_word = 8;
	adis->xfer[1].len = burst_length;

	spi_message_init(&adis->msg);
	spi_message_add_tail(&adis->xfer[0], &adis->msg);
	spi_message_add_tail(&adis->xfer[1], &adis->msg);

	return 0;
}

irqreturn_t adis16400_trigger_handler(int irq, void *p)
{
	struct iio_poll_func *pf = p;
	struct iio_dev *indio_dev = pf->indio_dev;
	struct adis16400_state *st = iio_priv(indio_dev);
	struct adis *adis = &st->adis;
	u32 old_speed_hz = st->adis.spi->max_speed_hz;
	void *buffer;
	int ret;

	if (!adis->buffer)
		return -ENOMEM;

	if (!(st->variant->flags & ADIS16400_NO_BURST) &&
		st->adis.spi->max_speed_hz > ADIS16400_SPI_BURST) {
		st->adis.spi->max_speed_hz = ADIS16400_SPI_BURST;
		spi_setup(st->adis.spi);
	}

	ret = spi_sync(adis->spi, &adis->msg);
	if (ret)
		dev_err(&adis->spi->dev, "Failed to read data: %d\n", ret);

	if (!(st->variant->flags & ADIS16400_NO_BURST)) {
		st->adis.spi->max_speed_hz = old_speed_hz;
		spi_setup(st->adis.spi);
	}

	if (st->variant->flags & ADIS16400_BURST_DIAG_STAT)
		buffer = adis->buffer + sizeof(u16);
	else
		buffer = adis->buffer;

	iio_push_to_buffers_with_timestamp(indio_dev, buffer,
		pf->timestamp);

	iio_trigger_notify_done(indio_dev->trig);

	return IRQ_HANDLED;
}

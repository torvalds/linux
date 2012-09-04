/*
 * AD7298 SPI ADC driver
 *
 * Copyright 2011-2012 Analog Devices Inc.
 *
 * Licensed under the GPL-2.
 */

#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/spi/spi.h>

#include <linux/iio/iio.h>
#include <linux/iio/buffer.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/iio/triggered_buffer.h>

#include "ad7298.h"

/**
 * ad7298_update_scan_mode() setup the spi transfer buffer for the new scan mask
 **/
int ad7298_update_scan_mode(struct iio_dev *indio_dev,
	const unsigned long *active_scan_mask)
{
	struct ad7298_state *st = iio_priv(indio_dev);
	int i, m;
	unsigned short command;
	int scan_count;

	/* Now compute overall size */
	scan_count = bitmap_weight(active_scan_mask, indio_dev->masklength);

	command = AD7298_WRITE | st->ext_ref;

	for (i = 0, m = AD7298_CH(0); i < AD7298_MAX_CHAN; i++, m >>= 1)
		if (test_bit(i, active_scan_mask))
			command |= m;

	st->tx_buf[0] = cpu_to_be16(command);

	/* build spi ring message */
	st->ring_xfer[0].tx_buf = &st->tx_buf[0];
	st->ring_xfer[0].len = 2;
	st->ring_xfer[0].cs_change = 1;
	st->ring_xfer[1].tx_buf = &st->tx_buf[1];
	st->ring_xfer[1].len = 2;
	st->ring_xfer[1].cs_change = 1;

	spi_message_init(&st->ring_msg);
	spi_message_add_tail(&st->ring_xfer[0], &st->ring_msg);
	spi_message_add_tail(&st->ring_xfer[1], &st->ring_msg);

	for (i = 0; i < scan_count; i++) {
		st->ring_xfer[i + 2].rx_buf = &st->rx_buf[i];
		st->ring_xfer[i + 2].len = 2;
		st->ring_xfer[i + 2].cs_change = 1;
		spi_message_add_tail(&st->ring_xfer[i + 2], &st->ring_msg);
	}
	/* make sure last transfer cs_change is not set */
	st->ring_xfer[i + 1].cs_change = 0;

	return 0;
}

/**
 * ad7298_trigger_handler() bh of trigger launched polling to ring buffer
 *
 * Currently there is no option in this driver to disable the saving of
 * timestamps within the ring.
 **/
static irqreturn_t ad7298_trigger_handler(int irq, void *p)
{
	struct iio_poll_func *pf = p;
	struct iio_dev *indio_dev = pf->indio_dev;
	struct ad7298_state *st = iio_priv(indio_dev);
	struct iio_buffer *ring = indio_dev->buffer;
	s64 time_ns = 0;
	__u16 buf[16];
	int b_sent, i;

	b_sent = spi_sync(st->spi, &st->ring_msg);
	if (b_sent)
		goto done;

	if (indio_dev->scan_timestamp) {
		time_ns = iio_get_time_ns();
		memcpy((u8 *)buf + indio_dev->scan_bytes - sizeof(s64),
			&time_ns, sizeof(time_ns));
	}

	for (i = 0; i < bitmap_weight(indio_dev->active_scan_mask,
						 indio_dev->masklength); i++)
		buf[i] = be16_to_cpu(st->rx_buf[i]);

	indio_dev->buffer->access->store_to(ring, (u8 *)buf, time_ns);

done:
	iio_trigger_notify_done(indio_dev->trig);

	return IRQ_HANDLED;
}

int ad7298_register_ring_funcs_and_init(struct iio_dev *indio_dev)
{
	return iio_triggered_buffer_setup(indio_dev, NULL,
			&ad7298_trigger_handler, NULL);
}

void ad7298_ring_cleanup(struct iio_dev *indio_dev)
{
	iio_triggered_buffer_cleanup(indio_dev);
}

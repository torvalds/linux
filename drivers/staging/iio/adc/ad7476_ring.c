/*
 * Copyright 2010-2012 Analog Devices Inc.
 * Copyright (C) 2008 Jonathan Cameron
 *
 * Licensed under the GPL-2 or later.
 *
 * ad7476_ring.c
 */

#include <linux/interrupt.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/spi/spi.h>

#include <linux/iio/iio.h>
#include <linux/iio/buffer.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/iio/triggered_buffer.h>

#include "ad7476.h"

static irqreturn_t ad7476_trigger_handler(int irq, void  *p)
{
	struct iio_poll_func *pf = p;
	struct iio_dev *indio_dev = pf->indio_dev;
	struct ad7476_state *st = iio_priv(indio_dev);
	s64 time_ns;
	__u8 *rxbuf;
	int b_sent;

	rxbuf = kzalloc(indio_dev->scan_bytes, GFP_KERNEL);
	if (rxbuf == NULL)
		goto done;

	b_sent = spi_read(st->spi, rxbuf,
			  st->chip_info->channel[0].scan_type.storagebits / 8);
	if (b_sent < 0)
		goto done;

	time_ns = iio_get_time_ns();

	if (indio_dev->scan_timestamp)
		memcpy(rxbuf + indio_dev->scan_bytes - sizeof(s64),
			&time_ns, sizeof(time_ns));

	iio_push_to_buffer(indio_dev->buffer, rxbuf, time_ns);
done:
	iio_trigger_notify_done(indio_dev->trig);
	kfree(rxbuf);

	return IRQ_HANDLED;
}

int ad7476_register_ring_funcs_and_init(struct iio_dev *indio_dev)
{
	return iio_triggered_buffer_setup(indio_dev, NULL,
			&ad7476_trigger_handler, NULL);
}

void ad7476_ring_cleanup(struct iio_dev *indio_dev)
{
	iio_triggered_buffer_cleanup(indio_dev);
}

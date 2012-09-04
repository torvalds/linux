/*
 * Copyright 2010-2012 Analog Devices Inc.
 * Copyright (C) 2008 Jonathan Cameron
 *
 * Licensed under the GPL-2.
 *
 * ad7887_ring.c
 */

#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/spi/spi.h>

#include <linux/iio/iio.h>
#include <linux/iio/buffer.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/iio/triggered_buffer.h>

#include "ad7887.h"

/**
 * ad7887_ring_preenable() setup the parameters of the ring before enabling
 *
 * The complex nature of the setting of the nuber of bytes per datum is due
 * to this driver currently ensuring that the timestamp is stored at an 8
 * byte boundary.
 **/
static int ad7887_ring_preenable(struct iio_dev *indio_dev)
{
	struct ad7887_state *st = iio_priv(indio_dev);
	int ret;

	ret = iio_sw_buffer_preenable(indio_dev);
	if (ret < 0)
		return ret;

	/* We know this is a single long so can 'cheat' */
	switch (*indio_dev->active_scan_mask) {
	case (1 << 0):
		st->ring_msg = &st->msg[AD7887_CH0];
		break;
	case (1 << 1):
		st->ring_msg = &st->msg[AD7887_CH1];
		/* Dummy read: push CH1 setting down to hardware */
		spi_sync(st->spi, st->ring_msg);
		break;
	case ((1 << 1) | (1 << 0)):
		st->ring_msg = &st->msg[AD7887_CH0_CH1];
		break;
	}

	return 0;
}

static int ad7887_ring_postdisable(struct iio_dev *indio_dev)
{
	struct ad7887_state *st = iio_priv(indio_dev);

	/* dummy read: restore default CH0 settin */
	return spi_sync(st->spi, &st->msg[AD7887_CH0]);
}

/**
 * ad7887_trigger_handler() bh of trigger launched polling to ring buffer
 *
 * Currently there is no option in this driver to disable the saving of
 * timestamps within the ring.
 **/
static irqreturn_t ad7887_trigger_handler(int irq, void *p)
{
	struct iio_poll_func *pf = p;
	struct iio_dev *indio_dev = pf->indio_dev;
	struct ad7887_state *st = iio_priv(indio_dev);
	s64 time_ns;
	__u8 *buf;
	int b_sent;

	unsigned int bytes = bitmap_weight(indio_dev->active_scan_mask,
					   indio_dev->masklength) *
		st->chip_info->channel[0].scan_type.storagebits / 8;

	buf = kzalloc(indio_dev->scan_bytes, GFP_KERNEL);
	if (buf == NULL)
		goto done;

	b_sent = spi_sync(st->spi, st->ring_msg);
	if (b_sent)
		goto done;

	time_ns = iio_get_time_ns();

	memcpy(buf, st->data, bytes);
	if (indio_dev->scan_timestamp)
		memcpy(buf + indio_dev->scan_bytes - sizeof(s64),
		       &time_ns, sizeof(time_ns));

	iio_push_to_buffer(indio_dev->buffer, buf, time_ns);
done:
	kfree(buf);
	iio_trigger_notify_done(indio_dev->trig);

	return IRQ_HANDLED;
}

static const struct iio_buffer_setup_ops ad7887_ring_setup_ops = {
	.preenable = &ad7887_ring_preenable,
	.postenable = &iio_triggered_buffer_postenable,
	.predisable = &iio_triggered_buffer_predisable,
	.postdisable = &ad7887_ring_postdisable,
};

int ad7887_register_ring_funcs_and_init(struct iio_dev *indio_dev)
{
	return iio_triggered_buffer_setup(indio_dev, &iio_pollfunc_store_time,
			&ad7887_trigger_handler, &ad7887_ring_setup_ops);
}

void ad7887_ring_cleanup(struct iio_dev *indio_dev)
{
	iio_triggered_buffer_cleanup(indio_dev);
}

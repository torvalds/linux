/*
 * Copyright 2010-2011 Analog Devices Inc.
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

#include "../iio.h"
#include "../buffer.h"
#include "../ring_sw.h"
#include "../trigger_consumer.h"

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
	struct iio_buffer *ring = indio_dev->buffer;

	st->d_size = bitmap_weight(indio_dev->active_scan_mask,
				   indio_dev->masklength) *
		st->chip_info->channel[0].scan_type.storagebits / 8;

	if (ring->scan_timestamp) {
		st->d_size += sizeof(s64);

		if (st->d_size % sizeof(s64))
			st->d_size += sizeof(s64) - (st->d_size % sizeof(s64));
	}

	if (indio_dev->buffer->access->set_bytes_per_datum)
		indio_dev->buffer->access->
			set_bytes_per_datum(indio_dev->buffer, st->d_size);

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
	struct iio_buffer *ring = indio_dev->buffer;
	s64 time_ns;
	__u8 *buf;
	int b_sent;

	unsigned int bytes = bitmap_weight(indio_dev->active_scan_mask,
					   indio_dev->masklength) *
		st->chip_info->channel[0].scan_type.storagebits / 8;

	buf = kzalloc(st->d_size, GFP_KERNEL);
	if (buf == NULL)
		return -ENOMEM;

	b_sent = spi_sync(st->spi, st->ring_msg);
	if (b_sent)
		goto done;

	time_ns = iio_get_time_ns();

	memcpy(buf, st->data, bytes);
	if (ring->scan_timestamp)
		memcpy(buf + st->d_size - sizeof(s64),
		       &time_ns, sizeof(time_ns));

	indio_dev->buffer->access->store_to(indio_dev->buffer, buf, time_ns);
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
	int ret;

	indio_dev->buffer = iio_sw_rb_allocate(indio_dev);
	if (!indio_dev->buffer) {
		ret = -ENOMEM;
		goto error_ret;
	}
	/* Effectively select the ring buffer implementation */
	indio_dev->buffer->access = &ring_sw_access_funcs;
	indio_dev->pollfunc = iio_alloc_pollfunc(&iio_pollfunc_store_time,
						 &ad7887_trigger_handler,
						 IRQF_ONESHOT,
						 indio_dev,
						 "ad7887_consumer%d",
						 indio_dev->id);
	if (indio_dev->pollfunc == NULL) {
		ret = -ENOMEM;
		goto error_deallocate_sw_rb;
	}
	/* Ring buffer functions - here trigger setup related */
	indio_dev->setup_ops = &ad7887_ring_setup_ops;

	/* Flag that polled ring buffering is possible */
	indio_dev->modes |= INDIO_BUFFER_TRIGGERED;
	return 0;

error_deallocate_sw_rb:
	iio_sw_rb_free(indio_dev->buffer);
error_ret:
	return ret;
}

void ad7887_ring_cleanup(struct iio_dev *indio_dev)
{
	iio_dealloc_pollfunc(indio_dev->pollfunc);
	iio_sw_rb_free(indio_dev->buffer);
}

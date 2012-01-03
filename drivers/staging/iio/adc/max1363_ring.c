/*
 * Copyright (C) 2008 Jonathan Cameron
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * max1363_ring.c
 */

#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/i2c.h>
#include <linux/bitops.h>

#include "../iio.h"
#include "../buffer.h"
#include "../ring_sw.h"
#include "../trigger_consumer.h"

#include "max1363.h"

int max1363_update_scan_mode(struct iio_dev *indio_dev,
			     const unsigned long *scan_mask)
{
	struct max1363_state *st = iio_priv(indio_dev);

	/*
	 * Need to figure out the current mode based upon the requested
	 * scan mask in iio_dev
	 */
	st->current_mode = max1363_match_mode(scan_mask, st->chip_info);
	if (!st->current_mode)
		return -EINVAL;
	max1363_set_scan_mode(st);
	return 0;
}

static irqreturn_t max1363_trigger_handler(int irq, void *p)
{
	struct iio_poll_func *pf = p;
	struct iio_dev *indio_dev = pf->indio_dev;
	struct max1363_state *st = iio_priv(indio_dev);
	s64 time_ns;
	__u8 *rxbuf;
	int b_sent;
	size_t d_size;
	unsigned long numvals = bitmap_weight(st->current_mode->modemask,
					      MAX1363_MAX_CHANNELS);

	/* Ensure the timestamp is 8 byte aligned */
	if (st->chip_info->bits != 8)
		d_size = numvals*2;
	else
		d_size = numvals;
	if (indio_dev->buffer->scan_timestamp) {
		d_size += sizeof(s64);
		if (d_size % sizeof(s64))
			d_size += sizeof(s64) - (d_size % sizeof(s64));
	}
	/* Monitor mode prevents reading. Whilst not currently implemented
	 * might as well have this test in here in the meantime as it does
	 * no harm.
	 */
	if (numvals == 0)
		return IRQ_HANDLED;

	rxbuf = kmalloc(d_size,	GFP_KERNEL);
	if (rxbuf == NULL)
		return -ENOMEM;
	if (st->chip_info->bits != 8)
		b_sent = i2c_master_recv(st->client, rxbuf, numvals*2);
	else
		b_sent = i2c_master_recv(st->client, rxbuf, numvals);
	if (b_sent < 0)
		goto done;

	time_ns = iio_get_time_ns();

	if (indio_dev->buffer->scan_timestamp)
		memcpy(rxbuf + d_size - sizeof(s64), &time_ns, sizeof(time_ns));
	iio_push_to_buffer(indio_dev->buffer, rxbuf, time_ns);

done:
	iio_trigger_notify_done(indio_dev->trig);
	kfree(rxbuf);

	return IRQ_HANDLED;
}

static const struct iio_buffer_setup_ops max1363_ring_setup_ops = {
	.postenable = &iio_triggered_buffer_postenable,
	.preenable = &iio_sw_buffer_preenable,
	.predisable = &iio_triggered_buffer_predisable,
};

int max1363_register_ring_funcs_and_init(struct iio_dev *indio_dev)
{
	struct max1363_state *st = iio_priv(indio_dev);
	int ret = 0;

	indio_dev->buffer = iio_sw_rb_allocate(indio_dev);
	if (!indio_dev->buffer) {
		ret = -ENOMEM;
		goto error_ret;
	}
	indio_dev->pollfunc = iio_alloc_pollfunc(NULL,
						 &max1363_trigger_handler,
						 IRQF_ONESHOT,
						 indio_dev,
						 "%s_consumer%d",
						 st->client->name,
						 indio_dev->id);
	if (indio_dev->pollfunc == NULL) {
		ret = -ENOMEM;
		goto error_deallocate_sw_rb;
	}
	/* Ring buffer functions - here trigger setup related */
	indio_dev->setup_ops = &max1363_ring_setup_ops;

	/* Flag that polled ring buffering is possible */
	indio_dev->modes |= INDIO_BUFFER_TRIGGERED;

	return 0;

error_deallocate_sw_rb:
	iio_sw_rb_free(indio_dev->buffer);
error_ret:
	return ret;
}

void max1363_ring_cleanup(struct iio_dev *indio_dev)
{
	/* ensure that the trigger has been detached */
	iio_dealloc_pollfunc(indio_dev->pollfunc);
	iio_sw_rb_free(indio_dev->buffer);
}

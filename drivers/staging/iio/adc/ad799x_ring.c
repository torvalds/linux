/*
 * Copyright (C) 2010 Michael Hennerich, Analog Devices Inc.
 * Copyright (C) 2008-2010 Jonathan Cameron
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * ad799x_ring.c
 */

#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/i2c.h>
#include <linux/bitops.h>

#include "../iio.h"
#include "../buffer.h"
#include "../ring_sw.h"
#include "../trigger_consumer.h"

#include "ad799x.h"

/**
 * ad799x_ring_preenable() setup the parameters of the ring before enabling
 *
 * The complex nature of the setting of the number of bytes per datum is due
 * to this driver currently ensuring that the timestamp is stored at an 8
 * byte boundary.
 **/
static int ad799x_ring_preenable(struct iio_dev *indio_dev)
{
	struct iio_buffer *ring = indio_dev->buffer;
	struct ad799x_state *st = iio_priv(indio_dev);

	/*
	 * Need to figure out the current mode based upon the requested
	 * scan mask in iio_dev
	 */

	if (st->id == ad7997 || st->id == ad7998)
		ad7997_8_set_scan_mode(st, *indio_dev->active_scan_mask);

	st->d_size = bitmap_weight(indio_dev->active_scan_mask,
				   indio_dev->masklength) * 2;

	if (ring->scan_timestamp) {
		st->d_size += sizeof(s64);

		if (st->d_size % sizeof(s64))
			st->d_size += sizeof(s64) - (st->d_size % sizeof(s64));
	}

	if (indio_dev->buffer->access->set_bytes_per_datum)
		indio_dev->buffer->access->
			set_bytes_per_datum(indio_dev->buffer, st->d_size);

	return 0;
}

/**
 * ad799x_trigger_handler() bh of trigger launched polling to ring buffer
 *
 * Currently there is no option in this driver to disable the saving of
 * timestamps within the ring.
 **/

static irqreturn_t ad799x_trigger_handler(int irq, void *p)
{
	struct iio_poll_func *pf = p;
	struct iio_dev *indio_dev = pf->indio_dev;
	struct ad799x_state *st = iio_priv(indio_dev);
	struct iio_buffer *ring = indio_dev->buffer;
	s64 time_ns;
	__u8 *rxbuf;
	int b_sent;
	u8 cmd;

	rxbuf = kmalloc(st->d_size, GFP_KERNEL);
	if (rxbuf == NULL)
		goto out;

	switch (st->id) {
	case ad7991:
	case ad7995:
	case ad7999:
		cmd = st->config |
			(*indio_dev->active_scan_mask << AD799X_CHANNEL_SHIFT);
		break;
	case ad7992:
	case ad7993:
	case ad7994:
		cmd = (*indio_dev->active_scan_mask << AD799X_CHANNEL_SHIFT) |
			AD7998_CONV_RES_REG;
		break;
	case ad7997:
	case ad7998:
		cmd = AD7997_8_READ_SEQUENCE | AD7998_CONV_RES_REG;
		break;
	default:
		cmd = 0;
	}

	b_sent = i2c_smbus_read_i2c_block_data(st->client,
			cmd, bitmap_weight(indio_dev->active_scan_mask,
					   indio_dev->masklength) * 2, rxbuf);
	if (b_sent < 0)
		goto done;

	time_ns = iio_get_time_ns();

	if (ring->scan_timestamp)
		memcpy(rxbuf + st->d_size - sizeof(s64),
			&time_ns, sizeof(time_ns));

	ring->access->store_to(indio_dev->buffer, rxbuf, time_ns);
done:
	kfree(rxbuf);
	if (b_sent < 0)
		return b_sent;
out:
	iio_trigger_notify_done(indio_dev->trig);

	return IRQ_HANDLED;
}

static const struct iio_buffer_setup_ops ad799x_buf_setup_ops = {
	.preenable = &ad799x_ring_preenable,
	.postenable = &iio_triggered_buffer_postenable,
	.predisable = &iio_triggered_buffer_predisable,
};

int ad799x_register_ring_funcs_and_init(struct iio_dev *indio_dev)
{
	int ret = 0;

	indio_dev->buffer = iio_sw_rb_allocate(indio_dev);
	if (!indio_dev->buffer) {
		ret = -ENOMEM;
		goto error_ret;
	}
	indio_dev->pollfunc = iio_alloc_pollfunc(NULL,
						 &ad799x_trigger_handler,
						 IRQF_ONESHOT,
						 indio_dev,
						 "%s_consumer%d",
						 indio_dev->name,
						 indio_dev->id);
	if (indio_dev->pollfunc == NULL) {
		ret = -ENOMEM;
		goto error_deallocate_sw_rb;
	}

	/* Ring buffer functions - here trigger setup related */
	indio_dev->setup_ops = &ad799x_buf_setup_ops;
	indio_dev->buffer->scan_timestamp = true;

	/* Flag that polled ring buffering is possible */
	indio_dev->modes |= INDIO_BUFFER_TRIGGERED;
	return 0;

error_deallocate_sw_rb:
	iio_sw_rb_free(indio_dev->buffer);
error_ret:
	return ret;
}

void ad799x_ring_cleanup(struct iio_dev *indio_dev)
{
	iio_dealloc_pollfunc(indio_dev->pollfunc);
	iio_sw_rb_free(indio_dev->buffer);
}

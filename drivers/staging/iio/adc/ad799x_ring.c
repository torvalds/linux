/*
 * Copyright (C) 2010-2012 Michael Hennerich, Analog Devices Inc.
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

#include <linux/iio/iio.h>
#include <linux/iio/buffer.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/iio/triggered_buffer.h>

#include "ad799x.h"

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
	s64 time_ns;
	__u8 *rxbuf;
	int b_sent;
	u8 cmd;

	rxbuf = kmalloc(indio_dev->scan_bytes, GFP_KERNEL);
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

	if (indio_dev->scan_timestamp)
		memcpy(rxbuf + indio_dev->scan_bytes - sizeof(s64),
			&time_ns, sizeof(time_ns));

	iio_push_to_buffer(indio_dev->buffer, rxbuf, time_ns);
done:
	kfree(rxbuf);
out:
	iio_trigger_notify_done(indio_dev->trig);

	return IRQ_HANDLED;
}

int ad799x_register_ring_funcs_and_init(struct iio_dev *indio_dev)
{
	return iio_triggered_buffer_setup(indio_dev, NULL,
		&ad799x_trigger_handler, NULL);
}

void ad799x_ring_cleanup(struct iio_dev *indio_dev)
{
	iio_triggered_buffer_cleanup(indio_dev);
}

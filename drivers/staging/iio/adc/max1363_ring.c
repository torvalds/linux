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
#include <linux/gpio.h>
#include <linux/workqueue.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/sysfs.h>
#include <linux/list.h>
#include <linux/i2c.h>
#include <linux/bitops.h>

#include "../iio.h"
#include "../ring_generic.h"
#include "../ring_sw.h"
#include "../trigger.h"
#include "../sysfs.h"

#include "max1363.h"

/* Todo: test this */
int max1363_single_channel_from_ring(long mask, struct max1363_state *st)
{
	unsigned long numvals;
	int count = 0, ret;
	u8 *ring_data;
	if (!(st->current_mode->modemask & mask)) {
		ret = -EBUSY;
		goto error_ret;
	}
	numvals = hweight_long(st->current_mode->modemask);

	ring_data = kmalloc(numvals*2, GFP_KERNEL);
	if (ring_data == NULL) {
		ret = -ENOMEM;
		goto error_ret;
	}
	ret = st->indio_dev->ring->access.read_last(st->indio_dev->ring,
						ring_data);
	if (ret)
		goto error_free_ring_data;
	/* Need a count of channels prior to this one */
	mask >>= 1;
	while (mask) {
		if (mask & st->current_mode->modemask)
			count++;
		mask >>= 1;
	}
	if (st->chip_info->bits != 8)
		ret = ((int)(ring_data[count*2 + 0] & 0x0F) << 8)
			+ (int)(ring_data[count*2 + 1]);
	else
		ret = ring_data[count];

error_free_ring_data:
	kfree(ring_data);
error_ret:
	return ret;
}

/**
 * max1363_ring_preenable() setup the parameters of the ring before enabling
 *
 * The complex nature of the setting of the nuber of bytes per datum is due
 * to this driver currently ensuring that the timestamp is stored at an 8
 * byte boundary.
 **/
static int max1363_ring_preenable(struct iio_dev *indio_dev)
{
	struct max1363_state *st = indio_dev->dev_data;
	size_t d_size;
	unsigned long numvals;

	/*
	 * Need to figure out the current mode based upon the requested
	 * scan mask in iio_dev
	 */
	st->current_mode = max1363_match_mode(st->indio_dev->scan_mask,
					st->chip_info);
	if (!st->current_mode)
		return -EINVAL;

	max1363_set_scan_mode(st);

	numvals = hweight_long(st->current_mode->modemask);
	if (indio_dev->ring->access.set_bpd) {
		if (st->chip_info->bits != 8)
			d_size = numvals*2 + sizeof(s64);
		else
			d_size = numvals + sizeof(s64);
		if (d_size % 8)
			d_size += 8 - (d_size % 8);
		indio_dev->ring->access.set_bpd(indio_dev->ring, d_size);
	}

	return 0;
}

/**
 * max1363_ring_postenable() typical ring post enable
 *
 * Only not moved into the core for the hardware ring buffer cases
 * that are more sophisticated.
 **/
static int max1363_ring_postenable(struct iio_dev *indio_dev)
{
	if (indio_dev->trig == NULL)
		return 0;
	return iio_trigger_attach_poll_func(indio_dev->trig,
					    indio_dev->pollfunc);
}

/**
 * max1363_ring_predisable() runs just prior to ring buffer being disabled
 *
 * Typical predisable function which ensures that no trigger events can
 * occur before we disable the ring buffer (and hence would have no idea
 * what to do with them)
 **/
static int max1363_ring_predisable(struct iio_dev *indio_dev)
{
	if (indio_dev->trig)
		return iio_trigger_dettach_poll_func(indio_dev->trig,
						     indio_dev->pollfunc);
	else
		return 0;
}

/**
 * max1363_poll_func_th() th of trigger launched polling to ring buffer
 *
 * As sampling only occurs on i2c comms occuring, leave timestamping until
 * then.  Some triggers will generate their own time stamp.  Currently
 * there is no way of notifying them when no one cares.
 **/
static void max1363_poll_func_th(struct iio_dev *indio_dev)
{
	struct max1363_state *st = indio_dev->dev_data;

	schedule_work(&st->poll_work);

	return;
}
/**
 * max1363_poll_bh_to_ring() bh of trigger launched polling to ring buffer
 * @work_s:	the work struct through which this was scheduled
 *
 * Currently there is no option in this driver to disable the saving of
 * timestamps within the ring.
 * I think the one copy of this at a time was to avoid problems if the
 * trigger was set far too high and the reads then locked up the computer.
 **/
static void max1363_poll_bh_to_ring(struct work_struct *work_s)
{
	struct max1363_state *st = container_of(work_s, struct max1363_state,
						  poll_work);
	struct iio_dev *indio_dev = st->indio_dev;
	struct iio_sw_ring_buffer *ring = iio_to_sw_ring(indio_dev->ring);
	s64 time_ns;
	__u8 *rxbuf;
	int b_sent;
	size_t d_size;
	unsigned long numvals = hweight_long(st->current_mode->modemask);

	/* Ensure the timestamp is 8 byte aligned */
	if (st->chip_info->bits != 8)
		d_size = numvals*2 + sizeof(s64);
	else
		d_size = numvals + sizeof(s64);
	if (d_size % sizeof(s64))
		d_size += sizeof(s64) - (d_size % sizeof(s64));

	/* Ensure only one copy of this function running at a time */
	if (atomic_inc_return(&st->protect_ring) > 1)
		return;

	/* Monitor mode prevents reading. Whilst not currently implemented
	 * might as well have this test in here in the meantime as it does
	 * no harm.
	 */
	if (numvals == 0)
		return;

	rxbuf = kmalloc(d_size,	GFP_KERNEL);
	if (rxbuf == NULL)
		return;
	if (st->chip_info->bits != 8)
		b_sent = i2c_master_recv(st->client, rxbuf, numvals*2);
	else
		b_sent = i2c_master_recv(st->client, rxbuf, numvals);
	if (b_sent < 0)
		goto done;

	time_ns = iio_get_time_ns();

	memcpy(rxbuf + d_size - sizeof(s64), &time_ns, sizeof(time_ns));

	indio_dev->ring->access.store_to(&ring->buf, rxbuf, time_ns);
done:
	kfree(rxbuf);
	atomic_dec(&st->protect_ring);
}


int max1363_register_ring_funcs_and_init(struct iio_dev *indio_dev)
{
	struct max1363_state *st = indio_dev->dev_data;
	int ret = 0;

	indio_dev->ring = iio_sw_rb_allocate(indio_dev);
	if (!indio_dev->ring) {
		ret = -ENOMEM;
		goto error_ret;
	}
	/* Effectively select the ring buffer implementation */
	iio_ring_sw_register_funcs(&st->indio_dev->ring->access);
	indio_dev->pollfunc = kzalloc(sizeof(*indio_dev->pollfunc), GFP_KERNEL);
	if (indio_dev->pollfunc == NULL) {
		ret = -ENOMEM;
		goto error_deallocate_sw_rb;
	}
	/* Configure the polling function called on trigger interrupts */
	indio_dev->pollfunc->poll_func_main = &max1363_poll_func_th;
	indio_dev->pollfunc->private_data = indio_dev;

	/* Ring buffer functions - here trigger setup related */
	indio_dev->ring->postenable = &max1363_ring_postenable;
	indio_dev->ring->preenable = &max1363_ring_preenable;
	indio_dev->ring->predisable = &max1363_ring_predisable;
	INIT_WORK(&st->poll_work, &max1363_poll_bh_to_ring);

	/* Flag that polled ring buffering is possible */
	indio_dev->modes |= INDIO_RING_TRIGGERED;
	return 0;
error_deallocate_sw_rb:
	iio_sw_rb_free(indio_dev->ring);
error_ret:
	return ret;
}

void max1363_ring_cleanup(struct iio_dev *indio_dev)
{
	/* ensure that the trigger has been detached */
	if (indio_dev->trig) {
		iio_put_trigger(indio_dev->trig);
		iio_trigger_dettach_poll_func(indio_dev->trig,
					      indio_dev->pollfunc);
	}
	kfree(indio_dev->pollfunc);
	iio_sw_rb_free(indio_dev->ring);
}

void max1363_uninitialize_ring(struct iio_ring_buffer *ring)
{
	iio_ring_buffer_unregister(ring);
};

int max1363_initialize_ring(struct iio_ring_buffer *ring)
{
	return iio_ring_buffer_register(ring, 0);
};

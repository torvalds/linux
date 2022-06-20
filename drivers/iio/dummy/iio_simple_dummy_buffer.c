// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2011 Jonathan Cameron
 *
 * Buffer handling elements of industrial I/O reference driver.
 * Uses the kfifo buffer.
 *
 * To test without hardware use the sysfs trigger.
 */

#include <linux/kernel.h>
#include <linux/export.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/bitmap.h>

#include <linux/iio/iio.h>
#include <linux/iio/buffer.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/iio/triggered_buffer.h>

#include "iio_simple_dummy.h"

/* Some fake data */

static const s16 fakedata[] = {
	[DUMMY_INDEX_VOLTAGE_0] = 7,
	[DUMMY_INDEX_DIFFVOLTAGE_1M2] = -33,
	[DUMMY_INDEX_DIFFVOLTAGE_3M4] = -2,
	[DUMMY_INDEX_ACCELX] = 344,
};

/**
 * iio_simple_dummy_trigger_h() - the trigger handler function
 * @irq: the interrupt number
 * @p: private data - always a pointer to the poll func.
 *
 * This is the guts of buffered capture. On a trigger event occurring,
 * if the pollfunc is attached then this handler is called as a threaded
 * interrupt (and hence may sleep). It is responsible for grabbing data
 * from the device and pushing it into the associated buffer.
 */
static irqreturn_t iio_simple_dummy_trigger_h(int irq, void *p)
{
	struct iio_poll_func *pf = p;
	struct iio_dev *indio_dev = pf->indio_dev;
	int i = 0, j;
	u16 *data;

	data = kmalloc(indio_dev->scan_bytes, GFP_KERNEL);
	if (!data)
		goto done;

	/*
	 * Three common options here:
	 * hardware scans:
	 *   certain combinations of channels make up a fast read. The capture
	 *   will consist of all of them. Hence we just call the grab data
	 *   function and fill the buffer without processing.
	 * software scans:
	 *   can be considered to be random access so efficient reading is just
	 *   a case of minimal bus transactions.
	 * software culled hardware scans:
	 *   occasionally a driver may process the nearest hardware scan to avoid
	 *   storing elements that are not desired. This is the fiddliest option
	 *   by far.
	 * Here let's pretend we have random access. And the values are in the
	 * constant table fakedata.
	 */
	for_each_set_bit(j, indio_dev->active_scan_mask, indio_dev->masklength)
		data[i++] = fakedata[j];

	iio_push_to_buffers_with_timestamp(indio_dev, data,
					   iio_get_time_ns(indio_dev));

	kfree(data);

done:
	/*
	 * Tell the core we are done with this trigger and ready for the
	 * next one.
	 */
	iio_trigger_notify_done(indio_dev->trig);

	return IRQ_HANDLED;
}

static const struct iio_buffer_setup_ops iio_simple_dummy_buffer_setup_ops = {
};

int iio_simple_dummy_configure_buffer(struct iio_dev *indio_dev)
{
	return iio_triggered_buffer_setup(indio_dev, NULL,
					  iio_simple_dummy_trigger_h,
					  &iio_simple_dummy_buffer_setup_ops);
}

/**
 * iio_simple_dummy_unconfigure_buffer() - release buffer resources
 * @indio_dev: device instance state
 */
void iio_simple_dummy_unconfigure_buffer(struct iio_dev *indio_dev)
{
	iio_triggered_buffer_cleanup(indio_dev);
}

/*
 * Copyright 2010 Analog Devices Inc.
 * Copyright (C) 2008 Jonathan Cameron
 *
 * Licensed under the GPL-2 or later.
 *
 * ad7476_ring.c
 */

#include <linux/interrupt.h>
#include <linux/gpio.h>
#include <linux/workqueue.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/sysfs.h>
#include <linux/list.h>
#include <linux/spi/spi.h>

#include "../iio.h"
#include "../ring_generic.h"
#include "../ring_sw.h"
#include "../trigger.h"
#include "../sysfs.h"

#include "ad7476.h"

static IIO_SCAN_EL_C(in0, 0, 0, NULL);

static ssize_t ad7476_show_type(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	struct iio_ring_buffer *ring = dev_get_drvdata(dev);
	struct iio_dev *indio_dev = ring->indio_dev;
	struct ad7476_state *st = indio_dev->dev_data;

	return sprintf(buf, "%c%d/%d>>%d\n", st->chip_info->sign,
		       st->chip_info->bits, st->chip_info->storagebits,
		       st->chip_info->res_shift);
}
static IIO_DEVICE_ATTR(in_type, S_IRUGO, ad7476_show_type, NULL, 0);

static struct attribute *ad7476_scan_el_attrs[] = {
	&iio_scan_el_in0.dev_attr.attr,
	&iio_const_attr_in0_index.dev_attr.attr,
	&iio_dev_attr_in_type.dev_attr.attr,
	NULL,
};

static struct attribute_group ad7476_scan_el_group = {
	.name = "scan_elements",
	.attrs = ad7476_scan_el_attrs,
};

int ad7476_scan_from_ring(struct ad7476_state *st)
{
	struct iio_ring_buffer *ring = st->indio_dev->ring;
	int ret;
	u8 *ring_data;

	ring_data = kmalloc(ring->access.get_bytes_per_datum(ring), GFP_KERNEL);
	if (ring_data == NULL) {
		ret = -ENOMEM;
		goto error_ret;
	}
	ret = ring->access.read_last(ring, ring_data);
	if (ret)
		goto error_free_ring_data;

	ret = (ring_data[0] << 8) | ring_data[1];

error_free_ring_data:
	kfree(ring_data);
error_ret:
	return ret;
}

/**
 * ad7476_ring_preenable() setup the parameters of the ring before enabling
 *
 * The complex nature of the setting of the nuber of bytes per datum is due
 * to this driver currently ensuring that the timestamp is stored at an 8
 * byte boundary.
 **/
static int ad7476_ring_preenable(struct iio_dev *indio_dev)
{
	struct ad7476_state *st = indio_dev->dev_data;
	size_t d_size;

	if (indio_dev->ring->access.set_bytes_per_datum) {
		d_size = st->chip_info->storagebits / 8 + sizeof(s64);
		if (d_size % 8)
			d_size += 8 - (d_size % 8);
		indio_dev->ring->access.set_bytes_per_datum(indio_dev->ring,
							    d_size);
	}

	return 0;
}

/**
 * ad7476_poll_func_th() th of trigger launched polling to ring buffer
 *
 * As sampling only occurs on i2c comms occuring, leave timestamping until
 * then.  Some triggers will generate their own time stamp.  Currently
 * there is no way of notifying them when no one cares.
 **/
static void ad7476_poll_func_th(struct iio_dev *indio_dev, s64 time)
{
	struct ad7476_state *st = indio_dev->dev_data;

	schedule_work(&st->poll_work);
	return;
}
/**
 * ad7476_poll_bh_to_ring() bh of trigger launched polling to ring buffer
 * @work_s:	the work struct through which this was scheduled
 *
 * Currently there is no option in this driver to disable the saving of
 * timestamps within the ring.
 * I think the one copy of this at a time was to avoid problems if the
 * trigger was set far too high and the reads then locked up the computer.
 **/
static void ad7476_poll_bh_to_ring(struct work_struct *work_s)
{
	struct ad7476_state *st = container_of(work_s, struct ad7476_state,
						  poll_work);
	struct iio_dev *indio_dev = st->indio_dev;
	struct iio_sw_ring_buffer *sw_ring = iio_to_sw_ring(indio_dev->ring);
	s64 time_ns;
	__u8 *rxbuf;
	int b_sent;
	size_t d_size;

	/* Ensure the timestamp is 8 byte aligned */
	d_size = st->chip_info->storagebits / 8 + sizeof(s64);
	if (d_size % sizeof(s64))
		d_size += sizeof(s64) - (d_size % sizeof(s64));

	/* Ensure only one copy of this function running at a time */
	if (atomic_inc_return(&st->protect_ring) > 1)
		return;

	rxbuf = kzalloc(d_size,	GFP_KERNEL);
	if (rxbuf == NULL)
		return;

	b_sent = spi_read(st->spi, rxbuf, st->chip_info->storagebits / 8);
	if (b_sent < 0)
		goto done;

	time_ns = iio_get_time_ns();

	memcpy(rxbuf + d_size - sizeof(s64), &time_ns, sizeof(time_ns));

	indio_dev->ring->access.store_to(&sw_ring->buf, rxbuf, time_ns);
done:
	kfree(rxbuf);
	atomic_dec(&st->protect_ring);
}

int ad7476_register_ring_funcs_and_init(struct iio_dev *indio_dev)
{
	struct ad7476_state *st = indio_dev->dev_data;
	int ret = 0;

	indio_dev->ring = iio_sw_rb_allocate(indio_dev);
	if (!indio_dev->ring) {
		ret = -ENOMEM;
		goto error_ret;
	}
	/* Effectively select the ring buffer implementation */
	iio_ring_sw_register_funcs(&indio_dev->ring->access);
	ret = iio_alloc_pollfunc(indio_dev, NULL, &ad7476_poll_func_th);
	if (ret)
		goto error_deallocate_sw_rb;

	/* Ring buffer functions - here trigger setup related */

	indio_dev->ring->preenable = &ad7476_ring_preenable;
	indio_dev->ring->postenable = &iio_triggered_ring_postenable;
	indio_dev->ring->predisable = &iio_triggered_ring_predisable;
	indio_dev->ring->scan_el_attrs = &ad7476_scan_el_group;

	INIT_WORK(&st->poll_work, &ad7476_poll_bh_to_ring);

	/* Flag that polled ring buffering is possible */
	indio_dev->modes |= INDIO_RING_TRIGGERED;
	return 0;
error_deallocate_sw_rb:
	iio_sw_rb_free(indio_dev->ring);
error_ret:
	return ret;
}

void ad7476_ring_cleanup(struct iio_dev *indio_dev)
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

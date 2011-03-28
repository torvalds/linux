/*
 * Copyright 2011 Analog Devices Inc.
 *
 * Licensed under the GPL-2.
 *
 */

#include <linux/interrupt.h>
#include <linux/gpio.h>
#include <linux/workqueue.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/sysfs.h>

#include "../iio.h"
#include "../ring_generic.h"
#include "../ring_sw.h"
#include "../trigger.h"
#include "../sysfs.h"

#include "ad7606.h"

static IIO_SCAN_EL_C(in0, 0, 0, NULL);
static IIO_SCAN_EL_C(in1, 1, 0, NULL);
static IIO_SCAN_EL_C(in2, 2, 0, NULL);
static IIO_SCAN_EL_C(in3, 3, 0, NULL);
static IIO_SCAN_EL_C(in4, 4, 0, NULL);
static IIO_SCAN_EL_C(in5, 5, 0, NULL);
static IIO_SCAN_EL_C(in6, 6, 0, NULL);
static IIO_SCAN_EL_C(in7, 7, 0, NULL);

static IIO_SCAN_EL_TIMESTAMP(8);
static IIO_CONST_ATTR_SCAN_EL_TYPE(timestamp, s, 64, 64);

static ssize_t ad7606_show_type(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	struct iio_ring_buffer *ring = dev_get_drvdata(dev);
	struct iio_dev *indio_dev = ring->indio_dev;
	struct ad7606_state *st = indio_dev->dev_data;

	return sprintf(buf, "%c%d/%d\n", st->chip_info->sign,
		       st->chip_info->bits, st->chip_info->bits);
}
static IIO_DEVICE_ATTR(in_type, S_IRUGO, ad7606_show_type, NULL, 0);

static struct attribute *ad7606_scan_el_attrs[] = {
	&iio_scan_el_in0.dev_attr.attr,
	&iio_const_attr_in0_index.dev_attr.attr,
	&iio_scan_el_in1.dev_attr.attr,
	&iio_const_attr_in1_index.dev_attr.attr,
	&iio_scan_el_in2.dev_attr.attr,
	&iio_const_attr_in2_index.dev_attr.attr,
	&iio_scan_el_in3.dev_attr.attr,
	&iio_const_attr_in3_index.dev_attr.attr,
	&iio_scan_el_in4.dev_attr.attr,
	&iio_const_attr_in4_index.dev_attr.attr,
	&iio_scan_el_in5.dev_attr.attr,
	&iio_const_attr_in5_index.dev_attr.attr,
	&iio_scan_el_in6.dev_attr.attr,
	&iio_const_attr_in6_index.dev_attr.attr,
	&iio_scan_el_in7.dev_attr.attr,
	&iio_const_attr_in7_index.dev_attr.attr,
	&iio_const_attr_timestamp_index.dev_attr.attr,
	&iio_scan_el_timestamp.dev_attr.attr,
	&iio_const_attr_timestamp_type.dev_attr.attr,
	&iio_dev_attr_in_type.dev_attr.attr,
	NULL,
};

static mode_t ad7606_scan_el_attr_is_visible(struct kobject *kobj,
				     struct attribute *attr, int n)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	struct iio_ring_buffer *ring = dev_get_drvdata(dev);
	struct iio_dev *indio_dev = ring->indio_dev;
	struct ad7606_state *st = indio_dev->dev_data;

	mode_t mode = attr->mode;

	if (st->chip_info->num_channels <= 6 &&
		(attr == &iio_scan_el_in7.dev_attr.attr ||
		attr == &iio_const_attr_in7_index.dev_attr.attr ||
		attr == &iio_scan_el_in6.dev_attr.attr ||
		attr == &iio_const_attr_in6_index.dev_attr.attr))
		mode = 0;
	else if (st->chip_info->num_channels <= 4 &&
		(attr == &iio_scan_el_in5.dev_attr.attr ||
		attr == &iio_const_attr_in5_index.dev_attr.attr ||
		attr == &iio_scan_el_in4.dev_attr.attr ||
		attr == &iio_const_attr_in4_index.dev_attr.attr))
		mode = 0;

	return mode;
}

static struct attribute_group ad7606_scan_el_group = {
	.name = "scan_elements",
	.attrs = ad7606_scan_el_attrs,
	.is_visible = ad7606_scan_el_attr_is_visible,
};

int ad7606_scan_from_ring(struct ad7606_state *st, unsigned ch)
{
	struct iio_ring_buffer *ring = st->indio_dev->ring;
	int ret;
	u16 *ring_data;

	ring_data = kmalloc(ring->access.get_bytes_per_datum(ring), GFP_KERNEL);
	if (ring_data == NULL) {
		ret = -ENOMEM;
		goto error_ret;
	}
	ret = ring->access.read_last(ring, (u8 *) ring_data);
	if (ret)
		goto error_free_ring_data;

	ret = ring_data[ch];

error_free_ring_data:
	kfree(ring_data);
error_ret:
	return ret;
}

/**
 * ad7606_ring_preenable() setup the parameters of the ring before enabling
 *
 * The complex nature of the setting of the nuber of bytes per datum is due
 * to this driver currently ensuring that the timestamp is stored at an 8
 * byte boundary.
 **/
static int ad7606_ring_preenable(struct iio_dev *indio_dev)
{
	struct ad7606_state *st = indio_dev->dev_data;
	struct iio_ring_buffer *ring = indio_dev->ring;
	size_t d_size;

	d_size = st->chip_info->num_channels *
		 st->chip_info->bits / 8;

	if (ring->scan_timestamp) {
		d_size += sizeof(s64);

		if (d_size % sizeof(s64))
			d_size += sizeof(s64) - (d_size % sizeof(s64));
	}

	if (ring->access.set_bytes_per_datum)
		ring->access.set_bytes_per_datum(ring, d_size);

	st->d_size = d_size;

	return 0;
}

/**
 * ad7606_poll_func_th() th of trigger launched polling to ring buffer
 *
 **/
static void ad7606_poll_func_th(struct iio_dev *indio_dev, s64 time)
{
	struct ad7606_state *st = indio_dev->dev_data;
	gpio_set_value(st->pdata->gpio_convst, 1);

	return;
}
/**
 * ad7606_poll_bh_to_ring() bh of trigger launched polling to ring buffer
 * @work_s:	the work struct through which this was scheduled
 *
 * Currently there is no option in this driver to disable the saving of
 * timestamps within the ring.
 * I think the one copy of this at a time was to avoid problems if the
 * trigger was set far too high and the reads then locked up the computer.
 **/
static void ad7606_poll_bh_to_ring(struct work_struct *work_s)
{
	struct ad7606_state *st = container_of(work_s, struct ad7606_state,
						poll_work);
	struct iio_dev *indio_dev = st->indio_dev;
	struct iio_sw_ring_buffer *sw_ring = iio_to_sw_ring(indio_dev->ring);
	struct iio_ring_buffer *ring = indio_dev->ring;
	s64 time_ns;
	__u8 *buf;
	int ret;

	/* Ensure only one copy of this function running at a time */
	if (atomic_inc_return(&st->protect_ring) > 1)
		return;

	buf = kzalloc(st->d_size, GFP_KERNEL);
	if (buf == NULL)
		return;

	if (st->have_frstdata) {
		ret = st->bops->read_block(st->dev, 1, buf);
		if (ret)
			goto done;
		if (!gpio_get_value(st->pdata->gpio_frstdata)) {
			/* This should never happen. However
			 * some signal glitch caused by bad PCB desgin or
			 * electrostatic discharge, could cause an extra read
			 * or clock. This allows recovery.
			 */
			ad7606_reset(st);
			goto done;
		}
		ret = st->bops->read_block(st->dev,
			st->chip_info->num_channels - 1, buf + 2);
		if (ret)
			goto done;
	} else {
		ret = st->bops->read_block(st->dev,
			st->chip_info->num_channels, buf);
		if (ret)
			goto done;
	}

	time_ns = iio_get_time_ns();

	if (ring->scan_timestamp)
		memcpy(buf + st->d_size - sizeof(s64),
			&time_ns, sizeof(time_ns));

	ring->access.store_to(&sw_ring->buf, buf, time_ns);
done:
	gpio_set_value(st->pdata->gpio_convst, 0);
	kfree(buf);
	atomic_dec(&st->protect_ring);
}

int ad7606_register_ring_funcs_and_init(struct iio_dev *indio_dev)
{
	struct ad7606_state *st = indio_dev->dev_data;
	int ret;

	indio_dev->ring = iio_sw_rb_allocate(indio_dev);
	if (!indio_dev->ring) {
		ret = -ENOMEM;
		goto error_ret;
	}

	/* Effectively select the ring buffer implementation */
	iio_ring_sw_register_funcs(&indio_dev->ring->access);
	ret = iio_alloc_pollfunc(indio_dev, NULL, &ad7606_poll_func_th);
	if (ret)
		goto error_deallocate_sw_rb;

	/* Ring buffer functions - here trigger setup related */

	indio_dev->ring->preenable = &ad7606_ring_preenable;
	indio_dev->ring->postenable = &iio_triggered_ring_postenable;
	indio_dev->ring->predisable = &iio_triggered_ring_predisable;
	indio_dev->ring->scan_el_attrs = &ad7606_scan_el_group;
	indio_dev->ring->scan_timestamp = true ;

	INIT_WORK(&st->poll_work, &ad7606_poll_bh_to_ring);

	/* Flag that polled ring buffering is possible */
	indio_dev->modes |= INDIO_RING_TRIGGERED;
	return 0;
error_deallocate_sw_rb:
	iio_sw_rb_free(indio_dev->ring);
error_ret:
	return ret;
}

void ad7606_ring_cleanup(struct iio_dev *indio_dev)
{
	if (indio_dev->trig) {
		iio_put_trigger(indio_dev->trig);
		iio_trigger_dettach_poll_func(indio_dev->trig,
					      indio_dev->pollfunc);
	}
	kfree(indio_dev->pollfunc);
	iio_sw_rb_free(indio_dev->ring);
}

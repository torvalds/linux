/*
 * Copyright 2010 Analog Devices Inc.
 * Copyright (C) 2008 Jonathan Cameron
 *
 * Licensed under the GPL-2 or later.
 *
 * ad7887_ring.c
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

#include "ad7887.h"

static IIO_SCAN_EL_C(in0, 0, 0, NULL);
static IIO_SCAN_EL_C(in1, 1, 0, NULL);
static IIO_SCAN_EL_TIMESTAMP(2);
static IIO_CONST_ATTR_SCAN_EL_TYPE(timestamp, s, 64, 64);

static ssize_t ad7887_show_type(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	struct iio_ring_buffer *ring = dev_get_drvdata(dev);
	struct iio_dev *indio_dev = ring->indio_dev;
	struct ad7887_state *st = indio_dev->dev_data;

	return sprintf(buf, "%c%d/%d>>%d\n", st->chip_info->sign,
		       st->chip_info->bits, st->chip_info->storagebits,
		       st->chip_info->left_shift);
}
static IIO_DEVICE_ATTR(in_type, S_IRUGO, ad7887_show_type, NULL, 0);

static struct attribute *ad7887_scan_el_attrs[] = {
	&iio_scan_el_in0.dev_attr.attr,
	&iio_const_attr_in0_index.dev_attr.attr,
	&iio_scan_el_in1.dev_attr.attr,
	&iio_const_attr_in1_index.dev_attr.attr,
	&iio_const_attr_timestamp_index.dev_attr.attr,
	&iio_scan_el_timestamp.dev_attr.attr,
	&iio_const_attr_timestamp_type.dev_attr.attr,
	&iio_dev_attr_in_type.dev_attr.attr,
	NULL,
};

static mode_t ad7887_scan_el_attr_is_visible(struct kobject *kobj,
				     struct attribute *attr, int n)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	struct iio_ring_buffer *ring = dev_get_drvdata(dev);
	struct iio_dev *indio_dev = ring->indio_dev;
	struct ad7887_state *st = indio_dev->dev_data;

	mode_t mode = attr->mode;

	if ((attr == &iio_scan_el_in1.dev_attr.attr) ||
		(attr == &iio_const_attr_in1_index.dev_attr.attr))
		if (!st->en_dual)
			mode = 0;

	return mode;
}

static struct attribute_group ad7887_scan_el_group = {
	.name = "scan_elements",
	.attrs = ad7887_scan_el_attrs,
	.is_visible = ad7887_scan_el_attr_is_visible,
};

int ad7887_scan_from_ring(struct ad7887_state *st, long mask)
{
	struct iio_ring_buffer *ring = st->indio_dev->ring;
	int count = 0, ret;
	u16 *ring_data;

	if (!(ring->scan_mask & mask)) {
		ret = -EBUSY;
		goto error_ret;
	}

	ring_data = kmalloc(ring->access.get_bytes_per_datum(ring), GFP_KERNEL);
	if (ring_data == NULL) {
		ret = -ENOMEM;
		goto error_ret;
	}
	ret = ring->access.read_last(ring, (u8 *) ring_data);
	if (ret)
		goto error_free_ring_data;

	/* for single channel scan the result is stored with zero offset */
	if ((ring->scan_mask == ((1 << 1) | (1 << 0))) && (mask == (1 << 1)))
		count = 1;

	ret = be16_to_cpu(ring_data[count]);

error_free_ring_data:
	kfree(ring_data);
error_ret:
	return ret;
}

/**
 * ad7887_ring_preenable() setup the parameters of the ring before enabling
 *
 * The complex nature of the setting of the nuber of bytes per datum is due
 * to this driver currently ensuring that the timestamp is stored at an 8
 * byte boundary.
 **/
static int ad7887_ring_preenable(struct iio_dev *indio_dev)
{
	struct ad7887_state *st = indio_dev->dev_data;
	struct iio_ring_buffer *ring = indio_dev->ring;

	st->d_size = ring->scan_count * st->chip_info->storagebits / 8;

	if (ring->scan_timestamp) {
		st->d_size += sizeof(s64);

		if (st->d_size % sizeof(s64))
			st->d_size += sizeof(s64) - (st->d_size % sizeof(s64));
	}

	if (indio_dev->ring->access.set_bytes_per_datum)
		indio_dev->ring->access.set_bytes_per_datum(indio_dev->ring,
							    st->d_size);

	switch (ring->scan_mask) {
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
	struct ad7887_state *st = indio_dev->dev_data;

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
	struct iio_dev *indio_dev = pf->private_data;
	struct ad7887_state *st = iio_dev_get_devdata(indio_dev);
	struct iio_ring_buffer *ring = indio_dev->ring;
	struct iio_sw_ring_buffer *sw_ring = iio_to_sw_ring(indio_dev->ring);
	s64 time_ns;
	__u8 *buf;
	int b_sent;

	unsigned int bytes = ring->scan_count * st->chip_info->storagebits / 8;

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

	indio_dev->ring->access.store_to(&sw_ring->buf, buf, time_ns);
done:
	kfree(buf);

	return IRQ_HANDLED;
}

int ad7887_register_ring_funcs_and_init(struct iio_dev *indio_dev)
{
	int ret;

	indio_dev->ring = iio_sw_rb_allocate(indio_dev);
	if (!indio_dev->ring) {
		ret = -ENOMEM;
		goto error_ret;
	}
	/* Effectively select the ring buffer implementation */
	iio_ring_sw_register_funcs(&indio_dev->ring->access);
	indio_dev->pollfunc = kzalloc(sizeof(*indio_dev->pollfunc), GFP_KERNEL);
	if (indio_dev->pollfunc == NULL) {
		ret = -ENOMEM;
		goto error_deallocate_sw_rb;
	}

	indio_dev->pollfunc->private_data = indio_dev;
	indio_dev->pollfunc->h = &iio_pollfunc_store_time;
	indio_dev->pollfunc->thread = &ad7887_trigger_handler;
	indio_dev->pollfunc->type = IRQF_ONESHOT;
	indio_dev->pollfunc->name =
		kasprintf(GFP_KERNEL, "ad7887_consumer%d", indio_dev->id);
	if (indio_dev->pollfunc->name == NULL) {
		ret = -ENOMEM;
		goto error_free_pollfunc;
	}
	/* Ring buffer functions - here trigger setup related */

	indio_dev->ring->preenable = &ad7887_ring_preenable;
	indio_dev->ring->postenable = &iio_triggered_ring_postenable;
	indio_dev->ring->predisable = &iio_triggered_ring_predisable;
	indio_dev->ring->postdisable = &ad7887_ring_postdisable;
	indio_dev->ring->scan_el_attrs = &ad7887_scan_el_group;
	indio_dev->ring->scan_timestamp = true;

	/* Flag that polled ring buffering is possible */
	indio_dev->modes |= INDIO_RING_TRIGGERED;
	return 0;
error_free_pollfunc:
	kfree(indio_dev->pollfunc);
error_deallocate_sw_rb:
	iio_sw_rb_free(indio_dev->ring);
error_ret:
	return ret;
}

void ad7887_ring_cleanup(struct iio_dev *indio_dev)
{
	/* ensure that the trigger has been detached */
	if (indio_dev->trig) {
		iio_put_trigger(indio_dev->trig);
		iio_trigger_dettach_poll_func(indio_dev->trig,
					      indio_dev->pollfunc);
	}
	kfree(indio_dev->pollfunc->name);
	kfree(indio_dev->pollfunc);
	iio_sw_rb_free(indio_dev->ring);
}

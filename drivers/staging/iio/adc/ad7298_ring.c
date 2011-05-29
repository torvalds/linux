/*
 * AD7298 SPI ADC driver
 *
 * Copyright 2011 Analog Devices Inc.
 *
 * Licensed under the GPL-2.
 */

#include <linux/interrupt.h>
#include <linux/workqueue.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/sysfs.h>
#include <linux/spi/spi.h>

#include "../iio.h"
#include "../ring_generic.h"
#include "../ring_sw.h"
#include "../trigger.h"
#include "../sysfs.h"

#include "ad7298.h"

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

static IIO_CONST_ATTR(in_type, "u12/16") ;

static struct attribute *ad7298_scan_el_attrs[] = {
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
	&iio_const_attr_in_type.dev_attr.attr,
	NULL,
};

static struct attribute_group ad7298_scan_el_group = {
	.name = "scan_elements",
	.attrs = ad7298_scan_el_attrs,
};

int ad7298_scan_from_ring(struct ad7298_state *st, long ch)
{
	struct iio_ring_buffer *ring = st->indio_dev->ring;
	int ret;
	u16 *ring_data;

	if (!(ring->scan_mask & (1 << ch))) {
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

	ret = be16_to_cpu(ring_data[ch]);

error_free_ring_data:
	kfree(ring_data);
error_ret:
	return ret;
}

/**
 * ad7298_ring_preenable() setup the parameters of the ring before enabling
 *
 * The complex nature of the setting of the number of bytes per datum is due
 * to this driver currently ensuring that the timestamp is stored at an 8
 * byte boundary.
 **/
static int ad7298_ring_preenable(struct iio_dev *indio_dev)
{
	struct ad7298_state *st = indio_dev->dev_data;
	struct iio_ring_buffer *ring = indio_dev->ring;
	size_t d_size;
	int i, m;
	unsigned short command;

	d_size = ring->scan_count * (AD7298_STORAGE_BITS / 8);

	if (ring->scan_timestamp) {
		d_size += sizeof(s64);

		if (d_size % sizeof(s64))
			d_size += sizeof(s64) - (d_size % sizeof(s64));
	}

	if (ring->access.set_bytes_per_datum)
		ring->access.set_bytes_per_datum(ring, d_size);

	st->d_size = d_size;

	command = AD7298_WRITE | st->ext_ref;

	for (i = 0, m = AD7298_CH(0); i < AD7298_MAX_CHAN; i++, m >>= 1)
		if (ring->scan_mask & (1 << i))
			command |= m;

	st->tx_buf[0] = cpu_to_be16(command);

	/* build spi ring message */
	st->ring_xfer[0].tx_buf = &st->tx_buf[0];
	st->ring_xfer[0].len = 2;
	st->ring_xfer[0].cs_change = 1;
	st->ring_xfer[1].tx_buf = &st->tx_buf[1];
	st->ring_xfer[1].len = 2;
	st->ring_xfer[1].cs_change = 1;

	spi_message_init(&st->ring_msg);
	spi_message_add_tail(&st->ring_xfer[0], &st->ring_msg);
	spi_message_add_tail(&st->ring_xfer[1], &st->ring_msg);

	for (i = 0; i < ring->scan_count; i++) {
		st->ring_xfer[i + 2].rx_buf = &st->rx_buf[i];
		st->ring_xfer[i + 2].len = 2;
		st->ring_xfer[i + 2].cs_change = 1;
		spi_message_add_tail(&st->ring_xfer[i + 2], &st->ring_msg);
	}
	/* make sure last transfer cs_change is not set */
	st->ring_xfer[i + 1].cs_change = 0;

	return 0;
}

/**
 * ad7298_poll_func_th() th of trigger launched polling to ring buffer
 *
 * As sampling only occurs on spi comms occurring, leave timestamping until
 * then.  Some triggers will generate their own time stamp.  Currently
 * there is no way of notifying them when no one cares.
 **/
static void ad7298_poll_func_th(struct iio_dev *indio_dev, s64 time)
{
	struct ad7298_state *st = indio_dev->dev_data;

	schedule_work(&st->poll_work);
	return;
}

/**
 * ad7298_poll_bh_to_ring() bh of trigger launched polling to ring buffer
 * @work_s:	the work struct through which this was scheduled
 *
 * Currently there is no option in this driver to disable the saving of
 * timestamps within the ring.
 * I think the one copy of this at a time was to avoid problems if the
 * trigger was set far too high and the reads then locked up the computer.
 **/
static void ad7298_poll_bh_to_ring(struct work_struct *work_s)
{
	struct ad7298_state *st = container_of(work_s, struct ad7298_state,
						  poll_work);
	struct iio_dev *indio_dev = st->indio_dev;
	struct iio_sw_ring_buffer *sw_ring = iio_to_sw_ring(indio_dev->ring);
	struct iio_ring_buffer *ring = indio_dev->ring;
	s64 time_ns;
	__u16 buf[16];
	int b_sent, i;

	/* Ensure only one copy of this function running at a time */
	if (atomic_inc_return(&st->protect_ring) > 1)
		return;

	b_sent = spi_sync(st->spi, &st->ring_msg);
	if (b_sent)
		goto done;

	if (ring->scan_timestamp) {
		time_ns = iio_get_time_ns();
		memcpy((u8 *)buf + st->d_size - sizeof(s64),
			&time_ns, sizeof(time_ns));
	}

	for (i = 0; i < ring->scan_count; i++)
		buf[i] = be16_to_cpu(st->rx_buf[i]);

	indio_dev->ring->access.store_to(&sw_ring->buf, (u8 *)buf, time_ns);
done:
	atomic_dec(&st->protect_ring);
}

int ad7298_register_ring_funcs_and_init(struct iio_dev *indio_dev)
{
	struct ad7298_state *st = indio_dev->dev_data;
	int ret;

	indio_dev->ring = iio_sw_rb_allocate(indio_dev);
	if (!indio_dev->ring) {
		ret = -ENOMEM;
		goto error_ret;
	}
	/* Effectively select the ring buffer implementation */
	iio_ring_sw_register_funcs(&indio_dev->ring->access);
	ret = iio_alloc_pollfunc(indio_dev, NULL, &ad7298_poll_func_th);
	if (ret)
		goto error_deallocate_sw_rb;

	/* Ring buffer functions - here trigger setup related */

	indio_dev->ring->preenable = &ad7298_ring_preenable;
	indio_dev->ring->postenable = &iio_triggered_ring_postenable;
	indio_dev->ring->predisable = &iio_triggered_ring_predisable;
	indio_dev->ring->scan_el_attrs = &ad7298_scan_el_group;
	indio_dev->ring->scan_timestamp = true;

	INIT_WORK(&st->poll_work, &ad7298_poll_bh_to_ring);

	/* Flag that polled ring buffering is possible */
	indio_dev->modes |= INDIO_RING_TRIGGERED;
	return 0;
error_deallocate_sw_rb:
	iio_sw_rb_free(indio_dev->ring);
error_ret:
	return ret;
}

void ad7298_ring_cleanup(struct iio_dev *indio_dev)
{
	if (indio_dev->trig) {
		iio_put_trigger(indio_dev->trig);
		iio_trigger_dettach_poll_func(indio_dev->trig,
					      indio_dev->pollfunc);
	}
	kfree(indio_dev->pollfunc);
	iio_sw_rb_free(indio_dev->ring);
}

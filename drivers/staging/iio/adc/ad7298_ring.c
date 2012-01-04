/*
 * AD7298 SPI ADC driver
 *
 * Copyright 2011 Analog Devices Inc.
 *
 * Licensed under the GPL-2.
 */

#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/spi/spi.h>

#include "../iio.h"
#include "../buffer_generic.h"
#include "../ring_sw.h"
#include "../trigger_consumer.h"

#include "ad7298.h"

int ad7298_scan_from_ring(struct iio_dev *indio_dev, long ch)
{
	struct iio_buffer *ring = indio_dev->buffer;
	int ret;
	u16 *ring_data;

	if (!(test_bit(ch, ring->scan_mask))) {
		ret = -EBUSY;
		goto error_ret;
	}

	ring_data = kmalloc(ring->access->get_bytes_per_datum(ring),
			    GFP_KERNEL);
	if (ring_data == NULL) {
		ret = -ENOMEM;
		goto error_ret;
	}
	ret = ring->access->read_last(ring, (u8 *) ring_data);
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
	struct ad7298_state *st = iio_priv(indio_dev);
	struct iio_buffer *ring = indio_dev->buffer;
	size_t d_size;
	int i, m;
	unsigned short command;

	d_size = ring->scan_count * (AD7298_STORAGE_BITS / 8);

	if (ring->scan_timestamp) {
		d_size += sizeof(s64);

		if (d_size % sizeof(s64))
			d_size += sizeof(s64) - (d_size % sizeof(s64));
	}

	if (ring->access->set_bytes_per_datum)
		ring->access->set_bytes_per_datum(ring, d_size);

	st->d_size = d_size;

	command = AD7298_WRITE | st->ext_ref;

	for (i = 0, m = AD7298_CH(0); i < AD7298_MAX_CHAN; i++, m >>= 1)
		if (test_bit(i, ring->scan_mask))
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
 * ad7298_trigger_handler() bh of trigger launched polling to ring buffer
 *
 * Currently there is no option in this driver to disable the saving of
 * timestamps within the ring.
 **/
static irqreturn_t ad7298_trigger_handler(int irq, void *p)
{
	struct iio_poll_func *pf = p;
	struct iio_dev *indio_dev = pf->indio_dev;
	struct ad7298_state *st = iio_priv(indio_dev);
	struct iio_buffer *ring = indio_dev->buffer;
	s64 time_ns;
	__u16 buf[16];
	int b_sent, i;

	b_sent = spi_sync(st->spi, &st->ring_msg);
	if (b_sent)
		return b_sent;

	if (ring->scan_timestamp) {
		time_ns = iio_get_time_ns();
		memcpy((u8 *)buf + st->d_size - sizeof(s64),
			&time_ns, sizeof(time_ns));
	}

	for (i = 0; i < ring->scan_count; i++)
		buf[i] = be16_to_cpu(st->rx_buf[i]);

	indio_dev->buffer->access->store_to(ring, (u8 *)buf, time_ns);
	iio_trigger_notify_done(indio_dev->trig);

	return IRQ_HANDLED;
}

static const struct iio_buffer_setup_ops ad7298_ring_setup_ops = {
	.preenable = &ad7298_ring_preenable,
	.postenable = &iio_triggered_buffer_postenable,
	.predisable = &iio_triggered_buffer_predisable,
};

int ad7298_register_ring_funcs_and_init(struct iio_dev *indio_dev)
{
	int ret;

	indio_dev->buffer = iio_sw_rb_allocate(indio_dev);
	if (!indio_dev->buffer) {
		ret = -ENOMEM;
		goto error_ret;
	}
	/* Effectively select the ring buffer implementation */
	indio_dev->buffer->access = &ring_sw_access_funcs;

	indio_dev->pollfunc = iio_alloc_pollfunc(NULL,
						 &ad7298_trigger_handler,
						 IRQF_ONESHOT,
						 indio_dev,
						 "ad7298_consumer%d",
						 indio_dev->id);

	if (indio_dev->pollfunc == NULL) {
		ret = -ENOMEM;
		goto error_deallocate_sw_rb;
	}

	/* Ring buffer functions - here trigger setup related */
	indio_dev->buffer->setup_ops = &ad7298_ring_setup_ops;
	indio_dev->buffer->scan_timestamp = true;

	/* Flag that polled ring buffering is possible */
	indio_dev->modes |= INDIO_BUFFER_TRIGGERED;
	return 0;

error_deallocate_sw_rb:
	iio_sw_rb_free(indio_dev->buffer);
error_ret:
	return ret;
}

void ad7298_ring_cleanup(struct iio_dev *indio_dev)
{
	iio_dealloc_pollfunc(indio_dev->pollfunc);
	iio_sw_rb_free(indio_dev->buffer);
}

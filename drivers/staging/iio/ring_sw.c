/* The industrial I/O simple minimally locked ring buffer.
 *
 * Copyright (c) 2008 Jonathan Cameron
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */

#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/workqueue.h>
#include <linux/sched.h>
#include <linux/poll.h>
#include "ring_sw.h"
#include "trigger.h"

/**
 * struct iio_sw_ring_buffer - software ring buffer
 * @buf:		generic ring buffer elements
 * @data:		the ring buffer memory
 * @read_p:		read pointer (oldest available)
 * @write_p:		write pointer
 * @half_p:		half buffer length behind write_p (event generation)
 * @update_needed:	flag to indicated change in size requested
 *
 * Note that the first element of all ring buffers must be a
 * struct iio_buffer.
**/
struct iio_sw_ring_buffer {
	struct iio_buffer  buf;
	unsigned char		*data;
	unsigned char		*read_p;
	unsigned char		*write_p;
	/* used to act as a point at which to signal an event */
	unsigned char		*half_p;
	int			update_needed;
};

#define iio_to_sw_ring(r) container_of(r, struct iio_sw_ring_buffer, buf)

static inline int __iio_allocate_sw_ring_buffer(struct iio_sw_ring_buffer *ring,
						int bytes_per_datum, int length)
{
	if ((length == 0) || (bytes_per_datum == 0))
		return -EINVAL;
	__iio_update_buffer(&ring->buf, bytes_per_datum, length);
	ring->data = kmalloc(length*ring->buf.bytes_per_datum, GFP_ATOMIC);
	ring->read_p = NULL;
	ring->write_p = NULL;
	ring->half_p = NULL;
	return ring->data ? 0 : -ENOMEM;
}

static inline void __iio_free_sw_ring_buffer(struct iio_sw_ring_buffer *ring)
{
	kfree(ring->data);
}

/* Ring buffer related functionality */
/* Store to ring is typically called in the bh of a data ready interrupt handler
 * in the device driver */
/* Lock always held if their is a chance this may be called */
/* Only one of these per ring may run concurrently - enforced by drivers */
static int iio_store_to_sw_ring(struct iio_sw_ring_buffer *ring,
				unsigned char *data, s64 timestamp)
{
	int ret = 0;
	unsigned char *temp_ptr, *change_test_ptr;

	/* initial store */
	if (unlikely(ring->write_p == NULL)) {
		ring->write_p = ring->data;
		/* Doesn't actually matter if this is out of the set
		 * as long as the read pointer is valid before this
		 * passes it - guaranteed as set later in this function.
		 */
		ring->half_p = ring->data - ring->buf.length*ring->buf.bytes_per_datum/2;
	}
	/* Copy data to where ever the current write pointer says */
	memcpy(ring->write_p, data, ring->buf.bytes_per_datum);
	barrier();
	/* Update the pointer used to get most recent value.
	 * Always valid as either points to latest or second latest value.
	 * Before this runs it is null and read attempts fail with -EAGAIN.
	 */
	barrier();
	/* temp_ptr used to ensure we never have an invalid pointer
	 * it may be slightly lagging, but never invalid
	 */
	temp_ptr = ring->write_p + ring->buf.bytes_per_datum;
	/* End of ring, back to the beginning */
	if (temp_ptr == ring->data + ring->buf.length*ring->buf.bytes_per_datum)
		temp_ptr = ring->data;
	/* Update the write pointer
	 * always valid as long as this is the only function able to write.
	 * Care needed with smp systems to ensure more than one ring fill
	 * is never scheduled.
	 */
	ring->write_p = temp_ptr;

	if (ring->read_p == NULL)
		ring->read_p = ring->data;
	/* Buffer full - move the read pointer and create / escalate
	 * ring event */
	/* Tricky case - if the read pointer moves before we adjust it.
	 * Handle by not pushing if it has moved - may result in occasional
	 * unnecessary buffer full events when it wasn't quite true.
	 */
	else if (ring->write_p == ring->read_p) {
		change_test_ptr = ring->read_p;
		temp_ptr = change_test_ptr + ring->buf.bytes_per_datum;
		if (temp_ptr
		    == ring->data + ring->buf.length*ring->buf.bytes_per_datum) {
			temp_ptr = ring->data;
		}
		/* We are moving pointer on one because the ring is full.  Any
		 * change to the read pointer will be this or greater.
		 */
		if (change_test_ptr == ring->read_p)
			ring->read_p = temp_ptr;
	}
	/* investigate if our event barrier has been passed */
	/* There are definite 'issues' with this and chances of
	 * simultaneous read */
	/* Also need to use loop count to ensure this only happens once */
	ring->half_p += ring->buf.bytes_per_datum;
	if (ring->half_p == ring->data + ring->buf.length*ring->buf.bytes_per_datum)
		ring->half_p = ring->data;
	if (ring->half_p == ring->read_p) {
		ring->buf.stufftoread = true;
		wake_up_interruptible(&ring->buf.pollq);
	}
	return ret;
}

static int iio_read_first_n_sw_rb(struct iio_buffer *r,
				  size_t n, char __user *buf)
{
	struct iio_sw_ring_buffer *ring = iio_to_sw_ring(r);

	u8 *initial_read_p, *initial_write_p, *current_read_p, *end_read_p;
	u8 *data;
	int ret, max_copied, bytes_to_rip, dead_offset;
	size_t data_available, buffer_size;

	/* A userspace program has probably made an error if it tries to
	 *  read something that is not a whole number of bpds.
	 * Return an error.
	 */
	if (n % ring->buf.bytes_per_datum) {
		ret = -EINVAL;
		printk(KERN_INFO "Ring buffer read request not whole number of"
		       "samples: Request bytes %zd, Current bytes per datum %d\n",
		       n, ring->buf.bytes_per_datum);
		goto error_ret;
	}

	buffer_size = ring->buf.bytes_per_datum*ring->buf.length;

	/* Limit size to whole of ring buffer */
	bytes_to_rip = min_t(size_t, buffer_size, n);

	data = kmalloc(bytes_to_rip, GFP_KERNEL);
	if (data == NULL) {
		ret = -ENOMEM;
		goto error_ret;
	}

	/* build local copy */
	initial_read_p = ring->read_p;
	if (unlikely(initial_read_p == NULL)) { /* No data here as yet */
		ret = 0;
		goto error_free_data_cpy;
	}

	initial_write_p = ring->write_p;

	/* Need a consistent pair */
	while ((initial_read_p != ring->read_p)
	       || (initial_write_p != ring->write_p)) {
		initial_read_p = ring->read_p;
		initial_write_p = ring->write_p;
	}
	if (initial_write_p == initial_read_p) {
		/* No new data available.*/
		ret = 0;
		goto error_free_data_cpy;
	}

	if (initial_write_p >= initial_read_p)
		data_available = initial_write_p - initial_read_p;
	else
		data_available = buffer_size - (initial_read_p - initial_write_p);

	if (data_available < bytes_to_rip)
		bytes_to_rip = data_available;

	if (initial_read_p + bytes_to_rip >= ring->data + buffer_size) {
		max_copied = ring->data + buffer_size - initial_read_p;
		memcpy(data, initial_read_p, max_copied);
		memcpy(data + max_copied, ring->data, bytes_to_rip - max_copied);
		end_read_p = ring->data + bytes_to_rip - max_copied;
	} else {
		memcpy(data, initial_read_p, bytes_to_rip);
		end_read_p = initial_read_p + bytes_to_rip;
	}

	/* Now to verify which section was cleanly copied - i.e. how far
	 * read pointer has been pushed */
	current_read_p = ring->read_p;

	if (initial_read_p <= current_read_p)
		dead_offset = current_read_p - initial_read_p;
	else
		dead_offset = buffer_size - (initial_read_p - current_read_p);

	/* possible issue if the initial write has been lapped or indeed
	 * the point we were reading to has been passed */
	/* No valid data read.
	 * In this case the read pointer is already correct having been
	 * pushed further than we would look. */
	if (bytes_to_rip - dead_offset < 0) {
		ret = 0;
		goto error_free_data_cpy;
	}

	/* setup the next read position */
	/* Beware, this may fail due to concurrency fun and games.
	 *  Possible that sufficient fill commands have run to push the read
	 * pointer past where we would be after the rip. If this occurs, leave
	 * it be.
	 */
	/* Tricky - deal with loops */

	while (ring->read_p != end_read_p)
		ring->read_p = end_read_p;

	ret = bytes_to_rip - dead_offset;

	if (copy_to_user(buf, data + dead_offset, ret))  {
		ret =  -EFAULT;
		goto error_free_data_cpy;
	}

	if (bytes_to_rip >= ring->buf.length*ring->buf.bytes_per_datum/2)
		ring->buf.stufftoread = 0;

error_free_data_cpy:
	kfree(data);
error_ret:

	return ret;
}

static int iio_store_to_sw_rb(struct iio_buffer *r,
			      u8 *data,
			      s64 timestamp)
{
	struct iio_sw_ring_buffer *ring = iio_to_sw_ring(r);
	return iio_store_to_sw_ring(ring, data, timestamp);
}

static int iio_request_update_sw_rb(struct iio_buffer *r)
{
	int ret = 0;
	struct iio_sw_ring_buffer *ring = iio_to_sw_ring(r);

	r->stufftoread = false;
	if (!ring->update_needed)
		goto error_ret;
	__iio_free_sw_ring_buffer(ring);
	ret = __iio_allocate_sw_ring_buffer(ring, ring->buf.bytes_per_datum,
					    ring->buf.length);
error_ret:
	return ret;
}

static int iio_get_bytes_per_datum_sw_rb(struct iio_buffer *r)
{
	struct iio_sw_ring_buffer *ring = iio_to_sw_ring(r);
	return ring->buf.bytes_per_datum;
}

static int iio_mark_update_needed_sw_rb(struct iio_buffer *r)
{
	struct iio_sw_ring_buffer *ring = iio_to_sw_ring(r);
	ring->update_needed = true;
	return 0;
}

static int iio_set_bytes_per_datum_sw_rb(struct iio_buffer *r, size_t bpd)
{
	if (r->bytes_per_datum != bpd) {
		r->bytes_per_datum = bpd;
		iio_mark_update_needed_sw_rb(r);
	}
	return 0;
}

static int iio_get_length_sw_rb(struct iio_buffer *r)
{
	return r->length;
}

static int iio_set_length_sw_rb(struct iio_buffer *r, int length)
{
	if (r->length != length) {
		r->length = length;
		iio_mark_update_needed_sw_rb(r);
	}
	return 0;
}

static IIO_BUFFER_ENABLE_ATTR;
static IIO_BUFFER_LENGTH_ATTR;

/* Standard set of ring buffer attributes */
static struct attribute *iio_ring_attributes[] = {
	&dev_attr_length.attr,
	&dev_attr_enable.attr,
	NULL,
};

static struct attribute_group iio_ring_attribute_group = {
	.attrs = iio_ring_attributes,
	.name = "buffer",
};

struct iio_buffer *iio_sw_rb_allocate(struct iio_dev *indio_dev)
{
	struct iio_buffer *buf;
	struct iio_sw_ring_buffer *ring;

	ring = kzalloc(sizeof *ring, GFP_KERNEL);
	if (!ring)
		return NULL;
	ring->update_needed = true;
	buf = &ring->buf;
	iio_buffer_init(buf);
	buf->attrs = &iio_ring_attribute_group;

	return buf;
}
EXPORT_SYMBOL(iio_sw_rb_allocate);

void iio_sw_rb_free(struct iio_buffer *r)
{
	kfree(iio_to_sw_ring(r));
}
EXPORT_SYMBOL(iio_sw_rb_free);

const struct iio_buffer_access_funcs ring_sw_access_funcs = {
	.store_to = &iio_store_to_sw_rb,
	.read_first_n = &iio_read_first_n_sw_rb,
	.request_update = &iio_request_update_sw_rb,
	.get_bytes_per_datum = &iio_get_bytes_per_datum_sw_rb,
	.set_bytes_per_datum = &iio_set_bytes_per_datum_sw_rb,
	.get_length = &iio_get_length_sw_rb,
	.set_length = &iio_set_length_sw_rb,
};
EXPORT_SYMBOL(ring_sw_access_funcs);

MODULE_DESCRIPTION("Industrialio I/O software ring buffer");
MODULE_LICENSE("GPL");

// SPDX-License-Identifier: GPL-2.0-only
/* The industrial I/O core
 *
 * Copyright (c) 2008 Jonathan Cameron
 *
 * Handling of buffer allocation / resizing.
 *
 * Things to look at here.
 * - Better memory allocation techniques?
 * - Alternative access techniques?
 */
#include <linux/atomic.h>
#include <linux/anon_inodes.h>
#include <linux/cleanup.h>
#include <linux/kernel.h>
#include <linux/export.h>
#include <linux/device.h>
#include <linux/dma-buf.h>
#include <linux/dma-fence.h>
#include <linux/dma-resv.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/poll.h>
#include <linux/sched/signal.h>

#include <linux/iio/iio.h>
#include <linux/iio/iio-opaque.h>
#include "iio_core.h"
#include "iio_core_trigger.h"
#include <linux/iio/sysfs.h>
#include <linux/iio/buffer.h>
#include <linux/iio/buffer_impl.h>

#define DMABUF_ENQUEUE_TIMEOUT_MS 5000

MODULE_IMPORT_NS("DMA_BUF");

struct iio_dmabuf_priv {
	struct list_head entry;
	struct kref ref;

	struct iio_buffer *buffer;
	struct iio_dma_buffer_block *block;

	u64 context;

	/* Spinlock used for locking the dma_fence */
	spinlock_t lock;

	struct dma_buf_attachment *attach;
	struct sg_table *sgt;
	enum dma_data_direction dir;
	atomic_t seqno;
};

struct iio_dma_fence {
	struct dma_fence base;
	struct iio_dmabuf_priv *priv;
	struct work_struct work;
};

static const char * const iio_endian_prefix[] = {
	[IIO_BE] = "be",
	[IIO_LE] = "le",
};

static bool iio_buffer_is_active(struct iio_buffer *buf)
{
	return !list_empty(&buf->buffer_list);
}

static size_t iio_buffer_data_available(struct iio_buffer *buf)
{
	return buf->access->data_available(buf);
}

static int iio_buffer_flush_hwfifo(struct iio_dev *indio_dev,
				   struct iio_buffer *buf, size_t required)
{
	if (!indio_dev->info->hwfifo_flush_to_buffer)
		return -ENODEV;

	return indio_dev->info->hwfifo_flush_to_buffer(indio_dev, required);
}

static bool iio_buffer_ready(struct iio_dev *indio_dev, struct iio_buffer *buf,
			     size_t to_wait, int to_flush)
{
	size_t avail;
	int flushed = 0;

	/* wakeup if the device was unregistered */
	if (!indio_dev->info)
		return true;

	/* drain the buffer if it was disabled */
	if (!iio_buffer_is_active(buf)) {
		to_wait = min_t(size_t, to_wait, 1);
		to_flush = 0;
	}

	avail = iio_buffer_data_available(buf);

	if (avail >= to_wait) {
		/* force a flush for non-blocking reads */
		if (!to_wait && avail < to_flush)
			iio_buffer_flush_hwfifo(indio_dev, buf,
						to_flush - avail);
		return true;
	}

	if (to_flush)
		flushed = iio_buffer_flush_hwfifo(indio_dev, buf,
						  to_wait - avail);
	if (flushed <= 0)
		return false;

	if (avail + flushed >= to_wait)
		return true;

	return false;
}

/**
 * iio_buffer_read() - chrdev read for buffer access
 * @filp:	File structure pointer for the char device
 * @buf:	Destination buffer for iio buffer read
 * @n:		First n bytes to read
 * @f_ps:	Long offset provided by the user as a seek position
 *
 * This function relies on all buffer implementations having an
 * iio_buffer as their first element.
 *
 * Return: negative values corresponding to error codes or ret != 0
 *	   for ending the reading activity
 **/
static ssize_t iio_buffer_read(struct file *filp, char __user *buf,
			       size_t n, loff_t *f_ps)
{
	struct iio_dev_buffer_pair *ib = filp->private_data;
	struct iio_buffer *rb = ib->buffer;
	struct iio_dev *indio_dev = ib->indio_dev;
	DEFINE_WAIT_FUNC(wait, woken_wake_function);
	size_t datum_size;
	size_t to_wait;
	int ret = 0;

	if (!indio_dev->info)
		return -ENODEV;

	if (!rb || !rb->access->read)
		return -EINVAL;

	if (rb->direction != IIO_BUFFER_DIRECTION_IN)
		return -EPERM;

	datum_size = rb->bytes_per_datum;

	/*
	 * If datum_size is 0 there will never be anything to read from the
	 * buffer, so signal end of file now.
	 */
	if (!datum_size)
		return 0;

	if (filp->f_flags & O_NONBLOCK)
		to_wait = 0;
	else
		to_wait = min_t(size_t, n / datum_size, rb->watermark);

	add_wait_queue(&rb->pollq, &wait);
	do {
		if (!indio_dev->info) {
			ret = -ENODEV;
			break;
		}

		if (!iio_buffer_ready(indio_dev, rb, to_wait, n / datum_size)) {
			if (signal_pending(current)) {
				ret = -ERESTARTSYS;
				break;
			}

			wait_woken(&wait, TASK_INTERRUPTIBLE,
				   MAX_SCHEDULE_TIMEOUT);
			continue;
		}

		ret = rb->access->read(rb, n, buf);
		if (ret == 0 && (filp->f_flags & O_NONBLOCK))
			ret = -EAGAIN;
	} while (ret == 0);
	remove_wait_queue(&rb->pollq, &wait);

	return ret;
}

static size_t iio_buffer_space_available(struct iio_buffer *buf)
{
	if (buf->access->space_available)
		return buf->access->space_available(buf);

	return SIZE_MAX;
}

static ssize_t iio_buffer_write(struct file *filp, const char __user *buf,
				size_t n, loff_t *f_ps)
{
	struct iio_dev_buffer_pair *ib = filp->private_data;
	struct iio_buffer *rb = ib->buffer;
	struct iio_dev *indio_dev = ib->indio_dev;
	DEFINE_WAIT_FUNC(wait, woken_wake_function);
	int ret = 0;
	size_t written;

	if (!indio_dev->info)
		return -ENODEV;

	if (!rb || !rb->access->write)
		return -EINVAL;

	if (rb->direction != IIO_BUFFER_DIRECTION_OUT)
		return -EPERM;

	written = 0;
	add_wait_queue(&rb->pollq, &wait);
	do {
		if (!indio_dev->info)
			return -ENODEV;

		if (!iio_buffer_space_available(rb)) {
			if (signal_pending(current)) {
				ret = -ERESTARTSYS;
				break;
			}

			if (filp->f_flags & O_NONBLOCK) {
				if (!written)
					ret = -EAGAIN;
				break;
			}

			wait_woken(&wait, TASK_INTERRUPTIBLE,
				   MAX_SCHEDULE_TIMEOUT);
			continue;
		}

		ret = rb->access->write(rb, n - written, buf + written);
		if (ret < 0)
			break;

		written += ret;

	} while (written != n);
	remove_wait_queue(&rb->pollq, &wait);

	return ret < 0 ? ret : written;
}

/**
 * iio_buffer_poll() - poll the buffer to find out if it has data
 * @filp:	File structure pointer for device access
 * @wait:	Poll table structure pointer for which the driver adds
 *		a wait queue
 *
 * Return: (EPOLLIN | EPOLLRDNORM) if data is available for reading
 *	   or 0 for other cases
 */
static __poll_t iio_buffer_poll(struct file *filp,
				struct poll_table_struct *wait)
{
	struct iio_dev_buffer_pair *ib = filp->private_data;
	struct iio_buffer *rb = ib->buffer;
	struct iio_dev *indio_dev = ib->indio_dev;

	if (!indio_dev->info || !rb)
		return 0;

	poll_wait(filp, &rb->pollq, wait);

	switch (rb->direction) {
	case IIO_BUFFER_DIRECTION_IN:
		if (iio_buffer_ready(indio_dev, rb, rb->watermark, 0))
			return EPOLLIN | EPOLLRDNORM;
		break;
	case IIO_BUFFER_DIRECTION_OUT:
		if (iio_buffer_space_available(rb))
			return EPOLLOUT | EPOLLWRNORM;
		break;
	}

	return 0;
}

ssize_t iio_buffer_read_wrapper(struct file *filp, char __user *buf,
				size_t n, loff_t *f_ps)
{
	struct iio_dev_buffer_pair *ib = filp->private_data;
	struct iio_buffer *rb = ib->buffer;

	/* check if buffer was opened through new API */
	if (test_bit(IIO_BUSY_BIT_POS, &rb->flags))
		return -EBUSY;

	return iio_buffer_read(filp, buf, n, f_ps);
}

ssize_t iio_buffer_write_wrapper(struct file *filp, const char __user *buf,
				 size_t n, loff_t *f_ps)
{
	struct iio_dev_buffer_pair *ib = filp->private_data;
	struct iio_buffer *rb = ib->buffer;

	/* check if buffer was opened through new API */
	if (test_bit(IIO_BUSY_BIT_POS, &rb->flags))
		return -EBUSY;

	return iio_buffer_write(filp, buf, n, f_ps);
}

__poll_t iio_buffer_poll_wrapper(struct file *filp,
				 struct poll_table_struct *wait)
{
	struct iio_dev_buffer_pair *ib = filp->private_data;
	struct iio_buffer *rb = ib->buffer;

	/* check if buffer was opened through new API */
	if (test_bit(IIO_BUSY_BIT_POS, &rb->flags))
		return 0;

	return iio_buffer_poll(filp, wait);
}

/**
 * iio_buffer_wakeup_poll - Wakes up the buffer waitqueue
 * @indio_dev: The IIO device
 *
 * Wakes up the event waitqueue used for poll(). Should usually
 * be called when the device is unregistered.
 */
void iio_buffer_wakeup_poll(struct iio_dev *indio_dev)
{
	struct iio_dev_opaque *iio_dev_opaque = to_iio_dev_opaque(indio_dev);
	struct iio_buffer *buffer;
	unsigned int i;

	for (i = 0; i < iio_dev_opaque->attached_buffers_cnt; i++) {
		buffer = iio_dev_opaque->attached_buffers[i];
		wake_up(&buffer->pollq);
	}
}

int iio_pop_from_buffer(struct iio_buffer *buffer, void *data)
{
	if (!buffer || !buffer->access || !buffer->access->remove_from)
		return -EINVAL;

	return buffer->access->remove_from(buffer, data);
}
EXPORT_SYMBOL_GPL(iio_pop_from_buffer);

void iio_buffer_init(struct iio_buffer *buffer)
{
	INIT_LIST_HEAD(&buffer->demux_list);
	INIT_LIST_HEAD(&buffer->buffer_list);
	INIT_LIST_HEAD(&buffer->dmabufs);
	mutex_init(&buffer->dmabufs_mutex);
	init_waitqueue_head(&buffer->pollq);
	kref_init(&buffer->ref);
	if (!buffer->watermark)
		buffer->watermark = 1;
}
EXPORT_SYMBOL(iio_buffer_init);

void iio_device_detach_buffers(struct iio_dev *indio_dev)
{
	struct iio_dev_opaque *iio_dev_opaque = to_iio_dev_opaque(indio_dev);
	struct iio_buffer *buffer;
	unsigned int i;

	for (i = 0; i < iio_dev_opaque->attached_buffers_cnt; i++) {
		buffer = iio_dev_opaque->attached_buffers[i];
		iio_buffer_put(buffer);
	}

	kfree(iio_dev_opaque->attached_buffers);
}

static ssize_t iio_show_scan_index(struct device *dev,
				   struct device_attribute *attr,
				   char *buf)
{
	return sysfs_emit(buf, "%u\n", to_iio_dev_attr(attr)->c->scan_index);
}

static ssize_t iio_show_fixed_type(struct device *dev,
				   struct device_attribute *attr,
				   char *buf)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct iio_dev_attr *this_attr = to_iio_dev_attr(attr);
	const struct iio_scan_type *scan_type;
	u8 type;

	scan_type = iio_get_current_scan_type(indio_dev, this_attr->c);
	if (IS_ERR(scan_type))
		return PTR_ERR(scan_type);

	type = scan_type->endianness;

	if (type == IIO_CPU) {
#ifdef __LITTLE_ENDIAN
		type = IIO_LE;
#else
		type = IIO_BE;
#endif
	}
	if (scan_type->repeat > 1)
		return sysfs_emit(buf, "%s:%c%d/%dX%d>>%u\n",
		       iio_endian_prefix[type],
		       scan_type->sign,
		       scan_type->realbits,
		       scan_type->storagebits,
		       scan_type->repeat,
		       scan_type->shift);
	else
		return sysfs_emit(buf, "%s:%c%d/%d>>%u\n",
		       iio_endian_prefix[type],
		       scan_type->sign,
		       scan_type->realbits,
		       scan_type->storagebits,
		       scan_type->shift);
}

static ssize_t iio_scan_el_show(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	int ret;
	struct iio_buffer *buffer = to_iio_dev_attr(attr)->buffer;

	/* Ensure ret is 0 or 1. */
	ret = !!test_bit(to_iio_dev_attr(attr)->address,
		       buffer->scan_mask);

	return sysfs_emit(buf, "%d\n", ret);
}

/* Note NULL used as error indicator as it doesn't make sense. */
static const unsigned long *iio_scan_mask_match(const unsigned long *av_masks,
						unsigned int masklength,
						const unsigned long *mask,
						bool strict)
{
	if (bitmap_empty(mask, masklength))
		return NULL;
	/*
	 * The condition here do not handle multi-long masks correctly.
	 * It only checks the first long to be zero, and will use such mask
	 * as a terminator even if there was bits set after the first long.
	 *
	 * Correct check would require using:
	 * while (!bitmap_empty(av_masks, masklength))
	 * instead. This is potentially hazardous because the
	 * avaliable_scan_masks is a zero terminated array of longs - and
	 * using the proper bitmap_empty() check for multi-long wide masks
	 * would require the array to be terminated with multiple zero longs -
	 * which is not such an usual pattern.
	 *
	 * As writing of this no multi-long wide masks were found in-tree, so
	 * the simple while (*av_masks) check is working.
	 */
	while (*av_masks) {
		if (strict) {
			if (bitmap_equal(mask, av_masks, masklength))
				return av_masks;
		} else {
			if (bitmap_subset(mask, av_masks, masklength))
				return av_masks;
		}
		av_masks += BITS_TO_LONGS(masklength);
	}
	return NULL;
}

static bool iio_validate_scan_mask(struct iio_dev *indio_dev,
				   const unsigned long *mask)
{
	if (!indio_dev->setup_ops->validate_scan_mask)
		return true;

	return indio_dev->setup_ops->validate_scan_mask(indio_dev, mask);
}

/**
 * iio_scan_mask_set() - set particular bit in the scan mask
 * @indio_dev: the iio device
 * @buffer: the buffer whose scan mask we are interested in
 * @bit: the bit to be set.
 *
 * Note that at this point we have no way of knowing what other
 * buffers might request, hence this code only verifies that the
 * individual buffers request is plausible.
 */
static int iio_scan_mask_set(struct iio_dev *indio_dev,
			     struct iio_buffer *buffer, int bit)
{
	unsigned int masklength = iio_get_masklength(indio_dev);
	const unsigned long *mask;
	unsigned long *trialmask;

	if (!masklength) {
		WARN(1, "Trying to set scanmask prior to registering buffer\n");
		return -EINVAL;
	}

	trialmask = bitmap_alloc(masklength, GFP_KERNEL);
	if (!trialmask)
		return -ENOMEM;
	bitmap_copy(trialmask, buffer->scan_mask, masklength);
	set_bit(bit, trialmask);

	if (!iio_validate_scan_mask(indio_dev, trialmask))
		goto err_invalid_mask;

	if (indio_dev->available_scan_masks) {
		mask = iio_scan_mask_match(indio_dev->available_scan_masks,
					   masklength, trialmask, false);
		if (!mask)
			goto err_invalid_mask;
	}
	bitmap_copy(buffer->scan_mask, trialmask, masklength);

	bitmap_free(trialmask);

	return 0;

err_invalid_mask:
	bitmap_free(trialmask);
	return -EINVAL;
}

static int iio_scan_mask_clear(struct iio_buffer *buffer, int bit)
{
	clear_bit(bit, buffer->scan_mask);
	return 0;
}

static int iio_scan_mask_query(struct iio_dev *indio_dev,
			       struct iio_buffer *buffer, int bit)
{
	if (bit > iio_get_masklength(indio_dev))
		return -EINVAL;

	if (!buffer->scan_mask)
		return 0;

	/* Ensure return value is 0 or 1. */
	return !!test_bit(bit, buffer->scan_mask);
};

static ssize_t iio_scan_el_store(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf,
				 size_t len)
{
	int ret;
	bool state;
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct iio_dev_opaque *iio_dev_opaque = to_iio_dev_opaque(indio_dev);
	struct iio_dev_attr *this_attr = to_iio_dev_attr(attr);
	struct iio_buffer *buffer = this_attr->buffer;

	ret = kstrtobool(buf, &state);
	if (ret < 0)
		return ret;

	guard(mutex)(&iio_dev_opaque->mlock);
	if (iio_buffer_is_active(buffer))
		return -EBUSY;

	ret = iio_scan_mask_query(indio_dev, buffer, this_attr->address);
	if (ret < 0)
		return ret;

	if (state && ret)
		return len;

	if (state)
		ret = iio_scan_mask_set(indio_dev, buffer, this_attr->address);
	else
		ret = iio_scan_mask_clear(buffer, this_attr->address);
	if (ret)
		return ret;

	return len;
}

static ssize_t iio_scan_el_ts_show(struct device *dev,
				   struct device_attribute *attr,
				   char *buf)
{
	struct iio_buffer *buffer = to_iio_dev_attr(attr)->buffer;

	return sysfs_emit(buf, "%d\n", buffer->scan_timestamp);
}

static ssize_t iio_scan_el_ts_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf,
				    size_t len)
{
	int ret;
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct iio_dev_opaque *iio_dev_opaque = to_iio_dev_opaque(indio_dev);
	struct iio_buffer *buffer = to_iio_dev_attr(attr)->buffer;
	bool state;

	ret = kstrtobool(buf, &state);
	if (ret < 0)
		return ret;

	guard(mutex)(&iio_dev_opaque->mlock);
	if (iio_buffer_is_active(buffer))
		return -EBUSY;

	buffer->scan_timestamp = state;

	return len;
}

static int iio_buffer_add_channel_sysfs(struct iio_dev *indio_dev,
					struct iio_buffer *buffer,
					const struct iio_chan_spec *chan)
{
	int ret, attrcount = 0;

	ret = __iio_add_chan_devattr("index",
				     chan,
				     &iio_show_scan_index,
				     NULL,
				     0,
				     IIO_SEPARATE,
				     &indio_dev->dev,
				     buffer,
				     &buffer->buffer_attr_list);
	if (ret)
		return ret;
	attrcount++;
	ret = __iio_add_chan_devattr("type",
				     chan,
				     &iio_show_fixed_type,
				     NULL,
				     0,
				     IIO_SEPARATE,
				     &indio_dev->dev,
				     buffer,
				     &buffer->buffer_attr_list);
	if (ret)
		return ret;
	attrcount++;
	if (chan->type != IIO_TIMESTAMP)
		ret = __iio_add_chan_devattr("en",
					     chan,
					     &iio_scan_el_show,
					     &iio_scan_el_store,
					     chan->scan_index,
					     IIO_SEPARATE,
					     &indio_dev->dev,
					     buffer,
					     &buffer->buffer_attr_list);
	else
		ret = __iio_add_chan_devattr("en",
					     chan,
					     &iio_scan_el_ts_show,
					     &iio_scan_el_ts_store,
					     chan->scan_index,
					     IIO_SEPARATE,
					     &indio_dev->dev,
					     buffer,
					     &buffer->buffer_attr_list);
	if (ret)
		return ret;
	attrcount++;
	ret = attrcount;
	return ret;
}

static ssize_t length_show(struct device *dev, struct device_attribute *attr,
			   char *buf)
{
	struct iio_buffer *buffer = to_iio_dev_attr(attr)->buffer;

	return sysfs_emit(buf, "%d\n", buffer->length);
}

static ssize_t length_store(struct device *dev, struct device_attribute *attr,
			    const char *buf, size_t len)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct iio_dev_opaque *iio_dev_opaque = to_iio_dev_opaque(indio_dev);
	struct iio_buffer *buffer = to_iio_dev_attr(attr)->buffer;
	unsigned int val;
	int ret;

	ret = kstrtouint(buf, 10, &val);
	if (ret)
		return ret;

	if (val == buffer->length)
		return len;

	guard(mutex)(&iio_dev_opaque->mlock);
	if (iio_buffer_is_active(buffer))
		return -EBUSY;

	buffer->access->set_length(buffer, val);

	if (buffer->length && buffer->length < buffer->watermark)
		buffer->watermark = buffer->length;

	return len;
}

static ssize_t enable_show(struct device *dev, struct device_attribute *attr,
			   char *buf)
{
	struct iio_buffer *buffer = to_iio_dev_attr(attr)->buffer;

	return sysfs_emit(buf, "%d\n", iio_buffer_is_active(buffer));
}

static int iio_storage_bytes_for_si(struct iio_dev *indio_dev,
				    unsigned int scan_index)
{
	const struct iio_chan_spec *ch;
	const struct iio_scan_type *scan_type;
	unsigned int bytes;

	ch = iio_find_channel_from_si(indio_dev, scan_index);
	scan_type = iio_get_current_scan_type(indio_dev, ch);
	if (IS_ERR(scan_type))
		return PTR_ERR(scan_type);

	bytes = scan_type->storagebits / 8;

	if (scan_type->repeat > 1)
		bytes *= scan_type->repeat;

	return bytes;
}

static int iio_storage_bytes_for_timestamp(struct iio_dev *indio_dev)
{
	struct iio_dev_opaque *iio_dev_opaque = to_iio_dev_opaque(indio_dev);

	return iio_storage_bytes_for_si(indio_dev,
					iio_dev_opaque->scan_index_timestamp);
}

static int iio_compute_scan_bytes(struct iio_dev *indio_dev,
				  const unsigned long *mask, bool timestamp)
{
	unsigned int bytes = 0;
	int length, i, largest = 0;

	/* How much space will the demuxed element take? */
	for_each_set_bit(i, mask, iio_get_masklength(indio_dev)) {
		length = iio_storage_bytes_for_si(indio_dev, i);
		if (length < 0)
			return length;

		bytes = ALIGN(bytes, length);
		bytes += length;
		largest = max(largest, length);
	}

	if (timestamp) {
		length = iio_storage_bytes_for_timestamp(indio_dev);
		if (length < 0)
			return length;

		bytes = ALIGN(bytes, length);
		bytes += length;
		largest = max(largest, length);
	}

	bytes = ALIGN(bytes, largest);
	return bytes;
}

static void iio_buffer_activate(struct iio_dev *indio_dev,
				struct iio_buffer *buffer)
{
	struct iio_dev_opaque *iio_dev_opaque = to_iio_dev_opaque(indio_dev);

	iio_buffer_get(buffer);
	list_add(&buffer->buffer_list, &iio_dev_opaque->buffer_list);
}

static void iio_buffer_deactivate(struct iio_buffer *buffer)
{
	list_del_init(&buffer->buffer_list);
	wake_up_interruptible(&buffer->pollq);
	iio_buffer_put(buffer);
}

static void iio_buffer_deactivate_all(struct iio_dev *indio_dev)
{
	struct iio_dev_opaque *iio_dev_opaque = to_iio_dev_opaque(indio_dev);
	struct iio_buffer *buffer, *_buffer;

	list_for_each_entry_safe(buffer, _buffer,
				 &iio_dev_opaque->buffer_list, buffer_list)
		iio_buffer_deactivate(buffer);
}

static int iio_buffer_enable(struct iio_buffer *buffer,
			     struct iio_dev *indio_dev)
{
	if (!buffer->access->enable)
		return 0;
	return buffer->access->enable(buffer, indio_dev);
}

static int iio_buffer_disable(struct iio_buffer *buffer,
			      struct iio_dev *indio_dev)
{
	if (!buffer->access->disable)
		return 0;
	return buffer->access->disable(buffer, indio_dev);
}

static void iio_buffer_update_bytes_per_datum(struct iio_dev *indio_dev,
					      struct iio_buffer *buffer)
{
	unsigned int bytes;

	if (!buffer->access->set_bytes_per_datum)
		return;

	bytes = iio_compute_scan_bytes(indio_dev, buffer->scan_mask,
				       buffer->scan_timestamp);

	buffer->access->set_bytes_per_datum(buffer, bytes);
}

static int iio_buffer_request_update(struct iio_dev *indio_dev,
				     struct iio_buffer *buffer)
{
	int ret;

	iio_buffer_update_bytes_per_datum(indio_dev, buffer);
	if (buffer->access->request_update) {
		ret = buffer->access->request_update(buffer);
		if (ret) {
			dev_dbg(&indio_dev->dev,
				"Buffer not started: buffer parameter update failed (%d)\n",
				ret);
			return ret;
		}
	}

	return 0;
}

static void iio_free_scan_mask(struct iio_dev *indio_dev,
			       const unsigned long *mask)
{
	/* If the mask is dynamically allocated free it, otherwise do nothing */
	if (!indio_dev->available_scan_masks)
		bitmap_free(mask);
}

struct iio_device_config {
	unsigned int mode;
	unsigned int watermark;
	const unsigned long *scan_mask;
	unsigned int scan_bytes;
	bool scan_timestamp;
};

static int iio_verify_update(struct iio_dev *indio_dev,
			     struct iio_buffer *insert_buffer,
			     struct iio_buffer *remove_buffer,
			     struct iio_device_config *config)
{
	struct iio_dev_opaque *iio_dev_opaque = to_iio_dev_opaque(indio_dev);
	unsigned int masklength = iio_get_masklength(indio_dev);
	unsigned long *compound_mask;
	const unsigned long *scan_mask;
	bool strict_scanmask = false;
	struct iio_buffer *buffer;
	bool scan_timestamp;
	unsigned int modes;

	if (insert_buffer &&
	    bitmap_empty(insert_buffer->scan_mask, masklength)) {
		dev_dbg(&indio_dev->dev,
			"At least one scan element must be enabled first\n");
		return -EINVAL;
	}

	memset(config, 0, sizeof(*config));
	config->watermark = ~0;

	/*
	 * If there is just one buffer and we are removing it there is nothing
	 * to verify.
	 */
	if (remove_buffer && !insert_buffer &&
	    list_is_singular(&iio_dev_opaque->buffer_list))
		return 0;

	modes = indio_dev->modes;

	list_for_each_entry(buffer, &iio_dev_opaque->buffer_list, buffer_list) {
		if (buffer == remove_buffer)
			continue;
		modes &= buffer->access->modes;
		config->watermark = min(config->watermark, buffer->watermark);
	}

	if (insert_buffer) {
		modes &= insert_buffer->access->modes;
		config->watermark = min(config->watermark,
					insert_buffer->watermark);
	}

	/* Definitely possible for devices to support both of these. */
	if ((modes & INDIO_BUFFER_TRIGGERED) && indio_dev->trig) {
		config->mode = INDIO_BUFFER_TRIGGERED;
	} else if (modes & INDIO_BUFFER_HARDWARE) {
		/*
		 * Keep things simple for now and only allow a single buffer to
		 * be connected in hardware mode.
		 */
		if (insert_buffer && !list_empty(&iio_dev_opaque->buffer_list))
			return -EINVAL;
		config->mode = INDIO_BUFFER_HARDWARE;
		strict_scanmask = true;
	} else if (modes & INDIO_BUFFER_SOFTWARE) {
		config->mode = INDIO_BUFFER_SOFTWARE;
	} else {
		/* Can only occur on first buffer */
		if (indio_dev->modes & INDIO_BUFFER_TRIGGERED)
			dev_dbg(&indio_dev->dev, "Buffer not started: no trigger\n");
		return -EINVAL;
	}

	/* What scan mask do we actually have? */
	compound_mask = bitmap_zalloc(masklength, GFP_KERNEL);
	if (!compound_mask)
		return -ENOMEM;

	scan_timestamp = false;

	list_for_each_entry(buffer, &iio_dev_opaque->buffer_list, buffer_list) {
		if (buffer == remove_buffer)
			continue;
		bitmap_or(compound_mask, compound_mask, buffer->scan_mask,
			  masklength);
		scan_timestamp |= buffer->scan_timestamp;
	}

	if (insert_buffer) {
		bitmap_or(compound_mask, compound_mask,
			  insert_buffer->scan_mask, masklength);
		scan_timestamp |= insert_buffer->scan_timestamp;
	}

	if (indio_dev->available_scan_masks) {
		scan_mask = iio_scan_mask_match(indio_dev->available_scan_masks,
						masklength, compound_mask,
						strict_scanmask);
		bitmap_free(compound_mask);
		if (!scan_mask)
			return -EINVAL;
	} else {
		scan_mask = compound_mask;
	}

	config->scan_bytes = iio_compute_scan_bytes(indio_dev,
						    scan_mask, scan_timestamp);
	config->scan_mask = scan_mask;
	config->scan_timestamp = scan_timestamp;

	return 0;
}

/**
 * struct iio_demux_table - table describing demux memcpy ops
 * @from:	index to copy from
 * @to:		index to copy to
 * @length:	how many bytes to copy
 * @l:		list head used for management
 */
struct iio_demux_table {
	unsigned int from;
	unsigned int to;
	unsigned int length;
	struct list_head l;
};

static void iio_buffer_demux_free(struct iio_buffer *buffer)
{
	struct iio_demux_table *p, *q;

	list_for_each_entry_safe(p, q, &buffer->demux_list, l) {
		list_del(&p->l);
		kfree(p);
	}
}

static int iio_buffer_add_demux(struct iio_buffer *buffer,
				struct iio_demux_table **p, unsigned int in_loc,
				unsigned int out_loc,
				unsigned int length)
{
	if (*p && (*p)->from + (*p)->length == in_loc &&
	    (*p)->to + (*p)->length == out_loc) {
		(*p)->length += length;
	} else {
		*p = kmalloc(sizeof(**p), GFP_KERNEL);
		if (!(*p))
			return -ENOMEM;
		(*p)->from = in_loc;
		(*p)->to = out_loc;
		(*p)->length = length;
		list_add_tail(&(*p)->l, &buffer->demux_list);
	}

	return 0;
}

static int iio_buffer_update_demux(struct iio_dev *indio_dev,
				   struct iio_buffer *buffer)
{
	unsigned int masklength = iio_get_masklength(indio_dev);
	int ret, in_ind = -1, out_ind, length;
	unsigned int in_loc = 0, out_loc = 0;
	struct iio_demux_table *p = NULL;

	/* Clear out any old demux */
	iio_buffer_demux_free(buffer);
	kfree(buffer->demux_bounce);
	buffer->demux_bounce = NULL;

	/* First work out which scan mode we will actually have */
	if (bitmap_equal(indio_dev->active_scan_mask,
			 buffer->scan_mask, masklength))
		return 0;

	/* Now we have the two masks, work from least sig and build up sizes */
	for_each_set_bit(out_ind, buffer->scan_mask, masklength) {
		in_ind = find_next_bit(indio_dev->active_scan_mask,
				       masklength, in_ind + 1);
		while (in_ind != out_ind) {
			ret = iio_storage_bytes_for_si(indio_dev, in_ind);
			if (ret < 0)
				goto error_clear_mux_table;

			length = ret;
			/* Make sure we are aligned */
			in_loc = roundup(in_loc, length) + length;
			in_ind = find_next_bit(indio_dev->active_scan_mask,
					       masklength, in_ind + 1);
		}
		ret = iio_storage_bytes_for_si(indio_dev, in_ind);
		if (ret < 0)
			goto error_clear_mux_table;

		length = ret;
		out_loc = roundup(out_loc, length);
		in_loc = roundup(in_loc, length);
		ret = iio_buffer_add_demux(buffer, &p, in_loc, out_loc, length);
		if (ret)
			goto error_clear_mux_table;
		out_loc += length;
		in_loc += length;
	}
	/* Relies on scan_timestamp being last */
	if (buffer->scan_timestamp) {
		ret = iio_storage_bytes_for_timestamp(indio_dev);
		if (ret < 0)
			goto error_clear_mux_table;

		length = ret;
		out_loc = roundup(out_loc, length);
		in_loc = roundup(in_loc, length);
		ret = iio_buffer_add_demux(buffer, &p, in_loc, out_loc, length);
		if (ret)
			goto error_clear_mux_table;
		out_loc += length;
	}
	buffer->demux_bounce = kzalloc(out_loc, GFP_KERNEL);
	if (!buffer->demux_bounce) {
		ret = -ENOMEM;
		goto error_clear_mux_table;
	}
	return 0;

error_clear_mux_table:
	iio_buffer_demux_free(buffer);

	return ret;
}

static int iio_update_demux(struct iio_dev *indio_dev)
{
	struct iio_dev_opaque *iio_dev_opaque = to_iio_dev_opaque(indio_dev);
	struct iio_buffer *buffer;
	int ret;

	list_for_each_entry(buffer, &iio_dev_opaque->buffer_list, buffer_list) {
		ret = iio_buffer_update_demux(indio_dev, buffer);
		if (ret < 0)
			goto error_clear_mux_table;
	}
	return 0;

error_clear_mux_table:
	list_for_each_entry(buffer, &iio_dev_opaque->buffer_list, buffer_list)
		iio_buffer_demux_free(buffer);

	return ret;
}

static int iio_enable_buffers(struct iio_dev *indio_dev,
			      struct iio_device_config *config)
{
	struct iio_dev_opaque *iio_dev_opaque = to_iio_dev_opaque(indio_dev);
	struct iio_buffer *buffer, *tmp = NULL;
	int ret;

	indio_dev->active_scan_mask = config->scan_mask;
	ACCESS_PRIVATE(indio_dev, scan_timestamp) = config->scan_timestamp;
	indio_dev->scan_bytes = config->scan_bytes;
	iio_dev_opaque->currentmode = config->mode;

	iio_update_demux(indio_dev);

	/* Wind up again */
	if (indio_dev->setup_ops->preenable) {
		ret = indio_dev->setup_ops->preenable(indio_dev);
		if (ret) {
			dev_dbg(&indio_dev->dev,
				"Buffer not started: buffer preenable failed (%d)\n", ret);
			goto err_undo_config;
		}
	}

	if (indio_dev->info->update_scan_mode) {
		ret = indio_dev->info
			->update_scan_mode(indio_dev,
					   indio_dev->active_scan_mask);
		if (ret < 0) {
			dev_dbg(&indio_dev->dev,
				"Buffer not started: update scan mode failed (%d)\n",
				ret);
			goto err_run_postdisable;
		}
	}

	if (indio_dev->info->hwfifo_set_watermark)
		indio_dev->info->hwfifo_set_watermark(indio_dev,
			config->watermark);

	list_for_each_entry(buffer, &iio_dev_opaque->buffer_list, buffer_list) {
		ret = iio_buffer_enable(buffer, indio_dev);
		if (ret) {
			tmp = buffer;
			goto err_disable_buffers;
		}
	}

	if (iio_dev_opaque->currentmode == INDIO_BUFFER_TRIGGERED) {
		ret = iio_trigger_attach_poll_func(indio_dev->trig,
						   indio_dev->pollfunc);
		if (ret)
			goto err_disable_buffers;
	}

	if (indio_dev->setup_ops->postenable) {
		ret = indio_dev->setup_ops->postenable(indio_dev);
		if (ret) {
			dev_dbg(&indio_dev->dev,
				"Buffer not started: postenable failed (%d)\n", ret);
			goto err_detach_pollfunc;
		}
	}

	return 0;

err_detach_pollfunc:
	if (iio_dev_opaque->currentmode == INDIO_BUFFER_TRIGGERED) {
		iio_trigger_detach_poll_func(indio_dev->trig,
					     indio_dev->pollfunc);
	}
err_disable_buffers:
	buffer = list_prepare_entry(tmp, &iio_dev_opaque->buffer_list, buffer_list);
	list_for_each_entry_continue_reverse(buffer, &iio_dev_opaque->buffer_list,
					     buffer_list)
		iio_buffer_disable(buffer, indio_dev);
err_run_postdisable:
	if (indio_dev->setup_ops->postdisable)
		indio_dev->setup_ops->postdisable(indio_dev);
err_undo_config:
	iio_dev_opaque->currentmode = INDIO_DIRECT_MODE;
	indio_dev->active_scan_mask = NULL;

	return ret;
}

static int iio_disable_buffers(struct iio_dev *indio_dev)
{
	struct iio_dev_opaque *iio_dev_opaque = to_iio_dev_opaque(indio_dev);
	struct iio_buffer *buffer;
	int ret = 0;
	int ret2;

	/* Wind down existing buffers - iff there are any */
	if (list_empty(&iio_dev_opaque->buffer_list))
		return 0;

	/*
	 * If things go wrong at some step in disable we still need to continue
	 * to perform the other steps, otherwise we leave the device in a
	 * inconsistent state. We return the error code for the first error we
	 * encountered.
	 */

	if (indio_dev->setup_ops->predisable) {
		ret2 = indio_dev->setup_ops->predisable(indio_dev);
		if (ret2 && !ret)
			ret = ret2;
	}

	if (iio_dev_opaque->currentmode == INDIO_BUFFER_TRIGGERED) {
		iio_trigger_detach_poll_func(indio_dev->trig,
					     indio_dev->pollfunc);
	}

	list_for_each_entry(buffer, &iio_dev_opaque->buffer_list, buffer_list) {
		ret2 = iio_buffer_disable(buffer, indio_dev);
		if (ret2 && !ret)
			ret = ret2;
	}

	if (indio_dev->setup_ops->postdisable) {
		ret2 = indio_dev->setup_ops->postdisable(indio_dev);
		if (ret2 && !ret)
			ret = ret2;
	}

	iio_free_scan_mask(indio_dev, indio_dev->active_scan_mask);
	indio_dev->active_scan_mask = NULL;
	iio_dev_opaque->currentmode = INDIO_DIRECT_MODE;

	return ret;
}

static int __iio_update_buffers(struct iio_dev *indio_dev,
				struct iio_buffer *insert_buffer,
				struct iio_buffer *remove_buffer)
{
	struct iio_dev_opaque *iio_dev_opaque = to_iio_dev_opaque(indio_dev);
	struct iio_device_config new_config;
	int ret;

	ret = iio_verify_update(indio_dev, insert_buffer, remove_buffer,
				&new_config);
	if (ret)
		return ret;

	if (insert_buffer) {
		ret = iio_buffer_request_update(indio_dev, insert_buffer);
		if (ret)
			goto err_free_config;
	}

	ret = iio_disable_buffers(indio_dev);
	if (ret)
		goto err_deactivate_all;

	if (remove_buffer)
		iio_buffer_deactivate(remove_buffer);
	if (insert_buffer)
		iio_buffer_activate(indio_dev, insert_buffer);

	/* If no buffers in list, we are done */
	if (list_empty(&iio_dev_opaque->buffer_list))
		return 0;

	ret = iio_enable_buffers(indio_dev, &new_config);
	if (ret)
		goto err_deactivate_all;

	return 0;

err_deactivate_all:
	/*
	 * We've already verified that the config is valid earlier. If things go
	 * wrong in either enable or disable the most likely reason is an IO
	 * error from the device. In this case there is no good recovery
	 * strategy. Just make sure to disable everything and leave the device
	 * in a sane state.  With a bit of luck the device might come back to
	 * life again later and userspace can try again.
	 */
	iio_buffer_deactivate_all(indio_dev);

err_free_config:
	iio_free_scan_mask(indio_dev, new_config.scan_mask);
	return ret;
}

int iio_update_buffers(struct iio_dev *indio_dev,
		       struct iio_buffer *insert_buffer,
		       struct iio_buffer *remove_buffer)
{
	struct iio_dev_opaque *iio_dev_opaque = to_iio_dev_opaque(indio_dev);

	if (insert_buffer == remove_buffer)
		return 0;

	if (insert_buffer &&
	    insert_buffer->direction == IIO_BUFFER_DIRECTION_OUT)
		return -EINVAL;

	guard(mutex)(&iio_dev_opaque->info_exist_lock);
	guard(mutex)(&iio_dev_opaque->mlock);

	if (insert_buffer && iio_buffer_is_active(insert_buffer))
		insert_buffer = NULL;

	if (remove_buffer && !iio_buffer_is_active(remove_buffer))
		remove_buffer = NULL;

	if (!insert_buffer && !remove_buffer)
		return 0;

	if (!indio_dev->info)
		return -ENODEV;

	return __iio_update_buffers(indio_dev, insert_buffer, remove_buffer);
}
EXPORT_SYMBOL_GPL(iio_update_buffers);

void iio_disable_all_buffers(struct iio_dev *indio_dev)
{
	iio_disable_buffers(indio_dev);
	iio_buffer_deactivate_all(indio_dev);
}

static ssize_t enable_store(struct device *dev, struct device_attribute *attr,
			    const char *buf, size_t len)
{
	int ret;
	bool requested_state;
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct iio_dev_opaque *iio_dev_opaque = to_iio_dev_opaque(indio_dev);
	struct iio_buffer *buffer = to_iio_dev_attr(attr)->buffer;
	bool inlist;

	ret = kstrtobool(buf, &requested_state);
	if (ret < 0)
		return ret;

	guard(mutex)(&iio_dev_opaque->mlock);

	/* Find out if it is in the list */
	inlist = iio_buffer_is_active(buffer);
	/* Already in desired state */
	if (inlist == requested_state)
		return len;

	if (requested_state)
		ret = __iio_update_buffers(indio_dev, buffer, NULL);
	else
		ret = __iio_update_buffers(indio_dev, NULL, buffer);
	if (ret)
		return ret;

	return len;
}

static ssize_t watermark_show(struct device *dev, struct device_attribute *attr,
			      char *buf)
{
	struct iio_buffer *buffer = to_iio_dev_attr(attr)->buffer;

	return sysfs_emit(buf, "%u\n", buffer->watermark);
}

static ssize_t watermark_store(struct device *dev,
			       struct device_attribute *attr,
			       const char *buf, size_t len)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct iio_dev_opaque *iio_dev_opaque = to_iio_dev_opaque(indio_dev);
	struct iio_buffer *buffer = to_iio_dev_attr(attr)->buffer;
	unsigned int val;
	int ret;

	ret = kstrtouint(buf, 10, &val);
	if (ret)
		return ret;
	if (!val)
		return -EINVAL;

	guard(mutex)(&iio_dev_opaque->mlock);

	if (val > buffer->length)
		return -EINVAL;

	if (iio_buffer_is_active(buffer))
		return -EBUSY;

	buffer->watermark = val;

	return len;
}

static ssize_t data_available_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct iio_buffer *buffer = to_iio_dev_attr(attr)->buffer;

	return sysfs_emit(buf, "%zu\n", iio_buffer_data_available(buffer));
}

static ssize_t direction_show(struct device *dev,
			      struct device_attribute *attr,
			      char *buf)
{
	struct iio_buffer *buffer = to_iio_dev_attr(attr)->buffer;

	switch (buffer->direction) {
	case IIO_BUFFER_DIRECTION_IN:
		return sysfs_emit(buf, "in\n");
	case IIO_BUFFER_DIRECTION_OUT:
		return sysfs_emit(buf, "out\n");
	default:
		return -EINVAL;
	}
}

static DEVICE_ATTR_RW(length);
static struct device_attribute dev_attr_length_ro = __ATTR_RO(length);
static DEVICE_ATTR_RW(enable);
static DEVICE_ATTR_RW(watermark);
static struct device_attribute dev_attr_watermark_ro = __ATTR_RO(watermark);
static DEVICE_ATTR_RO(data_available);
static DEVICE_ATTR_RO(direction);

/*
 * When adding new attributes here, put the at the end, at least until
 * the code that handles the length/length_ro & watermark/watermark_ro
 * assignments gets cleaned up. Otherwise these can create some weird
 * duplicate attributes errors under some setups.
 */
static struct attribute *iio_buffer_attrs[] = {
	&dev_attr_length.attr,
	&dev_attr_enable.attr,
	&dev_attr_watermark.attr,
	&dev_attr_data_available.attr,
	&dev_attr_direction.attr,
};

#define to_dev_attr(_attr) container_of(_attr, struct device_attribute, attr)

static struct attribute *iio_buffer_wrap_attr(struct iio_buffer *buffer,
					      struct attribute *attr)
{
	struct device_attribute *dattr = to_dev_attr(attr);
	struct iio_dev_attr *iio_attr;

	iio_attr = kzalloc(sizeof(*iio_attr), GFP_KERNEL);
	if (!iio_attr)
		return NULL;

	iio_attr->buffer = buffer;
	memcpy(&iio_attr->dev_attr, dattr, sizeof(iio_attr->dev_attr));
	iio_attr->dev_attr.attr.name = kstrdup_const(attr->name, GFP_KERNEL);
	if (!iio_attr->dev_attr.attr.name) {
		kfree(iio_attr);
		return NULL;
	}

	sysfs_attr_init(&iio_attr->dev_attr.attr);

	list_add(&iio_attr->l, &buffer->buffer_attr_list);

	return &iio_attr->dev_attr.attr;
}

static int iio_buffer_register_legacy_sysfs_groups(struct iio_dev *indio_dev,
						   struct attribute **buffer_attrs,
						   int buffer_attrcount,
						   int scan_el_attrcount)
{
	struct iio_dev_opaque *iio_dev_opaque = to_iio_dev_opaque(indio_dev);
	struct attribute_group *group;
	struct attribute **attrs;
	int ret;

	attrs = kcalloc(buffer_attrcount + 1, sizeof(*attrs), GFP_KERNEL);
	if (!attrs)
		return -ENOMEM;

	memcpy(attrs, buffer_attrs, buffer_attrcount * sizeof(*attrs));

	group = &iio_dev_opaque->legacy_buffer_group;
	group->attrs = attrs;
	group->name = "buffer";

	ret = iio_device_register_sysfs_group(indio_dev, group);
	if (ret)
		goto error_free_buffer_attrs;

	attrs = kcalloc(scan_el_attrcount + 1, sizeof(*attrs), GFP_KERNEL);
	if (!attrs) {
		ret = -ENOMEM;
		goto error_free_buffer_attrs;
	}

	memcpy(attrs, &buffer_attrs[buffer_attrcount],
	       scan_el_attrcount * sizeof(*attrs));

	group = &iio_dev_opaque->legacy_scan_el_group;
	group->attrs = attrs;
	group->name = "scan_elements";

	ret = iio_device_register_sysfs_group(indio_dev, group);
	if (ret)
		goto error_free_scan_el_attrs;

	return 0;

error_free_scan_el_attrs:
	kfree(iio_dev_opaque->legacy_scan_el_group.attrs);
error_free_buffer_attrs:
	kfree(iio_dev_opaque->legacy_buffer_group.attrs);

	return ret;
}

static void iio_buffer_unregister_legacy_sysfs_groups(struct iio_dev *indio_dev)
{
	struct iio_dev_opaque *iio_dev_opaque = to_iio_dev_opaque(indio_dev);

	kfree(iio_dev_opaque->legacy_buffer_group.attrs);
	kfree(iio_dev_opaque->legacy_scan_el_group.attrs);
}

static void iio_buffer_dmabuf_release(struct kref *ref)
{
	struct iio_dmabuf_priv *priv = container_of(ref, struct iio_dmabuf_priv, ref);
	struct dma_buf_attachment *attach = priv->attach;
	struct iio_buffer *buffer = priv->buffer;
	struct dma_buf *dmabuf = attach->dmabuf;

	dma_resv_lock(dmabuf->resv, NULL);
	dma_buf_unmap_attachment(attach, priv->sgt, priv->dir);
	dma_resv_unlock(dmabuf->resv);

	buffer->access->detach_dmabuf(buffer, priv->block);

	dma_buf_detach(attach->dmabuf, attach);
	dma_buf_put(dmabuf);
	kfree(priv);
}

static void iio_buffer_dmabuf_get(struct dma_buf_attachment *attach)
{
	struct iio_dmabuf_priv *priv = attach->importer_priv;

	kref_get(&priv->ref);
}

static void iio_buffer_dmabuf_put(struct dma_buf_attachment *attach)
{
	struct iio_dmabuf_priv *priv = attach->importer_priv;

	kref_put(&priv->ref, iio_buffer_dmabuf_release);
}

static int iio_buffer_chrdev_release(struct inode *inode, struct file *filep)
{
	struct iio_dev_buffer_pair *ib = filep->private_data;
	struct iio_dev *indio_dev = ib->indio_dev;
	struct iio_buffer *buffer = ib->buffer;
	struct iio_dmabuf_priv *priv, *tmp;

	wake_up(&buffer->pollq);

	guard(mutex)(&buffer->dmabufs_mutex);

	/* Close all attached DMABUFs */
	list_for_each_entry_safe(priv, tmp, &buffer->dmabufs, entry) {
		list_del_init(&priv->entry);
		iio_buffer_dmabuf_put(priv->attach);
	}

	kfree(ib);
	clear_bit(IIO_BUSY_BIT_POS, &buffer->flags);
	iio_device_put(indio_dev);

	return 0;
}

static int iio_dma_resv_lock(struct dma_buf *dmabuf, bool nonblock)
{
	if (!nonblock)
		return dma_resv_lock_interruptible(dmabuf->resv, NULL);

	if (!dma_resv_trylock(dmabuf->resv))
		return -EBUSY;

	return 0;
}

static struct device *iio_buffer_get_dma_dev(const struct iio_dev *indio_dev,
					     struct iio_buffer *buffer)
{
	if (buffer->access->get_dma_dev)
		return buffer->access->get_dma_dev(buffer);

	return indio_dev->dev.parent;
}

static struct dma_buf_attachment *
iio_buffer_find_attachment(struct iio_dev_buffer_pair *ib,
			   struct dma_buf *dmabuf, bool nonblock)
{
	struct iio_buffer *buffer = ib->buffer;
	struct device *dma_dev = iio_buffer_get_dma_dev(ib->indio_dev, buffer);
	struct dma_buf_attachment *attach = NULL;
	struct iio_dmabuf_priv *priv;

	guard(mutex)(&buffer->dmabufs_mutex);

	list_for_each_entry(priv, &buffer->dmabufs, entry) {
		if (priv->attach->dev == dma_dev
		    && priv->attach->dmabuf == dmabuf) {
			attach = priv->attach;
			break;
		}
	}

	if (attach)
		iio_buffer_dmabuf_get(attach);

	return attach ?: ERR_PTR(-EPERM);
}

static int iio_buffer_attach_dmabuf(struct iio_dev_buffer_pair *ib,
				    int __user *user_fd, bool nonblock)
{
	struct iio_dev *indio_dev = ib->indio_dev;
	struct iio_buffer *buffer = ib->buffer;
	struct device *dma_dev = iio_buffer_get_dma_dev(indio_dev, buffer);
	struct dma_buf_attachment *attach;
	struct iio_dmabuf_priv *priv, *each;
	struct dma_buf *dmabuf;
	int err, fd;

	if (!buffer->access->attach_dmabuf
	    || !buffer->access->detach_dmabuf
	    || !buffer->access->enqueue_dmabuf)
		return -EPERM;

	if (copy_from_user(&fd, user_fd, sizeof(fd)))
		return -EFAULT;

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	spin_lock_init(&priv->lock);
	priv->context = dma_fence_context_alloc(1);

	dmabuf = dma_buf_get(fd);
	if (IS_ERR(dmabuf)) {
		err = PTR_ERR(dmabuf);
		goto err_free_priv;
	}

	attach = dma_buf_attach(dmabuf, dma_dev);
	if (IS_ERR(attach)) {
		err = PTR_ERR(attach);
		goto err_dmabuf_put;
	}

	err = iio_dma_resv_lock(dmabuf, nonblock);
	if (err)
		goto err_dmabuf_detach;

	priv->dir = buffer->direction == IIO_BUFFER_DIRECTION_IN
		? DMA_FROM_DEVICE : DMA_TO_DEVICE;

	priv->sgt = dma_buf_map_attachment(attach, priv->dir);
	if (IS_ERR(priv->sgt)) {
		err = PTR_ERR(priv->sgt);
		dev_err(&indio_dev->dev, "Unable to map attachment: %d\n", err);
		goto err_resv_unlock;
	}

	kref_init(&priv->ref);
	priv->buffer = buffer;
	priv->attach = attach;
	attach->importer_priv = priv;

	priv->block = buffer->access->attach_dmabuf(buffer, attach);
	if (IS_ERR(priv->block)) {
		err = PTR_ERR(priv->block);
		goto err_dmabuf_unmap_attachment;
	}

	dma_resv_unlock(dmabuf->resv);

	mutex_lock(&buffer->dmabufs_mutex);

	/*
	 * Check whether we already have an attachment for this driver/DMABUF
	 * combo. If we do, refuse to attach.
	 */
	list_for_each_entry(each, &buffer->dmabufs, entry) {
		if (each->attach->dev == dma_dev
		    && each->attach->dmabuf == dmabuf) {
			/*
			 * We unlocked the reservation object, so going through
			 * the cleanup code would mean re-locking it first.
			 * At this stage it is simpler to free the attachment
			 * using iio_buffer_dma_put().
			 */
			mutex_unlock(&buffer->dmabufs_mutex);
			iio_buffer_dmabuf_put(attach);
			return -EBUSY;
		}
	}

	/* Otherwise, add the new attachment to our dmabufs list. */
	list_add(&priv->entry, &buffer->dmabufs);
	mutex_unlock(&buffer->dmabufs_mutex);

	return 0;

err_dmabuf_unmap_attachment:
	dma_buf_unmap_attachment(attach, priv->sgt, priv->dir);
err_resv_unlock:
	dma_resv_unlock(dmabuf->resv);
err_dmabuf_detach:
	dma_buf_detach(dmabuf, attach);
err_dmabuf_put:
	dma_buf_put(dmabuf);
err_free_priv:
	kfree(priv);

	return err;
}

static int iio_buffer_detach_dmabuf(struct iio_dev_buffer_pair *ib,
				    int __user *user_req, bool nonblock)
{
	struct iio_buffer *buffer = ib->buffer;
	struct iio_dev *indio_dev = ib->indio_dev;
	struct device *dma_dev = iio_buffer_get_dma_dev(indio_dev, buffer);
	struct iio_dmabuf_priv *priv;
	struct dma_buf *dmabuf;
	int dmabuf_fd, ret = -EPERM;

	if (copy_from_user(&dmabuf_fd, user_req, sizeof(dmabuf_fd)))
		return -EFAULT;

	dmabuf = dma_buf_get(dmabuf_fd);
	if (IS_ERR(dmabuf))
		return PTR_ERR(dmabuf);

	guard(mutex)(&buffer->dmabufs_mutex);

	list_for_each_entry(priv, &buffer->dmabufs, entry) {
		if (priv->attach->dev == dma_dev
		    && priv->attach->dmabuf == dmabuf) {
			list_del(&priv->entry);

			/* Unref the reference from iio_buffer_attach_dmabuf() */
			iio_buffer_dmabuf_put(priv->attach);
			ret = 0;
			break;
		}
	}

	dma_buf_put(dmabuf);

	return ret;
}

static const char *
iio_buffer_dma_fence_get_driver_name(struct dma_fence *fence)
{
	return "iio";
}

static void iio_buffer_dma_fence_release(struct dma_fence *fence)
{
	struct iio_dma_fence *iio_fence =
		container_of(fence, struct iio_dma_fence, base);

	kfree(iio_fence);
}

static const struct dma_fence_ops iio_buffer_dma_fence_ops = {
	.get_driver_name	= iio_buffer_dma_fence_get_driver_name,
	.get_timeline_name	= iio_buffer_dma_fence_get_driver_name,
	.release		= iio_buffer_dma_fence_release,
};

static int iio_buffer_enqueue_dmabuf(struct iio_dev_buffer_pair *ib,
				     struct iio_dmabuf __user *iio_dmabuf_req,
				     bool nonblock)
{
	struct iio_buffer *buffer = ib->buffer;
	struct iio_dmabuf iio_dmabuf;
	struct dma_buf_attachment *attach;
	struct iio_dmabuf_priv *priv;
	struct iio_dma_fence *fence;
	struct dma_buf *dmabuf;
	unsigned long timeout;
	bool cookie, cyclic, dma_to_ram;
	long retl;
	u32 seqno;
	int ret;

	if (copy_from_user(&iio_dmabuf, iio_dmabuf_req, sizeof(iio_dmabuf)))
		return -EFAULT;

	if (iio_dmabuf.flags & ~IIO_BUFFER_DMABUF_SUPPORTED_FLAGS)
		return -EINVAL;

	cyclic = iio_dmabuf.flags & IIO_BUFFER_DMABUF_CYCLIC;

	/* Cyclic flag is only supported on output buffers */
	if (cyclic && buffer->direction != IIO_BUFFER_DIRECTION_OUT)
		return -EINVAL;

	dmabuf = dma_buf_get(iio_dmabuf.fd);
	if (IS_ERR(dmabuf))
		return PTR_ERR(dmabuf);

	if (!iio_dmabuf.bytes_used || iio_dmabuf.bytes_used > dmabuf->size) {
		ret = -EINVAL;
		goto err_dmabuf_put;
	}

	attach = iio_buffer_find_attachment(ib, dmabuf, nonblock);
	if (IS_ERR(attach)) {
		ret = PTR_ERR(attach);
		goto err_dmabuf_put;
	}

	priv = attach->importer_priv;

	fence = kmalloc(sizeof(*fence), GFP_KERNEL);
	if (!fence) {
		ret = -ENOMEM;
		goto err_attachment_put;
	}

	fence->priv = priv;

	seqno = atomic_add_return(1, &priv->seqno);

	/*
	 * The transfers are guaranteed to be processed in the order they are
	 * enqueued, so we can use a simple incrementing sequence number for
	 * the dma_fence.
	 */
	dma_fence_init(&fence->base, &iio_buffer_dma_fence_ops,
		       &priv->lock, priv->context, seqno);

	ret = iio_dma_resv_lock(dmabuf, nonblock);
	if (ret)
		goto err_fence_put;

	timeout = nonblock ? 0 : msecs_to_jiffies(DMABUF_ENQUEUE_TIMEOUT_MS);
	dma_to_ram = buffer->direction == IIO_BUFFER_DIRECTION_IN;

	/* Make sure we don't have writers */
	retl = dma_resv_wait_timeout(dmabuf->resv,
				     dma_resv_usage_rw(dma_to_ram),
				     true, timeout);
	if (retl == 0)
		retl = -EBUSY;
	if (retl < 0) {
		ret = (int)retl;
		goto err_resv_unlock;
	}

	if (buffer->access->lock_queue)
		buffer->access->lock_queue(buffer);

	ret = dma_resv_reserve_fences(dmabuf->resv, 1);
	if (ret)
		goto err_queue_unlock;

	dma_resv_add_fence(dmabuf->resv, &fence->base,
			   dma_to_ram ? DMA_RESV_USAGE_WRITE : DMA_RESV_USAGE_READ);
	dma_resv_unlock(dmabuf->resv);

	cookie = dma_fence_begin_signalling();

	ret = buffer->access->enqueue_dmabuf(buffer, priv->block, &fence->base,
					     priv->sgt, iio_dmabuf.bytes_used,
					     cyclic);
	if (ret) {
		/*
		 * DMABUF enqueue failed, but we already added the fence.
		 * Signal the error through the fence completion mechanism.
		 */
		iio_buffer_signal_dmabuf_done(&fence->base, ret);
	}

	if (buffer->access->unlock_queue)
		buffer->access->unlock_queue(buffer);

	dma_fence_end_signalling(cookie);
	dma_buf_put(dmabuf);

	return ret;

err_queue_unlock:
	if (buffer->access->unlock_queue)
		buffer->access->unlock_queue(buffer);
err_resv_unlock:
	dma_resv_unlock(dmabuf->resv);
err_fence_put:
	dma_fence_put(&fence->base);
err_attachment_put:
	iio_buffer_dmabuf_put(attach);
err_dmabuf_put:
	dma_buf_put(dmabuf);

	return ret;
}

static void iio_buffer_cleanup(struct work_struct *work)
{
	struct iio_dma_fence *fence =
		container_of(work, struct iio_dma_fence, work);
	struct iio_dmabuf_priv *priv = fence->priv;
	struct dma_buf_attachment *attach = priv->attach;

	dma_fence_put(&fence->base);
	iio_buffer_dmabuf_put(attach);
}

void iio_buffer_signal_dmabuf_done(struct dma_fence *fence, int ret)
{
	struct iio_dma_fence *iio_fence =
		container_of(fence, struct iio_dma_fence, base);
	bool cookie = dma_fence_begin_signalling();

	/*
	 * Get a reference to the fence, so that it's not freed as soon as
	 * it's signaled.
	 */
	dma_fence_get(fence);

	fence->error = ret;
	dma_fence_signal(fence);
	dma_fence_end_signalling(cookie);

	/*
	 * The fence will be unref'd in iio_buffer_cleanup.
	 * It can't be done here, as the unref functions might try to lock the
	 * resv object, which can deadlock.
	 */
	INIT_WORK(&iio_fence->work, iio_buffer_cleanup);
	schedule_work(&iio_fence->work);
}
EXPORT_SYMBOL_GPL(iio_buffer_signal_dmabuf_done);

static long iio_buffer_chrdev_ioctl(struct file *filp,
				    unsigned int cmd, unsigned long arg)
{
	struct iio_dev_buffer_pair *ib = filp->private_data;
	void __user *_arg = (void __user *)arg;
	bool nonblock = filp->f_flags & O_NONBLOCK;

	switch (cmd) {
	case IIO_BUFFER_DMABUF_ATTACH_IOCTL:
		return iio_buffer_attach_dmabuf(ib, _arg, nonblock);
	case IIO_BUFFER_DMABUF_DETACH_IOCTL:
		return iio_buffer_detach_dmabuf(ib, _arg, nonblock);
	case IIO_BUFFER_DMABUF_ENQUEUE_IOCTL:
		return iio_buffer_enqueue_dmabuf(ib, _arg, nonblock);
	default:
		return -EINVAL;
	}
}

static const struct file_operations iio_buffer_chrdev_fileops = {
	.owner = THIS_MODULE,
	.llseek = noop_llseek,
	.read = iio_buffer_read,
	.write = iio_buffer_write,
	.unlocked_ioctl = iio_buffer_chrdev_ioctl,
	.compat_ioctl = compat_ptr_ioctl,
	.poll = iio_buffer_poll,
	.release = iio_buffer_chrdev_release,
};

static long iio_device_buffer_getfd(struct iio_dev *indio_dev, unsigned long arg)
{
	struct iio_dev_opaque *iio_dev_opaque = to_iio_dev_opaque(indio_dev);
	int __user *ival = (int __user *)arg;
	struct iio_dev_buffer_pair *ib;
	struct iio_buffer *buffer;
	int fd, idx, ret;

	if (copy_from_user(&idx, ival, sizeof(idx)))
		return -EFAULT;

	if (idx >= iio_dev_opaque->attached_buffers_cnt)
		return -ENODEV;

	iio_device_get(indio_dev);

	buffer = iio_dev_opaque->attached_buffers[idx];

	if (test_and_set_bit(IIO_BUSY_BIT_POS, &buffer->flags)) {
		ret = -EBUSY;
		goto error_iio_dev_put;
	}

	ib = kzalloc(sizeof(*ib), GFP_KERNEL);
	if (!ib) {
		ret = -ENOMEM;
		goto error_clear_busy_bit;
	}

	ib->indio_dev = indio_dev;
	ib->buffer = buffer;

	fd = anon_inode_getfd("iio:buffer", &iio_buffer_chrdev_fileops,
			      ib, O_RDWR | O_CLOEXEC);
	if (fd < 0) {
		ret = fd;
		goto error_free_ib;
	}

	if (copy_to_user(ival, &fd, sizeof(fd))) {
		/*
		 * "Leak" the fd, as there's not much we can do about this
		 * anyway. 'fd' might have been closed already, as
		 * anon_inode_getfd() called fd_install() on it, which made
		 * it reachable by userland.
		 *
		 * Instead of allowing a malicious user to play tricks with
		 * us, rely on the process exit path to do any necessary
		 * cleanup, as in releasing the file, if still needed.
		 */
		return -EFAULT;
	}

	return 0;

error_free_ib:
	kfree(ib);
error_clear_busy_bit:
	clear_bit(IIO_BUSY_BIT_POS, &buffer->flags);
error_iio_dev_put:
	iio_device_put(indio_dev);
	return ret;
}

static long iio_device_buffer_ioctl(struct iio_dev *indio_dev, struct file *filp,
				    unsigned int cmd, unsigned long arg)
{
	switch (cmd) {
	case IIO_BUFFER_GET_FD_IOCTL:
		return iio_device_buffer_getfd(indio_dev, arg);
	default:
		return IIO_IOCTL_UNHANDLED;
	}
}

static int iio_channel_validate_scan_type(struct device *dev, int ch,
					  const struct iio_scan_type *scan_type)
{
	/* Verify that sample bits fit into storage */
	if (scan_type->storagebits < scan_type->realbits + scan_type->shift) {
		dev_err(dev,
			"Channel %d storagebits (%d) < shifted realbits (%d + %d)\n",
			ch, scan_type->storagebits,
			scan_type->realbits,
			scan_type->shift);
		return -EINVAL;
	}

	return 0;
}

static int __iio_buffer_alloc_sysfs_and_mask(struct iio_buffer *buffer,
					     struct iio_dev *indio_dev,
					     int index)
{
	struct iio_dev_opaque *iio_dev_opaque = to_iio_dev_opaque(indio_dev);
	unsigned int masklength = iio_get_masklength(indio_dev);
	struct iio_dev_attr *p;
	const struct iio_dev_attr *id_attr;
	struct attribute **attr;
	int ret, i, attrn, scan_el_attrcount, buffer_attrcount;
	const struct iio_chan_spec *channels;

	buffer_attrcount = 0;
	if (buffer->attrs) {
		while (buffer->attrs[buffer_attrcount])
			buffer_attrcount++;
	}
	buffer_attrcount += ARRAY_SIZE(iio_buffer_attrs);

	scan_el_attrcount = 0;
	INIT_LIST_HEAD(&buffer->buffer_attr_list);
	channels = indio_dev->channels;
	if (channels) {
		/* new magic */
		for (i = 0; i < indio_dev->num_channels; i++) {
			const struct iio_scan_type *scan_type;

			if (channels[i].scan_index < 0)
				continue;

			if (channels[i].has_ext_scan_type) {
				int j;

				/*
				 * get_current_scan_type is required when using
				 * extended scan types.
				 */
				if (!indio_dev->info->get_current_scan_type) {
					ret = -EINVAL;
					goto error_cleanup_dynamic;
				}

				for (j = 0; j < channels[i].num_ext_scan_type; j++) {
					scan_type = &channels[i].ext_scan_type[j];

					ret = iio_channel_validate_scan_type(
						&indio_dev->dev, i, scan_type);
					if (ret)
						goto error_cleanup_dynamic;
				}
			} else {
				scan_type = &channels[i].scan_type;

				ret = iio_channel_validate_scan_type(
						&indio_dev->dev, i, scan_type);
				if (ret)
					goto error_cleanup_dynamic;
			}

			ret = iio_buffer_add_channel_sysfs(indio_dev, buffer,
							   &channels[i]);
			if (ret < 0)
				goto error_cleanup_dynamic;
			scan_el_attrcount += ret;
			if (channels[i].type == IIO_TIMESTAMP)
				iio_dev_opaque->scan_index_timestamp =
					channels[i].scan_index;
		}
		if (masklength && !buffer->scan_mask) {
			buffer->scan_mask = bitmap_zalloc(masklength,
							  GFP_KERNEL);
			if (!buffer->scan_mask) {
				ret = -ENOMEM;
				goto error_cleanup_dynamic;
			}
		}
	}

	attrn = buffer_attrcount + scan_el_attrcount;
	attr = kcalloc(attrn + 1, sizeof(*attr), GFP_KERNEL);
	if (!attr) {
		ret = -ENOMEM;
		goto error_free_scan_mask;
	}

	memcpy(attr, iio_buffer_attrs, sizeof(iio_buffer_attrs));
	if (!buffer->access->set_length)
		attr[0] = &dev_attr_length_ro.attr;

	if (buffer->access->flags & INDIO_BUFFER_FLAG_FIXED_WATERMARK)
		attr[2] = &dev_attr_watermark_ro.attr;

	if (buffer->attrs)
		for (i = 0, id_attr = buffer->attrs[i];
		     (id_attr = buffer->attrs[i]); i++)
			attr[ARRAY_SIZE(iio_buffer_attrs) + i] =
				(struct attribute *)&id_attr->dev_attr.attr;

	buffer->buffer_group.attrs = attr;

	for (i = 0; i < buffer_attrcount; i++) {
		struct attribute *wrapped;

		wrapped = iio_buffer_wrap_attr(buffer, attr[i]);
		if (!wrapped) {
			ret = -ENOMEM;
			goto error_free_buffer_attrs;
		}
		attr[i] = wrapped;
	}

	attrn = 0;
	list_for_each_entry(p, &buffer->buffer_attr_list, l)
		attr[attrn++] = &p->dev_attr.attr;

	buffer->buffer_group.name = kasprintf(GFP_KERNEL, "buffer%d", index);
	if (!buffer->buffer_group.name) {
		ret = -ENOMEM;
		goto error_free_buffer_attrs;
	}

	ret = iio_device_register_sysfs_group(indio_dev, &buffer->buffer_group);
	if (ret)
		goto error_free_buffer_attr_group_name;

	/* we only need to register the legacy groups for the first buffer */
	if (index > 0)
		return 0;

	ret = iio_buffer_register_legacy_sysfs_groups(indio_dev, attr,
						      buffer_attrcount,
						      scan_el_attrcount);
	if (ret)
		goto error_free_buffer_attr_group_name;

	return 0;

error_free_buffer_attr_group_name:
	kfree(buffer->buffer_group.name);
error_free_buffer_attrs:
	kfree(buffer->buffer_group.attrs);
error_free_scan_mask:
	bitmap_free(buffer->scan_mask);
error_cleanup_dynamic:
	iio_free_chan_devattr_list(&buffer->buffer_attr_list);

	return ret;
}

static void __iio_buffer_free_sysfs_and_mask(struct iio_buffer *buffer,
					     struct iio_dev *indio_dev,
					     int index)
{
	if (index == 0)
		iio_buffer_unregister_legacy_sysfs_groups(indio_dev);
	bitmap_free(buffer->scan_mask);
	kfree(buffer->buffer_group.name);
	kfree(buffer->buffer_group.attrs);
	iio_free_chan_devattr_list(&buffer->buffer_attr_list);
}

int iio_buffers_alloc_sysfs_and_mask(struct iio_dev *indio_dev)
{
	struct iio_dev_opaque *iio_dev_opaque = to_iio_dev_opaque(indio_dev);
	const struct iio_chan_spec *channels;
	struct iio_buffer *buffer;
	int ret, i, idx;
	size_t sz;

	channels = indio_dev->channels;
	if (channels) {
		int ml = 0;

		for (i = 0; i < indio_dev->num_channels; i++)
			ml = max(ml, channels[i].scan_index + 1);
		ACCESS_PRIVATE(indio_dev, masklength) = ml;
	}

	if (!iio_dev_opaque->attached_buffers_cnt)
		return 0;

	for (idx = 0; idx < iio_dev_opaque->attached_buffers_cnt; idx++) {
		buffer = iio_dev_opaque->attached_buffers[idx];
		ret = __iio_buffer_alloc_sysfs_and_mask(buffer, indio_dev, idx);
		if (ret)
			goto error_unwind_sysfs_and_mask;
	}

	sz = sizeof(*iio_dev_opaque->buffer_ioctl_handler);
	iio_dev_opaque->buffer_ioctl_handler = kzalloc(sz, GFP_KERNEL);
	if (!iio_dev_opaque->buffer_ioctl_handler) {
		ret = -ENOMEM;
		goto error_unwind_sysfs_and_mask;
	}

	iio_dev_opaque->buffer_ioctl_handler->ioctl = iio_device_buffer_ioctl;
	iio_device_ioctl_handler_register(indio_dev,
					  iio_dev_opaque->buffer_ioctl_handler);

	return 0;

error_unwind_sysfs_and_mask:
	while (idx--) {
		buffer = iio_dev_opaque->attached_buffers[idx];
		__iio_buffer_free_sysfs_and_mask(buffer, indio_dev, idx);
	}
	return ret;
}

void iio_buffers_free_sysfs_and_mask(struct iio_dev *indio_dev)
{
	struct iio_dev_opaque *iio_dev_opaque = to_iio_dev_opaque(indio_dev);
	struct iio_buffer *buffer;
	int i;

	if (!iio_dev_opaque->attached_buffers_cnt)
		return;

	iio_device_ioctl_handler_unregister(iio_dev_opaque->buffer_ioctl_handler);
	kfree(iio_dev_opaque->buffer_ioctl_handler);

	for (i = iio_dev_opaque->attached_buffers_cnt - 1; i >= 0; i--) {
		buffer = iio_dev_opaque->attached_buffers[i];
		__iio_buffer_free_sysfs_and_mask(buffer, indio_dev, i);
	}
}

/**
 * iio_validate_scan_mask_onehot() - Validates that exactly one channel is selected
 * @indio_dev: the iio device
 * @mask: scan mask to be checked
 *
 * Return true if exactly one bit is set in the scan mask, false otherwise. It
 * can be used for devices where only one channel can be active for sampling at
 * a time.
 */
bool iio_validate_scan_mask_onehot(struct iio_dev *indio_dev,
				   const unsigned long *mask)
{
	return bitmap_weight(mask, iio_get_masklength(indio_dev)) == 1;
}
EXPORT_SYMBOL_GPL(iio_validate_scan_mask_onehot);

static const void *iio_demux(struct iio_buffer *buffer,
			     const void *datain)
{
	struct iio_demux_table *t;

	if (list_empty(&buffer->demux_list))
		return datain;
	list_for_each_entry(t, &buffer->demux_list, l)
		memcpy(buffer->demux_bounce + t->to,
		       datain + t->from, t->length);

	return buffer->demux_bounce;
}

static int iio_push_to_buffer(struct iio_buffer *buffer, const void *data)
{
	const void *dataout = iio_demux(buffer, data);
	int ret;

	ret = buffer->access->store_to(buffer, dataout);
	if (ret)
		return ret;

	/*
	 * We can't just test for watermark to decide if we wake the poll queue
	 * because read may request less samples than the watermark.
	 */
	wake_up_interruptible_poll(&buffer->pollq, EPOLLIN | EPOLLRDNORM);
	return 0;
}

/**
 * iio_push_to_buffers() - push to a registered buffer.
 * @indio_dev:		iio_dev structure for device.
 * @data:		Full scan.
 */
int iio_push_to_buffers(struct iio_dev *indio_dev, const void *data)
{
	struct iio_dev_opaque *iio_dev_opaque = to_iio_dev_opaque(indio_dev);
	int ret;
	struct iio_buffer *buf;

	list_for_each_entry(buf, &iio_dev_opaque->buffer_list, buffer_list) {
		ret = iio_push_to_buffer(buf, data);
		if (ret < 0)
			return ret;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(iio_push_to_buffers);

/**
 * iio_push_to_buffers_with_ts_unaligned() - push to registered buffer,
 *    no alignment or space requirements.
 * @indio_dev:		iio_dev structure for device.
 * @data:		channel data excluding the timestamp.
 * @data_sz:		size of data.
 * @timestamp:		timestamp for the sample data.
 *
 * This special variant of iio_push_to_buffers_with_timestamp() does
 * not require space for the timestamp, or 8 byte alignment of data.
 * It does however require an allocation on first call and additional
 * copies on all calls, so should be avoided if possible.
 */
int iio_push_to_buffers_with_ts_unaligned(struct iio_dev *indio_dev,
					  const void *data,
					  size_t data_sz,
					  int64_t timestamp)
{
	struct iio_dev_opaque *iio_dev_opaque = to_iio_dev_opaque(indio_dev);

	/*
	 * Conservative estimate - we can always safely copy the minimum
	 * of either the data provided or the length of the destination buffer.
	 * This relaxed limit allows the calling drivers to be lax about
	 * tracking the size of the data they are pushing, at the cost of
	 * unnecessary copying of padding.
	 */
	data_sz = min_t(size_t, indio_dev->scan_bytes, data_sz);
	if (iio_dev_opaque->bounce_buffer_size !=  indio_dev->scan_bytes) {
		void *bb;

		bb = devm_krealloc(&indio_dev->dev,
				   iio_dev_opaque->bounce_buffer,
				   indio_dev->scan_bytes, GFP_KERNEL);
		if (!bb)
			return -ENOMEM;
		iio_dev_opaque->bounce_buffer = bb;
		iio_dev_opaque->bounce_buffer_size = indio_dev->scan_bytes;
	}
	memcpy(iio_dev_opaque->bounce_buffer, data, data_sz);
	return iio_push_to_buffers_with_timestamp(indio_dev,
						  iio_dev_opaque->bounce_buffer,
						  timestamp);
}
EXPORT_SYMBOL_GPL(iio_push_to_buffers_with_ts_unaligned);

/**
 * iio_buffer_release() - Free a buffer's resources
 * @ref: Pointer to the kref embedded in the iio_buffer struct
 *
 * This function is called when the last reference to the buffer has been
 * dropped. It will typically free all resources allocated by the buffer. Do not
 * call this function manually, always use iio_buffer_put() when done using a
 * buffer.
 */
static void iio_buffer_release(struct kref *ref)
{
	struct iio_buffer *buffer = container_of(ref, struct iio_buffer, ref);

	mutex_destroy(&buffer->dmabufs_mutex);
	buffer->access->release(buffer);
}

/**
 * iio_buffer_get() - Grab a reference to the buffer
 * @buffer: The buffer to grab a reference for, may be NULL
 *
 * Returns the pointer to the buffer that was passed into the function.
 */
struct iio_buffer *iio_buffer_get(struct iio_buffer *buffer)
{
	if (buffer)
		kref_get(&buffer->ref);

	return buffer;
}
EXPORT_SYMBOL_GPL(iio_buffer_get);

/**
 * iio_buffer_put() - Release the reference to the buffer
 * @buffer: The buffer to release the reference for, may be NULL
 */
void iio_buffer_put(struct iio_buffer *buffer)
{
	if (buffer)
		kref_put(&buffer->ref, iio_buffer_release);
}
EXPORT_SYMBOL_GPL(iio_buffer_put);

/**
 * iio_device_attach_buffer - Attach a buffer to a IIO device
 * @indio_dev: The device the buffer should be attached to
 * @buffer: The buffer to attach to the device
 *
 * Return 0 if successful, negative if error.
 *
 * This function attaches a buffer to a IIO device. The buffer stays attached to
 * the device until the device is freed. For legacy reasons, the first attached
 * buffer will also be assigned to 'indio_dev->buffer'.
 * The array allocated here, will be free'd via the iio_device_detach_buffers()
 * call which is handled by the iio_device_free().
 */
int iio_device_attach_buffer(struct iio_dev *indio_dev,
			     struct iio_buffer *buffer)
{
	struct iio_dev_opaque *iio_dev_opaque = to_iio_dev_opaque(indio_dev);
	struct iio_buffer **new, **old = iio_dev_opaque->attached_buffers;
	unsigned int cnt = iio_dev_opaque->attached_buffers_cnt;

	cnt++;

	new = krealloc(old, sizeof(*new) * cnt, GFP_KERNEL);
	if (!new)
		return -ENOMEM;
	iio_dev_opaque->attached_buffers = new;

	buffer = iio_buffer_get(buffer);

	/* first buffer is legacy; attach it to the IIO device directly */
	if (!indio_dev->buffer)
		indio_dev->buffer = buffer;

	iio_dev_opaque->attached_buffers[cnt - 1] = buffer;
	iio_dev_opaque->attached_buffers_cnt = cnt;

	return 0;
}
EXPORT_SYMBOL_GPL(iio_device_attach_buffer);

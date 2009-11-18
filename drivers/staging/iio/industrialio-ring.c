/* The industrial I/O core
 *
 * Copyright (c) 2008 Jonathan Cameron
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * Handling of ring allocation / resizing.
 *
 *
 * Things to look at here.
 * - Better memory allocation techniques?
 * - Alternative access techniques?
 */
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/fs.h>
#include <linux/poll.h>
#include <linux/module.h>
#include <linux/cdev.h>
#include <linux/idr.h>

#include "iio.h"
#include "ring_generic.h"

/* IDR for ring buffer identifier */
static DEFINE_IDR(iio_ring_idr);
/* IDR for ring event identifier */
static DEFINE_IDR(iio_ring_event_idr);
/* IDR for ring access identifier */
static DEFINE_IDR(iio_ring_access_idr);

int iio_push_ring_event(struct iio_ring_buffer *ring_buf,
		       int event_code,
		       s64 timestamp)
{
	return __iio_push_event(&ring_buf->ev_int,
			       event_code,
			       timestamp,
			       &ring_buf->shared_ev_pointer);
}
EXPORT_SYMBOL(iio_push_ring_event);

int iio_push_or_escallate_ring_event(struct iio_ring_buffer *ring_buf,
				    int event_code,
				    s64 timestamp)
{
	if (ring_buf->shared_ev_pointer.ev_p)
		__iio_change_event(ring_buf->shared_ev_pointer.ev_p,
				   event_code,
				   timestamp);
	else
		return iio_push_ring_event(ring_buf,
					  event_code,
					  timestamp);
	return 0;
}
EXPORT_SYMBOL(iio_push_or_escallate_ring_event);

/**
 * iio_ring_open() chrdev file open for ring buffer access
 *
 * This function relies on all ring buffer implementations having an
 * iio_ring_buffer as their first element.
 **/
int iio_ring_open(struct inode *inode, struct file *filp)
{
	struct iio_handler *hand
		= container_of(inode->i_cdev, struct iio_handler, chrdev);
	struct iio_ring_buffer *rb = hand->private;

	filp->private_data = hand->private;
	if (rb->access.mark_in_use)
		rb->access.mark_in_use(rb);

	return 0;
}

/**
 * iio_ring_release() -chrdev file close ring buffer access
 *
 * This function relies on all ring buffer implementations having an
 * iio_ring_buffer as their first element.
 **/
int iio_ring_release(struct inode *inode, struct file *filp)
{
	struct cdev *cd = inode->i_cdev;
	struct iio_handler *hand = iio_cdev_to_handler(cd);
	struct iio_ring_buffer *rb = hand->private;

	clear_bit(IIO_BUSY_BIT_POS, &rb->access_handler.flags);
	if (rb->access.unmark_in_use)
		rb->access.unmark_in_use(rb);

	return 0;
}

/**
 * iio_ring_rip_outer() chrdev read for ring buffer access
 *
 * This function relies on all ring buffer implementations having an
 * iio_ring _bufer as their first element.
 **/
ssize_t iio_ring_rip_outer(struct file *filp,
			   char *buf,
			   size_t count,
			   loff_t *f_ps)
{
	struct iio_ring_buffer *rb = filp->private_data;
	int ret, dead_offset, copied;
	u8 *data;
	/* rip lots must exist. */
	if (!rb->access.rip_lots)
		return -EINVAL;
	copied = rb->access.rip_lots(rb, count, &data, &dead_offset);

	if (copied < 0) {
		ret = copied;
		goto error_ret;
	}
	if (copy_to_user(buf, data + dead_offset, copied))  {
		ret =  -EFAULT;
		goto error_free_data_cpy;
	}
	/* In clever ring buffer designs this may not need to be freed.
	 * When such a design exists I'll add this to ring access funcs.
	 */
	kfree(data);

	return copied;

error_free_data_cpy:
	kfree(data);
error_ret:
	return ret;
}

static const struct file_operations iio_ring_fileops = {
	.read = iio_ring_rip_outer,
	.release = iio_ring_release,
	.open = iio_ring_open,
	.owner = THIS_MODULE,
};

/**
 * __iio_request_ring_buffer_event_chrdev() allocate ring event chrdev
 * @buf:	ring buffer whose event chrdev we are allocating
 * @owner:	the module who owns the ring buffer (for ref counting)
 * @dev:	device with which the chrdev is associated
 **/
static inline int
__iio_request_ring_buffer_event_chrdev(struct iio_ring_buffer *buf,
				       int id,
				       struct module *owner,
				       struct device *dev)
{
	int ret;
	ret = iio_get_new_idr_val(&iio_ring_event_idr);
	if (ret < 0)
		goto error_ret;
	else
		buf->ev_int.id = ret;

	snprintf(buf->ev_int._name, 20,
		 "ring_event_line%d",
		 buf->ev_int.id);
	ret = iio_setup_ev_int(&(buf->ev_int),
			       buf->ev_int._name,
			       owner,
			       dev);
	if (ret)
		goto error_free_id;
	return 0;

error_free_id:
	iio_free_idr_val(&iio_ring_event_idr, buf->ev_int.id);
error_ret:
	return ret;
}

static inline void
__iio_free_ring_buffer_event_chrdev(struct iio_ring_buffer *buf)
{
	iio_free_ev_int(&(buf->ev_int));
	iio_free_idr_val(&iio_ring_event_idr, buf->ev_int.id);
}

static void iio_ring_access_release(struct device *dev)
{
	struct iio_ring_buffer *buf
		= access_dev_to_iio_ring_buffer(dev);
	cdev_del(&buf->access_handler.chrdev);
	iio_device_free_chrdev_minor(MINOR(dev->devt));
}

static struct device_type iio_ring_access_type = {
	.release = iio_ring_access_release,
};

static inline int
__iio_request_ring_buffer_access_chrdev(struct iio_ring_buffer *buf,
					int id,
					struct module *owner)
{
	int ret, minor;

	buf->access_handler.flags = 0;

	buf->access_dev.parent = &buf->dev;
	buf->access_dev.class = &iio_class;
	buf->access_dev.type = &iio_ring_access_type;
	device_initialize(&buf->access_dev);

	minor = iio_device_get_chrdev_minor();
	if (minor < 0) {
		ret = minor;
		goto error_device_put;
	}
	buf->access_dev.devt = MKDEV(MAJOR(iio_devt), minor);

	ret = iio_get_new_idr_val(&iio_ring_access_idr);
	if (ret < 0)
		goto error_device_put;
	else
		buf->access_id = ret;
	dev_set_name(&buf->access_dev, "ring_access%d", buf->access_id);
	ret = device_add(&buf->access_dev);
	if (ret < 0) {
		printk(KERN_ERR "failed to add the ring access dev\n");
		goto error_free_idr;
	}

	cdev_init(&buf->access_handler.chrdev, &iio_ring_fileops);
	buf->access_handler.chrdev.owner = owner;

	ret = cdev_add(&buf->access_handler.chrdev, buf->access_dev.devt, 1);
	if (ret) {
		printk(KERN_ERR "failed to allocate ring access chrdev\n");
		goto error_device_unregister;
	}
	return 0;
error_device_unregister:
	device_unregister(&buf->access_dev);
error_free_idr:
	iio_free_idr_val(&iio_ring_access_idr, buf->access_id);
error_device_put:
	put_device(&buf->access_dev);

	return ret;
}

static void __iio_free_ring_buffer_access_chrdev(struct iio_ring_buffer *buf)
{
	iio_free_idr_val(&iio_ring_access_idr, buf->access_id);
	device_unregister(&buf->access_dev);
}

void iio_ring_buffer_init(struct iio_ring_buffer *ring,
			  struct iio_dev *dev_info)
{
	if (ring->access.mark_param_change)
		ring->access.mark_param_change(ring);
	ring->indio_dev = dev_info;
	ring->ev_int.private = ring;
	ring->access_handler.private = ring;
}
EXPORT_SYMBOL(iio_ring_buffer_init);

int iio_ring_buffer_register(struct iio_ring_buffer *ring)
{
	int ret;
	ret = iio_get_new_idr_val(&iio_ring_idr);
	if (ret < 0)
		goto error_ret;
	else
		ring->id = ret;

	dev_set_name(&ring->dev, "ring_buffer%d", ring->id);
	ret = device_add(&ring->dev);
	if (ret)
		goto error_free_id;

	ret = __iio_request_ring_buffer_event_chrdev(ring,
						     0,
						     ring->owner,
						     &ring->dev);
	if (ret)
		goto error_remove_device;

	ret = __iio_request_ring_buffer_access_chrdev(ring,
						      0,
						      ring->owner);

	if (ret)
		goto error_free_ring_buffer_event_chrdev;

	return ret;
error_free_ring_buffer_event_chrdev:
	__iio_free_ring_buffer_event_chrdev(ring);
error_remove_device:
	device_del(&ring->dev);
error_free_id:
	iio_free_idr_val(&iio_ring_idr, ring->id);
error_ret:
	return ret;
}
EXPORT_SYMBOL(iio_ring_buffer_register);

void iio_ring_buffer_unregister(struct iio_ring_buffer *ring)
{
	__iio_free_ring_buffer_access_chrdev(ring);
	__iio_free_ring_buffer_event_chrdev(ring);
	device_del(&ring->dev);
	iio_free_idr_val(&iio_ring_idr, ring->id);
}
EXPORT_SYMBOL(iio_ring_buffer_unregister);

ssize_t iio_read_ring_length(struct device *dev,
			     struct device_attribute *attr,
			     char *buf)
{
	int len = 0;
	struct iio_ring_buffer *ring = dev_get_drvdata(dev);

	if (ring->access.get_length)
		len = sprintf(buf, "%d\n",
			      ring->access.get_length(ring));

	return len;
}
EXPORT_SYMBOL(iio_read_ring_length);

 ssize_t iio_write_ring_length(struct device *dev,
			       struct device_attribute *attr,
			       const char *buf,
			       size_t len)
{
	int ret;
	ulong val;
	struct iio_ring_buffer *ring = dev_get_drvdata(dev);
	ret = strict_strtoul(buf, 10, &val);
	if (ret)
		return ret;

	if (ring->access.get_length)
		if (val == ring->access.get_length(ring))
			return len;

	if (ring->access.set_length) {
		ring->access.set_length(ring, val);
		if (ring->access.mark_param_change)
			ring->access.mark_param_change(ring);
	}

	return len;
}
EXPORT_SYMBOL(iio_write_ring_length);

ssize_t iio_read_ring_bps(struct device *dev,
			  struct device_attribute *attr,
			  char *buf)
{
	int len = 0;
	struct iio_ring_buffer *ring = dev_get_drvdata(dev);

	if (ring->access.get_bpd)
		len = sprintf(buf, "%d\n",
			      ring->access.get_bpd(ring));

	return len;
}
EXPORT_SYMBOL(iio_read_ring_bps);

ssize_t iio_store_ring_enable(struct device *dev,
			      struct device_attribute *attr,
			      const char *buf,
			      size_t len)
{
	int ret;
	bool requested_state, current_state;
	int previous_mode;
	struct iio_ring_buffer *ring = dev_get_drvdata(dev);
	struct iio_dev *dev_info = ring->indio_dev;

	mutex_lock(&dev_info->mlock);
	previous_mode = dev_info->currentmode;
	requested_state = !(buf[0] == '0');
	current_state = !!(previous_mode & INDIO_ALL_RING_MODES);
	if (current_state == requested_state) {
		printk(KERN_INFO "iio-ring, current state requested again\n");
		goto done;
	}
	if (requested_state) {
		if (ring->preenable) {
			ret = ring->preenable(dev_info);
			if (ret) {
				printk(KERN_ERR
				       "Buffer not started:"
				       "ring preenable failed\n");
				goto error_ret;
			}
		}
		if (ring->access.request_update) {
			ret = ring->access.request_update(ring);
			if (ret) {
				printk(KERN_INFO
				       "Buffer not started:"
				       "ring parameter update failed\n");
				goto error_ret;
			}
		}
		if (ring->access.mark_in_use)
			ring->access.mark_in_use(ring);
		/* Definitely possible for devices to support both of these.*/
		if (dev_info->modes & INDIO_RING_TRIGGERED) {
			if (!dev_info->trig) {
				printk(KERN_INFO
				       "Buffer not started: no trigger\n");
				ret = -EINVAL;
				if (ring->access.unmark_in_use)
					ring->access.unmark_in_use(ring);
				goto error_ret;
			}
			dev_info->currentmode = INDIO_RING_TRIGGERED;
		} else if (dev_info->modes & INDIO_RING_HARDWARE_BUFFER)
			dev_info->currentmode = INDIO_RING_HARDWARE_BUFFER;
		else { /* should never be reached */
			ret = -EINVAL;
			goto error_ret;
		}

		if (ring->postenable) {

			ret = ring->postenable(dev_info);
			if (ret) {
				printk(KERN_INFO
				       "Buffer not started:"
				       "postenable failed\n");
				if (ring->access.unmark_in_use)
					ring->access.unmark_in_use(ring);
				dev_info->currentmode = previous_mode;
				if (ring->postdisable)
					ring->postdisable(dev_info);
				goto error_ret;
			}
		}
	} else {
		if (ring->predisable) {
			ret = ring->predisable(dev_info);
			if (ret)
				goto error_ret;
		}
		if (ring->access.unmark_in_use)
			ring->access.unmark_in_use(ring);
		dev_info->currentmode = INDIO_DIRECT_MODE;
		if (ring->postdisable) {
			ret = ring->postdisable(dev_info);
			if (ret)
				goto error_ret;
		}
	}
done:
	mutex_unlock(&dev_info->mlock);
	return len;

error_ret:
	mutex_unlock(&dev_info->mlock);
	return ret;
}
EXPORT_SYMBOL(iio_store_ring_enable);
ssize_t iio_show_ring_enable(struct device *dev,
				    struct device_attribute *attr,
				    char *buf)
{
	struct iio_ring_buffer *ring = dev_get_drvdata(dev);
	return sprintf(buf, "%d\n", !!(ring->indio_dev->currentmode
				       & INDIO_ALL_RING_MODES));
}
EXPORT_SYMBOL(iio_show_ring_enable);

ssize_t iio_scan_el_show(struct device *dev,
			 struct device_attribute *attr,
			 char *buf)
{
	int ret;
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct iio_scan_el *this_el = to_iio_scan_el(attr);

	ret = iio_scan_mask_query(indio_dev, this_el->number);
	if (ret < 0)
		return ret;
	return sprintf(buf, "%d\n", ret);
}
EXPORT_SYMBOL(iio_scan_el_show);

ssize_t iio_scan_el_store(struct device *dev,
			  struct device_attribute *attr,
			  const char *buf,
			  size_t len)
{
	int ret = 0;
	bool state;
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct iio_scan_el *this_el = to_iio_scan_el(attr);

	state = !(buf[0] == '0');
	mutex_lock(&indio_dev->mlock);
	if (indio_dev->currentmode == INDIO_RING_TRIGGERED) {
		ret = -EBUSY;
		goto error_ret;
	}
	ret = iio_scan_mask_query(indio_dev, this_el->number);
	if (ret < 0)
		goto error_ret;
	if (!state && ret) {
		ret = iio_scan_mask_clear(indio_dev, this_el->number);
		if (ret)
			goto error_ret;
		indio_dev->scan_count--;
	} else if (state && !ret) {
		ret = iio_scan_mask_set(indio_dev, this_el->number);
		if (ret)
			goto error_ret;
		indio_dev->scan_count++;
	}
	if (this_el->set_state)
		ret = this_el->set_state(this_el, indio_dev, state);
error_ret:
	mutex_unlock(&indio_dev->mlock);

	return ret ? ret : len;

}
EXPORT_SYMBOL(iio_scan_el_store);

ssize_t iio_scan_el_ts_show(struct device *dev,
			    struct device_attribute *attr,
			    char *buf)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	return sprintf(buf, "%d\n", indio_dev->scan_timestamp);
}
EXPORT_SYMBOL(iio_scan_el_ts_show);

ssize_t iio_scan_el_ts_store(struct device *dev,
			     struct device_attribute *attr,
			     const char *buf,
			     size_t len)
{
	int ret = 0;
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	bool state;
	state = !(buf[0] == '0');
	mutex_lock(&indio_dev->mlock);
	if (indio_dev->currentmode == INDIO_RING_TRIGGERED) {
		ret = -EBUSY;
		goto error_ret;
	}
	indio_dev->scan_timestamp = state;
error_ret:
	mutex_unlock(&indio_dev->mlock);

	return ret ? ret : len;
}
EXPORT_SYMBOL(iio_scan_el_ts_store);


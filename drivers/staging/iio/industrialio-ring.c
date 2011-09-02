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
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/slab.h>
#include <linux/poll.h>

#include "iio.h"
#include "iio_core.h"
#include "sysfs.h"
#include "ring_generic.h"

static const char * const iio_endian_prefix[] = {
	[IIO_BE] = "be",
	[IIO_LE] = "le",
};

/**
 * iio_ring_read_first_n_outer() - chrdev read for ring buffer access
 *
 * This function relies on all ring buffer implementations having an
 * iio_ring _bufer as their first element.
 **/
ssize_t iio_ring_read_first_n_outer(struct file *filp, char __user *buf,
				    size_t n, loff_t *f_ps)
{
	struct iio_dev *indio_dev = filp->private_data;
	struct iio_ring_buffer *rb = indio_dev->ring;

	if (!rb->access->read_first_n)
		return -EINVAL;
	return rb->access->read_first_n(rb, n, buf);
}

/**
 * iio_ring_poll() - poll the ring to find out if it has data
 */
unsigned int iio_ring_poll(struct file *filp,
			   struct poll_table_struct *wait)
{
	struct iio_dev *indio_dev = filp->private_data;
	struct iio_ring_buffer *rb = indio_dev->ring;

	poll_wait(filp, &rb->pollq, wait);
	if (rb->stufftoread)
		return POLLIN | POLLRDNORM;
	/* need a way of knowing if there may be enough data... */
	return 0;
}

void iio_chrdev_ring_open(struct iio_dev *indio_dev)
{
	struct iio_ring_buffer *rb = indio_dev->ring;
	if (rb && rb->access->mark_in_use)
		rb->access->mark_in_use(rb);
}

void iio_chrdev_ring_release(struct iio_dev *indio_dev)
{
	struct iio_ring_buffer *rb = indio_dev->ring;

	clear_bit(IIO_BUSY_BIT_POS, &rb->flags);
	if (rb->access->unmark_in_use)
		rb->access->unmark_in_use(rb);

}

void iio_ring_buffer_init(struct iio_ring_buffer *ring,
			  struct iio_dev *dev_info)
{
	ring->indio_dev = dev_info;
	init_waitqueue_head(&ring->pollq);
}
EXPORT_SYMBOL(iio_ring_buffer_init);

static ssize_t iio_show_scan_index(struct device *dev,
				   struct device_attribute *attr,
				   char *buf)
{
	return sprintf(buf, "%u\n", to_iio_dev_attr(attr)->c->scan_index);
}

static ssize_t iio_show_fixed_type(struct device *dev,
				   struct device_attribute *attr,
				   char *buf)
{
	struct iio_dev_attr *this_attr = to_iio_dev_attr(attr);
	u8 type = this_attr->c->scan_type.endianness;

	if (type == IIO_CPU) {
		if (__LITTLE_ENDIAN)
			type = IIO_LE;
		else
			type = IIO_BE;
	}
	return sprintf(buf, "%s:%c%d/%d>>%u\n",
		       iio_endian_prefix[type],
		       this_attr->c->scan_type.sign,
		       this_attr->c->scan_type.realbits,
		       this_attr->c->scan_type.storagebits,
		       this_attr->c->scan_type.shift);
}

static ssize_t iio_scan_el_show(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	int ret;
	struct iio_dev *dev_info = dev_get_drvdata(dev);

	ret = iio_scan_mask_query(dev_info->ring,
				  to_iio_dev_attr(attr)->address);
	if (ret < 0)
		return ret;
	return sprintf(buf, "%d\n", ret);
}

static int iio_scan_mask_clear(struct iio_ring_buffer *ring, int bit)
{
	clear_bit(bit, ring->scan_mask);
	ring->scan_count--;
	return 0;
}

static ssize_t iio_scan_el_store(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf,
				 size_t len)
{
	int ret = 0;
	bool state;
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct iio_ring_buffer *ring = indio_dev->ring;
	struct iio_dev_attr *this_attr = to_iio_dev_attr(attr);

	state = !(buf[0] == '0');
	mutex_lock(&indio_dev->mlock);
	if (indio_dev->currentmode == INDIO_RING_TRIGGERED) {
		ret = -EBUSY;
		goto error_ret;
	}
	ret = iio_scan_mask_query(ring, this_attr->address);
	if (ret < 0)
		goto error_ret;
	if (!state && ret) {
		ret = iio_scan_mask_clear(ring, this_attr->address);
		if (ret)
			goto error_ret;
	} else if (state && !ret) {
		ret = iio_scan_mask_set(ring, this_attr->address);
		if (ret)
			goto error_ret;
	}

error_ret:
	mutex_unlock(&indio_dev->mlock);

	return ret ? ret : len;

}

static ssize_t iio_scan_el_ts_show(struct device *dev,
				   struct device_attribute *attr,
				   char *buf)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	return sprintf(buf, "%d\n", dev_info->ring->scan_timestamp);
}

static ssize_t iio_scan_el_ts_store(struct device *dev,
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
	indio_dev->ring->scan_timestamp = state;
error_ret:
	mutex_unlock(&indio_dev->mlock);

	return ret ? ret : len;
}

static int iio_ring_add_channel_sysfs(struct iio_dev *indio_dev,
				      const struct iio_chan_spec *chan)
{
	int ret;
	struct iio_ring_buffer *ring = indio_dev->ring;

	ret = __iio_add_chan_devattr("index", "scan_elements",
				     chan,
				     &iio_show_scan_index,
				     NULL,
				     0,
				     0,
				     &indio_dev->dev,
				     &ring->scan_el_dev_attr_list);
	if (ret)
		goto error_ret;

	ret = __iio_add_chan_devattr("type", "scan_elements",
				     chan,
				     &iio_show_fixed_type,
				     NULL,
				     0,
				     0,
				     &indio_dev->dev,
				     &ring->scan_el_dev_attr_list);
	if (ret)
		goto error_ret;

	if (chan->type != IIO_TIMESTAMP)
		ret = __iio_add_chan_devattr("en", "scan_elements",
					     chan,
					     &iio_scan_el_show,
					     &iio_scan_el_store,
					     chan->scan_index,
					     0,
					     &indio_dev->dev,
					     &ring->scan_el_dev_attr_list);
	else
		ret = __iio_add_chan_devattr("en", "scan_elements",
					     chan,
					     &iio_scan_el_ts_show,
					     &iio_scan_el_ts_store,
					     chan->scan_index,
					     0,
					     &indio_dev->dev,
					     &ring->scan_el_dev_attr_list);
error_ret:
	return ret;
}

static void iio_ring_remove_and_free_scan_dev_attr(struct iio_dev *indio_dev,
						   struct iio_dev_attr *p)
{
	sysfs_remove_file_from_group(&indio_dev->dev.kobj,
				     &p->dev_attr.attr, "scan_elements");
	kfree(p->dev_attr.attr.name);
	kfree(p);
}

static struct attribute *iio_scan_el_dummy_attrs[] = {
	NULL
};

static struct attribute_group iio_scan_el_dummy_group = {
	.name = "scan_elements",
	.attrs = iio_scan_el_dummy_attrs
};

static void __iio_ring_attr_cleanup(struct iio_dev *indio_dev)
{
	struct iio_dev_attr *p, *n;
	struct iio_ring_buffer *ring = indio_dev->ring;
	int anydynamic = !list_empty(&ring->scan_el_dev_attr_list);
	list_for_each_entry_safe(p, n,
				 &ring->scan_el_dev_attr_list, l)
		iio_ring_remove_and_free_scan_dev_attr(indio_dev, p);

	if (ring->scan_el_attrs)
		sysfs_remove_group(&indio_dev->dev.kobj,
				   ring->scan_el_attrs);
	else if (anydynamic)
		sysfs_remove_group(&indio_dev->dev.kobj,
				   &iio_scan_el_dummy_group);
}

int iio_ring_buffer_register(struct iio_dev *indio_dev,
			     const struct iio_chan_spec *channels,
			     int num_channels)
{
	struct iio_ring_buffer *ring = indio_dev->ring;
	int ret, i;

	if (ring->scan_el_attrs) {
		ret = sysfs_create_group(&indio_dev->dev.kobj,
					 ring->scan_el_attrs);
		if (ret) {
			dev_err(&indio_dev->dev,
				"Failed to add sysfs scan elements\n");
			goto error_ret;
		}
	} else if (channels) {
		ret = sysfs_create_group(&indio_dev->dev.kobj,
					 &iio_scan_el_dummy_group);
		if (ret)
			goto error_ret;
	}
	if (ring->attrs) {
		ret = sysfs_create_group(&indio_dev->dev.kobj,
					 ring->attrs);
		if (ret)
			goto error_cleanup_dynamic;
	}

	INIT_LIST_HEAD(&ring->scan_el_dev_attr_list);
	if (channels) {
		/* new magic */
		for (i = 0; i < num_channels; i++) {
			/* Establish necessary mask length */
			if (channels[i].scan_index >
			    (int)indio_dev->masklength - 1)
				indio_dev->masklength
					= indio_dev->channels[i].scan_index + 1;

			ret = iio_ring_add_channel_sysfs(indio_dev,
							 &channels[i]);
			if (ret < 0)
				goto error_cleanup_group;
		}
		if (indio_dev->masklength && ring->scan_mask == NULL) {
			ring->scan_mask
				= kzalloc(sizeof(*ring->scan_mask)*
					  BITS_TO_LONGS(indio_dev->masklength),
					  GFP_KERNEL);
			if (ring->scan_mask == NULL) {
				ret = -ENOMEM;
				goto error_cleanup_group;
			}
		}
	}

	return 0;
error_cleanup_group:
	if (ring->attrs)
		sysfs_remove_group(&indio_dev->dev.kobj, ring->attrs);
error_cleanup_dynamic:
	__iio_ring_attr_cleanup(indio_dev);
error_ret:
	return ret;
}
EXPORT_SYMBOL(iio_ring_buffer_register);

void iio_ring_buffer_unregister(struct iio_dev *indio_dev)
{
	kfree(indio_dev->ring->scan_mask);
	if (indio_dev->ring->attrs)
		sysfs_remove_group(&indio_dev->dev.kobj,
				   indio_dev->ring->attrs);
	__iio_ring_attr_cleanup(indio_dev);
}
EXPORT_SYMBOL(iio_ring_buffer_unregister);

ssize_t iio_read_ring_length(struct device *dev,
			     struct device_attribute *attr,
			     char *buf)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct iio_ring_buffer *ring = indio_dev->ring;

	if (ring->access->get_length)
		return sprintf(buf, "%d\n",
			       ring->access->get_length(ring));

	return 0;
}
EXPORT_SYMBOL(iio_read_ring_length);

ssize_t iio_write_ring_length(struct device *dev,
			       struct device_attribute *attr,
			       const char *buf,
			       size_t len)
{
	int ret;
	ulong val;
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct iio_ring_buffer *ring = indio_dev->ring;

	ret = strict_strtoul(buf, 10, &val);
	if (ret)
		return ret;

	if (ring->access->get_length)
		if (val == ring->access->get_length(ring))
			return len;

	if (ring->access->set_length) {
		ring->access->set_length(ring, val);
		if (ring->access->mark_param_change)
			ring->access->mark_param_change(ring);
	}

	return len;
}
EXPORT_SYMBOL(iio_write_ring_length);

ssize_t iio_read_ring_bytes_per_datum(struct device *dev,
			  struct device_attribute *attr,
			  char *buf)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct iio_ring_buffer *ring = indio_dev->ring;

	if (ring->access->get_bytes_per_datum)
		return sprintf(buf, "%d\n",
			       ring->access->get_bytes_per_datum(ring));

	return 0;
}
EXPORT_SYMBOL(iio_read_ring_bytes_per_datum);

ssize_t iio_store_ring_enable(struct device *dev,
			      struct device_attribute *attr,
			      const char *buf,
			      size_t len)
{
	int ret;
	bool requested_state, current_state;
	int previous_mode;
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct iio_ring_buffer *ring = dev_info->ring;

	mutex_lock(&dev_info->mlock);
	previous_mode = dev_info->currentmode;
	requested_state = !(buf[0] == '0');
	current_state = !!(previous_mode & INDIO_ALL_RING_MODES);
	if (current_state == requested_state) {
		printk(KERN_INFO "iio-ring, current state requested again\n");
		goto done;
	}
	if (requested_state) {
		if (ring->setup_ops->preenable) {
			ret = ring->setup_ops->preenable(dev_info);
			if (ret) {
				printk(KERN_ERR
				       "Buffer not started:"
				       "ring preenable failed\n");
				goto error_ret;
			}
		}
		if (ring->access->request_update) {
			ret = ring->access->request_update(ring);
			if (ret) {
				printk(KERN_INFO
				       "Buffer not started:"
				       "ring parameter update failed\n");
				goto error_ret;
			}
		}
		if (ring->access->mark_in_use)
			ring->access->mark_in_use(ring);
		/* Definitely possible for devices to support both of these.*/
		if (dev_info->modes & INDIO_RING_TRIGGERED) {
			if (!dev_info->trig) {
				printk(KERN_INFO
				       "Buffer not started: no trigger\n");
				ret = -EINVAL;
				if (ring->access->unmark_in_use)
					ring->access->unmark_in_use(ring);
				goto error_ret;
			}
			dev_info->currentmode = INDIO_RING_TRIGGERED;
		} else if (dev_info->modes & INDIO_RING_HARDWARE_BUFFER)
			dev_info->currentmode = INDIO_RING_HARDWARE_BUFFER;
		else { /* should never be reached */
			ret = -EINVAL;
			goto error_ret;
		}

		if (ring->setup_ops->postenable) {
			ret = ring->setup_ops->postenable(dev_info);
			if (ret) {
				printk(KERN_INFO
				       "Buffer not started:"
				       "postenable failed\n");
				if (ring->access->unmark_in_use)
					ring->access->unmark_in_use(ring);
				dev_info->currentmode = previous_mode;
				if (ring->setup_ops->postdisable)
					ring->setup_ops->postdisable(dev_info);
				goto error_ret;
			}
		}
	} else {
		if (ring->setup_ops->predisable) {
			ret = ring->setup_ops->predisable(dev_info);
			if (ret)
				goto error_ret;
		}
		if (ring->access->unmark_in_use)
			ring->access->unmark_in_use(ring);
		dev_info->currentmode = INDIO_DIRECT_MODE;
		if (ring->setup_ops->postdisable) {
			ret = ring->setup_ops->postdisable(dev_info);
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
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	return sprintf(buf, "%d\n", !!(dev_info->currentmode
				       & INDIO_ALL_RING_MODES));
}
EXPORT_SYMBOL(iio_show_ring_enable);

int iio_sw_ring_preenable(struct iio_dev *indio_dev)
{
	struct iio_ring_buffer *ring = indio_dev->ring;
	size_t size;
	dev_dbg(&indio_dev->dev, "%s\n", __func__);
	/* Check if there are any scan elements enabled, if not fail*/
	if (!(ring->scan_count || ring->scan_timestamp))
		return -EINVAL;
	if (ring->scan_timestamp)
		if (ring->scan_count)
			/* Timestamp (aligned to s64) and data */
			size = (((ring->scan_count * ring->bpe)
					+ sizeof(s64) - 1)
				& ~(sizeof(s64) - 1))
				+ sizeof(s64);
		else /* Timestamp only  */
			size = sizeof(s64);
	else /* Data only */
		size = ring->scan_count * ring->bpe;
	ring->access->set_bytes_per_datum(ring, size);

	return 0;
}
EXPORT_SYMBOL(iio_sw_ring_preenable);


/* note NULL used as error indicator as it doesn't make sense. */
static unsigned long *iio_scan_mask_match(unsigned long *av_masks,
					  unsigned int masklength,
					  unsigned long *mask)
{
	if (bitmap_empty(mask, masklength))
		return NULL;
	while (*av_masks) {
		if (bitmap_subset(mask, av_masks, masklength))
			return av_masks;
		av_masks += BITS_TO_LONGS(masklength);
	}
	return NULL;
}

/**
 * iio_scan_mask_set() - set particular bit in the scan mask
 * @ring: the ring buffer whose scan mask we are interested in
 * @bit: the bit to be set.
 **/
int iio_scan_mask_set(struct iio_ring_buffer *ring, int bit)
{
	struct iio_dev *dev_info = ring->indio_dev;
	unsigned long *mask;
	unsigned long *trialmask;

	trialmask = kmalloc(sizeof(*trialmask)*
			    BITS_TO_LONGS(dev_info->masklength),
			    GFP_KERNEL);

	if (trialmask == NULL)
		return -ENOMEM;
	if (!dev_info->masklength) {
		WARN_ON("trying to set scan mask prior to registering ring\n");
		kfree(trialmask);
		return -EINVAL;
	}
	bitmap_copy(trialmask, ring->scan_mask, dev_info->masklength);
	set_bit(bit, trialmask);

	if (dev_info->available_scan_masks) {
		mask = iio_scan_mask_match(dev_info->available_scan_masks,
					   dev_info->masklength,
					   trialmask);
		if (!mask) {
			kfree(trialmask);
			return -EINVAL;
		}
	}
	bitmap_copy(ring->scan_mask, trialmask, dev_info->masklength);
	ring->scan_count++;

	kfree(trialmask);

	return 0;
};
EXPORT_SYMBOL_GPL(iio_scan_mask_set);

int iio_scan_mask_query(struct iio_ring_buffer *ring, int bit)
{
	struct iio_dev *dev_info = ring->indio_dev;
	long *mask;

	if (bit > dev_info->masklength)
		return -EINVAL;

	if (!ring->scan_mask)
		return 0;
	if (dev_info->available_scan_masks)
		mask = iio_scan_mask_match(dev_info->available_scan_masks,
					   dev_info->masklength,
					   ring->scan_mask);
	else
		mask = ring->scan_mask;
	if (!mask)
		return 0;

	return test_bit(bit, mask);
};
EXPORT_SYMBOL_GPL(iio_scan_mask_query);

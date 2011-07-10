/* The industrial I/O core
 *
 * Copyright (c) 2008 Jonathan Cameron
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * Handling of buffer allocation / resizing.
 *
 *
 * Things to look at here.
 * - Better memory allocation techniques?
 * - Alternative access techniques?
 */
#include <linux/kernel.h>
#include <linux/export.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/slab.h>
#include <linux/poll.h>

#include "iio.h"
#include "iio_core.h"
#include "sysfs.h"
#include "buffer_generic.h"

static const char * const iio_endian_prefix[] = {
	[IIO_BE] = "be",
	[IIO_LE] = "le",
};

/**
 * iio_buffer_read_first_n_outer() - chrdev read for buffer access
 *
 * This function relies on all buffer implementations having an
 * iio_buffer as their first element.
 **/
ssize_t iio_buffer_read_first_n_outer(struct file *filp, char __user *buf,
				      size_t n, loff_t *f_ps)
{
	struct iio_dev *indio_dev = filp->private_data;
	struct iio_buffer *rb = indio_dev->buffer;

	if (!rb->access->read_first_n)
		return -EINVAL;
	return rb->access->read_first_n(rb, n, buf);
}

/**
 * iio_buffer_poll() - poll the buffer to find out if it has data
 */
unsigned int iio_buffer_poll(struct file *filp,
			     struct poll_table_struct *wait)
{
	struct iio_dev *indio_dev = filp->private_data;
	struct iio_buffer *rb = indio_dev->buffer;

	poll_wait(filp, &rb->pollq, wait);
	if (rb->stufftoread)
		return POLLIN | POLLRDNORM;
	/* need a way of knowing if there may be enough data... */
	return 0;
}

int iio_chrdev_buffer_open(struct iio_dev *indio_dev)
{
	struct iio_buffer *rb = indio_dev->buffer;
	if (!rb)
		return -EINVAL;
	if (rb->access->mark_in_use)
		rb->access->mark_in_use(rb);
	return 0;
}

void iio_chrdev_buffer_release(struct iio_dev *indio_dev)
{
	struct iio_buffer *rb = indio_dev->buffer;

	clear_bit(IIO_BUSY_BIT_POS, &rb->flags);
	if (rb->access->unmark_in_use)
		rb->access->unmark_in_use(rb);
}

void iio_buffer_init(struct iio_buffer *buffer, struct iio_dev *indio_dev)
{
	buffer->indio_dev = indio_dev;
	init_waitqueue_head(&buffer->pollq);
}
EXPORT_SYMBOL(iio_buffer_init);

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
#ifdef __LITTLE_ENDIAN
		type = IIO_LE;
#else
		type = IIO_BE;
#endif
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
	struct iio_dev *indio_dev = dev_get_drvdata(dev);

	ret = iio_scan_mask_query(indio_dev->buffer,
				  to_iio_dev_attr(attr)->address);
	if (ret < 0)
		return ret;
	return sprintf(buf, "%d\n", ret);
}

static int iio_scan_mask_clear(struct iio_buffer *buffer, int bit)
{
	clear_bit(bit, buffer->scan_mask);
	buffer->scan_count--;
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
	struct iio_buffer *buffer = indio_dev->buffer;
	struct iio_dev_attr *this_attr = to_iio_dev_attr(attr);

	state = !(buf[0] == '0');
	mutex_lock(&indio_dev->mlock);
	if (indio_dev->currentmode == INDIO_BUFFER_TRIGGERED) {
		ret = -EBUSY;
		goto error_ret;
	}
	ret = iio_scan_mask_query(buffer, this_attr->address);
	if (ret < 0)
		goto error_ret;
	if (!state && ret) {
		ret = iio_scan_mask_clear(buffer, this_attr->address);
		if (ret)
			goto error_ret;
	} else if (state && !ret) {
		ret = iio_scan_mask_set(buffer, this_attr->address);
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
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	return sprintf(buf, "%d\n", indio_dev->buffer->scan_timestamp);
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
	if (indio_dev->currentmode == INDIO_BUFFER_TRIGGERED) {
		ret = -EBUSY;
		goto error_ret;
	}
	indio_dev->buffer->scan_timestamp = state;
error_ret:
	mutex_unlock(&indio_dev->mlock);

	return ret ? ret : len;
}

static int iio_buffer_add_channel_sysfs(struct iio_dev *indio_dev,
					const struct iio_chan_spec *chan)
{
	int ret, attrcount = 0;
	struct iio_buffer *buffer = indio_dev->buffer;

	ret = __iio_add_chan_devattr("index",
				     chan,
				     &iio_show_scan_index,
				     NULL,
				     0,
				     0,
				     &indio_dev->dev,
				     &buffer->scan_el_dev_attr_list);
	if (ret)
		goto error_ret;
	attrcount++;
	ret = __iio_add_chan_devattr("type",
				     chan,
				     &iio_show_fixed_type,
				     NULL,
				     0,
				     0,
				     &indio_dev->dev,
				     &buffer->scan_el_dev_attr_list);
	if (ret)
		goto error_ret;
	attrcount++;
	if (chan->type != IIO_TIMESTAMP)
		ret = __iio_add_chan_devattr("en",
					     chan,
					     &iio_scan_el_show,
					     &iio_scan_el_store,
					     chan->scan_index,
					     0,
					     &indio_dev->dev,
					     &buffer->scan_el_dev_attr_list);
	else
		ret = __iio_add_chan_devattr("en",
					     chan,
					     &iio_scan_el_ts_show,
					     &iio_scan_el_ts_store,
					     chan->scan_index,
					     0,
					     &indio_dev->dev,
					     &buffer->scan_el_dev_attr_list);
	attrcount++;
	ret = attrcount;
error_ret:
	return ret;
}

static void iio_buffer_remove_and_free_scan_dev_attr(struct iio_dev *indio_dev,
						     struct iio_dev_attr *p)
{
	kfree(p->dev_attr.attr.name);
	kfree(p);
}

static void __iio_buffer_attr_cleanup(struct iio_dev *indio_dev)
{
	struct iio_dev_attr *p, *n;
	struct iio_buffer *buffer = indio_dev->buffer;

	list_for_each_entry_safe(p, n,
				 &buffer->scan_el_dev_attr_list, l)
		iio_buffer_remove_and_free_scan_dev_attr(indio_dev, p);
}

static const char * const iio_scan_elements_group_name = "scan_elements";

int iio_buffer_register(struct iio_dev *indio_dev,
			const struct iio_chan_spec *channels,
			int num_channels)
{
	struct iio_dev_attr *p;
	struct attribute **attr;
	struct iio_buffer *buffer = indio_dev->buffer;
	int ret, i, attrn, attrcount, attrcount_orig = 0;

	if (buffer->attrs)
		indio_dev->groups[indio_dev->groupcounter++] = buffer->attrs;

	if (buffer->scan_el_attrs != NULL) {
		attr = buffer->scan_el_attrs->attrs;
		while (*attr++ != NULL)
			attrcount_orig++;
	}
	attrcount = attrcount_orig;
	INIT_LIST_HEAD(&buffer->scan_el_dev_attr_list);
	if (channels) {
		/* new magic */
		for (i = 0; i < num_channels; i++) {
			/* Establish necessary mask length */
			if (channels[i].scan_index >
			    (int)indio_dev->masklength - 1)
				indio_dev->masklength
					= indio_dev->channels[i].scan_index + 1;

			ret = iio_buffer_add_channel_sysfs(indio_dev,
							 &channels[i]);
			if (ret < 0)
				goto error_cleanup_dynamic;
			attrcount += ret;
		}
		if (indio_dev->masklength && buffer->scan_mask == NULL) {
			buffer->scan_mask
				= kzalloc(sizeof(*buffer->scan_mask)*
					  BITS_TO_LONGS(indio_dev->masklength),
					  GFP_KERNEL);
			if (buffer->scan_mask == NULL) {
				ret = -ENOMEM;
				goto error_cleanup_dynamic;
			}
		}
	}

	buffer->scan_el_group.name = iio_scan_elements_group_name;

	buffer->scan_el_group.attrs
		= kzalloc(sizeof(buffer->scan_el_group.attrs[0])*
			  (attrcount + 1),
			  GFP_KERNEL);
	if (buffer->scan_el_group.attrs == NULL) {
		ret = -ENOMEM;
		goto error_free_scan_mask;
	}
	if (buffer->scan_el_attrs)
		memcpy(buffer->scan_el_group.attrs, buffer->scan_el_attrs,
		       sizeof(buffer->scan_el_group.attrs[0])*attrcount_orig);
	attrn = attrcount_orig;

	list_for_each_entry(p, &buffer->scan_el_dev_attr_list, l)
		buffer->scan_el_group.attrs[attrn++] = &p->dev_attr.attr;
	indio_dev->groups[indio_dev->groupcounter++] = &buffer->scan_el_group;

	return 0;

error_free_scan_mask:
	kfree(buffer->scan_mask);
error_cleanup_dynamic:
	__iio_buffer_attr_cleanup(indio_dev);

	return ret;
}
EXPORT_SYMBOL(iio_buffer_register);

void iio_buffer_unregister(struct iio_dev *indio_dev)
{
	kfree(indio_dev->buffer->scan_mask);
	kfree(indio_dev->buffer->scan_el_group.attrs);
	__iio_buffer_attr_cleanup(indio_dev);
}
EXPORT_SYMBOL(iio_buffer_unregister);

ssize_t iio_buffer_read_length(struct device *dev,
			       struct device_attribute *attr,
			       char *buf)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct iio_buffer *buffer = indio_dev->buffer;

	if (buffer->access->get_length)
		return sprintf(buf, "%d\n",
			       buffer->access->get_length(buffer));

	return 0;
}
EXPORT_SYMBOL(iio_buffer_read_length);

ssize_t iio_buffer_write_length(struct device *dev,
				struct device_attribute *attr,
				const char *buf,
				size_t len)
{
	int ret;
	ulong val;
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct iio_buffer *buffer = indio_dev->buffer;

	ret = strict_strtoul(buf, 10, &val);
	if (ret)
		return ret;

	if (buffer->access->get_length)
		if (val == buffer->access->get_length(buffer))
			return len;

	if (buffer->access->set_length) {
		buffer->access->set_length(buffer, val);
		if (buffer->access->mark_param_change)
			buffer->access->mark_param_change(buffer);
	}

	return len;
}
EXPORT_SYMBOL(iio_buffer_write_length);

ssize_t iio_buffer_read_bytes_per_datum(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct iio_buffer *buffer = indio_dev->buffer;

	if (buffer->access->get_bytes_per_datum)
		return sprintf(buf, "%d\n",
			       buffer->access->get_bytes_per_datum(buffer));

	return 0;
}
EXPORT_SYMBOL(iio_buffer_read_bytes_per_datum);

ssize_t iio_buffer_store_enable(struct device *dev,
				struct device_attribute *attr,
				const char *buf,
				size_t len)
{
	int ret;
	bool requested_state, current_state;
	int previous_mode;
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct iio_buffer *buffer = indio_dev->buffer;

	mutex_lock(&indio_dev->mlock);
	previous_mode = indio_dev->currentmode;
	requested_state = !(buf[0] == '0');
	current_state = !!(previous_mode & INDIO_ALL_BUFFER_MODES);
	if (current_state == requested_state) {
		printk(KERN_INFO "iio-buffer, current state requested again\n");
		goto done;
	}
	if (requested_state) {
		if (buffer->setup_ops->preenable) {
			ret = buffer->setup_ops->preenable(indio_dev);
			if (ret) {
				printk(KERN_ERR
				       "Buffer not started:"
				       "buffer preenable failed\n");
				goto error_ret;
			}
		}
		if (buffer->access->request_update) {
			ret = buffer->access->request_update(buffer);
			if (ret) {
				printk(KERN_INFO
				       "Buffer not started:"
				       "buffer parameter update failed\n");
				goto error_ret;
			}
		}
		if (buffer->access->mark_in_use)
			buffer->access->mark_in_use(buffer);
		/* Definitely possible for devices to support both of these.*/
		if (indio_dev->modes & INDIO_BUFFER_TRIGGERED) {
			if (!indio_dev->trig) {
				printk(KERN_INFO
				       "Buffer not started: no trigger\n");
				ret = -EINVAL;
				if (buffer->access->unmark_in_use)
					buffer->access->unmark_in_use(buffer);
				goto error_ret;
			}
			indio_dev->currentmode = INDIO_BUFFER_TRIGGERED;
		} else if (indio_dev->modes & INDIO_BUFFER_HARDWARE)
			indio_dev->currentmode = INDIO_BUFFER_HARDWARE;
		else { /* should never be reached */
			ret = -EINVAL;
			goto error_ret;
		}

		if (buffer->setup_ops->postenable) {
			ret = buffer->setup_ops->postenable(indio_dev);
			if (ret) {
				printk(KERN_INFO
				       "Buffer not started:"
				       "postenable failed\n");
				if (buffer->access->unmark_in_use)
					buffer->access->unmark_in_use(buffer);
				indio_dev->currentmode = previous_mode;
				if (buffer->setup_ops->postdisable)
					buffer->setup_ops->
						postdisable(indio_dev);
				goto error_ret;
			}
		}
	} else {
		if (buffer->setup_ops->predisable) {
			ret = buffer->setup_ops->predisable(indio_dev);
			if (ret)
				goto error_ret;
		}
		if (buffer->access->unmark_in_use)
			buffer->access->unmark_in_use(buffer);
		indio_dev->currentmode = INDIO_DIRECT_MODE;
		if (buffer->setup_ops->postdisable) {
			ret = buffer->setup_ops->postdisable(indio_dev);
			if (ret)
				goto error_ret;
		}
	}
done:
	mutex_unlock(&indio_dev->mlock);
	return len;

error_ret:
	mutex_unlock(&indio_dev->mlock);
	return ret;
}
EXPORT_SYMBOL(iio_buffer_store_enable);

ssize_t iio_buffer_show_enable(struct device *dev,
			       struct device_attribute *attr,
			       char *buf)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	return sprintf(buf, "%d\n", !!(indio_dev->currentmode
				       & INDIO_ALL_BUFFER_MODES));
}
EXPORT_SYMBOL(iio_buffer_show_enable);

int iio_sw_buffer_preenable(struct iio_dev *indio_dev)
{
	struct iio_buffer *buffer = indio_dev->buffer;
	size_t size;
	dev_dbg(&indio_dev->dev, "%s\n", __func__);
	/* Check if there are any scan elements enabled, if not fail*/
	if (!(buffer->scan_count || buffer->scan_timestamp))
		return -EINVAL;
	if (buffer->scan_timestamp)
		if (buffer->scan_count)
			/* Timestamp (aligned to s64) and data */
			size = (((buffer->scan_count * buffer->bpe)
					+ sizeof(s64) - 1)
				& ~(sizeof(s64) - 1))
				+ sizeof(s64);
		else /* Timestamp only  */
			size = sizeof(s64);
	else /* Data only */
		size = buffer->scan_count * buffer->bpe;
	buffer->access->set_bytes_per_datum(buffer, size);

	return 0;
}
EXPORT_SYMBOL(iio_sw_buffer_preenable);


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
 * @buffer: the buffer whose scan mask we are interested in
 * @bit: the bit to be set.
 **/
int iio_scan_mask_set(struct iio_buffer *buffer, int bit)
{
	struct iio_dev *indio_dev = buffer->indio_dev;
	unsigned long *mask;
	unsigned long *trialmask;

	trialmask = kmalloc(sizeof(*trialmask)*
			    BITS_TO_LONGS(indio_dev->masklength),
			    GFP_KERNEL);

	if (trialmask == NULL)
		return -ENOMEM;
	if (!indio_dev->masklength) {
		WARN_ON("trying to set scanmask prior to registering buffer\n");
		kfree(trialmask);
		return -EINVAL;
	}
	bitmap_copy(trialmask, buffer->scan_mask, indio_dev->masklength);
	set_bit(bit, trialmask);

	if (indio_dev->available_scan_masks) {
		mask = iio_scan_mask_match(indio_dev->available_scan_masks,
					   indio_dev->masklength,
					   trialmask);
		if (!mask) {
			kfree(trialmask);
			return -EINVAL;
		}
	}
	bitmap_copy(buffer->scan_mask, trialmask, indio_dev->masklength);
	buffer->scan_count++;

	kfree(trialmask);

	return 0;
};
EXPORT_SYMBOL_GPL(iio_scan_mask_set);

int iio_scan_mask_query(struct iio_buffer *buffer, int bit)
{
	struct iio_dev *indio_dev = buffer->indio_dev;
	long *mask;

	if (bit > indio_dev->masklength)
		return -EINVAL;

	if (!buffer->scan_mask)
		return 0;
	if (indio_dev->available_scan_masks)
		mask = iio_scan_mask_match(indio_dev->available_scan_masks,
					   indio_dev->masklength,
					   buffer->scan_mask);
	else
		mask = buffer->scan_mask;
	if (!mask)
		return 0;

	return test_bit(bit, mask);
};
EXPORT_SYMBOL_GPL(iio_scan_mask_query);

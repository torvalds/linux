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

#include <linux/iio/iio.h>
#include "iio_core.h"
#include <linux/iio/sysfs.h>
#include <linux/iio/buffer.h>

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

	if (!rb || !rb->access->read_first_n)
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

void iio_buffer_init(struct iio_buffer *buffer)
{
	INIT_LIST_HEAD(&buffer->demux_list);
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
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);

	ret = test_bit(to_iio_dev_attr(attr)->address,
		       indio_dev->buffer->scan_mask);

	return sprintf(buf, "%d\n", ret);
}

static int iio_scan_mask_clear(struct iio_buffer *buffer, int bit)
{
	clear_bit(bit, buffer->scan_mask);
	return 0;
}

static ssize_t iio_scan_el_store(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf,
				 size_t len)
{
	int ret;
	bool state;
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct iio_buffer *buffer = indio_dev->buffer;
	struct iio_dev_attr *this_attr = to_iio_dev_attr(attr);

	ret = strtobool(buf, &state);
	if (ret < 0)
		return ret;
	mutex_lock(&indio_dev->mlock);
	if (iio_buffer_enabled(indio_dev)) {
		ret = -EBUSY;
		goto error_ret;
	}
	ret = iio_scan_mask_query(indio_dev, buffer, this_attr->address);
	if (ret < 0)
		goto error_ret;
	if (!state && ret) {
		ret = iio_scan_mask_clear(buffer, this_attr->address);
		if (ret)
			goto error_ret;
	} else if (state && !ret) {
		ret = iio_scan_mask_set(indio_dev, buffer, this_attr->address);
		if (ret)
			goto error_ret;
	}

error_ret:
	mutex_unlock(&indio_dev->mlock);

	return ret < 0 ? ret : len;

}

static ssize_t iio_scan_el_ts_show(struct device *dev,
				   struct device_attribute *attr,
				   char *buf)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	return sprintf(buf, "%d\n", indio_dev->buffer->scan_timestamp);
}

static ssize_t iio_scan_el_ts_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf,
				    size_t len)
{
	int ret;
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	bool state;

	ret = strtobool(buf, &state);
	if (ret < 0)
		return ret;

	mutex_lock(&indio_dev->mlock);
	if (iio_buffer_enabled(indio_dev)) {
		ret = -EBUSY;
		goto error_ret;
	}
	indio_dev->buffer->scan_timestamp = state;
	indio_dev->scan_timestamp = state;
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
			if (channels[i].scan_index < 0)
				continue;

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
			if (channels[i].type == IIO_TIMESTAMP)
				indio_dev->scan_index_timestamp =
					channels[i].scan_index;
		}
		if (indio_dev->masklength && buffer->scan_mask == NULL) {
			buffer->scan_mask = kcalloc(BITS_TO_LONGS(indio_dev->masklength),
						    sizeof(*buffer->scan_mask),
						    GFP_KERNEL);
			if (buffer->scan_mask == NULL) {
				ret = -ENOMEM;
				goto error_cleanup_dynamic;
			}
		}
	}

	buffer->scan_el_group.name = iio_scan_elements_group_name;

	buffer->scan_el_group.attrs = kcalloc(attrcount + 1,
					      sizeof(buffer->scan_el_group.attrs[0]),
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
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
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
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct iio_buffer *buffer = indio_dev->buffer;

	ret = strict_strtoul(buf, 10, &val);
	if (ret)
		return ret;

	if (buffer->access->get_length)
		if (val == buffer->access->get_length(buffer))
			return len;

	mutex_lock(&indio_dev->mlock);
	if (iio_buffer_enabled(indio_dev)) {
		ret = -EBUSY;
	} else {
		if (buffer->access->set_length)
			buffer->access->set_length(buffer, val);
		ret = 0;
	}
	mutex_unlock(&indio_dev->mlock);

	return ret ? ret : len;
}
EXPORT_SYMBOL(iio_buffer_write_length);

ssize_t iio_buffer_store_enable(struct device *dev,
				struct device_attribute *attr,
				const char *buf,
				size_t len)
{
	int ret;
	bool requested_state, current_state;
	int previous_mode;
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct iio_buffer *buffer = indio_dev->buffer;

	mutex_lock(&indio_dev->mlock);
	previous_mode = indio_dev->currentmode;
	requested_state = !(buf[0] == '0');
	current_state = iio_buffer_enabled(indio_dev);
	if (current_state == requested_state) {
		printk(KERN_INFO "iio-buffer, current state requested again\n");
		goto done;
	}
	if (requested_state) {
		if (indio_dev->setup_ops->preenable) {
			ret = indio_dev->setup_ops->preenable(indio_dev);
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
		/* Definitely possible for devices to support both of these.*/
		if (indio_dev->modes & INDIO_BUFFER_TRIGGERED) {
			if (!indio_dev->trig) {
				printk(KERN_INFO
				       "Buffer not started: no trigger\n");
				ret = -EINVAL;
				goto error_ret;
			}
			indio_dev->currentmode = INDIO_BUFFER_TRIGGERED;
		} else if (indio_dev->modes & INDIO_BUFFER_HARDWARE)
			indio_dev->currentmode = INDIO_BUFFER_HARDWARE;
		else { /* should never be reached */
			ret = -EINVAL;
			goto error_ret;
		}

		if (indio_dev->setup_ops->postenable) {
			ret = indio_dev->setup_ops->postenable(indio_dev);
			if (ret) {
				printk(KERN_INFO
				       "Buffer not started:"
				       "postenable failed\n");
				indio_dev->currentmode = previous_mode;
				if (indio_dev->setup_ops->postdisable)
					indio_dev->setup_ops->
						postdisable(indio_dev);
				goto error_ret;
			}
		}
	} else {
		if (indio_dev->setup_ops->predisable) {
			ret = indio_dev->setup_ops->predisable(indio_dev);
			if (ret)
				goto error_ret;
		}
		indio_dev->currentmode = INDIO_DIRECT_MODE;
		if (indio_dev->setup_ops->postdisable) {
			ret = indio_dev->setup_ops->postdisable(indio_dev);
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
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	return sprintf(buf, "%d\n", iio_buffer_enabled(indio_dev));
}
EXPORT_SYMBOL(iio_buffer_show_enable);

/* note NULL used as error indicator as it doesn't make sense. */
static const unsigned long *iio_scan_mask_match(const unsigned long *av_masks,
					  unsigned int masklength,
					  const unsigned long *mask)
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

static int iio_compute_scan_bytes(struct iio_dev *indio_dev, const long *mask,
				  bool timestamp)
{
	const struct iio_chan_spec *ch;
	unsigned bytes = 0;
	int length, i;

	/* How much space will the demuxed element take? */
	for_each_set_bit(i, mask,
			 indio_dev->masklength) {
		ch = iio_find_channel_from_si(indio_dev, i);
		length = ch->scan_type.storagebits / 8;
		bytes = ALIGN(bytes, length);
		bytes += length;
	}
	if (timestamp) {
		ch = iio_find_channel_from_si(indio_dev,
					      indio_dev->scan_index_timestamp);
		length = ch->scan_type.storagebits / 8;
		bytes = ALIGN(bytes, length);
		bytes += length;
	}
	return bytes;
}

int iio_sw_buffer_preenable(struct iio_dev *indio_dev)
{
	struct iio_buffer *buffer = indio_dev->buffer;
	dev_dbg(&indio_dev->dev, "%s\n", __func__);

	/* How much space will the demuxed element take? */
	indio_dev->scan_bytes =
		iio_compute_scan_bytes(indio_dev, buffer->scan_mask,
				       buffer->scan_timestamp);
	buffer->access->set_bytes_per_datum(buffer, indio_dev->scan_bytes);

	/* What scan mask do we actually have ?*/
	if (indio_dev->available_scan_masks)
		indio_dev->active_scan_mask =
			iio_scan_mask_match(indio_dev->available_scan_masks,
					    indio_dev->masklength,
					    buffer->scan_mask);
	else
		indio_dev->active_scan_mask = buffer->scan_mask;

	if (indio_dev->active_scan_mask == NULL)
		return -EINVAL;

	iio_update_demux(indio_dev);

	if (indio_dev->info->update_scan_mode)
		return indio_dev->info
			->update_scan_mode(indio_dev,
					   indio_dev->active_scan_mask);
	return 0;
}
EXPORT_SYMBOL(iio_sw_buffer_preenable);

static bool iio_validate_scan_mask(struct iio_dev *indio_dev,
	const unsigned long *mask)
{
	if (!indio_dev->setup_ops->validate_scan_mask)
		return true;

	return indio_dev->setup_ops->validate_scan_mask(indio_dev, mask);
}

/**
 * iio_scan_mask_set() - set particular bit in the scan mask
 * @buffer: the buffer whose scan mask we are interested in
 * @bit: the bit to be set.
 **/
int iio_scan_mask_set(struct iio_dev *indio_dev,
		      struct iio_buffer *buffer, int bit)
{
	const unsigned long *mask;
	unsigned long *trialmask;

	trialmask = kmalloc(sizeof(*trialmask)*
			    BITS_TO_LONGS(indio_dev->masklength),
			    GFP_KERNEL);

	if (trialmask == NULL)
		return -ENOMEM;
	if (!indio_dev->masklength) {
		WARN_ON("trying to set scanmask prior to registering buffer\n");
		goto err_invalid_mask;
	}
	bitmap_copy(trialmask, buffer->scan_mask, indio_dev->masklength);
	set_bit(bit, trialmask);

	if (!iio_validate_scan_mask(indio_dev, trialmask))
		goto err_invalid_mask;

	if (indio_dev->available_scan_masks) {
		mask = iio_scan_mask_match(indio_dev->available_scan_masks,
					   indio_dev->masklength,
					   trialmask);
		if (!mask)
			goto err_invalid_mask;
	}
	bitmap_copy(buffer->scan_mask, trialmask, indio_dev->masklength);

	kfree(trialmask);

	return 0;

err_invalid_mask:
	kfree(trialmask);
	return -EINVAL;
}
EXPORT_SYMBOL_GPL(iio_scan_mask_set);

int iio_scan_mask_query(struct iio_dev *indio_dev,
			struct iio_buffer *buffer, int bit)
{
	if (bit > indio_dev->masklength)
		return -EINVAL;

	if (!buffer->scan_mask)
		return 0;

	return test_bit(bit, buffer->scan_mask);
};
EXPORT_SYMBOL_GPL(iio_scan_mask_query);

/**
 * struct iio_demux_table() - table describing demux memcpy ops
 * @from:	index to copy from
 * @to:	index to copy to
 * @length:	how many bytes to copy
 * @l:		list head used for management
 */
struct iio_demux_table {
	unsigned from;
	unsigned to;
	unsigned length;
	struct list_head l;
};

static unsigned char *iio_demux(struct iio_buffer *buffer,
				 unsigned char *datain)
{
	struct iio_demux_table *t;

	if (list_empty(&buffer->demux_list))
		return datain;
	list_for_each_entry(t, &buffer->demux_list, l)
		memcpy(buffer->demux_bounce + t->to,
		       datain + t->from, t->length);

	return buffer->demux_bounce;
}

int iio_push_to_buffer(struct iio_buffer *buffer, unsigned char *data,
		       s64 timestamp)
{
	unsigned char *dataout = iio_demux(buffer, data);

	return buffer->access->store_to(buffer, dataout, timestamp);
}
EXPORT_SYMBOL_GPL(iio_push_to_buffer);

static void iio_buffer_demux_free(struct iio_buffer *buffer)
{
	struct iio_demux_table *p, *q;
	list_for_each_entry_safe(p, q, &buffer->demux_list, l) {
		list_del(&p->l);
		kfree(p);
	}
}

int iio_update_demux(struct iio_dev *indio_dev)
{
	const struct iio_chan_spec *ch;
	struct iio_buffer *buffer = indio_dev->buffer;
	int ret, in_ind = -1, out_ind, length;
	unsigned in_loc = 0, out_loc = 0;
	struct iio_demux_table *p;

	/* Clear out any old demux */
	iio_buffer_demux_free(buffer);
	kfree(buffer->demux_bounce);
	buffer->demux_bounce = NULL;

	/* First work out which scan mode we will actually have */
	if (bitmap_equal(indio_dev->active_scan_mask,
			 buffer->scan_mask,
			 indio_dev->masklength))
		return 0;

	/* Now we have the two masks, work from least sig and build up sizes */
	for_each_set_bit(out_ind,
			 indio_dev->active_scan_mask,
			 indio_dev->masklength) {
		in_ind = find_next_bit(indio_dev->active_scan_mask,
				       indio_dev->masklength,
				       in_ind + 1);
		while (in_ind != out_ind) {
			in_ind = find_next_bit(indio_dev->active_scan_mask,
					       indio_dev->masklength,
					       in_ind + 1);
			ch = iio_find_channel_from_si(indio_dev, in_ind);
			length = ch->scan_type.storagebits/8;
			/* Make sure we are aligned */
			in_loc += length;
			if (in_loc % length)
				in_loc += length - in_loc % length;
		}
		p = kmalloc(sizeof(*p), GFP_KERNEL);
		if (p == NULL) {
			ret = -ENOMEM;
			goto error_clear_mux_table;
		}
		ch = iio_find_channel_from_si(indio_dev, in_ind);
		length = ch->scan_type.storagebits/8;
		if (out_loc % length)
			out_loc += length - out_loc % length;
		if (in_loc % length)
			in_loc += length - in_loc % length;
		p->from = in_loc;
		p->to = out_loc;
		p->length = length;
		list_add_tail(&p->l, &buffer->demux_list);
		out_loc += length;
		in_loc += length;
	}
	/* Relies on scan_timestamp being last */
	if (buffer->scan_timestamp) {
		p = kmalloc(sizeof(*p), GFP_KERNEL);
		if (p == NULL) {
			ret = -ENOMEM;
			goto error_clear_mux_table;
		}
		ch = iio_find_channel_from_si(indio_dev,
			indio_dev->scan_index_timestamp);
		length = ch->scan_type.storagebits/8;
		if (out_loc % length)
			out_loc += length - out_loc % length;
		if (in_loc % length)
			in_loc += length - in_loc % length;
		p->from = in_loc;
		p->to = out_loc;
		p->length = length;
		list_add_tail(&p->l, &buffer->demux_list);
		out_loc += length;
		in_loc += length;
	}
	buffer->demux_bounce = kzalloc(out_loc, GFP_KERNEL);
	if (buffer->demux_bounce == NULL) {
		ret = -ENOMEM;
		goto error_clear_mux_table;
	}
	return 0;

error_clear_mux_table:
	iio_buffer_demux_free(buffer);

	return ret;
}
EXPORT_SYMBOL_GPL(iio_update_demux);

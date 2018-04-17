/* Industrial I/O event handling
 *
 * Copyright (c) 2008 Jonathan Cameron
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * Based on elements of hwmon and input subsystems.
 */

#include <linux/anon_inodes.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/kfifo.h>
#include <linux/module.h>
#include <linux/poll.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/wait.h>
#include <linux/iio/iio.h>
#include "iio_core.h"
#include <linux/iio/sysfs.h>
#include <linux/iio/events.h>

/**
 * struct iio_event_interface - chrdev interface for an event line
 * @wait:		wait queue to allow blocking reads of events
 * @det_events:		list of detected events
 * @dev_attr_list:	list of event interface sysfs attribute
 * @flags:		file operations related flags including busy flag.
 * @group:		event interface sysfs attribute group
 * @read_lock:		lock to protect kfifo read operations
 */
struct iio_event_interface {
	wait_queue_head_t	wait;
	DECLARE_KFIFO(det_events, struct iio_event_data, 16);

	struct list_head	dev_attr_list;
	unsigned long		flags;
	struct attribute_group	group;
	struct mutex		read_lock;
};

bool iio_event_enabled(const struct iio_event_interface *ev_int)
{
	return !!test_bit(IIO_BUSY_BIT_POS, &ev_int->flags);
}

/**
 * iio_push_event() - try to add event to the list for userspace reading
 * @indio_dev:		IIO device structure
 * @ev_code:		What event
 * @timestamp:		When the event occurred
 *
 * Note: The caller must make sure that this function is not running
 * concurrently for the same indio_dev more than once.
 *
 * This function may be safely used as soon as a valid reference to iio_dev has
 * been obtained via iio_device_alloc(), but any events that are submitted
 * before iio_device_register() has successfully completed will be silently
 * discarded.
 **/
int iio_push_event(struct iio_dev *indio_dev, u64 ev_code, s64 timestamp)
{
	struct iio_event_interface *ev_int = indio_dev->event_interface;
	struct iio_event_data ev;
	int copied;

	if (!ev_int)
		return 0;

	/* Does anyone care? */
	if (iio_event_enabled(ev_int)) {

		ev.id = ev_code;
		ev.timestamp = timestamp;

		copied = kfifo_put(&ev_int->det_events, ev);
		if (copied != 0)
			wake_up_poll(&ev_int->wait, POLLIN);
	}

	return 0;
}
EXPORT_SYMBOL(iio_push_event);

/**
 * iio_event_poll() - poll the event queue to find out if it has data
 * @filep:	File structure pointer to identify the device
 * @wait:	Poll table pointer to add the wait queue on
 *
 * Return: (POLLIN | POLLRDNORM) if data is available for reading
 *	   or a negative error code on failure
 */
static __poll_t iio_event_poll(struct file *filep,
			     struct poll_table_struct *wait)
{
	struct iio_dev *indio_dev = filep->private_data;
	struct iio_event_interface *ev_int = indio_dev->event_interface;
	__poll_t events = 0;

	if (!indio_dev->info)
		return events;

	poll_wait(filep, &ev_int->wait, wait);

	if (!kfifo_is_empty(&ev_int->det_events))
		events = POLLIN | POLLRDNORM;

	return events;
}

static ssize_t iio_event_chrdev_read(struct file *filep,
				     char __user *buf,
				     size_t count,
				     loff_t *f_ps)
{
	struct iio_dev *indio_dev = filep->private_data;
	struct iio_event_interface *ev_int = indio_dev->event_interface;
	unsigned int copied;
	int ret;

	if (!indio_dev->info)
		return -ENODEV;

	if (count < sizeof(struct iio_event_data))
		return -EINVAL;

	do {
		if (kfifo_is_empty(&ev_int->det_events)) {
			if (filep->f_flags & O_NONBLOCK)
				return -EAGAIN;

			ret = wait_event_interruptible(ev_int->wait,
					!kfifo_is_empty(&ev_int->det_events) ||
					indio_dev->info == NULL);
			if (ret)
				return ret;
			if (indio_dev->info == NULL)
				return -ENODEV;
		}

		if (mutex_lock_interruptible(&ev_int->read_lock))
			return -ERESTARTSYS;
		ret = kfifo_to_user(&ev_int->det_events, buf, count, &copied);
		mutex_unlock(&ev_int->read_lock);

		if (ret)
			return ret;

		/*
		 * If we couldn't read anything from the fifo (a different
		 * thread might have been faster) we either return -EAGAIN if
		 * the file descriptor is non-blocking, otherwise we go back to
		 * sleep and wait for more data to arrive.
		 */
		if (copied == 0 && (filep->f_flags & O_NONBLOCK))
			return -EAGAIN;

	} while (copied == 0);

	return copied;
}

static int iio_event_chrdev_release(struct inode *inode, struct file *filep)
{
	struct iio_dev *indio_dev = filep->private_data;
	struct iio_event_interface *ev_int = indio_dev->event_interface;

	clear_bit(IIO_BUSY_BIT_POS, &ev_int->flags);

	iio_device_put(indio_dev);

	return 0;
}

static const struct file_operations iio_event_chrdev_fileops = {
	.read =  iio_event_chrdev_read,
	.poll =  iio_event_poll,
	.release = iio_event_chrdev_release,
	.owner = THIS_MODULE,
	.llseek = noop_llseek,
};

int iio_event_getfd(struct iio_dev *indio_dev)
{
	struct iio_event_interface *ev_int = indio_dev->event_interface;
	int fd;

	if (ev_int == NULL)
		return -ENODEV;

	fd = mutex_lock_interruptible(&indio_dev->mlock);
	if (fd)
		return fd;

	if (test_and_set_bit(IIO_BUSY_BIT_POS, &ev_int->flags)) {
		fd = -EBUSY;
		goto unlock;
	}

	iio_device_get(indio_dev);

	fd = anon_inode_getfd("iio:event", &iio_event_chrdev_fileops,
				indio_dev, O_RDONLY | O_CLOEXEC);
	if (fd < 0) {
		clear_bit(IIO_BUSY_BIT_POS, &ev_int->flags);
		iio_device_put(indio_dev);
	} else {
		kfifo_reset_out(&ev_int->det_events);
	}

unlock:
	mutex_unlock(&indio_dev->mlock);
	return fd;
}

static const char * const iio_ev_type_text[] = {
	[IIO_EV_TYPE_THRESH] = "thresh",
	[IIO_EV_TYPE_MAG] = "mag",
	[IIO_EV_TYPE_ROC] = "roc",
	[IIO_EV_TYPE_THRESH_ADAPTIVE] = "thresh_adaptive",
	[IIO_EV_TYPE_MAG_ADAPTIVE] = "mag_adaptive",
	[IIO_EV_TYPE_CHANGE] = "change",
};

static const char * const iio_ev_dir_text[] = {
	[IIO_EV_DIR_EITHER] = "either",
	[IIO_EV_DIR_RISING] = "rising",
	[IIO_EV_DIR_FALLING] = "falling"
};

static const char * const iio_ev_info_text[] = {
	[IIO_EV_INFO_ENABLE] = "en",
	[IIO_EV_INFO_VALUE] = "value",
	[IIO_EV_INFO_HYSTERESIS] = "hysteresis",
	[IIO_EV_INFO_PERIOD] = "period",
	[IIO_EV_INFO_HIGH_PASS_FILTER_3DB] = "high_pass_filter_3db",
	[IIO_EV_INFO_LOW_PASS_FILTER_3DB] = "low_pass_filter_3db",
};

static enum iio_event_direction iio_ev_attr_dir(struct iio_dev_attr *attr)
{
	return attr->c->event_spec[attr->address & 0xffff].dir;
}

static enum iio_event_type iio_ev_attr_type(struct iio_dev_attr *attr)
{
	return attr->c->event_spec[attr->address & 0xffff].type;
}

static enum iio_event_info iio_ev_attr_info(struct iio_dev_attr *attr)
{
	return (attr->address >> 16) & 0xffff;
}

static ssize_t iio_ev_state_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf,
				  size_t len)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct iio_dev_attr *this_attr = to_iio_dev_attr(attr);
	int ret;
	bool val;

	ret = strtobool(buf, &val);
	if (ret < 0)
		return ret;

	ret = indio_dev->info->write_event_config(indio_dev,
		this_attr->c, iio_ev_attr_type(this_attr),
		iio_ev_attr_dir(this_attr), val);

	return (ret < 0) ? ret : len;
}

static ssize_t iio_ev_state_show(struct device *dev,
				 struct device_attribute *attr,
				 char *buf)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct iio_dev_attr *this_attr = to_iio_dev_attr(attr);
	int val;

	val = indio_dev->info->read_event_config(indio_dev,
		this_attr->c, iio_ev_attr_type(this_attr),
		iio_ev_attr_dir(this_attr));
	if (val < 0)
		return val;
	else
		return sprintf(buf, "%d\n", val);
}

static ssize_t iio_ev_value_show(struct device *dev,
				 struct device_attribute *attr,
				 char *buf)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct iio_dev_attr *this_attr = to_iio_dev_attr(attr);
	int val, val2, val_arr[2];
	int ret;

	ret = indio_dev->info->read_event_value(indio_dev,
		this_attr->c, iio_ev_attr_type(this_attr),
		iio_ev_attr_dir(this_attr), iio_ev_attr_info(this_attr),
		&val, &val2);
	if (ret < 0)
		return ret;
	val_arr[0] = val;
	val_arr[1] = val2;
	return iio_format_value(buf, ret, 2, val_arr);
}

static ssize_t iio_ev_value_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf,
				  size_t len)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct iio_dev_attr *this_attr = to_iio_dev_attr(attr);
	int val, val2;
	int ret;

	if (!indio_dev->info->write_event_value)
		return -EINVAL;

	ret = iio_str_to_fixpoint(buf, 100000, &val, &val2);
	if (ret)
		return ret;
	ret = indio_dev->info->write_event_value(indio_dev,
		this_attr->c, iio_ev_attr_type(this_attr),
		iio_ev_attr_dir(this_attr), iio_ev_attr_info(this_attr),
		val, val2);
	if (ret < 0)
		return ret;

	return len;
}

static int iio_device_add_event(struct iio_dev *indio_dev,
	const struct iio_chan_spec *chan, unsigned int spec_index,
	enum iio_event_type type, enum iio_event_direction dir,
	enum iio_shared_by shared_by, const unsigned long *mask)
{
	ssize_t (*show)(struct device *, struct device_attribute *, char *);
	ssize_t (*store)(struct device *, struct device_attribute *,
		const char *, size_t);
	unsigned int attrcount = 0;
	unsigned int i;
	char *postfix;
	int ret;

	for_each_set_bit(i, mask, sizeof(*mask)*8) {
		if (i >= ARRAY_SIZE(iio_ev_info_text))
			return -EINVAL;
		if (dir != IIO_EV_DIR_NONE)
			postfix = kasprintf(GFP_KERNEL, "%s_%s_%s",
					iio_ev_type_text[type],
					iio_ev_dir_text[dir],
					iio_ev_info_text[i]);
		else
			postfix = kasprintf(GFP_KERNEL, "%s_%s",
					iio_ev_type_text[type],
					iio_ev_info_text[i]);
		if (postfix == NULL)
			return -ENOMEM;

		if (i == IIO_EV_INFO_ENABLE) {
			show = iio_ev_state_show;
			store = iio_ev_state_store;
		} else {
			show = iio_ev_value_show;
			store = iio_ev_value_store;
		}

		ret = __iio_add_chan_devattr(postfix, chan, show, store,
			 (i << 16) | spec_index, shared_by, &indio_dev->dev,
			&indio_dev->event_interface->dev_attr_list);
		kfree(postfix);

		if ((ret == -EBUSY) && (shared_by != IIO_SEPARATE))
			continue;

		if (ret)
			return ret;

		attrcount++;
	}

	return attrcount;
}

static int iio_device_add_event_sysfs(struct iio_dev *indio_dev,
	struct iio_chan_spec const *chan)
{
	int ret = 0, i, attrcount = 0;
	enum iio_event_direction dir;
	enum iio_event_type type;

	for (i = 0; i < chan->num_event_specs; i++) {
		type = chan->event_spec[i].type;
		dir = chan->event_spec[i].dir;

		ret = iio_device_add_event(indio_dev, chan, i, type, dir,
			IIO_SEPARATE, &chan->event_spec[i].mask_separate);
		if (ret < 0)
			return ret;
		attrcount += ret;

		ret = iio_device_add_event(indio_dev, chan, i, type, dir,
			IIO_SHARED_BY_TYPE,
			&chan->event_spec[i].mask_shared_by_type);
		if (ret < 0)
			return ret;
		attrcount += ret;

		ret = iio_device_add_event(indio_dev, chan, i, type, dir,
			IIO_SHARED_BY_DIR,
			&chan->event_spec[i].mask_shared_by_dir);
		if (ret < 0)
			return ret;
		attrcount += ret;

		ret = iio_device_add_event(indio_dev, chan, i, type, dir,
			IIO_SHARED_BY_ALL,
			&chan->event_spec[i].mask_shared_by_all);
		if (ret < 0)
			return ret;
		attrcount += ret;
	}
	ret = attrcount;
	return ret;
}

static inline int __iio_add_event_config_attrs(struct iio_dev *indio_dev)
{
	int j, ret, attrcount = 0;

	/* Dynamically created from the channels array */
	for (j = 0; j < indio_dev->num_channels; j++) {
		ret = iio_device_add_event_sysfs(indio_dev,
						 &indio_dev->channels[j]);
		if (ret < 0)
			return ret;
		attrcount += ret;
	}
	return attrcount;
}

static bool iio_check_for_dynamic_events(struct iio_dev *indio_dev)
{
	int j;

	for (j = 0; j < indio_dev->num_channels; j++) {
		if (indio_dev->channels[j].num_event_specs != 0)
			return true;
	}
	return false;
}

static void iio_setup_ev_int(struct iio_event_interface *ev_int)
{
	INIT_KFIFO(ev_int->det_events);
	init_waitqueue_head(&ev_int->wait);
	mutex_init(&ev_int->read_lock);
}

static const char *iio_event_group_name = "events";
int iio_device_register_eventset(struct iio_dev *indio_dev)
{
	struct iio_dev_attr *p;
	int ret = 0, attrcount_orig = 0, attrcount, attrn;
	struct attribute **attr;

	if (!(indio_dev->info->event_attrs ||
	      iio_check_for_dynamic_events(indio_dev)))
		return 0;

	indio_dev->event_interface =
		kzalloc(sizeof(struct iio_event_interface), GFP_KERNEL);
	if (indio_dev->event_interface == NULL)
		return -ENOMEM;

	INIT_LIST_HEAD(&indio_dev->event_interface->dev_attr_list);

	iio_setup_ev_int(indio_dev->event_interface);
	if (indio_dev->info->event_attrs != NULL) {
		attr = indio_dev->info->event_attrs->attrs;
		while (*attr++ != NULL)
			attrcount_orig++;
	}
	attrcount = attrcount_orig;
	if (indio_dev->channels) {
		ret = __iio_add_event_config_attrs(indio_dev);
		if (ret < 0)
			goto error_free_setup_event_lines;
		attrcount += ret;
	}

	indio_dev->event_interface->group.name = iio_event_group_name;
	indio_dev->event_interface->group.attrs = kcalloc(attrcount + 1,
							  sizeof(indio_dev->event_interface->group.attrs[0]),
							  GFP_KERNEL);
	if (indio_dev->event_interface->group.attrs == NULL) {
		ret = -ENOMEM;
		goto error_free_setup_event_lines;
	}
	if (indio_dev->info->event_attrs)
		memcpy(indio_dev->event_interface->group.attrs,
		       indio_dev->info->event_attrs->attrs,
		       sizeof(indio_dev->event_interface->group.attrs[0])
		       *attrcount_orig);
	attrn = attrcount_orig;
	/* Add all elements from the list. */
	list_for_each_entry(p,
			    &indio_dev->event_interface->dev_attr_list,
			    l)
		indio_dev->event_interface->group.attrs[attrn++] =
			&p->dev_attr.attr;
	indio_dev->groups[indio_dev->groupcounter++] =
		&indio_dev->event_interface->group;

	return 0;

error_free_setup_event_lines:
	iio_free_chan_devattr_list(&indio_dev->event_interface->dev_attr_list);
	kfree(indio_dev->event_interface);
	indio_dev->event_interface = NULL;
	return ret;
}

/**
 * iio_device_wakeup_eventset - Wakes up the event waitqueue
 * @indio_dev: The IIO device
 *
 * Wakes up the event waitqueue used for poll() and blocking read().
 * Should usually be called when the device is unregistered.
 */
void iio_device_wakeup_eventset(struct iio_dev *indio_dev)
{
	if (indio_dev->event_interface == NULL)
		return;
	wake_up(&indio_dev->event_interface->wait);
}

void iio_device_unregister_eventset(struct iio_dev *indio_dev)
{
	if (indio_dev->event_interface == NULL)
		return;
	iio_free_chan_devattr_list(&indio_dev->event_interface->dev_attr_list);
	kfree(indio_dev->event_interface->group.attrs);
	kfree(indio_dev->event_interface);
}

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
 */
struct iio_event_interface {
	wait_queue_head_t	wait;
	DECLARE_KFIFO(det_events, struct iio_event_data, 16);

	struct list_head	dev_attr_list;
	unsigned long		flags;
	struct attribute_group	group;
};

int iio_push_event(struct iio_dev *indio_dev, u64 ev_code, s64 timestamp)
{
	struct iio_event_interface *ev_int = indio_dev->event_interface;
	struct iio_event_data ev;
	unsigned long flags;
	int copied;

	/* Does anyone care? */
	spin_lock_irqsave(&ev_int->wait.lock, flags);
	if (test_bit(IIO_BUSY_BIT_POS, &ev_int->flags)) {

		ev.id = ev_code;
		ev.timestamp = timestamp;

		copied = kfifo_put(&ev_int->det_events, &ev);
		if (copied != 0)
			wake_up_locked_poll(&ev_int->wait, POLLIN);
	}
	spin_unlock_irqrestore(&ev_int->wait.lock, flags);

	return 0;
}
EXPORT_SYMBOL(iio_push_event);

/**
 * iio_event_poll() - poll the event queue to find out if it has data
 */
static unsigned int iio_event_poll(struct file *filep,
			     struct poll_table_struct *wait)
{
	struct iio_event_interface *ev_int = filep->private_data;
	unsigned int events = 0;

	poll_wait(filep, &ev_int->wait, wait);

	spin_lock_irq(&ev_int->wait.lock);
	if (!kfifo_is_empty(&ev_int->det_events))
		events = POLLIN | POLLRDNORM;
	spin_unlock_irq(&ev_int->wait.lock);

	return events;
}

static ssize_t iio_event_chrdev_read(struct file *filep,
				     char __user *buf,
				     size_t count,
				     loff_t *f_ps)
{
	struct iio_event_interface *ev_int = filep->private_data;
	unsigned int copied;
	int ret;

	if (count < sizeof(struct iio_event_data))
		return -EINVAL;

	spin_lock_irq(&ev_int->wait.lock);
	if (kfifo_is_empty(&ev_int->det_events)) {
		if (filep->f_flags & O_NONBLOCK) {
			ret = -EAGAIN;
			goto error_unlock;
		}
		/* Blocking on device; waiting for something to be there */
		ret = wait_event_interruptible_locked_irq(ev_int->wait,
					!kfifo_is_empty(&ev_int->det_events));
		if (ret)
			goto error_unlock;
		/* Single access device so no one else can get the data */
	}

	ret = kfifo_to_user(&ev_int->det_events, buf, count, &copied);

error_unlock:
	spin_unlock_irq(&ev_int->wait.lock);

	return ret ? ret : copied;
}

static int iio_event_chrdev_release(struct inode *inode, struct file *filep)
{
	struct iio_event_interface *ev_int = filep->private_data;

	spin_lock_irq(&ev_int->wait.lock);
	__clear_bit(IIO_BUSY_BIT_POS, &ev_int->flags);
	/*
	 * In order to maintain a clean state for reopening,
	 * clear out any awaiting events. The mask will prevent
	 * any new __iio_push_event calls running.
	 */
	kfifo_reset_out(&ev_int->det_events);
	spin_unlock_irq(&ev_int->wait.lock);

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

	spin_lock_irq(&ev_int->wait.lock);
	if (__test_and_set_bit(IIO_BUSY_BIT_POS, &ev_int->flags)) {
		spin_unlock_irq(&ev_int->wait.lock);
		return -EBUSY;
	}
	spin_unlock_irq(&ev_int->wait.lock);
	fd = anon_inode_getfd("iio:event",
				&iio_event_chrdev_fileops, ev_int, O_RDONLY | O_CLOEXEC);
	if (fd < 0) {
		spin_lock_irq(&ev_int->wait.lock);
		__clear_bit(IIO_BUSY_BIT_POS, &ev_int->flags);
		spin_unlock_irq(&ev_int->wait.lock);
	}
	return fd;
}

static const char * const iio_ev_type_text[] = {
	[IIO_EV_TYPE_THRESH] = "thresh",
	[IIO_EV_TYPE_MAG] = "mag",
	[IIO_EV_TYPE_ROC] = "roc",
	[IIO_EV_TYPE_THRESH_ADAPTIVE] = "thresh_adaptive",
	[IIO_EV_TYPE_MAG_ADAPTIVE] = "mag_adaptive",
};

static const char * const iio_ev_dir_text[] = {
	[IIO_EV_DIR_EITHER] = "either",
	[IIO_EV_DIR_RISING] = "rising",
	[IIO_EV_DIR_FALLING] = "falling"
};

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
						  this_attr->address,
						  val);
	return (ret < 0) ? ret : len;
}

static ssize_t iio_ev_state_show(struct device *dev,
				 struct device_attribute *attr,
				 char *buf)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct iio_dev_attr *this_attr = to_iio_dev_attr(attr);
	int val = indio_dev->info->read_event_config(indio_dev,
						     this_attr->address);

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
	int val, ret;

	ret = indio_dev->info->read_event_value(indio_dev,
						this_attr->address, &val);
	if (ret < 0)
		return ret;

	return sprintf(buf, "%d\n", val);
}

static ssize_t iio_ev_value_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf,
				  size_t len)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct iio_dev_attr *this_attr = to_iio_dev_attr(attr);
	int val;
	int ret;

	if (!indio_dev->info->write_event_value)
		return -EINVAL;

	ret = kstrtoint(buf, 10, &val);
	if (ret)
		return ret;

	ret = indio_dev->info->write_event_value(indio_dev, this_attr->address,
						 val);
	if (ret < 0)
		return ret;

	return len;
}

static int iio_device_add_event_sysfs(struct iio_dev *indio_dev,
				      struct iio_chan_spec const *chan)
{
	int ret = 0, i, attrcount = 0;
	u64 mask = 0;
	char *postfix;
	if (!chan->event_mask)
		return 0;

	for_each_set_bit(i, &chan->event_mask, sizeof(chan->event_mask)*8) {
		postfix = kasprintf(GFP_KERNEL, "%s_%s_en",
				    iio_ev_type_text[i/IIO_EV_DIR_MAX],
				    iio_ev_dir_text[i%IIO_EV_DIR_MAX]);
		if (postfix == NULL) {
			ret = -ENOMEM;
			goto error_ret;
		}
		if (chan->modified)
			mask = IIO_MOD_EVENT_CODE(chan->type, 0, chan->channel,
						  i/IIO_EV_DIR_MAX,
						  i%IIO_EV_DIR_MAX);
		else if (chan->differential)
			mask = IIO_EVENT_CODE(chan->type,
					      0, 0,
					      i%IIO_EV_DIR_MAX,
					      i/IIO_EV_DIR_MAX,
					      0,
					      chan->channel,
					      chan->channel2);
		else
			mask = IIO_UNMOD_EVENT_CODE(chan->type,
						    chan->channel,
						    i/IIO_EV_DIR_MAX,
						    i%IIO_EV_DIR_MAX);

		ret = __iio_add_chan_devattr(postfix,
					     chan,
					     &iio_ev_state_show,
					     iio_ev_state_store,
					     mask,
					     0,
					     &indio_dev->dev,
					     &indio_dev->event_interface->
					     dev_attr_list);
		kfree(postfix);
		if (ret)
			goto error_ret;
		attrcount++;
		postfix = kasprintf(GFP_KERNEL, "%s_%s_value",
				    iio_ev_type_text[i/IIO_EV_DIR_MAX],
				    iio_ev_dir_text[i%IIO_EV_DIR_MAX]);
		if (postfix == NULL) {
			ret = -ENOMEM;
			goto error_ret;
		}
		ret = __iio_add_chan_devattr(postfix, chan,
					     iio_ev_value_show,
					     iio_ev_value_store,
					     mask,
					     0,
					     &indio_dev->dev,
					     &indio_dev->event_interface->
					     dev_attr_list);
		kfree(postfix);
		if (ret)
			goto error_ret;
		attrcount++;
	}
	ret = attrcount;
error_ret:
	return ret;
}

static inline void __iio_remove_event_config_attrs(struct iio_dev *indio_dev)
{
	struct iio_dev_attr *p, *n;
	list_for_each_entry_safe(p, n,
				 &indio_dev->event_interface->
				 dev_attr_list, l) {
		kfree(p->dev_attr.attr.name);
		kfree(p);
	}
}

static inline int __iio_add_event_config_attrs(struct iio_dev *indio_dev)
{
	int j, ret, attrcount = 0;

	/* Dynically created from the channels array */
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

	for (j = 0; j < indio_dev->num_channels; j++)
		if (indio_dev->channels[j].event_mask != 0)
			return true;
	return false;
}

static void iio_setup_ev_int(struct iio_event_interface *ev_int)
{
	INIT_KFIFO(ev_int->det_events);
	init_waitqueue_head(&ev_int->wait);
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
	if (indio_dev->event_interface == NULL) {
		ret = -ENOMEM;
		goto error_ret;
	}

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
	__iio_remove_event_config_attrs(indio_dev);
	kfree(indio_dev->event_interface);
error_ret:

	return ret;
}

void iio_device_unregister_eventset(struct iio_dev *indio_dev)
{
	if (indio_dev->event_interface == NULL)
		return;
	__iio_remove_event_config_attrs(indio_dev);
	kfree(indio_dev->event_interface->group.attrs);
	kfree(indio_dev->event_interface);
}

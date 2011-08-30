/* The industrial I/O core
 *
 * Copyright (c) 2008 Jonathan Cameron
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * Based on elements of hwmon and input subsystems.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/idr.h>
#include <linux/kdev_t.h>
#include <linux/err.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/poll.h>
#include <linux/sched.h>
#include <linux/wait.h>
#include <linux/cdev.h>
#include <linux/slab.h>
#include <linux/anon_inodes.h>
#include "iio.h"
#include "iio_core.h"
#include "iio_core_trigger.h"
#include "chrdev.h"
/* IDA to assign each registered device a unique id*/
static DEFINE_IDA(iio_ida);

static dev_t iio_devt;

#define IIO_DEV_MAX 256
struct bus_type iio_bus_type = {
	.name = "iio",
};
EXPORT_SYMBOL(iio_bus_type);

static const char * const iio_chan_type_name_spec_shared[] = {
	[IIO_IN] = "in",
	[IIO_OUT] = "out",
	[IIO_CURRENT] = "current",
	[IIO_POWER] = "power",
	[IIO_ACCEL] = "accel",
	[IIO_IN_DIFF] = "in-in",
	[IIO_GYRO] = "gyro",
	[IIO_MAGN] = "magn",
	[IIO_LIGHT] = "illuminance",
	[IIO_INTENSITY] = "intensity",
	[IIO_PROXIMITY] = "proximity",
	[IIO_TEMP] = "temp",
	[IIO_INCLI] = "incli",
	[IIO_ROT] = "rot",
	[IIO_ANGL] = "angl",
	[IIO_TIMESTAMP] = "timestamp",
};

static const char * const iio_chan_type_name_spec_complex[] = {
	[IIO_IN_DIFF] = "in%d-in%d",
};

static const char * const iio_modifier_names_light[] = {
	[IIO_MOD_LIGHT_BOTH] = "both",
	[IIO_MOD_LIGHT_IR] = "ir",
};

static const char * const iio_modifier_names_axial[] = {
	[IIO_MOD_X] = "x",
	[IIO_MOD_Y] = "y",
	[IIO_MOD_Z] = "z",
};

/* relies on pairs of these shared then separate */
static const char * const iio_chan_info_postfix[] = {
	[IIO_CHAN_INFO_SCALE_SHARED/2] = "scale",
	[IIO_CHAN_INFO_OFFSET_SHARED/2] = "offset",
	[IIO_CHAN_INFO_CALIBSCALE_SHARED/2] = "calibscale",
	[IIO_CHAN_INFO_CALIBBIAS_SHARED/2] = "calibbias",
	[IIO_CHAN_INFO_PEAK_SHARED/2] = "peak_raw",
	[IIO_CHAN_INFO_PEAK_SCALE_SHARED/2] = "peak_scale",
	[IIO_CHAN_INFO_QUADRATURE_CORRECTION_RAW_SHARED/2]
	= "quadrature_correction_raw",
};

/**
 * struct iio_detected_event_list - list element for events that have occurred
 * @list:		linked list header
 * @ev:			the event itself
 */
struct iio_detected_event_list {
	struct list_head		list;
	struct iio_event_data		ev;
};

/**
 * struct iio_event_interface - chrdev interface for an event line
 * @dev:		device assocated with event interface
 * @wait:		wait queue to allow blocking reads of events
 * @event_list_lock:	mutex to protect the list of detected events
 * @det_events:		list of detected events
 * @max_events:		maximum number of events before new ones are dropped
 * @current_events:	number of events in detected list
 * @flags:		file operations related flags including busy flag.
 */
struct iio_event_interface {
	wait_queue_head_t			wait;
	struct mutex				event_list_lock;
	struct list_head			det_events;
	int					max_events;
	int					current_events;
	struct list_head dev_attr_list;
	unsigned long flags;
};

int iio_push_event(struct iio_dev *dev_info, int ev_code, s64 timestamp)
{
	struct iio_event_interface *ev_int = dev_info->event_interface;
	struct iio_detected_event_list *ev;
	int ret = 0;

	/* Does anyone care? */
	mutex_lock(&ev_int->event_list_lock);
	if (test_bit(IIO_BUSY_BIT_POS, &ev_int->flags)) {
		if (ev_int->current_events == ev_int->max_events) {
			mutex_unlock(&ev_int->event_list_lock);
			return 0;
		}
		ev = kmalloc(sizeof(*ev), GFP_KERNEL);
		if (ev == NULL) {
			ret = -ENOMEM;
			mutex_unlock(&ev_int->event_list_lock);
			goto error_ret;
		}
		ev->ev.id = ev_code;
		ev->ev.timestamp = timestamp;

		list_add_tail(&ev->list, &ev_int->det_events);
		ev_int->current_events++;
		mutex_unlock(&ev_int->event_list_lock);
		wake_up_interruptible(&ev_int->wait);
	} else
		mutex_unlock(&ev_int->event_list_lock);

error_ret:
	return ret;
}
EXPORT_SYMBOL(iio_push_event);


/* This turns up an awful lot */
ssize_t iio_read_const_attr(struct device *dev,
			    struct device_attribute *attr,
			    char *buf)
{
	return sprintf(buf, "%s\n", to_iio_const_attr(attr)->string);
}
EXPORT_SYMBOL(iio_read_const_attr);


static ssize_t iio_event_chrdev_read(struct file *filep,
				     char __user *buf,
				     size_t count,
				     loff_t *f_ps)
{
	struct iio_event_interface *ev_int = filep->private_data;
	struct iio_detected_event_list *el;
	int ret;
	size_t len;

	mutex_lock(&ev_int->event_list_lock);
	if (list_empty(&ev_int->det_events)) {
		if (filep->f_flags & O_NONBLOCK) {
			ret = -EAGAIN;
			goto error_mutex_unlock;
		}
		mutex_unlock(&ev_int->event_list_lock);
		/* Blocking on device; waiting for something to be there */
		ret = wait_event_interruptible(ev_int->wait,
					       !list_empty(&ev_int
							   ->det_events));
		if (ret)
			goto error_ret;
		/* Single access device so no one else can get the data */
		mutex_lock(&ev_int->event_list_lock);
	}

	el = list_first_entry(&ev_int->det_events,
			      struct iio_detected_event_list,
			      list);
	len = sizeof el->ev;
	if (copy_to_user(buf, &(el->ev), len)) {
		ret = -EFAULT;
		goto error_mutex_unlock;
	}
	list_del(&el->list);
	ev_int->current_events--;
	mutex_unlock(&ev_int->event_list_lock);
	kfree(el);

	return len;

error_mutex_unlock:
	mutex_unlock(&ev_int->event_list_lock);
error_ret:

	return ret;
}

static int iio_event_chrdev_release(struct inode *inode, struct file *filep)
{
	struct iio_event_interface *ev_int = filep->private_data;
	struct iio_detected_event_list *el, *t;

	mutex_lock(&ev_int->event_list_lock);
	clear_bit(IIO_BUSY_BIT_POS, &ev_int->flags);
	/*
	 * In order to maintain a clean state for reopening,
	 * clear out any awaiting events. The mask will prevent
	 * any new __iio_push_event calls running.
	 */
	list_for_each_entry_safe(el, t, &ev_int->det_events, list) {
		list_del(&el->list);
		kfree(el);
	}
	ev_int->current_events = 0;
	mutex_unlock(&ev_int->event_list_lock);

	return 0;
}

static const struct file_operations iio_event_chrdev_fileops = {
	.read =  iio_event_chrdev_read,
	.release = iio_event_chrdev_release,
	.owner = THIS_MODULE,
	.llseek = noop_llseek,
};

static int iio_event_getfd(struct iio_dev *indio_dev)
{
	if (indio_dev->event_interface == NULL)
		return -ENODEV;

	mutex_lock(&indio_dev->event_interface->event_list_lock);
	if (test_and_set_bit(IIO_BUSY_BIT_POS,
			     &indio_dev->event_interface->flags)) {
		mutex_unlock(&indio_dev->event_interface->event_list_lock);
		return -EBUSY;
	}
	mutex_unlock(&indio_dev->event_interface->event_list_lock);
	return anon_inode_getfd("iio:event",
				&iio_event_chrdev_fileops,
				indio_dev->event_interface, O_RDONLY);
}

static void iio_setup_ev_int(struct iio_event_interface *ev_int)
{
	mutex_init(&ev_int->event_list_lock);
	/* discussion point - make this variable? */
	ev_int->max_events = 10;
	ev_int->current_events = 0;
	INIT_LIST_HEAD(&ev_int->det_events);
	init_waitqueue_head(&ev_int->wait);
}

static int __init iio_init(void)
{
	int ret;

	/* Register sysfs bus */
	ret  = bus_register(&iio_bus_type);
	if (ret < 0) {
		printk(KERN_ERR
		       "%s could not register bus type\n",
			__FILE__);
		goto error_nothing;
	}

	ret = alloc_chrdev_region(&iio_devt, 0, IIO_DEV_MAX, "iio");
	if (ret < 0) {
		printk(KERN_ERR "%s: failed to allocate char dev region\n",
		       __FILE__);
		goto error_unregister_bus_type;
	}

	return 0;

error_unregister_bus_type:
	bus_unregister(&iio_bus_type);
error_nothing:
	return ret;
}

static void __exit iio_exit(void)
{
	if (iio_devt)
		unregister_chrdev_region(iio_devt, IIO_DEV_MAX);
	bus_unregister(&iio_bus_type);
}

static ssize_t iio_read_channel_info(struct device *dev,
				     struct device_attribute *attr,
				     char *buf)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct iio_dev_attr *this_attr = to_iio_dev_attr(attr);
	int val, val2;
	int ret = indio_dev->info->read_raw(indio_dev, this_attr->c,
					    &val, &val2, this_attr->address);

	if (ret < 0)
		return ret;

	if (ret == IIO_VAL_INT)
		return sprintf(buf, "%d\n", val);
	else if (ret == IIO_VAL_INT_PLUS_MICRO) {
		if (val2 < 0)
			return sprintf(buf, "-%d.%06u\n", val, -val2);
		else
			return sprintf(buf, "%d.%06u\n", val, val2);
	} else if (ret == IIO_VAL_INT_PLUS_NANO) {
		if (val2 < 0)
			return sprintf(buf, "-%d.%09u\n", val, -val2);
		else
			return sprintf(buf, "%d.%09u\n", val, val2);
	} else
		return 0;
}

static ssize_t iio_write_channel_info(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf,
				      size_t len)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct iio_dev_attr *this_attr = to_iio_dev_attr(attr);
	int ret, integer = 0, fract = 0, fract_mult = 100000;
	bool integer_part = true, negative = false;

	/* Assumes decimal - precision based on number of digits */
	if (!indio_dev->info->write_raw)
		return -EINVAL;

	if (indio_dev->info->write_raw_get_fmt)
		switch (indio_dev->info->write_raw_get_fmt(indio_dev,
			this_attr->c, this_attr->address)) {
		case IIO_VAL_INT_PLUS_MICRO:
			fract_mult = 100000;
			break;
		case IIO_VAL_INT_PLUS_NANO:
			fract_mult = 100000000;
			break;
		default:
			return -EINVAL;
		}

	if (buf[0] == '-') {
		negative = true;
		buf++;
	}

	while (*buf) {
		if ('0' <= *buf && *buf <= '9') {
			if (integer_part)
				integer = integer*10 + *buf - '0';
			else {
				fract += fract_mult*(*buf - '0');
				if (fract_mult == 1)
					break;
				fract_mult /= 10;
			}
		} else if (*buf == '\n') {
			if (*(buf + 1) == '\0')
				break;
			else
				return -EINVAL;
		} else if (*buf == '.') {
			integer_part = false;
		} else {
			return -EINVAL;
		}
		buf++;
	}
	if (negative) {
		if (integer)
			integer = -integer;
		else
			fract = -fract;
	}

	ret = indio_dev->info->write_raw(indio_dev, this_attr->c,
					 integer, fract, this_attr->address);
	if (ret)
		return ret;

	return len;
}

static int __iio_build_postfix(struct iio_chan_spec const *chan,
			       bool generic,
			       const char *postfix,
			       char **result)
{
	char *all_post;
	/* 3 options - generic, extend_name, modified - if generic, extend_name
	* and modified cannot apply.*/

	if (generic || (!chan->modified && !chan->extend_name)) {
		all_post = kasprintf(GFP_KERNEL, "%s", postfix);
	} else if (chan->modified) {
		const char *intermediate;
		switch (chan->type) {
		case IIO_INTENSITY:
			intermediate
				= iio_modifier_names_light[chan->channel2];
			break;
		case IIO_ACCEL:
		case IIO_GYRO:
		case IIO_MAGN:
		case IIO_INCLI:
		case IIO_ROT:
		case IIO_ANGL:
			intermediate
				= iio_modifier_names_axial[chan->channel2];
			break;
		default:
			return -EINVAL;
		}
		if (chan->extend_name)
			all_post = kasprintf(GFP_KERNEL, "%s_%s_%s",
					     intermediate,
					     chan->extend_name,
					     postfix);
		else
			all_post = kasprintf(GFP_KERNEL, "%s_%s",
					     intermediate,
					     postfix);
	} else
		all_post = kasprintf(GFP_KERNEL, "%s_%s", chan->extend_name,
				     postfix);
	if (all_post == NULL)
		return -ENOMEM;
	*result = all_post;
	return 0;
}

static
int __iio_device_attr_init(struct device_attribute *dev_attr,
			   const char *postfix,
			   struct iio_chan_spec const *chan,
			   ssize_t (*readfunc)(struct device *dev,
					       struct device_attribute *attr,
					       char *buf),
			   ssize_t (*writefunc)(struct device *dev,
						struct device_attribute *attr,
						const char *buf,
						size_t len),
			   bool generic)
{
	int ret;
	char *name_format, *full_postfix;
	sysfs_attr_init(&dev_attr->attr);
	ret = __iio_build_postfix(chan, generic, postfix, &full_postfix);
	if (ret)
		goto error_ret;

	/* Special case for types that uses both channel numbers in naming */
	if (chan->type == IIO_IN_DIFF && !generic)
		name_format
			= kasprintf(GFP_KERNEL, "%s_%s",
				    iio_chan_type_name_spec_complex[chan->type],
				    full_postfix);
	else if (generic || !chan->indexed)
		name_format
			= kasprintf(GFP_KERNEL, "%s_%s",
				    iio_chan_type_name_spec_shared[chan->type],
				    full_postfix);
	else
		name_format
			= kasprintf(GFP_KERNEL, "%s%d_%s",
				    iio_chan_type_name_spec_shared[chan->type],
				    chan->channel,
				    full_postfix);

	if (name_format == NULL) {
		ret = -ENOMEM;
		goto error_free_full_postfix;
	}
	dev_attr->attr.name = kasprintf(GFP_KERNEL,
					name_format,
					chan->channel,
					chan->channel2);
	if (dev_attr->attr.name == NULL) {
		ret = -ENOMEM;
		goto error_free_name_format;
	}

	if (readfunc) {
		dev_attr->attr.mode |= S_IRUGO;
		dev_attr->show = readfunc;
	}

	if (writefunc) {
		dev_attr->attr.mode |= S_IWUSR;
		dev_attr->store = writefunc;
	}
	kfree(name_format);
	kfree(full_postfix);

	return 0;

error_free_name_format:
	kfree(name_format);
error_free_full_postfix:
	kfree(full_postfix);
error_ret:
	return ret;
}

static void __iio_device_attr_deinit(struct device_attribute *dev_attr)
{
	kfree(dev_attr->attr.name);
}

int __iio_add_chan_devattr(const char *postfix,
			   const char *group,
			   struct iio_chan_spec const *chan,
			   ssize_t (*readfunc)(struct device *dev,
					       struct device_attribute *attr,
					       char *buf),
			   ssize_t (*writefunc)(struct device *dev,
						struct device_attribute *attr,
						const char *buf,
						size_t len),
			   int mask,
			   bool generic,
			   struct device *dev,
			   struct list_head *attr_list)
{
	int ret;
	struct iio_dev_attr *iio_attr, *t;

	iio_attr = kzalloc(sizeof *iio_attr, GFP_KERNEL);
	if (iio_attr == NULL) {
		ret = -ENOMEM;
		goto error_ret;
	}
	ret = __iio_device_attr_init(&iio_attr->dev_attr,
				     postfix, chan,
				     readfunc, writefunc, generic);
	if (ret)
		goto error_iio_dev_attr_free;
	iio_attr->c = chan;
	iio_attr->address = mask;
	list_for_each_entry(t, attr_list, l)
		if (strcmp(t->dev_attr.attr.name,
			   iio_attr->dev_attr.attr.name) == 0) {
			if (!generic)
				dev_err(dev, "tried to double register : %s\n",
					t->dev_attr.attr.name);
			ret = -EBUSY;
			goto error_device_attr_deinit;
		}

	ret = sysfs_add_file_to_group(&dev->kobj,
				      &iio_attr->dev_attr.attr, group);
	if (ret < 0)
		goto error_device_attr_deinit;

	list_add(&iio_attr->l, attr_list);

	return 0;

error_device_attr_deinit:
	__iio_device_attr_deinit(&iio_attr->dev_attr);
error_iio_dev_attr_free:
	kfree(iio_attr);
error_ret:
	return ret;
}

static int iio_device_add_channel_sysfs(struct iio_dev *dev_info,
					struct iio_chan_spec const *chan)
{
	int ret, i;


	if (chan->channel < 0)
		return 0;
	if (chan->processed_val)
		ret = __iio_add_chan_devattr("input", NULL, chan,
					     &iio_read_channel_info,
					     NULL,
					     0,
					     0,
					     &dev_info->dev,
					     &dev_info->channel_attr_list);
	else
		ret = __iio_add_chan_devattr("raw", NULL, chan,
					     &iio_read_channel_info,
					     (chan->type == IIO_OUT ?
					     &iio_write_channel_info : NULL),
					     0,
					     0,
					     &dev_info->dev,
					     &dev_info->channel_attr_list);
	if (ret)
		goto error_ret;

	for_each_set_bit(i, &chan->info_mask, sizeof(long)*8) {
		ret = __iio_add_chan_devattr(iio_chan_info_postfix[i/2],
					     NULL, chan,
					     &iio_read_channel_info,
					     &iio_write_channel_info,
					     (1 << i),
					     !(i%2),
					     &dev_info->dev,
					     &dev_info->channel_attr_list);
		if (ret == -EBUSY && (i%2 == 0)) {
			ret = 0;
			continue;
		}
		if (ret < 0)
			goto error_ret;
	}
error_ret:
	return ret;
}

static void iio_device_remove_and_free_read_attr(struct iio_dev *dev_info,
						 struct iio_dev_attr *p)
{
	sysfs_remove_file_from_group(&dev_info->dev.kobj,
				     &p->dev_attr.attr, NULL);
	kfree(p->dev_attr.attr.name);
	kfree(p);
}

static ssize_t iio_show_dev_name(struct device *dev,
				 struct device_attribute *attr,
				 char *buf)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	return sprintf(buf, "%s\n", indio_dev->name);
}

static DEVICE_ATTR(name, S_IRUGO, iio_show_dev_name, NULL);

static struct attribute *iio_base_dummy_attrs[] = {
	NULL
};
static struct attribute_group iio_base_dummy_group = {
	.attrs = iio_base_dummy_attrs,
};

static int iio_device_register_sysfs(struct iio_dev *dev_info)
{
	int i, ret = 0;
	struct iio_dev_attr *p, *n;

	if (dev_info->info->attrs)
		ret = sysfs_create_group(&dev_info->dev.kobj,
					 dev_info->info->attrs);
	else
		ret = sysfs_create_group(&dev_info->dev.kobj,
					 &iio_base_dummy_group);
	
	if (ret) {
		dev_err(dev_info->dev.parent,
			"Failed to register sysfs hooks\n");
		goto error_ret;
	}

	/*
	 * New channel registration method - relies on the fact a group does
	 *  not need to be initialized if it is name is NULL.
	 */
	INIT_LIST_HEAD(&dev_info->channel_attr_list);
	if (dev_info->channels)
		for (i = 0; i < dev_info->num_channels; i++) {
			ret = iio_device_add_channel_sysfs(dev_info,
							   &dev_info
							   ->channels[i]);
			if (ret < 0)
				goto error_clear_attrs;
		}
	if (dev_info->name) { 
		ret = sysfs_add_file_to_group(&dev_info->dev.kobj,
					      &dev_attr_name.attr,
					      NULL);
		if (ret)
			goto error_clear_attrs;
	}
	return 0;

error_clear_attrs:
	list_for_each_entry_safe(p, n,
				 &dev_info->channel_attr_list, l) {
		list_del(&p->l);
		iio_device_remove_and_free_read_attr(dev_info, p);
	}
	if (dev_info->info->attrs)
		sysfs_remove_group(&dev_info->dev.kobj, dev_info->info->attrs);
	else
		sysfs_remove_group(&dev_info->dev.kobj, &iio_base_dummy_group);
error_ret:
	return ret;

}

static void iio_device_unregister_sysfs(struct iio_dev *dev_info)
{

	struct iio_dev_attr *p, *n;
	if (dev_info->name)
		sysfs_remove_file_from_group(&dev_info->dev.kobj,
					     &dev_attr_name.attr,
					     NULL);
	list_for_each_entry_safe(p, n, &dev_info->channel_attr_list, l) {
		list_del(&p->l);
		iio_device_remove_and_free_read_attr(dev_info, p);
	}

	if (dev_info->info->attrs)
		sysfs_remove_group(&dev_info->dev.kobj, dev_info->info->attrs);
	else
		sysfs_remove_group(&dev_info->dev.kobj, &iio_base_dummy_group);
}

static const char * const iio_ev_type_text[] = {
	[IIO_EV_TYPE_THRESH] = "thresh",
	[IIO_EV_TYPE_MAG] = "mag",
	[IIO_EV_TYPE_ROC] = "roc"
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
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
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
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
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
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
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
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct iio_dev_attr *this_attr = to_iio_dev_attr(attr);
	unsigned long val;
	int ret;

	ret = strict_strtoul(buf, 10, &val);
	if (ret)
		return ret;

	ret = indio_dev->info->write_event_value(indio_dev, this_attr->address,
						 val);
	if (ret < 0)
		return ret;

	return len;
}

static int iio_device_add_event_sysfs(struct iio_dev *dev_info,
				      struct iio_chan_spec const *chan)
{

	int ret = 0, i, mask = 0;
	char *postfix;
	if (!chan->event_mask)
		return 0;

	for_each_set_bit(i, &chan->event_mask, sizeof(chan->event_mask)*8) {
		postfix = kasprintf(GFP_KERNEL, "%s_%s_en",
				    iio_ev_type_text[i/IIO_EV_TYPE_MAX],
				    iio_ev_dir_text[i%IIO_EV_TYPE_MAX]);
		if (postfix == NULL) {
			ret = -ENOMEM;
			goto error_ret;
		}
		switch (chan->type) {
			/* Switch this to a table at some point */
		case IIO_IN:
			mask = IIO_UNMOD_EVENT_CODE(chan->type, chan->channel,
						    i/IIO_EV_TYPE_MAX,
						    i%IIO_EV_TYPE_MAX);
			break;
		case IIO_ACCEL:
			mask = IIO_MOD_EVENT_CODE(chan->type, 0, chan->channel,
						  i/IIO_EV_TYPE_MAX,
						  i%IIO_EV_TYPE_MAX);
			break;
		case IIO_IN_DIFF:
			mask = IIO_MOD_EVENT_CODE(chan->type, chan->channel,
						  chan->channel2,
						  i/IIO_EV_TYPE_MAX,
						  i%IIO_EV_TYPE_MAX);
			break;
		default:
			printk(KERN_INFO "currently unhandled type of event\n");
			continue;
		}
		ret = __iio_add_chan_devattr(postfix,
					     "events",
					     chan,
					     &iio_ev_state_show,
					     iio_ev_state_store,
					     mask,
					     0,
					     &dev_info->dev,
					     &dev_info->event_interface->
					     dev_attr_list);
		kfree(postfix);
		if (ret)
			goto error_ret;

		postfix = kasprintf(GFP_KERNEL, "%s_%s_value",
				    iio_ev_type_text[i/IIO_EV_TYPE_MAX],
				    iio_ev_dir_text[i%IIO_EV_TYPE_MAX]);
		if (postfix == NULL) {
			ret = -ENOMEM;
			goto error_ret;
		}
		ret = __iio_add_chan_devattr(postfix, "events", chan,
					     iio_ev_value_show,
					     iio_ev_value_store,
					     mask,
					     0,
					     &dev_info->dev,
					     &dev_info->event_interface->
					     dev_attr_list);
		kfree(postfix);
		if (ret)
			goto error_ret;

	}

error_ret:
	return ret;
}

static inline void __iio_remove_event_config_attrs(struct iio_dev *dev_info)
{
	struct iio_dev_attr *p, *n;
	list_for_each_entry_safe(p, n,
				 &dev_info->event_interface->
				 dev_attr_list, l) {
		sysfs_remove_file_from_group(&dev_info->dev.kobj,
					     &p->dev_attr.attr,
					     NULL);
		kfree(p->dev_attr.attr.name);
		kfree(p);
	}
}

static inline int __iio_add_event_config_attrs(struct iio_dev *dev_info)
{
	int j;
	int ret;
	INIT_LIST_HEAD(&dev_info->event_interface->dev_attr_list);
	/* Dynically created from the channels array */
	for (j = 0; j < dev_info->num_channels; j++) {

		ret = iio_device_add_event_sysfs(dev_info,
						 &dev_info->channels[j]);
		if (ret)
			goto error_clear_attrs;
	}
	return 0;

error_clear_attrs:
	__iio_remove_event_config_attrs(dev_info);

	return ret;
}

static struct attribute *iio_events_dummy_attrs[] = {
	NULL
};

static struct attribute_group iio_events_dummy_group = {
	.name = "events",
	.attrs = iio_events_dummy_attrs
};

static bool iio_check_for_dynamic_events(struct iio_dev *dev_info)
{
	int j;
	for (j = 0; j < dev_info->num_channels; j++)
		if (dev_info->channels[j].event_mask != 0)
			return true;
	return false;
}

static int iio_device_register_eventset(struct iio_dev *dev_info)
{
	int ret = 0;

	if (!(dev_info->info->event_attrs ||
	      iio_check_for_dynamic_events(dev_info)))
		return 0;

	dev_info->event_interface =
		kzalloc(sizeof(struct iio_event_interface), GFP_KERNEL);
	if (dev_info->event_interface == NULL) {
		ret = -ENOMEM;
		goto error_ret;
	}

	iio_setup_ev_int(dev_info->event_interface);
	if (dev_info->info->event_attrs != NULL)
		ret = sysfs_create_group(&dev_info->dev.kobj,
					 dev_info->info->event_attrs);
	else
		ret = sysfs_create_group(&dev_info->dev.kobj,
					 &iio_events_dummy_group);
	if (ret) {
		dev_err(&dev_info->dev,
			"Failed to register sysfs for event attrs");
		goto error_free_setup_event_lines;
	}
	if (dev_info->channels) {
		ret = __iio_add_event_config_attrs(dev_info);
		if (ret) {
			if (dev_info->info->event_attrs != NULL)
				sysfs_remove_group(&dev_info->dev.kobj,
						   dev_info->info
						   ->event_attrs);
			else
				sysfs_remove_group(&dev_info->dev.kobj,
						   &iio_events_dummy_group);
			goto error_free_setup_event_lines;
		}
	}

	return 0;

error_free_setup_event_lines:
	__iio_remove_event_config_attrs(dev_info);
	if (dev_info->info->event_attrs != NULL)
		sysfs_remove_group(&dev_info->dev.kobj,
				   dev_info->info->event_attrs);
	else
		sysfs_remove_group(&dev_info->dev.kobj,
				   &iio_events_dummy_group);
	kfree(dev_info->event_interface);
error_ret:

	return ret;
}

static void iio_device_unregister_eventset(struct iio_dev *dev_info)
{
	if (dev_info->event_interface == NULL)
		return;
	__iio_remove_event_config_attrs(dev_info);
	if (dev_info->info->event_attrs != NULL)
		sysfs_remove_group(&dev_info->dev.kobj,
				   dev_info->info->event_attrs);
	else
		sysfs_remove_group(&dev_info->dev.kobj,
				   &iio_events_dummy_group);
	kfree(dev_info->event_interface);
}

static void iio_dev_release(struct device *device)
{
	struct iio_dev *dev_info = container_of(device, struct iio_dev, dev);
	cdev_del(&dev_info->chrdev);
	iio_put();
	kfree(dev_info);
}

static struct device_type iio_dev_type = {
	.name = "iio_device",
	.release = iio_dev_release,
};

struct iio_dev *iio_allocate_device(int sizeof_priv)
{
	struct iio_dev *dev;
	size_t alloc_size;

	alloc_size = sizeof(struct iio_dev);
	if (sizeof_priv) {
		alloc_size = ALIGN(alloc_size, IIO_ALIGN);
		alloc_size += sizeof_priv;
	}
	/* ensure 32-byte alignment of whole construct ? */
	alloc_size += IIO_ALIGN - 1;

	dev = kzalloc(alloc_size, GFP_KERNEL);

	if (dev) {
		dev->dev.type = &iio_dev_type;
		dev->dev.bus = &iio_bus_type;
		device_initialize(&dev->dev);
		dev_set_drvdata(&dev->dev, (void *)dev);
		mutex_init(&dev->mlock);
		iio_get();
	}

	return dev;
}
EXPORT_SYMBOL(iio_allocate_device);

void iio_free_device(struct iio_dev *dev)
{
	if (dev) {
		iio_put();
		kfree(dev);
	}
}
EXPORT_SYMBOL(iio_free_device);

/**
 * iio_chrdev_open() - chrdev file open for ring buffer access and ioctls
 **/
static int iio_chrdev_open(struct inode *inode, struct file *filp)
{
	struct iio_dev *dev_info = container_of(inode->i_cdev,
						struct iio_dev, chrdev);
	filp->private_data = dev_info;
	iio_chrdev_ring_open(dev_info);
	return 0;
}

/**
 * iio_chrdev_release() - chrdev file close ring buffer access and ioctls
 **/
static int iio_chrdev_release(struct inode *inode, struct file *filp)
{
	iio_chrdev_ring_release(container_of(inode->i_cdev,
					     struct iio_dev, chrdev));
	return 0;
}

/* Somewhat of a cross file organization violation - ioctls here are actually
 * event related */
static long iio_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct iio_dev *indio_dev = filp->private_data;
	int __user *ip = (int __user *)arg;
	int fd;

	if (cmd == IIO_GET_EVENT_FD_IOCTL) {
		fd = iio_event_getfd(indio_dev);
		if (copy_to_user(ip, &fd, sizeof(fd)))
			return -EFAULT;
		return 0;
	}
	return -EINVAL;
}

static const struct file_operations iio_ring_fileops = {
	.read = iio_ring_read_first_n_outer_addr,
	.release = iio_chrdev_release,
	.open = iio_chrdev_open,
	.poll = iio_ring_poll_addr,
	.owner = THIS_MODULE,
	.llseek = noop_llseek,
	.unlocked_ioctl = iio_ioctl,
	.compat_ioctl = iio_ioctl,
};

int iio_device_register(struct iio_dev *dev_info)
{
	int ret;

	dev_info->id = ida_simple_get(&iio_ida, 0, 0, GFP_KERNEL);
	if (dev_info->id < 0) {
		ret = dev_info->id;
		dev_err(&dev_info->dev, "Failed to get id\n");
		goto error_ret;
	}
	dev_set_name(&dev_info->dev, "iio:device%d", dev_info->id);

	/* configure elements for the chrdev */
	dev_info->dev.devt = MKDEV(MAJOR(iio_devt), dev_info->id);

	ret = device_add(&dev_info->dev);
	if (ret)
		goto error_free_ida;
	ret = iio_device_register_sysfs(dev_info);
	if (ret) {
		dev_err(dev_info->dev.parent,
			"Failed to register sysfs interfaces\n");
		goto error_del_device;
	}
	ret = iio_device_register_eventset(dev_info);
	if (ret) {
		dev_err(dev_info->dev.parent,
			"Failed to register event set\n");
		goto error_free_sysfs;
	}
	if (dev_info->modes & INDIO_RING_TRIGGERED)
		iio_device_register_trigger_consumer(dev_info);

	cdev_init(&dev_info->chrdev, &iio_ring_fileops);
	dev_info->chrdev.owner = dev_info->info->driver_module;
	ret = cdev_add(&dev_info->chrdev, dev_info->dev.devt, 1);
	return 0;

error_free_sysfs:
	iio_device_unregister_sysfs(dev_info);
error_del_device:
	device_del(&dev_info->dev);
error_free_ida:
	ida_simple_remove(&iio_ida, dev_info->id);
error_ret:
	return ret;
}
EXPORT_SYMBOL(iio_device_register);

void iio_device_unregister(struct iio_dev *dev_info)
{
	if (dev_info->modes & INDIO_RING_TRIGGERED)
		iio_device_unregister_trigger_consumer(dev_info);
	iio_device_unregister_eventset(dev_info);
	iio_device_unregister_sysfs(dev_info);
	ida_simple_remove(&iio_ida, dev_info->id);
	device_unregister(&dev_info->dev);
}
EXPORT_SYMBOL(iio_device_unregister);

void iio_put(void)
{
	module_put(THIS_MODULE);
}

void iio_get(void)
{
	__module_get(THIS_MODULE);
}

subsys_initcall(iio_init);
module_exit(iio_exit);

MODULE_AUTHOR("Jonathan Cameron <jic23@cam.ac.uk>");
MODULE_DESCRIPTION("Industrial I/O core");
MODULE_LICENSE("GPL");

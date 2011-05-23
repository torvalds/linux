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
#include "iio.h"
#include "trigger_consumer.h"

#define IIO_ID_PREFIX "device"
#define IIO_ID_FORMAT IIO_ID_PREFIX "%d"

/* IDR to assign each registered device a unique id*/
static DEFINE_IDA(iio_ida);
/* IDR to allocate character device minor numbers */
static DEFINE_IDA(iio_chrdev_ida);
/* Lock used to protect both of the above */
static DEFINE_SPINLOCK(iio_ida_lock);

dev_t iio_devt;
EXPORT_SYMBOL(iio_devt);

#define IIO_DEV_MAX 256
struct bus_type iio_bus_type = {
	.name = "iio",
};
EXPORT_SYMBOL(iio_bus_type);

static const char * const iio_chan_type_name_spec_shared[] = {
	[IIO_TIMESTAMP] = "timestamp",
	[IIO_ACCEL] = "accel",
	[IIO_IN] = "in",
	[IIO_CURRENT] = "current",
	[IIO_POWER] = "power",
	[IIO_IN_DIFF] = "in-in",
	[IIO_GYRO] = "gyro",
	[IIO_TEMP] = "temp",
	[IIO_MAGN] = "magn",
	[IIO_INCLI] = "incli",
	[IIO_ROT] = "rot",
	[IIO_INTENSITY] = "intensity",
	[IIO_LIGHT] = "illuminance",
	[IIO_ANGL] = "angl",
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
};

int iio_push_event(struct iio_dev *dev_info,
		   int ev_line,
		   int ev_code,
		   s64 timestamp)
{
	struct iio_event_interface *ev_int
		= &dev_info->event_interfaces[ev_line];
	struct iio_detected_event_list *ev;
	int ret = 0;

	/* Does anyone care? */
	mutex_lock(&ev_int->event_list_lock);
	if (test_bit(IIO_BUSY_BIT_POS, &ev_int->handler.flags)) {
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
	struct iio_handler *hand = iio_cdev_to_handler(inode->i_cdev);
	struct iio_event_interface *ev_int = hand->private;
	struct iio_detected_event_list *el, *t;

	mutex_lock(&ev_int->event_list_lock);
	clear_bit(IIO_BUSY_BIT_POS, &ev_int->handler.flags);
	/*
	 * In order to maintain a clean state for reopening,
	 * clear out any awaiting events. The mask will prevent
	 * any new __iio_push_event calls running.
	 */
	list_for_each_entry_safe(el, t, &ev_int->det_events, list) {
		list_del(&el->list);
		kfree(el);
	}
	mutex_unlock(&ev_int->event_list_lock);

	return 0;
}

static int iio_event_chrdev_open(struct inode *inode, struct file *filep)
{
	struct iio_handler *hand = iio_cdev_to_handler(inode->i_cdev);
	struct iio_event_interface *ev_int = hand->private;

	mutex_lock(&ev_int->event_list_lock);
	if (test_and_set_bit(IIO_BUSY_BIT_POS, &hand->flags)) {
		fops_put(filep->f_op);
		mutex_unlock(&ev_int->event_list_lock);
		return -EBUSY;
	}
	filep->private_data = hand->private;
	mutex_unlock(&ev_int->event_list_lock);

	return 0;
}

static const struct file_operations iio_event_chrdev_fileops = {
	.read =  iio_event_chrdev_read,
	.release = iio_event_chrdev_release,
	.open = iio_event_chrdev_open,
	.owner = THIS_MODULE,
	.llseek = noop_llseek,
};

static void iio_event_dev_release(struct device *dev)
{
	struct iio_event_interface *ev_int
		= container_of(dev, struct iio_event_interface, dev);
	cdev_del(&ev_int->handler.chrdev);
	iio_device_free_chrdev_minor(MINOR(dev->devt));
};

static struct device_type iio_event_type = {
	.release = iio_event_dev_release,
};

int iio_device_get_chrdev_minor(void)
{
	int ret, val;

ida_again:
	if (unlikely(ida_pre_get(&iio_chrdev_ida, GFP_KERNEL) == 0))
		return -ENOMEM;
	spin_lock(&iio_ida_lock);
	ret = ida_get_new(&iio_chrdev_ida, &val);
	spin_unlock(&iio_ida_lock);
	if (unlikely(ret == -EAGAIN))
		goto ida_again;
	else if (unlikely(ret))
		return ret;
	if (val > IIO_DEV_MAX)
		return -ENOMEM;
	return val;
}

void iio_device_free_chrdev_minor(int val)
{
	spin_lock(&iio_ida_lock);
	ida_remove(&iio_chrdev_ida, val);
	spin_unlock(&iio_ida_lock);
}

static int iio_setup_ev_int(struct iio_event_interface *ev_int,
			    const char *dev_name,
			    int index,
			    struct module *owner,
			    struct device *dev)
{
	int ret, minor;

	ev_int->dev.bus = &iio_bus_type;
	ev_int->dev.parent = dev;
	ev_int->dev.type = &iio_event_type;
	device_initialize(&ev_int->dev);

	minor = iio_device_get_chrdev_minor();
	if (minor < 0) {
		ret = minor;
		goto error_device_put;
	}
	ev_int->dev.devt = MKDEV(MAJOR(iio_devt), minor);
	dev_set_name(&ev_int->dev, "%s:event%d", dev_name, index);

	ret = device_add(&ev_int->dev);
	if (ret)
		goto error_free_minor;

	cdev_init(&ev_int->handler.chrdev, &iio_event_chrdev_fileops);
	ev_int->handler.chrdev.owner = owner;

	mutex_init(&ev_int->event_list_lock);
	/* discussion point - make this variable? */
	ev_int->max_events = 10;
	ev_int->current_events = 0;
	INIT_LIST_HEAD(&ev_int->det_events);
	init_waitqueue_head(&ev_int->wait);
	ev_int->handler.private = ev_int;
	ev_int->handler.flags = 0;

	ret = cdev_add(&ev_int->handler.chrdev, ev_int->dev.devt, 1);
	if (ret)
		goto error_unreg_device;

	return 0;

error_unreg_device:
	device_unregister(&ev_int->dev);
error_free_minor:
	iio_device_free_chrdev_minor(minor);
error_device_put:
	put_device(&ev_int->dev);

	return ret;
}

static void iio_free_ev_int(struct iio_event_interface *ev_int)
{
	device_unregister(&ev_int->dev);
	put_device(&ev_int->dev);
}

static int __init iio_dev_init(void)
{
	int err;

	err = alloc_chrdev_region(&iio_devt, 0, IIO_DEV_MAX, "iio");
	if (err < 0)
		printk(KERN_ERR "%s: failed to allocate char dev region\n",
		       __FILE__);

	return err;
}

static void __exit iio_dev_exit(void)
{
	if (iio_devt)
		unregister_chrdev_region(iio_devt, IIO_DEV_MAX);
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

	ret = iio_dev_init();
	if (ret < 0)
		goto error_unregister_bus_type;

	return 0;

error_unregister_bus_type:
	bus_unregister(&iio_bus_type);
error_nothing:
	return ret;
}

static void __exit iio_exit(void)
{
	iio_dev_exit();
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
	int ret, integer = 0, micro = 0, micro_mult = 100000;
	bool integer_part = true, negative = false;

	/* Assumes decimal - precision based on number of digits */
	if (!indio_dev->info->write_raw)
		return -EINVAL;
	if (buf[0] == '-') {
		negative = true;
		buf++;
	}
	while (*buf) {
		if ('0' <= *buf && *buf <= '9') {
			if (integer_part)
				integer = integer*10 + *buf - '0';
			else {
				micro += micro_mult*(*buf - '0');
				if (micro_mult == 1)
					break;
				micro_mult /= 10;
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
			micro = -micro;
	}

	ret = indio_dev->info->write_raw(indio_dev, this_attr->c,
					 integer, micro, this_attr->address);
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

void __iio_device_attr_deinit(struct device_attribute *dev_attr)
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
					     NULL,
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

static int iio_device_register_sysfs(struct iio_dev *dev_info)
{
	int i, ret = 0;
	struct iio_dev_attr *p, *n;

	if (dev_info->info->attrs) {
		ret = sysfs_create_group(&dev_info->dev.kobj,
					 dev_info->info->attrs);
		if (ret) {
			dev_err(dev_info->dev.parent,
				"Failed to register sysfs hooks\n");
			goto error_ret;
		}
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
}

/* Return a negative errno on failure */
int iio_get_new_ida_val(struct ida *this_ida)
{
	int ret;
	int val;

ida_again:
	if (unlikely(ida_pre_get(this_ida, GFP_KERNEL) == 0))
		return -ENOMEM;

	spin_lock(&iio_ida_lock);
	ret = ida_get_new(this_ida, &val);
	spin_unlock(&iio_ida_lock);
	if (unlikely(ret == -EAGAIN))
		goto ida_again;
	else if (unlikely(ret))
		return ret;

	return val;
}
EXPORT_SYMBOL(iio_get_new_ida_val);

void iio_free_ida_val(struct ida *this_ida, int id)
{
	spin_lock(&iio_ida_lock);
	ida_remove(this_ida, id);
	spin_unlock(&iio_ida_lock);
}
EXPORT_SYMBOL(iio_free_ida_val);

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

	int ret = 0, i, mask;
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
		}
		ret = __iio_add_chan_devattr(postfix,
					     NULL,
					     chan,
					     &iio_ev_state_show,
					     iio_ev_state_store,
					     mask,
					     /*HACK. - limits us to one
					       event interface - fix by
					       extending the bitmask - but
					       how far*/
					     0,
					     &dev_info->event_interfaces[0].dev,
					     &dev_info->event_interfaces[0].
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
		ret = __iio_add_chan_devattr(postfix, NULL, chan,
					     iio_ev_value_show,
					     iio_ev_value_store,
					     mask,
					     0,
					     &dev_info->event_interfaces[0]
					     .dev,
					     &dev_info->event_interfaces[0]
					     .dev_attr_list);
		kfree(postfix);
		if (ret)
			goto error_ret;

	}

error_ret:
	return ret;
}

static inline void __iio_remove_all_event_sysfs(struct iio_dev *dev_info,
						const char *groupname,
						int num)
{
	struct iio_dev_attr *p, *n;
	list_for_each_entry_safe(p, n,
				 &dev_info->event_interfaces[num].
				 dev_attr_list, l) {
		sysfs_remove_file_from_group(&dev_info
					     ->event_interfaces[num].dev.kobj,
					     &p->dev_attr.attr,
					     groupname);
		kfree(p->dev_attr.attr.name);
		kfree(p);
	}
}

static inline int __iio_add_event_config_attrs(struct iio_dev *dev_info, int i)
{
	int j;
	int ret;
	INIT_LIST_HEAD(&dev_info->event_interfaces[0].dev_attr_list);
	/* Dynically created from the channels array */
	if (dev_info->channels) {
		for (j = 0; j < dev_info->num_channels; j++) {
			ret = iio_device_add_event_sysfs(dev_info,
							 &dev_info
							 ->channels[j]);
			if (ret)
				goto error_clear_attrs;
		}
	}
	return 0;

error_clear_attrs:
	__iio_remove_all_event_sysfs(dev_info, NULL, i);

	return ret;
}

static inline int __iio_remove_event_config_attrs(struct iio_dev *dev_info,
						  int i)
{
	__iio_remove_all_event_sysfs(dev_info, NULL, i);
	return 0;
}

static int iio_device_register_eventset(struct iio_dev *dev_info)
{
	int ret = 0, i, j;

	if (dev_info->info->num_interrupt_lines == 0)
		return 0;

	dev_info->event_interfaces =
		kzalloc(sizeof(struct iio_event_interface)
			*dev_info->info->num_interrupt_lines,
			GFP_KERNEL);
	if (dev_info->event_interfaces == NULL) {
		ret = -ENOMEM;
		goto error_ret;
	}

	for (i = 0; i < dev_info->info->num_interrupt_lines; i++) {
		ret = iio_setup_ev_int(&dev_info->event_interfaces[i],
				       dev_name(&dev_info->dev),
				       i,
				       dev_info->info->driver_module,
				       &dev_info->dev);
		if (ret) {
			dev_err(&dev_info->dev,
				"Could not get chrdev interface\n");
			goto error_free_setup_ev_ints;
		}

		dev_set_drvdata(&dev_info->event_interfaces[i].dev,
				(void *)dev_info);

		if (dev_info->info->event_attrs != NULL)
			ret = sysfs_create_group(&dev_info
						 ->event_interfaces[i]
						 .dev.kobj,
						 &dev_info->info
						 ->event_attrs[i]);

		if (ret) {
			dev_err(&dev_info->dev,
				"Failed to register sysfs for event attrs");
			goto error_remove_sysfs_interfaces;
		}
	}

	for (i = 0; i < dev_info->info->num_interrupt_lines; i++) {
		ret = __iio_add_event_config_attrs(dev_info, i);
		if (ret)
			goto error_unregister_config_attrs;
	}

	return 0;

error_unregister_config_attrs:
	for (j = 0; j < i; j++)
		__iio_remove_event_config_attrs(dev_info, i);
	i = dev_info->info->num_interrupt_lines - 1;
error_remove_sysfs_interfaces:
	for (j = 0; j < i; j++)
		if (dev_info->info->event_attrs != NULL)
			sysfs_remove_group(&dev_info
				   ->event_interfaces[j].dev.kobj,
				   &dev_info->info->event_attrs[j]);
error_free_setup_ev_ints:
	for (j = 0; j < i; j++)
		iio_free_ev_int(&dev_info->event_interfaces[j]);
	kfree(dev_info->event_interfaces);
error_ret:

	return ret;
}

static void iio_device_unregister_eventset(struct iio_dev *dev_info)
{
	int i;

	if (dev_info->info->num_interrupt_lines == 0)
		return;
	for (i = 0; i < dev_info->info->num_interrupt_lines; i++) {
		__iio_remove_event_config_attrs(dev_info, i);
		if (dev_info->info->event_attrs != NULL)
			sysfs_remove_group(&dev_info
					   ->event_interfaces[i].dev.kobj,
					   &dev_info->info->event_attrs[i]);
	}

	for (i = 0; i < dev_info->info->num_interrupt_lines; i++)
		iio_free_ev_int(&dev_info->event_interfaces[i]);
	kfree(dev_info->event_interfaces);
}

static void iio_dev_release(struct device *device)
{
	iio_put();
	kfree(to_iio_dev(device));
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
	if (dev)
		iio_put_device(dev);
}
EXPORT_SYMBOL(iio_free_device);

int iio_device_register(struct iio_dev *dev_info)
{
	int ret;

	dev_info->id = iio_get_new_ida_val(&iio_ida);
	if (dev_info->id < 0) {
		ret = dev_info->id;
		dev_err(&dev_info->dev, "Failed to get id\n");
		goto error_ret;
	}
	dev_set_name(&dev_info->dev, "device%d", dev_info->id);

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

	return 0;

error_free_sysfs:
	iio_device_unregister_sysfs(dev_info);
error_del_device:
	device_del(&dev_info->dev);
error_free_ida:
	iio_free_ida_val(&iio_ida, dev_info->id);
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
	iio_free_ida_val(&iio_ida, dev_info->id);
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

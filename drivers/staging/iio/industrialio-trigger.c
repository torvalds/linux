/* The industrial I/O core, trigger handling functions
 *
 * Copyright (c) 2008 Jonathan Cameron
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/idr.h>
#include <linux/err.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/list.h>
#include <linux/slab.h>

#include "iio.h"
#include "trigger.h"
#include "trigger_consumer.h"

/* RFC - Question of approach
 * Make the common case (single sensor single trigger)
 * simple by starting trigger capture from when first sensors
 * is added.
 *
 * Complex simultaneous start requires use of 'hold' functionality
 * of the trigger. (not implemented)
 *
 * Any other suggestions?
 */


static DEFINE_IDR(iio_trigger_idr);
static DEFINE_SPINLOCK(iio_trigger_idr_lock);

/* Single list of all available triggers */
static LIST_HEAD(iio_trigger_list);
static DEFINE_MUTEX(iio_trigger_list_lock);

/**
 * iio_trigger_register_sysfs() - create a device for this trigger
 * @trig_info:	the trigger
 *
 * Also adds any control attribute registered by the trigger driver
 **/
static int iio_trigger_register_sysfs(struct iio_trigger *trig_info)
{
	int ret = 0;

	if (trig_info->control_attrs)
		ret = sysfs_create_group(&trig_info->dev.kobj,
					 trig_info->control_attrs);

	return ret;
}

static void iio_trigger_unregister_sysfs(struct iio_trigger *trig_info)
{
	if (trig_info->control_attrs)
		sysfs_remove_group(&trig_info->dev.kobj,
				   trig_info->control_attrs);
}


/**
 * iio_trigger_register_id() - get a unique id for this trigger
 * @trig_info:	the trigger
 **/
static int iio_trigger_register_id(struct iio_trigger *trig_info)
{
	int ret = 0;

idr_again:
	if (unlikely(idr_pre_get(&iio_trigger_idr, GFP_KERNEL) == 0))
		return -ENOMEM;

	spin_lock(&iio_trigger_idr_lock);
	ret = idr_get_new(&iio_trigger_idr, NULL, &trig_info->id);
	spin_unlock(&iio_trigger_idr_lock);
	if (unlikely(ret == -EAGAIN))
		goto idr_again;
	else if (likely(!ret))
		trig_info->id = trig_info->id & MAX_ID_MASK;

	return ret;
}

/**
 * iio_trigger_unregister_id() - free up unique id for use by another trigger
 * @trig_info: the trigger
 **/
static void iio_trigger_unregister_id(struct iio_trigger *trig_info)
{
	spin_lock(&iio_trigger_idr_lock);
	idr_remove(&iio_trigger_idr, trig_info->id);
	spin_unlock(&iio_trigger_idr_lock);
}

int iio_trigger_register(struct iio_trigger *trig_info)
{
	int ret;

	ret = iio_trigger_register_id(trig_info);
	if (ret)
		goto error_ret;
	/* Set the name used for the sysfs directory etc */
	dev_set_name(&trig_info->dev, "trigger%ld",
		     (unsigned long) trig_info->id);

	ret = device_add(&trig_info->dev);
	if (ret)
		goto error_unregister_id;

	ret = iio_trigger_register_sysfs(trig_info);
	if (ret)
		goto error_device_del;

	/* Add to list of available triggers held by the IIO core */
	mutex_lock(&iio_trigger_list_lock);
	list_add_tail(&trig_info->list, &iio_trigger_list);
	mutex_unlock(&iio_trigger_list_lock);

	return 0;

error_device_del:
	device_del(&trig_info->dev);
error_unregister_id:
	iio_trigger_unregister_id(trig_info);
error_ret:
	return ret;
}
EXPORT_SYMBOL(iio_trigger_register);

void iio_trigger_unregister(struct iio_trigger *trig_info)
{
	struct iio_trigger *cursor;

	mutex_lock(&iio_trigger_list_lock);
	list_for_each_entry(cursor, &iio_trigger_list, list)
		if (cursor == trig_info) {
			list_del(&cursor->list);
			break;
		}
	mutex_unlock(&iio_trigger_list_lock);

	iio_trigger_unregister_sysfs(trig_info);
	iio_trigger_unregister_id(trig_info);
	/* Possible issue in here */
	device_unregister(&trig_info->dev);
}
EXPORT_SYMBOL(iio_trigger_unregister);

struct iio_trigger *iio_trigger_find_by_name(const char *name, size_t len)
{
	struct iio_trigger *trig;
	bool found = false;

	if (len && name[len - 1] == '\n')
		len--;

	mutex_lock(&iio_trigger_list_lock);
	list_for_each_entry(trig, &iio_trigger_list, list) {
		if (strncmp(trig->name, name, len) == 0) {
			found = true;
			break;
		}
	}
	mutex_unlock(&iio_trigger_list_lock);

	return found ? trig : NULL;
}
EXPORT_SYMBOL(iio_trigger_find_by_name);

void iio_trigger_poll(struct iio_trigger *trig)
{
	struct iio_poll_func *pf_cursor;

	list_for_each_entry(pf_cursor, &trig->pollfunc_list, list) {
		if (pf_cursor->poll_func_immediate) {
			pf_cursor->poll_func_immediate(pf_cursor->private_data);
			trig->use_count++;
		}
	}
	list_for_each_entry(pf_cursor, &trig->pollfunc_list, list) {
		if (pf_cursor->poll_func_main) {
			pf_cursor->poll_func_main(pf_cursor->private_data);
			trig->use_count++;
		}
	}
}
EXPORT_SYMBOL(iio_trigger_poll);

void iio_trigger_notify_done(struct iio_trigger *trig)
{
	trig->use_count--;
	if (trig->use_count == 0 && trig->try_reenable)
		if (trig->try_reenable(trig)) {
			/* Missed and interrupt so launch new poll now */
			trig->timestamp = 0;
			iio_trigger_poll(trig);
		}
}
EXPORT_SYMBOL(iio_trigger_notify_done);

/**
 * iio_trigger_read_name() - retrieve useful identifying name
 **/
ssize_t iio_trigger_read_name(struct device *dev,
			      struct device_attribute *attr,
			      char *buf)
{
	struct iio_trigger *trig = dev_get_drvdata(dev);
	return sprintf(buf, "%s\n", trig->name);
}
EXPORT_SYMBOL(iio_trigger_read_name);

/* Trigger Consumer related functions */

/* Complexity in here.  With certain triggers (datardy) an acknowledgement
 * may be needed if the pollfuncs do not include the data read for the
 * triggering device.
 * This is not currently handled.  Alternative of not enabling trigger unless
 * the relevant function is in there may be the best option.
 */
/* Worth protecting against double additions?*/
int iio_trigger_attach_poll_func(struct iio_trigger *trig,
				 struct iio_poll_func *pf)
{
	int ret = 0;
	unsigned long flags;

	spin_lock_irqsave(&trig->pollfunc_list_lock, flags);
	list_add_tail(&pf->list, &trig->pollfunc_list);
	spin_unlock_irqrestore(&trig->pollfunc_list_lock, flags);

	if (trig->set_trigger_state)
		ret = trig->set_trigger_state(trig, true);
	if (ret) {
		printk(KERN_ERR "set trigger state failed\n");
		list_del(&pf->list);
	}
	return ret;
}
EXPORT_SYMBOL(iio_trigger_attach_poll_func);

int iio_trigger_dettach_poll_func(struct iio_trigger *trig,
				  struct iio_poll_func *pf)
{
	struct iio_poll_func *pf_cursor;
	unsigned long flags;
	int ret = -EINVAL;

	spin_lock_irqsave(&trig->pollfunc_list_lock, flags);
	list_for_each_entry(pf_cursor, &trig->pollfunc_list, list)
		if (pf_cursor == pf) {
			ret = 0;
			break;
		}
	if (!ret) {
		if (list_is_singular(&trig->pollfunc_list)
		    && trig->set_trigger_state) {
			spin_unlock_irqrestore(&trig->pollfunc_list_lock,
					       flags);
			/* May sleep hence cannot hold the spin lock */
			ret = trig->set_trigger_state(trig, false);
			if (ret)
				goto error_ret;
			spin_lock_irqsave(&trig->pollfunc_list_lock, flags);
		}
		/*
		 * Now we can delete safe in the knowledge that, if this is
		 * the last pollfunc then we have disabled the trigger anyway
		 * and so nothing should be able to call the pollfunc.
		 */
		list_del(&pf_cursor->list);
	}
	spin_unlock_irqrestore(&trig->pollfunc_list_lock, flags);

error_ret:
	return ret;
}
EXPORT_SYMBOL(iio_trigger_dettach_poll_func);

/**
 * iio_trigger_read_currrent() trigger consumer sysfs query which trigger
 *
 * For trigger consumers the current_trigger interface allows the trigger
 * used by the device to be queried.
 **/
static ssize_t iio_trigger_read_current(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	int len = 0;
	if (dev_info->trig)
		len = snprintf(buf,
			       IIO_TRIGGER_NAME_LENGTH,
			       "%s\n",
			       dev_info->trig->name);
	return len;
}

/**
 * iio_trigger_write_current() trigger consumer sysfs set current trigger
 *
 * For trigger consumers the current_trigger interface allows the trigger
 * used for this device to be specified at run time based on the triggers
 * name.
 **/
static ssize_t iio_trigger_write_current(struct device *dev,
					 struct device_attribute *attr,
					 const char *buf,
					 size_t len)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct iio_trigger *oldtrig = dev_info->trig;
	mutex_lock(&dev_info->mlock);
	if (dev_info->currentmode == INDIO_RING_TRIGGERED) {
		mutex_unlock(&dev_info->mlock);
		return -EBUSY;
	}
	mutex_unlock(&dev_info->mlock);

	len = len < IIO_TRIGGER_NAME_LENGTH ? len : IIO_TRIGGER_NAME_LENGTH;

	dev_info->trig = iio_trigger_find_by_name(buf, len);
	if (oldtrig && dev_info->trig != oldtrig)
		iio_put_trigger(oldtrig);
	if (dev_info->trig)
		iio_get_trigger(dev_info->trig);

	return len;
}

static DEVICE_ATTR(current_trigger, S_IRUGO | S_IWUSR,
		   iio_trigger_read_current,
		   iio_trigger_write_current);

static struct attribute *iio_trigger_consumer_attrs[] = {
	&dev_attr_current_trigger.attr,
	NULL,
};

static const struct attribute_group iio_trigger_consumer_attr_group = {
	.name = "trigger",
	.attrs = iio_trigger_consumer_attrs,
};

static void iio_trig_release(struct device *device)
{
	struct iio_trigger *trig = to_iio_trigger(device);
	kfree(trig);
	iio_put();
}

static struct device_type iio_trig_type = {
	.release = iio_trig_release,
};

struct iio_trigger *iio_allocate_trigger(void)
{
	struct iio_trigger *trig;
	trig = kzalloc(sizeof *trig, GFP_KERNEL);
	if (trig) {
		trig->dev.type = &iio_trig_type;
		trig->dev.bus = &iio_bus_type;
		device_initialize(&trig->dev);
		dev_set_drvdata(&trig->dev, (void *)trig);
		spin_lock_init(&trig->pollfunc_list_lock);
		INIT_LIST_HEAD(&trig->list);
		INIT_LIST_HEAD(&trig->pollfunc_list);
		iio_get();
	}
	return trig;
}
EXPORT_SYMBOL(iio_allocate_trigger);

void iio_free_trigger(struct iio_trigger *trig)
{
	if (trig)
		put_device(&trig->dev);
}
EXPORT_SYMBOL(iio_free_trigger);

int iio_device_register_trigger_consumer(struct iio_dev *dev_info)
{
	int ret;
	ret = sysfs_create_group(&dev_info->dev.kobj,
				 &iio_trigger_consumer_attr_group);
	return ret;
}
EXPORT_SYMBOL(iio_device_register_trigger_consumer);

int iio_device_unregister_trigger_consumer(struct iio_dev *dev_info)
{
	sysfs_remove_group(&dev_info->dev.kobj,
			   &iio_trigger_consumer_attr_group);
	return 0;
}
EXPORT_SYMBOL(iio_device_unregister_trigger_consumer);


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
 * iio_trigger_read_name() - retrieve useful identifying name
 **/
static ssize_t iio_trigger_read_name(struct device *dev,
				     struct device_attribute *attr,
				     char *buf)
{
	struct iio_trigger *trig = dev_get_drvdata(dev);
	return sprintf(buf, "%s\n", trig->name);
}

static DEVICE_ATTR(name, S_IRUGO, iio_trigger_read_name, NULL);

/**
 * iio_trigger_register_sysfs() - create a device for this trigger
 * @trig_info:	the trigger
 *
 * Also adds any control attribute registered by the trigger driver
 **/
static int iio_trigger_register_sysfs(struct iio_trigger *trig_info)
{
	return sysfs_add_file_to_group(&trig_info->dev.kobj,
				       &dev_attr_name.attr,
				       NULL);
}

static void iio_trigger_unregister_sysfs(struct iio_trigger *trig_info)
{
	sysfs_remove_file_from_group(&trig_info->dev.kobj,
					   &dev_attr_name.attr,
					   NULL);
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
	mutex_lock(&iio_trigger_list_lock);
	list_del(&trig_info->list);
	mutex_unlock(&iio_trigger_list_lock);

	iio_trigger_unregister_sysfs(trig_info);
	iio_trigger_unregister_id(trig_info);
	/* Possible issue in here */
	device_unregister(&trig_info->dev);
}
EXPORT_SYMBOL(iio_trigger_unregister);

static struct iio_trigger *iio_trigger_find_by_name(const char *name,
						    size_t len)
{
	struct iio_trigger *trig = NULL, *iter;

	mutex_lock(&iio_trigger_list_lock);
	list_for_each_entry(iter, &iio_trigger_list, list)
		if (sysfs_streq(iter->name, name)) {
			trig = iter;
			break;
		}
	mutex_unlock(&iio_trigger_list_lock);

	return trig;
}

void iio_trigger_poll(struct iio_trigger *trig, s64 time)
{
	int i;
	if (!trig->use_count) {
		for (i = 0; i < CONFIG_IIO_CONSUMERS_PER_TRIGGER; i++)
			if (trig->subirqs[i].enabled) {
				trig->use_count++;
				generic_handle_irq(trig->subirq_base + i);
			}
	}
}
EXPORT_SYMBOL(iio_trigger_poll);

irqreturn_t iio_trigger_generic_data_rdy_poll(int irq, void *private)
{
	iio_trigger_poll(private, iio_get_time_ns());
	return IRQ_HANDLED;
}
EXPORT_SYMBOL(iio_trigger_generic_data_rdy_poll);

void iio_trigger_poll_chained(struct iio_trigger *trig, s64 time)
{
	int i;
	if (!trig->use_count) {
		for (i = 0; i < CONFIG_IIO_CONSUMERS_PER_TRIGGER; i++)
			if (trig->subirqs[i].enabled) {
				trig->use_count++;
				handle_nested_irq(trig->subirq_base + i);
			}
	}
}
EXPORT_SYMBOL(iio_trigger_poll_chained);

void iio_trigger_notify_done(struct iio_trigger *trig)
{
	trig->use_count--;
	if (trig->use_count == 0 && trig->try_reenable)
		if (trig->try_reenable(trig)) {
			/* Missed and interrupt so launch new poll now */
			iio_trigger_poll(trig, 0);
		}
}
EXPORT_SYMBOL(iio_trigger_notify_done);

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
	bool notinuse
		= bitmap_empty(trig->pool, CONFIG_IIO_CONSUMERS_PER_TRIGGER);

	pf->irq = iio_trigger_get_irq(trig);
	ret = request_threaded_irq(pf->irq, pf->h, pf->thread,
				   pf->type, pf->name,
				   pf);
	if (trig->set_trigger_state && notinuse)
		ret = trig->set_trigger_state(trig, true);

	return ret;
}
EXPORT_SYMBOL(iio_trigger_attach_poll_func);

int iio_trigger_dettach_poll_func(struct iio_trigger *trig,
				  struct iio_poll_func *pf)
{
	int ret = 0;
	bool no_other_users
		= (bitmap_weight(trig->pool,
				 CONFIG_IIO_CONSUMERS_PER_TRIGGER)
		   == 1);
	if (trig->set_trigger_state && no_other_users) {
		ret = trig->set_trigger_state(trig, false);
		if (ret)
			goto error_ret;
	}
	iio_trigger_put_irq(trig, pf->irq);
	free_irq(pf->irq, pf);

error_ret:
	return ret;
}
EXPORT_SYMBOL(iio_trigger_dettach_poll_func);

irqreturn_t iio_pollfunc_store_time(int irq, void *p)
{
	struct iio_poll_func *pf = p;
	pf->timestamp = iio_get_time_ns();
	return IRQ_WAKE_THREAD;
}
EXPORT_SYMBOL(iio_pollfunc_store_time);

struct iio_poll_func
*iio_alloc_pollfunc(irqreturn_t (*h)(int irq, void *p),
		    irqreturn_t (*thread)(int irq, void *p),
		    int type,
		    void *private,
		    const char *fmt,
		    ...)
{
	va_list vargs;
	struct iio_poll_func *pf;

	pf = kmalloc(sizeof *pf, GFP_KERNEL);
	if (pf == NULL)
		return NULL;
	va_start(vargs, fmt);
	pf->name = kvasprintf(GFP_KERNEL, fmt, vargs);
	va_end(vargs);
	if (pf->name == NULL) {
		kfree(pf);
		return NULL;
	}
	pf->h = h;
	pf->thread = thread;
	pf->type = type;
	pf->private_data = private;

	return pf;
}
EXPORT_SYMBOL_GPL(iio_alloc_pollfunc);

void iio_dealloc_pollfunc(struct iio_poll_func *pf)
{
	kfree(pf->name);
	kfree(pf);
}
EXPORT_SYMBOL_GPL(iio_dealloc_pollfunc);

/**
 * iio_trigger_read_currrent() - trigger consumer sysfs query which trigger
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
		len = sprintf(buf,
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
	int i;

	if (trig->subirq_base) {
		for (i = 0; i < CONFIG_IIO_CONSUMERS_PER_TRIGGER; i++) {
			irq_modify_status(trig->subirq_base + i,
					  IRQ_NOAUTOEN,
					  IRQ_NOREQUEST | IRQ_NOPROBE);
			irq_set_chip(trig->subirq_base + i,
				     NULL);
			irq_set_handler(trig->subirq_base + i,
					NULL);
		}

		irq_free_descs(trig->subirq_base,
			       CONFIG_IIO_CONSUMERS_PER_TRIGGER);
	}
	kfree(trig->name);
	kfree(trig);
	iio_put();
}

static struct device_type iio_trig_type = {
	.release = iio_trig_release,
};

static void iio_trig_subirqmask(struct irq_data *d)
{
	struct irq_chip *chip = irq_data_get_irq_chip(d);
	struct iio_trigger *trig
		= container_of(chip,
			       struct iio_trigger, subirq_chip);
	trig->subirqs[d->irq - trig->subirq_base].enabled = false;
}

static void iio_trig_subirqunmask(struct irq_data *d)
{
	struct irq_chip *chip = irq_data_get_irq_chip(d);
	struct iio_trigger *trig
		= container_of(chip,
			       struct iio_trigger, subirq_chip);
	trig->subirqs[d->irq - trig->subirq_base].enabled = true;
}

struct iio_trigger *iio_allocate_trigger(const char *fmt, ...)
{
	va_list vargs;
	struct iio_trigger *trig;
	trig = kzalloc(sizeof *trig, GFP_KERNEL);
	if (trig) {
		int i;
		trig->dev.type = &iio_trig_type;
		trig->dev.bus = &iio_bus_type;
		device_initialize(&trig->dev);
		dev_set_drvdata(&trig->dev, (void *)trig);

		mutex_init(&trig->pool_lock);
		trig->subirq_base
			= irq_alloc_descs(-1, 0,
					  CONFIG_IIO_CONSUMERS_PER_TRIGGER,
					  0);
		if (trig->subirq_base < 0) {
			kfree(trig);
			return NULL;
		}
		va_start(vargs, fmt);
		trig->name = kvasprintf(GFP_KERNEL, fmt, vargs);
		va_end(vargs);
		if (trig->name == NULL) {
			irq_free_descs(trig->subirq_base,
				       CONFIG_IIO_CONSUMERS_PER_TRIGGER);
			kfree(trig);
			return NULL;
		}
		trig->subirq_chip.name = trig->name;
		trig->subirq_chip.irq_mask = &iio_trig_subirqmask;
		trig->subirq_chip.irq_unmask = &iio_trig_subirqunmask;
		for (i = 0; i < CONFIG_IIO_CONSUMERS_PER_TRIGGER; i++) {
			irq_set_chip(trig->subirq_base + i,
				     &trig->subirq_chip);
			irq_set_handler(trig->subirq_base + i,
					&handle_simple_irq);
			irq_modify_status(trig->subirq_base + i,
					  IRQ_NOREQUEST | IRQ_NOAUTOEN,
					  IRQ_NOPROBE);
		}
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

int iio_triggered_ring_postenable(struct iio_dev *indio_dev)
{
	return indio_dev->trig
		? iio_trigger_attach_poll_func(indio_dev->trig,
					       indio_dev->pollfunc)
		: 0;
}
EXPORT_SYMBOL(iio_triggered_ring_postenable);

int iio_triggered_ring_predisable(struct iio_dev *indio_dev)
{
	return indio_dev->trig
		? iio_trigger_dettach_poll_func(indio_dev->trig,
						indio_dev->pollfunc)
		: 0;
}
EXPORT_SYMBOL(iio_triggered_ring_predisable);

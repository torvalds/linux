// SPDX-License-Identifier: GPL-2.0
/*
 * Wakeup statistics in sysfs
 *
 * Copyright (c) 2019 Linux Foundation
 * Copyright (c) 2019 Greg Kroah-Hartman <gregkh@linuxfoundation.org>
 * Copyright (c) 2019 Google Inc.
 */

#include <linux/device.h>
#include <linux/idr.h>
#include <linux/init.h>
#include <linux/kdev_t.h>
#include <linux/kernel.h>
#include <linux/kobject.h>
#include <linux/slab.h>
#include <linux/timekeeping.h>

#include "power.h"

static struct class *wakeup_class;

#define wakeup_attr(_name)						\
static ssize_t _name##_show(struct device *dev,				\
			    struct device_attribute *attr, char *buf)	\
{									\
	struct wakeup_source *ws = dev_get_drvdata(dev);		\
									\
	return sysfs_emit(buf, "%lu\n", ws->_name);			\
}									\
static DEVICE_ATTR_RO(_name)

wakeup_attr(active_count);
wakeup_attr(event_count);
wakeup_attr(wakeup_count);
wakeup_attr(expire_count);

static ssize_t active_time_ms_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct wakeup_source *ws = dev_get_drvdata(dev);
	ktime_t active_time =
		ws->active ? ktime_sub(ktime_get(), ws->last_time) : 0;

	return sysfs_emit(buf, "%lld\n", ktime_to_ms(active_time));
}
static DEVICE_ATTR_RO(active_time_ms);

static ssize_t total_time_ms_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct wakeup_source *ws = dev_get_drvdata(dev);
	ktime_t active_time;
	ktime_t total_time = ws->total_time;

	if (ws->active) {
		active_time = ktime_sub(ktime_get(), ws->last_time);
		total_time = ktime_add(total_time, active_time);
	}

	return sysfs_emit(buf, "%lld\n", ktime_to_ms(total_time));
}
static DEVICE_ATTR_RO(total_time_ms);

static ssize_t max_time_ms_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct wakeup_source *ws = dev_get_drvdata(dev);
	ktime_t active_time;
	ktime_t max_time = ws->max_time;

	if (ws->active) {
		active_time = ktime_sub(ktime_get(), ws->last_time);
		if (active_time > max_time)
			max_time = active_time;
	}

	return sysfs_emit(buf, "%lld\n", ktime_to_ms(max_time));
}
static DEVICE_ATTR_RO(max_time_ms);

static ssize_t last_change_ms_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct wakeup_source *ws = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%lld\n", ktime_to_ms(ws->last_time));
}
static DEVICE_ATTR_RO(last_change_ms);

static ssize_t name_show(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	struct wakeup_source *ws = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%s\n", ws->name);
}
static DEVICE_ATTR_RO(name);

static ssize_t prevent_suspend_time_ms_show(struct device *dev,
					    struct device_attribute *attr,
					    char *buf)
{
	struct wakeup_source *ws = dev_get_drvdata(dev);
	ktime_t prevent_sleep_time = ws->prevent_sleep_time;

	if (ws->active && ws->autosleep_enabled) {
		prevent_sleep_time = ktime_add(prevent_sleep_time,
			ktime_sub(ktime_get(), ws->start_prevent_time));
	}

	return sysfs_emit(buf, "%lld\n", ktime_to_ms(prevent_sleep_time));
}
static DEVICE_ATTR_RO(prevent_suspend_time_ms);

static struct attribute *wakeup_source_attrs[] = {
	&dev_attr_name.attr,
	&dev_attr_active_count.attr,
	&dev_attr_event_count.attr,
	&dev_attr_wakeup_count.attr,
	&dev_attr_expire_count.attr,
	&dev_attr_active_time_ms.attr,
	&dev_attr_total_time_ms.attr,
	&dev_attr_max_time_ms.attr,
	&dev_attr_last_change_ms.attr,
	&dev_attr_prevent_suspend_time_ms.attr,
	NULL,
};
ATTRIBUTE_GROUPS(wakeup_source);

static void device_create_release(struct device *dev)
{
	kfree(dev);
}

static struct device *wakeup_source_device_create(struct device *parent,
						  struct wakeup_source *ws)
{
	struct device *dev = NULL;
	int retval;

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev) {
		retval = -ENOMEM;
		goto error;
	}

	device_initialize(dev);
	dev->devt = MKDEV(0, 0);
	dev->class = wakeup_class;
	dev->parent = parent;
	dev->groups = wakeup_source_groups;
	dev->release = device_create_release;
	dev_set_drvdata(dev, ws);
	device_set_pm_not_required(dev);

	retval = dev_set_name(dev, "wakeup%d", ws->id);
	if (retval)
		goto error;

	retval = device_add(dev);
	if (retval)
		goto error;

	return dev;

error:
	put_device(dev);
	return ERR_PTR(retval);
}

/**
 * wakeup_source_sysfs_add - Add wakeup_source attributes to sysfs.
 * @parent: Device given wakeup source is associated with (or NULL if virtual).
 * @ws: Wakeup source to be added in sysfs.
 */
int wakeup_source_sysfs_add(struct device *parent, struct wakeup_source *ws)
{
	struct device *dev;

	dev = wakeup_source_device_create(parent, ws);
	if (IS_ERR(dev))
		return PTR_ERR(dev);
	ws->dev = dev;

	return 0;
}

/**
 * pm_wakeup_source_sysfs_add - Add wakeup_source attributes to sysfs
 * for a device if they're missing.
 * @parent: Device given wakeup source is associated with
 */
int pm_wakeup_source_sysfs_add(struct device *parent)
{
	if (!parent->power.wakeup || parent->power.wakeup->dev)
		return 0;

	return wakeup_source_sysfs_add(parent, parent->power.wakeup);
}

/**
 * wakeup_source_sysfs_remove - Remove wakeup_source attributes from sysfs.
 * @ws: Wakeup source to be removed from sysfs.
 */
void wakeup_source_sysfs_remove(struct wakeup_source *ws)
{
	device_unregister(ws->dev);
}

static int __init wakeup_sources_sysfs_init(void)
{
	wakeup_class = class_create("wakeup");

	return PTR_ERR_OR_ZERO(wakeup_class);
}
postcore_initcall(wakeup_sources_sysfs_init);

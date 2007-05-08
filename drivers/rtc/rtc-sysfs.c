/*
 * RTC subsystem, sysfs interface
 *
 * Copyright (C) 2005 Tower Technologies
 * Author: Alessandro Zummo <a.zummo@towertech.it>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/module.h>
#include <linux/rtc.h>

#include "rtc-core.h"


/* device attributes */

static ssize_t rtc_sysfs_show_name(struct class_device *dev, char *buf)
{
	return sprintf(buf, "%s\n", to_rtc_device(dev)->name);
}
static CLASS_DEVICE_ATTR(name, S_IRUGO, rtc_sysfs_show_name, NULL);

static ssize_t rtc_sysfs_show_date(struct class_device *dev, char *buf)
{
	ssize_t retval;
	struct rtc_time tm;

	retval = rtc_read_time(to_rtc_device(dev), &tm);
	if (retval == 0) {
		retval = sprintf(buf, "%04d-%02d-%02d\n",
			tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
	}

	return retval;
}
static CLASS_DEVICE_ATTR(date, S_IRUGO, rtc_sysfs_show_date, NULL);

static ssize_t rtc_sysfs_show_time(struct class_device *dev, char *buf)
{
	ssize_t retval;
	struct rtc_time tm;

	retval = rtc_read_time(to_rtc_device(dev), &tm);
	if (retval == 0) {
		retval = sprintf(buf, "%02d:%02d:%02d\n",
			tm.tm_hour, tm.tm_min, tm.tm_sec);
	}

	return retval;
}
static CLASS_DEVICE_ATTR(time, S_IRUGO, rtc_sysfs_show_time, NULL);

static ssize_t rtc_sysfs_show_since_epoch(struct class_device *dev, char *buf)
{
	ssize_t retval;
	struct rtc_time tm;

	retval = rtc_read_time(to_rtc_device(dev), &tm);
	if (retval == 0) {
		unsigned long time;
		rtc_tm_to_time(&tm, &time);
		retval = sprintf(buf, "%lu\n", time);
	}

	return retval;
}
static CLASS_DEVICE_ATTR(since_epoch, S_IRUGO, rtc_sysfs_show_since_epoch, NULL);

static struct attribute *rtc_attrs[] = {
	&class_device_attr_name.attr,
	&class_device_attr_date.attr,
	&class_device_attr_time.attr,
	&class_device_attr_since_epoch.attr,
	NULL,
};

static struct attribute_group rtc_attr_group = {
	.attrs = rtc_attrs,
};


static ssize_t
rtc_sysfs_show_wakealarm(struct class_device *dev, char *buf)
{
	ssize_t retval;
	unsigned long alarm;
	struct rtc_wkalrm alm;

	/* Don't show disabled alarms; but the RTC could leave the
	 * alarm enabled after it's already triggered.  Alarms are
	 * conceptually one-shot, even though some common hardware
	 * (PCs) doesn't actually work that way.
	 *
	 * REVISIT maybe we should require RTC implementations to
	 * disable the RTC alarm after it triggers, for uniformity.
	 */
	retval = rtc_read_alarm(to_rtc_device(dev), &alm);
	if (retval == 0 && alm.enabled) {
		rtc_tm_to_time(&alm.time, &alarm);
		retval = sprintf(buf, "%lu\n", alarm);
	}

	return retval;
}

static ssize_t
rtc_sysfs_set_wakealarm(struct class_device *dev, const char *buf, size_t n)
{
	ssize_t retval;
	unsigned long now, alarm;
	struct rtc_wkalrm alm;
	struct rtc_device *rtc = to_rtc_device(dev);

	/* Only request alarms that trigger in the future.  Disable them
	 * by writing another time, e.g. 0 meaning Jan 1 1970 UTC.
	 */
	retval = rtc_read_time(rtc, &alm.time);
	if (retval < 0)
		return retval;
	rtc_tm_to_time(&alm.time, &now);

	alarm = simple_strtoul(buf, NULL, 0);
	if (alarm > now) {
		/* Avoid accidentally clobbering active alarms; we can't
		 * entirely prevent that here, without even the minimal
		 * locking from the /dev/rtcN api.
		 */
		retval = rtc_read_alarm(rtc, &alm);
		if (retval < 0)
			return retval;
		if (alm.enabled)
			return -EBUSY;

		alm.enabled = 1;
	} else {
		alm.enabled = 0;

		/* Provide a valid future alarm time.  Linux isn't EFI,
		 * this time won't be ignored when disabling the alarm.
		 */
		alarm = now + 300;
	}
	rtc_time_to_tm(alarm, &alm.time);

	retval = rtc_set_alarm(rtc, &alm);
	return (retval < 0) ? retval : n;
}
static const CLASS_DEVICE_ATTR(wakealarm, S_IRUGO | S_IWUSR,
		rtc_sysfs_show_wakealarm, rtc_sysfs_set_wakealarm);


/* The reason to trigger an alarm with no process watching it (via sysfs)
 * is its side effect:  waking from a system state like suspend-to-RAM or
 * suspend-to-disk.  So: no attribute unless that side effect is possible.
 * (Userspace may disable that mechanism later.)
 */
static inline int rtc_does_wakealarm(struct class_device *class_dev)
{
	struct rtc_device *rtc;

	if (!device_can_wakeup(class_dev->dev))
		return 0;
	rtc = to_rtc_device(class_dev);
	return rtc->ops->set_alarm != NULL;
}


static int rtc_sysfs_add_device(struct class_device *class_dev,
					struct class_interface *class_intf)
{
	int err;

	dev_dbg(class_dev->dev, "rtc intf: sysfs\n");

	err = sysfs_create_group(&class_dev->kobj, &rtc_attr_group);
	if (err)
		dev_err(class_dev->dev, "failed to create %s\n",
				"sysfs attributes");
	else if (rtc_does_wakealarm(class_dev)) {
		/* not all RTCs support both alarms and wakeup */
		err = class_device_create_file(class_dev,
					&class_device_attr_wakealarm);
		if (err) {
			dev_err(class_dev->dev, "failed to create %s\n",
					"alarm attribute");
			sysfs_remove_group(&class_dev->kobj, &rtc_attr_group);
		}
	}

	return err;
}

static void rtc_sysfs_remove_device(struct class_device *class_dev,
				struct class_interface *class_intf)
{
	if (rtc_does_wakealarm(class_dev))
		class_device_remove_file(class_dev,
				&class_device_attr_wakealarm);
	sysfs_remove_group(&class_dev->kobj, &rtc_attr_group);
}

/* interface registration */

static struct class_interface rtc_sysfs_interface = {
	.add = &rtc_sysfs_add_device,
	.remove = &rtc_sysfs_remove_device,
};

static int __init rtc_sysfs_init(void)
{
	return rtc_interface_register(&rtc_sysfs_interface);
}

static void __exit rtc_sysfs_exit(void)
{
	class_interface_unregister(&rtc_sysfs_interface);
}

subsys_initcall(rtc_sysfs_init);
module_exit(rtc_sysfs_exit);

MODULE_AUTHOR("Alessandro Zummo <a.zummo@towertech.it>");
MODULE_DESCRIPTION("RTC class sysfs interface");
MODULE_LICENSE("GPL");

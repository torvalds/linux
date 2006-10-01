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

	retval = rtc_read_time(dev, &tm);
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

	retval = rtc_read_time(dev, &tm);
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

	retval = rtc_read_time(dev, &tm);
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

static int __devinit rtc_sysfs_add_device(struct class_device *class_dev,
					struct class_interface *class_intf)
{
	int err;

	dev_info(class_dev->dev, "rtc intf: sysfs\n");

	err = sysfs_create_group(&class_dev->kobj, &rtc_attr_group);
	if (err)
		dev_err(class_dev->dev,
			"failed to create sysfs attributes\n");

	return err;
}

static void rtc_sysfs_remove_device(struct class_device *class_dev,
				struct class_interface *class_intf)
{
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

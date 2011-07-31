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

/*
 * NOTE:  RTC times displayed in sysfs use the RTC's timezone.  That's
 * ideally UTC.  However, PCs that also boot to MS-Windows normally use
 * the local time and change to match daylight savings time.  That affects
 * attributes including date, time, since_epoch, and wakealarm.
 */

static ssize_t
rtc_sysfs_show_name(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	return sprintf(buf, "%s\n", to_rtc_device(dev)->name);
}

static ssize_t
rtc_sysfs_show_date(struct device *dev, struct device_attribute *attr,
		char *buf)
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

static ssize_t
rtc_sysfs_show_time(struct device *dev, struct device_attribute *attr,
		char *buf)
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

static ssize_t
rtc_sysfs_show_since_epoch(struct device *dev, struct device_attribute *attr,
		char *buf)
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

static ssize_t
rtc_sysfs_show_max_user_freq(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	return sprintf(buf, "%d\n", to_rtc_device(dev)->max_user_freq);
}

static ssize_t
rtc_sysfs_set_max_user_freq(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t n)
{
	struct rtc_device *rtc = to_rtc_device(dev);
	unsigned long val = simple_strtoul(buf, NULL, 0);

	if (val >= 4096 || val == 0)
		return -EINVAL;

	rtc->max_user_freq = (int)val;

	return n;
}

static ssize_t
rtc_sysfs_show_hctosys(struct device *dev, struct device_attribute *attr,
		char *buf)
{
#ifdef CONFIG_RTC_HCTOSYS_DEVICE
	if (rtc_hctosys_ret == 0 &&
			strcmp(dev_name(&to_rtc_device(dev)->dev),
				CONFIG_RTC_HCTOSYS_DEVICE) == 0)
		return sprintf(buf, "1\n");
	else
#endif
		return sprintf(buf, "0\n");
}

static struct device_attribute rtc_attrs[] = {
	__ATTR(name, S_IRUGO, rtc_sysfs_show_name, NULL),
	__ATTR(date, S_IRUGO, rtc_sysfs_show_date, NULL),
	__ATTR(time, S_IRUGO, rtc_sysfs_show_time, NULL),
	__ATTR(since_epoch, S_IRUGO, rtc_sysfs_show_since_epoch, NULL),
	__ATTR(max_user_freq, S_IRUGO | S_IWUSR, rtc_sysfs_show_max_user_freq,
			rtc_sysfs_set_max_user_freq),
	__ATTR(hctosys, S_IRUGO, rtc_sysfs_show_hctosys, NULL),
	{ },
};

static ssize_t
rtc_sysfs_show_wakealarm(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	ssize_t retval;
	unsigned long alarm;
	struct rtc_wkalrm alm;

	/* Don't show disabled alarms.  For uniformity, RTC alarms are
	 * conceptually one-shot, even though some common RTCs (on PCs)
	 * don't actually work that way.
	 *
	 * NOTE: RTC implementations where the alarm doesn't match an
	 * exact YYYY-MM-DD HH:MM[:SS] date *must* disable their RTC
	 * alarms after they trigger, to ensure one-shot semantics.
	 */
	retval = rtc_read_alarm(to_rtc_device(dev), &alm);
	if (retval == 0 && alm.enabled) {
		rtc_tm_to_time(&alm.time, &alarm);
		retval = sprintf(buf, "%lu\n", alarm);
	}

	return retval;
}

static ssize_t
rtc_sysfs_set_wakealarm(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t n)
{
	ssize_t retval;
	unsigned long now, alarm;
	struct rtc_wkalrm alm;
	struct rtc_device *rtc = to_rtc_device(dev);
	char *buf_ptr;
	int adjust = 0;

	/* Only request alarms that trigger in the future.  Disable them
	 * by writing another time, e.g. 0 meaning Jan 1 1970 UTC.
	 */
	retval = rtc_read_time(rtc, &alm.time);
	if (retval < 0)
		return retval;
	rtc_tm_to_time(&alm.time, &now);

	buf_ptr = (char *)buf;
	if (*buf_ptr == '+') {
		buf_ptr++;
		adjust = 1;
	}
	alarm = simple_strtoul(buf_ptr, NULL, 0);
	if (adjust) {
		alarm += now;
	}
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
static DEVICE_ATTR(wakealarm, S_IRUGO | S_IWUSR,
		rtc_sysfs_show_wakealarm, rtc_sysfs_set_wakealarm);


/* The reason to trigger an alarm with no process watching it (via sysfs)
 * is its side effect:  waking from a system state like suspend-to-RAM or
 * suspend-to-disk.  So: no attribute unless that side effect is possible.
 * (Userspace may disable that mechanism later.)
 */
static inline int rtc_does_wakealarm(struct rtc_device *rtc)
{
	if (!device_can_wakeup(rtc->dev.parent))
		return 0;
	return rtc->ops->set_alarm != NULL;
}


void rtc_sysfs_add_device(struct rtc_device *rtc)
{
	int err;

	/* not all RTCs support both alarms and wakeup */
	if (!rtc_does_wakealarm(rtc))
		return;

	err = device_create_file(&rtc->dev, &dev_attr_wakealarm);
	if (err)
		dev_err(rtc->dev.parent,
			"failed to create alarm attribute, %d\n", err);
}

void rtc_sysfs_del_device(struct rtc_device *rtc)
{
	/* REVISIT did we add it successfully? */
	if (rtc_does_wakealarm(rtc))
		device_remove_file(&rtc->dev, &dev_attr_wakealarm);
}

void __init rtc_sysfs_init(struct class *rtc_class)
{
	rtc_class->dev_attrs = rtc_attrs;
}

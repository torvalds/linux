// SPDX-License-Identifier: GPL-2.0
/*
 * RTC subsystem, sysfs interface
 *
 * Copyright (C) 2005 Tower Technologies
 * Author: Alessandro Zummo <a.zummo@towertech.it>
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
name_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%s %s\n", dev_driver_string(dev->parent),
		       dev_name(dev->parent));
}
static DEVICE_ATTR_RO(name);

static ssize_t
date_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	ssize_t retval;
	struct rtc_time tm;

	retval = rtc_read_time(to_rtc_device(dev), &tm);
	if (retval)
		return retval;

	return sprintf(buf, "%ptRd\n", &tm);
}
static DEVICE_ATTR_RO(date);

static ssize_t
time_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	ssize_t retval;
	struct rtc_time tm;

	retval = rtc_read_time(to_rtc_device(dev), &tm);
	if (retval)
		return retval;

	return sprintf(buf, "%ptRt\n", &tm);
}
static DEVICE_ATTR_RO(time);

static ssize_t
since_epoch_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	ssize_t retval;
	struct rtc_time tm;

	retval = rtc_read_time(to_rtc_device(dev), &tm);
	if (retval == 0) {
		time64_t time;

		time = rtc_tm_to_time64(&tm);
		retval = sprintf(buf, "%lld\n", time);
	}

	return retval;
}
static DEVICE_ATTR_RO(since_epoch);

static ssize_t
max_user_freq_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", to_rtc_device(dev)->max_user_freq);
}

static ssize_t
max_user_freq_store(struct device *dev, struct device_attribute *attr,
		    const char *buf, size_t n)
{
	struct rtc_device *rtc = to_rtc_device(dev);
	unsigned long val;
	int err;

	err = kstrtoul(buf, 0, &val);
	if (err)
		return err;

	if (val >= 4096 || val == 0)
		return -EINVAL;

	rtc->max_user_freq = (int)val;

	return n;
}
static DEVICE_ATTR_RW(max_user_freq);

/**
 * rtc_sysfs_show_hctosys - indicate if the given RTC set the system time
 *
 * Returns 1 if the system clock was set by this RTC at the last
 * boot or resume event.
 */
static ssize_t
hctosys_show(struct device *dev, struct device_attribute *attr, char *buf)
{
#ifdef CONFIG_RTC_HCTOSYS_DEVICE
	if (rtc_hctosys_ret == 0 &&
	    strcmp(dev_name(&to_rtc_device(dev)->dev),
		   CONFIG_RTC_HCTOSYS_DEVICE) == 0)
		return sprintf(buf, "1\n");
#endif
	return sprintf(buf, "0\n");
}
static DEVICE_ATTR_RO(hctosys);

static ssize_t
wakealarm_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	ssize_t retval;
	time64_t alarm;
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
		alarm = rtc_tm_to_time64(&alm.time);
		retval = sprintf(buf, "%lld\n", alarm);
	}

	return retval;
}

static ssize_t
wakealarm_store(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t n)
{
	ssize_t retval;
	time64_t now, alarm;
	time64_t push = 0;
	struct rtc_wkalrm alm;
	struct rtc_device *rtc = to_rtc_device(dev);
	const char *buf_ptr;
	int adjust = 0;

	/* Only request alarms that trigger in the future.  Disable them
	 * by writing another time, e.g. 0 meaning Jan 1 1970 UTC.
	 */
	retval = rtc_read_time(rtc, &alm.time);
	if (retval < 0)
		return retval;
	now = rtc_tm_to_time64(&alm.time);

	buf_ptr = buf;
	if (*buf_ptr == '+') {
		buf_ptr++;
		if (*buf_ptr == '=') {
			buf_ptr++;
			push = 1;
		} else {
			adjust = 1;
		}
	}
	retval = kstrtos64(buf_ptr, 0, &alarm);
	if (retval)
		return retval;
	if (adjust)
		alarm += now;
	if (alarm > now || push) {
		/* Avoid accidentally clobbering active alarms; we can't
		 * entirely prevent that here, without even the minimal
		 * locking from the /dev/rtcN api.
		 */
		retval = rtc_read_alarm(rtc, &alm);
		if (retval < 0)
			return retval;
		if (alm.enabled) {
			if (push) {
				push = rtc_tm_to_time64(&alm.time);
				alarm += push;
			} else
				return -EBUSY;
		} else if (push)
			return -EINVAL;
		alm.enabled = 1;
	} else {
		alm.enabled = 0;

		/* Provide a valid future alarm time.  Linux isn't EFI,
		 * this time won't be ignored when disabling the alarm.
		 */
		alarm = now + 300;
	}
	rtc_time64_to_tm(alarm, &alm.time);

	retval = rtc_set_alarm(rtc, &alm);
	return (retval < 0) ? retval : n;
}
static DEVICE_ATTR_RW(wakealarm);

static ssize_t
offset_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	ssize_t retval;
	long offset;

	retval = rtc_read_offset(to_rtc_device(dev), &offset);
	if (retval == 0)
		retval = sprintf(buf, "%ld\n", offset);

	return retval;
}

static ssize_t
offset_store(struct device *dev, struct device_attribute *attr,
	     const char *buf, size_t n)
{
	ssize_t retval;
	long offset;

	retval = kstrtol(buf, 10, &offset);
	if (retval == 0)
		retval = rtc_set_offset(to_rtc_device(dev), offset);

	return (retval < 0) ? retval : n;
}
static DEVICE_ATTR_RW(offset);

static ssize_t
range_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "[%lld,%llu]\n", to_rtc_device(dev)->range_min,
		       to_rtc_device(dev)->range_max);
}
static DEVICE_ATTR_RO(range);

static struct attribute *rtc_attrs[] = {
	&dev_attr_name.attr,
	&dev_attr_date.attr,
	&dev_attr_time.attr,
	&dev_attr_since_epoch.attr,
	&dev_attr_max_user_freq.attr,
	&dev_attr_hctosys.attr,
	&dev_attr_wakealarm.attr,
	&dev_attr_offset.attr,
	&dev_attr_range.attr,
	NULL,
};

/* The reason to trigger an alarm with no process watching it (via sysfs)
 * is its side effect:  waking from a system state like suspend-to-RAM or
 * suspend-to-disk.  So: no attribute unless that side effect is possible.
 * (Userspace may disable that mechanism later.)
 */
static bool rtc_does_wakealarm(struct rtc_device *rtc)
{
	if (!device_can_wakeup(rtc->dev.parent))
		return false;

	return rtc->ops->set_alarm != NULL;
}

static umode_t rtc_attr_is_visible(struct kobject *kobj,
				   struct attribute *attr, int n)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	struct rtc_device *rtc = to_rtc_device(dev);
	umode_t mode = attr->mode;

	if (attr == &dev_attr_wakealarm.attr) {
		if (!rtc_does_wakealarm(rtc))
			mode = 0;
	} else if (attr == &dev_attr_offset.attr) {
		if (!rtc->ops->set_offset)
			mode = 0;
	} else if (attr == &dev_attr_range.attr) {
		if (!(rtc->range_max - rtc->range_min))
			mode = 0;
	}

	return mode;
}

static struct attribute_group rtc_attr_group = {
	.is_visible	= rtc_attr_is_visible,
	.attrs		= rtc_attrs,
};

static const struct attribute_group *rtc_attr_groups[] = {
	&rtc_attr_group,
	NULL
};

const struct attribute_group **rtc_get_dev_attribute_groups(void)
{
	return rtc_attr_groups;
}

int rtc_add_groups(struct rtc_device *rtc, const struct attribute_group **grps)
{
	size_t old_cnt = 0, add_cnt = 0, new_cnt;
	const struct attribute_group **groups, **old;

	if (rtc->registered)
		return -EINVAL;
	if (!grps)
		return -EINVAL;

	groups = rtc->dev.groups;
	if (groups)
		for (; *groups; groups++)
			old_cnt++;

	for (groups = grps; *groups; groups++)
		add_cnt++;

	new_cnt = old_cnt + add_cnt + 1;
	groups = devm_kcalloc(&rtc->dev, new_cnt, sizeof(*groups), GFP_KERNEL);
	if (!groups)
		return -ENOMEM;
	memcpy(groups, rtc->dev.groups, old_cnt * sizeof(*groups));
	memcpy(groups + old_cnt, grps, add_cnt * sizeof(*groups));
	groups[old_cnt + add_cnt] = NULL;

	old = rtc->dev.groups;
	rtc->dev.groups = groups;
	if (old && old != rtc_attr_groups)
		devm_kfree(&rtc->dev, old);

	return 0;
}
EXPORT_SYMBOL(rtc_add_groups);

int rtc_add_group(struct rtc_device *rtc, const struct attribute_group *grp)
{
	const struct attribute_group *groups[] = { grp, NULL };

	return rtc_add_groups(rtc, groups);
}
EXPORT_SYMBOL(rtc_add_group);

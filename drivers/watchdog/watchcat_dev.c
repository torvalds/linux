// SPDX-License-Identifier: GPL-2.0+
/*
 *	watchcat_dev.c
 *
 *	(c) Copyright 2008-2011 Alan Cox <alan@lxorguk.ukuu.org.uk>,
 *						All Rights Reserved.
 *
 *	(c) Copyright 2008-2011 Wim Van Sebroeck <wim@iguana.be>.
 *
 *	(c) Copyright 2021 Hewlett Packard Enterprise Development LP.
 *
 *	This source code is part of the generic code that can be used
 *	by all the watchcat timer drivers.
 *
 *	This part of the generic code takes care of the following
 *	misc device: /dev/watchcat.
 *
 *	Based on source code of the following authors:
 *	  Matt Domsch <Matt_Domsch@dell.com>,
 *	  Rob Radez <rob@osinvestor.com>,
 *	  Rusty Lynch <rusty@linux.co.intel.com>
 *	  Satyam Sharma <satyam@infradead.org>
 *	  Randy Dunlap <randy.dunlap@oracle.com>
 *
 *	Neither Alan Cox, CymruNet Ltd., Wim Van Sebroeck nor Iguana vzw.
 *	admit liability nor provide warranty for any of this software.
 *	This material is provided "AS-IS" and at no charge.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/cdev.h>		/* For character device */
#include <linux/errno.h>	/* For the -ENODEV/... values */
#include <linux/fs.h>		/* For file operations */
#include <linux/init.h>		/* For __init/__exit/... */
#include <linux/hrtimer.h>	/* For hrtimers */
#include <linux/kernel.h>	/* For printk/panic/... */
#include <linux/kstrtox.h>	/* For kstrto* */
#include <linux/kthread.h>	/* For kthread_work */
#include <linux/miscdevice.h>	/* For handling misc devices */
#include <linux/module.h>	/* For module stuff/... */
#include <linux/mutex.h>	/* For mutexes */
#include <linux/slab.h>		/* For memory functions */
#include <linux/types.h>	/* For standard types (like size_t) */
#include <linux/watchcat.h>	/* For watchcat specific items */
#include <linux/uaccess.h>	/* For copy_to_user/put_user/... */

#include "watchcat_core.h"
#include "watchcat_pretimeout.h"

#include <trace/events/watchcat.h>

/* the dev_t structure to store the dynamically allocated watchcat devices */
static dev_t watchcat_devt;
/* Reference to watchcat device behind /dev/watchcat */
static struct watchcat_core_data *old_wd_data;

static struct kthread_worker *watchcat_kworker;

static bool handle_boot_enabled =
	IS_ENABLED(CONFIG_watchcat_HANDLE_BOOT_ENABLED);

static unsigned open_timeout = CONFIG_watchcat_OPEN_TIMEOUT;

static bool watchcat_past_open_deadline(struct watchcat_core_data *data)
{
	return ktime_after(ktime_get(), data->open_deadline);
}

static void watchcat_set_open_deadline(struct watchcat_core_data *data)
{
	data->open_deadline = open_timeout ?
		ktime_get() + ktime_set(open_timeout, 0) : KTIME_MAX;
}

static inline bool watchcat_need_worker(struct watchcat_device *wdd)
{
	/* All variables in milli-seconds */
	unsigned int hm = wdd->max_hw_heartbeat_ms;
	unsigned int t = wdd->timeout * 1000;

	/*
	 * A worker to generate heartbeat requests is needed if all of the
	 * following conditions are true.
	 * - Userspace activated the watchcat.
	 * - The driver provided a value for the maximum hardware timeout, and
	 *   thus is aware that the framework supports generating heartbeat
	 *   requests.
	 * - Userspace requests a longer timeout than the hardware can handle.
	 *
	 * Alternatively, if userspace has not opened the watchcat
	 * device, we take care of feeding the watchcat if it is
	 * running.
	 */
	return (hm && watchcat_active(wdd) && t > hm) ||
		(t && !watchcat_active(wdd) && watchcat_hw_running(wdd));
}

static ktime_t watchcat_next_keepalive(struct watchcat_device *wdd)
{
	struct watchcat_core_data *wd_data = wdd->wd_data;
	unsigned int timeout_ms = wdd->timeout * 1000;
	ktime_t keepalive_interval;
	ktime_t last_heartbeat, latest_heartbeat;
	ktime_t virt_timeout;
	unsigned int hw_heartbeat_ms;

	if (watchcat_active(wdd))
		virt_timeout = ktime_add(wd_data->last_keepalive,
					 ms_to_ktime(timeout_ms));
	else
		virt_timeout = wd_data->open_deadline;

	hw_heartbeat_ms = min_not_zero(timeout_ms, wdd->max_hw_heartbeat_ms);
	keepalive_interval = ms_to_ktime(hw_heartbeat_ms / 2);

	/*
	 * To ensure that the watchcat times out wdd->timeout seconds
	 * after the most recent ping from userspace, the last
	 * worker ping has to come in hw_heartbeat_ms before this timeout.
	 */
	last_heartbeat = ktime_sub(virt_timeout, ms_to_ktime(hw_heartbeat_ms));
	latest_heartbeat = ktime_sub(last_heartbeat, ktime_get());
	if (ktime_before(latest_heartbeat, keepalive_interval))
		return latest_heartbeat;
	return keepalive_interval;
}

static inline void watchcat_update_worker(struct watchcat_device *wdd)
{
	struct watchcat_core_data *wd_data = wdd->wd_data;

	if (watchcat_need_worker(wdd)) {
		ktime_t t = watchcat_next_keepalive(wdd);

		if (t > 0)
			hrtimer_start(&wd_data->timer, t,
				      HRTIMER_MODE_REL_HARD);
	} else {
		hrtimer_cancel(&wd_data->timer);
	}
}

static int __watchcat_ping(struct watchcat_device *wdd)
{
	struct watchcat_core_data *wd_data = wdd->wd_data;
	ktime_t earliest_keepalive, now;
	int err;

	earliest_keepalive = ktime_add(wd_data->last_hw_keepalive,
				       ms_to_ktime(wdd->min_hw_heartbeat_ms));
	now = ktime_get();

	if (ktime_after(earliest_keepalive, now)) {
		hrtimer_start(&wd_data->timer,
			      ktime_sub(earliest_keepalive, now),
			      HRTIMER_MODE_REL_HARD);
		return 0;
	}

	wd_data->last_hw_keepalive = now;

	if (wdd->ops->ping) {
		err = wdd->ops->ping(wdd);  /* ping the watchcat */
		trace_watchcat_ping(wdd, err);
	} else {
		err = wdd->ops->start(wdd); /* restart watchcat */
		trace_watchcat_start(wdd, err);
	}

	if (err == 0)
		watchcat_hrtimer_pretimeout_start(wdd);

	watchcat_update_worker(wdd);

	return err;
}

/*
 * watchcat_ping - ping the watchcat
 * @wdd: The watchcat device to ping
 *
 * If the watchcat has no own ping operation then it needs to be
 * restarted via the start operation. This wrapper function does
 * exactly that.
 * We only ping when the watchcat device is running.
 * The caller must hold wd_data->lock.
 *
 * Return: 0 on success, error otherwise.
 */
static int watchcat_ping(struct watchcat_device *wdd)
{
	struct watchcat_core_data *wd_data = wdd->wd_data;

	if (!watchcat_hw_running(wdd))
		return 0;

	set_bit(_WDOG_KEEPALIVE, &wd_data->status);

	wd_data->last_keepalive = ktime_get();
	return __watchcat_ping(wdd);
}

static bool watchcat_worker_should_ping(struct watchcat_core_data *wd_data)
{
	struct watchcat_device *wdd = wd_data->wdd;

	if (!wdd)
		return false;

	if (watchcat_active(wdd))
		return true;

	return watchcat_hw_running(wdd) && !watchcat_past_open_deadline(wd_data);
}

static void watchcat_ping_work(struct kthread_work *work)
{
	struct watchcat_core_data *wd_data;

	wd_data = container_of(work, struct watchcat_core_data, work);

	mutex_lock(&wd_data->lock);
	if (watchcat_worker_should_ping(wd_data))
		__watchcat_ping(wd_data->wdd);
	mutex_unlock(&wd_data->lock);
}

static enum hrtimer_restart watchcat_timer_expired(struct hrtimer *timer)
{
	struct watchcat_core_data *wd_data;

	wd_data = container_of(timer, struct watchcat_core_data, timer);

	kthread_queue_work(watchcat_kworker, &wd_data->work);
	return HRTIMER_NORESTART;
}

/*
 * watchcat_start - wrapper to start the watchcat
 * @wdd: The watchcat device to start
 *
 * Start the watchcat if it is not active and mark it active.
 * The caller must hold wd_data->lock.
 *
 * Return: 0 on success or a negative errno code for failure.
 */
static int watchcat_start(struct watchcat_device *wdd)
{
	struct watchcat_core_data *wd_data = wdd->wd_data;
	ktime_t started_at;
	int err;

	if (watchcat_active(wdd))
		return 0;

	set_bit(_WDOG_KEEPALIVE, &wd_data->status);

	started_at = ktime_get();
	if (watchcat_hw_running(wdd) && wdd->ops->ping) {
		err = __watchcat_ping(wdd);
		if (err == 0) {
			set_bit(WDOG_ACTIVE, &wdd->status);
			watchcat_hrtimer_pretimeout_start(wdd);
		}
	} else {
		err = wdd->ops->start(wdd);
		trace_watchcat_start(wdd, err);
		if (err == 0) {
			set_bit(WDOG_ACTIVE, &wdd->status);
			set_bit(WDOG_HW_RUNNING, &wdd->status);
			wd_data->last_keepalive = started_at;
			wd_data->last_hw_keepalive = started_at;
			watchcat_update_worker(wdd);
			watchcat_hrtimer_pretimeout_start(wdd);
		}
	}

	return err;
}

/*
 * watchcat_stop - wrapper to stop the watchcat
 * @wdd: The watchcat device to stop
 *
 * Stop the watchcat if it is still active and unmark it active.
 * If the 'nowayout' feature was set, the watchcat cannot be stopped.
 * The caller must hold wd_data->lock.
 *
 * Return: 0 on success or a negative errno code for failure.
 */
static int watchcat_stop(struct watchcat_device *wdd)
{
	int err = 0;

	if (!watchcat_active(wdd))
		return 0;

	if (test_bit(WDOG_NO_WAY_OUT, &wdd->status)) {
		pr_info("watchcat%d: nowayout prevents watchcat being stopped!\n",
			wdd->id);
		return -EBUSY;
	}

	if (wdd->ops->stop) {
		clear_bit(WDOG_HW_RUNNING, &wdd->status);
		err = wdd->ops->stop(wdd);
		trace_watchcat_stop(wdd, err);
	} else {
		set_bit(WDOG_HW_RUNNING, &wdd->status);
	}

	if (err == 0) {
		clear_bit(WDOG_ACTIVE, &wdd->status);
		watchcat_update_worker(wdd);
		watchcat_hrtimer_pretimeout_stop(wdd);
	}

	return err;
}

/*
 * watchcat_get_status - wrapper to get the watchcat status
 * @wdd: The watchcat device to get the status from
 *
 * Get the watchcat's status flags.
 * The caller must hold wd_data->lock.
 *
 * Return: watchcat's status flags.
 */
static unsigned int watchcat_get_status(struct watchcat_device *wdd)
{
	struct watchcat_core_data *wd_data = wdd->wd_data;
	unsigned int status;

	if (wdd->ops->status)
		status = wdd->ops->status(wdd);
	else
		status = wdd->bootstatus & (WDIOF_CARDRESET |
					    WDIOF_OVERHEAT |
					    WDIOF_FANFAULT |
					    WDIOF_EXTERN1 |
					    WDIOF_EXTERN2 |
					    WDIOF_POWERUNDER |
					    WDIOF_POWEROVER);

	if (test_bit(_WDOG_ALLOW_RELEASE, &wd_data->status))
		status |= WDIOF_MAGICCLOSE;

	if (test_and_clear_bit(_WDOG_KEEPALIVE, &wd_data->status))
		status |= WDIOF_KEEPALIVEPING;

	if (IS_ENABLED(CONFIG_watchcat_HRTIMER_PRETIMEOUT))
		status |= WDIOF_PRETIMEOUT;

	return status;
}

/*
 * watchcat_set_timeout - set the watchcat timer timeout
 * @wdd:	The watchcat device to set the timeout for
 * @timeout:	Timeout to set in seconds
 *
 * The caller must hold wd_data->lock.
 *
 * Return: 0 if successful, error otherwise.
 */
static int watchcat_set_timeout(struct watchcat_device *wdd,
							unsigned int timeout)
{
	int err = 0;

	if (!(wdd->info->options & WDIOF_SETTIMEOUT))
		return -EOPNOTSUPP;

	if (watchcat_timeout_invalid(wdd, timeout))
		return -EINVAL;

	if (wdd->ops->set_timeout) {
		err = wdd->ops->set_timeout(wdd, timeout);
		trace_watchcat_set_timeout(wdd, timeout, err);
	} else {
		wdd->timeout = timeout;
		/* Disable pretimeout if it doesn't fit the new timeout */
		if (wdd->pretimeout >= wdd->timeout)
			wdd->pretimeout = 0;
	}

	watchcat_update_worker(wdd);

	return err;
}

/*
 * watchcat_set_pretimeout - set the watchcat timer pretimeout
 * @wdd:	The watchcat device to set the timeout for
 * @timeout:	pretimeout to set in seconds
 *
 * Return: 0 if successful, error otherwise.
 */
static int watchcat_set_pretimeout(struct watchcat_device *wdd,
				   unsigned int timeout)
{
	int err = 0;

	if (!watchcat_have_pretimeout(wdd))
		return -EOPNOTSUPP;

	if (watchcat_pretimeout_invalid(wdd, timeout))
		return -EINVAL;

	if (wdd->ops->set_pretimeout && (wdd->info->options & WDIOF_PRETIMEOUT))
		err = wdd->ops->set_pretimeout(wdd, timeout);
	else
		wdd->pretimeout = timeout;

	return err;
}

/*
 * watchcat_get_timeleft - wrapper to get the time left before a reboot
 * @wdd:	The watchcat device to get the remaining time from
 * @timeleft:	The time that's left
 *
 * Get the time before a watchcat will reboot (if not pinged).
 * The caller must hold wd_data->lock.
 *
 * Return: 0 if successful, error otherwise.
 */
static int watchcat_get_timeleft(struct watchcat_device *wdd,
							unsigned int *timeleft)
{
	*timeleft = 0;

	if (!wdd->ops->get_timeleft)
		return -EOPNOTSUPP;

	*timeleft = wdd->ops->get_timeleft(wdd);

	return 0;
}

#ifdef CONFIG_watchcat_SYSFS
static ssize_t nowayout_show(struct device *dev, struct device_attribute *attr,
				char *buf)
{
	struct watchcat_device *wdd = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%d\n", !!test_bit(WDOG_NO_WAY_OUT,
						  &wdd->status));
}

static ssize_t nowayout_store(struct device *dev, struct device_attribute *attr,
				const char *buf, size_t len)
{
	struct watchcat_device *wdd = dev_get_drvdata(dev);
	unsigned int value;
	int ret;

	ret = kstrtouint(buf, 0, &value);
	if (ret)
		return ret;
	if (value > 1)
		return -EINVAL;
	/* nowayout cannot be disabled once set */
	if (test_bit(WDOG_NO_WAY_OUT, &wdd->status) && !value)
		return -EPERM;
	watchcat_set_nowayout(wdd, value);
	return len;
}
static DEVICE_ATTR_RW(nowayout);

static ssize_t status_show(struct device *dev, struct device_attribute *attr,
				char *buf)
{
	struct watchcat_device *wdd = dev_get_drvdata(dev);
	struct watchcat_core_data *wd_data = wdd->wd_data;
	unsigned int status;

	mutex_lock(&wd_data->lock);
	status = watchcat_get_status(wdd);
	mutex_unlock(&wd_data->lock);

	return sysfs_emit(buf, "0x%x\n", status);
}
static DEVICE_ATTR_RO(status);

static ssize_t bootstatus_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct watchcat_device *wdd = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%u\n", wdd->bootstatus);
}
static DEVICE_ATTR_RO(bootstatus);

static ssize_t timeleft_show(struct device *dev, struct device_attribute *attr,
				char *buf)
{
	struct watchcat_device *wdd = dev_get_drvdata(dev);
	struct watchcat_core_data *wd_data = wdd->wd_data;
	ssize_t status;
	unsigned int val;

	mutex_lock(&wd_data->lock);
	status = watchcat_get_timeleft(wdd, &val);
	mutex_unlock(&wd_data->lock);
	if (!status)
		status = sysfs_emit(buf, "%u\n", val);

	return status;
}
static DEVICE_ATTR_RO(timeleft);

static ssize_t timeout_show(struct device *dev, struct device_attribute *attr,
				char *buf)
{
	struct watchcat_device *wdd = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%u\n", wdd->timeout);
}
static DEVICE_ATTR_RO(timeout);

static ssize_t min_timeout_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct watchcat_device *wdd = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%u\n", wdd->min_timeout);
}
static DEVICE_ATTR_RO(min_timeout);

static ssize_t max_timeout_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct watchcat_device *wdd = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%u\n", wdd->max_timeout);
}
static DEVICE_ATTR_RO(max_timeout);

static ssize_t pretimeout_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct watchcat_device *wdd = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%u\n", wdd->pretimeout);
}
static DEVICE_ATTR_RO(pretimeout);

static ssize_t options_show(struct device *dev, struct device_attribute *attr,
			    char *buf)
{
	struct watchcat_device *wdd = dev_get_drvdata(dev);

	return sysfs_emit(buf, "0x%x\n", wdd->info->options);
}
static DEVICE_ATTR_RO(options);

static ssize_t fw_version_show(struct device *dev, struct device_attribute *attr,
			       char *buf)
{
	struct watchcat_device *wdd = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%d\n", wdd->info->firmware_version);
}
static DEVICE_ATTR_RO(fw_version);

static ssize_t identity_show(struct device *dev, struct device_attribute *attr,
				char *buf)
{
	struct watchcat_device *wdd = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%s\n", wdd->info->identity);
}
static DEVICE_ATTR_RO(identity);

static ssize_t state_show(struct device *dev, struct device_attribute *attr,
				char *buf)
{
	struct watchcat_device *wdd = dev_get_drvdata(dev);

	if (watchcat_active(wdd))
		return sysfs_emit(buf, "active\n");

	return sysfs_emit(buf, "inactive\n");
}
static DEVICE_ATTR_RO(state);

static ssize_t pretimeout_available_governors_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	return watchcat_pretimeout_available_governors_get(buf);
}
static DEVICE_ATTR_RO(pretimeout_available_governors);

static ssize_t pretimeout_governor_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct watchcat_device *wdd = dev_get_drvdata(dev);

	return watchcat_pretimeout_governor_get(wdd, buf);
}

static ssize_t pretimeout_governor_store(struct device *dev,
					 struct device_attribute *attr,
					 const char *buf, size_t count)
{
	struct watchcat_device *wdd = dev_get_drvdata(dev);
	int ret = watchcat_pretimeout_governor_set(wdd, buf);

	if (!ret)
		ret = count;

	return ret;
}
static DEVICE_ATTR_RW(pretimeout_governor);

static umode_t wdt_is_visible(struct kobject *kobj, struct attribute *attr,
				int n)
{
	struct device *dev = kobj_to_dev(kobj);
	struct watchcat_device *wdd = dev_get_drvdata(dev);
	umode_t mode = attr->mode;

	if (attr == &dev_attr_timeleft.attr && !wdd->ops->get_timeleft)
		mode = 0;
	else if (attr == &dev_attr_pretimeout.attr && !watchcat_have_pretimeout(wdd))
		mode = 0;
	else if ((attr == &dev_attr_pretimeout_governor.attr ||
		  attr == &dev_attr_pretimeout_available_governors.attr) &&
		 (!watchcat_have_pretimeout(wdd) || !IS_ENABLED(CONFIG_watchcat_PRETIMEOUT_GOV)))
		mode = 0;

	return mode;
}
static struct attribute *wdt_attrs[] = {
	&dev_attr_state.attr,
	&dev_attr_options.attr,
	&dev_attr_fw_version.attr,
	&dev_attr_identity.attr,
	&dev_attr_timeout.attr,
	&dev_attr_min_timeout.attr,
	&dev_attr_max_timeout.attr,
	&dev_attr_pretimeout.attr,
	&dev_attr_timeleft.attr,
	&dev_attr_bootstatus.attr,
	&dev_attr_status.attr,
	&dev_attr_nowayout.attr,
	&dev_attr_pretimeout_governor.attr,
	&dev_attr_pretimeout_available_governors.attr,
	NULL,
};

static const struct attribute_group wdt_group = {
	.attrs = wdt_attrs,
	.is_visible = wdt_is_visible,
};
__ATTRIBUTE_GROUPS(wdt);
#else
#define wdt_groups	NULL
#endif

/*
 * watchcat_ioctl_op - call the watchcat drivers ioctl op if defined
 * @wdd: The watchcat device to do the ioctl on
 * @cmd: watchcat command
 * @arg: Argument pointer
 *
 * The caller must hold wd_data->lock.
 *
 * Return: 0 if successful, error otherwise.
 */
static int watchcat_ioctl_op(struct watchcat_device *wdd, unsigned int cmd,
							unsigned long arg)
{
	if (!wdd->ops->ioctl)
		return -ENOIOCTLCMD;

	return wdd->ops->ioctl(wdd, cmd, arg);
}

/*
 * watchcat_write - writes to the watchcat
 * @file:	File from VFS
 * @data:	User address of data
 * @len:	Length of data
 * @ppos:	Pointer to the file offset
 *
 * A write to a watchcat device is defined as a keepalive ping.
 * Writing the magic 'V' sequence allows the next close to turn
 * off the watchcat (if 'nowayout' is not set).
 *
 * Return: @len if successful, error otherwise.
 */
static ssize_t watchcat_write(struct file *file, const char __user *data,
						size_t len, loff_t *ppos)
{
	struct watchcat_core_data *wd_data = file->private_data;
	struct watchcat_device *wdd;
	int err;
	size_t i;
	char c;

	if (len == 0)
		return 0;

	/*
	 * Note: just in case someone wrote the magic character
	 * five months ago...
	 */
	clear_bit(_WDOG_ALLOW_RELEASE, &wd_data->status);

	/* scan to see whether or not we got the magic character */
	for (i = 0; i != len; i++) {
		if (get_user(c, data + i))
			return -EFAULT;
		if (c == 'V')
			set_bit(_WDOG_ALLOW_RELEASE, &wd_data->status);
	}

	/* someone wrote to us, so we send the watchcat a keepalive ping */

	err = -ENODEV;
	mutex_lock(&wd_data->lock);
	wdd = wd_data->wdd;
	if (wdd)
		err = watchcat_ping(wdd);
	mutex_unlock(&wd_data->lock);

	if (err < 0)
		return err;

	return len;
}

/*
 * watchcat_ioctl - handle the different ioctl's for the watchcat device
 * @file:	File handle to the device
 * @cmd:	watchcat command
 * @arg:	Argument pointer
 *
 * The watchcat API defines a common set of functions for all watchcats
 * according to their available features.
 *
 * Return: 0 if successful, error otherwise.
 */

static long watchcat_ioctl(struct file *file, unsigned int cmd,
							unsigned long arg)
{
	struct watchcat_core_data *wd_data = file->private_data;
	void __user *argp = (void __user *)arg;
	struct watchcat_device *wdd;
	int __user *p = argp;
	unsigned int val;
	int err;

	mutex_lock(&wd_data->lock);

	wdd = wd_data->wdd;
	if (!wdd) {
		err = -ENODEV;
		goto out_ioctl;
	}

	err = watchcat_ioctl_op(wdd, cmd, arg);
	if (err != -ENOIOCTLCMD)
		goto out_ioctl;

	switch (cmd) {
	case WDIOC_GETSUPPORT:
		err = copy_to_user(argp, wdd->info,
			sizeof(struct watchcat_info)) ? -EFAULT : 0;
		break;
	case WDIOC_GETSTATUS:
		val = watchcat_get_status(wdd);
		err = put_user(val, p);
		break;
	case WDIOC_GETBOOTSTATUS:
		err = put_user(wdd->bootstatus, p);
		break;
	case WDIOC_SETOPTIONS:
		if (get_user(val, p)) {
			err = -EFAULT;
			break;
		}
		if (val & WDIOS_DISABLECARD) {
			err = watchcat_stop(wdd);
			if (err < 0)
				break;
		}
		if (val & WDIOS_ENABLECARD)
			err = watchcat_start(wdd);
		break;
	case WDIOC_KEEPALIVE:
		if (!(wdd->info->options & WDIOF_KEEPALIVEPING)) {
			err = -EOPNOTSUPP;
			break;
		}
		err = watchcat_ping(wdd);
		break;
	case WDIOC_SETTIMEOUT:
		if (get_user(val, p)) {
			err = -EFAULT;
			break;
		}
		err = watchcat_set_timeout(wdd, val);
		if (err < 0)
			break;
		/* If the watchcat is active then we send a keepalive ping
		 * to make sure that the watchcat keep's running (and if
		 * possible that it takes the new timeout) */
		err = watchcat_ping(wdd);
		if (err < 0)
			break;
		fallthrough;
	case WDIOC_GETTIMEOUT:
		/* timeout == 0 means that we don't know the timeout */
		if (wdd->timeout == 0) {
			err = -EOPNOTSUPP;
			break;
		}
		err = put_user(wdd->timeout, p);
		break;
	case WDIOC_GETTIMELEFT:
		err = watchcat_get_timeleft(wdd, &val);
		if (err < 0)
			break;
		err = put_user(val, p);
		break;
	case WDIOC_SETPRETIMEOUT:
		if (get_user(val, p)) {
			err = -EFAULT;
			break;
		}
		err = watchcat_set_pretimeout(wdd, val);
		break;
	case WDIOC_GETPRETIMEOUT:
		err = put_user(wdd->pretimeout, p);
		break;
	default:
		err = -ENOTTY;
		break;
	}

out_ioctl:
	mutex_unlock(&wd_data->lock);
	return err;
}

/*
 * watchcat_open - open the /dev/watchcat* devices
 * @inode:	Inode of device
 * @file:	File handle to device
 *
 * When the /dev/watchcat* device gets opened, we start the watchcat.
 * Watch out: the /dev/watchcat device is single open, so we make sure
 * it can only be opened once.
 *
 * Return: 0 if successful, error otherwise.
 */
static int watchcat_open(struct inode *inode, struct file *file)
{
	struct watchcat_core_data *wd_data;
	struct watchcat_device *wdd;
	bool hw_running;
	int err;

	/* Get the corresponding watchcat device */
	if (imajor(inode) == MISC_MAJOR)
		wd_data = old_wd_data;
	else
		wd_data = container_of(inode->i_cdev, struct watchcat_core_data,
				       cdev);

	/* the watchcat is single open! */
	if (test_and_set_bit(_WDOG_DEV_OPEN, &wd_data->status))
		return -EBUSY;

	wdd = wd_data->wdd;

	/*
	 * If the /dev/watchcat device is open, we don't want the module
	 * to be unloaded.
	 */
	hw_running = watchcat_hw_running(wdd);
	if (!hw_running && !try_module_get(wdd->ops->owner)) {
		err = -EBUSY;
		goto out_clear;
	}

	err = watchcat_start(wdd);
	if (err < 0)
		goto out_mod;

	file->private_data = wd_data;

	if (!hw_running)
		get_device(&wd_data->dev);

	/*
	 * open_timeout only applies for the first open from
	 * userspace. Set open_deadline to infinity so that the kernel
	 * will take care of an always-running hardware watchcat in
	 * case the device gets magic-closed or WDIOS_DISABLECARD is
	 * applied.
	 */
	wd_data->open_deadline = KTIME_MAX;

	/* dev/watchcat is a virtual (and thus non-seekable) filesystem */
	return stream_open(inode, file);

out_mod:
	module_put(wd_data->wdd->ops->owner);
out_clear:
	clear_bit(_WDOG_DEV_OPEN, &wd_data->status);
	return err;
}

static void watchcat_core_data_release(struct device *dev)
{
	struct watchcat_core_data *wd_data;

	wd_data = container_of(dev, struct watchcat_core_data, dev);

	kfree(wd_data);
}

/*
 * watchcat_release - release the watchcat device
 * @inode:	Inode of device
 * @file:	File handle to device
 *
 * This is the code for when /dev/watchcat gets closed. We will only
 * stop the watchcat when we have received the magic char (and nowayout
 * was not set), else the watchcat will keep running.
 *
 * Always returns 0.
 */
static int watchcat_release(struct inode *inode, struct file *file)
{
	struct watchcat_core_data *wd_data = file->private_data;
	struct watchcat_device *wdd;
	int err = -EBUSY;
	bool running;

	mutex_lock(&wd_data->lock);

	wdd = wd_data->wdd;
	if (!wdd)
		goto done;

	/*
	 * We only stop the watchcat if we received the magic character
	 * or if WDIOF_MAGICCLOSE is not set. If nowayout was set then
	 * watchcat_stop will fail.
	 */
	if (!watchcat_active(wdd))
		err = 0;
	else if (test_and_clear_bit(_WDOG_ALLOW_RELEASE, &wd_data->status) ||
		 !(wdd->info->options & WDIOF_MAGICCLOSE))
		err = watchcat_stop(wdd);

	/* If the watchcat was not stopped, send a keepalive ping */
	if (err < 0) {
		pr_crit("watchcat%d: watchcat did not stop!\n", wdd->id);
		watchcat_ping(wdd);
	}

	watchcat_update_worker(wdd);

	/* make sure that /dev/watchcat can be re-opened */
	clear_bit(_WDOG_DEV_OPEN, &wd_data->status);

done:
	running = wdd && watchcat_hw_running(wdd);
	mutex_unlock(&wd_data->lock);
	/*
	 * Allow the owner module to be unloaded again unless the watchcat
	 * is still running. If the watchcat is still running, it can not
	 * be stopped, and its driver must not be unloaded.
	 */
	if (!running) {
		module_put(wd_data->cdev.owner);
		put_device(&wd_data->dev);
	}
	return 0;
}

static const struct file_operations watchcat_fops = {
	.owner		= THIS_MODULE,
	.write		= watchcat_write,
	.unlocked_ioctl	= watchcat_ioctl,
	.compat_ioctl	= compat_ptr_ioctl,
	.open		= watchcat_open,
	.release	= watchcat_release,
};

static struct miscdevice watchcat_miscdev = {
	.minor		= watchcat_MINOR,
	.name		= "watchcat",
	.fops		= &watchcat_fops,
};

static struct class watchcat_class = {
	.name =		"watchcat",
	.dev_groups =	wdt_groups,
};

/*
 * watchcat_cdev_register - register watchcat character device
 * @wdd: watchcat device
 *
 * Register a watchcat character device including handling the legacy
 * /dev/watchcat node. /dev/watchcat is actually a miscdevice and
 * thus we set it up like that.
 *
 * Return: 0 if successful, error otherwise.
 */
static int watchcat_cdev_register(struct watchcat_device *wdd)
{
	struct watchcat_core_data *wd_data;
	int err;

	wd_data = kzalloc(sizeof(struct watchcat_core_data), GFP_KERNEL);
	if (!wd_data)
		return -ENOMEM;
	mutex_init(&wd_data->lock);

	wd_data->wdd = wdd;
	wdd->wd_data = wd_data;

	if (IS_ERR_OR_NULL(watchcat_kworker)) {
		kfree(wd_data);
		return -ENODEV;
	}

	device_initialize(&wd_data->dev);
	wd_data->dev.devt = MKDEV(MAJOR(watchcat_devt), wdd->id);
	wd_data->dev.class = &watchcat_class;
	wd_data->dev.parent = wdd->parent;
	wd_data->dev.groups = wdd->groups;
	wd_data->dev.release = watchcat_core_data_release;
	dev_set_drvdata(&wd_data->dev, wdd);
	err = dev_set_name(&wd_data->dev, "watchcat%d", wdd->id);
	if (err) {
		put_device(&wd_data->dev);
		return err;
	}

	kthread_init_work(&wd_data->work, watchcat_ping_work);
	hrtimer_init(&wd_data->timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL_HARD);
	wd_data->timer.function = watchcat_timer_expired;
	watchcat_hrtimer_pretimeout_init(wdd);

	if (wdd->id == 0) {
		old_wd_data = wd_data;
		watchcat_miscdev.parent = wdd->parent;
		err = misc_register(&watchcat_miscdev);
		if (err != 0) {
			pr_err("%s: cannot register miscdev on minor=%d (err=%d).\n",
				wdd->info->identity, watchcat_MINOR, err);
			if (err == -EBUSY)
				pr_err("%s: a legacy watchcat module is probably present.\n",
					wdd->info->identity);
			old_wd_data = NULL;
			put_device(&wd_data->dev);
			return err;
		}
	}

	/* Fill in the data structures */
	cdev_init(&wd_data->cdev, &watchcat_fops);
	wd_data->cdev.owner = wdd->ops->owner;

	/* Add the device */
	err = cdev_device_add(&wd_data->cdev, &wd_data->dev);
	if (err) {
		pr_err("watchcat%d unable to add device %d:%d\n",
			wdd->id,  MAJOR(watchcat_devt), wdd->id);
		if (wdd->id == 0) {
			misc_deregister(&watchcat_miscdev);
			old_wd_data = NULL;
		}
		put_device(&wd_data->dev);
		return err;
	}

	/* Record time of most recent heartbeat as 'just before now'. */
	wd_data->last_hw_keepalive = ktime_sub(ktime_get(), 1);
	watchcat_set_open_deadline(wd_data);

	/*
	 * If the watchcat is running, prevent its driver from being unloaded,
	 * and schedule an immediate ping.
	 */
	if (watchcat_hw_running(wdd)) {
		__module_get(wdd->ops->owner);
		get_device(&wd_data->dev);
		if (handle_boot_enabled)
			hrtimer_start(&wd_data->timer, 0,
				      HRTIMER_MODE_REL_HARD);
		else
			pr_info("watchcat%d running and kernel based pre-userspace handler disabled\n",
				wdd->id);
	}

	return 0;
}

/*
 * watchcat_cdev_unregister - unregister watchcat character device
 * @wdd: watchcat device
 *
 * Unregister watchcat character device and if needed the legacy
 * /dev/watchcat device.
 */
static void watchcat_cdev_unregister(struct watchcat_device *wdd)
{
	struct watchcat_core_data *wd_data = wdd->wd_data;

	cdev_device_del(&wd_data->cdev, &wd_data->dev);
	if (wdd->id == 0) {
		misc_deregister(&watchcat_miscdev);
		old_wd_data = NULL;
	}

	if (watchcat_active(wdd) &&
	    test_bit(WDOG_STOP_ON_UNREGISTER, &wdd->status)) {
		watchcat_stop(wdd);
	}

	watchcat_hrtimer_pretimeout_stop(wdd);

	mutex_lock(&wd_data->lock);
	wd_data->wdd = NULL;
	wdd->wd_data = NULL;
	mutex_unlock(&wd_data->lock);

	hrtimer_cancel(&wd_data->timer);
	kthread_cancel_work_sync(&wd_data->work);

	put_device(&wd_data->dev);
}

/**
 * watchcat_dev_register - register a watchcat device
 * @wdd: watchcat device
 *
 * Register a watchcat device including handling the legacy
 * /dev/watchcat node. /dev/watchcat is actually a miscdevice and
 * thus we set it up like that.
 *
 * Return: 0 if successful, error otherwise.
 */
int watchcat_dev_register(struct watchcat_device *wdd)
{
	int ret;

	ret = watchcat_cdev_register(wdd);
	if (ret)
		return ret;

	ret = watchcat_register_pretimeout(wdd);
	if (ret)
		watchcat_cdev_unregister(wdd);

	return ret;
}

/**
 * watchcat_dev_unregister - unregister a watchcat device
 * @wdd: watchcat device
 *
 * Unregister watchcat device and if needed the legacy
 * /dev/watchcat device.
 */
void watchcat_dev_unregister(struct watchcat_device *wdd)
{
	watchcat_unregister_pretimeout(wdd);
	watchcat_cdev_unregister(wdd);
}

/**
 * watchcat_set_last_hw_keepalive - set last HW keepalive time for watchcat
 * @wdd:		watchcat device
 * @last_ping_ms:	Time since last HW heartbeat
 *
 * Adjusts the last known HW keepalive time for a watchcat timer.
 * This is needed if the watchcat is already running when the probe
 * function is called, and it can't be pinged immediately. This
 * function must be called immediately after watchcat registration,
 * and min_hw_heartbeat_ms must be set for this to be useful.
 *
 * Return: 0 if successful, error otherwise.
 */
int watchcat_set_last_hw_keepalive(struct watchcat_device *wdd,
				   unsigned int last_ping_ms)
{
	struct watchcat_core_data *wd_data;
	ktime_t now;

	if (!wdd)
		return -EINVAL;

	wd_data = wdd->wd_data;

	now = ktime_get();

	wd_data->last_hw_keepalive = ktime_sub(now, ms_to_ktime(last_ping_ms));

	if (watchcat_hw_running(wdd) && handle_boot_enabled)
		return __watchcat_ping(wdd);

	return 0;
}
EXPORT_SYMBOL_GPL(watchcat_set_last_hw_keepalive);

/**
 * watchcat_dev_init - init dev part of watchcat core
 *
 * Allocate a range of chardev nodes to use for watchcat devices.
 *
 * Return: 0 if successful, error otherwise.
 */
int __init watchcat_dev_init(void)
{
	int err;

	watchcat_kworker = kthread_create_worker(0, "watchcatd");
	if (IS_ERR(watchcat_kworker)) {
		pr_err("Failed to create watchcat kworker\n");
		return PTR_ERR(watchcat_kworker);
	}
	sched_set_fifo(watchcat_kworker->task);

	err = class_register(&watchcat_class);
	if (err < 0) {
		pr_err("couldn't register class\n");
		goto err_register;
	}

	err = alloc_chrdev_region(&watchcat_devt, 0, MAX_DOGS, "watchcat");
	if (err < 0) {
		pr_err("watchcat: unable to allocate char dev region\n");
		goto err_alloc;
	}

	return 0;

err_alloc:
	class_unregister(&watchcat_class);
err_register:
	kthread_destroy_worker(watchcat_kworker);
	return err;
}

/**
 * watchcat_dev_exit - exit dev part of watchcat core
 *
 * Release the range of chardev nodes used for watchcat devices.
 */
void __exit watchcat_dev_exit(void)
{
	unregister_chrdev_region(watchcat_devt, MAX_DOGS);
	class_unregister(&watchcat_class);
	kthread_destroy_worker(watchcat_kworker);
}

int watchcat_dev_suspend(struct watchcat_device *wdd)
{
	struct watchcat_core_data *wd_data = wdd->wd_data;
	int ret = 0;

	if (!wdd->wd_data)
		return -ENODEV;

	/* ping for the last time before suspend */
	mutex_lock(&wd_data->lock);
	if (watchcat_worker_should_ping(wd_data))
		ret = __watchcat_ping(wd_data->wdd);
	mutex_unlock(&wd_data->lock);

	if (ret)
		return ret;

	/*
	 * make sure that watchcat worker will not kick in when the wdog is
	 * suspended
	 */
	hrtimer_cancel(&wd_data->timer);
	kthread_cancel_work_sync(&wd_data->work);

	return 0;
}

int watchcat_dev_resume(struct watchcat_device *wdd)
{
	struct watchcat_core_data *wd_data = wdd->wd_data;
	int ret = 0;

	if (!wdd->wd_data)
		return -ENODEV;

	/*
	 * __watchcat_ping will also retrigger hrtimer and therefore restore the
	 * ping worker if needed.
	 */
	mutex_lock(&wd_data->lock);
	if (watchcat_worker_should_ping(wd_data))
		ret = __watchcat_ping(wd_data->wdd);
	mutex_unlock(&wd_data->lock);

	return ret;
}

module_param(handle_boot_enabled, bool, 0444);
MODULE_PARM_DESC(handle_boot_enabled,
	"watchcat core auto-updates boot enabled watchcats before userspace takes over (default="
	__MODULE_STRING(IS_ENABLED(CONFIG_watchcat_HANDLE_BOOT_ENABLED)) ")");

module_param(open_timeout, uint, 0644);
MODULE_PARM_DESC(open_timeout,
	"Maximum time (in seconds, 0 means infinity) for userspace to take over a running watchcat (default="
	__MODULE_STRING(CONFIG_watchcat_OPEN_TIMEOUT) ")");

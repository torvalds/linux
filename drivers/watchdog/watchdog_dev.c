/*
 *	watchdog_dev.c
 *
 *	(c) Copyright 2008-2011 Alan Cox <alan@lxorguk.ukuu.org.uk>,
 *						All Rights Reserved.
 *
 *	(c) Copyright 2008-2011 Wim Van Sebroeck <wim@iguana.be>.
 *
 *
 *	This source code is part of the generic code that can be used
 *	by all the watchdog timer drivers.
 *
 *	This part of the generic code takes care of the following
 *	misc device: /dev/watchdog.
 *
 *	Based on source code of the following authors:
 *	  Matt Domsch <Matt_Domsch@dell.com>,
 *	  Rob Radez <rob@osinvestor.com>,
 *	  Rusty Lynch <rusty@linux.co.intel.com>
 *	  Satyam Sharma <satyam@infradead.org>
 *	  Randy Dunlap <randy.dunlap@oracle.com>
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
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
#include <linux/kthread.h>	/* For kthread_work */
#include <linux/miscdevice.h>	/* For handling misc devices */
#include <linux/module.h>	/* For module stuff/... */
#include <linux/mutex.h>	/* For mutexes */
#include <linux/slab.h>		/* For memory functions */
#include <linux/types.h>	/* For standard types (like size_t) */
#include <linux/watchdog.h>	/* For watchdog specific items */
#include <linux/uaccess.h>	/* For copy_to_user/put_user/... */

#include <uapi/linux/sched/types.h>	/* For struct sched_param */

#include "watchdog_core.h"
#include "watchdog_pretimeout.h"

/*
 * struct watchdog_core_data - watchdog core internal data
 * @dev:	The watchdog's internal device
 * @cdev:	The watchdog's Character device.
 * @wdd:	Pointer to watchdog device.
 * @lock:	Lock for watchdog core.
 * @status:	Watchdog core internal status bits.
 */
struct watchdog_core_data {
	struct device dev;
	struct cdev cdev;
	struct watchdog_device *wdd;
	struct mutex lock;
	ktime_t last_keepalive;
	ktime_t last_hw_keepalive;
	struct hrtimer timer;
	struct kthread_work work;
	unsigned long status;		/* Internal status bits */
#define _WDOG_DEV_OPEN		0	/* Opened ? */
#define _WDOG_ALLOW_RELEASE	1	/* Did we receive the magic char ? */
#define _WDOG_KEEPALIVE		2	/* Did we receive a keepalive ? */
};

/* the dev_t structure to store the dynamically allocated watchdog devices */
static dev_t watchdog_devt;
/* Reference to watchdog device behind /dev/watchdog */
static struct watchdog_core_data *old_wd_data;

static struct kthread_worker *watchdog_kworker;

static bool handle_boot_enabled =
	IS_ENABLED(CONFIG_WATCHDOG_HANDLE_BOOT_ENABLED);

static inline bool watchdog_need_worker(struct watchdog_device *wdd)
{
	/* All variables in milli-seconds */
	unsigned int hm = wdd->max_hw_heartbeat_ms;
	unsigned int t = wdd->timeout * 1000;

	/*
	 * A worker to generate heartbeat requests is needed if all of the
	 * following conditions are true.
	 * - Userspace activated the watchdog.
	 * - The driver provided a value for the maximum hardware timeout, and
	 *   thus is aware that the framework supports generating heartbeat
	 *   requests.
	 * - Userspace requests a longer timeout than the hardware can handle.
	 *
	 * Alternatively, if userspace has not opened the watchdog
	 * device, we take care of feeding the watchdog if it is
	 * running.
	 */
	return (hm && watchdog_active(wdd) && t > hm) ||
		(t && !watchdog_active(wdd) && watchdog_hw_running(wdd));
}

static ktime_t watchdog_next_keepalive(struct watchdog_device *wdd)
{
	struct watchdog_core_data *wd_data = wdd->wd_data;
	unsigned int timeout_ms = wdd->timeout * 1000;
	ktime_t keepalive_interval;
	ktime_t last_heartbeat, latest_heartbeat;
	ktime_t virt_timeout;
	unsigned int hw_heartbeat_ms;

	virt_timeout = ktime_add(wd_data->last_keepalive,
				 ms_to_ktime(timeout_ms));
	hw_heartbeat_ms = min_not_zero(timeout_ms, wdd->max_hw_heartbeat_ms);
	keepalive_interval = ms_to_ktime(hw_heartbeat_ms / 2);

	if (!watchdog_active(wdd))
		return keepalive_interval;

	/*
	 * To ensure that the watchdog times out wdd->timeout seconds
	 * after the most recent ping from userspace, the last
	 * worker ping has to come in hw_heartbeat_ms before this timeout.
	 */
	last_heartbeat = ktime_sub(virt_timeout, ms_to_ktime(hw_heartbeat_ms));
	latest_heartbeat = ktime_sub(last_heartbeat, ktime_get());
	if (ktime_before(latest_heartbeat, keepalive_interval))
		return latest_heartbeat;
	return keepalive_interval;
}

static inline void watchdog_update_worker(struct watchdog_device *wdd)
{
	struct watchdog_core_data *wd_data = wdd->wd_data;

	if (watchdog_need_worker(wdd)) {
		ktime_t t = watchdog_next_keepalive(wdd);

		if (t > 0)
			hrtimer_start(&wd_data->timer, t, HRTIMER_MODE_REL);
	} else {
		hrtimer_cancel(&wd_data->timer);
	}
}

static int __watchdog_ping(struct watchdog_device *wdd)
{
	struct watchdog_core_data *wd_data = wdd->wd_data;
	ktime_t earliest_keepalive, now;
	int err;

	earliest_keepalive = ktime_add(wd_data->last_hw_keepalive,
				       ms_to_ktime(wdd->min_hw_heartbeat_ms));
	now = ktime_get();

	if (ktime_after(earliest_keepalive, now)) {
		hrtimer_start(&wd_data->timer,
			      ktime_sub(earliest_keepalive, now),
			      HRTIMER_MODE_REL);
		return 0;
	}

	wd_data->last_hw_keepalive = now;

	if (wdd->ops->ping)
		err = wdd->ops->ping(wdd);  /* ping the watchdog */
	else
		err = wdd->ops->start(wdd); /* restart watchdog */

	watchdog_update_worker(wdd);

	return err;
}

/*
 *	watchdog_ping: ping the watchdog.
 *	@wdd: the watchdog device to ping
 *
 *	The caller must hold wd_data->lock.
 *
 *	If the watchdog has no own ping operation then it needs to be
 *	restarted via the start operation. This wrapper function does
 *	exactly that.
 *	We only ping when the watchdog device is running.
 */

static int watchdog_ping(struct watchdog_device *wdd)
{
	struct watchdog_core_data *wd_data = wdd->wd_data;

	if (!watchdog_active(wdd) && !watchdog_hw_running(wdd))
		return 0;

	set_bit(_WDOG_KEEPALIVE, &wd_data->status);

	wd_data->last_keepalive = ktime_get();
	return __watchdog_ping(wdd);
}

static bool watchdog_worker_should_ping(struct watchdog_core_data *wd_data)
{
	struct watchdog_device *wdd = wd_data->wdd;

	return wdd && (watchdog_active(wdd) || watchdog_hw_running(wdd));
}

static void watchdog_ping_work(struct kthread_work *work)
{
	struct watchdog_core_data *wd_data;

	wd_data = container_of(work, struct watchdog_core_data, work);

	mutex_lock(&wd_data->lock);
	if (watchdog_worker_should_ping(wd_data))
		__watchdog_ping(wd_data->wdd);
	mutex_unlock(&wd_data->lock);
}

static enum hrtimer_restart watchdog_timer_expired(struct hrtimer *timer)
{
	struct watchdog_core_data *wd_data;

	wd_data = container_of(timer, struct watchdog_core_data, timer);

	kthread_queue_work(watchdog_kworker, &wd_data->work);
	return HRTIMER_NORESTART;
}

/*
 *	watchdog_start: wrapper to start the watchdog.
 *	@wdd: the watchdog device to start
 *
 *	The caller must hold wd_data->lock.
 *
 *	Start the watchdog if it is not active and mark it active.
 *	This function returns zero on success or a negative errno code for
 *	failure.
 */

static int watchdog_start(struct watchdog_device *wdd)
{
	struct watchdog_core_data *wd_data = wdd->wd_data;
	ktime_t started_at;
	int err;

	if (watchdog_active(wdd))
		return 0;

	set_bit(_WDOG_KEEPALIVE, &wd_data->status);

	started_at = ktime_get();
	if (watchdog_hw_running(wdd) && wdd->ops->ping)
		err = wdd->ops->ping(wdd);
	else
		err = wdd->ops->start(wdd);
	if (err == 0) {
		set_bit(WDOG_ACTIVE, &wdd->status);
		wd_data->last_keepalive = started_at;
		wd_data->last_hw_keepalive = started_at;
		watchdog_update_worker(wdd);
	}

	return err;
}

/*
 *	watchdog_stop: wrapper to stop the watchdog.
 *	@wdd: the watchdog device to stop
 *
 *	The caller must hold wd_data->lock.
 *
 *	Stop the watchdog if it is still active and unmark it active.
 *	This function returns zero on success or a negative errno code for
 *	failure.
 *	If the 'nowayout' feature was set, the watchdog cannot be stopped.
 */

static int watchdog_stop(struct watchdog_device *wdd)
{
	int err = 0;

	if (!watchdog_active(wdd))
		return 0;

	if (test_bit(WDOG_NO_WAY_OUT, &wdd->status)) {
		pr_info("watchdog%d: nowayout prevents watchdog being stopped!\n",
			wdd->id);
		return -EBUSY;
	}

	if (wdd->ops->stop) {
		clear_bit(WDOG_HW_RUNNING, &wdd->status);
		err = wdd->ops->stop(wdd);
	} else {
		set_bit(WDOG_HW_RUNNING, &wdd->status);
	}

	if (err == 0) {
		clear_bit(WDOG_ACTIVE, &wdd->status);
		watchdog_update_worker(wdd);
	}

	return err;
}

/*
 *	watchdog_get_status: wrapper to get the watchdog status
 *	@wdd: the watchdog device to get the status from
 *
 *	The caller must hold wd_data->lock.
 *
 *	Get the watchdog's status flags.
 */

static unsigned int watchdog_get_status(struct watchdog_device *wdd)
{
	struct watchdog_core_data *wd_data = wdd->wd_data;
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

	return status;
}

/*
 *	watchdog_set_timeout: set the watchdog timer timeout
 *	@wdd: the watchdog device to set the timeout for
 *	@timeout: timeout to set in seconds
 *
 *	The caller must hold wd_data->lock.
 */

static int watchdog_set_timeout(struct watchdog_device *wdd,
							unsigned int timeout)
{
	int err = 0;

	if (!(wdd->info->options & WDIOF_SETTIMEOUT))
		return -EOPNOTSUPP;

	if (watchdog_timeout_invalid(wdd, timeout))
		return -EINVAL;

	if (wdd->ops->set_timeout) {
		err = wdd->ops->set_timeout(wdd, timeout);
	} else {
		wdd->timeout = timeout;
		/* Disable pretimeout if it doesn't fit the new timeout */
		if (wdd->pretimeout >= wdd->timeout)
			wdd->pretimeout = 0;
	}

	watchdog_update_worker(wdd);

	return err;
}

/*
 *	watchdog_set_pretimeout: set the watchdog timer pretimeout
 *	@wdd: the watchdog device to set the timeout for
 *	@timeout: pretimeout to set in seconds
 */

static int watchdog_set_pretimeout(struct watchdog_device *wdd,
				   unsigned int timeout)
{
	int err = 0;

	if (!(wdd->info->options & WDIOF_PRETIMEOUT))
		return -EOPNOTSUPP;

	if (watchdog_pretimeout_invalid(wdd, timeout))
		return -EINVAL;

	if (wdd->ops->set_pretimeout)
		err = wdd->ops->set_pretimeout(wdd, timeout);
	else
		wdd->pretimeout = timeout;

	return err;
}

/*
 *	watchdog_get_timeleft: wrapper to get the time left before a reboot
 *	@wdd: the watchdog device to get the remaining time from
 *	@timeleft: the time that's left
 *
 *	The caller must hold wd_data->lock.
 *
 *	Get the time before a watchdog will reboot (if not pinged).
 */

static int watchdog_get_timeleft(struct watchdog_device *wdd,
							unsigned int *timeleft)
{
	*timeleft = 0;

	if (!wdd->ops->get_timeleft)
		return -EOPNOTSUPP;

	*timeleft = wdd->ops->get_timeleft(wdd);

	return 0;
}

#ifdef CONFIG_WATCHDOG_SYSFS
static ssize_t nowayout_show(struct device *dev, struct device_attribute *attr,
				char *buf)
{
	struct watchdog_device *wdd = dev_get_drvdata(dev);

	return sprintf(buf, "%d\n", !!test_bit(WDOG_NO_WAY_OUT, &wdd->status));
}
static DEVICE_ATTR_RO(nowayout);

static ssize_t status_show(struct device *dev, struct device_attribute *attr,
				char *buf)
{
	struct watchdog_device *wdd = dev_get_drvdata(dev);
	struct watchdog_core_data *wd_data = wdd->wd_data;
	unsigned int status;

	mutex_lock(&wd_data->lock);
	status = watchdog_get_status(wdd);
	mutex_unlock(&wd_data->lock);

	return sprintf(buf, "0x%x\n", status);
}
static DEVICE_ATTR_RO(status);

static ssize_t bootstatus_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct watchdog_device *wdd = dev_get_drvdata(dev);

	return sprintf(buf, "%u\n", wdd->bootstatus);
}
static DEVICE_ATTR_RO(bootstatus);

static ssize_t timeleft_show(struct device *dev, struct device_attribute *attr,
				char *buf)
{
	struct watchdog_device *wdd = dev_get_drvdata(dev);
	struct watchdog_core_data *wd_data = wdd->wd_data;
	ssize_t status;
	unsigned int val;

	mutex_lock(&wd_data->lock);
	status = watchdog_get_timeleft(wdd, &val);
	mutex_unlock(&wd_data->lock);
	if (!status)
		status = sprintf(buf, "%u\n", val);

	return status;
}
static DEVICE_ATTR_RO(timeleft);

static ssize_t timeout_show(struct device *dev, struct device_attribute *attr,
				char *buf)
{
	struct watchdog_device *wdd = dev_get_drvdata(dev);

	return sprintf(buf, "%u\n", wdd->timeout);
}
static DEVICE_ATTR_RO(timeout);

static ssize_t pretimeout_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct watchdog_device *wdd = dev_get_drvdata(dev);

	return sprintf(buf, "%u\n", wdd->pretimeout);
}
static DEVICE_ATTR_RO(pretimeout);

static ssize_t identity_show(struct device *dev, struct device_attribute *attr,
				char *buf)
{
	struct watchdog_device *wdd = dev_get_drvdata(dev);

	return sprintf(buf, "%s\n", wdd->info->identity);
}
static DEVICE_ATTR_RO(identity);

static ssize_t state_show(struct device *dev, struct device_attribute *attr,
				char *buf)
{
	struct watchdog_device *wdd = dev_get_drvdata(dev);

	if (watchdog_active(wdd))
		return sprintf(buf, "active\n");

	return sprintf(buf, "inactive\n");
}
static DEVICE_ATTR_RO(state);

static ssize_t pretimeout_available_governors_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	return watchdog_pretimeout_available_governors_get(buf);
}
static DEVICE_ATTR_RO(pretimeout_available_governors);

static ssize_t pretimeout_governor_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct watchdog_device *wdd = dev_get_drvdata(dev);

	return watchdog_pretimeout_governor_get(wdd, buf);
}

static ssize_t pretimeout_governor_store(struct device *dev,
					 struct device_attribute *attr,
					 const char *buf, size_t count)
{
	struct watchdog_device *wdd = dev_get_drvdata(dev);
	int ret = watchdog_pretimeout_governor_set(wdd, buf);

	if (!ret)
		ret = count;

	return ret;
}
static DEVICE_ATTR_RW(pretimeout_governor);

static umode_t wdt_is_visible(struct kobject *kobj, struct attribute *attr,
				int n)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	struct watchdog_device *wdd = dev_get_drvdata(dev);
	umode_t mode = attr->mode;

	if (attr == &dev_attr_timeleft.attr && !wdd->ops->get_timeleft)
		mode = 0;
	else if (attr == &dev_attr_pretimeout.attr &&
		 !(wdd->info->options & WDIOF_PRETIMEOUT))
		mode = 0;
	else if ((attr == &dev_attr_pretimeout_governor.attr ||
		  attr == &dev_attr_pretimeout_available_governors.attr) &&
		 (!(wdd->info->options & WDIOF_PRETIMEOUT) ||
		  !IS_ENABLED(CONFIG_WATCHDOG_PRETIMEOUT_GOV)))
		mode = 0;

	return mode;
}
static struct attribute *wdt_attrs[] = {
	&dev_attr_state.attr,
	&dev_attr_identity.attr,
	&dev_attr_timeout.attr,
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
 *	watchdog_ioctl_op: call the watchdog drivers ioctl op if defined
 *	@wdd: the watchdog device to do the ioctl on
 *	@cmd: watchdog command
 *	@arg: argument pointer
 *
 *	The caller must hold wd_data->lock.
 */

static int watchdog_ioctl_op(struct watchdog_device *wdd, unsigned int cmd,
							unsigned long arg)
{
	if (!wdd->ops->ioctl)
		return -ENOIOCTLCMD;

	return wdd->ops->ioctl(wdd, cmd, arg);
}

/*
 *	watchdog_write: writes to the watchdog.
 *	@file: file from VFS
 *	@data: user address of data
 *	@len: length of data
 *	@ppos: pointer to the file offset
 *
 *	A write to a watchdog device is defined as a keepalive ping.
 *	Writing the magic 'V' sequence allows the next close to turn
 *	off the watchdog (if 'nowayout' is not set).
 */

static ssize_t watchdog_write(struct file *file, const char __user *data,
						size_t len, loff_t *ppos)
{
	struct watchdog_core_data *wd_data = file->private_data;
	struct watchdog_device *wdd;
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

	/* someone wrote to us, so we send the watchdog a keepalive ping */

	err = -ENODEV;
	mutex_lock(&wd_data->lock);
	wdd = wd_data->wdd;
	if (wdd)
		err = watchdog_ping(wdd);
	mutex_unlock(&wd_data->lock);

	if (err < 0)
		return err;

	return len;
}

/*
 *	watchdog_ioctl: handle the different ioctl's for the watchdog device.
 *	@file: file handle to the device
 *	@cmd: watchdog command
 *	@arg: argument pointer
 *
 *	The watchdog API defines a common set of functions for all watchdogs
 *	according to their available features.
 */

static long watchdog_ioctl(struct file *file, unsigned int cmd,
							unsigned long arg)
{
	struct watchdog_core_data *wd_data = file->private_data;
	void __user *argp = (void __user *)arg;
	struct watchdog_device *wdd;
	int __user *p = argp;
	unsigned int val;
	int err;

	mutex_lock(&wd_data->lock);

	wdd = wd_data->wdd;
	if (!wdd) {
		err = -ENODEV;
		goto out_ioctl;
	}

	err = watchdog_ioctl_op(wdd, cmd, arg);
	if (err != -ENOIOCTLCMD)
		goto out_ioctl;

	switch (cmd) {
	case WDIOC_GETSUPPORT:
		err = copy_to_user(argp, wdd->info,
			sizeof(struct watchdog_info)) ? -EFAULT : 0;
		break;
	case WDIOC_GETSTATUS:
		val = watchdog_get_status(wdd);
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
			err = watchdog_stop(wdd);
			if (err < 0)
				break;
		}
		if (val & WDIOS_ENABLECARD)
			err = watchdog_start(wdd);
		break;
	case WDIOC_KEEPALIVE:
		if (!(wdd->info->options & WDIOF_KEEPALIVEPING)) {
			err = -EOPNOTSUPP;
			break;
		}
		err = watchdog_ping(wdd);
		break;
	case WDIOC_SETTIMEOUT:
		if (get_user(val, p)) {
			err = -EFAULT;
			break;
		}
		err = watchdog_set_timeout(wdd, val);
		if (err < 0)
			break;
		/* If the watchdog is active then we send a keepalive ping
		 * to make sure that the watchdog keep's running (and if
		 * possible that it takes the new timeout) */
		err = watchdog_ping(wdd);
		if (err < 0)
			break;
		/* fall through */
	case WDIOC_GETTIMEOUT:
		/* timeout == 0 means that we don't know the timeout */
		if (wdd->timeout == 0) {
			err = -EOPNOTSUPP;
			break;
		}
		err = put_user(wdd->timeout, p);
		break;
	case WDIOC_GETTIMELEFT:
		err = watchdog_get_timeleft(wdd, &val);
		if (err < 0)
			break;
		err = put_user(val, p);
		break;
	case WDIOC_SETPRETIMEOUT:
		if (get_user(val, p)) {
			err = -EFAULT;
			break;
		}
		err = watchdog_set_pretimeout(wdd, val);
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
 *	watchdog_open: open the /dev/watchdog* devices.
 *	@inode: inode of device
 *	@file: file handle to device
 *
 *	When the /dev/watchdog* device gets opened, we start the watchdog.
 *	Watch out: the /dev/watchdog device is single open, so we make sure
 *	it can only be opened once.
 */

static int watchdog_open(struct inode *inode, struct file *file)
{
	struct watchdog_core_data *wd_data;
	struct watchdog_device *wdd;
	bool hw_running;
	int err;

	/* Get the corresponding watchdog device */
	if (imajor(inode) == MISC_MAJOR)
		wd_data = old_wd_data;
	else
		wd_data = container_of(inode->i_cdev, struct watchdog_core_data,
				       cdev);

	/* the watchdog is single open! */
	if (test_and_set_bit(_WDOG_DEV_OPEN, &wd_data->status))
		return -EBUSY;

	wdd = wd_data->wdd;

	/*
	 * If the /dev/watchdog device is open, we don't want the module
	 * to be unloaded.
	 */
	hw_running = watchdog_hw_running(wdd);
	if (!hw_running && !try_module_get(wdd->ops->owner)) {
		err = -EBUSY;
		goto out_clear;
	}

	err = watchdog_start(wdd);
	if (err < 0)
		goto out_mod;

	file->private_data = wd_data;

	if (!hw_running)
		get_device(&wd_data->dev);

	/* dev/watchdog is a virtual (and thus non-seekable) filesystem */
	return nonseekable_open(inode, file);

out_mod:
	module_put(wd_data->wdd->ops->owner);
out_clear:
	clear_bit(_WDOG_DEV_OPEN, &wd_data->status);
	return err;
}

static void watchdog_core_data_release(struct device *dev)
{
	struct watchdog_core_data *wd_data;

	wd_data = container_of(dev, struct watchdog_core_data, dev);

	kfree(wd_data);
}

/*
 *	watchdog_release: release the watchdog device.
 *	@inode: inode of device
 *	@file: file handle to device
 *
 *	This is the code for when /dev/watchdog gets closed. We will only
 *	stop the watchdog when we have received the magic char (and nowayout
 *	was not set), else the watchdog will keep running.
 */

static int watchdog_release(struct inode *inode, struct file *file)
{
	struct watchdog_core_data *wd_data = file->private_data;
	struct watchdog_device *wdd;
	int err = -EBUSY;
	bool running;

	mutex_lock(&wd_data->lock);

	wdd = wd_data->wdd;
	if (!wdd)
		goto done;

	/*
	 * We only stop the watchdog if we received the magic character
	 * or if WDIOF_MAGICCLOSE is not set. If nowayout was set then
	 * watchdog_stop will fail.
	 */
	if (!test_bit(WDOG_ACTIVE, &wdd->status))
		err = 0;
	else if (test_and_clear_bit(_WDOG_ALLOW_RELEASE, &wd_data->status) ||
		 !(wdd->info->options & WDIOF_MAGICCLOSE))
		err = watchdog_stop(wdd);

	/* If the watchdog was not stopped, send a keepalive ping */
	if (err < 0) {
		pr_crit("watchdog%d: watchdog did not stop!\n", wdd->id);
		watchdog_ping(wdd);
	}

	watchdog_update_worker(wdd);

	/* make sure that /dev/watchdog can be re-opened */
	clear_bit(_WDOG_DEV_OPEN, &wd_data->status);

done:
	running = wdd && watchdog_hw_running(wdd);
	mutex_unlock(&wd_data->lock);
	/*
	 * Allow the owner module to be unloaded again unless the watchdog
	 * is still running. If the watchdog is still running, it can not
	 * be stopped, and its driver must not be unloaded.
	 */
	if (!running) {
		module_put(wd_data->cdev.owner);
		put_device(&wd_data->dev);
	}
	return 0;
}

static const struct file_operations watchdog_fops = {
	.owner		= THIS_MODULE,
	.write		= watchdog_write,
	.unlocked_ioctl	= watchdog_ioctl,
	.open		= watchdog_open,
	.release	= watchdog_release,
};

static struct miscdevice watchdog_miscdev = {
	.minor		= WATCHDOG_MINOR,
	.name		= "watchdog",
	.fops		= &watchdog_fops,
};

static struct class watchdog_class = {
	.name =		"watchdog",
	.owner =	THIS_MODULE,
	.dev_groups =	wdt_groups,
};

/*
 *	watchdog_cdev_register: register watchdog character device
 *	@wdd: watchdog device
 *
 *	Register a watchdog character device including handling the legacy
 *	/dev/watchdog node. /dev/watchdog is actually a miscdevice and
 *	thus we set it up like that.
 */

static int watchdog_cdev_register(struct watchdog_device *wdd)
{
	struct watchdog_core_data *wd_data;
	int err;

	wd_data = kzalloc(sizeof(struct watchdog_core_data), GFP_KERNEL);
	if (!wd_data)
		return -ENOMEM;
	mutex_init(&wd_data->lock);

	wd_data->wdd = wdd;
	wdd->wd_data = wd_data;

	if (IS_ERR_OR_NULL(watchdog_kworker))
		return -ENODEV;

	kthread_init_work(&wd_data->work, watchdog_ping_work);
	hrtimer_init(&wd_data->timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	wd_data->timer.function = watchdog_timer_expired;

	if (wdd->id == 0) {
		old_wd_data = wd_data;
		watchdog_miscdev.parent = wdd->parent;
		err = misc_register(&watchdog_miscdev);
		if (err != 0) {
			pr_err("%s: cannot register miscdev on minor=%d (err=%d).\n",
				wdd->info->identity, WATCHDOG_MINOR, err);
			if (err == -EBUSY)
				pr_err("%s: a legacy watchdog module is probably present.\n",
					wdd->info->identity);
			old_wd_data = NULL;
			kfree(wd_data);
			return err;
		}
	}

	device_initialize(&wd_data->dev);
	wd_data->dev.devt = MKDEV(MAJOR(watchdog_devt), wdd->id);
	wd_data->dev.class = &watchdog_class;
	wd_data->dev.parent = wdd->parent;
	wd_data->dev.groups = wdd->groups;
	wd_data->dev.release = watchdog_core_data_release;
	dev_set_drvdata(&wd_data->dev, wdd);
	dev_set_name(&wd_data->dev, "watchdog%d", wdd->id);

	/* Fill in the data structures */
	cdev_init(&wd_data->cdev, &watchdog_fops);

	/* Add the device */
	err = cdev_device_add(&wd_data->cdev, &wd_data->dev);
	if (err) {
		pr_err("watchdog%d unable to add device %d:%d\n",
			wdd->id,  MAJOR(watchdog_devt), wdd->id);
		if (wdd->id == 0) {
			misc_deregister(&watchdog_miscdev);
			old_wd_data = NULL;
			put_device(&wd_data->dev);
		}
		return err;
	}

	wd_data->cdev.owner = wdd->ops->owner;

	/* Record time of most recent heartbeat as 'just before now'. */
	wd_data->last_hw_keepalive = ktime_sub(ktime_get(), 1);

	/*
	 * If the watchdog is running, prevent its driver from being unloaded,
	 * and schedule an immediate ping.
	 */
	if (watchdog_hw_running(wdd)) {
		__module_get(wdd->ops->owner);
		get_device(&wd_data->dev);
		if (handle_boot_enabled)
			hrtimer_start(&wd_data->timer, 0, HRTIMER_MODE_REL);
		else
			pr_info("watchdog%d running and kernel based pre-userspace handler disabled\n",
				wdd->id);
	}

	return 0;
}

/*
 *	watchdog_cdev_unregister: unregister watchdog character device
 *	@watchdog: watchdog device
 *
 *	Unregister watchdog character device and if needed the legacy
 *	/dev/watchdog device.
 */

static void watchdog_cdev_unregister(struct watchdog_device *wdd)
{
	struct watchdog_core_data *wd_data = wdd->wd_data;

	cdev_device_del(&wd_data->cdev, &wd_data->dev);
	if (wdd->id == 0) {
		misc_deregister(&watchdog_miscdev);
		old_wd_data = NULL;
	}

	if (watchdog_active(wdd) &&
	    test_bit(WDOG_STOP_ON_UNREGISTER, &wdd->status)) {
		watchdog_stop(wdd);
	}

	mutex_lock(&wd_data->lock);
	wd_data->wdd = NULL;
	wdd->wd_data = NULL;
	mutex_unlock(&wd_data->lock);

	hrtimer_cancel(&wd_data->timer);
	kthread_cancel_work_sync(&wd_data->work);

	put_device(&wd_data->dev);
}

/*
 *	watchdog_dev_register: register a watchdog device
 *	@wdd: watchdog device
 *
 *	Register a watchdog device including handling the legacy
 *	/dev/watchdog node. /dev/watchdog is actually a miscdevice and
 *	thus we set it up like that.
 */

int watchdog_dev_register(struct watchdog_device *wdd)
{
	int ret;

	ret = watchdog_cdev_register(wdd);
	if (ret)
		return ret;

	ret = watchdog_register_pretimeout(wdd);
	if (ret)
		watchdog_cdev_unregister(wdd);

	return ret;
}

/*
 *	watchdog_dev_unregister: unregister a watchdog device
 *	@watchdog: watchdog device
 *
 *	Unregister watchdog device and if needed the legacy
 *	/dev/watchdog device.
 */

void watchdog_dev_unregister(struct watchdog_device *wdd)
{
	watchdog_unregister_pretimeout(wdd);
	watchdog_cdev_unregister(wdd);
}

/*
 *	watchdog_dev_init: init dev part of watchdog core
 *
 *	Allocate a range of chardev nodes to use for watchdog devices
 */

int __init watchdog_dev_init(void)
{
	int err;
	struct sched_param param = {.sched_priority = MAX_RT_PRIO - 1,};

	watchdog_kworker = kthread_create_worker(0, "watchdogd");
	if (IS_ERR(watchdog_kworker)) {
		pr_err("Failed to create watchdog kworker\n");
		return PTR_ERR(watchdog_kworker);
	}
	sched_setscheduler(watchdog_kworker->task, SCHED_FIFO, &param);

	err = class_register(&watchdog_class);
	if (err < 0) {
		pr_err("couldn't register class\n");
		goto err_register;
	}

	err = alloc_chrdev_region(&watchdog_devt, 0, MAX_DOGS, "watchdog");
	if (err < 0) {
		pr_err("watchdog: unable to allocate char dev region\n");
		goto err_alloc;
	}

	return 0;

err_alloc:
	class_unregister(&watchdog_class);
err_register:
	kthread_destroy_worker(watchdog_kworker);
	return err;
}

/*
 *	watchdog_dev_exit: exit dev part of watchdog core
 *
 *	Release the range of chardev nodes used for watchdog devices
 */

void __exit watchdog_dev_exit(void)
{
	unregister_chrdev_region(watchdog_devt, MAX_DOGS);
	class_unregister(&watchdog_class);
	kthread_destroy_worker(watchdog_kworker);
}

module_param(handle_boot_enabled, bool, 0444);
MODULE_PARM_DESC(handle_boot_enabled,
	"Watchdog core auto-updates boot enabled watchdogs before userspace takes over (default="
	__MODULE_STRING(IS_ENABLED(CONFIG_WATCHDOG_HANDLE_BOOT_ENABLED)) ")");

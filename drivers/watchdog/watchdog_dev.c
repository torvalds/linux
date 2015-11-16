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

#include <linux/module.h>	/* For module stuff/... */
#include <linux/types.h>	/* For standard types (like size_t) */
#include <linux/errno.h>	/* For the -ENODEV/... values */
#include <linux/kernel.h>	/* For printk/panic/... */
#include <linux/fs.h>		/* For file operations */
#include <linux/watchdog.h>	/* For watchdog specific items */
#include <linux/miscdevice.h>	/* For handling misc devices */
#include <linux/init.h>		/* For __init/__exit/... */
#include <linux/uaccess.h>	/* For copy_to_user/put_user/... */

#include "watchdog_core.h"

/* the dev_t structure to store the dynamically allocated watchdog devices */
static dev_t watchdog_devt;
/* the watchdog device behind /dev/watchdog */
static struct watchdog_device *old_wdd;

/*
 *	watchdog_ping: ping the watchdog.
 *	@wdd: the watchdog device to ping
 *
 *	If the watchdog has no own ping operation then it needs to be
 *	restarted via the start operation. This wrapper function does
 *	exactly that.
 *	We only ping when the watchdog device is running.
 */

static int watchdog_ping(struct watchdog_device *wdd)
{
	int err = 0;

	mutex_lock(&wdd->lock);

	if (test_bit(WDOG_UNREGISTERED, &wdd->status)) {
		err = -ENODEV;
		goto out_ping;
	}

	if (!watchdog_active(wdd))
		goto out_ping;

	if (wdd->ops->ping)
		err = wdd->ops->ping(wdd);	/* ping the watchdog */
	else
		err = wdd->ops->start(wdd);	/* restart watchdog */

out_ping:
	mutex_unlock(&wdd->lock);
	return err;
}

/*
 *	watchdog_start: wrapper to start the watchdog.
 *	@wdd: the watchdog device to start
 *
 *	Start the watchdog if it is not active and mark it active.
 *	This function returns zero on success or a negative errno code for
 *	failure.
 */

static int watchdog_start(struct watchdog_device *wdd)
{
	int err = 0;

	mutex_lock(&wdd->lock);

	if (test_bit(WDOG_UNREGISTERED, &wdd->status)) {
		err = -ENODEV;
		goto out_start;
	}

	if (watchdog_active(wdd))
		goto out_start;

	err = wdd->ops->start(wdd);
	if (err == 0)
		set_bit(WDOG_ACTIVE, &wdd->status);

out_start:
	mutex_unlock(&wdd->lock);
	return err;
}

/*
 *	watchdog_stop: wrapper to stop the watchdog.
 *	@wdd: the watchdog device to stop
 *
 *	Stop the watchdog if it is still active and unmark it active.
 *	This function returns zero on success or a negative errno code for
 *	failure.
 *	If the 'nowayout' feature was set, the watchdog cannot be stopped.
 */

static int watchdog_stop(struct watchdog_device *wdd)
{
	int err = 0;

	mutex_lock(&wdd->lock);

	if (test_bit(WDOG_UNREGISTERED, &wdd->status)) {
		err = -ENODEV;
		goto out_stop;
	}

	if (!watchdog_active(wdd))
		goto out_stop;

	if (test_bit(WDOG_NO_WAY_OUT, &wdd->status)) {
		dev_info(wdd->dev, "nowayout prevents watchdog being stopped!\n");
		err = -EBUSY;
		goto out_stop;
	}

	err = wdd->ops->stop(wdd);
	if (err == 0)
		clear_bit(WDOG_ACTIVE, &wdd->status);

out_stop:
	mutex_unlock(&wdd->lock);
	return err;
}

/*
 *	watchdog_get_status: wrapper to get the watchdog status
 *	@wdd: the watchdog device to get the status from
 *	@status: the status of the watchdog device
 *
 *	Get the watchdog's status flags.
 */

static int watchdog_get_status(struct watchdog_device *wdd,
							unsigned int *status)
{
	int err = 0;

	*status = 0;
	if (!wdd->ops->status)
		return -EOPNOTSUPP;

	mutex_lock(&wdd->lock);

	if (test_bit(WDOG_UNREGISTERED, &wdd->status)) {
		err = -ENODEV;
		goto out_status;
	}

	*status = wdd->ops->status(wdd);

out_status:
	mutex_unlock(&wdd->lock);
	return err;
}

/*
 *	watchdog_set_timeout: set the watchdog timer timeout
 *	@wdd: the watchdog device to set the timeout for
 *	@timeout: timeout to set in seconds
 */

static int watchdog_set_timeout(struct watchdog_device *wdd,
							unsigned int timeout)
{
	int err;

	if (!wdd->ops->set_timeout || !(wdd->info->options & WDIOF_SETTIMEOUT))
		return -EOPNOTSUPP;

	if (watchdog_timeout_invalid(wdd, timeout))
		return -EINVAL;

	mutex_lock(&wdd->lock);

	if (test_bit(WDOG_UNREGISTERED, &wdd->status)) {
		err = -ENODEV;
		goto out_timeout;
	}

	err = wdd->ops->set_timeout(wdd, timeout);

out_timeout:
	mutex_unlock(&wdd->lock);
	return err;
}

/*
 *	watchdog_get_timeleft: wrapper to get the time left before a reboot
 *	@wdd: the watchdog device to get the remaining time from
 *	@timeleft: the time that's left
 *
 *	Get the time before a watchdog will reboot (if not pinged).
 */

static int watchdog_get_timeleft(struct watchdog_device *wdd,
							unsigned int *timeleft)
{
	int err = 0;

	*timeleft = 0;
	if (!wdd->ops->get_timeleft)
		return -EOPNOTSUPP;

	mutex_lock(&wdd->lock);

	if (test_bit(WDOG_UNREGISTERED, &wdd->status)) {
		err = -ENODEV;
		goto out_timeleft;
	}

	*timeleft = wdd->ops->get_timeleft(wdd);

out_timeleft:
	mutex_unlock(&wdd->lock);
	return err;
}

/*
 *	watchdog_ioctl_op: call the watchdog drivers ioctl op if defined
 *	@wdd: the watchdog device to do the ioctl on
 *	@cmd: watchdog command
 *	@arg: argument pointer
 */

static int watchdog_ioctl_op(struct watchdog_device *wdd, unsigned int cmd,
							unsigned long arg)
{
	int err;

	if (!wdd->ops->ioctl)
		return -ENOIOCTLCMD;

	mutex_lock(&wdd->lock);

	if (test_bit(WDOG_UNREGISTERED, &wdd->status)) {
		err = -ENODEV;
		goto out_ioctl;
	}

	err = wdd->ops->ioctl(wdd, cmd, arg);

out_ioctl:
	mutex_unlock(&wdd->lock);
	return err;
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
	struct watchdog_device *wdd = file->private_data;
	size_t i;
	char c;
	int err;

	if (len == 0)
		return 0;

	/*
	 * Note: just in case someone wrote the magic character
	 * five months ago...
	 */
	clear_bit(WDOG_ALLOW_RELEASE, &wdd->status);

	/* scan to see whether or not we got the magic character */
	for (i = 0; i != len; i++) {
		if (get_user(c, data + i))
			return -EFAULT;
		if (c == 'V')
			set_bit(WDOG_ALLOW_RELEASE, &wdd->status);
	}

	/* someone wrote to us, so we send the watchdog a keepalive ping */
	err = watchdog_ping(wdd);
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
	struct watchdog_device *wdd = file->private_data;
	void __user *argp = (void __user *)arg;
	int __user *p = argp;
	unsigned int val;
	int err;

	err = watchdog_ioctl_op(wdd, cmd, arg);
	if (err != -ENOIOCTLCMD)
		return err;

	switch (cmd) {
	case WDIOC_GETSUPPORT:
		return copy_to_user(argp, wdd->info,
			sizeof(struct watchdog_info)) ? -EFAULT : 0;
	case WDIOC_GETSTATUS:
		err = watchdog_get_status(wdd, &val);
		if (err == -ENODEV)
			return err;
		return put_user(val, p);
	case WDIOC_GETBOOTSTATUS:
		return put_user(wdd->bootstatus, p);
	case WDIOC_SETOPTIONS:
		if (get_user(val, p))
			return -EFAULT;
		if (val & WDIOS_DISABLECARD) {
			err = watchdog_stop(wdd);
			if (err < 0)
				return err;
		}
		if (val & WDIOS_ENABLECARD) {
			err = watchdog_start(wdd);
			if (err < 0)
				return err;
		}
		return 0;
	case WDIOC_KEEPALIVE:
		if (!(wdd->info->options & WDIOF_KEEPALIVEPING))
			return -EOPNOTSUPP;
		return watchdog_ping(wdd);
	case WDIOC_SETTIMEOUT:
		if (get_user(val, p))
			return -EFAULT;
		err = watchdog_set_timeout(wdd, val);
		if (err < 0)
			return err;
		/* If the watchdog is active then we send a keepalive ping
		 * to make sure that the watchdog keep's running (and if
		 * possible that it takes the new timeout) */
		err = watchdog_ping(wdd);
		if (err < 0)
			return err;
		/* Fall */
	case WDIOC_GETTIMEOUT:
		/* timeout == 0 means that we don't know the timeout */
		if (wdd->timeout == 0)
			return -EOPNOTSUPP;
		return put_user(wdd->timeout, p);
	case WDIOC_GETTIMELEFT:
		err = watchdog_get_timeleft(wdd, &val);
		if (err)
			return err;
		return put_user(val, p);
	default:
		return -ENOTTY;
	}
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
	int err = -EBUSY;
	struct watchdog_device *wdd;

	/* Get the corresponding watchdog device */
	if (imajor(inode) == MISC_MAJOR)
		wdd = old_wdd;
	else
		wdd = container_of(inode->i_cdev, struct watchdog_device, cdev);

	/* the watchdog is single open! */
	if (test_and_set_bit(WDOG_DEV_OPEN, &wdd->status))
		return -EBUSY;

	/*
	 * If the /dev/watchdog device is open, we don't want the module
	 * to be unloaded.
	 */
	if (!try_module_get(wdd->ops->owner))
		goto out;

	err = watchdog_start(wdd);
	if (err < 0)
		goto out_mod;

	file->private_data = wdd;

	if (wdd->ops->ref)
		wdd->ops->ref(wdd);

	/* dev/watchdog is a virtual (and thus non-seekable) filesystem */
	return nonseekable_open(inode, file);

out_mod:
	module_put(wdd->ops->owner);
out:
	clear_bit(WDOG_DEV_OPEN, &wdd->status);
	return err;
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
	struct watchdog_device *wdd = file->private_data;
	int err = -EBUSY;

	/*
	 * We only stop the watchdog if we received the magic character
	 * or if WDIOF_MAGICCLOSE is not set. If nowayout was set then
	 * watchdog_stop will fail.
	 */
	if (!test_bit(WDOG_ACTIVE, &wdd->status))
		err = 0;
	else if (test_and_clear_bit(WDOG_ALLOW_RELEASE, &wdd->status) ||
		 !(wdd->info->options & WDIOF_MAGICCLOSE))
		err = watchdog_stop(wdd);

	/* If the watchdog was not stopped, send a keepalive ping */
	if (err < 0) {
		mutex_lock(&wdd->lock);
		if (!test_bit(WDOG_UNREGISTERED, &wdd->status))
			dev_crit(wdd->dev, "watchdog did not stop!\n");
		mutex_unlock(&wdd->lock);
		watchdog_ping(wdd);
	}

	/* Allow the owner module to be unloaded again */
	module_put(wdd->ops->owner);

	/* make sure that /dev/watchdog can be re-opened */
	clear_bit(WDOG_DEV_OPEN, &wdd->status);

	/* Note wdd may be gone after this, do not use after this! */
	if (wdd->ops->unref)
		wdd->ops->unref(wdd);

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
	int err, devno;

	if (wdd->id == 0) {
		old_wdd = wdd;
		watchdog_miscdev.parent = wdd->parent;
		err = misc_register(&watchdog_miscdev);
		if (err != 0) {
			pr_err("%s: cannot register miscdev on minor=%d (err=%d).\n",
				wdd->info->identity, WATCHDOG_MINOR, err);
			if (err == -EBUSY)
				pr_err("%s: a legacy watchdog module is probably present.\n",
					wdd->info->identity);
			old_wdd = NULL;
			return err;
		}
	}

	/* Fill in the data structures */
	devno = MKDEV(MAJOR(watchdog_devt), wdd->id);
	cdev_init(&wdd->cdev, &watchdog_fops);
	wdd->cdev.owner = wdd->ops->owner;

	/* Add the device */
	err  = cdev_add(&wdd->cdev, devno, 1);
	if (err) {
		pr_err("watchdog%d unable to add device %d:%d\n",
			wdd->id,  MAJOR(watchdog_devt), wdd->id);
		if (wdd->id == 0) {
			misc_deregister(&watchdog_miscdev);
			old_wdd = NULL;
		}
	}
	return err;
}

/*
 *	watchdog_dev_unregister: unregister a watchdog device
 *	@watchdog: watchdog device
 *
 *	Unregister the watchdog and if needed the legacy /dev/watchdog device.
 */

int watchdog_dev_unregister(struct watchdog_device *wdd)
{
	mutex_lock(&wdd->lock);
	set_bit(WDOG_UNREGISTERED, &wdd->status);
	mutex_unlock(&wdd->lock);

	cdev_del(&wdd->cdev);
	if (wdd->id == 0) {
		misc_deregister(&watchdog_miscdev);
		old_wdd = NULL;
	}
	return 0;
}

/*
 *	watchdog_dev_init: init dev part of watchdog core
 *
 *	Allocate a range of chardev nodes to use for watchdog devices
 */

int __init watchdog_dev_init(void)
{
	int err = alloc_chrdev_region(&watchdog_devt, 0, MAX_DOGS, "watchdog");
	if (err < 0)
		pr_err("watchdog: unable to allocate char dev region\n");
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
}

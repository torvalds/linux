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

/* make sure we only register one /dev/watchdog device */
static unsigned long watchdog_dev_busy;
/* the watchdog device behind /dev/watchdog */
static struct watchdog_device *wdd;

/*
 *	watchdog_ping: ping the watchdog.
 *	@wddev: the watchdog device to ping
 *
 *	If the watchdog has no own ping operation then it needs to be
 *	restarted via the start operation. This wrapper function does
 *	exactly that.
 *	We only ping when the watchdog device is running.
 */

static int watchdog_ping(struct watchdog_device *wddev)
{
	if (test_bit(WDOG_ACTIVE, &wddev->status)) {
		if (wddev->ops->ping)
			return wddev->ops->ping(wddev);  /* ping the watchdog */
		else
			return wddev->ops->start(wddev); /* restart watchdog */
	}
	return 0;
}

/*
 *	watchdog_start: wrapper to start the watchdog.
 *	@wddev: the watchdog device to start
 *
 *	Start the watchdog if it is not active and mark it active.
 *	This function returns zero on success or a negative errno code for
 *	failure.
 */

static int watchdog_start(struct watchdog_device *wddev)
{
	int err;

	if (!test_bit(WDOG_ACTIVE, &wddev->status)) {
		err = wddev->ops->start(wddev);
		if (err < 0)
			return err;

		set_bit(WDOG_ACTIVE, &wddev->status);
	}
	return 0;
}

/*
 *	watchdog_stop: wrapper to stop the watchdog.
 *	@wddev: the watchdog device to stop
 *
 *	Stop the watchdog if it is still active and unmark it active.
 *	This function returns zero on success or a negative errno code for
 *	failure.
 *	If the 'nowayout' feature was set, the watchdog cannot be stopped.
 */

static int watchdog_stop(struct watchdog_device *wddev)
{
	int err = -EBUSY;

	if (test_bit(WDOG_NO_WAY_OUT, &wddev->status)) {
		pr_info("%s: nowayout prevents watchdog to be stopped!\n",
							wddev->info->identity);
		return err;
	}

	if (test_bit(WDOG_ACTIVE, &wddev->status)) {
		err = wddev->ops->stop(wddev);
		if (err < 0)
			return err;

		clear_bit(WDOG_ACTIVE, &wddev->status);
	}
	return 0;
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
	size_t i;
	char c;

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
	watchdog_ping(wdd);

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
	void __user *argp = (void __user *)arg;
	int __user *p = argp;
	unsigned int val;
	int err;

	if (wdd->ops->ioctl) {
		err = wdd->ops->ioctl(wdd, cmd, arg);
		if (err != -ENOIOCTLCMD)
			return err;
	}

	switch (cmd) {
	case WDIOC_GETSUPPORT:
		return copy_to_user(argp, wdd->info,
			sizeof(struct watchdog_info)) ? -EFAULT : 0;
	case WDIOC_GETSTATUS:
		val = wdd->ops->status ? wdd->ops->status(wdd) : 0;
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
		watchdog_ping(wdd);
		return 0;
	case WDIOC_SETTIMEOUT:
		if ((wdd->ops->set_timeout == NULL) ||
		    !(wdd->info->options & WDIOF_SETTIMEOUT))
			return -EOPNOTSUPP;
		if (get_user(val, p))
			return -EFAULT;
		if ((wdd->max_timeout != 0) &&
		    (val < wdd->min_timeout || val > wdd->max_timeout))
				return -EINVAL;
		err = wdd->ops->set_timeout(wdd, val);
		if (err < 0)
			return err;
		/* If the watchdog is active then we send a keepalive ping
		 * to make sure that the watchdog keep's running (and if
		 * possible that it takes the new timeout) */
		watchdog_ping(wdd);
		/* Fall */
	case WDIOC_GETTIMEOUT:
		/* timeout == 0 means that we don't know the timeout */
		if (wdd->timeout == 0)
			return -EOPNOTSUPP;
		return put_user(wdd->timeout, p);
	case WDIOC_GETTIMELEFT:
		if (!wdd->ops->get_timeleft)
			return -EOPNOTSUPP;

		return put_user(wdd->ops->get_timeleft(wdd), p);
	default:
		return -ENOTTY;
	}
}

/*
 *	watchdog_open: open the /dev/watchdog device.
 *	@inode: inode of device
 *	@file: file handle to device
 *
 *	When the /dev/watchdog device gets opened, we start the watchdog.
 *	Watch out: the /dev/watchdog device is single open, so we make sure
 *	it can only be opened once.
 */

static int watchdog_open(struct inode *inode, struct file *file)
{
	int err = -EBUSY;

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

	/* dev/watchdog is a virtual (and thus non-seekable) filesystem */
	return nonseekable_open(inode, file);

out_mod:
	module_put(wdd->ops->owner);
out:
	clear_bit(WDOG_DEV_OPEN, &wdd->status);
	return err;
}

/*
 *      watchdog_release: release the /dev/watchdog device.
 *      @inode: inode of device
 *      @file: file handle to device
 *
 *	This is the code for when /dev/watchdog gets closed. We will only
 *	stop the watchdog when we have received the magic char (and nowayout
 *	was not set), else the watchdog will keep running.
 */

static int watchdog_release(struct inode *inode, struct file *file)
{
	int err = -EBUSY;

	/*
	 * We only stop the watchdog if we received the magic character
	 * or if WDIOF_MAGICCLOSE is not set. If nowayout was set then
	 * watchdog_stop will fail.
	 */
	if (test_and_clear_bit(WDOG_ALLOW_RELEASE, &wdd->status) ||
	    !(wdd->info->options & WDIOF_MAGICCLOSE))
		err = watchdog_stop(wdd);

	/* If the watchdog was not stopped, send a keepalive ping */
	if (err < 0) {
		pr_crit("%s: watchdog did not stop!\n", wdd->info->identity);
		watchdog_ping(wdd);
	}

	/* Allow the owner module to be unloaded again */
	module_put(wdd->ops->owner);

	/* make sure that /dev/watchdog can be re-opened */
	clear_bit(WDOG_DEV_OPEN, &wdd->status);

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
 *	watchdog_dev_register:
 *	@watchdog: watchdog device
 *
 *	Register a watchdog device as /dev/watchdog. /dev/watchdog
 *	is actually a miscdevice and thus we set it up like that.
 */

int watchdog_dev_register(struct watchdog_device *watchdog)
{
	int err;

	/* Only one device can register for /dev/watchdog */
	if (test_and_set_bit(0, &watchdog_dev_busy)) {
		pr_err("only one watchdog can use /dev/watchdog\n");
		return -EBUSY;
	}

	wdd = watchdog;

	err = misc_register(&watchdog_miscdev);
	if (err != 0) {
		pr_err("%s: cannot register miscdev on minor=%d (err=%d)\n",
		       watchdog->info->identity, WATCHDOG_MINOR, err);
		goto out;
	}

	return 0;

out:
	wdd = NULL;
	clear_bit(0, &watchdog_dev_busy);
	return err;
}

/*
 *	watchdog_dev_unregister:
 *	@watchdog: watchdog device
 *
 *	Deregister the /dev/watchdog device.
 */

int watchdog_dev_unregister(struct watchdog_device *watchdog)
{
	/* Check that a watchdog device was registered in the past */
	if (!test_bit(0, &watchdog_dev_busy) || !wdd)
		return -ENODEV;

	/* We can only unregister the watchdog device that was registered */
	if (watchdog != wdd) {
		pr_err("%s: watchdog was not registered as /dev/watchdog\n",
		       watchdog->info->identity);
		return -ENODEV;
	}

	misc_deregister(&watchdog_miscdev);
	wdd = NULL;
	clear_bit(0, &watchdog_dev_busy);
	return 0;
}

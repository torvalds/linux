/*
 *	Xen Watchdog Driver
 *
 *	(c) Copyright 2010 Novell, Inc.
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#define DRV_NAME	"wdt"
#define DRV_VERSION	"0.01"

#include <linux/bug.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/hrtimer.h>
#include <linux/kernel.h>
#include <linux/ktime.h>
#include <linux/init.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/platform_device.h>
#include <linux/spinlock.h>
#include <linux/uaccess.h>
#include <linux/watchdog.h>
#include <xen/xen.h>
#include <asm/xen/hypercall.h>
#include <xen/interface/sched.h>

static struct platform_device *platform_device;
static DEFINE_SPINLOCK(wdt_lock);
static struct sched_watchdog wdt;
static __kernel_time_t wdt_expires;
static bool is_active, expect_release;

#define WATCHDOG_TIMEOUT 60 /* in seconds */
static unsigned int timeout = WATCHDOG_TIMEOUT;
module_param(timeout, uint, S_IRUGO);
MODULE_PARM_DESC(timeout, "Watchdog timeout in seconds "
	"(default=" __MODULE_STRING(WATCHDOG_TIMEOUT) ")");

static bool nowayout = WATCHDOG_NOWAYOUT;
module_param(nowayout, bool, S_IRUGO);
MODULE_PARM_DESC(nowayout, "Watchdog cannot be stopped once started "
	"(default=" __MODULE_STRING(WATCHDOG_NOWAYOUT) ")");

static inline __kernel_time_t set_timeout(void)
{
	wdt.timeout = timeout;
	return ktime_to_timespec(ktime_get()).tv_sec + timeout;
}

static int xen_wdt_start(void)
{
	__kernel_time_t expires;
	int err;

	spin_lock(&wdt_lock);

	expires = set_timeout();
	if (!wdt.id)
		err = HYPERVISOR_sched_op(SCHEDOP_watchdog, &wdt);
	else
		err = -EBUSY;
	if (err > 0) {
		wdt.id = err;
		wdt_expires = expires;
		err = 0;
	} else
		BUG_ON(!err);

	spin_unlock(&wdt_lock);

	return err;
}

static int xen_wdt_stop(void)
{
	int err = 0;

	spin_lock(&wdt_lock);

	wdt.timeout = 0;
	if (wdt.id)
		err = HYPERVISOR_sched_op(SCHEDOP_watchdog, &wdt);
	if (!err)
		wdt.id = 0;

	spin_unlock(&wdt_lock);

	return err;
}

static int xen_wdt_kick(void)
{
	__kernel_time_t expires;
	int err;

	spin_lock(&wdt_lock);

	expires = set_timeout();
	if (wdt.id)
		err = HYPERVISOR_sched_op(SCHEDOP_watchdog, &wdt);
	else
		err = -ENXIO;
	if (!err)
		wdt_expires = expires;

	spin_unlock(&wdt_lock);

	return err;
}

static int xen_wdt_open(struct inode *inode, struct file *file)
{
	int err;

	/* /dev/watchdog can only be opened once */
	if (xchg(&is_active, true))
		return -EBUSY;

	err = xen_wdt_start();
	if (err == -EBUSY)
		err = xen_wdt_kick();
	return err ?: nonseekable_open(inode, file);
}

static int xen_wdt_release(struct inode *inode, struct file *file)
{
	int err = 0;

	if (expect_release)
		err = xen_wdt_stop();
	else {
		pr_crit("unexpected close, not stopping watchdog!\n");
		xen_wdt_kick();
	}
	is_active = err;
	expect_release = false;
	return err;
}

static ssize_t xen_wdt_write(struct file *file, const char __user *data,
			     size_t len, loff_t *ppos)
{
	/* See if we got the magic character 'V' and reload the timer */
	if (len) {
		if (!nowayout) {
			size_t i;

			/* in case it was set long ago */
			expect_release = false;

			/* scan to see whether or not we got the magic
			   character */
			for (i = 0; i != len; i++) {
				char c;
				if (get_user(c, data + i))
					return -EFAULT;
				if (c == 'V')
					expect_release = true;
			}
		}

		/* someone wrote to us, we should reload the timer */
		xen_wdt_kick();
	}
	return len;
}

static long xen_wdt_ioctl(struct file *file, unsigned int cmd,
			  unsigned long arg)
{
	int new_options, retval = -EINVAL;
	int new_timeout;
	int __user *argp = (void __user *)arg;
	static const struct watchdog_info ident = {
		.options =		WDIOF_SETTIMEOUT | WDIOF_MAGICCLOSE,
		.firmware_version =	0,
		.identity =		DRV_NAME,
	};

	switch (cmd) {
	case WDIOC_GETSUPPORT:
		return copy_to_user(argp, &ident, sizeof(ident)) ? -EFAULT : 0;

	case WDIOC_GETSTATUS:
	case WDIOC_GETBOOTSTATUS:
		return put_user(0, argp);

	case WDIOC_SETOPTIONS:
		if (get_user(new_options, argp))
			return -EFAULT;

		if (new_options & WDIOS_DISABLECARD)
			retval = xen_wdt_stop();
		if (new_options & WDIOS_ENABLECARD) {
			retval = xen_wdt_start();
			if (retval == -EBUSY)
				retval = xen_wdt_kick();
		}
		return retval;

	case WDIOC_KEEPALIVE:
		xen_wdt_kick();
		return 0;

	case WDIOC_SETTIMEOUT:
		if (get_user(new_timeout, argp))
			return -EFAULT;
		if (!new_timeout)
			return -EINVAL;
		timeout = new_timeout;
		xen_wdt_kick();
		/* fall through */
	case WDIOC_GETTIMEOUT:
		return put_user(timeout, argp);

	case WDIOC_GETTIMELEFT:
		retval = wdt_expires - ktime_to_timespec(ktime_get()).tv_sec;
		return put_user(retval, argp);
	}

	return -ENOTTY;
}

static const struct file_operations xen_wdt_fops = {
	.owner =		THIS_MODULE,
	.llseek =		no_llseek,
	.write =		xen_wdt_write,
	.unlocked_ioctl =	xen_wdt_ioctl,
	.open =			xen_wdt_open,
	.release =		xen_wdt_release,
};

static struct miscdevice xen_wdt_miscdev = {
	.minor =	WATCHDOG_MINOR,
	.name =		"watchdog",
	.fops =		&xen_wdt_fops,
};

static int xen_wdt_probe(struct platform_device *dev)
{
	struct sched_watchdog wd = { .id = ~0 };
	int ret = HYPERVISOR_sched_op(SCHEDOP_watchdog, &wd);

	switch (ret) {
	case -EINVAL:
		if (!timeout) {
			timeout = WATCHDOG_TIMEOUT;
			pr_info("timeout value invalid, using %d\n", timeout);
		}

		ret = misc_register(&xen_wdt_miscdev);
		if (ret) {
			pr_err("cannot register miscdev on minor=%d (%d)\n",
			       WATCHDOG_MINOR, ret);
			break;
		}

		pr_info("initialized (timeout=%ds, nowayout=%d)\n",
			timeout, nowayout);
		break;

	case -ENOSYS:
		pr_info("not supported\n");
		ret = -ENODEV;
		break;

	default:
		pr_info("bogus return value %d\n", ret);
		break;
	}

	return ret;
}

static int xen_wdt_remove(struct platform_device *dev)
{
	/* Stop the timer before we leave */
	if (!nowayout)
		xen_wdt_stop();

	misc_deregister(&xen_wdt_miscdev);

	return 0;
}

static void xen_wdt_shutdown(struct platform_device *dev)
{
	xen_wdt_stop();
}

static int xen_wdt_suspend(struct platform_device *dev, pm_message_t state)
{
	typeof(wdt.id) id = wdt.id;
	int rc = xen_wdt_stop();

	wdt.id = id;
	return rc;
}

static int xen_wdt_resume(struct platform_device *dev)
{
	if (!wdt.id)
		return 0;
	wdt.id = 0;
	return xen_wdt_start();
}

static struct platform_driver xen_wdt_driver = {
	.probe          = xen_wdt_probe,
	.remove         = xen_wdt_remove,
	.shutdown       = xen_wdt_shutdown,
	.suspend        = xen_wdt_suspend,
	.resume         = xen_wdt_resume,
	.driver         = {
		.owner  = THIS_MODULE,
		.name   = DRV_NAME,
	},
};

static int __init xen_wdt_init_module(void)
{
	int err;

	if (!xen_domain())
		return -ENODEV;

	pr_info("Xen WatchDog Timer Driver v%s\n", DRV_VERSION);

	err = platform_driver_register(&xen_wdt_driver);
	if (err)
		return err;

	platform_device = platform_device_register_simple(DRV_NAME,
								  -1, NULL, 0);
	if (IS_ERR(platform_device)) {
		err = PTR_ERR(platform_device);
		platform_driver_unregister(&xen_wdt_driver);
	}

	return err;
}

static void __exit xen_wdt_cleanup_module(void)
{
	platform_device_unregister(platform_device);
	platform_driver_unregister(&xen_wdt_driver);
	pr_info("module unloaded\n");
}

module_init(xen_wdt_init_module);
module_exit(xen_wdt_cleanup_module);

MODULE_AUTHOR("Jan Beulich <jbeulich@novell.com>");
MODULE_DESCRIPTION("Xen WatchDog Timer Driver");
MODULE_VERSION(DRV_VERSION);
MODULE_LICENSE("GPL");
MODULE_ALIAS_MISCDEV(WATCHDOG_MINOR);

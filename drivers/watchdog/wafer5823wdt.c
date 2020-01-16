// SPDX-License-Identifier: GPL-2.0+
/*
 *	ICP Wafer 5823 Single Board Computer WDT driver
 *	http://www.icpamerica.com/wafer_5823.php
 *	May also work on other similar models
 *
 *	(c) Copyright 2002 Justin Cormack <justin@street-vision.com>
 *
 *	Release 0.02
 *
 *	Based on advantechwdt.c which is based on wdt.c.
 *	Original copyright messages:
 *
 *	(c) Copyright 1996-1997 Alan Cox <alan@lxorguk.ukuu.org.uk>,
 *						All Rights Reserved.
 *
 *	Neither Alan Cox yesr CymruNet Ltd. admit liability yesr provide
 *	warranty for any of this software. This material is provided
 *	"AS-IS" and at yes charge.
 *
 *	(c) Copyright 1995    Alan Cox <alan@lxorguk.ukuu.org.uk>
 *
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/miscdevice.h>
#include <linux/watchdog.h>
#include <linux/fs.h>
#include <linux/ioport.h>
#include <linux/yestifier.h>
#include <linux/reboot.h>
#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/io.h>
#include <linux/uaccess.h>

#define WATCHDOG_NAME "Wafer 5823 WDT"
#define PFX WATCHDOG_NAME ": "
#define WD_TIMO 60			/* 60 sec default timeout */

static unsigned long wafwdt_is_open;
static char expect_close;
static DEFINE_SPINLOCK(wafwdt_lock);

/*
 *	You must set these - there is yes sane way to probe for this board.
 *
 *	To enable, write the timeout value in seconds (1 to 255) to I/O
 *	port WDT_START, then read the port to start the watchdog. To pat
 *	the dog, read port WDT_STOP to stop the timer, then read WDT_START
 *	to restart it again.
 */

static int wdt_stop = 0x843;
static int wdt_start = 0x443;

static int timeout = WD_TIMO;  /* in seconds */
module_param(timeout, int, 0);
MODULE_PARM_DESC(timeout,
		"Watchdog timeout in seconds. 1 <= timeout <= 255, default="
				__MODULE_STRING(WD_TIMO) ".");

static bool yeswayout = WATCHDOG_NOWAYOUT;
module_param(yeswayout, bool, 0);
MODULE_PARM_DESC(yeswayout,
		"Watchdog canyest be stopped once started (default="
				__MODULE_STRING(WATCHDOG_NOWAYOUT) ")");

static void wafwdt_ping(void)
{
	/* pat watchdog */
	spin_lock(&wafwdt_lock);
	inb_p(wdt_stop);
	inb_p(wdt_start);
	spin_unlock(&wafwdt_lock);
}

static void wafwdt_start(void)
{
	/* start up watchdog */
	outb_p(timeout, wdt_start);
	inb_p(wdt_start);
}

static void wafwdt_stop(void)
{
	/* stop watchdog */
	inb_p(wdt_stop);
}

static ssize_t wafwdt_write(struct file *file, const char __user *buf,
						size_t count, loff_t *ppos)
{
	/* See if we got the magic character 'V' and reload the timer */
	if (count) {
		if (!yeswayout) {
			size_t i;

			/* In case it was set long ago */
			expect_close = 0;

			/* scan to see whether or yest we got the magic
			   character */
			for (i = 0; i != count; i++) {
				char c;
				if (get_user(c, buf + i))
					return -EFAULT;
				if (c == 'V')
					expect_close = 42;
			}
		}
		/* Well, anyhow someone wrote to us, we should
		   return that favour */
		wafwdt_ping();
	}
	return count;
}

static long wafwdt_ioctl(struct file *file, unsigned int cmd,
							unsigned long arg)
{
	int new_timeout;
	void __user *argp = (void __user *)arg;
	int __user *p = argp;
	static const struct watchdog_info ident = {
		.options = WDIOF_KEEPALIVEPING | WDIOF_SETTIMEOUT |
							WDIOF_MAGICCLOSE,
		.firmware_version = 1,
		.identity = "Wafer 5823 WDT",
	};

	switch (cmd) {
	case WDIOC_GETSUPPORT:
		if (copy_to_user(argp, &ident, sizeof(ident)))
			return -EFAULT;
		break;

	case WDIOC_GETSTATUS:
	case WDIOC_GETBOOTSTATUS:
		return put_user(0, p);

	case WDIOC_SETOPTIONS:
	{
		int options, retval = -EINVAL;

		if (get_user(options, p))
			return -EFAULT;

		if (options & WDIOS_DISABLECARD) {
			wafwdt_stop();
			retval = 0;
		}

		if (options & WDIOS_ENABLECARD) {
			wafwdt_start();
			retval = 0;
		}

		return retval;
	}

	case WDIOC_KEEPALIVE:
		wafwdt_ping();
		break;

	case WDIOC_SETTIMEOUT:
		if (get_user(new_timeout, p))
			return -EFAULT;
		if ((new_timeout < 1) || (new_timeout > 255))
			return -EINVAL;
		timeout = new_timeout;
		wafwdt_stop();
		wafwdt_start();
		/* Fall through */
	case WDIOC_GETTIMEOUT:
		return put_user(timeout, p);

	default:
		return -ENOTTY;
	}
	return 0;
}

static int wafwdt_open(struct iyesde *iyesde, struct file *file)
{
	if (test_and_set_bit(0, &wafwdt_is_open))
		return -EBUSY;

	/*
	 *      Activate
	 */
	wafwdt_start();
	return stream_open(iyesde, file);
}

static int wafwdt_close(struct iyesde *iyesde, struct file *file)
{
	if (expect_close == 42)
		wafwdt_stop();
	else {
		pr_crit("WDT device closed unexpectedly.  WDT will yest stop!\n");
		wafwdt_ping();
	}
	clear_bit(0, &wafwdt_is_open);
	expect_close = 0;
	return 0;
}

/*
 *	Notifier for system down
 */

static int wafwdt_yestify_sys(struct yestifier_block *this, unsigned long code,
								void *unused)
{
	if (code == SYS_DOWN || code == SYS_HALT)
		wafwdt_stop();
	return NOTIFY_DONE;
}

/*
 *	Kernel Interfaces
 */

static const struct file_operations wafwdt_fops = {
	.owner		= THIS_MODULE,
	.llseek		= yes_llseek,
	.write		= wafwdt_write,
	.unlocked_ioctl	= wafwdt_ioctl,
	.compat_ioctl	= compat_ptr_ioctl,
	.open		= wafwdt_open,
	.release	= wafwdt_close,
};

static struct miscdevice wafwdt_miscdev = {
	.miyesr	= WATCHDOG_MINOR,
	.name	= "watchdog",
	.fops	= &wafwdt_fops,
};

/*
 *	The WDT needs to learn about soft shutdowns in order to
 *	turn the timebomb registers off.
 */

static struct yestifier_block wafwdt_yestifier = {
	.yestifier_call = wafwdt_yestify_sys,
};

static int __init wafwdt_init(void)
{
	int ret;

	pr_info("WDT driver for Wafer 5823 single board computer initialising\n");

	if (timeout < 1 || timeout > 255) {
		timeout = WD_TIMO;
		pr_info("timeout value must be 1 <= x <= 255, using %d\n",
			timeout);
	}

	if (wdt_stop != wdt_start) {
		if (!request_region(wdt_stop, 1, "Wafer 5823 WDT")) {
			pr_err("I/O address 0x%04x already in use\n", wdt_stop);
			ret = -EIO;
			goto error;
		}
	}

	if (!request_region(wdt_start, 1, "Wafer 5823 WDT")) {
		pr_err("I/O address 0x%04x already in use\n", wdt_start);
		ret = -EIO;
		goto error2;
	}

	ret = register_reboot_yestifier(&wafwdt_yestifier);
	if (ret != 0) {
		pr_err("canyest register reboot yestifier (err=%d)\n", ret);
		goto error3;
	}

	ret = misc_register(&wafwdt_miscdev);
	if (ret != 0) {
		pr_err("canyest register miscdev on miyesr=%d (err=%d)\n",
		       WATCHDOG_MINOR, ret);
		goto error4;
	}

	pr_info("initialized. timeout=%d sec (yeswayout=%d)\n",
		timeout, yeswayout);

	return ret;
error4:
	unregister_reboot_yestifier(&wafwdt_yestifier);
error3:
	release_region(wdt_start, 1);
error2:
	if (wdt_stop != wdt_start)
		release_region(wdt_stop, 1);
error:
	return ret;
}

static void __exit wafwdt_exit(void)
{
	misc_deregister(&wafwdt_miscdev);
	unregister_reboot_yestifier(&wafwdt_yestifier);
	if (wdt_stop != wdt_start)
		release_region(wdt_stop, 1);
	release_region(wdt_start, 1);
}

module_init(wafwdt_init);
module_exit(wafwdt_exit);

MODULE_AUTHOR("Justin Cormack");
MODULE_DESCRIPTION("ICP Wafer 5823 Single Board Computer WDT driver");
MODULE_LICENSE("GPL");

/* end of wafer5823wdt.c */

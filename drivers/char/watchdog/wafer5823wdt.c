/*
 *	ICP Wafer 5823 Single Board Computer WDT driver
 *      http://www.icpamerica.com/wafer_5823.php
 *      May also work on other similar models
 *
 *	(c) Copyright 2002 Justin Cormack <justin@street-vision.com>
 *
 *      Release 0.02
 *
 *	Based on advantechwdt.c which is based on wdt.c.
 *	Original copyright messages:
 *
 *	(c) Copyright 1996-1997 Alan Cox <alan@redhat.com>, All Rights Reserved.
 *				http://www.redhat.com
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 *
 *	Neither Alan Cox nor CymruNet Ltd. admit liability nor provide
 *	warranty for any of this software. This material is provided
 *	"AS-IS" and at no charge.
 *
 *	(c) Copyright 1995    Alan Cox <alan@lxorguk.ukuu.org.uk>
 *
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/miscdevice.h>
#include <linux/watchdog.h>
#include <linux/fs.h>
#include <linux/ioport.h>
#include <linux/notifier.h>
#include <linux/reboot.h>
#include <linux/init.h>
#include <linux/spinlock.h>
#include <asm/io.h>
#include <asm/uaccess.h>

#define WATCHDOG_NAME "Wafer 5823 WDT"
#define PFX WATCHDOG_NAME ": "
#define WD_TIMO 60			/* 60 sec default timeout */

static unsigned long wafwdt_is_open;
static char expect_close;
static spinlock_t wafwdt_lock;

/*
 *	You must set these - there is no sane way to probe for this board.
 *
 *      To enable, write the timeout value in seconds (1 to 255) to I/O
 *      port WDT_START, then read the port to start the watchdog. To pat
 *      the dog, read port WDT_STOP to stop the timer, then read WDT_START
 *      to restart it again.
 */

static int wdt_stop = 0x843;
static int wdt_start = 0x443;

static int timeout = WD_TIMO;  /* in seconds */
module_param(timeout, int, 0);
MODULE_PARM_DESC(timeout, "Watchdog timeout in seconds. 1<= timeout <=255, default=" __MODULE_STRING(WD_TIMO) ".");

static int nowayout = WATCHDOG_NOWAYOUT;
module_param(nowayout, int, 0);
MODULE_PARM_DESC(nowayout, "Watchdog cannot be stopped once started (default=CONFIG_WATCHDOG_NOWAYOUT)");

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

static void
wafwdt_stop(void)
{
	/* stop watchdog */
	inb_p(wdt_stop);
}

static ssize_t wafwdt_write(struct file *file, const char __user *buf, size_t count, loff_t * ppos)
{
	/* See if we got the magic character 'V' and reload the timer */
	if (count) {
		if (!nowayout) {
			size_t i;

			/* In case it was set long ago */
			expect_close = 0;

			/* scan to see whether or not we got the magic character */
			for (i = 0; i != count; i++) {
				char c;
				if (get_user(c, buf + i))
					return -EFAULT;
				if (c == 'V')
					expect_close = 42;
			}
		}
		/* Well, anyhow someone wrote to us, we should return that favour */
		wafwdt_ping();
	}
	return count;
}

static int wafwdt_ioctl(struct inode *inode, struct file *file, unsigned int cmd,
	     unsigned long arg)
{
	int new_timeout;
	void __user *argp = (void __user *)arg;
	int __user *p = argp;
	static struct watchdog_info ident = {
		.options = WDIOF_KEEPALIVEPING | WDIOF_SETTIMEOUT | WDIOF_MAGICCLOSE,
		.firmware_version = 1,
		.identity = "Wafer 5823 WDT",
	};

	switch (cmd) {
	case WDIOC_GETSUPPORT:
		if (copy_to_user(argp, &ident, sizeof (ident)))
			return -EFAULT;
		break;

	case WDIOC_GETSTATUS:
	case WDIOC_GETBOOTSTATUS:
		return put_user(0, p);

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
		/* Fall */
	case WDIOC_GETTIMEOUT:
		return put_user(timeout, p);

	case WDIOC_SETOPTIONS:
	{
		int options, retval = -EINVAL;

		if (get_user(options, p))
			return -EFAULT;

		if (options & WDIOS_DISABLECARD) {
			wafwdt_start();
			retval = 0;
		}

		if (options & WDIOS_ENABLECARD) {
			wafwdt_stop();
			retval = 0;
		}

		return retval;
	}

	default:
		return -ENOIOCTLCMD;
	}
	return 0;
}

static int wafwdt_open(struct inode *inode, struct file *file)
{
	if (test_and_set_bit(0, &wafwdt_is_open))
		return -EBUSY;

	/*
	 *      Activate
	 */
	wafwdt_start();
	return nonseekable_open(inode, file);
}

static int
wafwdt_close(struct inode *inode, struct file *file)
{
	if (expect_close == 42) {
		wafwdt_stop();
	} else {
		printk(KERN_CRIT PFX "WDT device closed unexpectedly.  WDT will not stop!\n");
		wafwdt_ping();
	}
	clear_bit(0, &wafwdt_is_open);
	expect_close = 0;
	return 0;
}

/*
 *	Notifier for system down
 */

static int wafwdt_notify_sys(struct notifier_block *this, unsigned long code, void *unused)
{
	if (code == SYS_DOWN || code == SYS_HALT) {
		/* Turn the WDT off */
		wafwdt_stop();
	}
	return NOTIFY_DONE;
}

/*
 *	Kernel Interfaces
 */

static struct file_operations wafwdt_fops = {
	.owner		= THIS_MODULE,
	.llseek		= no_llseek,
	.write		= wafwdt_write,
	.ioctl		= wafwdt_ioctl,
	.open		= wafwdt_open,
	.release	= wafwdt_close,
};

static struct miscdevice wafwdt_miscdev = {
	.minor	= WATCHDOG_MINOR,
	.name	= "watchdog",
	.fops	= &wafwdt_fops,
};

/*
 *	The WDT needs to learn about soft shutdowns in order to
 *	turn the timebomb registers off.
 */

static struct notifier_block wafwdt_notifier = {
	.notifier_call = wafwdt_notify_sys,
};

static int __init wafwdt_init(void)
{
	int ret;

	printk(KERN_INFO "WDT driver for Wafer 5823 single board computer initialising.\n");

	spin_lock_init(&wafwdt_lock);

	if (timeout < 1 || timeout > 255) {
		timeout = WD_TIMO;
		printk (KERN_INFO PFX "timeout value must be 1<=x<=255, using %d\n",
			timeout);
	}

	if (wdt_stop != wdt_start) {
		if(!request_region(wdt_stop, 1, "Wafer 5823 WDT")) {
			printk (KERN_ERR PFX "I/O address 0x%04x already in use\n",
			wdt_stop);
			ret = -EIO;
			goto error;
		}
	}

	if(!request_region(wdt_start, 1, "Wafer 5823 WDT")) {
		printk (KERN_ERR PFX "I/O address 0x%04x already in use\n",
			wdt_start);
		ret = -EIO;
		goto error2;
	}

	ret = register_reboot_notifier(&wafwdt_notifier);
	if (ret != 0) {
		printk (KERN_ERR PFX "cannot register reboot notifier (err=%d)\n",
			ret);
		goto error3;
	}

	ret = misc_register(&wafwdt_miscdev);
	if (ret != 0) {
		printk (KERN_ERR PFX "cannot register miscdev on minor=%d (err=%d)\n",
			WATCHDOG_MINOR, ret);
		goto error4;
	}

	printk (KERN_INFO PFX "initialized. timeout=%d sec (nowayout=%d)\n",
		timeout, nowayout);

	return ret;
error4:
	unregister_reboot_notifier(&wafwdt_notifier);
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
	unregister_reboot_notifier(&wafwdt_notifier);
	if(wdt_stop != wdt_start)
		release_region(wdt_stop, 1);
	release_region(wdt_start, 1);
}

module_init(wafwdt_init);
module_exit(wafwdt_exit);

MODULE_AUTHOR("Justin Cormack");
MODULE_DESCRIPTION("ICP Wafer 5823 Single Board Computer WDT driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS_MISCDEV(WATCHDOG_MINOR);

/* end of wafer5823wdt.c */

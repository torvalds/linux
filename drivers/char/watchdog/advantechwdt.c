/*
 *	Advantech Single Board Computer WDT driver
 *
 *	(c) Copyright 2000-2001 Marek Michalkiewicz <marekm@linux.org.pl>
 *
 *	Based on acquirewdt.c which is based on wdt.c.
 *	Original copyright messages:
 *
 *	(c) Copyright 1996 Alan Cox <alan@redhat.com>, All Rights Reserved.
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
 *	(c) Copyright 1995    Alan Cox <alan@redhat.com>
 *
 *	14-Dec-2001 Matt Domsch <Matt_Domsch@dell.com>
 *	    Added nowayout module option to override CONFIG_WATCHDOG_NOWAYOUT
 *
 *	16-Oct-2002 Rob Radez <rob@osinvestor.com>
 *	    Clean up ioctls, clean up init + exit, add expect close support,
 *	    add wdt_start and wdt_stop as parameters.
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/types.h>
#include <linux/miscdevice.h>
#include <linux/watchdog.h>
#include <linux/fs.h>
#include <linux/ioport.h>
#include <linux/notifier.h>
#include <linux/reboot.h>
#include <linux/init.h>

#include <asm/io.h>
#include <asm/uaccess.h>
#include <asm/system.h>

#define WATCHDOG_NAME "Advantech WDT"
#define PFX WATCHDOG_NAME ": "
#define WATCHDOG_TIMEOUT 60		/* 60 sec default timeout */

static unsigned long advwdt_is_open;
static char adv_expect_close;

/*
 *	You must set these - there is no sane way to probe for this board.
 *
 *	To enable or restart, write the timeout value in seconds (1 to 63)
 *	to I/O port wdt_start.  To disable, read I/O port wdt_stop.
 *	Both are 0x443 for most boards (tested on a PCA-6276VE-00B1), but
 *	check your manual (at least the PCA-6159 seems to be different -
 *	the manual says wdt_stop is 0x43, not 0x443).
 *	(0x43 is also a write-only control register for the 8254 timer!)
 */

static int wdt_stop = 0x443;
module_param(wdt_stop, int, 0);
MODULE_PARM_DESC(wdt_stop, "Advantech WDT 'stop' io port (default 0x443)");

static int wdt_start = 0x443;
module_param(wdt_start, int, 0);
MODULE_PARM_DESC(wdt_start, "Advantech WDT 'start' io port (default 0x443)");

static int timeout = WATCHDOG_TIMEOUT;	/* in seconds */
module_param(timeout, int, 0);
MODULE_PARM_DESC(timeout, "Watchdog timeout in seconds. 1<= timeout <=63, default=" __MODULE_STRING(WATCHDOG_TIMEOUT) ".");

static int nowayout = WATCHDOG_NOWAYOUT;
module_param(nowayout, int, 0);
MODULE_PARM_DESC(nowayout, "Watchdog cannot be stopped once started (default=CONFIG_WATCHDOG_NOWAYOUT)");

/*
 *	Kernel methods.
 */

static void
advwdt_ping(void)
{
	/* Write a watchdog value */
	outb_p(timeout, wdt_start);
}

static void
advwdt_disable(void)
{
	inb_p(wdt_stop);
}

static ssize_t
advwdt_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos)
{
	if (count) {
		if (!nowayout) {
			size_t i;

			adv_expect_close = 0;

			for (i = 0; i != count; i++) {
				char c;
				if (get_user(c, buf+i))
					return -EFAULT;
				if (c == 'V')
					adv_expect_close = 42;
			}
		}
		advwdt_ping();
	}
	return count;
}

static int
advwdt_ioctl(struct inode *inode, struct file *file, unsigned int cmd,
	  unsigned long arg)
{
	int new_timeout;
	void __user *argp = (void __user *)arg;
	int __user *p = argp;
	static struct watchdog_info ident = {
		.options = WDIOF_KEEPALIVEPING | WDIOF_SETTIMEOUT | WDIOF_MAGICCLOSE,
		.firmware_version = 1,
		.identity = "Advantech WDT",
	};

	switch (cmd) {
	case WDIOC_GETSUPPORT:
	  if (copy_to_user(argp, &ident, sizeof(ident)))
	    return -EFAULT;
	  break;

	case WDIOC_GETSTATUS:
	case WDIOC_GETBOOTSTATUS:
	  return put_user(0, p);

	case WDIOC_KEEPALIVE:
	  advwdt_ping();
	  break;

	case WDIOC_SETTIMEOUT:
	  if (get_user(new_timeout, p))
		  return -EFAULT;
	  if ((new_timeout < 1) || (new_timeout > 63))
		  return -EINVAL;
	  timeout = new_timeout;
	  advwdt_ping();
	  /* Fall */

	case WDIOC_GETTIMEOUT:
	  return put_user(timeout, p);

	case WDIOC_SETOPTIONS:
	{
	  int options, retval = -EINVAL;

	  if (get_user(options, p))
	    return -EFAULT;

	  if (options & WDIOS_DISABLECARD) {
	    advwdt_disable();
	    retval = 0;
	  }

	  if (options & WDIOS_ENABLECARD) {
	    advwdt_ping();
	    retval = 0;
	  }

	  return retval;
	}

	default:
	  return -ENOIOCTLCMD;
	}
	return 0;
}

static int
advwdt_open(struct inode *inode, struct file *file)
{
	if (test_and_set_bit(0, &advwdt_is_open))
		return -EBUSY;
	/*
	 *	Activate
	 */

	advwdt_ping();
	return nonseekable_open(inode, file);
}

static int
advwdt_close(struct inode *inode, struct file *file)
{
	if (adv_expect_close == 42) {
		advwdt_disable();
	} else {
		printk(KERN_CRIT PFX "Unexpected close, not stopping watchdog!\n");
		advwdt_ping();
	}
	clear_bit(0, &advwdt_is_open);
	adv_expect_close = 0;
	return 0;
}

/*
 *	Notifier for system down
 */

static int
advwdt_notify_sys(struct notifier_block *this, unsigned long code,
	void *unused)
{
	if (code == SYS_DOWN || code == SYS_HALT) {
		/* Turn the WDT off */
		advwdt_disable();
	}
	return NOTIFY_DONE;
}

/*
 *	Kernel Interfaces
 */

static struct file_operations advwdt_fops = {
	.owner		= THIS_MODULE,
	.llseek		= no_llseek,
	.write		= advwdt_write,
	.ioctl		= advwdt_ioctl,
	.open		= advwdt_open,
	.release	= advwdt_close,
};

static struct miscdevice advwdt_miscdev = {
	.minor = WATCHDOG_MINOR,
	.name = "watchdog",
	.fops = &advwdt_fops,
};

/*
 *	The WDT needs to learn about soft shutdowns in order to
 *	turn the timebomb registers off.
 */

static struct notifier_block advwdt_notifier = {
	.notifier_call = advwdt_notify_sys,
};

static int __init
advwdt_init(void)
{
	int ret;

	printk(KERN_INFO "WDT driver for Advantech single board computer initialising.\n");

	if (timeout < 1 || timeout > 63) {
		timeout = WATCHDOG_TIMEOUT;
		printk (KERN_INFO PFX "timeout value must be 1<=x<=63, using %d\n",
			timeout);
	}

	if (wdt_stop != wdt_start) {
		if (!request_region(wdt_stop, 1, WATCHDOG_NAME)) {
			printk (KERN_ERR PFX "I/O address 0x%04x already in use\n",
				wdt_stop);
			ret = -EIO;
			goto out;
		}
	}

	if (!request_region(wdt_start, 1, WATCHDOG_NAME)) {
		printk (KERN_ERR PFX "I/O address 0x%04x already in use\n",
			wdt_start);
		ret = -EIO;
		goto unreg_stop;
	}

	ret = register_reboot_notifier(&advwdt_notifier);
	if (ret != 0) {
		printk (KERN_ERR PFX "cannot register reboot notifier (err=%d)\n",
			ret);
		goto unreg_regions;
	}

	ret = misc_register(&advwdt_miscdev);
	if (ret != 0) {
		printk (KERN_ERR PFX "cannot register miscdev on minor=%d (err=%d)\n",
			WATCHDOG_MINOR, ret);
		goto unreg_reboot;
	}

	printk (KERN_INFO PFX "initialized. timeout=%d sec (nowayout=%d)\n",
		timeout, nowayout);

out:
	return ret;
unreg_reboot:
	unregister_reboot_notifier(&advwdt_notifier);
unreg_regions:
	release_region(wdt_start, 1);
unreg_stop:
	if (wdt_stop != wdt_start)
		release_region(wdt_stop, 1);
	goto out;
}

static void __exit
advwdt_exit(void)
{
	misc_deregister(&advwdt_miscdev);
	unregister_reboot_notifier(&advwdt_notifier);
	if(wdt_stop != wdt_start)
		release_region(wdt_stop,1);
	release_region(wdt_start,1);
}

module_init(advwdt_init);
module_exit(advwdt_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Marek Michalkiewicz <marekm@linux.org.pl>");
MODULE_DESCRIPTION("Advantech Single Board Computer WDT driver");
MODULE_ALIAS_MISCDEV(WATCHDOG_MINOR);

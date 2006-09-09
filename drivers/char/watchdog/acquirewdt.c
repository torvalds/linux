/*
 *	Acquire Single Board Computer Watchdog Timer driver
 *
 *      Based on wdt.c. Original copyright messages:
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
 *      14-Dec-2001 Matt Domsch <Matt_Domsch@dell.com>
 *          Added nowayout module option to override CONFIG_WATCHDOG_NOWAYOUT
 *          Can't add timeout - driver doesn't allow changing value
 */

/*
 *	Theory of Operation:
 *		The Watch-Dog Timer is provided to ensure that standalone
 *		Systems can always recover from catastrophic conditions that
 *		caused the CPU to crash. This condition may have occured by
 *		external EMI or a software bug. When the CPU stops working
 *		correctly, hardware on the board will either perform a hardware
 *		reset (cold boot) or a non-maskable interrupt (NMI) to bring the
 *		system back to a known state.
 *
 *		The Watch-Dog Timer is controlled by two I/O Ports.
 *		  443 hex	- Read	- Enable or refresh the Watch-Dog Timer
 *		  043 hex	- Read	- Disable the Watch-Dog Timer
 *
 *		To enable the Watch-Dog Timer, a read from I/O port 443h must
 *		be performed. This will enable and activate the countdown timer
 *		which will eventually time out and either reset the CPU or cause
 *		an NMI depending on the setting of a jumper. To ensure that this
 *		reset condition does not occur, the Watch-Dog Timer must be
 *		periodically refreshed by reading the same I/O port 443h.
 *		The Watch-Dog Timer is disabled by reading I/O port 043h.
 *
 *		The Watch-Dog Timer Time-Out Period is set via jumpers.
 *		It can be 1, 2, 10, 20, 110 or 220 seconds.
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

#define WATCHDOG_NAME "Acquire WDT"
#define PFX WATCHDOG_NAME ": "
#define WATCHDOG_HEARTBEAT 0	/* There is no way to see what the correct time-out period is */

static unsigned long acq_is_open;
static char expect_close;

/*
 *	You must set these - there is no sane way to probe for this board.
 */

static int wdt_stop = 0x43;
module_param(wdt_stop, int, 0);
MODULE_PARM_DESC(wdt_stop, "Acquire WDT 'stop' io port (default 0x43)");

static int wdt_start = 0x443;
module_param(wdt_start, int, 0);
MODULE_PARM_DESC(wdt_start, "Acquire WDT 'start' io port (default 0x443)");

static int nowayout = WATCHDOG_NOWAYOUT;
module_param(nowayout, int, 0);
MODULE_PARM_DESC(nowayout, "Watchdog cannot be stopped once started (default=CONFIG_WATCHDOG_NOWAYOUT)");

/*
 *	Kernel methods.
 */

static void acq_keepalive(void)
{
	/* Write a watchdog value */
	inb_p(wdt_start);
}

static void acq_stop(void)
{
	/* Turn the card off */
	inb_p(wdt_stop);
}

/*
 *	/dev/watchdog handling.
 */

static ssize_t acq_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos)
{
	/* See if we got the magic character 'V' and reload the timer */
	if(count) {
		if (!nowayout) {
			size_t i;

			/* note: just in case someone wrote the magic character
			 * five months ago... */
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
		acq_keepalive();
	}
	return count;
}

static int acq_ioctl(struct inode *inode, struct file *file, unsigned int cmd,
	unsigned long arg)
{
	int options, retval = -EINVAL;
	void __user *argp = (void __user *)arg;
	int __user *p = argp;
	static struct watchdog_info ident =
	{
		.options = WDIOF_KEEPALIVEPING | WDIOF_MAGICCLOSE,
		.firmware_version = 1,
		.identity = "Acquire WDT",
	};

	switch(cmd)
	{
	case WDIOC_GETSUPPORT:
	  return copy_to_user(argp, &ident, sizeof(ident)) ? -EFAULT : 0;

	case WDIOC_GETSTATUS:
	case WDIOC_GETBOOTSTATUS:
	  return put_user(0, p);

	case WDIOC_KEEPALIVE:
	  acq_keepalive();
	  return 0;

	case WDIOC_GETTIMEOUT:
	  return put_user(WATCHDOG_HEARTBEAT, p);

	case WDIOC_SETOPTIONS:
	{
	    if (get_user(options, p))
	      return -EFAULT;

	    if (options & WDIOS_DISABLECARD)
	    {
	      acq_stop();
	      retval = 0;
	    }

	    if (options & WDIOS_ENABLECARD)
	    {
	      acq_keepalive();
	      retval = 0;
	    }

	    return retval;
	}

	default:
	  return -ENOTTY;
	}
}

static int acq_open(struct inode *inode, struct file *file)
{
	if (test_and_set_bit(0, &acq_is_open))
		return -EBUSY;

	if (nowayout)
		__module_get(THIS_MODULE);

	/* Activate */
	acq_keepalive();
	return nonseekable_open(inode, file);
}

static int acq_close(struct inode *inode, struct file *file)
{
	if (expect_close == 42) {
		acq_stop();
	} else {
		printk(KERN_CRIT PFX "Unexpected close, not stopping watchdog!\n");
		acq_keepalive();
	}
	clear_bit(0, &acq_is_open);
	expect_close = 0;
	return 0;
}

/*
 *	Notifier for system down
 */

static int acq_notify_sys(struct notifier_block *this, unsigned long code,
	void *unused)
{
	if(code==SYS_DOWN || code==SYS_HALT) {
		/* Turn the WDT off */
		acq_stop();
	}
	return NOTIFY_DONE;
}

/*
 *	Kernel Interfaces
 */

static const struct file_operations acq_fops = {
	.owner		= THIS_MODULE,
	.llseek		= no_llseek,
	.write		= acq_write,
	.ioctl		= acq_ioctl,
	.open		= acq_open,
	.release	= acq_close,
};

static struct miscdevice acq_miscdev=
{
	.minor = WATCHDOG_MINOR,
	.name = "watchdog",
	.fops = &acq_fops,
};

/*
 *	The WDT card needs to learn about soft shutdowns in order to
 *	turn the timebomb registers off.
 */

static struct notifier_block acq_notifier =
{
	.notifier_call = acq_notify_sys,
};

static int __init acq_init(void)
{
	int ret;

	printk(KERN_INFO "WDT driver for Acquire single board computer initialising.\n");

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

	ret = register_reboot_notifier(&acq_notifier);
	if (ret != 0) {
		printk (KERN_ERR PFX "cannot register reboot notifier (err=%d)\n",
			ret);
		goto unreg_regions;
	}

	ret = misc_register(&acq_miscdev);
	if (ret != 0) {
		printk (KERN_ERR PFX "cannot register miscdev on minor=%d (err=%d)\n",
			WATCHDOG_MINOR, ret);
		goto unreg_reboot;
	}

	printk (KERN_INFO PFX "initialized. (nowayout=%d)\n",
		nowayout);

	return 0;

unreg_reboot:
	unregister_reboot_notifier(&acq_notifier);
unreg_regions:
	release_region(wdt_start, 1);
unreg_stop:
	if (wdt_stop != wdt_start)
		release_region(wdt_stop, 1);
out:
	return ret;
}

static void __exit acq_exit(void)
{
	misc_deregister(&acq_miscdev);
	unregister_reboot_notifier(&acq_notifier);
	if(wdt_stop != wdt_start)
		release_region(wdt_stop,1);
	release_region(wdt_start,1);
}

module_init(acq_init);
module_exit(acq_exit);

MODULE_AUTHOR("David Woodhouse");
MODULE_DESCRIPTION("Acquire Inc. Single Board Computer Watchdog Timer driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS_MISCDEV(WATCHDOG_MINOR);

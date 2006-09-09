/*
 *	IB700 Single Board Computer WDT driver
 *
 *	(c) Copyright 2001 Charles Howes <chowes@vsol.net>
 *
 *      Based on advantechwdt.c which is based on acquirewdt.c which
 *       is based on wdt.c.
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
 *      14-Dec-2001 Matt Domsch <Matt_Domsch@dell.com>
 *           Added nowayout module option to override CONFIG_WATCHDOG_NOWAYOUT
 *           Added timeout module option to override default
 *
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/miscdevice.h>
#include <linux/watchdog.h>
#include <linux/ioport.h>
#include <linux/notifier.h>
#include <linux/fs.h>
#include <linux/reboot.h>
#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/moduleparam.h>

#include <asm/io.h>
#include <asm/uaccess.h>
#include <asm/system.h>

static unsigned long ibwdt_is_open;
static spinlock_t ibwdt_lock;
static char expect_close;

#define PFX "ib700wdt: "

/*
 *
 * Watchdog Timer Configuration
 *
 * The function of the watchdog timer is to reset the system
 * automatically and is defined at I/O port 0443H.  To enable the
 * watchdog timer and allow the system to reset, write I/O port 0443H.
 * To disable the timer, write I/O port 0441H for the system to stop the
 * watchdog function.  The timer has a tolerance of 20% for its
 * intervals.
 *
 * The following describes how the timer should be programmed.
 *
 * Enabling Watchdog:
 * MOV AX,000FH (Choose the values from 0 to F)
 * MOV DX,0443H
 * OUT DX,AX
 *
 * Disabling Watchdog:
 * MOV AX,000FH (Any value is fine.)
 * MOV DX,0441H
 * OUT DX,AX
 *
 * Watchdog timer control table:
 * Level   Value  Time/sec | Level Value Time/sec
 *   1       F       0     |   9     7      16
 *   2       E       2     |   10    6      18
 *   3       D       4     |   11    5      20
 *   4       C       6     |   12    4      22
 *   5       B       8     |   13    3      24
 *   6       A       10    |   14    2      26
 *   7       9       12    |   15    1      28
 *   8       8       14    |   16    0      30
 *
 */

static int wd_times[] = {
	30,	/* 0x0 */
	28,	/* 0x1 */
	26,	/* 0x2 */
	24,	/* 0x3 */
	22,	/* 0x4 */
	20,	/* 0x5 */
	18,	/* 0x6 */
	16,	/* 0x7 */
	14,	/* 0x8 */
	12,	/* 0x9 */
	10,	/* 0xA */
	8,	/* 0xB */
	6,	/* 0xC */
	4,	/* 0xD */
	2,	/* 0xE */
	0,	/* 0xF */
};

#define WDT_STOP 0x441
#define WDT_START 0x443

/* Default timeout */
#define WD_TIMO 0		/* 30 seconds +/- 20%, from table */

static int wd_margin = WD_TIMO;

static int nowayout = WATCHDOG_NOWAYOUT;
module_param(nowayout, int, 0);
MODULE_PARM_DESC(nowayout, "Watchdog cannot be stopped once started (default=CONFIG_WATCHDOG_NOWAYOUT)");


/*
 *	Kernel methods.
 */

static void
ibwdt_ping(void)
{
	/* Write a watchdog value */
	outb_p(wd_margin, WDT_START);
}

static ssize_t
ibwdt_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos)
{
	if (count) {
		if (!nowayout) {
			size_t i;

			/* In case it was set long ago */
			expect_close = 0;

			for (i = 0; i != count; i++) {
				char c;
				if (get_user(c, buf + i))
					return -EFAULT;
				if (c == 'V')
					expect_close = 42;
			}
		}
		ibwdt_ping();
	}
	return count;
}

static int
ibwdt_ioctl(struct inode *inode, struct file *file, unsigned int cmd,
	  unsigned long arg)
{
	int i, new_margin;
	void __user *argp = (void __user *)arg;
	int __user *p = argp;

	static struct watchdog_info ident = {
		.options = WDIOF_KEEPALIVEPING | WDIOF_SETTIMEOUT | WDIOF_MAGICCLOSE,
		.firmware_version = 1,
		.identity = "IB700 WDT",
	};

	switch (cmd) {
	case WDIOC_GETSUPPORT:
	  if (copy_to_user(argp, &ident, sizeof(ident)))
	    return -EFAULT;
	  break;

	case WDIOC_GETSTATUS:
	  return put_user(0, p);

	case WDIOC_KEEPALIVE:
	  ibwdt_ping();
	  break;

	case WDIOC_SETTIMEOUT:
	  if (get_user(new_margin, p))
		  return -EFAULT;
	  if ((new_margin < 0) || (new_margin > 30))
		  return -EINVAL;
	  for (i = 0x0F; i > -1; i--)
		  if (wd_times[i] > new_margin)
			  break;
	  wd_margin = i;
	  ibwdt_ping();
	  /* Fall */

	case WDIOC_GETTIMEOUT:
	  return put_user(wd_times[wd_margin], p);
	  break;

	default:
	  return -ENOTTY;
	}
	return 0;
}

static int
ibwdt_open(struct inode *inode, struct file *file)
{
	spin_lock(&ibwdt_lock);
	if (test_and_set_bit(0, &ibwdt_is_open)) {
		spin_unlock(&ibwdt_lock);
		return -EBUSY;
	}
	if (nowayout)
		__module_get(THIS_MODULE);

	/* Activate */
	ibwdt_ping();
	spin_unlock(&ibwdt_lock);
	return nonseekable_open(inode, file);
}

static int
ibwdt_close(struct inode *inode, struct file *file)
{
	spin_lock(&ibwdt_lock);
	if (expect_close == 42)
		outb_p(0, WDT_STOP);
	else
		printk(KERN_CRIT PFX "WDT device closed unexpectedly.  WDT will not stop!\n");

	clear_bit(0, &ibwdt_is_open);
	expect_close = 0;
	spin_unlock(&ibwdt_lock);
	return 0;
}

/*
 *	Notifier for system down
 */

static int
ibwdt_notify_sys(struct notifier_block *this, unsigned long code,
	void *unused)
{
	if (code == SYS_DOWN || code == SYS_HALT) {
		/* Turn the WDT off */
		outb_p(0, WDT_STOP);
	}
	return NOTIFY_DONE;
}

/*
 *	Kernel Interfaces
 */

static const struct file_operations ibwdt_fops = {
	.owner		= THIS_MODULE,
	.llseek		= no_llseek,
	.write		= ibwdt_write,
	.ioctl		= ibwdt_ioctl,
	.open		= ibwdt_open,
	.release	= ibwdt_close,
};

static struct miscdevice ibwdt_miscdev = {
	.minor = WATCHDOG_MINOR,
	.name = "watchdog",
	.fops = &ibwdt_fops,
};

/*
 *	The WDT needs to learn about soft shutdowns in order to
 *	turn the timebomb registers off.
 */

static struct notifier_block ibwdt_notifier = {
	.notifier_call = ibwdt_notify_sys,
};

static int __init ibwdt_init(void)
{
	int res;

	printk(KERN_INFO PFX "WDT driver for IB700 single board computer initialising.\n");

	spin_lock_init(&ibwdt_lock);
	res = misc_register(&ibwdt_miscdev);
	if (res) {
		printk (KERN_ERR PFX "failed to register misc device\n");
		goto out_nomisc;
	}

#if WDT_START != WDT_STOP
	if (!request_region(WDT_STOP, 1, "IB700 WDT")) {
		printk (KERN_ERR PFX "STOP method I/O %X is not available.\n", WDT_STOP);
		res = -EIO;
		goto out_nostopreg;
	}
#endif

	if (!request_region(WDT_START, 1, "IB700 WDT")) {
		printk (KERN_ERR PFX "START method I/O %X is not available.\n", WDT_START);
		res = -EIO;
		goto out_nostartreg;
	}
	res = register_reboot_notifier(&ibwdt_notifier);
	if (res) {
		printk (KERN_ERR PFX "Failed to register reboot notifier.\n");
		goto out_noreboot;
	}
	return 0;

out_noreboot:
	release_region(WDT_START, 1);
out_nostartreg:
#if WDT_START != WDT_STOP
	release_region(WDT_STOP, 1);
#endif
out_nostopreg:
	misc_deregister(&ibwdt_miscdev);
out_nomisc:
	return res;
}

static void __exit
ibwdt_exit(void)
{
	misc_deregister(&ibwdt_miscdev);
	unregister_reboot_notifier(&ibwdt_notifier);
#if WDT_START != WDT_STOP
	release_region(WDT_STOP,1);
#endif
	release_region(WDT_START,1);
}

module_init(ibwdt_init);
module_exit(ibwdt_exit);

MODULE_AUTHOR("Charles Howes <chowes@vsol.net>");
MODULE_DESCRIPTION("IB700 SBC watchdog driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS_MISCDEV(WATCHDOG_MINOR);

/* end of ib700wdt.c */

/*
 *	IndyDog	0.3	A Hardware Watchdog Device for SGI IP22
 *
 *	(c) Copyright 2002 Guido Guenther <agx@sigxcpu.org>, All Rights Reserved.
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 *
 *	based on softdog.c by Alan Cox <alan@redhat.com>
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/miscdevice.h>
#include <linux/watchdog.h>
#include <linux/notifier.h>
#include <linux/reboot.h>
#include <linux/init.h>
#include <asm/uaccess.h>
#include <asm/sgi/mc.h>

#define PFX "indydog: "
static int indydog_alive;

#define WATCHDOG_TIMEOUT 30		/* 30 sec default timeout */

static int nowayout = WATCHDOG_NOWAYOUT;
module_param(nowayout, int, 0);
MODULE_PARM_DESC(nowayout, "Watchdog cannot be stopped once started (default=CONFIG_WATCHDOG_NOWAYOUT)");

static void indydog_start(void)
{
	u32 mc_ctrl0 = sgimc->cpuctrl0;

	mc_ctrl0 = sgimc->cpuctrl0 | SGIMC_CCTRL0_WDOG;
	sgimc->cpuctrl0 = mc_ctrl0;
}

static void indydog_stop(void)
{
	u32 mc_ctrl0 = sgimc->cpuctrl0;

	mc_ctrl0 &= ~SGIMC_CCTRL0_WDOG;
	sgimc->cpuctrl0 = mc_ctrl0;

	printk(KERN_INFO PFX "Stopped watchdog timer.\n");
}

static void indydog_ping(void)
{
	sgimc->watchdogt = 0;
}

/*
 *	Allow only one person to hold it open
 */
static int indydog_open(struct inode *inode, struct file *file)
{
	if (indydog_alive)
		return -EBUSY;

	if (nowayout)
		__module_get(THIS_MODULE);

	/* Activate timer */
	indydog_start();
	indydog_ping();

	indydog_alive = 1;
	printk(KERN_INFO "Started watchdog timer.\n");

	return nonseekable_open(inode, file);
}

static int indydog_release(struct inode *inode, struct file *file)
{
	/* Shut off the timer.
	 * Lock it in if it's a module and we defined ...NOWAYOUT */
	if (!nowayout)
		indydog_stop();		/* Turn the WDT off */

	indydog_alive = 0;

	return 0;
}

static ssize_t indydog_write(struct file *file, const char *data, size_t len, loff_t *ppos)
{
	/* Refresh the timer. */
	if (len) {
		indydog_ping();
	}
	return len;
}

static int indydog_ioctl(struct inode *inode, struct file *file,
	unsigned int cmd, unsigned long arg)
{
	int options, retval = -EINVAL;
	static struct watchdog_info ident = {
		.options		= WDIOF_KEEPALIVEPING |
					  WDIOF_MAGICCLOSE,
		.firmware_version	= 0,
		.identity		= "Hardware Watchdog for SGI IP22",
	};

	switch (cmd) {
		default:
			return -ENOTTY;
		case WDIOC_GETSUPPORT:
			if (copy_to_user((struct watchdog_info *)arg,
					 &ident, sizeof(ident)))
				return -EFAULT;
			return 0;
		case WDIOC_GETSTATUS:
		case WDIOC_GETBOOTSTATUS:
			return put_user(0,(int *)arg);
		case WDIOC_KEEPALIVE:
			indydog_ping();
			return 0;
		case WDIOC_GETTIMEOUT:
			return put_user(WATCHDOG_TIMEOUT,(int *)arg);
		case WDIOC_SETOPTIONS:
		{
			if (get_user(options, (int *)arg))
				return -EFAULT;

			if (options & WDIOS_DISABLECARD) {
				indydog_stop();
				retval = 0;
			}

			if (options & WDIOS_ENABLECARD) {
				indydog_start();
				retval = 0;
			}

			return retval;
		}
	}
}

static int indydog_notify_sys(struct notifier_block *this, unsigned long code, void *unused)
{
	if (code == SYS_DOWN || code == SYS_HALT)
		indydog_stop();		/* Turn the WDT off */

	return NOTIFY_DONE;
}

static const struct file_operations indydog_fops = {
	.owner		= THIS_MODULE,
	.llseek		= no_llseek,
	.write		= indydog_write,
	.ioctl		= indydog_ioctl,
	.open		= indydog_open,
	.release	= indydog_release,
};

static struct miscdevice indydog_miscdev = {
	.minor		= WATCHDOG_MINOR,
	.name		= "watchdog",
	.fops		= &indydog_fops,
};

static struct notifier_block indydog_notifier = {
	.notifier_call = indydog_notify_sys,
};

static char banner[] __initdata =
	KERN_INFO PFX "Hardware Watchdog Timer for SGI IP22: 0.3\n";

static int __init watchdog_init(void)
{
	int ret;

	ret = register_reboot_notifier(&indydog_notifier);
	if (ret) {
		printk(KERN_ERR PFX "cannot register reboot notifier (err=%d)\n",
			ret);
		return ret;
	}

	ret = misc_register(&indydog_miscdev);
	if (ret) {
		printk(KERN_ERR PFX "cannot register miscdev on minor=%d (err=%d)\n",
			WATCHDOG_MINOR, ret);
		unregister_reboot_notifier(&indydog_notifier);
		return ret;
	}

	printk(banner);

	return 0;
}

static void __exit watchdog_exit(void)
{
	misc_deregister(&indydog_miscdev);
	unregister_reboot_notifier(&indydog_notifier);
}

module_init(watchdog_init);
module_exit(watchdog_exit);

MODULE_AUTHOR("Guido Guenther <agx@sigxcpu.org>");
MODULE_DESCRIPTION("Hardware Watchdog Device for SGI IP22");
MODULE_LICENSE("GPL");
MODULE_ALIAS_MISCDEV(WATCHDOG_MINOR);

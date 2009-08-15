/*
 *  Watchdog driver for Broadcom BCM47XX
 *
 *  Copyright (C) 2008 Aleksandar Radovanovic <biblbroks@sezampro.rs>
 *  Copyright (C) 2009 Matthieu CASTET <castet.matthieu@free.fr>
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 */

#include <linux/bitops.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/reboot.h>
#include <linux/types.h>
#include <linux/uaccess.h>
#include <linux/watchdog.h>
#include <linux/timer.h>
#include <linux/jiffies.h>
#include <linux/ssb/ssb_embedded.h>
#include <asm/mach-bcm47xx/bcm47xx.h>

#define DRV_NAME		"bcm47xx_wdt"

#define WDT_DEFAULT_TIME	30	/* seconds */
#define WDT_MAX_TIME		255	/* seconds */

static int wdt_time = WDT_DEFAULT_TIME;
static int nowayout = WATCHDOG_NOWAYOUT;

module_param(wdt_time, int, 0);
MODULE_PARM_DESC(wdt_time, "Watchdog time in seconds. (default="
				__MODULE_STRING(WDT_DEFAULT_TIME) ")");

#ifdef CONFIG_WATCHDOG_NOWAYOUT
module_param(nowayout, int, 0);
MODULE_PARM_DESC(nowayout,
		"Watchdog cannot be stopped once started (default="
				__MODULE_STRING(WATCHDOG_NOWAYOUT) ")");
#endif

static unsigned long bcm47xx_wdt_busy;
static char expect_release;
static struct timer_list wdt_timer;
static atomic_t ticks;

static inline void bcm47xx_wdt_hw_start(void)
{
	/* this is 2,5s on 100Mhz clock  and 2s on 133 Mhz */
	ssb_watchdog_timer_set(&ssb_bcm47xx, 0xfffffff);
}

static inline int bcm47xx_wdt_hw_stop(void)
{
	return ssb_watchdog_timer_set(&ssb_bcm47xx, 0);
}

static void bcm47xx_timer_tick(unsigned long unused)
{
	if (!atomic_dec_and_test(&ticks)) {
		bcm47xx_wdt_hw_start();
		mod_timer(&wdt_timer, jiffies + HZ);
	} else {
		printk(KERN_CRIT DRV_NAME "Watchdog will fire soon!!!\n");
	}
}

static inline void bcm47xx_wdt_pet(void)
{
	atomic_set(&ticks, wdt_time);
}

static void bcm47xx_wdt_start(void)
{
	bcm47xx_wdt_pet();
	bcm47xx_timer_tick(0);
}

static void bcm47xx_wdt_pause(void)
{
	del_timer_sync(&wdt_timer);
	bcm47xx_wdt_hw_stop();
}

static void bcm47xx_wdt_stop(void)
{
	bcm47xx_wdt_pause();
}

static int bcm47xx_wdt_settimeout(int new_time)
{
	if ((new_time <= 0) || (new_time > WDT_MAX_TIME))
		return -EINVAL;

	wdt_time = new_time;
	return 0;
}

static int bcm47xx_wdt_open(struct inode *inode, struct file *file)
{
	if (test_and_set_bit(0, &bcm47xx_wdt_busy))
		return -EBUSY;

	bcm47xx_wdt_start();
	return nonseekable_open(inode, file);
}

static int bcm47xx_wdt_release(struct inode *inode, struct file *file)
{
	if (expect_release == 42) {
		bcm47xx_wdt_stop();
	} else {
		printk(KERN_CRIT DRV_NAME
			": Unexpected close, not stopping watchdog!\n");
		bcm47xx_wdt_start();
	}

	clear_bit(0, &bcm47xx_wdt_busy);
	expect_release = 0;
	return 0;
}

static ssize_t bcm47xx_wdt_write(struct file *file, const char __user *data,
				size_t len, loff_t *ppos)
{
	if (len) {
		if (!nowayout) {
			size_t i;

			expect_release = 0;

			for (i = 0; i != len; i++) {
				char c;
				if (get_user(c, data + i))
					return -EFAULT;
				if (c == 'V')
					expect_release = 42;
			}
		}
		bcm47xx_wdt_pet();
	}
	return len;
}

static struct watchdog_info bcm47xx_wdt_info = {
	.identity 	= DRV_NAME,
	.options 	= WDIOF_SETTIMEOUT |
				WDIOF_KEEPALIVEPING |
				WDIOF_MAGICCLOSE,
};

static long bcm47xx_wdt_ioctl(struct file *file,
					unsigned int cmd, unsigned long arg)
{
	void __user *argp = (void __user *)arg;
	int __user *p = argp;
	int new_value, retval = -EINVAL;;

	switch (cmd) {
	case WDIOC_GETSUPPORT:
		return copy_to_user(argp, &bcm47xx_wdt_info,
				sizeof(bcm47xx_wdt_info)) ? -EFAULT : 0;

	case WDIOC_GETSTATUS:
	case WDIOC_GETBOOTSTATUS:
		return put_user(0, p);

	case WDIOC_SETOPTIONS:
		if (get_user(new_value, p))
			return -EFAULT;

		if (new_value & WDIOS_DISABLECARD) {
			bcm47xx_wdt_stop();
			retval = 0;
		}

		if (new_value & WDIOS_ENABLECARD) {
			bcm47xx_wdt_start();
			retval = 0;
		}

		return retval;

	case WDIOC_KEEPALIVE:
		bcm47xx_wdt_pet();
		return 0;

	case WDIOC_SETTIMEOUT:
		if (get_user(new_value, p))
			return -EFAULT;

		if (bcm47xx_wdt_settimeout(new_value))
			return -EINVAL;

		bcm47xx_wdt_pet();

	case WDIOC_GETTIMEOUT:
		return put_user(wdt_time, p);

	default:
		return -ENOTTY;
	}
}

static int bcm47xx_wdt_notify_sys(struct notifier_block *this,
	unsigned long code, void *unused)
{
	if (code == SYS_DOWN || code == SYS_HALT)
		bcm47xx_wdt_stop();
	return NOTIFY_DONE;
}

static const struct file_operations bcm47xx_wdt_fops = {
	.owner		= THIS_MODULE,
	.llseek		= no_llseek,
	.unlocked_ioctl	= bcm47xx_wdt_ioctl,
	.open		= bcm47xx_wdt_open,
	.release	= bcm47xx_wdt_release,
	.write		= bcm47xx_wdt_write,
};

static struct miscdevice bcm47xx_wdt_miscdev = {
	.minor		= WATCHDOG_MINOR,
	.name		= "watchdog",
	.fops		= &bcm47xx_wdt_fops,
};

static struct notifier_block bcm47xx_wdt_notifier = {
	.notifier_call = bcm47xx_wdt_notify_sys,
};

static int __init bcm47xx_wdt_init(void)
{
	int ret;

	if (bcm47xx_wdt_hw_stop() < 0)
		return -ENODEV;

	setup_timer(&wdt_timer, bcm47xx_timer_tick, 0L);

	if (bcm47xx_wdt_settimeout(wdt_time)) {
		bcm47xx_wdt_settimeout(WDT_DEFAULT_TIME);
		printk(KERN_INFO DRV_NAME ": "
			"wdt_time value must be 0 < wdt_time < %d, using %d\n",
			(WDT_MAX_TIME + 1), wdt_time);
	}

	ret = register_reboot_notifier(&bcm47xx_wdt_notifier);
	if (ret)
		return ret;

	ret = misc_register(&bcm47xx_wdt_miscdev);
	if (ret) {
		unregister_reboot_notifier(&bcm47xx_wdt_notifier);
		return ret;
	}

	printk(KERN_INFO "BCM47xx Watchdog Timer enabled (%d seconds%s)\n",
				wdt_time, nowayout ? ", nowayout" : "");
	return 0;
}

static void __exit bcm47xx_wdt_exit(void)
{
	if (!nowayout)
		bcm47xx_wdt_stop();

	misc_deregister(&bcm47xx_wdt_miscdev);

	unregister_reboot_notifier(&bcm47xx_wdt_notifier);
}

module_init(bcm47xx_wdt_init);
module_exit(bcm47xx_wdt_exit);

MODULE_AUTHOR("Aleksandar Radovanovic");
MODULE_DESCRIPTION("Watchdog driver for Broadcom BCM47xx");
MODULE_LICENSE("GPL");
MODULE_ALIAS_MISCDEV(WATCHDOG_MINOR);

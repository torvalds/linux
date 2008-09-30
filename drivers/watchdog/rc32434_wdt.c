/*
 *  IDT Interprise 79RC32434 watchdog driver
 *
 *  Copyright (C) 2006, Ondrej Zajicek <santiago@crfreenet.org>
 *  Copyright (C) 2008, Florian Fainelli <florian@openwrt.org>
 *
 *  based on
 *  SoftDog 0.05:	A Software Watchdog Device
 *
 *  (c) Copyright 1996 Alan Cox <alan@redhat.com>, All Rights Reserved.
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 *
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/miscdevice.h>
#include <linux/watchdog.h>
#include <linux/reboot.h>
#include <linux/smp_lock.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/uaccess.h>

#include <asm/bootinfo.h>
#include <asm/time.h>
#include <asm/mach-rc32434/integ.h>

#define MAX_TIMEOUT			20
#define RC32434_WDT_INTERVAL		(15 * HZ)

#define VERSION "0.2"

static struct {
	struct completion stop;
	int running;
	struct timer_list timer;
	int queue;
	int default_ticks;
	unsigned long inuse;
} rc32434_wdt_device;

static struct integ __iomem *wdt_reg;
static int ticks = 100 * HZ;

static int expect_close;
static int timeout;

static int nowayout = WATCHDOG_NOWAYOUT;
module_param(nowayout, int, 0);
MODULE_PARM_DESC(nowayout, "Watchdog cannot be stopped once started (default="
	__MODULE_STRING(WATCHDOG_NOWAYOUT) ")");


static void rc32434_wdt_start(void)
{
	u32 val;

	if (!rc32434_wdt_device.inuse) {
		writel(0, &wdt_reg->wtcount);

		val = RC32434_ERR_WRE;
		writel(readl(&wdt_reg->errcs) | val, &wdt_reg->errcs);

		val = RC32434_WTC_EN;
		writel(readl(&wdt_reg->wtc) | val, &wdt_reg->wtc);
	}
	rc32434_wdt_device.running++;
}

static void rc32434_wdt_stop(void)
{
	u32 val;

	if (rc32434_wdt_device.running) {

		val = ~RC32434_WTC_EN;
		writel(readl(&wdt_reg->wtc) & val, &wdt_reg->wtc);

		val = ~RC32434_ERR_WRE;
		writel(readl(&wdt_reg->errcs) & val, &wdt_reg->errcs);

		rc32434_wdt_device.running = 0;
	}
}

static void rc32434_wdt_set(int new_timeout)
{
	u32 cmp = new_timeout * HZ;
	u32 state, val;

	timeout = new_timeout;
	/*
	 * store and disable WTC
	 */
	state = (u32)(readl(&wdt_reg->wtc) & RC32434_WTC_EN);
	val = ~RC32434_WTC_EN;
	writel(readl(&wdt_reg->wtc) & val, &wdt_reg->wtc);

	writel(0, &wdt_reg->wtcount);
	writel(cmp, &wdt_reg->wtcompare);

	/*
	 * restore WTC
	 */

	writel(readl(&wdt_reg->wtc) | state, &wdt_reg);
}

static void rc32434_wdt_reset(void)
{
	ticks = rc32434_wdt_device.default_ticks;
}

static void rc32434_wdt_update(unsigned long unused)
{
	if (rc32434_wdt_device.running)
		ticks--;

	writel(0, &wdt_reg->wtcount);

	if (rc32434_wdt_device.queue && ticks)
		mod_timer(&rc32434_wdt_device.timer,
			jiffies + RC32434_WDT_INTERVAL);
	else
		complete(&rc32434_wdt_device.stop);
}

static int rc32434_wdt_open(struct inode *inode, struct file *file)
{
	if (test_and_set_bit(0, &rc32434_wdt_device.inuse))
		return -EBUSY;

	if (nowayout)
		__module_get(THIS_MODULE);

	return nonseekable_open(inode, file);
}

static int rc32434_wdt_release(struct inode *inode, struct file *file)
{
	if (expect_close && nowayout == 0) {
		rc32434_wdt_stop();
		printk(KERN_INFO KBUILD_MODNAME ": disabling watchdog timer\n");
		module_put(THIS_MODULE);
	} else
		printk(KERN_CRIT KBUILD_MODNAME
			": device closed unexpectedly. WDT will not stop !\n");

	clear_bit(0, &rc32434_wdt_device.inuse);
	return 0;
}

static ssize_t rc32434_wdt_write(struct file *file, const char *data,
				size_t len, loff_t *ppos)
{
	if (len) {
		if (!nowayout) {
			size_t i;

			/* In case it was set long ago */
			expect_close = 0;

			for (i = 0; i != len; i++) {
				char c;
				if (get_user(c, data + i))
					return -EFAULT;
				if (c == 'V')
					expect_close = 1;
			}
		}
		rc32434_wdt_update(0);
		return len;
	}
	return 0;
}

static long rc32434_wdt_ioctl(struct file *file, unsigned int cmd,
				unsigned long arg)
{
	void __user *argp = (void __user *)arg;
	int new_timeout;
	unsigned int value;
	static struct watchdog_info ident = {
		.options =		WDIOF_SETTIMEOUT |
					WDIOF_KEEPALIVEPING |
					WDIOF_MAGICCLOSE,
		.identity =		"RC32434_WDT Watchdog",
	};
	switch (cmd) {
	case WDIOC_KEEPALIVE:
		rc32434_wdt_reset();
		break;
	case WDIOC_GETSTATUS:
	case WDIOC_GETBOOTSTATUS:
		value = readl(&wdt_reg->wtcount);
		if (copy_to_user(argp, &value, sizeof(int)))
			return -EFAULT;
		break;
	case WDIOC_GETSUPPORT:
		if (copy_to_user(argp, &ident, sizeof(ident)))
			return -EFAULT;
		break;
	case WDIOC_SETOPTIONS:
		if (copy_from_user(&value, argp, sizeof(int)))
			return -EFAULT;
		switch (value) {
		case WDIOS_ENABLECARD:
			rc32434_wdt_start();
			break;
		case WDIOS_DISABLECARD:
			rc32434_wdt_stop();
		default:
			return -EINVAL;
		}
		break;
	case WDIOC_SETTIMEOUT:
		if (copy_from_user(&new_timeout, argp, sizeof(int)))
			return -EFAULT;
		if (new_timeout < 1)
			return -EINVAL;
		if (new_timeout > MAX_TIMEOUT)
			return -EINVAL;
		rc32434_wdt_set(new_timeout);
	case WDIOC_GETTIMEOUT:
		return copy_to_user(argp, &timeout, sizeof(int));
	default:
		return -ENOTTY;
	}

	return 0;
}

static struct file_operations rc32434_wdt_fops = {
	.owner		= THIS_MODULE,
	.llseek		= no_llseek,
	.write		= rc32434_wdt_write,
	.unlocked_ioctl	= rc32434_wdt_ioctl,
	.open		= rc32434_wdt_open,
	.release	= rc32434_wdt_release,
};

static struct miscdevice rc32434_wdt_miscdev = {
	.minor	= WATCHDOG_MINOR,
	.name	= "watchdog",
	.fops	= &rc32434_wdt_fops,
};

static char banner[] = KERN_INFO KBUILD_MODNAME
		": Watchdog Timer version " VERSION ", timer margin: %d sec\n";

static int rc32434_wdt_probe(struct platform_device *pdev)
{
	int ret;
	struct resource *r;

	r = platform_get_resource_byname(pdev, IORESOURCE_MEM, "rb500_wdt_res");
	if (!r) {
		printk(KERN_ERR KBUILD_MODNAME
			"failed to retrieve resources\n");
		return -ENODEV;
	}

	wdt_reg = ioremap_nocache(r->start, r->end - r->start);
	if (!wdt_reg) {
		printk(KERN_ERR KBUILD_MODNAME
			"failed to remap I/O resources\n");
		return -ENXIO;
	}

	ret = misc_register(&rc32434_wdt_miscdev);

	if (ret < 0) {
		printk(KERN_ERR KBUILD_MODNAME
			"failed to register watchdog device\n");
		goto unmap;
	}

	init_completion(&rc32434_wdt_device.stop);
	rc32434_wdt_device.queue = 0;

	clear_bit(0, &rc32434_wdt_device.inuse);

	setup_timer(&rc32434_wdt_device.timer, rc32434_wdt_update, 0L);

	rc32434_wdt_device.default_ticks = ticks;

	rc32434_wdt_start();

	printk(banner, timeout);

	return 0;

unmap:
	iounmap(wdt_reg);
	return ret;
}

static int rc32434_wdt_remove(struct platform_device *pdev)
{
	if (rc32434_wdt_device.queue) {
		rc32434_wdt_device.queue = 0;
		wait_for_completion(&rc32434_wdt_device.stop);
	}
	misc_deregister(&rc32434_wdt_miscdev);

	iounmap(wdt_reg);

	return 0;
}

static struct platform_driver rc32434_wdt = {
	.probe	= rc32434_wdt_probe,
	.remove = rc32434_wdt_remove,
	.driver = {
		.name = "rc32434_wdt",
	}
};

static int __init rc32434_wdt_init(void)
{
	return platform_driver_register(&rc32434_wdt);
}

static void __exit rc32434_wdt_exit(void)
{
	platform_driver_unregister(&rc32434_wdt);
}

module_init(rc32434_wdt_init);
module_exit(rc32434_wdt_exit);

MODULE_AUTHOR("Ondrej Zajicek <santiago@crfreenet.org>,"
		"Florian Fainelli <florian@openwrt.org>");
MODULE_DESCRIPTION("Driver for the IDT RC32434 SoC watchdog");
MODULE_LICENSE("GPL");
MODULE_ALIAS_MISCDEV(WATCHDOG_MINOR);

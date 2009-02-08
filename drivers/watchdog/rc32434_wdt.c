/*
 *  IDT Interprise 79RC32434 watchdog driver
 *
 *  Copyright (C) 2006, Ondrej Zajicek <santiago@crfreenet.org>
 *  Copyright (C) 2008, Florian Fainelli <florian@openwrt.org>
 *
 *  based on
 *  SoftDog 0.05:	A Software Watchdog Device
 *
 *  (c) Copyright 1996 Alan Cox <alan@lxorguk.ukuu.org.uk>,
 *					All Rights Reserved.
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

#define VERSION "0.4"

static struct {
	unsigned long inuse;
} rc32434_wdt_device;

static struct integ __iomem *wdt_reg;

static int expect_close;

/* Board internal clock speed in Hz,
 * the watchdog timer ticks at. */
extern unsigned int idt_cpu_freq;

/* translate wtcompare value to seconds and vice versa */
#define WTCOMP2SEC(x)	(x / idt_cpu_freq)
#define SEC2WTCOMP(x)	(x * idt_cpu_freq)

/* Use a default timeout of 20s. This should be
 * safe for CPU clock speeds up to 400MHz, as
 * ((2 ^ 32) - 1) / (400MHz / 2) = 21s.  */
#define WATCHDOG_TIMEOUT 20

static int timeout = WATCHDOG_TIMEOUT;

static int nowayout = WATCHDOG_NOWAYOUT;
module_param(nowayout, int, 0);
MODULE_PARM_DESC(nowayout, "Watchdog cannot be stopped once started (default="
	__MODULE_STRING(WATCHDOG_NOWAYOUT) ")");

/* apply or and nand masks to data read from addr and write back */
#define SET_BITS(addr, or, nand) \
	writel((readl(&addr) | or) & ~nand, &addr)

static void rc32434_wdt_start(void)
{
	u32 or, nand;

	/* zero the counter before enabling */
	writel(0, &wdt_reg->wtcount);

	/* don't generate a non-maskable interrupt,
	 * do a warm reset instead */
	nand = 1 << RC32434_ERR_WNE;
	or = 1 << RC32434_ERR_WRE;

	/* reset the ERRCS timeout bit in case it's set */
	nand |= 1 << RC32434_ERR_WTO;

	SET_BITS(wdt_reg->errcs, or, nand);

	/* reset WTC timeout bit and enable WDT */
	nand = 1 << RC32434_WTC_TO;
	or = 1 << RC32434_WTC_EN;

	SET_BITS(wdt_reg->wtc, or, nand);
}

static void rc32434_wdt_stop(void)
{
	/* Disable WDT */
	SET_BITS(wdt_reg->wtc, 0, 1 << RC32434_WTC_EN);
}

static int rc32434_wdt_set(int new_timeout)
{
	int max_to = WTCOMP2SEC((u32)-1);

	if (new_timeout < 0 || new_timeout > max_to) {
		printk(KERN_ERR KBUILD_MODNAME
			": timeout value must be between 0 and %d",
			max_to);
		return -EINVAL;
	}
	timeout = new_timeout;
	writel(SEC2WTCOMP(timeout), &wdt_reg->wtcompare);

	return 0;
}

static void rc32434_wdt_ping(void)
{
	writel(0, &wdt_reg->wtcount);
}

static int rc32434_wdt_open(struct inode *inode, struct file *file)
{
	if (test_and_set_bit(0, &rc32434_wdt_device.inuse))
		return -EBUSY;

	if (nowayout)
		__module_get(THIS_MODULE);

	rc32434_wdt_start();
	rc32434_wdt_ping();

	return nonseekable_open(inode, file);
}

static int rc32434_wdt_release(struct inode *inode, struct file *file)
{
	if (expect_close == 42) {
		rc32434_wdt_stop();
		printk(KERN_INFO KBUILD_MODNAME ": disabling watchdog timer\n");
		module_put(THIS_MODULE);
	} else {
		printk(KERN_CRIT KBUILD_MODNAME
			": device closed unexpectedly. WDT will not stop !\n");
		rc32434_wdt_ping();
	}
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
					expect_close = 42;
			}
		}
		rc32434_wdt_ping();
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
		rc32434_wdt_ping();
		break;
	case WDIOC_GETSTATUS:
	case WDIOC_GETBOOTSTATUS:
		value = 0;
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
			break;
		default:
			return -EINVAL;
		}
		break;
	case WDIOC_SETTIMEOUT:
		if (copy_from_user(&new_timeout, argp, sizeof(int)))
			return -EFAULT;
		if (rc32434_wdt_set(new_timeout))
			return -EINVAL;
		/* Fall through */
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

static char banner[] __devinitdata = KERN_INFO KBUILD_MODNAME
		": Watchdog Timer version " VERSION ", timer margin: %d sec\n";

static int __devinit rc32434_wdt_probe(struct platform_device *pdev)
{
	int ret;
	struct resource *r;

	r = platform_get_resource_byname(pdev, IORESOURCE_MEM, "rb532_wdt_res");
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

	printk(banner, timeout);

	return 0;

unmap:
	iounmap(wdt_reg);
	return ret;
}

static int __devexit rc32434_wdt_remove(struct platform_device *pdev)
{
	misc_deregister(&rc32434_wdt_miscdev);
	iounmap(wdt_reg);
	return 0;
}

static struct platform_driver rc32434_wdt = {
	.probe	= rc32434_wdt_probe,
	.remove	= __devexit_p(rc32434_wdt_remove),
	.driver	= {
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

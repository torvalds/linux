// SPDX-License-Identifier: GPL-2.0-or-later
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
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>		/* For module specific items */
#include <linux/moduleparam.h>		/* For new moduleparam's */
#include <linux/types.h>		/* For standard types (like size_t) */
#include <linux/errno.h>		/* For the -ENODEV/... values */
#include <linux/kernel.h>		/* For printk/panic/... */
#include <linux/fs.h>			/* For file operations */
#include <linux/miscdevice.h>		/* For struct miscdevice */
#include <linux/watchdog.h>		/* For the watchdog specific items */
#include <linux/init.h>			/* For __init/__exit/... */
#include <linux/platform_device.h>	/* For platform_driver framework */
#include <linux/spinlock.h>		/* For spin_lock/spin_unlock/... */
#include <linux/uaccess.h>		/* For copy_to_user/put_user/... */
#include <linux/io.h>			/* For devm_ioremap */

#include <asm/mach-rc32434/integ.h>	/* For the Watchdog registers */

#define VERSION "1.0"

static struct {
	unsigned long inuse;
	spinlock_t io_lock;
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
module_param(timeout, int, 0);
MODULE_PARM_DESC(timeout, "Watchdog timeout value, in seconds (default="
		__MODULE_STRING(WATCHDOG_TIMEOUT) ")");

static bool nowayout = WATCHDOG_NOWAYOUT;
module_param(nowayout, bool, 0);
MODULE_PARM_DESC(nowayout, "Watchdog cannot be stopped once started (default="
	__MODULE_STRING(WATCHDOG_NOWAYOUT) ")");

/* apply or and nand masks to data read from addr and write back */
#define SET_BITS(addr, or, nand) \
	writel((readl(&addr) | or) & ~nand, &addr)

static int rc32434_wdt_set(int new_timeout)
{
	int max_to = WTCOMP2SEC((u32)-1);

	if (new_timeout < 0 || new_timeout > max_to) {
		pr_err("timeout value must be between 0 and %d\n", max_to);
		return -EINVAL;
	}
	timeout = new_timeout;
	spin_lock(&rc32434_wdt_device.io_lock);
	writel(SEC2WTCOMP(timeout), &wdt_reg->wtcompare);
	spin_unlock(&rc32434_wdt_device.io_lock);

	return 0;
}

static void rc32434_wdt_start(void)
{
	u32 or, nand;

	spin_lock(&rc32434_wdt_device.io_lock);

	/* zero the counter before enabling */
	writel(0, &wdt_reg->wtcount);

	/* don't generate a non-maskable interrupt,
	 * do a warm reset instead */
	nand = 1 << RC32434_ERR_WNE;
	or = 1 << RC32434_ERR_WRE;

	/* reset the ERRCS timeout bit in case it's set */
	nand |= 1 << RC32434_ERR_WTO;

	SET_BITS(wdt_reg->errcs, or, nand);

	/* set the timeout (either default or based on module param) */
	rc32434_wdt_set(timeout);

	/* reset WTC timeout bit and enable WDT */
	nand = 1 << RC32434_WTC_TO;
	or = 1 << RC32434_WTC_EN;

	SET_BITS(wdt_reg->wtc, or, nand);

	spin_unlock(&rc32434_wdt_device.io_lock);
	pr_info("Started watchdog timer\n");
}

static void rc32434_wdt_stop(void)
{
	spin_lock(&rc32434_wdt_device.io_lock);

	/* Disable WDT */
	SET_BITS(wdt_reg->wtc, 0, 1 << RC32434_WTC_EN);

	spin_unlock(&rc32434_wdt_device.io_lock);
	pr_info("Stopped watchdog timer\n");
}

static void rc32434_wdt_ping(void)
{
	spin_lock(&rc32434_wdt_device.io_lock);
	writel(0, &wdt_reg->wtcount);
	spin_unlock(&rc32434_wdt_device.io_lock);
}

static int rc32434_wdt_open(struct inode *inode, struct file *file)
{
	if (test_and_set_bit(0, &rc32434_wdt_device.inuse))
		return -EBUSY;

	if (nowayout)
		__module_get(THIS_MODULE);

	rc32434_wdt_start();
	rc32434_wdt_ping();

	return stream_open(inode, file);
}

static int rc32434_wdt_release(struct inode *inode, struct file *file)
{
	if (expect_close == 42) {
		rc32434_wdt_stop();
		module_put(THIS_MODULE);
	} else {
		pr_crit("device closed unexpectedly. WDT will not stop!\n");
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
	static const struct watchdog_info ident = {
		.options =		WDIOF_SETTIMEOUT |
					WDIOF_KEEPALIVEPING |
					WDIOF_MAGICCLOSE,
		.identity =		"RC32434_WDT Watchdog",
	};
	switch (cmd) {
	case WDIOC_GETSUPPORT:
		if (copy_to_user(argp, &ident, sizeof(ident)))
			return -EFAULT;
		break;
	case WDIOC_GETSTATUS:
	case WDIOC_GETBOOTSTATUS:
		value = 0;
		if (copy_to_user(argp, &value, sizeof(int)))
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
	case WDIOC_KEEPALIVE:
		rc32434_wdt_ping();
		break;
	case WDIOC_SETTIMEOUT:
		if (copy_from_user(&new_timeout, argp, sizeof(int)))
			return -EFAULT;
		if (rc32434_wdt_set(new_timeout))
			return -EINVAL;
		fallthrough;
	case WDIOC_GETTIMEOUT:
		return copy_to_user(argp, &timeout, sizeof(int)) ? -EFAULT : 0;
	default:
		return -ENOTTY;
	}

	return 0;
}

static const struct file_operations rc32434_wdt_fops = {
	.owner		= THIS_MODULE,
	.write		= rc32434_wdt_write,
	.unlocked_ioctl	= rc32434_wdt_ioctl,
	.compat_ioctl	= compat_ptr_ioctl,
	.open		= rc32434_wdt_open,
	.release	= rc32434_wdt_release,
};

static struct miscdevice rc32434_wdt_miscdev = {
	.minor	= WATCHDOG_MINOR,
	.name	= "watchdog",
	.fops	= &rc32434_wdt_fops,
};

static int rc32434_wdt_probe(struct platform_device *pdev)
{
	int ret;
	struct resource *r;

	r = platform_get_resource_byname(pdev, IORESOURCE_MEM, "rb532_wdt_res");
	if (!r) {
		pr_err("failed to retrieve resources\n");
		return -ENODEV;
	}

	wdt_reg = devm_ioremap(&pdev->dev, r->start, resource_size(r));
	if (!wdt_reg) {
		pr_err("failed to remap I/O resources\n");
		return -ENXIO;
	}

	spin_lock_init(&rc32434_wdt_device.io_lock);

	/* Make sure the watchdog is not running */
	rc32434_wdt_stop();

	/* Check that the heartbeat value is within it's range;
	 * if not reset to the default */
	if (rc32434_wdt_set(timeout)) {
		rc32434_wdt_set(WATCHDOG_TIMEOUT);
		pr_info("timeout value must be between 0 and %d\n",
			WTCOMP2SEC((u32)-1));
	}

	ret = misc_register(&rc32434_wdt_miscdev);
	if (ret < 0) {
		pr_err("failed to register watchdog device\n");
		return ret;
	}

	pr_info("Watchdog Timer version " VERSION ", timer margin: %d sec\n",
		timeout);

	return 0;
}

static void rc32434_wdt_remove(struct platform_device *pdev)
{
	misc_deregister(&rc32434_wdt_miscdev);
}

static void rc32434_wdt_shutdown(struct platform_device *pdev)
{
	rc32434_wdt_stop();
}

static struct platform_driver rc32434_wdt_driver = {
	.probe		= rc32434_wdt_probe,
	.remove_new	= rc32434_wdt_remove,
	.shutdown	= rc32434_wdt_shutdown,
	.driver		= {
			.name = "rc32434_wdt",
	}
};

module_platform_driver(rc32434_wdt_driver);

MODULE_AUTHOR("Ondrej Zajicek <santiago@crfreenet.org>,"
		"Florian Fainelli <florian@openwrt.org>");
MODULE_DESCRIPTION("Driver for the IDT RC32434 SoC watchdog");
MODULE_LICENSE("GPL");

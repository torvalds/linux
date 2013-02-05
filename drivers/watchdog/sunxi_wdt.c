/*
 *	sunxi Watchdog Driver
 *
 *	Copyright (c) 2012 Henrik Nordstrom
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 *
 *	Based on xen_wdt.c
 *	(c) Copyright 2010 Novell, Inc.
 *
 * Known issues:
 * 	* Timer scale is not seconds. 0-23 seems to be in ~0.5s.
 * 	* Unsure if values above 23 actually fires, or if they fire
 * 	  what scale they represent.
 * 	* There is a small window while the watchdog is being reloded
 * 	  where there is no watchdog. If not tne watchdog apparently
 * 	  ignores the reload and happily continues where it was.
 */

#define DRV_NAME	"sunxi_wdt"
#define DRV_VERSION	"1.0"
#define PFX		DRV_NAME ": "

#include <linux/bug.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/platform_device.h>
#include <linux/resource.h>
#include <linux/uaccess.h>
#include <linux/watchdog.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <mach/platform.h>

static struct platform_device *platform_device;
static bool is_active, expect_release;

static struct sunxi_watchdog_reg {
	u32 ctrl;
	u32 mode;
	u32 reserved[2];
} __iomem *wdt_reg;

#define SW_WDT_CTRL_RELOAD ((0xA57 << 1) | (1 << 0))

#define SW_WDT_MODE_ENABLE (1 << 0)
#define SW_WDT_MODE_RESET (1 << 1)
#define SW_WDT_MODE_TIMEOUT(n) (n << 2)

#define WATCHDOG_TIMEOUT 23 /* in some unit / scale */
#define MAX_TIMEOUT 23	    /* register fits ((1<<6)-1), but seems halted above 23 */
static unsigned int timeout = WATCHDOG_TIMEOUT;
module_param(timeout, uint, S_IRUGO);
MODULE_PARM_DESC(timeout, "Watchdog timeout in seconds "
	"(default=" __MODULE_STRING(WATCHDOG_TIMEOUT) ")");

static bool nowayout = WATCHDOG_NOWAYOUT;
module_param(nowayout, bool, S_IRUGO);
MODULE_PARM_DESC(nowayout, "Watchdog cannot be stopped once started "
	"(default=" __MODULE_STRING(WATCHDOG_NOWAYOUT) ")");

static int sunxi_wdt_stop(void)
{
	int err = 0;

	writel(0, &wdt_reg->mode);

	return err;
}

static int sunxi_wdt_kick(void)
{
	writel(SW_WDT_CTRL_RELOAD, &wdt_reg->ctrl);
	return 0;
}

static int sunxi_wdt_set_timeout(int timeout)
{
	writel(SW_WDT_MODE_TIMEOUT(timeout) | SW_WDT_MODE_RESET | SW_WDT_MODE_ENABLE, &wdt_reg->mode);
	sunxi_wdt_kick();
	return 0;
}

static int sunxi_wdt_start(void)
{
	return sunxi_wdt_set_timeout(timeout);
}

static int sunxi_wdt_open(struct inode *inode, struct file *file)
{
	int err;

	/* /dev/watchdog can only be opened once */
	if (xchg(&is_active, true))
		return -EBUSY;

	err = sunxi_wdt_start();
	return err ?: nonseekable_open(inode, file);
}

static int sunxi_wdt_release(struct inode *inode, struct file *file)
{
	if (expect_release)
		sunxi_wdt_stop();
	else {
		printk(KERN_CRIT PFX
		       "unexpected close, not stopping watchdog!\n");
		sunxi_wdt_kick();
	}
	is_active = false;
	expect_release = false;
	return 0;
}

static ssize_t sunxi_wdt_write(struct file *file, const char __user *data,
			     size_t len, loff_t *ppos)
{
	/* See if we got the magic character 'V' and reload the timer */
	if (len) {
		if (!nowayout) {
			size_t i;

			/* in case it was set long ago */
			expect_release = false;

			/* scan to see whether or not we got the magic
			   character */
			for (i = 0; i != len; i++) {
				char c;
				if (get_user(c, data + i))
					return -EFAULT;
				if (c == 'V')
					expect_release = true;
			}
		}

		/* someone wrote to us, we should reload the timer */
		sunxi_wdt_kick();
	}
	return len;
}

static long sunxi_wdt_ioctl(struct file *file, unsigned int cmd,
			  unsigned long arg)
{
	int new_options, retval = -EINVAL;
	int new_timeout;
	int __user *argp = (void __user *)arg;
	static const struct watchdog_info ident = {
		.options =		WDIOF_SETTIMEOUT | WDIOF_MAGICCLOSE,
		.firmware_version =	0,
		.identity =		DRV_NAME,
	};

	switch (cmd) {
	case WDIOC_GETSUPPORT:
		return copy_to_user(argp, &ident, sizeof(ident)) ? -EFAULT : 0;

	case WDIOC_GETSTATUS:
	case WDIOC_GETBOOTSTATUS:
		return put_user(0, argp);

	case WDIOC_SETOPTIONS:
		if (get_user(new_options, argp))
			return -EFAULT;
		if (new_options & WDIOS_DISABLECARD)
			retval = sunxi_wdt_stop();
		if (new_options & WDIOS_ENABLECARD) {
			retval = sunxi_wdt_start();
		}
		return retval;

	case WDIOC_KEEPALIVE:
		sunxi_wdt_kick();
		return 0;

	case WDIOC_SETTIMEOUT:
		if (get_user(new_timeout, argp))
			return -EFAULT;
		if (!new_timeout || new_timeout > MAX_TIMEOUT)
			return -EINVAL;
		timeout = new_timeout;
		sunxi_wdt_set_timeout(timeout);
		/* fall through */
	case WDIOC_GETTIMEOUT:
		return put_user(timeout, argp);
	}

	return -ENOTTY;
}

static const struct file_operations sunxi_wdt_fops = {
	.owner =		THIS_MODULE,
	.llseek =		no_llseek,
	.write =		sunxi_wdt_write,
	.unlocked_ioctl =	sunxi_wdt_ioctl,
	.open =			sunxi_wdt_open,
	.release =		sunxi_wdt_release,
};

static struct miscdevice sunxi_wdt_miscdev = {
	.minor =	WATCHDOG_MINOR,
	.name =		"watchdog",
	.fops =		&sunxi_wdt_fops,
};

static int __devinit sunxi_wdt_probe(struct platform_device *pdev)
{
	int ret;
	struct resource *res;

	if (!timeout || timeout > MAX_TIMEOUT ) {
		timeout = WATCHDOG_TIMEOUT;
		printk(KERN_INFO PFX
		       "timeout value invalid, using %d\n", timeout);
	}

	/*
	 * As this driver only covers the global watchdog case, reject
	 * any attempts to register per-CPU watchdogs.
	 */
	if (pdev->id != -1)
		return -EINVAL;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (unlikely(!res)) {
		ret = -EINVAL;
		goto err_get_resource;
	}

	if (!devm_request_mem_region(&pdev->dev, res->start,
				     resource_size(res), DRV_NAME)) {
		ret = -EBUSY;
		goto err_request_mem_region;
	}

	wdt_reg = devm_ioremap(&pdev->dev, res->start, resource_size(res));
	if (!wdt_reg) {
		ret = -ENXIO;
		goto err_ioremap;
	}

	ret = misc_register(&sunxi_wdt_miscdev);
	if (ret) {
		printk(KERN_ERR PFX
		       "cannot register miscdev on minor=%d (%d)\n",
		       WATCHDOG_MINOR, ret);
		goto err_misc_register;
	}

	printk(KERN_INFO PFX
	       "initialized (timeout=%ds, nowayout=%d)\n",
	       timeout, nowayout);

	sunxi_wdt_kick(); /* give userspace a bit more time to settle if watchdog already running */

	return ret;

err_misc_register:
	devm_iounmap(&pdev->dev, wdt_reg);
err_ioremap:
	devm_release_mem_region(&pdev->dev, res->start, resource_size(res));
err_request_mem_region:
err_get_resource:
	return ret;
}

static int __devexit sunxi_wdt_remove(struct platform_device *pdev)
{
	struct resource *res = platform_get_resource(pdev, IORESOURCE_MEM, 0);

	/* Stop the timer before we leave */
	sunxi_wdt_stop();

	misc_deregister(&sunxi_wdt_miscdev);
	devm_iounmap(&pdev->dev, wdt_reg);
	devm_release_mem_region(&pdev->dev, res->start, resource_size(res));

	return 0;
}

static void sunxi_wdt_shutdown(struct platform_device *pdev)
{
	sunxi_wdt_stop();
}

static int sunxi_wdt_suspend(struct platform_device *dev, pm_message_t state)
{
	if (is_active)
		return sunxi_wdt_stop();
	else
		return 0;
}

static int sunxi_wdt_resume(struct platform_device *dev)
{
	if (is_active)
		return sunxi_wdt_start();
	else
		return 0;
}

static struct platform_driver sunxi_wdt_driver = {
	.probe          = sunxi_wdt_probe,
	.remove         = __devexit_p(sunxi_wdt_remove),
	.shutdown       = sunxi_wdt_shutdown,
	.suspend        = sunxi_wdt_suspend,
	.resume         = sunxi_wdt_resume,
	.driver         = {
		.owner  = THIS_MODULE,
		.name   = DRV_NAME,
	},
};

static struct resource sunxi_wdt_res[] = {
	{
		.start	= SW_PA_TIMERC_IO_BASE+0x90,
		.end	= SW_PA_TIMERC_IO_BASE+0x90+0x10-1,
		.flags	= IORESOURCE_MEM,
	},
};

static int __init sunxi_wdt_init_module(void)
{
	int err;

	printk(KERN_INFO PFX "sunxi WatchDog Timer Driver v%s\n", DRV_VERSION);

	err = platform_driver_register(&sunxi_wdt_driver);
	if (err)
		goto err_driver_register;

	platform_device = platform_device_register_simple(DRV_NAME, -1, sunxi_wdt_res, ARRAY_SIZE(sunxi_wdt_res));
	if (IS_ERR(platform_device)) {
		err = PTR_ERR(platform_device);
		goto err_platform_device;
	}

	return err;

err_platform_device:
	platform_driver_unregister(&sunxi_wdt_driver);
err_driver_register:
	return err;
}

static void __exit sunxi_wdt_cleanup_module(void)
{
	platform_device_unregister(platform_device);
	platform_driver_unregister(&sunxi_wdt_driver);
	printk(KERN_INFO PFX "module unloaded\n");
}

module_init(sunxi_wdt_init_module);
module_exit(sunxi_wdt_cleanup_module);

MODULE_AUTHOR("Henrik Nordstrom <henrik@henriknordstrom.net>");
MODULE_DESCRIPTION("sunxi WatchDog Timer Driver");
MODULE_VERSION(DRV_VERSION);
MODULE_LICENSE("GPL");
MODULE_ALIAS_MISCDEV(WATCHDOG_MINOR);

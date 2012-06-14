/*
 *  Broadcom BCM63xx SoC watchdog driver
 *
 *  Copyright (C) 2007, Miguel Gaio <miguel.gaio@efixo.com>
 *  Copyright (C) 2008, Florian Fainelli <florian@openwrt.org>
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/bitops.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/types.h>
#include <linux/uaccess.h>
#include <linux/watchdog.h>
#include <linux/timer.h>
#include <linux/jiffies.h>
#include <linux/interrupt.h>
#include <linux/ptrace.h>
#include <linux/resource.h>
#include <linux/platform_device.h>

#include <bcm63xx_cpu.h>
#include <bcm63xx_io.h>
#include <bcm63xx_regs.h>
#include <bcm63xx_timer.h>

#define PFX KBUILD_MODNAME

#define WDT_HZ		50000000 /* Fclk */
#define WDT_DEFAULT_TIME	30      /* seconds */
#define WDT_MAX_TIME		256     /* seconds */

static struct {
	void __iomem *regs;
	struct timer_list timer;
	int default_ticks;
	unsigned long inuse;
	atomic_t ticks;
} bcm63xx_wdt_device;

static int expect_close;

static int wdt_time = WDT_DEFAULT_TIME;
static bool nowayout = WATCHDOG_NOWAYOUT;
module_param(nowayout, bool, 0);
MODULE_PARM_DESC(nowayout, "Watchdog cannot be stopped once started (default="
	__MODULE_STRING(WATCHDOG_NOWAYOUT) ")");

/* HW functions */
static void bcm63xx_wdt_hw_start(void)
{
	bcm_writel(0xfffffffe, bcm63xx_wdt_device.regs + WDT_DEFVAL_REG);
	bcm_writel(WDT_START_1, bcm63xx_wdt_device.regs + WDT_CTL_REG);
	bcm_writel(WDT_START_2, bcm63xx_wdt_device.regs + WDT_CTL_REG);
}

static void bcm63xx_wdt_hw_stop(void)
{
	bcm_writel(WDT_STOP_1, bcm63xx_wdt_device.regs + WDT_CTL_REG);
	bcm_writel(WDT_STOP_2, bcm63xx_wdt_device.regs + WDT_CTL_REG);
}

static void bcm63xx_wdt_isr(void *data)
{
	struct pt_regs *regs = get_irq_regs();

	die(PFX " fire", regs);
}

static void bcm63xx_timer_tick(unsigned long unused)
{
	if (!atomic_dec_and_test(&bcm63xx_wdt_device.ticks)) {
		bcm63xx_wdt_hw_start();
		mod_timer(&bcm63xx_wdt_device.timer, jiffies + HZ);
	} else
		pr_crit("watchdog will restart system\n");
}

static void bcm63xx_wdt_pet(void)
{
	atomic_set(&bcm63xx_wdt_device.ticks, wdt_time);
}

static void bcm63xx_wdt_start(void)
{
	bcm63xx_wdt_pet();
	bcm63xx_timer_tick(0);
}

static void bcm63xx_wdt_pause(void)
{
	del_timer_sync(&bcm63xx_wdt_device.timer);
	bcm63xx_wdt_hw_stop();
}

static int bcm63xx_wdt_settimeout(int new_time)
{
	if ((new_time <= 0) || (new_time > WDT_MAX_TIME))
		return -EINVAL;

	wdt_time = new_time;

	return 0;
}

static int bcm63xx_wdt_open(struct inode *inode, struct file *file)
{
	if (test_and_set_bit(0, &bcm63xx_wdt_device.inuse))
		return -EBUSY;

	bcm63xx_wdt_start();
	return nonseekable_open(inode, file);
}

static int bcm63xx_wdt_release(struct inode *inode, struct file *file)
{
	if (expect_close == 42)
		bcm63xx_wdt_pause();
	else {
		pr_crit("Unexpected close, not stopping watchdog!\n");
		bcm63xx_wdt_start();
	}
	clear_bit(0, &bcm63xx_wdt_device.inuse);
	expect_close = 0;
	return 0;
}

static ssize_t bcm63xx_wdt_write(struct file *file, const char *data,
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
		bcm63xx_wdt_pet();
	}
	return len;
}

static struct watchdog_info bcm63xx_wdt_info = {
	.identity       = PFX,
	.options        = WDIOF_SETTIMEOUT |
				WDIOF_KEEPALIVEPING |
				WDIOF_MAGICCLOSE,
};


static long bcm63xx_wdt_ioctl(struct file *file, unsigned int cmd,
				unsigned long arg)
{
	void __user *argp = (void __user *)arg;
	int __user *p = argp;
	int new_value, retval = -EINVAL;

	switch (cmd) {
	case WDIOC_GETSUPPORT:
		return copy_to_user(argp, &bcm63xx_wdt_info,
			sizeof(bcm63xx_wdt_info)) ? -EFAULT : 0;

	case WDIOC_GETSTATUS:
	case WDIOC_GETBOOTSTATUS:
		return put_user(0, p);

	case WDIOC_SETOPTIONS:
		if (get_user(new_value, p))
			return -EFAULT;

		if (new_value & WDIOS_DISABLECARD) {
			bcm63xx_wdt_pause();
			retval = 0;
		}
		if (new_value & WDIOS_ENABLECARD) {
			bcm63xx_wdt_start();
			retval = 0;
		}

		return retval;

	case WDIOC_KEEPALIVE:
		bcm63xx_wdt_pet();
		return 0;

	case WDIOC_SETTIMEOUT:
		if (get_user(new_value, p))
			return -EFAULT;

		if (bcm63xx_wdt_settimeout(new_value))
			return -EINVAL;

		bcm63xx_wdt_pet();

	case WDIOC_GETTIMEOUT:
		return put_user(wdt_time, p);

	default:
		return -ENOTTY;

	}
}

static const struct file_operations bcm63xx_wdt_fops = {
	.owner		= THIS_MODULE,
	.llseek		= no_llseek,
	.write		= bcm63xx_wdt_write,
	.unlocked_ioctl	= bcm63xx_wdt_ioctl,
	.open		= bcm63xx_wdt_open,
	.release	= bcm63xx_wdt_release,
};

static struct miscdevice bcm63xx_wdt_miscdev = {
	.minor	= WATCHDOG_MINOR,
	.name	= "watchdog",
	.fops	= &bcm63xx_wdt_fops,
};


static int __devinit bcm63xx_wdt_probe(struct platform_device *pdev)
{
	int ret;
	struct resource *r;

	setup_timer(&bcm63xx_wdt_device.timer, bcm63xx_timer_tick, 0L);

	r = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!r) {
		dev_err(&pdev->dev, "failed to get resources\n");
		return -ENODEV;
	}

	bcm63xx_wdt_device.regs = ioremap_nocache(r->start, resource_size(r));
	if (!bcm63xx_wdt_device.regs) {
		dev_err(&pdev->dev, "failed to remap I/O resources\n");
		return -ENXIO;
	}

	ret = bcm63xx_timer_register(TIMER_WDT_ID, bcm63xx_wdt_isr, NULL);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to register wdt timer isr\n");
		goto unmap;
	}

	if (bcm63xx_wdt_settimeout(wdt_time)) {
		bcm63xx_wdt_settimeout(WDT_DEFAULT_TIME);
		dev_info(&pdev->dev,
			": wdt_time value must be 1 <= wdt_time <= 256, using %d\n",
			wdt_time);
	}

	ret = misc_register(&bcm63xx_wdt_miscdev);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to register watchdog device\n");
		goto unregister_timer;
	}

	dev_info(&pdev->dev, " started, timer margin: %d sec\n",
						WDT_DEFAULT_TIME);

	return 0;

unregister_timer:
	bcm63xx_timer_unregister(TIMER_WDT_ID);
unmap:
	iounmap(bcm63xx_wdt_device.regs);
	return ret;
}

static int __devexit bcm63xx_wdt_remove(struct platform_device *pdev)
{
	if (!nowayout)
		bcm63xx_wdt_pause();

	misc_deregister(&bcm63xx_wdt_miscdev);
	bcm63xx_timer_unregister(TIMER_WDT_ID);
	iounmap(bcm63xx_wdt_device.regs);
	return 0;
}

static void bcm63xx_wdt_shutdown(struct platform_device *pdev)
{
	bcm63xx_wdt_pause();
}

static struct platform_driver bcm63xx_wdt = {
	.probe	= bcm63xx_wdt_probe,
	.remove = __devexit_p(bcm63xx_wdt_remove),
	.shutdown = bcm63xx_wdt_shutdown,
	.driver = {
		.owner = THIS_MODULE,
		.name = "bcm63xx-wdt",
	}
};

module_platform_driver(bcm63xx_wdt);

MODULE_AUTHOR("Miguel Gaio <miguel.gaio@efixo.com>");
MODULE_AUTHOR("Florian Fainelli <florian@openwrt.org>");
MODULE_DESCRIPTION("Driver for the Broadcom BCM63xx SoC watchdog");
MODULE_LICENSE("GPL");
MODULE_ALIAS_MISCDEV(WATCHDOG_MINOR);
MODULE_ALIAS("platform:bcm63xx-wdt");

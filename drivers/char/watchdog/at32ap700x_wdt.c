/*
 * Watchdog driver for Atmel AT32AP700X devices
 *
 * Copyright (C) 2005-2006 Atmel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/platform_device.h>
#include <linux/watchdog.h>
#include <linux/uaccess.h>
#include <linux/io.h>

#define TIMEOUT_MIN		1
#define TIMEOUT_MAX		2
#define TIMEOUT_DEFAULT		TIMEOUT_MAX

/* module parameters */
static int timeout =  TIMEOUT_DEFAULT;
module_param(timeout, int, 0);
MODULE_PARM_DESC(timeout,
		"Timeout value. Limited to be 1 or 2 seconds. (default="
		__MODULE_STRING(TIMEOUT_DEFAULT) ")");

/* Watchdog registers and write/read macro */
#define WDT_CTRL		0x00
#define WDT_CTRL_EN		   0
#define WDT_CTRL_PSEL		   8
#define WDT_CTRL_KEY		  24

#define WDT_CLR			0x04

#define WDT_BIT(name)		(1 << WDT_##name)
#define WDT_BF(name, value)	((value) << WDT_##name)

#define wdt_readl(dev, reg)				\
	__raw_readl((dev)->regs + WDT_##reg)
#define wdt_writel(dev, reg, value)			\
	__raw_writel((value), (dev)->regs + WDT_##reg)

struct wdt_at32ap700x {
	void __iomem		*regs;
	int			timeout;
	int			users;
	struct miscdevice	miscdev;
};

static struct wdt_at32ap700x *wdt;

/*
 * Disable the watchdog.
 */
static inline void at32_wdt_stop(void)
{
	unsigned long psel = wdt_readl(wdt, CTRL) & WDT_BF(CTRL_PSEL, 0x0f);
	wdt_writel(wdt, CTRL, psel | WDT_BF(CTRL_KEY, 0x55));
	wdt_writel(wdt, CTRL, psel | WDT_BF(CTRL_KEY, 0xaa));
}

/*
 * Enable and reset the watchdog.
 */
static inline void at32_wdt_start(void)
{
	/* 0xf is 2^16 divider = 2 sec, 0xe is 2^15 divider = 1 sec */
	unsigned long psel = (wdt->timeout > 1) ? 0xf : 0xe;

	wdt_writel(wdt, CTRL, WDT_BIT(CTRL_EN)
			| WDT_BF(CTRL_PSEL, psel)
			| WDT_BF(CTRL_KEY, 0x55));
	wdt_writel(wdt, CTRL, WDT_BIT(CTRL_EN)
			| WDT_BF(CTRL_PSEL, psel)
			| WDT_BF(CTRL_KEY, 0xaa));
}

/*
 * Pat the watchdog timer.
 */
static inline void at32_wdt_pat(void)
{
	wdt_writel(wdt, CLR, 0x42);
}

/*
 * Watchdog device is opened, and watchdog starts running.
 */
static int at32_wdt_open(struct inode *inode, struct file *file)
{
	if (test_and_set_bit(1, &wdt->users))
		return -EBUSY;

	at32_wdt_start();
	return nonseekable_open(inode, file);
}

/*
 * Close the watchdog device. If CONFIG_WATCHDOG_NOWAYOUT is _not_ defined then
 * the watchdog is also disabled.
 */
static int at32_wdt_close(struct inode *inode, struct file *file)
{
#ifndef CONFIG_WATCHDOG_NOWAYOUT
	at32_wdt_stop();
#endif
	clear_bit(1, &wdt->users);
	return 0;
}

/*
 * Change the watchdog time interval.
 */
static int at32_wdt_settimeout(int time)
{
	/*
	 * All counting occurs at 1 / SLOW_CLOCK (32 kHz) and max prescaler is
	 * 2 ^ 16 allowing up to 2 seconds timeout.
	 */
	if ((time < TIMEOUT_MIN) || (time > TIMEOUT_MAX))
		return -EINVAL;

	/*
	 * Set new watchdog time. It will be used when at32_wdt_start() is
	 * called.
	 */
	wdt->timeout = time;
	return 0;
}

static struct watchdog_info at32_wdt_info = {
	.identity	= "at32ap700x watchdog",
	.options	= WDIOF_SETTIMEOUT | WDIOF_KEEPALIVEPING,
};

/*
 * Handle commands from user-space.
 */
static int at32_wdt_ioctl(struct inode *inode, struct file *file,
		unsigned int cmd, unsigned long arg)
{
	int ret = -ENOTTY;
	int time;
	void __user *argp = (void __user *)arg;
	int __user *p = argp;

	switch (cmd) {
	case WDIOC_KEEPALIVE:
		at32_wdt_pat();
		ret = 0;
		break;
	case WDIOC_GETSUPPORT:
		ret = copy_to_user(argp, &at32_wdt_info,
				sizeof(at32_wdt_info)) ? -EFAULT : 0;
		break;
	case WDIOC_SETTIMEOUT:
		ret = get_user(time, p);
		if (ret)
			break;
		ret = at32_wdt_settimeout(time);
		if (ret)
			break;
		/* Enable new time value */
		at32_wdt_start();
		/* fall through */
	case WDIOC_GETTIMEOUT:
		ret = put_user(wdt->timeout, p);
		break;
	case WDIOC_GETSTATUS: /* fall through */
	case WDIOC_GETBOOTSTATUS:
		ret = put_user(0, p);
		break;
	case WDIOC_SETOPTIONS:
		ret = get_user(time, p);
		if (ret)
			break;
		if (time & WDIOS_DISABLECARD)
			at32_wdt_stop();
		if (time & WDIOS_ENABLECARD)
			at32_wdt_start();
		ret = 0;
		break;
	}

	return ret;
}

static ssize_t at32_wdt_write(struct file *file, const char *data, size_t len,
				loff_t *ppos)
{
	at32_wdt_pat();
	return len;
}

static const struct file_operations at32_wdt_fops = {
	.owner		= THIS_MODULE,
	.llseek		= no_llseek,
	.ioctl		= at32_wdt_ioctl,
	.open		= at32_wdt_open,
	.release	= at32_wdt_close,
	.write		= at32_wdt_write,
};

static int __init at32_wdt_probe(struct platform_device *pdev)
{
	struct resource	*regs;
	int ret;

	if (wdt) {
		dev_dbg(&pdev->dev, "only 1 wdt instance supported.\n");
		return -EBUSY;
	}

	regs = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!regs) {
		dev_dbg(&pdev->dev, "missing mmio resource\n");
		return -ENXIO;
	}

	wdt = kzalloc(sizeof(struct wdt_at32ap700x), GFP_KERNEL);
	if (!wdt) {
		dev_dbg(&pdev->dev, "no memory for wdt structure\n");
		return -ENOMEM;
	}

	wdt->regs = ioremap(regs->start, regs->end - regs->start + 1);
	if (!wdt->regs) {
		ret = -ENOMEM;
		dev_dbg(&pdev->dev, "could not map I/O memory\n");
		goto err_free;
	}
	wdt->users = 0;
	wdt->miscdev.minor = WATCHDOG_MINOR;
	wdt->miscdev.name = "watchdog";
	wdt->miscdev.fops = &at32_wdt_fops;

	if (at32_wdt_settimeout(timeout)) {
		at32_wdt_settimeout(TIMEOUT_DEFAULT);
		dev_dbg(&pdev->dev,
			"default timeout invalid, set to %d sec.\n",
			TIMEOUT_DEFAULT);
	}

	ret = misc_register(&wdt->miscdev);
	if (ret) {
		dev_dbg(&pdev->dev, "failed to register wdt miscdev\n");
		goto err_iounmap;
	}

	platform_set_drvdata(pdev, wdt);
	wdt->miscdev.parent = &pdev->dev;
	dev_info(&pdev->dev, "AT32AP700X WDT at 0x%p, timeout %d sec\n",
		wdt->regs, wdt->timeout);

	return 0;

err_iounmap:
	iounmap(wdt->regs);
err_free:
	kfree(wdt);
	wdt = NULL;
	return ret;
}

static int __exit at32_wdt_remove(struct platform_device *pdev)
{
	if (wdt && platform_get_drvdata(pdev) == wdt) {
		misc_deregister(&wdt->miscdev);
		iounmap(wdt->regs);
		kfree(wdt);
		wdt = NULL;
		platform_set_drvdata(pdev, NULL);
	}

	return 0;
}

static void at32_wdt_shutdown(struct platform_device *pdev)
{
	at32_wdt_stop();
}

#ifdef CONFIG_PM
static int at32_wdt_suspend(struct platform_device *pdev, pm_message_t message)
{
	at32_wdt_stop();
	return 0;
}

static int at32_wdt_resume(struct platform_device *pdev)
{
	if (wdt->users)
		at32_wdt_start();
	return 0;
}
#else
#define at32_wdt_suspend NULL
#define at32_wdt_resume NULL
#endif

static struct platform_driver at32_wdt_driver = {
	.remove		= __exit_p(at32_wdt_remove),
	.suspend	= at32_wdt_suspend,
	.resume		= at32_wdt_resume,
	.driver		= {
		.name	= "at32_wdt",
		.owner	= THIS_MODULE,
	},
	.shutdown	= at32_wdt_shutdown,
};

static int __init at32_wdt_init(void)
{
	return platform_driver_probe(&at32_wdt_driver, at32_wdt_probe);
}
module_init(at32_wdt_init);

static void __exit at32_wdt_exit(void)
{
	platform_driver_unregister(&at32_wdt_driver);
}
module_exit(at32_wdt_exit);

MODULE_AUTHOR("Hans-Christian Egtvedt <hcegtvedt@atmel.com>");
MODULE_DESCRIPTION("Watchdog driver for Atmel AT32AP700X");
MODULE_LICENSE("GPL");
MODULE_ALIAS_MISCDEV(WATCHDOG_MINOR);

/*
 * Watchdog driver for Atmel AT32AP700X devices
 *
 * Copyright (C) 2005-2006 Atmel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *
 * Errata: WDT Clear is blocked after WDT Reset
 *
 * A watchdog timer event will, after reset, block writes to the WDT_CLEAR
 * register, preventing the program to clear the next Watchdog Timer Reset.
 *
 * If you still want to use the WDT after a WDT reset a small code can be
 * insterted at the startup checking the AVR32_PM.rcause register for WDT reset
 * and use a GPIO pin to reset the system. This method requires that one of the
 * GPIO pins are available and connected externally to the RESET_N pin. After
 * the GPIO pin has pulled down the reset line the GPIO will be reset and leave
 * the pin tristated with pullup.
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
#include <linux/spinlock.h>
#include <linux/slab.h>

#define TIMEOUT_MIN		1
#define TIMEOUT_MAX		2
#define TIMEOUT_DEFAULT		TIMEOUT_MAX

/* module parameters */
static int timeout =  TIMEOUT_DEFAULT;
module_param(timeout, int, 0);
MODULE_PARM_DESC(timeout,
		"Timeout value. Limited to be 1 or 2 seconds. (default="
		__MODULE_STRING(TIMEOUT_DEFAULT) ")");

static bool nowayout = WATCHDOG_NOWAYOUT;
module_param(nowayout, bool, 0);
MODULE_PARM_DESC(nowayout, "Watchdog cannot be stopped once started (default="
		__MODULE_STRING(WATCHDOG_NOWAYOUT) ")");

/* Watchdog registers and write/read macro */
#define WDT_CTRL		0x00
#define WDT_CTRL_EN		   0
#define WDT_CTRL_PSEL		   8
#define WDT_CTRL_KEY		  24

#define WDT_CLR			0x04

#define WDT_RCAUSE		0x10
#define WDT_RCAUSE_POR		   0
#define WDT_RCAUSE_EXT		   2
#define WDT_RCAUSE_WDT		   3
#define WDT_RCAUSE_JTAG		   4
#define WDT_RCAUSE_SERP		   5

#define WDT_BIT(name)		(1 << WDT_##name)
#define WDT_BF(name, value)	((value) << WDT_##name)

#define wdt_readl(dev, reg)				\
	__raw_readl((dev)->regs + WDT_##reg)
#define wdt_writel(dev, reg, value)			\
	__raw_writel((value), (dev)->regs + WDT_##reg)

struct wdt_at32ap700x {
	void __iomem		*regs;
	spinlock_t		io_lock;
	int			timeout;
	int			boot_status;
	unsigned long		users;
	struct miscdevice	miscdev;
};

static struct wdt_at32ap700x *wdt;
static char expect_release;

/*
 * Disable the watchdog.
 */
static inline void at32_wdt_stop(void)
{
	unsigned long psel;

	spin_lock(&wdt->io_lock);
	psel = wdt_readl(wdt, CTRL) & WDT_BF(CTRL_PSEL, 0x0f);
	wdt_writel(wdt, CTRL, psel | WDT_BF(CTRL_KEY, 0x55));
	wdt_writel(wdt, CTRL, psel | WDT_BF(CTRL_KEY, 0xaa));
	spin_unlock(&wdt->io_lock);
}

/*
 * Enable and reset the watchdog.
 */
static inline void at32_wdt_start(void)
{
	/* 0xf is 2^16 divider = 2 sec, 0xe is 2^15 divider = 1 sec */
	unsigned long psel = (wdt->timeout > 1) ? 0xf : 0xe;

	spin_lock(&wdt->io_lock);
	wdt_writel(wdt, CTRL, WDT_BIT(CTRL_EN)
			| WDT_BF(CTRL_PSEL, psel)
			| WDT_BF(CTRL_KEY, 0x55));
	wdt_writel(wdt, CTRL, WDT_BIT(CTRL_EN)
			| WDT_BF(CTRL_PSEL, psel)
			| WDT_BF(CTRL_KEY, 0xaa));
	spin_unlock(&wdt->io_lock);
}

/*
 * Pat the watchdog timer.
 */
static inline void at32_wdt_pat(void)
{
	spin_lock(&wdt->io_lock);
	wdt_writel(wdt, CLR, 0x42);
	spin_unlock(&wdt->io_lock);
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
 * Close the watchdog device.
 */
static int at32_wdt_close(struct inode *inode, struct file *file)
{
	if (expect_release == 42) {
		at32_wdt_stop();
	} else {
		dev_dbg(wdt->miscdev.parent,
			"unexpected close, not stopping watchdog!\n");
		at32_wdt_pat();
	}
	clear_bit(1, &wdt->users);
	expect_release = 0;
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

/*
 * Get the watchdog status.
 */
static int at32_wdt_get_status(void)
{
	int rcause;
	int status = 0;

	rcause = wdt_readl(wdt, RCAUSE);

	switch (rcause) {
	case WDT_BIT(RCAUSE_EXT):
		status = WDIOF_EXTERN1;
		break;
	case WDT_BIT(RCAUSE_WDT):
		status = WDIOF_CARDRESET;
		break;
	case WDT_BIT(RCAUSE_POR):  /* fall through */
	case WDT_BIT(RCAUSE_JTAG): /* fall through */
	case WDT_BIT(RCAUSE_SERP): /* fall through */
	default:
		break;
	}

	return status;
}

static const struct watchdog_info at32_wdt_info = {
	.identity	= "at32ap700x watchdog",
	.options	= WDIOF_SETTIMEOUT |
			  WDIOF_KEEPALIVEPING |
			  WDIOF_MAGICCLOSE,
};

/*
 * Handle commands from user-space.
 */
static long at32_wdt_ioctl(struct file *file,
				unsigned int cmd, unsigned long arg)
{
	int ret = -ENOTTY;
	int time;
	void __user *argp = (void __user *)arg;
	int __user *p = argp;

	switch (cmd) {
	case WDIOC_GETSUPPORT:
		ret = copy_to_user(argp, &at32_wdt_info,
				sizeof(at32_wdt_info)) ? -EFAULT : 0;
		break;
	case WDIOC_GETSTATUS:
		ret = put_user(0, p);
		break;
	case WDIOC_GETBOOTSTATUS:
		ret = put_user(wdt->boot_status, p);
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
	case WDIOC_KEEPALIVE:
		at32_wdt_pat();
		ret = 0;
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
	}

	return ret;
}

static ssize_t at32_wdt_write(struct file *file, const char __user *data,
				size_t len, loff_t *ppos)
{
	/* See if we got the magic character 'V' and reload the timer */
	if (len) {
		if (!nowayout) {
			size_t i;

			/*
			 * note: just in case someone wrote the magic
			 * character five months ago...
			 */
			expect_release = 0;

			/*
			 * scan to see whether or not we got the magic
			 * character
			 */
			for (i = 0; i != len; i++) {
				char c;
				if (get_user(c, data + i))
					return -EFAULT;
				if (c == 'V')
					expect_release = 42;
			}
		}
		/* someone wrote to us, we should pat the watchdog */
		at32_wdt_pat();
	}
	return len;
}

static const struct file_operations at32_wdt_fops = {
	.owner		= THIS_MODULE,
	.llseek		= no_llseek,
	.unlocked_ioctl	= at32_wdt_ioctl,
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

	wdt->regs = ioremap(regs->start, resource_size(regs));
	if (!wdt->regs) {
		ret = -ENOMEM;
		dev_dbg(&pdev->dev, "could not map I/O memory\n");
		goto err_free;
	}

	spin_lock_init(&wdt->io_lock);
	wdt->boot_status = at32_wdt_get_status();

	/* Work-around for watchdog silicon errata. */
	if (wdt->boot_status & WDIOF_CARDRESET) {
		dev_info(&pdev->dev, "CPU must be reset with external "
				"reset or POR due to silicon errata.\n");
		ret = -EIO;
		goto err_iounmap;
	} else {
		wdt->users = 0;
	}

	wdt->miscdev.minor	= WATCHDOG_MINOR;
	wdt->miscdev.name	= "watchdog";
	wdt->miscdev.fops	= &at32_wdt_fops;
	wdt->miscdev.parent	= &pdev->dev;

	platform_set_drvdata(pdev, wdt);

	if (at32_wdt_settimeout(timeout)) {
		at32_wdt_settimeout(TIMEOUT_DEFAULT);
		dev_dbg(&pdev->dev,
			"default timeout invalid, set to %d sec.\n",
			TIMEOUT_DEFAULT);
	}

	ret = misc_register(&wdt->miscdev);
	if (ret) {
		dev_dbg(&pdev->dev, "failed to register wdt miscdev\n");
		goto err_register;
	}

	dev_info(&pdev->dev,
		"AT32AP700X WDT at 0x%p, timeout %d sec (nowayout=%d)\n",
		wdt->regs, wdt->timeout, nowayout);

	return 0;

err_register:
	platform_set_drvdata(pdev, NULL);
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
		/* Stop the timer before we leave */
		if (!nowayout)
			at32_wdt_stop();

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

/* work with hotplug and coldplug */
MODULE_ALIAS("platform:at32_wdt");

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

MODULE_AUTHOR("Hans-Christian Egtvedt <egtvedt@samfundet.no>");
MODULE_DESCRIPTION("Watchdog driver for Atmel AT32AP700X");
MODULE_LICENSE("GPL");
MODULE_ALIAS_MISCDEV(WATCHDOG_MINOR);

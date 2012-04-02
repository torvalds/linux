/*
 * Copyright 2010-2011 Picochip Ltd., Jamie Iles
 * http://www.picochip.com
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 * This file implements a driver for the Synopsys DesignWare watchdog device
 * in the many ARM subsystems. The watchdog has 16 different timeout periods
 * and these are a function of the input clock frequency.
 *
 * The DesignWare watchdog cannot be stopped once it has been started so we
 * use a software timer to implement a ping that will keep the watchdog alive.
 * If we receive an expected close for the watchdog then we keep the timer
 * running, otherwise the timer is stopped and the watchdog will expire.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/fs.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/pm.h>
#include <linux/platform_device.h>
#include <linux/spinlock.h>
#include <linux/timer.h>
#include <linux/uaccess.h>
#include <linux/watchdog.h>

#define WDOG_CONTROL_REG_OFFSET		    0x00
#define WDOG_CONTROL_REG_WDT_EN_MASK	    0x01
#define WDOG_TIMEOUT_RANGE_REG_OFFSET	    0x04
#define WDOG_CURRENT_COUNT_REG_OFFSET	    0x08
#define WDOG_COUNTER_RESTART_REG_OFFSET     0x0c
#define WDOG_COUNTER_RESTART_KICK_VALUE	    0x76

/* The maximum TOP (timeout period) value that can be set in the watchdog. */
#define DW_WDT_MAX_TOP		15

static bool nowayout = WATCHDOG_NOWAYOUT;
module_param(nowayout, bool, 0);
MODULE_PARM_DESC(nowayout, "Watchdog cannot be stopped once started "
		 "(default=" __MODULE_STRING(WATCHDOG_NOWAYOUT) ")");

#define WDT_TIMEOUT		(HZ / 2)

static struct {
	spinlock_t		lock;
	void __iomem		*regs;
	struct clk		*clk;
	unsigned long		in_use;
	unsigned long		next_heartbeat;
	struct timer_list	timer;
	int			expect_close;
} dw_wdt;

static inline int dw_wdt_is_enabled(void)
{
	return readl(dw_wdt.regs + WDOG_CONTROL_REG_OFFSET) &
		WDOG_CONTROL_REG_WDT_EN_MASK;
}

static inline int dw_wdt_top_in_seconds(unsigned top)
{
	/*
	 * There are 16 possible timeout values in 0..15 where the number of
	 * cycles is 2 ^ (16 + i) and the watchdog counts down.
	 */
	return (1 << (16 + top)) / clk_get_rate(dw_wdt.clk);
}

static int dw_wdt_get_top(void)
{
	int top = readl(dw_wdt.regs + WDOG_TIMEOUT_RANGE_REG_OFFSET) & 0xF;

	return dw_wdt_top_in_seconds(top);
}

static inline void dw_wdt_set_next_heartbeat(void)
{
	dw_wdt.next_heartbeat = jiffies + dw_wdt_get_top() * HZ;
}

static int dw_wdt_set_top(unsigned top_s)
{
	int i, top_val = DW_WDT_MAX_TOP;

	/*
	 * Iterate over the timeout values until we find the closest match. We
	 * always look for >=.
	 */
	for (i = 0; i <= DW_WDT_MAX_TOP; ++i)
		if (dw_wdt_top_in_seconds(i) >= top_s) {
			top_val = i;
			break;
		}

	/* Set the new value in the watchdog. */
	writel(top_val, dw_wdt.regs + WDOG_TIMEOUT_RANGE_REG_OFFSET);

	dw_wdt_set_next_heartbeat();

	return dw_wdt_top_in_seconds(top_val);
}

static void dw_wdt_keepalive(void)
{
	writel(WDOG_COUNTER_RESTART_KICK_VALUE, dw_wdt.regs +
	       WDOG_COUNTER_RESTART_REG_OFFSET);
}

static void dw_wdt_ping(unsigned long data)
{
	if (time_before(jiffies, dw_wdt.next_heartbeat) ||
	    (!nowayout && !dw_wdt.in_use)) {
		dw_wdt_keepalive();
		mod_timer(&dw_wdt.timer, jiffies + WDT_TIMEOUT);
	} else
		pr_crit("keepalive missed, machine will reset\n");
}

static int dw_wdt_open(struct inode *inode, struct file *filp)
{
	if (test_and_set_bit(0, &dw_wdt.in_use))
		return -EBUSY;

	/* Make sure we don't get unloaded. */
	__module_get(THIS_MODULE);

	spin_lock(&dw_wdt.lock);
	if (!dw_wdt_is_enabled()) {
		/*
		 * The watchdog is not currently enabled. Set the timeout to
		 * the maximum and then start it.
		 */
		dw_wdt_set_top(DW_WDT_MAX_TOP);
		writel(WDOG_CONTROL_REG_WDT_EN_MASK,
		       dw_wdt.regs + WDOG_CONTROL_REG_OFFSET);
	}

	dw_wdt_set_next_heartbeat();

	spin_unlock(&dw_wdt.lock);

	return nonseekable_open(inode, filp);
}

ssize_t dw_wdt_write(struct file *filp, const char __user *buf, size_t len,
		     loff_t *offset)
{
	if (!len)
		return 0;

	if (!nowayout) {
		size_t i;

		dw_wdt.expect_close = 0;

		for (i = 0; i < len; ++i) {
			char c;

			if (get_user(c, buf + i))
				return -EFAULT;

			if (c == 'V') {
				dw_wdt.expect_close = 1;
				break;
			}
		}
	}

	dw_wdt_set_next_heartbeat();
	mod_timer(&dw_wdt.timer, jiffies + WDT_TIMEOUT);

	return len;
}

static u32 dw_wdt_time_left(void)
{
	return readl(dw_wdt.regs + WDOG_CURRENT_COUNT_REG_OFFSET) /
		clk_get_rate(dw_wdt.clk);
}

static const struct watchdog_info dw_wdt_ident = {
	.options	= WDIOF_KEEPALIVEPING | WDIOF_SETTIMEOUT |
			  WDIOF_MAGICCLOSE,
	.identity	= "Synopsys DesignWare Watchdog",
};

static long dw_wdt_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	unsigned long val;
	int timeout;

	switch (cmd) {
	case WDIOC_GETSUPPORT:
		return copy_to_user((struct watchdog_info *)arg, &dw_wdt_ident,
				    sizeof(dw_wdt_ident)) ? -EFAULT : 0;

	case WDIOC_GETSTATUS:
	case WDIOC_GETBOOTSTATUS:
		return put_user(0, (int *)arg);

	case WDIOC_KEEPALIVE:
		dw_wdt_set_next_heartbeat();
		return 0;

	case WDIOC_SETTIMEOUT:
		if (get_user(val, (int __user *)arg))
			return -EFAULT;
		timeout = dw_wdt_set_top(val);
		return put_user(timeout , (int __user *)arg);

	case WDIOC_GETTIMEOUT:
		return put_user(dw_wdt_get_top(), (int __user *)arg);

	case WDIOC_GETTIMELEFT:
		/* Get the time left until expiry. */
		if (get_user(val, (int __user *)arg))
			return -EFAULT;
		return put_user(dw_wdt_time_left(), (int __user *)arg);

	default:
		return -ENOTTY;
	}
}

static int dw_wdt_release(struct inode *inode, struct file *filp)
{
	clear_bit(0, &dw_wdt.in_use);

	if (!dw_wdt.expect_close) {
		del_timer(&dw_wdt.timer);

		if (!nowayout)
			pr_crit("unexpected close, system will reboot soon\n");
		else
			pr_crit("watchdog cannot be disabled, system will reboot soon\n");
	}

	dw_wdt.expect_close = 0;

	return 0;
}

#ifdef CONFIG_PM
static int dw_wdt_suspend(struct device *dev)
{
	clk_disable(dw_wdt.clk);

	return 0;
}

static int dw_wdt_resume(struct device *dev)
{
	int err = clk_enable(dw_wdt.clk);

	if (err)
		return err;

	dw_wdt_keepalive();

	return 0;
}

static const struct dev_pm_ops dw_wdt_pm_ops = {
	.suspend	= dw_wdt_suspend,
	.resume		= dw_wdt_resume,
};
#endif /* CONFIG_PM */

static const struct file_operations wdt_fops = {
	.owner		= THIS_MODULE,
	.llseek		= no_llseek,
	.open		= dw_wdt_open,
	.write		= dw_wdt_write,
	.unlocked_ioctl	= dw_wdt_ioctl,
	.release	= dw_wdt_release
};

static struct miscdevice dw_wdt_miscdev = {
	.fops		= &wdt_fops,
	.name		= "watchdog",
	.minor		= WATCHDOG_MINOR,
};

static int __devinit dw_wdt_drv_probe(struct platform_device *pdev)
{
	int ret;
	struct resource *mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);

	if (!mem)
		return -EINVAL;

	dw_wdt.regs = devm_request_and_ioremap(&pdev->dev, mem);
	if (!dw_wdt.regs)
		return -ENOMEM;

	dw_wdt.clk = clk_get(&pdev->dev, NULL);
	if (IS_ERR(dw_wdt.clk))
		return PTR_ERR(dw_wdt.clk);

	ret = clk_enable(dw_wdt.clk);
	if (ret)
		goto out_put_clk;

	spin_lock_init(&dw_wdt.lock);

	ret = misc_register(&dw_wdt_miscdev);
	if (ret)
		goto out_disable_clk;

	dw_wdt_set_next_heartbeat();
	setup_timer(&dw_wdt.timer, dw_wdt_ping, 0);
	mod_timer(&dw_wdt.timer, jiffies + WDT_TIMEOUT);

	return 0;

out_disable_clk:
	clk_disable(dw_wdt.clk);
out_put_clk:
	clk_put(dw_wdt.clk);

	return ret;
}

static int __devexit dw_wdt_drv_remove(struct platform_device *pdev)
{
	misc_deregister(&dw_wdt_miscdev);

	clk_disable(dw_wdt.clk);
	clk_put(dw_wdt.clk);

	return 0;
}

static struct platform_driver dw_wdt_driver = {
	.probe		= dw_wdt_drv_probe,
	.remove		= __devexit_p(dw_wdt_drv_remove),
	.driver		= {
		.name	= "dw_wdt",
		.owner	= THIS_MODULE,
#ifdef CONFIG_PM
		.pm	= &dw_wdt_pm_ops,
#endif /* CONFIG_PM */
	},
};

module_platform_driver(dw_wdt_driver);

MODULE_AUTHOR("Jamie Iles");
MODULE_DESCRIPTION("Synopsys DesignWare Watchdog Driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS_MISCDEV(WATCHDOG_MINOR);

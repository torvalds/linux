/*
 * Atheros AR71XX/AR724X/AR913X built-in hardware watchdog timer.
 *
 * Copyright (C) 2008-2011 Gabor Juhos <juhosg@openwrt.org>
 * Copyright (C) 2008 Imre Kaloz <kaloz@openwrt.org>
 *
 * This driver was based on: drivers/watchdog/ixp4xx_wdt.c
 *	Author: Deepak Saxena <dsaxena@plexity.net>
 *	Copyright 2004 (c) MontaVista, Software, Inc.
 *
 * which again was based on sa1100 driver,
 *	Copyright (C) 2000 Oleg Drokin <green@crimea.edu>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 *
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/bitops.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/platform_device.h>
#include <linux/types.h>
#include <linux/watchdog.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/of.h>
#include <linux/of_platform.h>

#define DRIVER_NAME	"ath79-wdt"

#define WDT_TIMEOUT	15	/* seconds */

#define WDOG_REG_CTRL		0x00
#define WDOG_REG_TIMER		0x04

#define WDOG_CTRL_LAST_RESET	BIT(31)
#define WDOG_CTRL_ACTION_MASK	3
#define WDOG_CTRL_ACTION_NONE	0	/* no action */
#define WDOG_CTRL_ACTION_GPI	1	/* general purpose interrupt */
#define WDOG_CTRL_ACTION_NMI	2	/* NMI */
#define WDOG_CTRL_ACTION_FCR	3	/* full chip reset */

static bool nowayout = WATCHDOG_NOWAYOUT;
module_param(nowayout, bool, 0);
MODULE_PARM_DESC(nowayout, "Watchdog cannot be stopped once started "
			   "(default=" __MODULE_STRING(WATCHDOG_NOWAYOUT) ")");

static int timeout = WDT_TIMEOUT;
module_param(timeout, int, 0);
MODULE_PARM_DESC(timeout, "Watchdog timeout in seconds "
			  "(default=" __MODULE_STRING(WDT_TIMEOUT) "s)");

static unsigned long wdt_flags;

#define WDT_FLAGS_BUSY		0
#define WDT_FLAGS_EXPECT_CLOSE	1

static struct clk *wdt_clk;
static unsigned long wdt_freq;
static int boot_status;
static int max_timeout;
static void __iomem *wdt_base;

static inline void ath79_wdt_wr(unsigned reg, u32 val)
{
	iowrite32(val, wdt_base + reg);
}

static inline u32 ath79_wdt_rr(unsigned reg)
{
	return ioread32(wdt_base + reg);
}

static inline void ath79_wdt_keepalive(void)
{
	ath79_wdt_wr(WDOG_REG_TIMER, wdt_freq * timeout);
	/* flush write */
	ath79_wdt_rr(WDOG_REG_TIMER);
}

static inline void ath79_wdt_enable(void)
{
	ath79_wdt_keepalive();
	ath79_wdt_wr(WDOG_REG_CTRL, WDOG_CTRL_ACTION_FCR);
	/* flush write */
	ath79_wdt_rr(WDOG_REG_CTRL);
}

static inline void ath79_wdt_disable(void)
{
	ath79_wdt_wr(WDOG_REG_CTRL, WDOG_CTRL_ACTION_NONE);
	/* flush write */
	ath79_wdt_rr(WDOG_REG_CTRL);
}

static int ath79_wdt_set_timeout(int val)
{
	if (val < 1 || val > max_timeout)
		return -EINVAL;

	timeout = val;
	ath79_wdt_keepalive();

	return 0;
}

static int ath79_wdt_open(struct inode *inode, struct file *file)
{
	if (test_and_set_bit(WDT_FLAGS_BUSY, &wdt_flags))
		return -EBUSY;

	clear_bit(WDT_FLAGS_EXPECT_CLOSE, &wdt_flags);
	ath79_wdt_enable();

	return nonseekable_open(inode, file);
}

static int ath79_wdt_release(struct inode *inode, struct file *file)
{
	if (test_bit(WDT_FLAGS_EXPECT_CLOSE, &wdt_flags))
		ath79_wdt_disable();
	else {
		pr_crit("device closed unexpectedly, watchdog timer will not stop!\n");
		ath79_wdt_keepalive();
	}

	clear_bit(WDT_FLAGS_BUSY, &wdt_flags);
	clear_bit(WDT_FLAGS_EXPECT_CLOSE, &wdt_flags);

	return 0;
}

static ssize_t ath79_wdt_write(struct file *file, const char *data,
				size_t len, loff_t *ppos)
{
	if (len) {
		if (!nowayout) {
			size_t i;

			clear_bit(WDT_FLAGS_EXPECT_CLOSE, &wdt_flags);

			for (i = 0; i != len; i++) {
				char c;

				if (get_user(c, data + i))
					return -EFAULT;

				if (c == 'V')
					set_bit(WDT_FLAGS_EXPECT_CLOSE,
						&wdt_flags);
			}
		}

		ath79_wdt_keepalive();
	}

	return len;
}

static const struct watchdog_info ath79_wdt_info = {
	.options		= WDIOF_SETTIMEOUT | WDIOF_KEEPALIVEPING |
				  WDIOF_MAGICCLOSE | WDIOF_CARDRESET,
	.firmware_version	= 0,
	.identity		= "ATH79 watchdog",
};

static long ath79_wdt_ioctl(struct file *file, unsigned int cmd,
			    unsigned long arg)
{
	void __user *argp = (void __user *)arg;
	int __user *p = argp;
	int err;
	int t;

	switch (cmd) {
	case WDIOC_GETSUPPORT:
		err = copy_to_user(argp, &ath79_wdt_info,
				   sizeof(ath79_wdt_info)) ? -EFAULT : 0;
		break;

	case WDIOC_GETSTATUS:
		err = put_user(0, p);
		break;

	case WDIOC_GETBOOTSTATUS:
		err = put_user(boot_status, p);
		break;

	case WDIOC_KEEPALIVE:
		ath79_wdt_keepalive();
		err = 0;
		break;

	case WDIOC_SETTIMEOUT:
		err = get_user(t, p);
		if (err)
			break;

		err = ath79_wdt_set_timeout(t);
		if (err)
			break;

		/* fallthrough */
	case WDIOC_GETTIMEOUT:
		err = put_user(timeout, p);
		break;

	default:
		err = -ENOTTY;
		break;
	}

	return err;
}

static const struct file_operations ath79_wdt_fops = {
	.owner		= THIS_MODULE,
	.llseek		= no_llseek,
	.write		= ath79_wdt_write,
	.unlocked_ioctl	= ath79_wdt_ioctl,
	.open		= ath79_wdt_open,
	.release	= ath79_wdt_release,
};

static struct miscdevice ath79_wdt_miscdev = {
	.minor = WATCHDOG_MINOR,
	.name = "watchdog",
	.fops = &ath79_wdt_fops,
};

static int ath79_wdt_probe(struct platform_device *pdev)
{
	struct resource *res;
	u32 ctrl;
	int err;

	if (wdt_base)
		return -EBUSY;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	wdt_base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(wdt_base))
		return PTR_ERR(wdt_base);

	wdt_clk = devm_clk_get(&pdev->dev, "wdt");
	if (IS_ERR(wdt_clk))
		return PTR_ERR(wdt_clk);

	err = clk_enable(wdt_clk);
	if (err)
		return err;

	wdt_freq = clk_get_rate(wdt_clk);
	if (!wdt_freq) {
		err = -EINVAL;
		goto err_clk_disable;
	}

	max_timeout = (0xfffffffful / wdt_freq);
	if (timeout < 1 || timeout > max_timeout) {
		timeout = max_timeout;
		dev_info(&pdev->dev,
			"timeout value must be 0 < timeout < %d, using %d\n",
			max_timeout, timeout);
	}

	ctrl = ath79_wdt_rr(WDOG_REG_CTRL);
	boot_status = (ctrl & WDOG_CTRL_LAST_RESET) ? WDIOF_CARDRESET : 0;

	err = misc_register(&ath79_wdt_miscdev);
	if (err) {
		dev_err(&pdev->dev,
			"unable to register misc device, err=%d\n", err);
		goto err_clk_disable;
	}

	return 0;

err_clk_disable:
	clk_disable(wdt_clk);
	return err;
}

static int ath79_wdt_remove(struct platform_device *pdev)
{
	misc_deregister(&ath79_wdt_miscdev);
	clk_disable(wdt_clk);
	return 0;
}

static void ath97_wdt_shutdown(struct platform_device *pdev)
{
	ath79_wdt_disable();
}

#ifdef CONFIG_OF
static const struct of_device_id ath79_wdt_match[] = {
	{ .compatible = "qca,ar7130-wdt" },
	{},
};
MODULE_DEVICE_TABLE(of, ath79_wdt_match);
#endif

static struct platform_driver ath79_wdt_driver = {
	.probe		= ath79_wdt_probe,
	.remove		= ath79_wdt_remove,
	.shutdown	= ath97_wdt_shutdown,
	.driver		= {
		.name	= DRIVER_NAME,
		.owner	= THIS_MODULE,
		.of_match_table = of_match_ptr(ath79_wdt_match),
	},
};

module_platform_driver(ath79_wdt_driver);

MODULE_DESCRIPTION("Atheros AR71XX/AR724X/AR913X hardware watchdog driver");
MODULE_AUTHOR("Gabor Juhos <juhosg@openwrt.org");
MODULE_AUTHOR("Imre Kaloz <kaloz@openwrt.org");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:" DRIVER_NAME);

/*
 * txx9wdt: A Hardware Watchdog Driver for TXx9 SoCs
 *
 * Copyright (C) 2007 Atsushi Nemoto <anemo@mba.ocn.ne.jp>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/types.h>
#include <linux/miscdevice.h>
#include <linux/watchdog.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/io.h>
#include <asm/txx9tmr.h>

#define WD_TIMER_CCD	7		/* 1/256 */
#define WD_TIMER_CLK	(clk_get_rate(txx9_imclk) / (2 << WD_TIMER_CCD))
#define WD_MAX_TIMEOUT	((0xffffffff >> (32 - TXX9_TIMER_BITS)) / WD_TIMER_CLK)
#define TIMER_MARGIN	60		/* Default is 60 seconds */

static unsigned int timeout = TIMER_MARGIN;	/* in seconds */
module_param(timeout, uint, 0);
MODULE_PARM_DESC(timeout,
	"Watchdog timeout in seconds. "
	"(0<timeout<((2^" __MODULE_STRING(TXX9_TIMER_BITS) ")/(IMCLK/256)), "
	"default=" __MODULE_STRING(TIMER_MARGIN) ")");

static bool nowayout = WATCHDOG_NOWAYOUT;
module_param(nowayout, bool, 0);
MODULE_PARM_DESC(nowayout,
	"Watchdog cannot be stopped once started "
	"(default=" __MODULE_STRING(WATCHDOG_NOWAYOUT) ")");

static struct txx9_tmr_reg __iomem *txx9wdt_reg;
static struct clk *txx9_imclk;
static DEFINE_SPINLOCK(txx9_lock);

static int txx9wdt_ping(struct watchdog_device *wdt_dev)
{
	spin_lock(&txx9_lock);
	__raw_writel(TXx9_TMWTMR_TWIE | TXx9_TMWTMR_TWC, &txx9wdt_reg->wtmr);
	spin_unlock(&txx9_lock);
	return 0;
}

static int txx9wdt_start(struct watchdog_device *wdt_dev)
{
	spin_lock(&txx9_lock);
	__raw_writel(WD_TIMER_CLK * wdt_dev->timeout, &txx9wdt_reg->cpra);
	__raw_writel(WD_TIMER_CCD, &txx9wdt_reg->ccdr);
	__raw_writel(0, &txx9wdt_reg->tisr);	/* clear pending interrupt */
	__raw_writel(TXx9_TMTCR_TCE | TXx9_TMTCR_CCDE | TXx9_TMTCR_TMODE_WDOG,
		     &txx9wdt_reg->tcr);
	__raw_writel(TXx9_TMWTMR_TWIE | TXx9_TMWTMR_TWC, &txx9wdt_reg->wtmr);
	spin_unlock(&txx9_lock);
	return 0;
}

static int txx9wdt_stop(struct watchdog_device *wdt_dev)
{
	spin_lock(&txx9_lock);
	__raw_writel(TXx9_TMWTMR_WDIS, &txx9wdt_reg->wtmr);
	__raw_writel(__raw_readl(&txx9wdt_reg->tcr) & ~TXx9_TMTCR_TCE,
		     &txx9wdt_reg->tcr);
	spin_unlock(&txx9_lock);
	return 0;
}

static int txx9wdt_set_timeout(struct watchdog_device *wdt_dev,
			       unsigned int new_timeout)
{
	wdt_dev->timeout = new_timeout;
	txx9wdt_stop(wdt_dev);
	txx9wdt_start(wdt_dev);
	return 0;
}

static const struct watchdog_info txx9wdt_info = {
	.options = WDIOF_SETTIMEOUT | WDIOF_KEEPALIVEPING | WDIOF_MAGICCLOSE,
	.identity = "Hardware Watchdog for TXx9",
};

static const struct watchdog_ops txx9wdt_ops = {
	.owner = THIS_MODULE,
	.start = txx9wdt_start,
	.stop = txx9wdt_stop,
	.ping = txx9wdt_ping,
	.set_timeout = txx9wdt_set_timeout,
};

static struct watchdog_device txx9wdt = {
	.info = &txx9wdt_info,
	.ops = &txx9wdt_ops,
};

static int __init txx9wdt_probe(struct platform_device *dev)
{
	struct resource *res;
	int ret;

	txx9_imclk = clk_get(NULL, "imbus_clk");
	if (IS_ERR(txx9_imclk)) {
		ret = PTR_ERR(txx9_imclk);
		txx9_imclk = NULL;
		goto exit;
	}
	ret = clk_enable(txx9_imclk);
	if (ret) {
		clk_put(txx9_imclk);
		txx9_imclk = NULL;
		goto exit;
	}

	res = platform_get_resource(dev, IORESOURCE_MEM, 0);
	txx9wdt_reg = devm_ioremap_resource(&dev->dev, res);
	if (IS_ERR(txx9wdt_reg)) {
		ret = PTR_ERR(txx9wdt_reg);
		goto exit;
	}

	if (timeout < 1 || timeout > WD_MAX_TIMEOUT)
		timeout = TIMER_MARGIN;
	txx9wdt.timeout = timeout;
	txx9wdt.min_timeout = 1;
	txx9wdt.max_timeout = WD_MAX_TIMEOUT;
	watchdog_set_nowayout(&txx9wdt, nowayout);

	ret = watchdog_register_device(&txx9wdt);
	if (ret)
		goto exit;

	pr_info("Hardware Watchdog Timer: timeout=%d sec (max %ld) (nowayout= %d)\n",
		timeout, WD_MAX_TIMEOUT, nowayout);

	return 0;
exit:
	if (txx9_imclk) {
		clk_disable(txx9_imclk);
		clk_put(txx9_imclk);
	}
	return ret;
}

static int __exit txx9wdt_remove(struct platform_device *dev)
{
	watchdog_unregister_device(&txx9wdt);
	clk_disable(txx9_imclk);
	clk_put(txx9_imclk);
	return 0;
}

static void txx9wdt_shutdown(struct platform_device *dev)
{
	txx9wdt_stop(&txx9wdt);
}

static struct platform_driver txx9wdt_driver = {
	.remove = __exit_p(txx9wdt_remove),
	.shutdown = txx9wdt_shutdown,
	.driver = {
		.name = "txx9wdt",
		.owner = THIS_MODULE,
	},
};

module_platform_driver_probe(txx9wdt_driver, txx9wdt_probe);

MODULE_DESCRIPTION("TXx9 Watchdog Driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS_MISCDEV(WATCHDOG_MINOR);
MODULE_ALIAS("platform:txx9wdt");

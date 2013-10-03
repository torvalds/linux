/*
 *  Copyright (C) 2010, Paul Cercueil <paul@crapouillou.net>
 *  JZ4740 Watchdog driver
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under  the terms of the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the License, or (at your
 *  option) any later version.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/miscdevice.h>
#include <linux/watchdog.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/device.h>
#include <linux/clk.h>
#include <linux/slab.h>
#include <linux/err.h>

#include <asm/mach-jz4740/timer.h>

#define JZ_REG_WDT_TIMER_DATA     0x0
#define JZ_REG_WDT_COUNTER_ENABLE 0x4
#define JZ_REG_WDT_TIMER_COUNTER  0x8
#define JZ_REG_WDT_TIMER_CONTROL  0xC

#define JZ_WDT_CLOCK_PCLK 0x1
#define JZ_WDT_CLOCK_RTC  0x2
#define JZ_WDT_CLOCK_EXT  0x4

#define JZ_WDT_CLOCK_DIV_SHIFT   3

#define JZ_WDT_CLOCK_DIV_1    (0 << JZ_WDT_CLOCK_DIV_SHIFT)
#define JZ_WDT_CLOCK_DIV_4    (1 << JZ_WDT_CLOCK_DIV_SHIFT)
#define JZ_WDT_CLOCK_DIV_16   (2 << JZ_WDT_CLOCK_DIV_SHIFT)
#define JZ_WDT_CLOCK_DIV_64   (3 << JZ_WDT_CLOCK_DIV_SHIFT)
#define JZ_WDT_CLOCK_DIV_256  (4 << JZ_WDT_CLOCK_DIV_SHIFT)
#define JZ_WDT_CLOCK_DIV_1024 (5 << JZ_WDT_CLOCK_DIV_SHIFT)

#define DEFAULT_HEARTBEAT 5
#define MAX_HEARTBEAT     2048

static bool nowayout = WATCHDOG_NOWAYOUT;
module_param(nowayout, bool, 0);
MODULE_PARM_DESC(nowayout,
		 "Watchdog cannot be stopped once started (default="
		 __MODULE_STRING(WATCHDOG_NOWAYOUT) ")");

static unsigned int heartbeat = DEFAULT_HEARTBEAT;
module_param(heartbeat, uint, 0);
MODULE_PARM_DESC(heartbeat,
		"Watchdog heartbeat period in seconds from 1 to "
		__MODULE_STRING(MAX_HEARTBEAT) ", default "
		__MODULE_STRING(DEFAULT_HEARTBEAT));

struct jz4740_wdt_drvdata {
	struct watchdog_device wdt;
	void __iomem *base;
	struct clk *rtc_clk;
};

static int jz4740_wdt_ping(struct watchdog_device *wdt_dev)
{
	struct jz4740_wdt_drvdata *drvdata = watchdog_get_drvdata(wdt_dev);

	writew(0x0, drvdata->base + JZ_REG_WDT_TIMER_COUNTER);
	return 0;
}

static int jz4740_wdt_set_timeout(struct watchdog_device *wdt_dev,
				    unsigned int new_timeout)
{
	struct jz4740_wdt_drvdata *drvdata = watchdog_get_drvdata(wdt_dev);
	unsigned int rtc_clk_rate;
	unsigned int timeout_value;
	unsigned short clock_div = JZ_WDT_CLOCK_DIV_1;

	rtc_clk_rate = clk_get_rate(drvdata->rtc_clk);

	timeout_value = rtc_clk_rate * new_timeout;
	while (timeout_value > 0xffff) {
		if (clock_div == JZ_WDT_CLOCK_DIV_1024) {
			/* Requested timeout too high;
			* use highest possible value. */
			timeout_value = 0xffff;
			break;
		}
		timeout_value >>= 2;
		clock_div += (1 << JZ_WDT_CLOCK_DIV_SHIFT);
	}

	writeb(0x0, drvdata->base + JZ_REG_WDT_COUNTER_ENABLE);
	writew(clock_div, drvdata->base + JZ_REG_WDT_TIMER_CONTROL);

	writew((u16)timeout_value, drvdata->base + JZ_REG_WDT_TIMER_DATA);
	writew(0x0, drvdata->base + JZ_REG_WDT_TIMER_COUNTER);
	writew(clock_div | JZ_WDT_CLOCK_RTC,
		drvdata->base + JZ_REG_WDT_TIMER_CONTROL);

	writeb(0x1, drvdata->base + JZ_REG_WDT_COUNTER_ENABLE);

	wdt_dev->timeout = new_timeout;
	return 0;
}

static int jz4740_wdt_start(struct watchdog_device *wdt_dev)
{
	jz4740_timer_enable_watchdog();
	jz4740_wdt_set_timeout(wdt_dev, wdt_dev->timeout);

	return 0;
}

static int jz4740_wdt_stop(struct watchdog_device *wdt_dev)
{
	struct jz4740_wdt_drvdata *drvdata = watchdog_get_drvdata(wdt_dev);

	jz4740_timer_disable_watchdog();
	writeb(0x0, drvdata->base + JZ_REG_WDT_COUNTER_ENABLE);

	return 0;
}

static const struct watchdog_info jz4740_wdt_info = {
	.options = WDIOF_SETTIMEOUT | WDIOF_KEEPALIVEPING | WDIOF_MAGICCLOSE,
	.identity = "jz4740 Watchdog",
};

static const struct watchdog_ops jz4740_wdt_ops = {
	.owner = THIS_MODULE,
	.start = jz4740_wdt_start,
	.stop = jz4740_wdt_stop,
	.ping = jz4740_wdt_ping,
	.set_timeout = jz4740_wdt_set_timeout,
};

static int jz4740_wdt_probe(struct platform_device *pdev)
{
	struct jz4740_wdt_drvdata *drvdata;
	struct watchdog_device *jz4740_wdt;
	struct resource	*res;
	int ret;

	drvdata = devm_kzalloc(&pdev->dev, sizeof(struct jz4740_wdt_drvdata),
			       GFP_KERNEL);
	if (!drvdata) {
		dev_err(&pdev->dev, "Unable to alloacate watchdog device\n");
		return -ENOMEM;
	}

	if (heartbeat < 1 || heartbeat > MAX_HEARTBEAT)
		heartbeat = DEFAULT_HEARTBEAT;

	jz4740_wdt = &drvdata->wdt;
	jz4740_wdt->info = &jz4740_wdt_info;
	jz4740_wdt->ops = &jz4740_wdt_ops;
	jz4740_wdt->timeout = heartbeat;
	jz4740_wdt->min_timeout = 1;
	jz4740_wdt->max_timeout = MAX_HEARTBEAT;
	watchdog_set_nowayout(jz4740_wdt, nowayout);
	watchdog_set_drvdata(jz4740_wdt, drvdata);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	drvdata->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(drvdata->base)) {
		ret = PTR_ERR(drvdata->base);
		goto err_out;
	}

	drvdata->rtc_clk = clk_get(&pdev->dev, "rtc");
	if (IS_ERR(drvdata->rtc_clk)) {
		dev_err(&pdev->dev, "cannot find RTC clock\n");
		ret = PTR_ERR(drvdata->rtc_clk);
		goto err_out;
	}

	ret = watchdog_register_device(&drvdata->wdt);
	if (ret < 0)
		goto err_disable_clk;

	platform_set_drvdata(pdev, drvdata);
	return 0;

err_disable_clk:
	clk_put(drvdata->rtc_clk);
err_out:
	return ret;
}

static int jz4740_wdt_remove(struct platform_device *pdev)
{
	struct jz4740_wdt_drvdata *drvdata = platform_get_drvdata(pdev);

	jz4740_wdt_stop(&drvdata->wdt);
	watchdog_unregister_device(&drvdata->wdt);
	clk_put(drvdata->rtc_clk);

	return 0;
}

static struct platform_driver jz4740_wdt_driver = {
	.probe = jz4740_wdt_probe,
	.remove = jz4740_wdt_remove,
	.driver = {
		.name = "jz4740-wdt",
		.owner	= THIS_MODULE,
	},
};

module_platform_driver(jz4740_wdt_driver);

MODULE_AUTHOR("Paul Cercueil <paul@crapouillou.net>");
MODULE_DESCRIPTION("jz4740 Watchdog Driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS_MISCDEV(WATCHDOG_MINOR);
MODULE_ALIAS("platform:jz4740-wdt");

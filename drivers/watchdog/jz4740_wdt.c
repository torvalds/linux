// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Copyright (C) 2010, Paul Cercueil <paul@crapouillou.net>
 *  JZ4740 Watchdog driver
 */

#include <linux/mfd/ingenic-tcu.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/watchdog.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/device.h>
#include <linux/clk.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/of.h>

#include <asm/mach-jz4740/timer.h>

#define JZ_WDT_CLOCK_PCLK 0x1
#define JZ_WDT_CLOCK_RTC  0x2
#define JZ_WDT_CLOCK_EXT  0x4

#define JZ_WDT_CLOCK_DIV_1    (0 << TCU_TCSR_PRESCALE_LSB)
#define JZ_WDT_CLOCK_DIV_4    (1 << TCU_TCSR_PRESCALE_LSB)
#define JZ_WDT_CLOCK_DIV_16   (2 << TCU_TCSR_PRESCALE_LSB)
#define JZ_WDT_CLOCK_DIV_64   (3 << TCU_TCSR_PRESCALE_LSB)
#define JZ_WDT_CLOCK_DIV_256  (4 << TCU_TCSR_PRESCALE_LSB)
#define JZ_WDT_CLOCK_DIV_1024 (5 << TCU_TCSR_PRESCALE_LSB)

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

	writew(0x0, drvdata->base + TCU_REG_WDT_TCNT);
	return 0;
}

static int jz4740_wdt_set_timeout(struct watchdog_device *wdt_dev,
				    unsigned int new_timeout)
{
	struct jz4740_wdt_drvdata *drvdata = watchdog_get_drvdata(wdt_dev);
	unsigned int rtc_clk_rate;
	unsigned int timeout_value;
	unsigned short clock_div = JZ_WDT_CLOCK_DIV_1;
	u8 tcer;

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
		clock_div += (1 << TCU_TCSR_PRESCALE_LSB);
	}

	tcer = readb(drvdata->base + TCU_REG_WDT_TCER);
	writeb(0x0, drvdata->base + TCU_REG_WDT_TCER);
	writew(clock_div, drvdata->base + TCU_REG_WDT_TCSR);

	writew((u16)timeout_value, drvdata->base + TCU_REG_WDT_TDR);
	writew(0x0, drvdata->base + TCU_REG_WDT_TCNT);
	writew(clock_div | JZ_WDT_CLOCK_RTC, drvdata->base + TCU_REG_WDT_TCSR);

	if (tcer & TCU_WDT_TCER_TCEN)
		writeb(TCU_WDT_TCER_TCEN, drvdata->base + TCU_REG_WDT_TCER);

	wdt_dev->timeout = new_timeout;
	return 0;
}

static int jz4740_wdt_start(struct watchdog_device *wdt_dev)
{
	struct jz4740_wdt_drvdata *drvdata = watchdog_get_drvdata(wdt_dev);
	u8 tcer;

	tcer = readb(drvdata->base + TCU_REG_WDT_TCER);

	jz4740_timer_enable_watchdog();
	jz4740_wdt_set_timeout(wdt_dev, wdt_dev->timeout);

	/* Start watchdog if it wasn't started already */
	if (!(tcer & TCU_WDT_TCER_TCEN))
		writeb(TCU_WDT_TCER_TCEN, drvdata->base + TCU_REG_WDT_TCER);

	return 0;
}

static int jz4740_wdt_stop(struct watchdog_device *wdt_dev)
{
	struct jz4740_wdt_drvdata *drvdata = watchdog_get_drvdata(wdt_dev);

	writeb(0x0, drvdata->base + TCU_REG_WDT_TCER);
	jz4740_timer_disable_watchdog();

	return 0;
}

static int jz4740_wdt_restart(struct watchdog_device *wdt_dev,
			      unsigned long action, void *data)
{
	wdt_dev->timeout = 0;
	jz4740_wdt_start(wdt_dev);
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
	.restart = jz4740_wdt_restart,
};

#ifdef CONFIG_OF
static const struct of_device_id jz4740_wdt_of_matches[] = {
	{ .compatible = "ingenic,jz4740-watchdog", },
	{ .compatible = "ingenic,jz4780-watchdog", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, jz4740_wdt_of_matches);
#endif

static int jz4740_wdt_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct jz4740_wdt_drvdata *drvdata;
	struct watchdog_device *jz4740_wdt;

	drvdata = devm_kzalloc(dev, sizeof(struct jz4740_wdt_drvdata),
			       GFP_KERNEL);
	if (!drvdata)
		return -ENOMEM;

	if (heartbeat < 1 || heartbeat > MAX_HEARTBEAT)
		heartbeat = DEFAULT_HEARTBEAT;

	jz4740_wdt = &drvdata->wdt;
	jz4740_wdt->info = &jz4740_wdt_info;
	jz4740_wdt->ops = &jz4740_wdt_ops;
	jz4740_wdt->timeout = heartbeat;
	jz4740_wdt->min_timeout = 1;
	jz4740_wdt->max_timeout = MAX_HEARTBEAT;
	jz4740_wdt->parent = dev;
	watchdog_set_nowayout(jz4740_wdt, nowayout);
	watchdog_set_drvdata(jz4740_wdt, drvdata);

	drvdata->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(drvdata->base))
		return PTR_ERR(drvdata->base);

	drvdata->rtc_clk = devm_clk_get(dev, "rtc");
	if (IS_ERR(drvdata->rtc_clk)) {
		dev_err(dev, "cannot find RTC clock\n");
		return PTR_ERR(drvdata->rtc_clk);
	}

	return devm_watchdog_register_device(dev, &drvdata->wdt);
}

static struct platform_driver jz4740_wdt_driver = {
	.probe = jz4740_wdt_probe,
	.driver = {
		.name = "jz4740-wdt",
		.of_match_table = of_match_ptr(jz4740_wdt_of_matches),
	},
};

module_platform_driver(jz4740_wdt_driver);

MODULE_AUTHOR("Paul Cercueil <paul@crapouillou.net>");
MODULE_DESCRIPTION("jz4740 Watchdog Driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:jz4740-wdt");

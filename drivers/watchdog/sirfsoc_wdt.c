/*
 * Watchdog driver for CSR SiRFprimaII and SiRFatlasVI
 *
 * Copyright (c) 2013 Cambridge Silicon Radio Limited, a CSR plc group company.
 *
 * Licensed under GPLv2 or later.
 */

#include <linux/module.h>
#include <linux/watchdog.h>
#include <linux/platform_device.h>
#include <linux/moduleparam.h>
#include <linux/of.h>
#include <linux/io.h>
#include <linux/uaccess.h>

#define CLOCK_FREQ	1000000

#define SIRFSOC_TIMER_COUNTER_LO	0x0000
#define SIRFSOC_TIMER_MATCH_0		0x0008
#define SIRFSOC_TIMER_INT_EN		0x0024
#define SIRFSOC_TIMER_WATCHDOG_EN	0x0028
#define SIRFSOC_TIMER_LATCH		0x0030
#define SIRFSOC_TIMER_LATCHED_LO	0x0034

#define SIRFSOC_TIMER_WDT_INDEX		5

#define SIRFSOC_WDT_MIN_TIMEOUT		30		/* 30 secs */
#define SIRFSOC_WDT_MAX_TIMEOUT		(10 * 60)	/* 10 mins */
#define SIRFSOC_WDT_DEFAULT_TIMEOUT	30		/* 30 secs */

static unsigned int timeout;
static bool nowayout = WATCHDOG_NOWAYOUT;

module_param(timeout, uint, 0);
module_param(nowayout, bool, 0);

MODULE_PARM_DESC(timeout, "Default watchdog timeout (in seconds)");
MODULE_PARM_DESC(nowayout, "Watchdog cannot be stopped once started (default="
			__MODULE_STRING(WATCHDOG_NOWAYOUT) ")");

static void __iomem *sirfsoc_wdt_base(struct watchdog_device *wdd)
{
	return (void __iomem __force *)watchdog_get_drvdata(wdd);
}

static unsigned int sirfsoc_wdt_gettimeleft(struct watchdog_device *wdd)
{
	u32 counter, match;
	void __iomem *wdt_base;
	int time_left;

	wdt_base = sirfsoc_wdt_base(wdd);
	counter = readl(wdt_base + SIRFSOC_TIMER_COUNTER_LO);
	match = readl(wdt_base +
		SIRFSOC_TIMER_MATCH_0 + (SIRFSOC_TIMER_WDT_INDEX << 2));

	time_left = match - counter;

	return time_left / CLOCK_FREQ;
}

static int sirfsoc_wdt_updatetimeout(struct watchdog_device *wdd)
{
	u32 counter, timeout_ticks;
	void __iomem *wdt_base;

	timeout_ticks = wdd->timeout * CLOCK_FREQ;
	wdt_base = sirfsoc_wdt_base(wdd);

	/* Enable the latch before reading the LATCH_LO register */
	writel(1, wdt_base + SIRFSOC_TIMER_LATCH);

	/* Set the TO value */
	counter = readl(wdt_base + SIRFSOC_TIMER_LATCHED_LO);

	counter += timeout_ticks;

	writel(counter, wdt_base +
		SIRFSOC_TIMER_MATCH_0 + (SIRFSOC_TIMER_WDT_INDEX << 2));

	return 0;
}

static int sirfsoc_wdt_enable(struct watchdog_device *wdd)
{
	void __iomem *wdt_base = sirfsoc_wdt_base(wdd);
	sirfsoc_wdt_updatetimeout(wdd);

	/*
	 * NOTE: If interrupt is not enabled
	 * then WD-Reset doesn't get generated at all.
	 */
	writel(readl(wdt_base + SIRFSOC_TIMER_INT_EN)
		| (1 << SIRFSOC_TIMER_WDT_INDEX),
		wdt_base + SIRFSOC_TIMER_INT_EN);
	writel(1, wdt_base + SIRFSOC_TIMER_WATCHDOG_EN);

	return 0;
}

static int sirfsoc_wdt_disable(struct watchdog_device *wdd)
{
	void __iomem *wdt_base = sirfsoc_wdt_base(wdd);

	writel(0, wdt_base + SIRFSOC_TIMER_WATCHDOG_EN);
	writel(readl(wdt_base + SIRFSOC_TIMER_INT_EN)
		& (~(1 << SIRFSOC_TIMER_WDT_INDEX)),
		wdt_base + SIRFSOC_TIMER_INT_EN);

	return 0;
}

static int sirfsoc_wdt_settimeout(struct watchdog_device *wdd, unsigned int to)
{
	wdd->timeout = to;
	sirfsoc_wdt_updatetimeout(wdd);

	return 0;
}

#define OPTIONS (WDIOF_SETTIMEOUT | WDIOF_KEEPALIVEPING | WDIOF_MAGICCLOSE)

static const struct watchdog_info sirfsoc_wdt_ident = {
	.options          =     OPTIONS,
	.firmware_version =	0,
	.identity         =	"SiRFSOC Watchdog",
};

static const struct watchdog_ops sirfsoc_wdt_ops = {
	.owner = THIS_MODULE,
	.start = sirfsoc_wdt_enable,
	.stop = sirfsoc_wdt_disable,
	.get_timeleft = sirfsoc_wdt_gettimeleft,
	.ping = sirfsoc_wdt_updatetimeout,
	.set_timeout = sirfsoc_wdt_settimeout,
};

static struct watchdog_device sirfsoc_wdd = {
	.info = &sirfsoc_wdt_ident,
	.ops = &sirfsoc_wdt_ops,
	.timeout = SIRFSOC_WDT_DEFAULT_TIMEOUT,
	.min_timeout = SIRFSOC_WDT_MIN_TIMEOUT,
	.max_timeout = SIRFSOC_WDT_MAX_TIMEOUT,
};

static int sirfsoc_wdt_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	int ret;
	void __iomem *base;

	base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(base))
		return PTR_ERR(base);

	watchdog_set_drvdata(&sirfsoc_wdd, (__force void *)base);

	watchdog_init_timeout(&sirfsoc_wdd, timeout, dev);
	watchdog_set_nowayout(&sirfsoc_wdd, nowayout);
	sirfsoc_wdd.parent = dev;

	watchdog_stop_on_reboot(&sirfsoc_wdd);
	watchdog_stop_on_unregister(&sirfsoc_wdd);
	ret = devm_watchdog_register_device(dev, &sirfsoc_wdd);
	if (ret)
		return ret;

	platform_set_drvdata(pdev, &sirfsoc_wdd);

	return 0;
}

#ifdef	CONFIG_PM_SLEEP
static int sirfsoc_wdt_suspend(struct device *dev)
{
	return 0;
}

static int sirfsoc_wdt_resume(struct device *dev)
{
	struct watchdog_device *wdd = dev_get_drvdata(dev);

	/*
	 * NOTE: Since timer controller registers settings are saved
	 * and restored back by the timer-prima2.c, so we need not
	 * update WD settings except refreshing timeout.
	 */
	sirfsoc_wdt_updatetimeout(wdd);

	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(sirfsoc_wdt_pm_ops,
		sirfsoc_wdt_suspend, sirfsoc_wdt_resume);

static const struct of_device_id sirfsoc_wdt_of_match[] = {
	{ .compatible = "sirf,prima2-tick"},
	{},
};
MODULE_DEVICE_TABLE(of, sirfsoc_wdt_of_match);

static struct platform_driver sirfsoc_wdt_driver = {
	.driver = {
		.name = "sirfsoc-wdt",
		.pm = &sirfsoc_wdt_pm_ops,
		.of_match_table	= sirfsoc_wdt_of_match,
	},
	.probe = sirfsoc_wdt_probe,
};
module_platform_driver(sirfsoc_wdt_driver);

MODULE_DESCRIPTION("SiRF SoC watchdog driver");
MODULE_AUTHOR("Xianglong Du <Xianglong.Du@csr.com>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:sirfsoc-wdt");

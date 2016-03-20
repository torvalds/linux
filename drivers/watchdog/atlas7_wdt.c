/*
 * Watchdog driver for CSR Atlas7
 *
 * Copyright (c) 2015 Cambridge Silicon Radio Limited, a CSR plc group company.
 *
 * Licensed under GPLv2.
 */

#include <linux/clk.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/watchdog.h>

#define ATLAS7_TIMER_WDT_INDEX		5
#define ATLAS7_WDT_DEFAULT_TIMEOUT	20

#define ATLAS7_WDT_CNT_CTRL	(0 + 4 * ATLAS7_TIMER_WDT_INDEX)
#define ATLAS7_WDT_CNT_MATCH	(0x18 + 4 * ATLAS7_TIMER_WDT_INDEX)
#define ATLAS7_WDT_CNT		(0x48 +  4 * ATLAS7_TIMER_WDT_INDEX)
#define ATLAS7_WDT_CNT_EN	(BIT(0) | BIT(1))
#define ATLAS7_WDT_EN		0x64

static unsigned int timeout = ATLAS7_WDT_DEFAULT_TIMEOUT;
static bool nowayout = WATCHDOG_NOWAYOUT;

module_param(timeout, uint, 0);
module_param(nowayout, bool, 0);

MODULE_PARM_DESC(timeout, "Default watchdog timeout (in seconds)");
MODULE_PARM_DESC(nowayout, "Watchdog cannot be stopped once started (default="
			__MODULE_STRING(WATCHDOG_NOWAYOUT) ")");

struct atlas7_wdog {
	struct device *dev;
	void __iomem *base;
	unsigned long tick_rate;
	struct clk *clk;
};

static unsigned int atlas7_wdt_gettimeleft(struct watchdog_device *wdd)
{
	struct atlas7_wdog *wdt = watchdog_get_drvdata(wdd);
	u32 counter, match, delta;

	counter = readl(wdt->base + ATLAS7_WDT_CNT);
	match = readl(wdt->base + ATLAS7_WDT_CNT_MATCH);
	delta = match - counter;

	return  delta / wdt->tick_rate;
}

static int atlas7_wdt_ping(struct watchdog_device *wdd)
{
	struct atlas7_wdog *wdt = watchdog_get_drvdata(wdd);
	u32 counter, match, delta;

	counter = readl(wdt->base + ATLAS7_WDT_CNT);
	delta = wdd->timeout * wdt->tick_rate;
	match = counter + delta;

	writel(match, wdt->base + ATLAS7_WDT_CNT_MATCH);

	return 0;
}

static int atlas7_wdt_enable(struct watchdog_device *wdd)
{
	struct atlas7_wdog *wdt = watchdog_get_drvdata(wdd);

	atlas7_wdt_ping(wdd);

	writel(readl(wdt->base + ATLAS7_WDT_CNT_CTRL) | ATLAS7_WDT_CNT_EN,
	      wdt->base + ATLAS7_WDT_CNT_CTRL);
	writel(1, wdt->base + ATLAS7_WDT_EN);

	return 0;
}

static int atlas7_wdt_disable(struct watchdog_device *wdd)
{
	struct atlas7_wdog *wdt = watchdog_get_drvdata(wdd);

	writel(0, wdt->base + ATLAS7_WDT_EN);
	writel(readl(wdt->base + ATLAS7_WDT_CNT_CTRL) & ~ATLAS7_WDT_CNT_EN,
	      wdt->base + ATLAS7_WDT_CNT_CTRL);

	return 0;
}

static int atlas7_wdt_settimeout(struct watchdog_device *wdd, unsigned int to)
{
	wdd->timeout = to;

	return 0;
}

#define OPTIONS (WDIOF_SETTIMEOUT | WDIOF_KEEPALIVEPING | WDIOF_MAGICCLOSE)

static const struct watchdog_info atlas7_wdt_ident = {
	.options = OPTIONS,
	.firmware_version = 0,
	.identity = "atlas7 Watchdog",
};

static struct watchdog_ops atlas7_wdt_ops = {
	.owner = THIS_MODULE,
	.start = atlas7_wdt_enable,
	.stop = atlas7_wdt_disable,
	.get_timeleft = atlas7_wdt_gettimeleft,
	.ping = atlas7_wdt_ping,
	.set_timeout = atlas7_wdt_settimeout,
};

static struct watchdog_device atlas7_wdd = {
	.info = &atlas7_wdt_ident,
	.ops = &atlas7_wdt_ops,
	.timeout = ATLAS7_WDT_DEFAULT_TIMEOUT,
};

static const struct of_device_id atlas7_wdt_ids[] = {
	{ .compatible = "sirf,atlas7-tick"},
	{}
};

static int atlas7_wdt_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct atlas7_wdog *wdt;
	struct resource *res;
	struct clk *clk;
	int ret;

	wdt = devm_kzalloc(&pdev->dev, sizeof(*wdt), GFP_KERNEL);
	if (!wdt)
		return -ENOMEM;
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	wdt->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(wdt->base))
		return PTR_ERR(wdt->base);

	clk = of_clk_get(np, 0);
	if (IS_ERR(clk))
		return PTR_ERR(clk);
	ret = clk_prepare_enable(clk);
	if (ret) {
		dev_err(&pdev->dev, "clk enable failed\n");
		goto err;
	}

	/* disable watchdog hardware */
	writel(0, wdt->base + ATLAS7_WDT_CNT_CTRL);

	wdt->tick_rate = clk_get_rate(clk);
	if (!wdt->tick_rate) {
		ret = -EINVAL;
		goto err1;
	}

	wdt->clk = clk;
	atlas7_wdd.min_timeout = 1;
	atlas7_wdd.max_timeout = UINT_MAX / wdt->tick_rate;

	watchdog_init_timeout(&atlas7_wdd, 0, &pdev->dev);
	watchdog_set_nowayout(&atlas7_wdd, nowayout);

	watchdog_set_drvdata(&atlas7_wdd, wdt);
	platform_set_drvdata(pdev, &atlas7_wdd);

	ret = watchdog_register_device(&atlas7_wdd);
	if (ret)
		goto err1;

	return 0;

err1:
	clk_disable_unprepare(clk);
err:
	clk_put(clk);
	return ret;
}

static void atlas7_wdt_shutdown(struct platform_device *pdev)
{
	struct watchdog_device *wdd = platform_get_drvdata(pdev);
	struct atlas7_wdog *wdt = watchdog_get_drvdata(wdd);

	atlas7_wdt_disable(wdd);
	clk_disable_unprepare(wdt->clk);
}

static int atlas7_wdt_remove(struct platform_device *pdev)
{
	struct watchdog_device *wdd = platform_get_drvdata(pdev);
	struct atlas7_wdog *wdt = watchdog_get_drvdata(wdd);

	atlas7_wdt_shutdown(pdev);
	clk_put(wdt->clk);
	return 0;
}

static int __maybe_unused atlas7_wdt_suspend(struct device *dev)
{
	/*
	 * NOTE:timer controller registers settings are saved
	 * and restored back by the timer-atlas7.c
	 */
	return 0;
}

static int __maybe_unused atlas7_wdt_resume(struct device *dev)
{
	struct watchdog_device *wdd = dev_get_drvdata(dev);

	/*
	 * NOTE: Since timer controller registers settings are saved
	 * and restored back by the timer-atlas7.c, so we need not
	 * update WD settings except refreshing timeout.
	 */
	atlas7_wdt_ping(wdd);

	return 0;
}

static SIMPLE_DEV_PM_OPS(atlas7_wdt_pm_ops,
		atlas7_wdt_suspend, atlas7_wdt_resume);

MODULE_DEVICE_TABLE(of, atlas7_wdt_ids);

static struct platform_driver atlas7_wdt_driver = {
	.driver = {
		.name = "atlas7-wdt",
		.pm = &atlas7_wdt_pm_ops,
		.of_match_table	= atlas7_wdt_ids,
	},
	.probe = atlas7_wdt_probe,
	.remove = atlas7_wdt_remove,
	.shutdown = atlas7_wdt_shutdown,
};
module_platform_driver(atlas7_wdt_driver);

MODULE_DESCRIPTION("CSRatlas7 watchdog driver");
MODULE_AUTHOR("Guo Zeng <Guo.Zeng@csr.com>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:atlas7-wdt");

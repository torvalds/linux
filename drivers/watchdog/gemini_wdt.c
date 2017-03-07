/*
 * Watchdog driver for Cortina Systems Gemini SoC
 *
 * Copyright (C) 2017 Linus Walleij <linus.walleij@linaro.org>
 *
 * Inspired by the out-of-tree drivers from OpenWRT:
 * Copyright (C) 2009 Paulius Zaleckas <paulius.zaleckas@teltonika.lt>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/bitops.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/watchdog.h>

#define GEMINI_WDCOUNTER	0x0
#define GEMINI_WDLOAD		0x4
#define GEMINI_WDRESTART	0x8
#define GEMINI_WDCR		0xC

#define WDRESTART_MAGIC		0x5AB9

#define WDCR_CLOCK_5MHZ		BIT(4)
#define WDCR_SYS_RST		BIT(1)
#define WDCR_ENABLE		BIT(0)

#define WDT_CLOCK		5000000		/* 5 MHz */

struct gemini_wdt {
	struct watchdog_device	wdd;
	struct device		*dev;
	void __iomem		*base;
};

static inline
struct gemini_wdt *to_gemini_wdt(struct watchdog_device *wdd)
{
	return container_of(wdd, struct gemini_wdt, wdd);
}

static int gemini_wdt_start(struct watchdog_device *wdd)
{
	struct gemini_wdt *gwdt = to_gemini_wdt(wdd);

	writel(wdd->timeout * WDT_CLOCK, gwdt->base + GEMINI_WDLOAD);
	writel(WDRESTART_MAGIC, gwdt->base + GEMINI_WDRESTART);
	/* set clock before enabling */
	writel(WDCR_CLOCK_5MHZ | WDCR_SYS_RST,
			gwdt->base + GEMINI_WDCR);
	writel(WDCR_CLOCK_5MHZ | WDCR_SYS_RST | WDCR_ENABLE,
			gwdt->base + GEMINI_WDCR);

	return 0;
}

static int gemini_wdt_stop(struct watchdog_device *wdd)
{
	struct gemini_wdt *gwdt = to_gemini_wdt(wdd);

	writel(0, gwdt->base + GEMINI_WDCR);

	return 0;
}

static int gemini_wdt_ping(struct watchdog_device *wdd)
{
	struct gemini_wdt *gwdt = to_gemini_wdt(wdd);

	writel(WDRESTART_MAGIC, gwdt->base + GEMINI_WDRESTART);

	return 0;
}

static int gemini_wdt_set_timeout(struct watchdog_device *wdd,
				  unsigned int timeout)
{
	wdd->timeout = timeout;
	if (watchdog_active(wdd))
		gemini_wdt_start(wdd);

	return 0;
}

static irqreturn_t gemini_wdt_interrupt(int irq, void *data)
{
	struct gemini_wdt *gwdt = data;

	watchdog_notify_pretimeout(&gwdt->wdd);

	return IRQ_HANDLED;
}

static const struct watchdog_ops gemini_wdt_ops = {
	.start		= gemini_wdt_start,
	.stop		= gemini_wdt_stop,
	.ping		= gemini_wdt_ping,
	.set_timeout	= gemini_wdt_set_timeout,
	.owner		= THIS_MODULE,
};

static const struct watchdog_info gemini_wdt_info = {
	.options	= WDIOF_KEEPALIVEPING
			| WDIOF_MAGICCLOSE
			| WDIOF_SETTIMEOUT,
	.identity	= KBUILD_MODNAME,
};


static int gemini_wdt_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct resource *res;
	struct gemini_wdt *gwdt;
	unsigned int reg;
	int irq;
	int ret;

	gwdt = devm_kzalloc(dev, sizeof(*gwdt), GFP_KERNEL);
	if (!gwdt)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	gwdt->base = devm_ioremap_resource(dev, res);
	if (IS_ERR(gwdt->base))
		return PTR_ERR(gwdt->base);

	irq = platform_get_irq(pdev, 0);
	if (!irq)
		return -EINVAL;

	gwdt->dev = dev;
	gwdt->wdd.info = &gemini_wdt_info;
	gwdt->wdd.ops = &gemini_wdt_ops;
	gwdt->wdd.min_timeout = 1;
	gwdt->wdd.max_timeout = 0xFFFFFFFF / WDT_CLOCK;
	gwdt->wdd.parent = dev;

	/*
	 * If 'timeout-sec' unspecified in devicetree, assume a 13 second
	 * default.
	 */
	gwdt->wdd.timeout = 13U;
	watchdog_init_timeout(&gwdt->wdd, 0, dev);

	reg = readw(gwdt->base + GEMINI_WDCR);
	if (reg & WDCR_ENABLE) {
		/* Watchdog was enabled by the bootloader, disable it. */
		reg &= ~WDCR_ENABLE;
		writel(reg, gwdt->base + GEMINI_WDCR);
	}

	ret = devm_request_irq(dev, irq, gemini_wdt_interrupt, 0,
			       "watchdog bark", gwdt);
	if (ret)
		return ret;

	ret = devm_watchdog_register_device(dev, &gwdt->wdd);
	if (ret) {
		dev_err(&pdev->dev, "failed to register watchdog\n");
		return ret;
	}

	/* Set up platform driver data */
	platform_set_drvdata(pdev, gwdt);
	dev_info(dev, "Gemini watchdog driver enabled\n");

	return 0;
}

static int __maybe_unused gemini_wdt_suspend(struct device *dev)
{
	struct gemini_wdt *gwdt = dev_get_drvdata(dev);
	unsigned int reg;

	reg = readw(gwdt->base + GEMINI_WDCR);
	reg &= ~WDCR_ENABLE;
	writel(reg, gwdt->base + GEMINI_WDCR);

	return 0;
}

static int __maybe_unused gemini_wdt_resume(struct device *dev)
{
	struct gemini_wdt *gwdt = dev_get_drvdata(dev);
	unsigned int reg;

	if (watchdog_active(&gwdt->wdd)) {
		reg = readw(gwdt->base + GEMINI_WDCR);
		reg |= WDCR_ENABLE;
		writel(reg, gwdt->base + GEMINI_WDCR);
	}

	return 0;
}

static const struct dev_pm_ops gemini_wdt_dev_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(gemini_wdt_suspend,
				gemini_wdt_resume)
};

#ifdef CONFIG_OF
static const struct of_device_id gemini_wdt_match[] = {
	{ .compatible = "cortina,gemini-watchdog" },
	{},
};
MODULE_DEVICE_TABLE(of, gemini_wdt_match);
#endif

static struct platform_driver gemini_wdt_driver = {
	.probe		= gemini_wdt_probe,
	.driver		= {
		.name	= "gemini-wdt",
		.of_match_table = of_match_ptr(gemini_wdt_match),
		.pm = &gemini_wdt_dev_pm_ops,
	},
};
module_platform_driver(gemini_wdt_driver);
MODULE_AUTHOR("Linus Walleij");
MODULE_DESCRIPTION("Watchdog driver for Gemini");
MODULE_LICENSE("GPL");

// SPDX-License-Identifier: GPL-2.0-only
/*
 * drivers/char/watchdog/ixp4xx_wdt.c
 *
 * Watchdog driver for Intel IXP4xx network processors
 *
 * Author: Deepak Saxena <dsaxena@plexity.net>
 * Author: Linus Walleij <linus.walleij@linaro.org>
 *
 * Copyright 2004 (c) MontaVista, Software, Inc.
 * Based on sa1100 driver, Copyright (C) 2000 Oleg Drokin <green@crimea.edu>
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/watchdog.h>
#include <linux/bits.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/soc/ixp4xx/cpu.h>

struct ixp4xx_wdt {
	struct watchdog_device wdd;
	void __iomem *base;
	unsigned long rate;
};

/* Fallback if we do not have a clock for this */
#define IXP4XX_TIMER_FREQ	66666000

/* Registers after the timer registers */
#define IXP4XX_OSWT_OFFSET	0x14  /* Watchdog Timer */
#define IXP4XX_OSWE_OFFSET	0x18  /* Watchdog Enable */
#define IXP4XX_OSWK_OFFSET	0x1C  /* Watchdog Key */
#define IXP4XX_OSST_OFFSET	0x20  /* Timer Status */

#define IXP4XX_OSST_TIMER_WDOG_PEND	0x00000008
#define IXP4XX_OSST_TIMER_WARM_RESET	0x00000010
#define IXP4XX_WDT_KEY			0x0000482E
#define IXP4XX_WDT_RESET_ENABLE		0x00000001
#define IXP4XX_WDT_IRQ_ENABLE		0x00000002
#define IXP4XX_WDT_COUNT_ENABLE		0x00000004

static inline
struct ixp4xx_wdt *to_ixp4xx_wdt(struct watchdog_device *wdd)
{
	return container_of(wdd, struct ixp4xx_wdt, wdd);
}

static int ixp4xx_wdt_start(struct watchdog_device *wdd)
{
	struct ixp4xx_wdt *iwdt = to_ixp4xx_wdt(wdd);

	__raw_writel(IXP4XX_WDT_KEY, iwdt->base + IXP4XX_OSWK_OFFSET);
	__raw_writel(0, iwdt->base + IXP4XX_OSWE_OFFSET);
	__raw_writel(wdd->timeout * iwdt->rate,
		     iwdt->base + IXP4XX_OSWT_OFFSET);
	__raw_writel(IXP4XX_WDT_COUNT_ENABLE | IXP4XX_WDT_RESET_ENABLE,
		     iwdt->base + IXP4XX_OSWE_OFFSET);
	__raw_writel(0, iwdt->base + IXP4XX_OSWK_OFFSET);

	return 0;
}

static int ixp4xx_wdt_stop(struct watchdog_device *wdd)
{
	struct ixp4xx_wdt *iwdt = to_ixp4xx_wdt(wdd);

	__raw_writel(IXP4XX_WDT_KEY, iwdt->base + IXP4XX_OSWK_OFFSET);
	__raw_writel(0, iwdt->base + IXP4XX_OSWE_OFFSET);
	__raw_writel(0, iwdt->base + IXP4XX_OSWK_OFFSET);

	return 0;
}

static int ixp4xx_wdt_set_timeout(struct watchdog_device *wdd,
				  unsigned int timeout)
{
	wdd->timeout = timeout;
	if (watchdog_active(wdd))
		ixp4xx_wdt_start(wdd);

	return 0;
}

static int ixp4xx_wdt_restart(struct watchdog_device *wdd,
                              unsigned long action, void *data)
{
	struct ixp4xx_wdt *iwdt = to_ixp4xx_wdt(wdd);

	__raw_writel(IXP4XX_WDT_KEY, iwdt->base + IXP4XX_OSWK_OFFSET);
	__raw_writel(0, iwdt->base + IXP4XX_OSWT_OFFSET);
	__raw_writel(IXP4XX_WDT_COUNT_ENABLE | IXP4XX_WDT_RESET_ENABLE,
		     iwdt->base + IXP4XX_OSWE_OFFSET);

	return 0;
}

static const struct watchdog_ops ixp4xx_wdt_ops = {
	.start = ixp4xx_wdt_start,
	.stop = ixp4xx_wdt_stop,
	.set_timeout = ixp4xx_wdt_set_timeout,
	.restart = ixp4xx_wdt_restart,
	.owner = THIS_MODULE,
};

static const struct watchdog_info ixp4xx_wdt_info = {
	.options = WDIOF_KEEPALIVEPING
		| WDIOF_MAGICCLOSE
		| WDIOF_SETTIMEOUT,
	.identity = KBUILD_MODNAME,
};

/* Devres-handled clock disablement */
static void ixp4xx_clock_action(void *d)
{
	clk_disable_unprepare(d);
}

static int ixp4xx_wdt_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct ixp4xx_wdt *iwdt;
	struct clk *clk;
	int ret;

	if (!(read_cpuid_id() & 0xf) && !cpu_is_ixp46x()) {
		dev_err(dev, "Rev. A0 IXP42x CPU detected - watchdog disabled\n");
		return -ENODEV;
	}

	iwdt = devm_kzalloc(dev, sizeof(*iwdt), GFP_KERNEL);
	if (!iwdt)
		return -ENOMEM;
	iwdt->base = (void __iomem *)dev->platform_data;

	/*
	 * Retrieve rate from a fixed clock from the device tree if
	 * the parent has that, else use the default clock rate.
	 */
	clk = devm_clk_get(dev->parent, NULL);
	if (!IS_ERR(clk)) {
		ret = clk_prepare_enable(clk);
		if (ret)
			return ret;
		ret = devm_add_action_or_reset(dev, ixp4xx_clock_action, clk);
		if (ret)
			return ret;
		iwdt->rate = clk_get_rate(clk);
	}
	if (!iwdt->rate)
		iwdt->rate = IXP4XX_TIMER_FREQ;

	iwdt->wdd.info = &ixp4xx_wdt_info;
	iwdt->wdd.ops = &ixp4xx_wdt_ops;
	iwdt->wdd.min_timeout = 1;
	iwdt->wdd.max_timeout = U32_MAX / iwdt->rate;
	iwdt->wdd.parent = dev;
	/* Default to 60 seconds */
	iwdt->wdd.timeout = 60U;
	watchdog_init_timeout(&iwdt->wdd, 0, dev);

	if (__raw_readl(iwdt->base + IXP4XX_OSST_OFFSET) &
	    IXP4XX_OSST_TIMER_WARM_RESET)
		iwdt->wdd.bootstatus = WDIOF_CARDRESET;

	ret = devm_watchdog_register_device(dev, &iwdt->wdd);
	if (ret)
		return ret;

	dev_info(dev, "IXP4xx watchdog available\n");

	return 0;
}

static struct platform_driver ixp4xx_wdt_driver = {
	.probe = ixp4xx_wdt_probe,
	.driver = {
		.name   = "ixp4xx-watchdog",
	},
};
module_platform_driver(ixp4xx_wdt_driver);

MODULE_AUTHOR("Deepak Saxena <dsaxena@plexity.net>");
MODULE_DESCRIPTION("IXP4xx Network Processor Watchdog");
MODULE_LICENSE("GPL");

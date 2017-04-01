/*
 *      Meson Watchdog Driver
 *
 *      Copyright (c) 2014 Carlo Caione
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/types.h>
#include <linux/watchdog.h>

#define DRV_NAME		"meson_wdt"

#define MESON_WDT_TC		0x00
#define MESON_WDT_DC_RESET	(3 << 24)

#define MESON_WDT_RESET		0x04

#define MESON_WDT_TIMEOUT	30
#define MESON_WDT_MIN_TIMEOUT	1

#define MESON_SEC_TO_TC(s, c)	((s) * (c))

static bool nowayout = WATCHDOG_NOWAYOUT;
static unsigned int timeout = MESON_WDT_TIMEOUT;

struct meson_wdt_data {
	unsigned int enable;
	unsigned int terminal_count_mask;
	unsigned int count_unit;
};

static struct meson_wdt_data meson6_wdt_data = {
	.enable			= BIT(22),
	.terminal_count_mask	= 0x3fffff,
	.count_unit		= 100000, /* 10 us */
};

static struct meson_wdt_data meson8b_wdt_data = {
	.enable			= BIT(19),
	.terminal_count_mask	= 0xffff,
	.count_unit		= 7812, /* 128 us */
};

struct meson_wdt_dev {
	struct watchdog_device wdt_dev;
	void __iomem *wdt_base;
	const struct meson_wdt_data *data;
};

static int meson_wdt_restart(struct watchdog_device *wdt_dev,
			     unsigned long action, void *data)
{
	struct meson_wdt_dev *meson_wdt = watchdog_get_drvdata(wdt_dev);
	u32 tc_reboot = MESON_WDT_DC_RESET;

	tc_reboot |= meson_wdt->data->enable;

	while (1) {
		writel(tc_reboot, meson_wdt->wdt_base + MESON_WDT_TC);
		mdelay(5);
	}

	return 0;
}

static int meson_wdt_ping(struct watchdog_device *wdt_dev)
{
	struct meson_wdt_dev *meson_wdt = watchdog_get_drvdata(wdt_dev);

	writel(0, meson_wdt->wdt_base + MESON_WDT_RESET);

	return 0;
}

static void meson_wdt_change_timeout(struct watchdog_device *wdt_dev,
				     unsigned int timeout)
{
	struct meson_wdt_dev *meson_wdt = watchdog_get_drvdata(wdt_dev);
	u32 reg;

	reg = readl(meson_wdt->wdt_base + MESON_WDT_TC);
	reg &= ~meson_wdt->data->terminal_count_mask;
	reg |= MESON_SEC_TO_TC(timeout, meson_wdt->data->count_unit);
	writel(reg, meson_wdt->wdt_base + MESON_WDT_TC);
}

static int meson_wdt_set_timeout(struct watchdog_device *wdt_dev,
				 unsigned int timeout)
{
	wdt_dev->timeout = timeout;

	meson_wdt_change_timeout(wdt_dev, timeout);
	meson_wdt_ping(wdt_dev);

	return 0;
}

static int meson_wdt_stop(struct watchdog_device *wdt_dev)
{
	struct meson_wdt_dev *meson_wdt = watchdog_get_drvdata(wdt_dev);
	u32 reg;

	reg = readl(meson_wdt->wdt_base + MESON_WDT_TC);
	reg &= ~meson_wdt->data->enable;
	writel(reg, meson_wdt->wdt_base + MESON_WDT_TC);

	return 0;
}

static int meson_wdt_start(struct watchdog_device *wdt_dev)
{
	struct meson_wdt_dev *meson_wdt = watchdog_get_drvdata(wdt_dev);
	u32 reg;

	meson_wdt_change_timeout(wdt_dev, meson_wdt->wdt_dev.timeout);
	meson_wdt_ping(wdt_dev);

	reg = readl(meson_wdt->wdt_base + MESON_WDT_TC);
	reg |= meson_wdt->data->enable;
	writel(reg, meson_wdt->wdt_base + MESON_WDT_TC);

	return 0;
}

static const struct watchdog_info meson_wdt_info = {
	.identity	= DRV_NAME,
	.options	= WDIOF_SETTIMEOUT |
			  WDIOF_KEEPALIVEPING |
			  WDIOF_MAGICCLOSE,
};

static const struct watchdog_ops meson_wdt_ops = {
	.owner		= THIS_MODULE,
	.start		= meson_wdt_start,
	.stop		= meson_wdt_stop,
	.ping		= meson_wdt_ping,
	.set_timeout	= meson_wdt_set_timeout,
	.restart        = meson_wdt_restart,
};

static const struct of_device_id meson_wdt_dt_ids[] = {
	{ .compatible = "amlogic,meson6-wdt", .data = &meson6_wdt_data },
	{ .compatible = "amlogic,meson8b-wdt", .data = &meson8b_wdt_data },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, meson_wdt_dt_ids);

static int meson_wdt_probe(struct platform_device *pdev)
{
	struct resource *res;
	struct meson_wdt_dev *meson_wdt;
	const struct of_device_id *of_id;
	int err;

	meson_wdt = devm_kzalloc(&pdev->dev, sizeof(*meson_wdt), GFP_KERNEL);
	if (!meson_wdt)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	meson_wdt->wdt_base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(meson_wdt->wdt_base))
		return PTR_ERR(meson_wdt->wdt_base);

	of_id = of_match_device(meson_wdt_dt_ids, &pdev->dev);
	if (!of_id) {
		dev_err(&pdev->dev, "Unable to initialize WDT data\n");
		return -ENODEV;
	}
	meson_wdt->data = of_id->data;

	meson_wdt->wdt_dev.parent = &pdev->dev;
	meson_wdt->wdt_dev.info = &meson_wdt_info;
	meson_wdt->wdt_dev.ops = &meson_wdt_ops;
	meson_wdt->wdt_dev.max_timeout =
		meson_wdt->data->terminal_count_mask / meson_wdt->data->count_unit;
	meson_wdt->wdt_dev.min_timeout = MESON_WDT_MIN_TIMEOUT;
	meson_wdt->wdt_dev.timeout = min_t(unsigned int,
					   MESON_WDT_TIMEOUT,
					   meson_wdt->wdt_dev.max_timeout);

	watchdog_set_drvdata(&meson_wdt->wdt_dev, meson_wdt);

	watchdog_init_timeout(&meson_wdt->wdt_dev, timeout, &pdev->dev);
	watchdog_set_nowayout(&meson_wdt->wdt_dev, nowayout);
	watchdog_set_restart_priority(&meson_wdt->wdt_dev, 128);

	meson_wdt_stop(&meson_wdt->wdt_dev);

	watchdog_stop_on_reboot(&meson_wdt->wdt_dev);
	err = devm_watchdog_register_device(&pdev->dev, &meson_wdt->wdt_dev);
	if (err)
		return err;

	dev_info(&pdev->dev, "Watchdog enabled (timeout=%d sec, nowayout=%d)",
		 meson_wdt->wdt_dev.timeout, nowayout);

	return 0;
}

static struct platform_driver meson_wdt_driver = {
	.probe		= meson_wdt_probe,
	.driver		= {
		.name		= DRV_NAME,
		.of_match_table	= meson_wdt_dt_ids,
	},
};

module_platform_driver(meson_wdt_driver);

module_param(timeout, uint, 0);
MODULE_PARM_DESC(timeout, "Watchdog heartbeat in seconds");

module_param(nowayout, bool, 0);
MODULE_PARM_DESC(nowayout,
		 "Watchdog cannot be stopped once started (default="
		 __MODULE_STRING(WATCHDOG_NOWAYOUT) ")");

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Carlo Caione <carlo@caione.org>");
MODULE_DESCRIPTION("Meson Watchdog Timer Driver");

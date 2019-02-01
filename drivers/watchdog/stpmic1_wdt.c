// SPDX-License-Identifier: GPL-2.0
// Copyright (C) STMicroelectronics 2018
// Author: Pascal Paillet <p.paillet@st.com> for STMicroelectronics.

#include <linux/kernel.h>
#include <linux/mfd/stpmic1.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/watchdog.h>

/* WATCHDOG CONTROL REGISTER bit */
#define WDT_START		BIT(0)
#define WDT_PING		BIT(1)
#define WDT_START_MASK		BIT(0)
#define WDT_PING_MASK		BIT(1)
#define WDT_STOP		0

#define PMIC_WDT_MIN_TIMEOUT 1
#define PMIC_WDT_MAX_TIMEOUT 256
#define PMIC_WDT_DEFAULT_TIMEOUT 30

static bool nowayout = WATCHDOG_NOWAYOUT;
module_param(nowayout, bool, 0);
MODULE_PARM_DESC(nowayout, "Watchdog cannot be stopped once started (default="
		 __MODULE_STRING(WATCHDOG_NOWAYOUT) ")");

struct stpmic1_wdt {
	struct stpmic1 *pmic;
	struct watchdog_device wdtdev;
};

static int pmic_wdt_start(struct watchdog_device *wdd)
{
	struct stpmic1_wdt *wdt = watchdog_get_drvdata(wdd);

	return regmap_update_bits(wdt->pmic->regmap,
				  WCHDG_CR, WDT_START_MASK, WDT_START);
}

static int pmic_wdt_stop(struct watchdog_device *wdd)
{
	struct stpmic1_wdt *wdt = watchdog_get_drvdata(wdd);

	return regmap_update_bits(wdt->pmic->regmap,
				  WCHDG_CR, WDT_START_MASK, WDT_STOP);
}

static int pmic_wdt_ping(struct watchdog_device *wdd)
{
	struct stpmic1_wdt *wdt = watchdog_get_drvdata(wdd);

	return regmap_update_bits(wdt->pmic->regmap,
				  WCHDG_CR, WDT_PING_MASK, WDT_PING);
}

static int pmic_wdt_set_timeout(struct watchdog_device *wdd,
				unsigned int timeout)
{
	struct stpmic1_wdt *wdt = watchdog_get_drvdata(wdd);

	wdd->timeout = timeout;
	/* timeout is equal to register value + 1 */
	return regmap_write(wdt->pmic->regmap, WCHDG_TIMER_CR, timeout - 1);
}

static const struct watchdog_info pmic_watchdog_info = {
	.options = WDIOF_SETTIMEOUT | WDIOF_KEEPALIVEPING | WDIOF_MAGICCLOSE,
	.identity = "STPMIC1 PMIC Watchdog",
};

static const struct watchdog_ops pmic_watchdog_ops = {
	.owner = THIS_MODULE,
	.start = pmic_wdt_start,
	.stop = pmic_wdt_stop,
	.ping = pmic_wdt_ping,
	.set_timeout = pmic_wdt_set_timeout,
};

static int pmic_wdt_probe(struct platform_device *pdev)
{
	int ret;
	struct stpmic1 *pmic;
	struct stpmic1_wdt *wdt;

	if (!pdev->dev.parent)
		return -EINVAL;

	pmic = dev_get_drvdata(pdev->dev.parent);
	if (!pmic)
		return -EINVAL;

	wdt = devm_kzalloc(&pdev->dev, sizeof(struct stpmic1_wdt), GFP_KERNEL);
	if (!wdt)
		return -ENOMEM;

	wdt->pmic = pmic;

	wdt->wdtdev.info = &pmic_watchdog_info;
	wdt->wdtdev.ops = &pmic_watchdog_ops;
	wdt->wdtdev.min_timeout = PMIC_WDT_MIN_TIMEOUT;
	wdt->wdtdev.max_timeout = PMIC_WDT_MAX_TIMEOUT;
	wdt->wdtdev.parent = &pdev->dev;

	wdt->wdtdev.timeout = PMIC_WDT_DEFAULT_TIMEOUT;
	watchdog_init_timeout(&wdt->wdtdev, 0, &pdev->dev);

	watchdog_set_nowayout(&wdt->wdtdev, nowayout);
	watchdog_set_drvdata(&wdt->wdtdev, wdt);

	ret = devm_watchdog_register_device(&pdev->dev, &wdt->wdtdev);
	if (ret)
		return ret;

	dev_dbg(wdt->pmic->dev, "PMIC Watchdog driver probed\n");
	return 0;
}

static const struct of_device_id of_pmic_wdt_match[] = {
	{ .compatible = "st,stpmic1-wdt" },
	{ },
};

MODULE_DEVICE_TABLE(of, of_pmic_wdt_match);

static struct platform_driver stpmic1_wdt_driver = {
	.probe = pmic_wdt_probe,
	.driver = {
		.name = "stpmic1-wdt",
		.of_match_table = of_pmic_wdt_match,
	},
};
module_platform_driver(stpmic1_wdt_driver);

MODULE_DESCRIPTION("Watchdog driver for STPMIC1 device");
MODULE_AUTHOR("Pascal Paillet <p.paillet@st.com>");
MODULE_LICENSE("GPL v2");

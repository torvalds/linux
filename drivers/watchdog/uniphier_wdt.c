// SPDX-License-Identifier: GPL-2.0
/*
 * Watchdog driver for the UniPhier watchdog timer
 *
 * (c) Copyright 2014 Panasonic Corporation
 * (c) Copyright 2016 Socionext Inc.
 * All rights reserved.
 */

#include <linux/bitops.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/watchdog.h>

/* WDT timer setting register */
#define WDTTIMSET			0x3004
#define   WDTTIMSET_PERIOD_MASK		(0xf << 0)
#define   WDTTIMSET_PERIOD_1_SEC	(0x3 << 0)

/* WDT reset selection register */
#define WDTRSTSEL			0x3008
#define   WDTRSTSEL_RSTSEL_MASK		(0x3 << 0)
#define   WDTRSTSEL_RSTSEL_BOTH		(0x0 << 0)
#define   WDTRSTSEL_RSTSEL_IRQ_ONLY	(0x2 << 0)

/* WDT control register */
#define WDTCTRL				0x300c
#define   WDTCTRL_STATUS		BIT(8)
#define   WDTCTRL_CLEAR			BIT(1)
#define   WDTCTRL_ENABLE		BIT(0)

#define SEC_TO_WDTTIMSET_PRD(sec) \
		(ilog2(sec) + WDTTIMSET_PERIOD_1_SEC)

#define WDTST_TIMEOUT			1000 /* usec */

#define WDT_DEFAULT_TIMEOUT		64   /* Default is 64 seconds */
#define WDT_PERIOD_MIN			1
#define WDT_PERIOD_MAX			128

static unsigned int timeout = 0;
static bool nowayout = WATCHDOG_NOWAYOUT;

struct uniphier_wdt_dev {
	struct watchdog_device wdt_dev;
	struct regmap	*regmap;
};

/*
 * UniPhier Watchdog operations
 */
static int uniphier_watchdog_ping(struct watchdog_device *w)
{
	struct uniphier_wdt_dev *wdev = watchdog_get_drvdata(w);
	unsigned int val;
	int ret;

	/* Clear counter */
	ret = regmap_write_bits(wdev->regmap, WDTCTRL,
				WDTCTRL_CLEAR, WDTCTRL_CLEAR);
	if (!ret)
		/*
		 * As SoC specification, after clear counter,
		 * it needs to wait until counter status is 1.
		 */
		ret = regmap_read_poll_timeout(wdev->regmap, WDTCTRL, val,
					       (val & WDTCTRL_STATUS),
					       0, WDTST_TIMEOUT);

	return ret;
}

static int __uniphier_watchdog_start(struct regmap *regmap, unsigned int sec)
{
	unsigned int val;
	int ret;

	ret = regmap_read_poll_timeout(regmap, WDTCTRL, val,
				       !(val & WDTCTRL_STATUS),
				       0, WDTST_TIMEOUT);
	if (ret)
		return ret;

	/* Setup period */
	ret = regmap_write(regmap, WDTTIMSET,
			   SEC_TO_WDTTIMSET_PRD(sec));
	if (ret)
		return ret;

	/* Enable and clear watchdog */
	ret = regmap_write(regmap, WDTCTRL, WDTCTRL_ENABLE | WDTCTRL_CLEAR);
	if (!ret)
		/*
		 * As SoC specification, after clear counter,
		 * it needs to wait until counter status is 1.
		 */
		ret = regmap_read_poll_timeout(regmap, WDTCTRL, val,
					       (val & WDTCTRL_STATUS),
					       0, WDTST_TIMEOUT);

	return ret;
}

static int __uniphier_watchdog_stop(struct regmap *regmap)
{
	/* Disable and stop watchdog */
	return regmap_write_bits(regmap, WDTCTRL, WDTCTRL_ENABLE, 0);
}

static int __uniphier_watchdog_restart(struct regmap *regmap, unsigned int sec)
{
	int ret;

	ret = __uniphier_watchdog_stop(regmap);
	if (ret)
		return ret;

	return __uniphier_watchdog_start(regmap, sec);
}

static int uniphier_watchdog_start(struct watchdog_device *w)
{
	struct uniphier_wdt_dev *wdev = watchdog_get_drvdata(w);
	unsigned int tmp_timeout;

	tmp_timeout = roundup_pow_of_two(w->timeout);

	return __uniphier_watchdog_start(wdev->regmap, tmp_timeout);
}

static int uniphier_watchdog_stop(struct watchdog_device *w)
{
	struct uniphier_wdt_dev *wdev = watchdog_get_drvdata(w);

	return __uniphier_watchdog_stop(wdev->regmap);
}

static int uniphier_watchdog_set_timeout(struct watchdog_device *w,
					 unsigned int t)
{
	struct uniphier_wdt_dev *wdev = watchdog_get_drvdata(w);
	unsigned int tmp_timeout;
	int ret;

	tmp_timeout = roundup_pow_of_two(t);
	if (tmp_timeout == w->timeout)
		return 0;

	if (watchdog_active(w)) {
		ret = __uniphier_watchdog_restart(wdev->regmap, tmp_timeout);
		if (ret)
			return ret;
	}

	w->timeout = tmp_timeout;

	return 0;
}

/*
 * Kernel Interfaces
 */
static const struct watchdog_info uniphier_wdt_info = {
	.identity	= "uniphier-wdt",
	.options	= WDIOF_SETTIMEOUT |
			  WDIOF_KEEPALIVEPING |
			  WDIOF_MAGICCLOSE |
			  WDIOF_OVERHEAT,
};

static const struct watchdog_ops uniphier_wdt_ops = {
	.owner		= THIS_MODULE,
	.start		= uniphier_watchdog_start,
	.stop		= uniphier_watchdog_stop,
	.ping		= uniphier_watchdog_ping,
	.set_timeout	= uniphier_watchdog_set_timeout,
};

static int uniphier_wdt_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct uniphier_wdt_dev *wdev;
	struct regmap *regmap;
	struct device_node *parent;
	int ret;

	wdev = devm_kzalloc(dev, sizeof(*wdev), GFP_KERNEL);
	if (!wdev)
		return -ENOMEM;

	parent = of_get_parent(dev->of_node); /* parent should be syscon node */
	regmap = syscon_node_to_regmap(parent);
	of_node_put(parent);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	wdev->regmap = regmap;
	wdev->wdt_dev.info = &uniphier_wdt_info;
	wdev->wdt_dev.ops = &uniphier_wdt_ops;
	wdev->wdt_dev.max_timeout = WDT_PERIOD_MAX;
	wdev->wdt_dev.min_timeout = WDT_PERIOD_MIN;
	wdev->wdt_dev.timeout = WDT_DEFAULT_TIMEOUT;
	wdev->wdt_dev.parent = dev;

	watchdog_init_timeout(&wdev->wdt_dev, timeout, dev);
	watchdog_set_nowayout(&wdev->wdt_dev, nowayout);
	watchdog_stop_on_reboot(&wdev->wdt_dev);

	watchdog_set_drvdata(&wdev->wdt_dev, wdev);

	uniphier_watchdog_stop(&wdev->wdt_dev);
	ret = regmap_write(wdev->regmap, WDTRSTSEL, WDTRSTSEL_RSTSEL_BOTH);
	if (ret)
		return ret;

	ret = devm_watchdog_register_device(dev, &wdev->wdt_dev);
	if (ret)
		return ret;

	dev_info(dev, "watchdog driver (timeout=%d sec, nowayout=%d)\n",
		 wdev->wdt_dev.timeout, nowayout);

	return 0;
}

static const struct of_device_id uniphier_wdt_dt_ids[] = {
	{ .compatible = "socionext,uniphier-wdt" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, uniphier_wdt_dt_ids);

static struct platform_driver uniphier_wdt_driver = {
	.probe		= uniphier_wdt_probe,
	.driver		= {
		.name		= "uniphier-wdt",
		.of_match_table	= uniphier_wdt_dt_ids,
	},
};

module_platform_driver(uniphier_wdt_driver);

module_param(timeout, uint, 0000);
MODULE_PARM_DESC(timeout,
	"Watchdog timeout seconds in power of 2. (0 < timeout < 128, default="
				__MODULE_STRING(WDT_DEFAULT_TIMEOUT) ")");

module_param(nowayout, bool, 0000);
MODULE_PARM_DESC(nowayout,
	"Watchdog cannot be stopped once started (default="
				__MODULE_STRING(WATCHDOG_NOWAYOUT) ")");

MODULE_AUTHOR("Keiji Hayashibara <hayashibara.keiji@socionext.com>");
MODULE_DESCRIPTION("UniPhier Watchdog Device Driver");
MODULE_LICENSE("GPL v2");

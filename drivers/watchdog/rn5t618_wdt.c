/*
 * Watchdog driver for Ricoh RN5T618 PMIC
 *
 * Copyright (C) 2014 Beniamino Galvani <b.galvani@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/device.h>
#include <linux/mfd/rn5t618.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/watchdog.h>

#define DRIVER_NAME "rn5t618-wdt"

static bool nowayout = WATCHDOG_NOWAYOUT;
static unsigned int timeout;

module_param(timeout, uint, 0);
MODULE_PARM_DESC(timeout, "Initial watchdog timeout in seconds");

module_param(nowayout, bool, 0);
MODULE_PARM_DESC(nowayout, "Watchdog cannot be stopped once started (default="
		 __MODULE_STRING(WATCHDOG_NOWAYOUT) ")");

struct rn5t618_wdt {
	struct watchdog_device wdt_dev;
	struct rn5t618 *rn5t618;
};

/*
 * This array encodes the values of WDOGTIM field for the supported
 * watchdog expiration times. If the watchdog is not accessed before
 * the timer expiration, the PMU generates an interrupt and if the CPU
 * doesn't clear it within one second the system is restarted.
 */
static const struct {
	u8 reg_val;
	unsigned int time;
} rn5t618_wdt_map[] = {
	{ 0, 1 },
	{ 1, 8 },
	{ 2, 32 },
	{ 3, 128 },
};

static int rn5t618_wdt_set_timeout(struct watchdog_device *wdt_dev,
				   unsigned int t)
{
	struct rn5t618_wdt *wdt = watchdog_get_drvdata(wdt_dev);
	int ret, i;

	for (i = 0; i < ARRAY_SIZE(rn5t618_wdt_map); i++) {
		if (rn5t618_wdt_map[i].time + 1 >= t)
			break;
	}

	if (i == ARRAY_SIZE(rn5t618_wdt_map))
		return -EINVAL;

	ret = regmap_update_bits(wdt->rn5t618->regmap, RN5T618_WATCHDOG,
				 RN5T618_WATCHDOG_WDOGTIM_M,
				 rn5t618_wdt_map[i].reg_val);
	if (!ret)
		wdt_dev->timeout = rn5t618_wdt_map[i].time;

	return ret;
}

static int rn5t618_wdt_start(struct watchdog_device *wdt_dev)
{
	struct rn5t618_wdt *wdt = watchdog_get_drvdata(wdt_dev);
	int ret;

	ret = rn5t618_wdt_set_timeout(wdt_dev, wdt_dev->timeout);
	if (ret)
		return ret;

	/* enable repower-on */
	ret = regmap_update_bits(wdt->rn5t618->regmap, RN5T618_REPCNT,
				 RN5T618_REPCNT_REPWRON,
				 RN5T618_REPCNT_REPWRON);
	if (ret)
		return ret;

	/* enable watchdog */
	ret = regmap_update_bits(wdt->rn5t618->regmap, RN5T618_WATCHDOG,
				 RN5T618_WATCHDOG_WDOGEN,
				 RN5T618_WATCHDOG_WDOGEN);
	if (ret)
		return ret;

	/* enable watchdog interrupt */
	return regmap_update_bits(wdt->rn5t618->regmap, RN5T618_PWRIREN,
				  RN5T618_PWRIRQ_IR_WDOG,
				  RN5T618_PWRIRQ_IR_WDOG);
}

static int rn5t618_wdt_stop(struct watchdog_device *wdt_dev)
{
	struct rn5t618_wdt *wdt = watchdog_get_drvdata(wdt_dev);

	return regmap_update_bits(wdt->rn5t618->regmap, RN5T618_WATCHDOG,
				  RN5T618_WATCHDOG_WDOGEN, 0);
}

static int rn5t618_wdt_ping(struct watchdog_device *wdt_dev)
{
	struct rn5t618_wdt *wdt = watchdog_get_drvdata(wdt_dev);
	unsigned int val;
	int ret;

	/* The counter is restarted after a R/W access to watchdog register */
	ret = regmap_read(wdt->rn5t618->regmap, RN5T618_WATCHDOG, &val);
	if (ret)
		return ret;

	ret = regmap_write(wdt->rn5t618->regmap, RN5T618_WATCHDOG, val);
	if (ret)
		return ret;

	/* Clear pending watchdog interrupt */
	return regmap_update_bits(wdt->rn5t618->regmap, RN5T618_PWRIRQ,
				  RN5T618_PWRIRQ_IR_WDOG, 0);
}

static struct watchdog_info rn5t618_wdt_info = {
	.options	= WDIOF_SETTIMEOUT | WDIOF_MAGICCLOSE |
			  WDIOF_KEEPALIVEPING,
	.identity	= DRIVER_NAME,
};

static struct watchdog_ops rn5t618_wdt_ops = {
	.owner          = THIS_MODULE,
	.start          = rn5t618_wdt_start,
	.stop           = rn5t618_wdt_stop,
	.ping           = rn5t618_wdt_ping,
	.set_timeout    = rn5t618_wdt_set_timeout,
};

static int rn5t618_wdt_probe(struct platform_device *pdev)
{
	struct rn5t618 *rn5t618 = dev_get_drvdata(pdev->dev.parent);
	struct rn5t618_wdt *wdt;
	int min_timeout, max_timeout;

	wdt = devm_kzalloc(&pdev->dev, sizeof(struct rn5t618_wdt), GFP_KERNEL);
	if (!wdt)
		return -ENOMEM;

	min_timeout = rn5t618_wdt_map[0].time;
	max_timeout = rn5t618_wdt_map[ARRAY_SIZE(rn5t618_wdt_map) - 1].time;

	wdt->rn5t618 = rn5t618;
	wdt->wdt_dev.info = &rn5t618_wdt_info;
	wdt->wdt_dev.ops = &rn5t618_wdt_ops;
	wdt->wdt_dev.min_timeout = min_timeout;
	wdt->wdt_dev.max_timeout = max_timeout;
	wdt->wdt_dev.timeout = max_timeout;
	wdt->wdt_dev.parent = &pdev->dev;

	watchdog_set_drvdata(&wdt->wdt_dev, wdt);
	watchdog_init_timeout(&wdt->wdt_dev, timeout, &pdev->dev);
	watchdog_set_nowayout(&wdt->wdt_dev, nowayout);

	platform_set_drvdata(pdev, wdt);

	return watchdog_register_device(&wdt->wdt_dev);
}

static int rn5t618_wdt_remove(struct platform_device *pdev)
{
	struct rn5t618_wdt *wdt = platform_get_drvdata(pdev);

	watchdog_unregister_device(&wdt->wdt_dev);

	return 0;
}

static struct platform_driver rn5t618_wdt_driver = {
	.probe = rn5t618_wdt_probe,
	.remove = rn5t618_wdt_remove,
	.driver = {
		.name	= DRIVER_NAME,
	},
};

module_platform_driver(rn5t618_wdt_driver);

MODULE_AUTHOR("Beniamino Galvani <b.galvani@gmail.com>");
MODULE_DESCRIPTION("RN5T618 watchdog driver");
MODULE_LICENSE("GPL v2");

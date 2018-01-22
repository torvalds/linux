/*
 * Watchdog driver for DA9063 PMICs.
 *
 * Copyright(c) 2012 Dialog Semiconductor Ltd.
 *
 * Author: Mariusz Wojtasik <mariusz.wojtasik@diasemi.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/watchdog.h>
#include <linux/platform_device.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/mfd/da9063/registers.h>
#include <linux/mfd/da9063/core.h>
#include <linux/regmap.h>

/*
 * Watchdog selector to timeout in seconds.
 *   0: WDT disabled;
 *   others: timeout = 2048 ms * 2^(TWDSCALE-1).
 */
static const unsigned int wdt_timeout[] = { 0, 2, 4, 8, 16, 32, 65, 131 };
#define DA9063_TWDSCALE_DISABLE		0
#define DA9063_TWDSCALE_MIN		1
#define DA9063_TWDSCALE_MAX		(ARRAY_SIZE(wdt_timeout) - 1)
#define DA9063_WDT_MIN_TIMEOUT		wdt_timeout[DA9063_TWDSCALE_MIN]
#define DA9063_WDT_MAX_TIMEOUT		wdt_timeout[DA9063_TWDSCALE_MAX]
#define DA9063_WDG_TIMEOUT		wdt_timeout[3]
#define DA9063_RESET_PROTECTION_MS	256

static unsigned int da9063_wdt_timeout_to_sel(unsigned int secs)
{
	unsigned int i;

	for (i = DA9063_TWDSCALE_MIN; i <= DA9063_TWDSCALE_MAX; i++) {
		if (wdt_timeout[i] >= secs)
			return i;
	}

	return DA9063_TWDSCALE_MAX;
}

static int _da9063_wdt_set_timeout(struct da9063 *da9063, unsigned int regval)
{
	return regmap_update_bits(da9063->regmap, DA9063_REG_CONTROL_D,
				  DA9063_TWDSCALE_MASK, regval);
}

static int da9063_wdt_start(struct watchdog_device *wdd)
{
	struct da9063 *da9063 = watchdog_get_drvdata(wdd);
	unsigned int selector;
	int ret;

	selector = da9063_wdt_timeout_to_sel(wdd->timeout);
	ret = _da9063_wdt_set_timeout(da9063, selector);
	if (ret)
		dev_err(da9063->dev, "Watchdog failed to start (err = %d)\n",
			ret);

	return ret;
}

static int da9063_wdt_stop(struct watchdog_device *wdd)
{
	struct da9063 *da9063 = watchdog_get_drvdata(wdd);
	int ret;

	ret = regmap_update_bits(da9063->regmap, DA9063_REG_CONTROL_D,
				 DA9063_TWDSCALE_MASK, DA9063_TWDSCALE_DISABLE);
	if (ret)
		dev_alert(da9063->dev, "Watchdog failed to stop (err = %d)\n",
			  ret);

	return ret;
}

static int da9063_wdt_ping(struct watchdog_device *wdd)
{
	struct da9063 *da9063 = watchdog_get_drvdata(wdd);
	int ret;

	ret = regmap_write(da9063->regmap, DA9063_REG_CONTROL_F,
			   DA9063_WATCHDOG);
	if (ret)
		dev_alert(da9063->dev, "Failed to ping the watchdog (err = %d)\n",
			  ret);

	return ret;
}

static int da9063_wdt_set_timeout(struct watchdog_device *wdd,
				  unsigned int timeout)
{
	struct da9063 *da9063 = watchdog_get_drvdata(wdd);
	unsigned int selector;
	int ret;

	selector = da9063_wdt_timeout_to_sel(timeout);
	ret = _da9063_wdt_set_timeout(da9063, selector);
	if (ret)
		dev_err(da9063->dev, "Failed to set watchdog timeout (err = %d)\n",
			ret);
	else
		wdd->timeout = wdt_timeout[selector];

	return ret;
}

static int da9063_wdt_restart(struct watchdog_device *wdd, unsigned long action,
			      void *data)
{
	struct da9063 *da9063 = watchdog_get_drvdata(wdd);
	int ret;

	ret = regmap_write(da9063->regmap, DA9063_REG_CONTROL_F,
			   DA9063_SHUTDOWN);
	if (ret)
		dev_alert(da9063->dev, "Failed to shutdown (err = %d)\n",
			  ret);

	return ret;
}

static const struct watchdog_info da9063_watchdog_info = {
	.options = WDIOF_SETTIMEOUT | WDIOF_KEEPALIVEPING,
	.identity = "DA9063 Watchdog",
};

static const struct watchdog_ops da9063_watchdog_ops = {
	.owner = THIS_MODULE,
	.start = da9063_wdt_start,
	.stop = da9063_wdt_stop,
	.ping = da9063_wdt_ping,
	.set_timeout = da9063_wdt_set_timeout,
	.restart = da9063_wdt_restart,
};

static int da9063_wdt_probe(struct platform_device *pdev)
{
	struct da9063 *da9063;
	struct watchdog_device *wdd;

	if (!pdev->dev.parent)
		return -EINVAL;

	da9063 = dev_get_drvdata(pdev->dev.parent);
	if (!da9063)
		return -EINVAL;

	wdd = devm_kzalloc(&pdev->dev, sizeof(*wdd), GFP_KERNEL);
	if (!wdd)
		return -ENOMEM;

	wdd->info = &da9063_watchdog_info;
	wdd->ops = &da9063_watchdog_ops;
	wdd->min_timeout = DA9063_WDT_MIN_TIMEOUT;
	wdd->max_timeout = DA9063_WDT_MAX_TIMEOUT;
	wdd->min_hw_heartbeat_ms = DA9063_RESET_PROTECTION_MS;
	wdd->timeout = DA9063_WDG_TIMEOUT;
	wdd->parent = &pdev->dev;

	wdd->status = WATCHDOG_NOWAYOUT_INIT_STATUS;

	watchdog_set_restart_priority(wdd, 128);

	watchdog_set_drvdata(wdd, da9063);

	return devm_watchdog_register_device(&pdev->dev, wdd);
}

static struct platform_driver da9063_wdt_driver = {
	.probe = da9063_wdt_probe,
	.driver = {
		.name = DA9063_DRVNAME_WATCHDOG,
	},
};
module_platform_driver(da9063_wdt_driver);

MODULE_AUTHOR("Mariusz Wojtasik <mariusz.wojtasik@diasemi.com>");
MODULE_DESCRIPTION("Watchdog driver for Dialog DA9063");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:" DA9063_DRVNAME_WATCHDOG);

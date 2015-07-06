/*
 * da9062_wdt.c - WDT device driver for DA9062
 * Copyright (C) 2015  Dialog Semiconductor Ltd.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/watchdog.h>
#include <linux/platform_device.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/jiffies.h>
#include <linux/mfd/da9062/registers.h>
#include <linux/mfd/da9062/core.h>
#include <linux/regmap.h>
#include <linux/of.h>

static const unsigned int wdt_timeout[] = { 0, 2, 4, 8, 16, 32, 65, 131 };
#define DA9062_TWDSCALE_DISABLE		0
#define DA9062_TWDSCALE_MIN		1
#define DA9062_TWDSCALE_MAX		(ARRAY_SIZE(wdt_timeout) - 1)
#define DA9062_WDT_MIN_TIMEOUT		wdt_timeout[DA9062_TWDSCALE_MIN]
#define DA9062_WDT_MAX_TIMEOUT		wdt_timeout[DA9062_TWDSCALE_MAX]
#define DA9062_WDG_DEFAULT_TIMEOUT	wdt_timeout[DA9062_TWDSCALE_MAX-1]
#define DA9062_RESET_PROTECTION_MS	300

struct da9062_watchdog {
	struct da9062 *hw;
	struct watchdog_device wdtdev;
	unsigned long j_time_stamp;
};

static void da9062_set_window_start(struct da9062_watchdog *wdt)
{
	wdt->j_time_stamp = jiffies;
}

static void da9062_apply_window_protection(struct da9062_watchdog *wdt)
{
	unsigned long delay = msecs_to_jiffies(DA9062_RESET_PROTECTION_MS);
	unsigned long timeout = wdt->j_time_stamp + delay;
	unsigned long now = jiffies;
	unsigned int diff_ms;

	/* if time-limit has not elapsed then wait for remainder */
	if (time_before(now, timeout)) {
		diff_ms = jiffies_to_msecs(timeout-now);
		dev_dbg(wdt->hw->dev,
			"Kicked too quickly. Delaying %u msecs\n", diff_ms);
		msleep(diff_ms);
	}
}

static unsigned int da9062_wdt_timeout_to_sel(unsigned int secs)
{
	unsigned int i;

	for (i = DA9062_TWDSCALE_MIN; i <= DA9062_TWDSCALE_MAX; i++) {
		if (wdt_timeout[i] >= secs)
			return i;
	}

	return DA9062_TWDSCALE_MAX;
}

static int da9062_reset_watchdog_timer(struct da9062_watchdog *wdt)
{
	int ret;

	da9062_apply_window_protection(wdt);

	ret = regmap_update_bits(wdt->hw->regmap,
			   DA9062AA_CONTROL_F,
			   DA9062AA_WATCHDOG_MASK,
			   DA9062AA_WATCHDOG_MASK);

	da9062_set_window_start(wdt);

	return ret;
}

static int da9062_wdt_update_timeout_register(struct da9062_watchdog *wdt,
					      unsigned int regval)
{
	struct da9062 *chip = wdt->hw;
	int ret;

	ret = da9062_reset_watchdog_timer(wdt);
	if (ret)
		return ret;

	return regmap_update_bits(chip->regmap,
				  DA9062AA_CONTROL_D,
				  DA9062AA_TWDSCALE_MASK,
				  regval);
}

static int da9062_wdt_start(struct watchdog_device *wdd)
{
	struct da9062_watchdog *wdt = watchdog_get_drvdata(wdd);
	unsigned int selector;
	int ret;

	selector = da9062_wdt_timeout_to_sel(wdt->wdtdev.timeout);
	ret = da9062_wdt_update_timeout_register(wdt, selector);
	if (ret)
		dev_err(wdt->hw->dev, "Watchdog failed to start (err = %d)\n",
			ret);

	return ret;
}

static int da9062_wdt_stop(struct watchdog_device *wdd)
{
	struct da9062_watchdog *wdt = watchdog_get_drvdata(wdd);
	int ret;

	ret = da9062_reset_watchdog_timer(wdt);
	if (ret) {
		dev_err(wdt->hw->dev, "Failed to ping the watchdog (err = %d)\n",
			ret);
		return ret;
	}

	ret = regmap_update_bits(wdt->hw->regmap,
				 DA9062AA_CONTROL_D,
				 DA9062AA_TWDSCALE_MASK,
				 DA9062_TWDSCALE_DISABLE);
	if (ret)
		dev_err(wdt->hw->dev, "Watchdog failed to stop (err = %d)\n",
			ret);

	return ret;
}

static int da9062_wdt_ping(struct watchdog_device *wdd)
{
	struct da9062_watchdog *wdt = watchdog_get_drvdata(wdd);
	int ret;

	ret = da9062_reset_watchdog_timer(wdt);
	if (ret)
		dev_err(wdt->hw->dev, "Failed to ping the watchdog (err = %d)\n",
			ret);

	return ret;
}

static int da9062_wdt_set_timeout(struct watchdog_device *wdd,
				  unsigned int timeout)
{
	struct da9062_watchdog *wdt = watchdog_get_drvdata(wdd);
	unsigned int selector;
	int ret;

	selector = da9062_wdt_timeout_to_sel(timeout);
	ret = da9062_wdt_update_timeout_register(wdt, selector);
	if (ret)
		dev_err(wdt->hw->dev, "Failed to set watchdog timeout (err = %d)\n",
			ret);
	else
		wdd->timeout = wdt_timeout[selector];

	return ret;
}

static const struct watchdog_info da9062_watchdog_info = {
	.options = WDIOF_SETTIMEOUT | WDIOF_KEEPALIVEPING,
	.identity = "DA9062 WDT",
};

static const struct watchdog_ops da9062_watchdog_ops = {
	.owner = THIS_MODULE,
	.start = da9062_wdt_start,
	.stop = da9062_wdt_stop,
	.ping = da9062_wdt_ping,
	.set_timeout = da9062_wdt_set_timeout,
};

static int da9062_wdt_probe(struct platform_device *pdev)
{
	int ret;
	struct da9062 *chip;
	struct da9062_watchdog *wdt;

	chip = dev_get_drvdata(pdev->dev.parent);
	if (!chip)
		return -EINVAL;

	wdt = devm_kzalloc(&pdev->dev, sizeof(*wdt), GFP_KERNEL);
	if (!wdt)
		return -ENOMEM;

	wdt->hw = chip;

	wdt->wdtdev.info = &da9062_watchdog_info;
	wdt->wdtdev.ops = &da9062_watchdog_ops;
	wdt->wdtdev.min_timeout = DA9062_WDT_MIN_TIMEOUT;
	wdt->wdtdev.max_timeout = DA9062_WDT_MAX_TIMEOUT;
	wdt->wdtdev.timeout = DA9062_WDG_DEFAULT_TIMEOUT;
	wdt->wdtdev.status = WATCHDOG_NOWAYOUT_INIT_STATUS;

	watchdog_set_drvdata(&wdt->wdtdev, wdt);
	dev_set_drvdata(&pdev->dev, wdt);

	ret = watchdog_register_device(&wdt->wdtdev);
	if (ret < 0) {
		dev_err(wdt->hw->dev,
			"watchdog registration failed (%d)\n", ret);
		return ret;
	}

	da9062_set_window_start(wdt);

	ret = da9062_wdt_ping(&wdt->wdtdev);
	if (ret < 0)
		watchdog_unregister_device(&wdt->wdtdev);

	return ret;
}

static int da9062_wdt_remove(struct platform_device *pdev)
{
	struct da9062_watchdog *wdt = dev_get_drvdata(&pdev->dev);

	watchdog_unregister_device(&wdt->wdtdev);
	return 0;
}

static struct platform_driver da9062_wdt_driver = {
	.probe = da9062_wdt_probe,
	.remove = da9062_wdt_remove,
	.driver = {
		.name = "da9062-watchdog",
	},
};
module_platform_driver(da9062_wdt_driver);

MODULE_AUTHOR("S Twiss <stwiss.opensource@diasemi.com>");
MODULE_DESCRIPTION("WDT device driver for Dialog DA9062");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:da9062-watchdog");

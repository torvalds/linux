// SPDX-License-Identifier: GPL-2.0+
/*
 * Watchdog device driver for DA9062 and DA9061 PMICs
 * Copyright (C) 2015  Dialog Semiconductor Ltd.
 *
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
};

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
	return regmap_update_bits(wdt->hw->regmap, DA9062AA_CONTROL_F,
				  DA9062AA_WATCHDOG_MASK,
				  DA9062AA_WATCHDOG_MASK);
}

static int da9062_wdt_update_timeout_register(struct da9062_watchdog *wdt,
					      unsigned int regval)
{
	struct da9062 *chip = wdt->hw;
	int ret;

	ret = da9062_reset_watchdog_timer(wdt);
	if (ret)
		return ret;

	regmap_update_bits(chip->regmap,
				  DA9062AA_CONTROL_D,
				  DA9062AA_TWDSCALE_MASK,
				  DA9062_TWDSCALE_DISABLE);

	usleep_range(150, 300);

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

static int da9062_wdt_restart(struct watchdog_device *wdd, unsigned long action,
			      void *data)
{
	struct da9062_watchdog *wdt = watchdog_get_drvdata(wdd);
	int ret;

	ret = regmap_write(wdt->hw->regmap,
			   DA9062AA_CONTROL_F,
			   DA9062AA_SHUTDOWN_MASK);
	if (ret)
		dev_alert(wdt->hw->dev, "Failed to shutdown (err = %d)\n",
			  ret);

	/* wait for reset to assert... */
	mdelay(500);

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
	.restart = da9062_wdt_restart,
};

static const struct of_device_id da9062_compatible_id_table[] = {
	{ .compatible = "dlg,da9062-watchdog", },
	{ },
};

MODULE_DEVICE_TABLE(of, da9062_compatible_id_table);

static int da9062_wdt_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	int ret;
	struct da9062 *chip;
	struct da9062_watchdog *wdt;

	chip = dev_get_drvdata(dev->parent);
	if (!chip)
		return -EINVAL;

	wdt = devm_kzalloc(dev, sizeof(*wdt), GFP_KERNEL);
	if (!wdt)
		return -ENOMEM;

	wdt->hw = chip;

	wdt->wdtdev.info = &da9062_watchdog_info;
	wdt->wdtdev.ops = &da9062_watchdog_ops;
	wdt->wdtdev.min_timeout = DA9062_WDT_MIN_TIMEOUT;
	wdt->wdtdev.max_timeout = DA9062_WDT_MAX_TIMEOUT;
	wdt->wdtdev.min_hw_heartbeat_ms = DA9062_RESET_PROTECTION_MS;
	wdt->wdtdev.timeout = DA9062_WDG_DEFAULT_TIMEOUT;
	wdt->wdtdev.status = WATCHDOG_NOWAYOUT_INIT_STATUS;
	wdt->wdtdev.parent = dev;

	watchdog_set_restart_priority(&wdt->wdtdev, 128);

	watchdog_set_drvdata(&wdt->wdtdev, wdt);

	ret = devm_watchdog_register_device(dev, &wdt->wdtdev);
	if (ret < 0)
		return ret;

	return da9062_wdt_ping(&wdt->wdtdev);
}

static struct platform_driver da9062_wdt_driver = {
	.probe = da9062_wdt_probe,
	.driver = {
		.name = "da9062-watchdog",
		.of_match_table = da9062_compatible_id_table,
	},
};
module_platform_driver(da9062_wdt_driver);

MODULE_AUTHOR("S Twiss <stwiss.opensource@diasemi.com>");
MODULE_DESCRIPTION("WDT device driver for Dialog DA9062 and DA9061");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:da9062-watchdog");

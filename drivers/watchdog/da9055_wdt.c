// SPDX-License-Identifier: GPL-2.0+
/*
 * System monitoring driver for DA9055 PMICs.
 *
 * Copyright(c) 2012 Dialog Semiconductor Ltd.
 *
 * Author: David Dajun Chen <dchen@diasemi.com>
 *
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/watchdog.h>
#include <linux/delay.h>

#include <linux/mfd/da9055/core.h>
#include <linux/mfd/da9055/reg.h>

static bool nowayout = WATCHDOG_NOWAYOUT;
module_param(nowayout, bool, 0);
MODULE_PARM_DESC(nowayout,
		 "Watchdog cannot be stopped once started (default="
		 __MODULE_STRING(WATCHDOG_NOWAYOUT) ")");

#define DA9055_DEF_TIMEOUT	4
#define DA9055_TWDMIN		256

struct da9055_wdt_data {
	struct watchdog_device wdt;
	struct da9055 *da9055;
};

static const struct {
	u8 reg_val;
	int user_time;  /* In seconds */
} da9055_wdt_maps[] = {
	{ 0, 0 },
	{ 1, 2 },
	{ 2, 4 },
	{ 3, 8 },
	{ 4, 16 },
	{ 5, 32 },
	{ 5, 33 },  /* Actual time  32.768s so included both 32s and 33s */
	{ 6, 65 },
	{ 6, 66 },  /* Actual time 65.536s so include both, 65s and 66s */
	{ 7, 131 },
};

static int da9055_wdt_set_timeout(struct watchdog_device *wdt_dev,
				  unsigned int timeout)
{
	struct da9055_wdt_data *driver_data = watchdog_get_drvdata(wdt_dev);
	struct da9055 *da9055 = driver_data->da9055;
	int ret, i;

	for (i = 0; i < ARRAY_SIZE(da9055_wdt_maps); i++)
		if (da9055_wdt_maps[i].user_time == timeout)
			break;

	if (i == ARRAY_SIZE(da9055_wdt_maps))
		ret = -EINVAL;
	else
		ret = da9055_reg_update(da9055, DA9055_REG_CONTROL_B,
					DA9055_TWDSCALE_MASK,
					da9055_wdt_maps[i].reg_val <<
					DA9055_TWDSCALE_SHIFT);
	if (ret < 0) {
		dev_err(da9055->dev,
			"Failed to update timescale bit, %d\n", ret);
		return ret;
	}

	wdt_dev->timeout = timeout;

	return 0;
}

static int da9055_wdt_ping(struct watchdog_device *wdt_dev)
{
	struct da9055_wdt_data *driver_data = watchdog_get_drvdata(wdt_dev);
	struct da9055 *da9055 = driver_data->da9055;

	/*
	 * We have a minimum time for watchdog window called TWDMIN. A write
	 * to the watchdog before this elapsed time will cause an error.
	 */
	mdelay(DA9055_TWDMIN);

	/* Reset the watchdog timer */
	return da9055_reg_update(da9055, DA9055_REG_CONTROL_E,
				 DA9055_WATCHDOG_MASK, 1);
}

static int da9055_wdt_start(struct watchdog_device *wdt_dev)
{
	return da9055_wdt_set_timeout(wdt_dev, wdt_dev->timeout);
}

static int da9055_wdt_stop(struct watchdog_device *wdt_dev)
{
	return da9055_wdt_set_timeout(wdt_dev, 0);
}

static const struct watchdog_info da9055_wdt_info = {
	.options	= WDIOF_SETTIMEOUT | WDIOF_KEEPALIVEPING,
	.identity	= "DA9055 Watchdog",
};

static const struct watchdog_ops da9055_wdt_ops = {
	.owner = THIS_MODULE,
	.start = da9055_wdt_start,
	.stop = da9055_wdt_stop,
	.ping = da9055_wdt_ping,
	.set_timeout = da9055_wdt_set_timeout,
};

static int da9055_wdt_probe(struct platform_device *pdev)
{
	struct da9055 *da9055 = dev_get_drvdata(pdev->dev.parent);
	struct da9055_wdt_data *driver_data;
	struct watchdog_device *da9055_wdt;
	int ret;

	driver_data = devm_kzalloc(&pdev->dev, sizeof(*driver_data),
				   GFP_KERNEL);
	if (!driver_data)
		return -ENOMEM;

	driver_data->da9055 = da9055;

	da9055_wdt = &driver_data->wdt;

	da9055_wdt->timeout = DA9055_DEF_TIMEOUT;
	da9055_wdt->info = &da9055_wdt_info;
	da9055_wdt->ops = &da9055_wdt_ops;
	da9055_wdt->parent = &pdev->dev;
	watchdog_set_nowayout(da9055_wdt, nowayout);
	watchdog_set_drvdata(da9055_wdt, driver_data);

	ret = da9055_wdt_stop(da9055_wdt);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to stop watchdog, %d\n", ret);
		return ret;
	}

	ret = devm_watchdog_register_device(&pdev->dev, &driver_data->wdt);
	if (ret != 0)
		dev_err(da9055->dev, "watchdog_register_device() failed: %d\n",
			ret);

	return ret;
}

static struct platform_driver da9055_wdt_driver = {
	.probe = da9055_wdt_probe,
	.driver = {
		.name	= "da9055-watchdog",
	},
};

module_platform_driver(da9055_wdt_driver);

MODULE_AUTHOR("David Dajun Chen <dchen@diasemi.com>");
MODULE_DESCRIPTION("DA9055 watchdog");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:da9055-watchdog");

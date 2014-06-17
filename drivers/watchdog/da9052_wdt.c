/*
 * System monitoring driver for DA9052 PMICs.
 *
 * Copyright(c) 2012 Dialog Semiconductor Ltd.
 *
 * Author: Anthony Olech <Anthony.Olech@diasemi.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/uaccess.h>
#include <linux/platform_device.h>
#include <linux/time.h>
#include <linux/watchdog.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/jiffies.h>

#include <linux/mfd/da9052/reg.h>
#include <linux/mfd/da9052/da9052.h>

#define DA9052_DEF_TIMEOUT	4
#define DA9052_TWDMIN		256

struct da9052_wdt_data {
	struct watchdog_device wdt;
	struct da9052 *da9052;
	struct kref kref;
	unsigned long jpast;
};

static const struct {
	u8 reg_val;
	int time;  /* Seconds */
} da9052_wdt_maps[] = {
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


static void da9052_wdt_release_resources(struct kref *r)
{
}

static int da9052_wdt_set_timeout(struct watchdog_device *wdt_dev,
				  unsigned int timeout)
{
	struct da9052_wdt_data *driver_data = watchdog_get_drvdata(wdt_dev);
	struct da9052 *da9052 = driver_data->da9052;
	int ret, i;

	/*
	 * Disable the Watchdog timer before setting
	 * new time out.
	 */
	ret = da9052_reg_update(da9052, DA9052_CONTROL_D_REG,
				DA9052_CONTROLD_TWDSCALE, 0);
	if (ret < 0) {
		dev_err(da9052->dev, "Failed to disable watchdog bit, %d\n",
			ret);
		return ret;
	}
	if (timeout) {
		/*
		 * To change the timeout, da9052 needs to
		 * be disabled for at least 150 us.
		 */
		udelay(150);

		/* Set the desired timeout */
		for (i = 0; i < ARRAY_SIZE(da9052_wdt_maps); i++)
			if (da9052_wdt_maps[i].time == timeout)
				break;

		if (i == ARRAY_SIZE(da9052_wdt_maps))
			ret = -EINVAL;
		else
			ret = da9052_reg_update(da9052, DA9052_CONTROL_D_REG,
						DA9052_CONTROLD_TWDSCALE,
						da9052_wdt_maps[i].reg_val);
		if (ret < 0) {
			dev_err(da9052->dev,
				"Failed to update timescale bit, %d\n", ret);
			return ret;
		}

		wdt_dev->timeout = timeout;
		driver_data->jpast = jiffies;
	}

	return 0;
}

static void da9052_wdt_ref(struct watchdog_device *wdt_dev)
{
	struct da9052_wdt_data *driver_data = watchdog_get_drvdata(wdt_dev);

	kref_get(&driver_data->kref);
}

static void da9052_wdt_unref(struct watchdog_device *wdt_dev)
{
	struct da9052_wdt_data *driver_data = watchdog_get_drvdata(wdt_dev);

	kref_put(&driver_data->kref, da9052_wdt_release_resources);
}

static int da9052_wdt_start(struct watchdog_device *wdt_dev)
{
	return da9052_wdt_set_timeout(wdt_dev, wdt_dev->timeout);
}

static int da9052_wdt_stop(struct watchdog_device *wdt_dev)
{
	return da9052_wdt_set_timeout(wdt_dev, 0);
}

static int da9052_wdt_ping(struct watchdog_device *wdt_dev)
{
	struct da9052_wdt_data *driver_data = watchdog_get_drvdata(wdt_dev);
	struct da9052 *da9052 = driver_data->da9052;
	unsigned long msec, jnow = jiffies;
	int ret;

	/*
	 * We have a minimum time for watchdog window called TWDMIN. A write
	 * to the watchdog before this elapsed time should cause an error.
	 */
	msec = (jnow - driver_data->jpast) * 1000/HZ;
	if (msec < DA9052_TWDMIN)
		mdelay(msec);

	/* Reset the watchdog timer */
	ret = da9052_reg_update(da9052, DA9052_CONTROL_D_REG,
				DA9052_CONTROLD_WATCHDOG, 1 << 7);
	if (ret < 0)
		goto err_strobe;

	/*
	 * FIXME: Reset the watchdog core, in general PMIC
	 * is supposed to do this
	 */
	ret = da9052_reg_update(da9052, DA9052_CONTROL_D_REG,
				DA9052_CONTROLD_WATCHDOG, 0 << 7);
err_strobe:
	return ret;
}

static struct watchdog_info da9052_wdt_info = {
	.options	= WDIOF_SETTIMEOUT | WDIOF_KEEPALIVEPING,
	.identity	= "DA9052 Watchdog",
};

static const struct watchdog_ops da9052_wdt_ops = {
	.owner = THIS_MODULE,
	.start = da9052_wdt_start,
	.stop = da9052_wdt_stop,
	.ping = da9052_wdt_ping,
	.set_timeout = da9052_wdt_set_timeout,
	.ref = da9052_wdt_ref,
	.unref = da9052_wdt_unref,
};


static int da9052_wdt_probe(struct platform_device *pdev)
{
	struct da9052 *da9052 = dev_get_drvdata(pdev->dev.parent);
	struct da9052_wdt_data *driver_data;
	struct watchdog_device *da9052_wdt;
	int ret;

	driver_data = devm_kzalloc(&pdev->dev, sizeof(*driver_data),
				   GFP_KERNEL);
	if (!driver_data) {
		ret = -ENOMEM;
		goto err;
	}
	driver_data->da9052 = da9052;

	da9052_wdt = &driver_data->wdt;

	da9052_wdt->timeout = DA9052_DEF_TIMEOUT;
	da9052_wdt->info = &da9052_wdt_info;
	da9052_wdt->ops = &da9052_wdt_ops;
	watchdog_set_drvdata(da9052_wdt, driver_data);

	kref_init(&driver_data->kref);

	ret = da9052_reg_update(da9052, DA9052_CONTROL_D_REG,
				DA9052_CONTROLD_TWDSCALE, 0);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to disable watchdog bits, %d\n",
			ret);
		goto err;
	}

	ret = watchdog_register_device(&driver_data->wdt);
	if (ret != 0) {
		dev_err(da9052->dev, "watchdog_register_device() failed: %d\n",
			ret);
		goto err;
	}

	platform_set_drvdata(pdev, driver_data);
err:
	return ret;
}

static int da9052_wdt_remove(struct platform_device *pdev)
{
	struct da9052_wdt_data *driver_data = platform_get_drvdata(pdev);

	watchdog_unregister_device(&driver_data->wdt);
	kref_put(&driver_data->kref, da9052_wdt_release_resources);

	return 0;
}

static struct platform_driver da9052_wdt_driver = {
	.probe = da9052_wdt_probe,
	.remove = da9052_wdt_remove,
	.driver = {
		.name	= "da9052-watchdog",
	},
};

module_platform_driver(da9052_wdt_driver);

MODULE_AUTHOR("Anthony Olech <Anthony.Olech@diasemi.com>");
MODULE_DESCRIPTION("DA9052 SM Device Driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:da9052-watchdog");

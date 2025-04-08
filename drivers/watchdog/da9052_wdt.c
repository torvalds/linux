// SPDX-License-Identifier: GPL-2.0+
/*
 * System monitoring driver for DA9052 PMICs.
 *
 * Copyright(c) 2012 Dialog Semiconductor Ltd.
 *
 * Author: Anthony Olech <Anthony.Olech@diasemi.com>
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
		return ret;

	/*
	 * FIXME: Reset the watchdog core, in general PMIC
	 * is supposed to do this
	 */
	return da9052_reg_update(da9052, DA9052_CONTROL_D_REG,
				 DA9052_CONTROLD_WATCHDOG, 0 << 7);
}

static const struct watchdog_info da9052_wdt_info = {
	.options =	WDIOF_SETTIMEOUT |
			WDIOF_KEEPALIVEPING |
			WDIOF_CARDRESET |
			WDIOF_OVERHEAT |
			WDIOF_POWERUNDER,
	.identity	= "DA9052 Watchdog",
};

static const struct watchdog_ops da9052_wdt_ops = {
	.owner = THIS_MODULE,
	.start = da9052_wdt_start,
	.stop = da9052_wdt_stop,
	.ping = da9052_wdt_ping,
	.set_timeout = da9052_wdt_set_timeout,
};


static int da9052_wdt_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct da9052 *da9052 = dev_get_drvdata(dev->parent);
	struct da9052_wdt_data *driver_data;
	struct watchdog_device *da9052_wdt;
	int ret;

	driver_data = devm_kzalloc(dev, sizeof(*driver_data), GFP_KERNEL);
	if (!driver_data)
		return -ENOMEM;
	driver_data->da9052 = da9052;

	da9052_wdt = &driver_data->wdt;

	da9052_wdt->timeout = DA9052_DEF_TIMEOUT;
	da9052_wdt->info = &da9052_wdt_info;
	da9052_wdt->ops = &da9052_wdt_ops;
	da9052_wdt->parent = dev;
	watchdog_set_drvdata(da9052_wdt, driver_data);

	if (da9052->fault_log & DA9052_FAULTLOG_TWDERROR)
		da9052_wdt->bootstatus |= WDIOF_CARDRESET;
	if (da9052->fault_log & DA9052_FAULTLOG_TEMPOVER)
		da9052_wdt->bootstatus |= WDIOF_OVERHEAT;
	if (da9052->fault_log & DA9052_FAULTLOG_VDDFAULT)
		da9052_wdt->bootstatus |= WDIOF_POWERUNDER;

	ret = da9052_reg_update(da9052, DA9052_CONTROL_D_REG,
				DA9052_CONTROLD_TWDSCALE, 0);
	if (ret < 0) {
		dev_err(dev, "Failed to disable watchdog bits, %d\n", ret);
		return ret;
	}

	return devm_watchdog_register_device(dev, &driver_data->wdt);
}

static struct platform_driver da9052_wdt_driver = {
	.probe = da9052_wdt_probe,
	.driver = {
		.name	= "da9052-watchdog",
	},
};

module_platform_driver(da9052_wdt_driver);

MODULE_AUTHOR("Anthony Olech <Anthony.Olech@diasemi.com>");
MODULE_DESCRIPTION("DA9052 SM Device Driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:da9052-watchdog");

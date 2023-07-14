// SPDX-License-Identifier: GPL-2.0+
/*
 * Watchdog driver for DA9063 PMICs.
 *
 * Copyright(c) 2012 Dialog Semiconductor Ltd.
 *
 * Author: Mariusz Wojtasik <mariusz.wojtasik@diasemi.com>
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/watchdog.h>
#include <linux/platform_device.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/mfd/da9063/registers.h>
#include <linux/mfd/da9063/core.h>
#include <linux/property.h>
#include <linux/regmap.h>

/*
 * Watchdog selector to timeout in seconds.
 *   0: WDT disabled;
 *   others: timeout = 2048 ms * 2^(TWDSCALE-1).
 */
static const unsigned int wdt_timeout[] = { 0, 2, 4, 8, 16, 32, 65, 131 };
static bool use_sw_pm;

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

/*
 * Read the currently active timeout.
 * Zero means the watchdog is disabled.
 */
static unsigned int da9063_wdt_read_timeout(struct da9063 *da9063)
{
	unsigned int val;

	regmap_read(da9063->regmap, DA9063_REG_CONTROL_D, &val);

	return wdt_timeout[val & DA9063_TWDSCALE_MASK];
}

static int da9063_wdt_disable_timer(struct da9063 *da9063)
{
	return regmap_update_bits(da9063->regmap, DA9063_REG_CONTROL_D,
				  DA9063_TWDSCALE_MASK,
				  DA9063_TWDSCALE_DISABLE);
}

static int
da9063_wdt_update_timeout(struct da9063 *da9063, unsigned int timeout)
{
	unsigned int regval;
	int ret;

	/*
	 * The watchdog triggers a reboot if a timeout value is already
	 * programmed because the timeout value combines two functions
	 * in one: indicating the counter limit and starting the watchdog.
	 * The watchdog must be disabled to be able to change the timeout
	 * value if the watchdog is already running. Then we can set the
	 * new timeout value which enables the watchdog again.
	 */
	ret = da9063_wdt_disable_timer(da9063);
	if (ret)
		return ret;

	usleep_range(150, 300);
	regval = da9063_wdt_timeout_to_sel(timeout);

	return regmap_update_bits(da9063->regmap, DA9063_REG_CONTROL_D,
				  DA9063_TWDSCALE_MASK, regval);
}

static int da9063_wdt_start(struct watchdog_device *wdd)
{
	struct da9063 *da9063 = watchdog_get_drvdata(wdd);
	int ret;

	ret = da9063_wdt_update_timeout(da9063, wdd->timeout);
	if (ret)
		dev_err(da9063->dev, "Watchdog failed to start (err = %d)\n",
			ret);

	return ret;
}

static int da9063_wdt_stop(struct watchdog_device *wdd)
{
	struct da9063 *da9063 = watchdog_get_drvdata(wdd);
	int ret;

	ret = da9063_wdt_disable_timer(da9063);
	if (ret)
		dev_alert(da9063->dev, "Watchdog failed to stop (err = %d)\n",
			  ret);

	return ret;
}

static int da9063_wdt_ping(struct watchdog_device *wdd)
{
	struct da9063 *da9063 = watchdog_get_drvdata(wdd);
	int ret;

	/*
	 * Prevent pings from occurring late in system poweroff/reboot sequence
	 * and possibly locking out restart handler from accessing i2c bus.
	 */
	if (system_state > SYSTEM_RUNNING)
		return 0;

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
	int ret = 0;

	/*
	 * There are two cases when a set_timeout() will be called:
	 * 1. The watchdog is off and someone wants to set the timeout for the
	 *    further use.
	 * 2. The watchdog is already running and a new timeout value should be
	 *    set.
	 *
	 * The watchdog can't store a timeout value not equal zero without
	 * enabling the watchdog, so the timeout must be buffered by the driver.
	 */
	if (watchdog_active(wdd))
		ret = da9063_wdt_update_timeout(da9063, timeout);

	if (ret)
		dev_err(da9063->dev, "Failed to set watchdog timeout (err = %d)\n",
			ret);
	else
		wdd->timeout = wdt_timeout[da9063_wdt_timeout_to_sel(timeout)];

	return ret;
}

static int da9063_wdt_restart(struct watchdog_device *wdd, unsigned long action,
			      void *data)
{
	struct da9063 *da9063 = watchdog_get_drvdata(wdd);
	struct i2c_client *client = to_i2c_client(da9063->dev);
	union i2c_smbus_data msg;
	int ret;

	/*
	 * Don't use regmap because it is not atomic safe. Additionally, use
	 * unlocked flavor of i2c_smbus_xfer to avoid scenario where i2c bus
	 * might previously be locked by some process unable to release the
	 * lock due to interrupts already being disabled at this late stage.
	 */
	msg.byte = DA9063_SHUTDOWN;
	ret = __i2c_smbus_xfer(client->adapter, client->addr, client->flags,
			I2C_SMBUS_WRITE, DA9063_REG_CONTROL_F,
			I2C_SMBUS_BYTE_DATA, &msg);

	if (ret < 0)
		dev_alert(da9063->dev, "Failed to shutdown (err = %d)\n",
			  ret);

	/* wait for reset to assert... */
	mdelay(500);

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
	struct device *dev = &pdev->dev;
	struct da9063 *da9063;
	struct watchdog_device *wdd;
	unsigned int timeout;

	if (!dev->parent)
		return -EINVAL;

	da9063 = dev_get_drvdata(dev->parent);
	if (!da9063)
		return -EINVAL;

	wdd = devm_kzalloc(dev, sizeof(*wdd), GFP_KERNEL);
	if (!wdd)
		return -ENOMEM;

	use_sw_pm = device_property_present(dev, "dlg,use-sw-pm");

	wdd->info = &da9063_watchdog_info;
	wdd->ops = &da9063_watchdog_ops;
	wdd->min_timeout = DA9063_WDT_MIN_TIMEOUT;
	wdd->max_timeout = DA9063_WDT_MAX_TIMEOUT;
	wdd->min_hw_heartbeat_ms = DA9063_RESET_PROTECTION_MS;
	wdd->parent = dev;
	wdd->status = WATCHDOG_NOWAYOUT_INIT_STATUS;

	watchdog_set_restart_priority(wdd, 128);
	watchdog_set_drvdata(wdd, da9063);
	dev_set_drvdata(dev, wdd);

	wdd->timeout = DA9063_WDG_TIMEOUT;

	/* Use pre-configured timeout if watchdog is already running. */
	timeout = da9063_wdt_read_timeout(da9063);
	if (timeout)
		wdd->timeout = timeout;

	/* Set timeout, maybe override it with DT value, scale it */
	watchdog_init_timeout(wdd, 0, dev);
	da9063_wdt_set_timeout(wdd, wdd->timeout);

	/* Update timeout if the watchdog is already running. */
	if (timeout) {
		da9063_wdt_update_timeout(da9063, wdd->timeout);
		set_bit(WDOG_HW_RUNNING, &wdd->status);
	}

	return devm_watchdog_register_device(dev, wdd);
}

static int __maybe_unused da9063_wdt_suspend(struct device *dev)
{
	struct watchdog_device *wdd = dev_get_drvdata(dev);

	if (!use_sw_pm)
		return 0;

	if (watchdog_active(wdd))
		return da9063_wdt_stop(wdd);

	return 0;
}

static int __maybe_unused da9063_wdt_resume(struct device *dev)
{
	struct watchdog_device *wdd = dev_get_drvdata(dev);

	if (!use_sw_pm)
		return 0;

	if (watchdog_active(wdd))
		return da9063_wdt_start(wdd);

	return 0;
}

static SIMPLE_DEV_PM_OPS(da9063_wdt_pm_ops,
			da9063_wdt_suspend, da9063_wdt_resume);

static struct platform_driver da9063_wdt_driver = {
	.probe = da9063_wdt_probe,
	.driver = {
		.name = DA9063_DRVNAME_WATCHDOG,
		.pm = &da9063_wdt_pm_ops,
	},
};
module_platform_driver(da9063_wdt_driver);

MODULE_AUTHOR("Mariusz Wojtasik <mariusz.wojtasik@diasemi.com>");
MODULE_DESCRIPTION("Watchdog driver for Dialog DA9063");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:" DA9063_DRVNAME_WATCHDOG);

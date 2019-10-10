// SPDX-License-Identifier: GPL-2.0+
/*
 * Mellanox watchdog driver
 *
 * Copyright (C) 2019 Mellanox Technologies
 * Copyright (C) 2019 Michael Shych <mshych@mellanox.com>
 */

#include <linux/bitops.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/log2.h>
#include <linux/module.h>
#include <linux/platform_data/mlxreg.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/spinlock.h>
#include <linux/types.h>
#include <linux/watchdog.h>

#define MLXREG_WDT_CLOCK_SCALE		1000
#define MLXREG_WDT_MAX_TIMEOUT_TYPE1	32
#define MLXREG_WDT_MAX_TIMEOUT_TYPE2	255
#define MLXREG_WDT_MIN_TIMEOUT		1
#define MLXREG_WDT_OPTIONS_BASE (WDIOF_KEEPALIVEPING | WDIOF_MAGICCLOSE | \
				 WDIOF_SETTIMEOUT)

/**
 * struct mlxreg_wdt - wd private data:
 *
 * @wdd:	watchdog device;
 * @device:	basic device;
 * @pdata:	data received from platform driver;
 * @regmap:	register map of parent device;
 * @timeout:	defined timeout in sec.;
 * @action_idx:	index for direct access to action register;
 * @timeout_idx:index for direct access to TO register;
 * @tleft_idx:	index for direct access to time left register;
 * @ping_idx:	index for direct access to ping register;
 * @reset_idx:	index for direct access to reset cause register;
 * @wd_type:	watchdog HW type;
 */
struct mlxreg_wdt {
	struct watchdog_device wdd;
	struct mlxreg_core_platform_data *pdata;
	void *regmap;
	int action_idx;
	int timeout_idx;
	int tleft_idx;
	int ping_idx;
	int reset_idx;
	enum mlxreg_wdt_type wdt_type;
};

static void mlxreg_wdt_check_card_reset(struct mlxreg_wdt *wdt)
{
	struct mlxreg_core_data *reg_data;
	u32 regval;
	int rc;

	if (wdt->reset_idx == -EINVAL)
		return;

	if (!(wdt->wdd.info->options & WDIOF_CARDRESET))
		return;

	reg_data = &wdt->pdata->data[wdt->reset_idx];
	rc = regmap_read(wdt->regmap, reg_data->reg, &regval);
	if (!rc) {
		if (regval & ~reg_data->mask) {
			wdt->wdd.bootstatus = WDIOF_CARDRESET;
			dev_info(wdt->wdd.parent,
				 "watchdog previously reset the CPU\n");
		}
	}
}

static int mlxreg_wdt_start(struct watchdog_device *wdd)
{
	struct mlxreg_wdt *wdt = watchdog_get_drvdata(wdd);
	struct mlxreg_core_data *reg_data = &wdt->pdata->data[wdt->action_idx];

	return regmap_update_bits(wdt->regmap, reg_data->reg, ~reg_data->mask,
				  BIT(reg_data->bit));
}

static int mlxreg_wdt_stop(struct watchdog_device *wdd)
{
	struct mlxreg_wdt *wdt = watchdog_get_drvdata(wdd);
	struct mlxreg_core_data *reg_data = &wdt->pdata->data[wdt->action_idx];

	return regmap_update_bits(wdt->regmap, reg_data->reg, ~reg_data->mask,
				  ~BIT(reg_data->bit));
}

static int mlxreg_wdt_ping(struct watchdog_device *wdd)
{
	struct mlxreg_wdt *wdt = watchdog_get_drvdata(wdd);
	struct mlxreg_core_data *reg_data = &wdt->pdata->data[wdt->ping_idx];

	return regmap_update_bits_base(wdt->regmap, reg_data->reg,
				       ~reg_data->mask, BIT(reg_data->bit),
				       NULL, false, true);
}

static int mlxreg_wdt_set_timeout(struct watchdog_device *wdd,
				  unsigned int timeout)
{
	struct mlxreg_wdt *wdt = watchdog_get_drvdata(wdd);
	struct mlxreg_core_data *reg_data = &wdt->pdata->data[wdt->timeout_idx];
	u32 regval, set_time, hw_timeout;
	int rc;

	if (wdt->wdt_type == MLX_WDT_TYPE1) {
		rc = regmap_read(wdt->regmap, reg_data->reg, &regval);
		if (rc)
			return rc;

		hw_timeout = order_base_2(timeout * MLXREG_WDT_CLOCK_SCALE);
		regval = (regval & reg_data->mask) | hw_timeout;
		/* Rowndown to actual closest number of sec. */
		set_time = BIT(hw_timeout) / MLXREG_WDT_CLOCK_SCALE;
	} else {
		set_time = timeout;
		regval = timeout;
	}

	wdd->timeout = set_time;
	rc = regmap_write(wdt->regmap, reg_data->reg, regval);

	if (!rc) {
		/*
		 * Restart watchdog with new timeout period
		 * if watchdog is already started.
		 */
		if (watchdog_active(wdd)) {
			rc = mlxreg_wdt_stop(wdd);
			if (!rc)
				rc = mlxreg_wdt_start(wdd);
		}
	}

	return rc;
}

static unsigned int mlxreg_wdt_get_timeleft(struct watchdog_device *wdd)
{
	struct mlxreg_wdt *wdt = watchdog_get_drvdata(wdd);
	struct mlxreg_core_data *reg_data = &wdt->pdata->data[wdt->tleft_idx];
	u32 regval;
	int rc;

	rc = regmap_read(wdt->regmap, reg_data->reg, &regval);
	/* Return 0 timeleft in case of failure register read. */
	return rc == 0 ? regval : 0;
}

static const struct watchdog_ops mlxreg_wdt_ops_type1 = {
	.start		= mlxreg_wdt_start,
	.stop		= mlxreg_wdt_stop,
	.ping		= mlxreg_wdt_ping,
	.set_timeout	= mlxreg_wdt_set_timeout,
	.owner		= THIS_MODULE,
};

static const struct watchdog_ops mlxreg_wdt_ops_type2 = {
	.start		= mlxreg_wdt_start,
	.stop		= mlxreg_wdt_stop,
	.ping		= mlxreg_wdt_ping,
	.set_timeout	= mlxreg_wdt_set_timeout,
	.get_timeleft	= mlxreg_wdt_get_timeleft,
	.owner		= THIS_MODULE,
};

static const struct watchdog_info mlxreg_wdt_main_info = {
	.options	= MLXREG_WDT_OPTIONS_BASE
			| WDIOF_CARDRESET,
	.identity	= "mlx-wdt-main",
};

static const struct watchdog_info mlxreg_wdt_aux_info = {
	.options	= MLXREG_WDT_OPTIONS_BASE
			| WDIOF_ALARMONLY,
	.identity	= "mlx-wdt-aux",
};

static void mlxreg_wdt_config(struct mlxreg_wdt *wdt,
			      struct mlxreg_core_platform_data *pdata)
{
	struct mlxreg_core_data *data = pdata->data;
	int i;

	wdt->reset_idx = -EINVAL;
	for (i = 0; i < pdata->counter; i++, data++) {
		if (strnstr(data->label, "action", sizeof(data->label)))
			wdt->action_idx = i;
		else if (strnstr(data->label, "timeout", sizeof(data->label)))
			wdt->timeout_idx = i;
		else if (strnstr(data->label, "timeleft", sizeof(data->label)))
			wdt->tleft_idx = i;
		else if (strnstr(data->label, "ping", sizeof(data->label)))
			wdt->ping_idx = i;
		else if (strnstr(data->label, "reset", sizeof(data->label)))
			wdt->reset_idx = i;
	}

	wdt->pdata = pdata;
	if (strnstr(pdata->identity, mlxreg_wdt_main_info.identity,
		    sizeof(mlxreg_wdt_main_info.identity)))
		wdt->wdd.info = &mlxreg_wdt_main_info;
	else
		wdt->wdd.info = &mlxreg_wdt_aux_info;

	wdt->wdt_type = pdata->version;
	if (wdt->wdt_type == MLX_WDT_TYPE2) {
		wdt->wdd.ops = &mlxreg_wdt_ops_type2;
		wdt->wdd.max_timeout = MLXREG_WDT_MAX_TIMEOUT_TYPE2;
	} else {
		wdt->wdd.ops = &mlxreg_wdt_ops_type1;
		wdt->wdd.max_timeout = MLXREG_WDT_MAX_TIMEOUT_TYPE1;
	}
	wdt->wdd.min_timeout = MLXREG_WDT_MIN_TIMEOUT;
}

static int mlxreg_wdt_init_timeout(struct mlxreg_wdt *wdt,
				   struct mlxreg_core_platform_data *pdata)
{
	u32 timeout;

	timeout = pdata->data[wdt->timeout_idx].health_cntr;
	return mlxreg_wdt_set_timeout(&wdt->wdd, timeout);
}

static int mlxreg_wdt_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct mlxreg_core_platform_data *pdata;
	struct mlxreg_wdt *wdt;
	int rc;

	pdata = dev_get_platdata(dev);
	if (!pdata) {
		dev_err(dev, "Failed to get platform data.\n");
		return -EINVAL;
	}
	wdt = devm_kzalloc(dev, sizeof(*wdt), GFP_KERNEL);
	if (!wdt)
		return -ENOMEM;

	wdt->wdd.parent = dev;
	wdt->regmap = pdata->regmap;
	mlxreg_wdt_config(wdt, pdata);

	if ((pdata->features & MLXREG_CORE_WD_FEATURE_NOWAYOUT))
		watchdog_set_nowayout(&wdt->wdd, WATCHDOG_NOWAYOUT);
	watchdog_stop_on_reboot(&wdt->wdd);
	watchdog_stop_on_unregister(&wdt->wdd);
	watchdog_set_drvdata(&wdt->wdd, wdt);
	rc = mlxreg_wdt_init_timeout(wdt, pdata);
	if (rc)
		goto register_error;

	if ((pdata->features & MLXREG_CORE_WD_FEATURE_START_AT_BOOT)) {
		rc = mlxreg_wdt_start(&wdt->wdd);
		if (rc)
			goto register_error;
		set_bit(WDOG_HW_RUNNING, &wdt->wdd.status);
	}
	mlxreg_wdt_check_card_reset(wdt);
	rc = devm_watchdog_register_device(dev, &wdt->wdd);

register_error:
	if (rc)
		dev_err(dev, "Cannot register watchdog device (err=%d)\n", rc);
	return rc;
}

static struct platform_driver mlxreg_wdt_driver = {
	.probe	= mlxreg_wdt_probe,
	.driver	= {
			.name = "mlx-wdt",
	},
};

module_platform_driver(mlxreg_wdt_driver);

MODULE_AUTHOR("Michael Shych <michaelsh@mellanox.com>");
MODULE_DESCRIPTION("Mellanox watchdog driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:mlx-wdt");

// SPDX-License-Identifier: GPL-2.0-only
/*
 * ARM Secure Monitor Call watchdog driver
 *
 * Copyright 2020 Google LLC.
 * Julius Werner <jwerner@chromium.org>
 * Based on mtk_wdt.c
 */

#include <linux/arm-smccc.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/types.h>
#include <linux/watchdog.h>
#include <uapi/linux/psci.h>

#define DRV_NAME		"arm_smc_wdt"
#define DRV_VERSION		"1.0"

enum smcwd_call {
	SMCWD_INIT		= 0,
	SMCWD_SET_TIMEOUT	= 1,
	SMCWD_ENABLE		= 2,
	SMCWD_PET		= 3,
	SMCWD_GET_TIMELEFT	= 4,
};

static bool nowayout = WATCHDOG_NOWAYOUT;
static unsigned int timeout;

static int smcwd_call(struct watchdog_device *wdd, enum smcwd_call call,
		      unsigned long arg, struct arm_smccc_res *res)
{
	struct arm_smccc_res local_res;

	if (!res)
		res = &local_res;

	arm_smccc_smc((u32)(uintptr_t)watchdog_get_drvdata(wdd), call, arg, 0,
		      0, 0, 0, 0, res);

	if (res->a0 == PSCI_RET_NOT_SUPPORTED)
		return -ENODEV;
	if (res->a0 == PSCI_RET_INVALID_PARAMS)
		return -EINVAL;
	if (res->a0 == PSCI_RET_DISABLED)
		return -ENODATA;
	if (res->a0 != PSCI_RET_SUCCESS)
		return -EIO;
	return 0;
}

static int smcwd_ping(struct watchdog_device *wdd)
{
	return smcwd_call(wdd, SMCWD_PET, 0, NULL);
}

static unsigned int smcwd_get_timeleft(struct watchdog_device *wdd)
{
	struct arm_smccc_res res;

	smcwd_call(wdd, SMCWD_GET_TIMELEFT, 0, &res);
	if (res.a0)
		return 0;
	return res.a1;
}

static int smcwd_set_timeout(struct watchdog_device *wdd, unsigned int timeout)
{
	int res;

	res = smcwd_call(wdd, SMCWD_SET_TIMEOUT, timeout, NULL);
	if (!res)
		wdd->timeout = timeout;
	return res;
}

static int smcwd_stop(struct watchdog_device *wdd)
{
	return smcwd_call(wdd, SMCWD_ENABLE, 0, NULL);
}

static int smcwd_start(struct watchdog_device *wdd)
{
	return smcwd_call(wdd, SMCWD_ENABLE, 1, NULL);
}

static const struct watchdog_info smcwd_info = {
	.identity	= DRV_NAME,
	.options	= WDIOF_SETTIMEOUT |
			  WDIOF_KEEPALIVEPING |
			  WDIOF_MAGICCLOSE,
};

static const struct watchdog_ops smcwd_ops = {
	.start		= smcwd_start,
	.stop		= smcwd_stop,
	.ping		= smcwd_ping,
	.set_timeout	= smcwd_set_timeout,
};

static const struct watchdog_ops smcwd_timeleft_ops = {
	.start		= smcwd_start,
	.stop		= smcwd_stop,
	.ping		= smcwd_ping,
	.set_timeout	= smcwd_set_timeout,
	.get_timeleft	= smcwd_get_timeleft,
};

static int smcwd_probe(struct platform_device *pdev)
{
	struct watchdog_device *wdd;
	int err;
	struct arm_smccc_res res;
	u32 smc_func_id;

	wdd = devm_kzalloc(&pdev->dev, sizeof(*wdd), GFP_KERNEL);
	if (!wdd)
		return -ENOMEM;
	platform_set_drvdata(pdev, wdd);

	if (of_property_read_u32(pdev->dev.of_node, "arm,smc-id",
				 &smc_func_id))
		smc_func_id = 0x82003D06;
	watchdog_set_drvdata(wdd, (void *)(uintptr_t)smc_func_id);

	err = smcwd_call(wdd, SMCWD_INIT, 0, &res);
	if (err < 0)
		return err;

	wdd->info = &smcwd_info;
	/* get_timeleft is optional */
	err = smcwd_call(wdd, SMCWD_GET_TIMELEFT, 0, NULL);
	switch (err) {
	case 0:
		set_bit(WDOG_HW_RUNNING, &wdd->status);
		fallthrough;
	case -ENODATA:
		wdd->ops = &smcwd_timeleft_ops;
		break;
	default:
		wdd->ops = &smcwd_ops;
		break;
	}

	wdd->timeout = res.a2;
	wdd->max_timeout = res.a2;
	wdd->min_timeout = res.a1;
	wdd->parent = &pdev->dev;

	watchdog_stop_on_reboot(wdd);
	watchdog_stop_on_unregister(wdd);
	watchdog_set_nowayout(wdd, nowayout);
	watchdog_init_timeout(wdd, timeout, &pdev->dev);
	err = smcwd_set_timeout(wdd, wdd->timeout);
	if (err)
		return err;

	err = devm_watchdog_register_device(&pdev->dev, wdd);
	if (err)
		return err;

	dev_info(&pdev->dev,
		 "Watchdog registered (timeout=%d sec, nowayout=%d)\n",
		 wdd->timeout, nowayout);

	return 0;
}

static const struct of_device_id smcwd_dt_ids[] = {
	{ .compatible = "arm,smc-wdt" },
	{}
};
MODULE_DEVICE_TABLE(of, smcwd_dt_ids);

static struct platform_driver smcwd_driver = {
	.probe		= smcwd_probe,
	.driver		= {
		.name		= DRV_NAME,
		.of_match_table	= smcwd_dt_ids,
	},
};

module_platform_driver(smcwd_driver);

module_param(timeout, uint, 0);
MODULE_PARM_DESC(timeout, "Watchdog heartbeat in seconds");

module_param(nowayout, bool, 0);
MODULE_PARM_DESC(nowayout, "Watchdog cannot be stopped once started (default="
			__MODULE_STRING(WATCHDOG_NOWAYOUT) ")");

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Julius Werner <jwerner@chromium.org>");
MODULE_DESCRIPTION("ARM Secure Monitor Call Watchdog Driver");
MODULE_VERSION(DRV_VERSION);

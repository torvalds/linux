// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/arm-smccc.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/watchdog.h>

#define GUNYAH_WDT_SMCCC_CALL_VAL(func_id) \
	ARM_SMCCC_CALL_VAL(ARM_SMCCC_FAST_CALL, ARM_SMCCC_SMC_32,\
			   ARM_SMCCC_OWNER_VENDOR_HYP, func_id)

/* SMCCC function IDs for watchdog operations */
#define GUNYAH_WDT_CONTROL   GUNYAH_WDT_SMCCC_CALL_VAL(0x0005)
#define GUNYAH_WDT_STATUS    GUNYAH_WDT_SMCCC_CALL_VAL(0x0006)
#define GUNYAH_WDT_PING      GUNYAH_WDT_SMCCC_CALL_VAL(0x0007)
#define GUNYAH_WDT_SET_TIME  GUNYAH_WDT_SMCCC_CALL_VAL(0x0008)

/*
 * Control values for GUNYAH_WDT_CONTROL.
 * Bit 0 is used to enable or disable the watchdog. If this bit is set,
 * then the watchdog is enabled and vice versa.
 * Bit 1 should always be set to 1 as this bit is reserved in Gunyah and
 * it's expected to be 1.
 */
#define WDT_CTRL_ENABLE  (BIT(1) | BIT(0))
#define WDT_CTRL_DISABLE BIT(1)

enum gunyah_error {
	GUNYAH_ERROR_OK				= 0,
	GUNYAH_ERROR_UNIMPLEMENTED		= -1,
	GUNYAH_ERROR_ARG_INVAL			= 1,
};

/**
 * gunyah_error_remap() - Remap Gunyah hypervisor errors into a Linux error code
 * @gunyah_error: Gunyah hypercall return value
 */
static inline int gunyah_error_remap(enum gunyah_error gunyah_error)
{
	switch (gunyah_error) {
	case GUNYAH_ERROR_OK:
		return 0;
	case GUNYAH_ERROR_UNIMPLEMENTED:
		return -EOPNOTSUPP;
	default:
		return -EINVAL;
	}
}

static int gunyah_wdt_call(unsigned long func_id, unsigned long arg1,
			   unsigned long arg2)
{
	struct arm_smccc_res res;

	arm_smccc_1_1_smc(func_id, arg1, arg2, &res);
	return gunyah_error_remap(res.a0);
}

static int gunyah_wdt_start(struct watchdog_device *wdd)
{
	unsigned int timeout_ms;
	struct device *dev = wdd->parent;
	int ret;

	ret = gunyah_wdt_call(GUNYAH_WDT_CONTROL, WDT_CTRL_DISABLE, 0);
	if (ret && watchdog_active(wdd)) {
		dev_err(dev, "%s: Failed to stop gunyah wdt %d\n", __func__, ret);
		return ret;
	}

	timeout_ms = wdd->timeout * 1000;
	ret = gunyah_wdt_call(GUNYAH_WDT_SET_TIME, timeout_ms, timeout_ms);
	if (ret) {
		dev_err(dev, "%s: Failed to set timeout for gunyah wdt %d\n",
			__func__, ret);
		return ret;
	}

	ret = gunyah_wdt_call(GUNYAH_WDT_CONTROL, WDT_CTRL_ENABLE, 0);
	if (ret)
		dev_err(dev, "%s: Failed to start gunyah wdt %d\n", __func__, ret);

	return ret;
}

static int gunyah_wdt_stop(struct watchdog_device *wdd)
{
	return gunyah_wdt_call(GUNYAH_WDT_CONTROL, WDT_CTRL_DISABLE, 0);
}

static int gunyah_wdt_ping(struct watchdog_device *wdd)
{
	return gunyah_wdt_call(GUNYAH_WDT_PING, 0, 0);
}

static int gunyah_wdt_set_timeout(struct watchdog_device *wdd,
				  unsigned int timeout_sec)
{
	wdd->timeout = timeout_sec;

	if (watchdog_active(wdd))
		return gunyah_wdt_start(wdd);

	return 0;
}

static int gunyah_wdt_get_time_since_last_ping(void)
{
	struct arm_smccc_res res;

	arm_smccc_1_1_smc(GUNYAH_WDT_STATUS, 0, 0, &res);
	if (res.a0)
		return gunyah_error_remap(res.a0);

	return res.a2 / 1000;
}

static unsigned int gunyah_wdt_get_timeleft(struct watchdog_device *wdd)
{
	int seconds_since_last_ping;

	seconds_since_last_ping = gunyah_wdt_get_time_since_last_ping();
	if (seconds_since_last_ping < 0 ||
	    seconds_since_last_ping > wdd->timeout)
		return 0;

	return wdd->timeout - seconds_since_last_ping;
}

static int gunyah_wdt_restart(struct watchdog_device *wdd,
			      unsigned long action, void *data)
{
	/* Set timeout to 1ms and send a ping */
	gunyah_wdt_call(GUNYAH_WDT_CONTROL, WDT_CTRL_DISABLE, 0);
	gunyah_wdt_call(GUNYAH_WDT_SET_TIME, 1, 1);
	gunyah_wdt_call(GUNYAH_WDT_CONTROL, WDT_CTRL_ENABLE, 0);
	gunyah_wdt_call(GUNYAH_WDT_PING, 0, 0);

	/* Wait to make sure reset occurs */
	mdelay(100);

	return 0;
}

static const struct watchdog_info gunyah_wdt_info = {
	.identity = "Gunyah Watchdog",
	.options = WDIOF_SETTIMEOUT
		 | WDIOF_KEEPALIVEPING
		 | WDIOF_MAGICCLOSE,
};

static const struct watchdog_ops gunyah_wdt_ops = {
	.owner = THIS_MODULE,
	.start = gunyah_wdt_start,
	.stop = gunyah_wdt_stop,
	.ping = gunyah_wdt_ping,
	.set_timeout = gunyah_wdt_set_timeout,
	.get_timeleft = gunyah_wdt_get_timeleft,
	.restart = gunyah_wdt_restart
};

static int gunyah_wdt_probe(struct platform_device *pdev)
{
	struct watchdog_device *wdd;
	struct device *dev = &pdev->dev;
	int ret;

	ret = gunyah_wdt_call(GUNYAH_WDT_STATUS, 0, 0);
	if (ret == -EOPNOTSUPP)
		return -ENODEV;

	if (ret)
		return dev_err_probe(dev, ret, "status check failed\n");

	wdd = devm_kzalloc(dev, sizeof(*wdd), GFP_KERNEL);
	if (!wdd)
		return -ENOMEM;

	wdd->info = &gunyah_wdt_info;
	wdd->ops = &gunyah_wdt_ops;
	wdd->parent = dev;

	/*
	 * Although Gunyah expects 16-bit unsigned int values as timeout values
	 * in milliseconds, values above 0x8000 are reserved. This limits the
	 * max timeout value to 32 seconds.
	 */
	wdd->max_timeout = 32; /* seconds */
	wdd->min_timeout = 1; /* seconds */
	wdd->timeout = wdd->max_timeout;

	gunyah_wdt_stop(wdd);
	platform_set_drvdata(pdev, wdd);
	watchdog_set_restart_priority(wdd, 0);

	return devm_watchdog_register_device(dev, wdd);
}

static void gunyah_wdt_remove(struct platform_device *pdev)
{
	struct watchdog_device *wdd = platform_get_drvdata(pdev);

	gunyah_wdt_stop(wdd);
}

static int gunyah_wdt_suspend(struct device *dev)
{
	struct watchdog_device *wdd = dev_get_drvdata(dev);

	if (watchdog_active(wdd))
		gunyah_wdt_stop(wdd);

	return 0;
}

static int gunyah_wdt_resume(struct device *dev)
{
	struct watchdog_device *wdd = dev_get_drvdata(dev);

	if (watchdog_active(wdd))
		gunyah_wdt_start(wdd);

	return 0;
}

static DEFINE_SIMPLE_DEV_PM_OPS(gunyah_wdt_pm_ops, gunyah_wdt_suspend, gunyah_wdt_resume);

/*
 * Gunyah watchdog is a vendor-specific hypervisor interface provided by the
 * Gunyah hypervisor. Using QCOM SCM driver to detect Gunyah watchdog SMCCC
 * hypervisor service and register platform device when the service is available
 * allows this driver to operate independently of the devicetree and avoids
 * adding the non-hardware nodes to the devicetree.
 */
static const struct platform_device_id gunyah_wdt_id[] = {
	{ .name = "gunyah-wdt" },
	{}
};
MODULE_DEVICE_TABLE(platform, gunyah_wdt_id);

static struct platform_driver gunyah_wdt_driver = {
	.driver = {
		.name = "gunyah-wdt",
		.pm = pm_sleep_ptr(&gunyah_wdt_pm_ops),
	},
	.id_table = gunyah_wdt_id,
	.probe = gunyah_wdt_probe,
	.remove = gunyah_wdt_remove,
};

module_platform_driver(gunyah_wdt_driver);

MODULE_DESCRIPTION("Gunyah Watchdog Driver");
MODULE_LICENSE("GPL");

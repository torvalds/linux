// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2024 Google LLC.
 * Author: Lukasz Majczak <lma@chromium.com>
 */

#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/platform_data/cros_ec_commands.h>
#include <linux/platform_data/cros_ec_proto.h>
#include <linux/platform_device.h>
#include <linux/watchdog.h>

#define CROS_EC_WATCHDOG_DEFAULT_TIME	30 /* seconds */
#define DRV_NAME	"cros-ec-wdt"

union cros_ec_wdt_data {
	struct ec_params_hang_detect req;
	struct ec_response_hang_detect resp;
} __packed;

static int cros_ec_wdt_send_cmd(struct cros_ec_device *cros_ec,
				union cros_ec_wdt_data *arg)
{
	int ret;
	DEFINE_RAW_FLEX(struct cros_ec_command, msg, data,
			sizeof(union cros_ec_wdt_data));

	msg->version = 0;
	msg->command = EC_CMD_HANG_DETECT;
	msg->insize  = (arg->req.command == EC_HANG_DETECT_CMD_GET_STATUS) ?
		   sizeof(struct ec_response_hang_detect) :
		   0;
	msg->outsize = sizeof(struct ec_params_hang_detect);
	*(struct ec_params_hang_detect *)msg->data = arg->req;

	ret = cros_ec_cmd_xfer_status(cros_ec, msg);
	if (ret < 0)
		return ret;

	arg->resp = *(struct ec_response_hang_detect *)msg->data;

	return 0;
}

static int cros_ec_wdt_ping(struct watchdog_device *wdd)
{
	struct cros_ec_device *cros_ec = watchdog_get_drvdata(wdd);
	union cros_ec_wdt_data arg;
	int ret;

	arg.req.command = EC_HANG_DETECT_CMD_RELOAD;
	ret = cros_ec_wdt_send_cmd(cros_ec, &arg);
	if (ret < 0)
		dev_dbg(wdd->parent, "Failed to ping watchdog (%d)\n", ret);

	return ret;
}

static int cros_ec_wdt_start(struct watchdog_device *wdd)
{
	struct cros_ec_device *cros_ec = watchdog_get_drvdata(wdd);
	union cros_ec_wdt_data arg;
	int ret;

	/* Prepare watchdog on EC side */
	arg.req.command = EC_HANG_DETECT_CMD_SET_TIMEOUT;
	arg.req.reboot_timeout_sec = wdd->timeout;
	ret = cros_ec_wdt_send_cmd(cros_ec, &arg);
	if (ret < 0)
		dev_dbg(wdd->parent, "Failed to start watchdog (%d)\n", ret);

	return ret;
}

static int cros_ec_wdt_stop(struct watchdog_device *wdd)
{
	struct cros_ec_device *cros_ec = watchdog_get_drvdata(wdd);
	union cros_ec_wdt_data arg;
	int ret;

	arg.req.command = EC_HANG_DETECT_CMD_CANCEL;
	ret = cros_ec_wdt_send_cmd(cros_ec, &arg);
	if (ret < 0)
		dev_dbg(wdd->parent, "Failed to stop watchdog (%d)\n", ret);

	return ret;
}

static int cros_ec_wdt_set_timeout(struct watchdog_device *wdd, unsigned int t)
{
	unsigned int old_timeout = wdd->timeout;
	int ret;

	wdd->timeout = t;
	ret = cros_ec_wdt_start(wdd);
	if (ret < 0)
		wdd->timeout = old_timeout;

	return ret;
}

static const struct watchdog_info cros_ec_wdt_ident = {
	.options          = WDIOF_SETTIMEOUT | WDIOF_KEEPALIVEPING | WDIOF_MAGICCLOSE,
	.firmware_version = 0,
	.identity         = DRV_NAME,
};

static const struct watchdog_ops cros_ec_wdt_ops = {
	.owner		 = THIS_MODULE,
	.ping		 = cros_ec_wdt_ping,
	.start		 = cros_ec_wdt_start,
	.stop		 = cros_ec_wdt_stop,
	.set_timeout = cros_ec_wdt_set_timeout,
};

static int cros_ec_wdt_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct cros_ec_dev *ec_dev = dev_get_drvdata(dev->parent);
	struct cros_ec_device *cros_ec = ec_dev->ec_dev;
	struct watchdog_device *wdd;
	union cros_ec_wdt_data arg;
	int ret = 0;

	wdd = devm_kzalloc(&pdev->dev, sizeof(*wdd), GFP_KERNEL);
	if (!wdd)
		return -ENOMEM;

	arg.req.command = EC_HANG_DETECT_CMD_GET_STATUS;
	ret = cros_ec_wdt_send_cmd(cros_ec, &arg);
	if (ret < 0)
		return dev_err_probe(dev, ret, "Failed to get watchdog bootstatus\n");

	wdd->parent = &pdev->dev;
	wdd->info = &cros_ec_wdt_ident;
	wdd->ops = &cros_ec_wdt_ops;
	wdd->timeout = CROS_EC_WATCHDOG_DEFAULT_TIME;
	wdd->min_timeout = EC_HANG_DETECT_MIN_TIMEOUT;
	wdd->max_timeout = EC_HANG_DETECT_MAX_TIMEOUT;
	if (arg.resp.status == EC_HANG_DETECT_AP_BOOT_EC_WDT)
		wdd->bootstatus = WDIOF_CARDRESET;

	arg.req.command = EC_HANG_DETECT_CMD_CLEAR_STATUS;
	ret = cros_ec_wdt_send_cmd(cros_ec, &arg);
	if (ret < 0)
		return dev_err_probe(dev, ret, "Failed to clear watchdog bootstatus\n");

	watchdog_stop_on_reboot(wdd);
	watchdog_stop_on_unregister(wdd);
	watchdog_set_drvdata(wdd, cros_ec);
	platform_set_drvdata(pdev, wdd);

	return devm_watchdog_register_device(dev, wdd);
}

static int __maybe_unused cros_ec_wdt_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct watchdog_device *wdd = platform_get_drvdata(pdev);
	int ret = 0;

	if (watchdog_active(wdd))
		ret = cros_ec_wdt_stop(wdd);

	return ret;
}

static int __maybe_unused cros_ec_wdt_resume(struct platform_device *pdev)
{
	struct watchdog_device *wdd = platform_get_drvdata(pdev);
	int ret = 0;

	if (watchdog_active(wdd))
		ret = cros_ec_wdt_start(wdd);

	return ret;
}

static const struct platform_device_id cros_ec_wdt_id[] = {
	{ DRV_NAME, 0 },
	{}
};

static struct platform_driver cros_ec_wdt_driver = {
	.probe		= cros_ec_wdt_probe,
	.suspend	= pm_ptr(cros_ec_wdt_suspend),
	.resume		= pm_ptr(cros_ec_wdt_resume),
	.driver		= {
		.name	= DRV_NAME,
	},
	.id_table	= cros_ec_wdt_id,
};

module_platform_driver(cros_ec_wdt_driver);

MODULE_DEVICE_TABLE(platform, cros_ec_wdt_id);
MODULE_DESCRIPTION("Cros EC Watchdog Device Driver");
MODULE_LICENSE("GPL");

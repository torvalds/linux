// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2018-2019 NXP.
 */

#include <linux/arm-smccc.h>
#include <linux/io.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/reboot.h>
#include <linux/watchdog.h>

#define DEFAULT_TIMEOUT 60
/*
 * Software timer tick implemented in scfw side, support 10ms to 0xffffffff ms
 * in theory, but for normal case, 1s~128s is enough, you can change this max
 * value in case it's not enough.
 */
#define MAX_TIMEOUT 128

#define IMX_SIP_TIMER			0xC2000002
#define IMX_SIP_TIMER_START_WDOG		0x01
#define IMX_SIP_TIMER_STOP_WDOG		0x02
#define IMX_SIP_TIMER_SET_WDOG_ACT	0x03
#define IMX_SIP_TIMER_PING_WDOG		0x04
#define IMX_SIP_TIMER_SET_TIMEOUT_WDOG	0x05
#define IMX_SIP_TIMER_GET_WDOG_STAT	0x06
#define IMX_SIP_TIMER_SET_PRETIME_WDOG	0x07

#define SC_TIMER_WDOG_ACTION_PARTITION	0

static bool nowayout = WATCHDOG_NOWAYOUT;
module_param(nowayout, bool, 0000);
MODULE_PARM_DESC(nowayout, "Watchdog cannot be stopped once started (default="
		 __MODULE_STRING(WATCHDOG_NOWAYOUT) ")");

static int imx_sc_wdt_ping(struct watchdog_device *wdog)
{
	struct arm_smccc_res res;

	arm_smccc_smc(IMX_SIP_TIMER, IMX_SIP_TIMER_PING_WDOG,
		      0, 0, 0, 0, 0, 0, &res);

	return 0;
}

static int imx_sc_wdt_start(struct watchdog_device *wdog)
{
	struct arm_smccc_res res;

	arm_smccc_smc(IMX_SIP_TIMER, IMX_SIP_TIMER_START_WDOG,
		      0, 0, 0, 0, 0, 0, &res);
	if (res.a0)
		return -EACCES;

	arm_smccc_smc(IMX_SIP_TIMER, IMX_SIP_TIMER_SET_WDOG_ACT,
		      SC_TIMER_WDOG_ACTION_PARTITION,
		      0, 0, 0, 0, 0, &res);
	return res.a0 ? -EACCES : 0;
}

static int imx_sc_wdt_stop(struct watchdog_device *wdog)
{
	struct arm_smccc_res res;

	arm_smccc_smc(IMX_SIP_TIMER, IMX_SIP_TIMER_STOP_WDOG,
		      0, 0, 0, 0, 0, 0, &res);

	return res.a0 ? -EACCES : 0;
}

static int imx_sc_wdt_set_timeout(struct watchdog_device *wdog,
				unsigned int timeout)
{
	struct arm_smccc_res res;

	wdog->timeout = timeout;
	arm_smccc_smc(IMX_SIP_TIMER, IMX_SIP_TIMER_SET_TIMEOUT_WDOG,
		      timeout * 1000, 0, 0, 0, 0, 0, &res);

	return res.a0 ? -EACCES : 0;
}

static const struct watchdog_ops imx_sc_wdt_ops = {
	.owner = THIS_MODULE,
	.start = imx_sc_wdt_start,
	.stop  = imx_sc_wdt_stop,
	.ping  = imx_sc_wdt_ping,
	.set_timeout = imx_sc_wdt_set_timeout,
};

static const struct watchdog_info imx_sc_wdt_info = {
	.identity	= "i.MX SC watchdog timer",
	.options	= WDIOF_SETTIMEOUT | WDIOF_KEEPALIVEPING |
			  WDIOF_MAGICCLOSE | WDIOF_PRETIMEOUT,
};

static int imx_sc_wdt_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct watchdog_device *imx_sc_wdd;
	int ret;

	imx_sc_wdd = devm_kzalloc(dev, sizeof(*imx_sc_wdd), GFP_KERNEL);
	if (!imx_sc_wdd)
		return -ENOMEM;

	platform_set_drvdata(pdev, imx_sc_wdd);

	imx_sc_wdd->info = &imx_sc_wdt_info;
	imx_sc_wdd->ops = &imx_sc_wdt_ops;
	imx_sc_wdd->min_timeout = 1;
	imx_sc_wdd->max_timeout = MAX_TIMEOUT;
	imx_sc_wdd->parent = dev;
	imx_sc_wdd->timeout = DEFAULT_TIMEOUT;

	watchdog_init_timeout(imx_sc_wdd, 0, dev);
	watchdog_stop_on_reboot(imx_sc_wdd);
	watchdog_stop_on_unregister(imx_sc_wdd);

	ret = devm_watchdog_register_device(dev, imx_sc_wdd);
	if (ret) {
		dev_err(dev, "Failed to register watchdog device\n");
		return ret;
	}

	return 0;
}

static int __maybe_unused imx_sc_wdt_suspend(struct device *dev)
{
	struct watchdog_device *imx_sc_wdd = dev_get_drvdata(dev);

	if (watchdog_active(imx_sc_wdd))
		imx_sc_wdt_stop(imx_sc_wdd);

	return 0;
}

static int __maybe_unused imx_sc_wdt_resume(struct device *dev)
{
	struct watchdog_device *imx_sc_wdd = dev_get_drvdata(dev);

	if (watchdog_active(imx_sc_wdd))
		imx_sc_wdt_start(imx_sc_wdd);

	return 0;
}

static SIMPLE_DEV_PM_OPS(imx_sc_wdt_pm_ops,
			 imx_sc_wdt_suspend, imx_sc_wdt_resume);

static const struct of_device_id imx_sc_wdt_dt_ids[] = {
	{ .compatible = "fsl,imx-sc-wdt", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, imx_sc_wdt_dt_ids);

static struct platform_driver imx_sc_wdt_driver = {
	.probe		= imx_sc_wdt_probe,
	.driver		= {
		.name	= "imx-sc-wdt",
		.of_match_table = imx_sc_wdt_dt_ids,
		.pm	= &imx_sc_wdt_pm_ops,
	},
};
module_platform_driver(imx_sc_wdt_driver);

MODULE_AUTHOR("Robin Gong <yibin.gong@nxp.com>");
MODULE_DESCRIPTION("NXP i.MX system controller watchdog driver");
MODULE_LICENSE("GPL v2");

// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2018-2019 NXP.
 */

#include <linux/arm-smccc.h>
#include <linux/firmware/imx/sci.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/of.h>
#include <linux/platform_device.h>
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

#define SC_IRQ_WDOG			1
#define SC_IRQ_GROUP_WDOG		1

static bool nowayout = WATCHDOG_NOWAYOUT;
module_param(nowayout, bool, 0000);
MODULE_PARM_DESC(nowayout, "Watchdog cannot be stopped once started (default="
		 __MODULE_STRING(WATCHDOG_NOWAYOUT) ")");

struct imx_sc_wdt_device {
	struct watchdog_device wdd;
	struct notifier_block wdt_notifier;
};

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

static int imx_sc_wdt_set_pretimeout(struct watchdog_device *wdog,
				     unsigned int pretimeout)
{
	struct arm_smccc_res res;

	/*
	 * SCU firmware calculates pretimeout based on current time
	 * stamp instead of watchdog timeout stamp, need to convert
	 * the pretimeout to SCU firmware's timeout value.
	 */
	arm_smccc_smc(IMX_SIP_TIMER, IMX_SIP_TIMER_SET_PRETIME_WDOG,
		      (wdog->timeout - pretimeout) * 1000, 0, 0, 0,
		      0, 0, &res);
	if (res.a0)
		return -EACCES;

	wdog->pretimeout = pretimeout;

	return 0;
}

static int imx_sc_wdt_notify(struct notifier_block *nb,
			     unsigned long event, void *group)
{
	struct imx_sc_wdt_device *imx_sc_wdd =
				 container_of(nb,
					      struct imx_sc_wdt_device,
					      wdt_notifier);

	if (event & SC_IRQ_WDOG &&
	    *(u8 *)group == SC_IRQ_GROUP_WDOG)
		watchdog_notify_pretimeout(&imx_sc_wdd->wdd);

	return 0;
}

static void imx_sc_wdt_action(void *data)
{
	struct notifier_block *wdt_notifier = data;

	imx_scu_irq_unregister_notifier(wdt_notifier);
	imx_scu_irq_group_enable(SC_IRQ_GROUP_WDOG,
				 SC_IRQ_WDOG,
				 false);
}

static const struct watchdog_ops imx_sc_wdt_ops = {
	.owner = THIS_MODULE,
	.start = imx_sc_wdt_start,
	.stop  = imx_sc_wdt_stop,
	.ping  = imx_sc_wdt_ping,
	.set_timeout = imx_sc_wdt_set_timeout,
	.set_pretimeout = imx_sc_wdt_set_pretimeout,
};

static struct watchdog_info imx_sc_wdt_info = {
	.identity	= "i.MX SC watchdog timer",
	.options	= WDIOF_SETTIMEOUT | WDIOF_KEEPALIVEPING |
			  WDIOF_MAGICCLOSE,
};

static int imx_sc_wdt_probe(struct platform_device *pdev)
{
	struct imx_sc_wdt_device *imx_sc_wdd;
	struct watchdog_device *wdog;
	struct device *dev = &pdev->dev;
	int ret;

	imx_sc_wdd = devm_kzalloc(dev, sizeof(*imx_sc_wdd), GFP_KERNEL);
	if (!imx_sc_wdd)
		return -ENOMEM;

	platform_set_drvdata(pdev, imx_sc_wdd);

	wdog = &imx_sc_wdd->wdd;
	wdog->info = &imx_sc_wdt_info;
	wdog->ops = &imx_sc_wdt_ops;
	wdog->min_timeout = 1;
	wdog->max_timeout = MAX_TIMEOUT;
	wdog->parent = dev;
	wdog->timeout = DEFAULT_TIMEOUT;

	watchdog_init_timeout(wdog, 0, dev);

	ret = imx_sc_wdt_set_timeout(wdog, wdog->timeout);
	if (ret)
		return ret;

	watchdog_stop_on_reboot(wdog);
	watchdog_stop_on_unregister(wdog);

	ret = imx_scu_irq_group_enable(SC_IRQ_GROUP_WDOG,
				       SC_IRQ_WDOG,
				       true);
	if (ret) {
		dev_warn(dev, "Enable irq failed, pretimeout NOT supported\n");
		goto register_device;
	}

	imx_sc_wdd->wdt_notifier.notifier_call = imx_sc_wdt_notify;
	ret = imx_scu_irq_register_notifier(&imx_sc_wdd->wdt_notifier);
	if (ret) {
		imx_scu_irq_group_enable(SC_IRQ_GROUP_WDOG,
					 SC_IRQ_WDOG,
					 false);
		dev_warn(dev,
			 "Register irq notifier failed, pretimeout NOT supported\n");
		goto register_device;
	}

	ret = devm_add_action_or_reset(dev, imx_sc_wdt_action,
				       &imx_sc_wdd->wdt_notifier);
	if (!ret)
		imx_sc_wdt_info.options |= WDIOF_PRETIMEOUT;
	else
		dev_warn(dev, "Add action failed, pretimeout NOT supported\n");

register_device:
	return devm_watchdog_register_device(dev, wdog);
}

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
	},
};
module_platform_driver(imx_sc_wdt_driver);

MODULE_AUTHOR("Robin Gong <yibin.gong@nxp.com>");
MODULE_DESCRIPTION("NXP i.MX system controller watchdog driver");
MODULE_LICENSE("GPL v2");

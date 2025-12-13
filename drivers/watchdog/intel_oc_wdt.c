// SPDX-License-Identifier: GPL-2.0
/*
 * Intel OC Watchdog driver
 *
 * Copyright (C) 2025, Siemens
 * Author: Diogo Ivo <diogo.ivo@siemens.com>
 */

#define DRV_NAME	"intel_oc_wdt"

#include <linux/acpi.h>
#include <linux/bits.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/platform_device.h>
#include <linux/watchdog.h>

#define INTEL_OC_WDT_TOV		GENMASK(9, 0)
#define INTEL_OC_WDT_MIN_TOV		1
#define INTEL_OC_WDT_MAX_TOV		1024
#define INTEL_OC_WDT_DEF_TOV		60

/*
 * One-time writable lock bit. If set forbids
 * modification of itself, _TOV and _EN until
 * next reboot.
 */
#define INTEL_OC_WDT_CTL_LCK		BIT(12)

#define INTEL_OC_WDT_EN			BIT(14)
#define INTEL_OC_WDT_NO_ICCSURV_STS	BIT(24)
#define INTEL_OC_WDT_ICCSURV_STS	BIT(25)
#define INTEL_OC_WDT_RLD		BIT(31)

#define INTEL_OC_WDT_STS_BITS (INTEL_OC_WDT_NO_ICCSURV_STS | \
			       INTEL_OC_WDT_ICCSURV_STS)

#define INTEL_OC_WDT_CTRL_REG(wdt)	((wdt)->ctrl_res->start)

struct intel_oc_wdt {
	struct watchdog_device wdd;
	struct resource *ctrl_res;
	struct watchdog_info info;
	bool locked;
};

static int heartbeat;
module_param(heartbeat, uint, 0);
MODULE_PARM_DESC(heartbeat, "Watchdog heartbeats in seconds. (default="
		 __MODULE_STRING(WDT_HEARTBEAT) ")");

static bool nowayout = WATCHDOG_NOWAYOUT;
module_param(nowayout, bool, 0);
MODULE_PARM_DESC(nowayout, "Watchdog cannot be stopped once started (default="
		 __MODULE_STRING(WATCHDOG_NOWAYOUT) ")");

static int intel_oc_wdt_start(struct watchdog_device *wdd)
{
	struct intel_oc_wdt *oc_wdt = watchdog_get_drvdata(wdd);

	if (oc_wdt->locked)
		return 0;

	outl(inl(INTEL_OC_WDT_CTRL_REG(oc_wdt)) | INTEL_OC_WDT_EN,
	     INTEL_OC_WDT_CTRL_REG(oc_wdt));

	return 0;
}

static int intel_oc_wdt_stop(struct watchdog_device *wdd)
{
	struct intel_oc_wdt *oc_wdt = watchdog_get_drvdata(wdd);

	outl(inl(INTEL_OC_WDT_CTRL_REG(oc_wdt)) & ~INTEL_OC_WDT_EN,
	     INTEL_OC_WDT_CTRL_REG(oc_wdt));

	return 0;
}

static int intel_oc_wdt_ping(struct watchdog_device *wdd)
{
	struct intel_oc_wdt *oc_wdt = watchdog_get_drvdata(wdd);

	outl(inl(INTEL_OC_WDT_CTRL_REG(oc_wdt)) | INTEL_OC_WDT_RLD,
	     INTEL_OC_WDT_CTRL_REG(oc_wdt));

	return 0;
}

static int intel_oc_wdt_set_timeout(struct watchdog_device *wdd,
				    unsigned int t)
{
	struct intel_oc_wdt *oc_wdt = watchdog_get_drvdata(wdd);

	outl((inl(INTEL_OC_WDT_CTRL_REG(oc_wdt)) & ~INTEL_OC_WDT_TOV) | (t - 1),
	     INTEL_OC_WDT_CTRL_REG(oc_wdt));

	wdd->timeout = t;

	return 0;
}

static const struct watchdog_info intel_oc_wdt_info = {
	.options = WDIOF_SETTIMEOUT | WDIOF_MAGICCLOSE | WDIOF_KEEPALIVEPING,
	.identity = DRV_NAME,
};

static const struct watchdog_ops intel_oc_wdt_ops = {
	.owner = THIS_MODULE,
	.start = intel_oc_wdt_start,
	.stop = intel_oc_wdt_stop,
	.ping = intel_oc_wdt_ping,
	.set_timeout = intel_oc_wdt_set_timeout,
};

static int intel_oc_wdt_setup(struct intel_oc_wdt *oc_wdt)
{
	unsigned long val;

	val = inl(INTEL_OC_WDT_CTRL_REG(oc_wdt));

	if (val & INTEL_OC_WDT_STS_BITS)
		oc_wdt->wdd.bootstatus |= WDIOF_CARDRESET;

	oc_wdt->locked = !!(val & INTEL_OC_WDT_CTL_LCK);

	if (val & INTEL_OC_WDT_EN) {
		/*
		 * No need to issue a ping here to "commit" the new timeout
		 * value to hardware as the watchdog core schedules one
		 * immediately when registering the watchdog.
		 */
		set_bit(WDOG_HW_RUNNING, &oc_wdt->wdd.status);

		if (oc_wdt->locked) {
			/*
			 * Set nowayout unconditionally as we cannot stop
			 * the watchdog.
			 */
			nowayout = true;
			/*
			 * If we are locked read the current timeout value
			 * and inform the core we can't change it.
			 */
			oc_wdt->wdd.timeout = (val & INTEL_OC_WDT_TOV) + 1;
			oc_wdt->info.options &= ~WDIOF_SETTIMEOUT;

			dev_info(oc_wdt->wdd.parent,
				 "Register access locked, heartbeat fixed at: %u s\n",
				 oc_wdt->wdd.timeout);
		}
	} else if (oc_wdt->locked) {
		/*
		 * In case the watchdog is disabled and locked there
		 * is nothing we can do with it so just fail probing.
		 */
		return -EACCES;
	}

	val &= ~INTEL_OC_WDT_TOV;
	outl(val | (oc_wdt->wdd.timeout - 1), INTEL_OC_WDT_CTRL_REG(oc_wdt));

	return 0;
}

static int intel_oc_wdt_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct intel_oc_wdt *oc_wdt;
	struct watchdog_device *wdd;
	int ret;

	oc_wdt = devm_kzalloc(&pdev->dev, sizeof(*oc_wdt), GFP_KERNEL);
	if (!oc_wdt)
		return -ENOMEM;

	oc_wdt->ctrl_res = platform_get_resource(pdev, IORESOURCE_IO, 0);
	if (!oc_wdt->ctrl_res) {
		dev_err(&pdev->dev, "missing I/O resource\n");
		return -ENODEV;
	}

	if (!devm_request_region(&pdev->dev, oc_wdt->ctrl_res->start,
				 resource_size(oc_wdt->ctrl_res), pdev->name)) {
		dev_err(dev, "resource %pR already in use, device disabled\n",
			oc_wdt->ctrl_res);
		return -EBUSY;
	}

	wdd = &oc_wdt->wdd;
	wdd->min_timeout = INTEL_OC_WDT_MIN_TOV;
	wdd->max_timeout = INTEL_OC_WDT_MAX_TOV;
	wdd->timeout = INTEL_OC_WDT_DEF_TOV;
	oc_wdt->info = intel_oc_wdt_info;
	wdd->info = &oc_wdt->info;
	wdd->ops = &intel_oc_wdt_ops;
	wdd->parent = dev;

	watchdog_init_timeout(wdd, heartbeat, dev);

	ret = intel_oc_wdt_setup(oc_wdt);
	if (ret)
		return ret;

	watchdog_set_drvdata(wdd, oc_wdt);
	watchdog_set_nowayout(wdd, nowayout);
	watchdog_stop_on_reboot(wdd);
	watchdog_stop_on_unregister(wdd);

	return devm_watchdog_register_device(dev, wdd);
}

static const struct acpi_device_id intel_oc_wdt_match[] = {
	{ "INT3F0D" },
	{ "INTC1099" },
	{ },
};
MODULE_DEVICE_TABLE(acpi, intel_oc_wdt_match);

static struct platform_driver intel_oc_wdt_platform_driver = {
	.driver = {
		.name = DRV_NAME,
		.acpi_match_table = intel_oc_wdt_match,
	},
	.probe = intel_oc_wdt_probe,
};

module_platform_driver(intel_oc_wdt_platform_driver);

MODULE_AUTHOR("Diogo Ivo <diogo.ivo@siemens.com>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Intel OC Watchdog driver");

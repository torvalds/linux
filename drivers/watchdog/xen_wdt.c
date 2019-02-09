/*
 *	Xen Watchdog Driver
 *
 *	(c) Copyright 2010 Novell, Inc.
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 */

#define DRV_NAME	"xen_wdt"

#include <linux/bug.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/hrtimer.h>
#include <linux/kernel.h>
#include <linux/ktime.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/platform_device.h>
#include <linux/watchdog.h>
#include <xen/xen.h>
#include <asm/xen/hypercall.h>
#include <xen/interface/sched.h>

static struct platform_device *platform_device;
static struct sched_watchdog wdt;
static time64_t wdt_expires;

#define WATCHDOG_TIMEOUT 60 /* in seconds */
static unsigned int timeout;
module_param(timeout, uint, S_IRUGO);
MODULE_PARM_DESC(timeout, "Watchdog timeout in seconds "
	"(default=" __MODULE_STRING(WATCHDOG_TIMEOUT) ")");

static bool nowayout = WATCHDOG_NOWAYOUT;
module_param(nowayout, bool, S_IRUGO);
MODULE_PARM_DESC(nowayout, "Watchdog cannot be stopped once started "
	"(default=" __MODULE_STRING(WATCHDOG_NOWAYOUT) ")");

static inline time64_t set_timeout(struct watchdog_device *wdd)
{
	wdt.timeout = wdd->timeout;
	return ktime_get_seconds() + wdd->timeout;
}

static int xen_wdt_start(struct watchdog_device *wdd)
{
	time64_t expires;
	int err;

	expires = set_timeout(wdd);
	if (!wdt.id)
		err = HYPERVISOR_sched_op(SCHEDOP_watchdog, &wdt);
	else
		err = -EBUSY;
	if (err > 0) {
		wdt.id = err;
		wdt_expires = expires;
		err = 0;
	} else
		BUG_ON(!err);

	return err;
}

static int xen_wdt_stop(struct watchdog_device *wdd)
{
	int err = 0;

	wdt.timeout = 0;
	if (wdt.id)
		err = HYPERVISOR_sched_op(SCHEDOP_watchdog, &wdt);
	if (!err)
		wdt.id = 0;

	return err;
}

static int xen_wdt_kick(struct watchdog_device *wdd)
{
	time64_t expires;
	int err;

	expires = set_timeout(wdd);
	if (wdt.id)
		err = HYPERVISOR_sched_op(SCHEDOP_watchdog, &wdt);
	else
		err = -ENXIO;
	if (!err)
		wdt_expires = expires;

	return err;
}

static unsigned int xen_wdt_get_timeleft(struct watchdog_device *wdd)
{
	return wdt_expires - ktime_get_seconds();
}

static struct watchdog_info xen_wdt_info = {
	.identity = DRV_NAME,
	.options = WDIOF_SETTIMEOUT | WDIOF_KEEPALIVEPING | WDIOF_MAGICCLOSE,
};

static const struct watchdog_ops xen_wdt_ops = {
	.owner = THIS_MODULE,
	.start = xen_wdt_start,
	.stop = xen_wdt_stop,
	.ping = xen_wdt_kick,
	.get_timeleft = xen_wdt_get_timeleft,
};

static struct watchdog_device xen_wdt_dev = {
	.info = &xen_wdt_info,
	.ops = &xen_wdt_ops,
	.timeout = WATCHDOG_TIMEOUT,
};

static int xen_wdt_probe(struct platform_device *pdev)
{
	struct sched_watchdog wd = { .id = ~0 };
	int ret = HYPERVISOR_sched_op(SCHEDOP_watchdog, &wd);

	if (ret == -ENOSYS) {
		dev_err(&pdev->dev, "watchdog not supported by hypervisor\n");
		return -ENODEV;
	}

	if (ret != -EINVAL) {
		dev_err(&pdev->dev, "unexpected hypervisor error (%d)\n", ret);
		return -ENODEV;
	}

	if (watchdog_init_timeout(&xen_wdt_dev, timeout, NULL))
		dev_info(&pdev->dev, "timeout value invalid, using %d\n",
			xen_wdt_dev.timeout);
	watchdog_set_nowayout(&xen_wdt_dev, nowayout);
	watchdog_stop_on_reboot(&xen_wdt_dev);
	watchdog_stop_on_unregister(&xen_wdt_dev);

	ret = devm_watchdog_register_device(&pdev->dev, &xen_wdt_dev);
	if (ret) {
		dev_err(&pdev->dev, "cannot register watchdog device (%d)\n",
			ret);
		return ret;
	}

	dev_info(&pdev->dev, "initialized (timeout=%ds, nowayout=%d)\n",
		xen_wdt_dev.timeout, nowayout);

	return 0;
}

static int xen_wdt_suspend(struct platform_device *dev, pm_message_t state)
{
	typeof(wdt.id) id = wdt.id;
	int rc = xen_wdt_stop(&xen_wdt_dev);

	wdt.id = id;
	return rc;
}

static int xen_wdt_resume(struct platform_device *dev)
{
	if (!wdt.id)
		return 0;
	wdt.id = 0;
	return xen_wdt_start(&xen_wdt_dev);
}

static struct platform_driver xen_wdt_driver = {
	.probe          = xen_wdt_probe,
	.suspend        = xen_wdt_suspend,
	.resume         = xen_wdt_resume,
	.driver         = {
		.name   = DRV_NAME,
	},
};

static int __init xen_wdt_init_module(void)
{
	int err;

	if (!xen_domain())
		return -ENODEV;

	err = platform_driver_register(&xen_wdt_driver);
	if (err)
		return err;

	platform_device = platform_device_register_simple(DRV_NAME,
								  -1, NULL, 0);
	if (IS_ERR(platform_device)) {
		err = PTR_ERR(platform_device);
		platform_driver_unregister(&xen_wdt_driver);
	}

	return err;
}

static void __exit xen_wdt_cleanup_module(void)
{
	platform_device_unregister(platform_device);
	platform_driver_unregister(&xen_wdt_driver);
}

module_init(xen_wdt_init_module);
module_exit(xen_wdt_cleanup_module);

MODULE_AUTHOR("Jan Beulich <jbeulich@novell.com>");
MODULE_DESCRIPTION("Xen WatchDog Timer Driver");
MODULE_LICENSE("GPL");

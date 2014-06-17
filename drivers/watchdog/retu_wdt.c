/*
 * Retu watchdog driver
 *
 * Copyright (C) 2004, 2005 Nokia Corporation
 *
 * Based on code written by Amit Kucheria and Michael Buesch.
 * Rewritten by Aaro Koskinen.
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License. See the file "COPYING" in the main directory of this
 * archive for more details.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mfd/retu.h>
#include <linux/watchdog.h>
#include <linux/platform_device.h>

/* Watchdog timer values in seconds */
#define RETU_WDT_MAX_TIMER	63

struct retu_wdt_dev {
	struct retu_dev		*rdev;
	struct device		*dev;
	struct delayed_work	ping_work;
};

/*
 * Since Retu watchdog cannot be disabled in hardware, we must kick it
 * with a timer until userspace watchdog software takes over. If
 * CONFIG_WATCHDOG_NOWAYOUT is set, we never start the feeding.
 */
static void retu_wdt_ping_enable(struct retu_wdt_dev *wdev)
{
	retu_write(wdev->rdev, RETU_REG_WATCHDOG, RETU_WDT_MAX_TIMER);
	schedule_delayed_work(&wdev->ping_work,
			round_jiffies_relative(RETU_WDT_MAX_TIMER * HZ / 2));
}

static void retu_wdt_ping_disable(struct retu_wdt_dev *wdev)
{
	retu_write(wdev->rdev, RETU_REG_WATCHDOG, RETU_WDT_MAX_TIMER);
	cancel_delayed_work_sync(&wdev->ping_work);
}

static void retu_wdt_ping_work(struct work_struct *work)
{
	struct retu_wdt_dev *wdev = container_of(to_delayed_work(work),
						struct retu_wdt_dev, ping_work);
	retu_wdt_ping_enable(wdev);
}

static int retu_wdt_start(struct watchdog_device *wdog)
{
	struct retu_wdt_dev *wdev = watchdog_get_drvdata(wdog);

	retu_wdt_ping_disable(wdev);

	return retu_write(wdev->rdev, RETU_REG_WATCHDOG, wdog->timeout);
}

static int retu_wdt_stop(struct watchdog_device *wdog)
{
	struct retu_wdt_dev *wdev = watchdog_get_drvdata(wdog);

	retu_wdt_ping_enable(wdev);

	return 0;
}

static int retu_wdt_ping(struct watchdog_device *wdog)
{
	struct retu_wdt_dev *wdev = watchdog_get_drvdata(wdog);

	return retu_write(wdev->rdev, RETU_REG_WATCHDOG, wdog->timeout);
}

static int retu_wdt_set_timeout(struct watchdog_device *wdog,
				unsigned int timeout)
{
	struct retu_wdt_dev *wdev = watchdog_get_drvdata(wdog);

	wdog->timeout = timeout;
	return retu_write(wdev->rdev, RETU_REG_WATCHDOG, wdog->timeout);
}

static const struct watchdog_info retu_wdt_info = {
	.options = WDIOF_SETTIMEOUT | WDIOF_KEEPALIVEPING,
	.identity = "Retu watchdog",
};

static const struct watchdog_ops retu_wdt_ops = {
	.owner		= THIS_MODULE,
	.start		= retu_wdt_start,
	.stop		= retu_wdt_stop,
	.ping		= retu_wdt_ping,
	.set_timeout	= retu_wdt_set_timeout,
};

static int retu_wdt_probe(struct platform_device *pdev)
{
	struct retu_dev *rdev = dev_get_drvdata(pdev->dev.parent);
	bool nowayout = WATCHDOG_NOWAYOUT;
	struct watchdog_device *retu_wdt;
	struct retu_wdt_dev *wdev;
	int ret;

	retu_wdt = devm_kzalloc(&pdev->dev, sizeof(*retu_wdt), GFP_KERNEL);
	if (!retu_wdt)
		return -ENOMEM;

	wdev = devm_kzalloc(&pdev->dev, sizeof(*wdev), GFP_KERNEL);
	if (!wdev)
		return -ENOMEM;

	retu_wdt->info		= &retu_wdt_info;
	retu_wdt->ops		= &retu_wdt_ops;
	retu_wdt->timeout	= RETU_WDT_MAX_TIMER;
	retu_wdt->min_timeout	= 0;
	retu_wdt->max_timeout	= RETU_WDT_MAX_TIMER;

	watchdog_set_drvdata(retu_wdt, wdev);
	watchdog_set_nowayout(retu_wdt, nowayout);

	wdev->rdev		= rdev;
	wdev->dev		= &pdev->dev;

	INIT_DELAYED_WORK(&wdev->ping_work, retu_wdt_ping_work);

	ret = watchdog_register_device(retu_wdt);
	if (ret < 0)
		return ret;

	if (nowayout)
		retu_wdt_ping(retu_wdt);
	else
		retu_wdt_ping_enable(wdev);

	platform_set_drvdata(pdev, retu_wdt);

	return 0;
}

static int retu_wdt_remove(struct platform_device *pdev)
{
	struct watchdog_device *wdog = platform_get_drvdata(pdev);
	struct retu_wdt_dev *wdev = watchdog_get_drvdata(wdog);

	watchdog_unregister_device(wdog);
	cancel_delayed_work_sync(&wdev->ping_work);

	return 0;
}

static struct platform_driver retu_wdt_driver = {
	.probe		= retu_wdt_probe,
	.remove		= retu_wdt_remove,
	.driver		= {
		.name	= "retu-wdt",
	},
};
module_platform_driver(retu_wdt_driver);

MODULE_ALIAS("platform:retu-wdt");
MODULE_DESCRIPTION("Retu watchdog");
MODULE_AUTHOR("Amit Kucheria");
MODULE_AUTHOR("Aaro Koskinen <aaro.koskinen@iki.fi>");
MODULE_LICENSE("GPL");

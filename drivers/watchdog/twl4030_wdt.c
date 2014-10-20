/*
 * Copyright (C) Nokia Corporation
 *
 * Written by Timo Kokkonen <timo.t.kokkonen at nokia.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/watchdog.h>
#include <linux/platform_device.h>
#include <linux/i2c/twl.h>

#define TWL4030_WATCHDOG_CFG_REG_OFFS	0x3

static bool nowayout = WATCHDOG_NOWAYOUT;
module_param(nowayout, bool, 0);
MODULE_PARM_DESC(nowayout, "Watchdog cannot be stopped once started "
	"(default=" __MODULE_STRING(WATCHDOG_NOWAYOUT) ")");

static int twl4030_wdt_write(unsigned char val)
{
	return twl_i2c_write_u8(TWL_MODULE_PM_RECEIVER, val,
					TWL4030_WATCHDOG_CFG_REG_OFFS);
}

static int twl4030_wdt_start(struct watchdog_device *wdt)
{
	return twl4030_wdt_write(wdt->timeout + 1);
}

static int twl4030_wdt_stop(struct watchdog_device *wdt)
{
	return twl4030_wdt_write(0);
}

static int twl4030_wdt_set_timeout(struct watchdog_device *wdt,
				   unsigned int timeout)
{
	wdt->timeout = timeout;
	return 0;
}

static const struct watchdog_info twl4030_wdt_info = {
	.options = WDIOF_SETTIMEOUT | WDIOF_KEEPALIVEPING,
	.identity = "TWL4030 Watchdog",
};

static const struct watchdog_ops twl4030_wdt_ops = {
	.owner		= THIS_MODULE,
	.start		= twl4030_wdt_start,
	.stop		= twl4030_wdt_stop,
	.set_timeout	= twl4030_wdt_set_timeout,
};

static int twl4030_wdt_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct watchdog_device *wdt;

	wdt = devm_kzalloc(&pdev->dev, sizeof(*wdt), GFP_KERNEL);
	if (!wdt)
		return -ENOMEM;

	wdt->info		= &twl4030_wdt_info;
	wdt->ops		= &twl4030_wdt_ops;
	wdt->status		= 0;
	wdt->timeout		= 30;
	wdt->min_timeout	= 1;
	wdt->max_timeout	= 30;

	watchdog_set_nowayout(wdt, nowayout);
	platform_set_drvdata(pdev, wdt);

	twl4030_wdt_stop(wdt);

	ret = watchdog_register_device(wdt);
	if (ret)
		return ret;

	return 0;
}

static int twl4030_wdt_remove(struct platform_device *pdev)
{
	struct watchdog_device *wdt = platform_get_drvdata(pdev);

	watchdog_unregister_device(wdt);

	return 0;
}

#ifdef CONFIG_PM
static int twl4030_wdt_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct watchdog_device *wdt = platform_get_drvdata(pdev);
	if (watchdog_active(wdt))
		return twl4030_wdt_stop(wdt);

	return 0;
}

static int twl4030_wdt_resume(struct platform_device *pdev)
{
	struct watchdog_device *wdt = platform_get_drvdata(pdev);
	if (watchdog_active(wdt))
		return twl4030_wdt_start(wdt);

	return 0;
}
#else
#define twl4030_wdt_suspend        NULL
#define twl4030_wdt_resume         NULL
#endif

static const struct of_device_id twl_wdt_of_match[] = {
	{ .compatible = "ti,twl4030-wdt", },
	{ },
};
MODULE_DEVICE_TABLE(of, twl_wdt_of_match);

static struct platform_driver twl4030_wdt_driver = {
	.probe		= twl4030_wdt_probe,
	.remove		= twl4030_wdt_remove,
	.suspend	= twl4030_wdt_suspend,
	.resume		= twl4030_wdt_resume,
	.driver		= {
		.name		= "twl4030_wdt",
		.of_match_table	= twl_wdt_of_match,
	},
};

module_platform_driver(twl4030_wdt_driver);

MODULE_AUTHOR("Nokia Corporation");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:twl4030_wdt");


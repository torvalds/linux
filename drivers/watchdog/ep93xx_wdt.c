/*
 * Watchdog driver for Cirrus Logic EP93xx family of devices.
 *
 * Copyright (c) 2004 Ray Lehtiniemi
 * Copyright (c) 2006 Tower Technologies
 * Based on ep93xx driver, bits from alim7101_wdt.c
 *
 * Authors: Ray Lehtiniemi <rayl@mail.com>,
 *	Alessandro Zummo <a.zummo@towertech.it>
 *
 * Copyright (c) 2012 H Hartley Sweeten <hsweeten@visionengravers.com>
 *	Convert to a platform device and use the watchdog framework API
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 *
 * This watchdog fires after 250msec, which is a too short interval
 * for us to rely on the user space daemon alone. So we ping the
 * wdt each ~200msec and eventually stop doing it if the user space
 * daemon dies.
 *
 * TODO:
 *
 *	- Test last reset from watchdog status
 *	- Add a few missing ioctls
 */

#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/watchdog.h>
#include <linux/timer.h>
#include <linux/io.h>

#define WDT_VERSION	"0.4"

/* default timeout (secs) */
#define WDT_TIMEOUT 30

static bool nowayout = WATCHDOG_NOWAYOUT;
module_param(nowayout, bool, 0);
MODULE_PARM_DESC(nowayout, "Watchdog cannot be stopped once started");

static unsigned int timeout = WDT_TIMEOUT;
module_param(timeout, uint, 0);
MODULE_PARM_DESC(timeout,
	"Watchdog timeout in seconds. (1<=timeout<=3600, default="
				__MODULE_STRING(WDT_TIMEOUT) ")");

static void __iomem *mmio_base;
static struct timer_list timer;
static unsigned long next_heartbeat;

#define EP93XX_WATCHDOG		0x00
#define EP93XX_WDSTATUS		0x04

/* reset the wdt every ~200ms - the heartbeat of the device is 0.250 seconds*/
#define WDT_INTERVAL (HZ/5)

static void ep93xx_wdt_timer_ping(unsigned long data)
{
	if (time_before(jiffies, next_heartbeat))
		writel(0x5555, mmio_base + EP93XX_WATCHDOG);

	/* Re-set the timer interval */
	mod_timer(&timer, jiffies + WDT_INTERVAL);
}

static int ep93xx_wdt_start(struct watchdog_device *wdd)
{
	next_heartbeat = jiffies + (timeout * HZ);

	writel(0xaaaa, mmio_base + EP93XX_WATCHDOG);
	mod_timer(&timer, jiffies + WDT_INTERVAL);

	return 0;
}

static int ep93xx_wdt_stop(struct watchdog_device *wdd)
{
	del_timer_sync(&timer);
	writel(0xaa55, mmio_base + EP93XX_WATCHDOG);

	return 0;
}

static int ep93xx_wdt_keepalive(struct watchdog_device *wdd)
{
	/* user land ping */
	next_heartbeat = jiffies + (timeout * HZ);

	return 0;
}

static const struct watchdog_info ep93xx_wdt_ident = {
	.options	= WDIOF_CARDRESET |
			  WDIOF_MAGICCLOSE |
			  WDIOF_KEEPALIVEPING,
	.identity	= "EP93xx Watchdog",
};

static struct watchdog_ops ep93xx_wdt_ops = {
	.owner		= THIS_MODULE,
	.start		= ep93xx_wdt_start,
	.stop		= ep93xx_wdt_stop,
	.ping		= ep93xx_wdt_keepalive,
};

static struct watchdog_device ep93xx_wdt_wdd = {
	.info		= &ep93xx_wdt_ident,
	.ops		= &ep93xx_wdt_ops,
};

static int ep93xx_wdt_probe(struct platform_device *pdev)
{
	struct resource *res;
	unsigned long val;
	int err;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	mmio_base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(mmio_base))
		return PTR_ERR(mmio_base);

	if (timeout < 1 || timeout > 3600) {
		timeout = WDT_TIMEOUT;
		dev_warn(&pdev->dev,
			"timeout value must be 1<=x<=3600, using %d\n",
			timeout);
	}

	val = readl(mmio_base + EP93XX_WATCHDOG);
	ep93xx_wdt_wdd.bootstatus = (val & 0x01) ? WDIOF_CARDRESET : 0;
	ep93xx_wdt_wdd.timeout = timeout;

	watchdog_set_nowayout(&ep93xx_wdt_wdd, nowayout);

	setup_timer(&timer, ep93xx_wdt_timer_ping, 1);

	err = watchdog_register_device(&ep93xx_wdt_wdd);
	if (err)
		return err;

	dev_info(&pdev->dev,
		"EP93XX watchdog, driver version " WDT_VERSION "%s\n",
		(val & 0x08) ? " (nCS1 disable detected)" : "");

	return 0;
}

static int ep93xx_wdt_remove(struct platform_device *pdev)
{
	watchdog_unregister_device(&ep93xx_wdt_wdd);
	return 0;
}

static struct platform_driver ep93xx_wdt_driver = {
	.driver		= {
		.owner	= THIS_MODULE,
		.name	= "ep93xx-wdt",
	},
	.probe		= ep93xx_wdt_probe,
	.remove		= ep93xx_wdt_remove,
};

module_platform_driver(ep93xx_wdt_driver);

MODULE_AUTHOR("Ray Lehtiniemi <rayl@mail.com>");
MODULE_AUTHOR("Alessandro Zummo <a.zummo@towertech.it>");
MODULE_AUTHOR("H Hartley Sweeten <hsweeten@visionengravers.com>");
MODULE_DESCRIPTION("EP93xx Watchdog");
MODULE_LICENSE("GPL");
MODULE_VERSION(WDT_VERSION);

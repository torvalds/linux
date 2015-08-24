/*
 * omap_wdt.c
 *
 * Watchdog driver for the TI OMAP 16xx & 24xx/34xx 32KHz (non-secure) watchdog
 *
 * Author: MontaVista Software, Inc.
 *	 <gdavis@mvista.com> or <source@mvista.com>
 *
 * 2003 (c) MontaVista Software, Inc. This file is licensed under the
 * terms of the GNU General Public License version 2. This program is
 * licensed "as is" without any warranty of any kind, whether express
 * or implied.
 *
 * History:
 *
 * 20030527: George G. Davis <gdavis@mvista.com>
 *	Initially based on linux-2.4.19-rmk7-pxa1/drivers/char/sa1100_wdt.c
 *	(c) Copyright 2000 Oleg Drokin <green@crimea.edu>
 *	Based on SoftDog driver by Alan Cox <alan@lxorguk.ukuu.org.uk>
 *
 * Copyright (c) 2004 Texas Instruments.
 *	1. Modified to support OMAP1610 32-KHz watchdog timer
 *	2. Ported to 2.6 kernel
 *
 * Copyright (c) 2005 David Brownell
 *	Use the driver model and standard identifiers; handle bigger timeouts.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/watchdog.h>
#include <linux/reboot.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/moduleparam.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/pm_runtime.h>
#include <linux/platform_data/omap-wd-timer.h>

#include "omap_wdt.h"

static bool nowayout = WATCHDOG_NOWAYOUT;
module_param(nowayout, bool, 0);
MODULE_PARM_DESC(nowayout, "Watchdog cannot be stopped once started "
	"(default=" __MODULE_STRING(WATCHDOG_NOWAYOUT) ")");

static unsigned timer_margin;
module_param(timer_margin, uint, 0);
MODULE_PARM_DESC(timer_margin, "initial watchdog timeout (in seconds)");

#define to_omap_wdt_dev(_wdog)	container_of(_wdog, struct omap_wdt_dev, wdog)

static bool early_enable;
module_param(early_enable, bool, 0);
MODULE_PARM_DESC(early_enable,
	"Watchdog is started on module insertion (default=0)");

struct omap_wdt_dev {
	struct watchdog_device wdog;
	void __iomem    *base;          /* physical */
	struct device   *dev;
	bool		omap_wdt_users;
	int		wdt_trgr_pattern;
	struct mutex	lock;		/* to avoid races with PM */
};

static void omap_wdt_reload(struct omap_wdt_dev *wdev)
{
	void __iomem    *base = wdev->base;

	/* wait for posted write to complete */
	while ((readl_relaxed(base + OMAP_WATCHDOG_WPS)) & 0x08)
		cpu_relax();

	wdev->wdt_trgr_pattern = ~wdev->wdt_trgr_pattern;
	writel_relaxed(wdev->wdt_trgr_pattern, (base + OMAP_WATCHDOG_TGR));

	/* wait for posted write to complete */
	while ((readl_relaxed(base + OMAP_WATCHDOG_WPS)) & 0x08)
		cpu_relax();
	/* reloaded WCRR from WLDR */
}

static void omap_wdt_enable(struct omap_wdt_dev *wdev)
{
	void __iomem *base = wdev->base;

	/* Sequence to enable the watchdog */
	writel_relaxed(0xBBBB, base + OMAP_WATCHDOG_SPR);
	while ((readl_relaxed(base + OMAP_WATCHDOG_WPS)) & 0x10)
		cpu_relax();

	writel_relaxed(0x4444, base + OMAP_WATCHDOG_SPR);
	while ((readl_relaxed(base + OMAP_WATCHDOG_WPS)) & 0x10)
		cpu_relax();
}

static void omap_wdt_disable(struct omap_wdt_dev *wdev)
{
	void __iomem *base = wdev->base;

	/* sequence required to disable watchdog */
	writel_relaxed(0xAAAA, base + OMAP_WATCHDOG_SPR);	/* TIMER_MODE */
	while (readl_relaxed(base + OMAP_WATCHDOG_WPS) & 0x10)
		cpu_relax();

	writel_relaxed(0x5555, base + OMAP_WATCHDOG_SPR);	/* TIMER_MODE */
	while (readl_relaxed(base + OMAP_WATCHDOG_WPS) & 0x10)
		cpu_relax();
}

static void omap_wdt_set_timer(struct omap_wdt_dev *wdev,
				   unsigned int timeout)
{
	u32 pre_margin = GET_WLDR_VAL(timeout);
	void __iomem *base = wdev->base;

	/* just count up at 32 KHz */
	while (readl_relaxed(base + OMAP_WATCHDOG_WPS) & 0x04)
		cpu_relax();

	writel_relaxed(pre_margin, base + OMAP_WATCHDOG_LDR);
	while (readl_relaxed(base + OMAP_WATCHDOG_WPS) & 0x04)
		cpu_relax();
}

static int omap_wdt_start(struct watchdog_device *wdog)
{
	struct omap_wdt_dev *wdev = to_omap_wdt_dev(wdog);
	void __iomem *base = wdev->base;

	mutex_lock(&wdev->lock);

	wdev->omap_wdt_users = true;

	pm_runtime_get_sync(wdev->dev);

	/*
	 * Make sure the watchdog is disabled. This is unfortunately required
	 * because writing to various registers with the watchdog running has no
	 * effect.
	 */
	omap_wdt_disable(wdev);

	/* initialize prescaler */
	while (readl_relaxed(base + OMAP_WATCHDOG_WPS) & 0x01)
		cpu_relax();

	writel_relaxed((1 << 5) | (PTV << 2), base + OMAP_WATCHDOG_CNTRL);
	while (readl_relaxed(base + OMAP_WATCHDOG_WPS) & 0x01)
		cpu_relax();

	omap_wdt_set_timer(wdev, wdog->timeout);
	omap_wdt_reload(wdev); /* trigger loading of new timeout value */
	omap_wdt_enable(wdev);

	mutex_unlock(&wdev->lock);

	return 0;
}

static int omap_wdt_stop(struct watchdog_device *wdog)
{
	struct omap_wdt_dev *wdev = to_omap_wdt_dev(wdog);

	mutex_lock(&wdev->lock);
	omap_wdt_disable(wdev);
	pm_runtime_put_sync(wdev->dev);
	wdev->omap_wdt_users = false;
	mutex_unlock(&wdev->lock);
	return 0;
}

static int omap_wdt_ping(struct watchdog_device *wdog)
{
	struct omap_wdt_dev *wdev = to_omap_wdt_dev(wdog);

	mutex_lock(&wdev->lock);
	omap_wdt_reload(wdev);
	mutex_unlock(&wdev->lock);

	return 0;
}

static int omap_wdt_set_timeout(struct watchdog_device *wdog,
				unsigned int timeout)
{
	struct omap_wdt_dev *wdev = to_omap_wdt_dev(wdog);

	mutex_lock(&wdev->lock);
	omap_wdt_disable(wdev);
	omap_wdt_set_timer(wdev, timeout);
	omap_wdt_enable(wdev);
	omap_wdt_reload(wdev);
	wdog->timeout = timeout;
	mutex_unlock(&wdev->lock);

	return 0;
}

static unsigned int omap_wdt_get_timeleft(struct watchdog_device *wdog)
{
	struct omap_wdt_dev *wdev = watchdog_get_drvdata(wdog);
	void __iomem *base = wdev->base;
	u32 value;

	value = readl_relaxed(base + OMAP_WATCHDOG_CRR);
	return GET_WCCR_SECS(value);
}

static const struct watchdog_info omap_wdt_info = {
	.options = WDIOF_SETTIMEOUT | WDIOF_MAGICCLOSE | WDIOF_KEEPALIVEPING,
	.identity = "OMAP Watchdog",
};

static const struct watchdog_ops omap_wdt_ops = {
	.owner		= THIS_MODULE,
	.start		= omap_wdt_start,
	.stop		= omap_wdt_stop,
	.ping		= omap_wdt_ping,
	.set_timeout	= omap_wdt_set_timeout,
	.get_timeleft	= omap_wdt_get_timeleft,
};

static int omap_wdt_probe(struct platform_device *pdev)
{
	struct omap_wd_timer_platform_data *pdata = dev_get_platdata(&pdev->dev);
	struct resource *res;
	struct omap_wdt_dev *wdev;
	int ret;

	wdev = devm_kzalloc(&pdev->dev, sizeof(*wdev), GFP_KERNEL);
	if (!wdev)
		return -ENOMEM;

	wdev->omap_wdt_users	= false;
	wdev->dev		= &pdev->dev;
	wdev->wdt_trgr_pattern	= 0x1234;
	mutex_init(&wdev->lock);

	/* reserve static register mappings */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	wdev->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(wdev->base))
		return PTR_ERR(wdev->base);

	wdev->wdog.info = &omap_wdt_info;
	wdev->wdog.ops = &omap_wdt_ops;
	wdev->wdog.min_timeout = TIMER_MARGIN_MIN;
	wdev->wdog.max_timeout = TIMER_MARGIN_MAX;

	if (watchdog_init_timeout(&wdev->wdog, timer_margin, &pdev->dev) < 0)
		wdev->wdog.timeout = TIMER_MARGIN_DEFAULT;

	watchdog_set_nowayout(&wdev->wdog, nowayout);

	platform_set_drvdata(pdev, wdev);

	pm_runtime_enable(wdev->dev);
	pm_runtime_get_sync(wdev->dev);

	if (pdata && pdata->read_reset_sources) {
		u32 rs = pdata->read_reset_sources();
		if (rs & (1 << OMAP_MPU_WD_RST_SRC_ID_SHIFT))
			wdev->wdog.bootstatus = WDIOF_CARDRESET;
	}

	omap_wdt_disable(wdev);

	ret = watchdog_register_device(&wdev->wdog);
	if (ret) {
		pm_runtime_disable(wdev->dev);
		return ret;
	}

	pr_info("OMAP Watchdog Timer Rev 0x%02x: initial timeout %d sec\n",
		readl_relaxed(wdev->base + OMAP_WATCHDOG_REV) & 0xFF,
		wdev->wdog.timeout);

	pm_runtime_put_sync(wdev->dev);

	if (early_enable)
		omap_wdt_start(&wdev->wdog);

	return 0;
}

static void omap_wdt_shutdown(struct platform_device *pdev)
{
	struct omap_wdt_dev *wdev = platform_get_drvdata(pdev);

	mutex_lock(&wdev->lock);
	if (wdev->omap_wdt_users) {
		omap_wdt_disable(wdev);
		pm_runtime_put_sync(wdev->dev);
	}
	mutex_unlock(&wdev->lock);
}

static int omap_wdt_remove(struct platform_device *pdev)
{
	struct omap_wdt_dev *wdev = platform_get_drvdata(pdev);

	pm_runtime_disable(wdev->dev);
	watchdog_unregister_device(&wdev->wdog);

	return 0;
}

#ifdef	CONFIG_PM

/* REVISIT ... not clear this is the best way to handle system suspend; and
 * it's very inappropriate for selective device suspend (e.g. suspending this
 * through sysfs rather than by stopping the watchdog daemon).  Also, this
 * may not play well enough with NOWAYOUT...
 */

static int omap_wdt_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct omap_wdt_dev *wdev = platform_get_drvdata(pdev);

	mutex_lock(&wdev->lock);
	if (wdev->omap_wdt_users) {
		omap_wdt_disable(wdev);
		pm_runtime_put_sync(wdev->dev);
	}
	mutex_unlock(&wdev->lock);

	return 0;
}

static int omap_wdt_resume(struct platform_device *pdev)
{
	struct omap_wdt_dev *wdev = platform_get_drvdata(pdev);

	mutex_lock(&wdev->lock);
	if (wdev->omap_wdt_users) {
		pm_runtime_get_sync(wdev->dev);
		omap_wdt_enable(wdev);
		omap_wdt_reload(wdev);
	}
	mutex_unlock(&wdev->lock);

	return 0;
}

#else
#define	omap_wdt_suspend	NULL
#define	omap_wdt_resume		NULL
#endif

static const struct of_device_id omap_wdt_of_match[] = {
	{ .compatible = "ti,omap3-wdt", },
	{},
};
MODULE_DEVICE_TABLE(of, omap_wdt_of_match);

static struct platform_driver omap_wdt_driver = {
	.probe		= omap_wdt_probe,
	.remove		= omap_wdt_remove,
	.shutdown	= omap_wdt_shutdown,
	.suspend	= omap_wdt_suspend,
	.resume		= omap_wdt_resume,
	.driver		= {
		.name	= "omap_wdt",
		.of_match_table = omap_wdt_of_match,
	},
};

module_platform_driver(omap_wdt_driver);

MODULE_AUTHOR("George G. Davis");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:omap_wdt");

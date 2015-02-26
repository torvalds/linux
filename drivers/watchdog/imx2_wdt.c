/*
 * Watchdog driver for IMX2 and later processors
 *
 *  Copyright (C) 2010 Wolfram Sang, Pengutronix e.K. <w.sang@pengutronix.de>
 *  Copyright (C) 2014 Freescale Semiconductor, Inc.
 *
 * some parts adapted by similar drivers from Darius Augulis and Vladimir
 * Zapolskiy, additional improvements by Wim Van Sebroeck.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * NOTE: MX1 has a slightly different Watchdog than MX2 and later:
 *
 *			MX1:		MX2+:
 *			----		-----
 * Registers:		32-bit		16-bit
 * Stopable timer:	Yes		No
 * Need to enable clk:	No		Yes
 * Halt on suspend:	Manual		Can be automatic
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/notifier.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/reboot.h>
#include <linux/regmap.h>
#include <linux/timer.h>
#include <linux/watchdog.h>

#define DRIVER_NAME "imx2-wdt"

#define IMX2_WDT_WCR		0x00		/* Control Register */
#define IMX2_WDT_WCR_WT		(0xFF << 8)	/* -> Watchdog Timeout Field */
#define IMX2_WDT_WCR_WRE	(1 << 3)	/* -> WDOG Reset Enable */
#define IMX2_WDT_WCR_WDE	(1 << 2)	/* -> Watchdog Enable */
#define IMX2_WDT_WCR_WDZST	(1 << 0)	/* -> Watchdog timer Suspend */

#define IMX2_WDT_WSR		0x02		/* Service Register */
#define IMX2_WDT_SEQ1		0x5555		/* -> service sequence 1 */
#define IMX2_WDT_SEQ2		0xAAAA		/* -> service sequence 2 */

#define IMX2_WDT_WRSR		0x04		/* Reset Status Register */
#define IMX2_WDT_WRSR_TOUT	(1 << 1)	/* -> Reset due to Timeout */

#define IMX2_WDT_WMCR		0x08		/* Misc Register */

#define IMX2_WDT_MAX_TIME	128
#define IMX2_WDT_DEFAULT_TIME	60		/* in seconds */

#define WDOG_SEC_TO_COUNT(s)	((s * 2 - 1) << 8)

struct imx2_wdt_device {
	struct clk *clk;
	struct regmap *regmap;
	struct timer_list timer;	/* Pings the watchdog when closed */
	struct watchdog_device wdog;
	struct notifier_block restart_handler;
};

static bool nowayout = WATCHDOG_NOWAYOUT;
module_param(nowayout, bool, 0);
MODULE_PARM_DESC(nowayout, "Watchdog cannot be stopped once started (default="
				__MODULE_STRING(WATCHDOG_NOWAYOUT) ")");


static unsigned timeout = IMX2_WDT_DEFAULT_TIME;
module_param(timeout, uint, 0);
MODULE_PARM_DESC(timeout, "Watchdog timeout in seconds (default="
				__MODULE_STRING(IMX2_WDT_DEFAULT_TIME) ")");

static const struct watchdog_info imx2_wdt_info = {
	.identity = "imx2+ watchdog",
	.options = WDIOF_KEEPALIVEPING | WDIOF_SETTIMEOUT | WDIOF_MAGICCLOSE,
};

static int imx2_restart_handler(struct notifier_block *this, unsigned long mode,
				void *cmd)
{
	unsigned int wcr_enable = IMX2_WDT_WCR_WDE;
	struct imx2_wdt_device *wdev = container_of(this,
						    struct imx2_wdt_device,
						    restart_handler);
	/* Assert SRS signal */
	regmap_write(wdev->regmap, 0, wcr_enable);
	/*
	 * Due to imx6q errata ERR004346 (WDOG: WDOG SRS bit requires to be
	 * written twice), we add another two writes to ensure there must be at
	 * least two writes happen in the same one 32kHz clock period.  We save
	 * the target check here, since the writes shouldn't be a huge burden
	 * for other platforms.
	 */
	regmap_write(wdev->regmap, 0, wcr_enable);
	regmap_write(wdev->regmap, 0, wcr_enable);

	/* wait for reset to assert... */
	mdelay(500);

	return NOTIFY_DONE;
}

static inline void imx2_wdt_setup(struct watchdog_device *wdog)
{
	struct imx2_wdt_device *wdev = watchdog_get_drvdata(wdog);
	u32 val;

	regmap_read(wdev->regmap, IMX2_WDT_WCR, &val);

	/* Suspend timer in low power mode, write once-only */
	val |= IMX2_WDT_WCR_WDZST;
	/* Strip the old watchdog Time-Out value */
	val &= ~IMX2_WDT_WCR_WT;
	/* Generate reset if WDOG times out */
	val &= ~IMX2_WDT_WCR_WRE;
	/* Keep Watchdog Disabled */
	val &= ~IMX2_WDT_WCR_WDE;
	/* Set the watchdog's Time-Out value */
	val |= WDOG_SEC_TO_COUNT(wdog->timeout);

	regmap_write(wdev->regmap, IMX2_WDT_WCR, val);

	/* enable the watchdog */
	val |= IMX2_WDT_WCR_WDE;
	regmap_write(wdev->regmap, IMX2_WDT_WCR, val);
}

static inline bool imx2_wdt_is_running(struct imx2_wdt_device *wdev)
{
	u32 val;

	regmap_read(wdev->regmap, IMX2_WDT_WCR, &val);

	return val & IMX2_WDT_WCR_WDE;
}

static int imx2_wdt_ping(struct watchdog_device *wdog)
{
	struct imx2_wdt_device *wdev = watchdog_get_drvdata(wdog);

	regmap_write(wdev->regmap, IMX2_WDT_WSR, IMX2_WDT_SEQ1);
	regmap_write(wdev->regmap, IMX2_WDT_WSR, IMX2_WDT_SEQ2);
	return 0;
}

static void imx2_wdt_timer_ping(unsigned long arg)
{
	struct watchdog_device *wdog = (struct watchdog_device *)arg;
	struct imx2_wdt_device *wdev = watchdog_get_drvdata(wdog);

	/* ping it every wdog->timeout / 2 seconds to prevent reboot */
	imx2_wdt_ping(wdog);
	mod_timer(&wdev->timer, jiffies + wdog->timeout * HZ / 2);
}

static int imx2_wdt_set_timeout(struct watchdog_device *wdog,
				unsigned int new_timeout)
{
	struct imx2_wdt_device *wdev = watchdog_get_drvdata(wdog);

	regmap_update_bits(wdev->regmap, IMX2_WDT_WCR, IMX2_WDT_WCR_WT,
			   WDOG_SEC_TO_COUNT(new_timeout));
	return 0;
}

static int imx2_wdt_start(struct watchdog_device *wdog)
{
	struct imx2_wdt_device *wdev = watchdog_get_drvdata(wdog);

	if (imx2_wdt_is_running(wdev)) {
		/* delete the timer that pings the watchdog after close */
		del_timer_sync(&wdev->timer);
		imx2_wdt_set_timeout(wdog, wdog->timeout);
	} else
		imx2_wdt_setup(wdog);

	return imx2_wdt_ping(wdog);
}

static int imx2_wdt_stop(struct watchdog_device *wdog)
{
	/*
	 * We don't need a clk_disable, it cannot be disabled once started.
	 * We use a timer to ping the watchdog while /dev/watchdog is closed
	 */
	imx2_wdt_timer_ping((unsigned long)wdog);
	return 0;
}

static inline void imx2_wdt_ping_if_active(struct watchdog_device *wdog)
{
	struct imx2_wdt_device *wdev = watchdog_get_drvdata(wdog);

	if (imx2_wdt_is_running(wdev)) {
		imx2_wdt_set_timeout(wdog, wdog->timeout);
		imx2_wdt_timer_ping((unsigned long)wdog);
	}
}

static const struct watchdog_ops imx2_wdt_ops = {
	.owner = THIS_MODULE,
	.start = imx2_wdt_start,
	.stop = imx2_wdt_stop,
	.ping = imx2_wdt_ping,
	.set_timeout = imx2_wdt_set_timeout,
};

static const struct regmap_config imx2_wdt_regmap_config = {
	.reg_bits = 16,
	.reg_stride = 2,
	.val_bits = 16,
	.max_register = 0x8,
};

static int __init imx2_wdt_probe(struct platform_device *pdev)
{
	struct imx2_wdt_device *wdev;
	struct watchdog_device *wdog;
	struct resource *res;
	void __iomem *base;
	int ret;
	u32 val;

	wdev = devm_kzalloc(&pdev->dev, sizeof(*wdev), GFP_KERNEL);
	if (!wdev)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(base))
		return PTR_ERR(base);

	wdev->regmap = devm_regmap_init_mmio_clk(&pdev->dev, NULL, base,
						 &imx2_wdt_regmap_config);
	if (IS_ERR(wdev->regmap)) {
		dev_err(&pdev->dev, "regmap init failed\n");
		return PTR_ERR(wdev->regmap);
	}

	wdev->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(wdev->clk)) {
		dev_err(&pdev->dev, "can't get Watchdog clock\n");
		return PTR_ERR(wdev->clk);
	}

	wdog			= &wdev->wdog;
	wdog->info		= &imx2_wdt_info;
	wdog->ops		= &imx2_wdt_ops;
	wdog->min_timeout	= 1;
	wdog->max_timeout	= IMX2_WDT_MAX_TIME;

	clk_prepare_enable(wdev->clk);

	regmap_read(wdev->regmap, IMX2_WDT_WRSR, &val);
	wdog->bootstatus = val & IMX2_WDT_WRSR_TOUT ? WDIOF_CARDRESET : 0;

	wdog->timeout = clamp_t(unsigned, timeout, 1, IMX2_WDT_MAX_TIME);
	if (wdog->timeout != timeout)
		dev_warn(&pdev->dev, "Initial timeout out of range! Clamped from %u to %u\n",
			 timeout, wdog->timeout);

	platform_set_drvdata(pdev, wdog);
	watchdog_set_drvdata(wdog, wdev);
	watchdog_set_nowayout(wdog, nowayout);
	watchdog_init_timeout(wdog, timeout, &pdev->dev);

	setup_timer(&wdev->timer, imx2_wdt_timer_ping, (unsigned long)wdog);

	imx2_wdt_ping_if_active(wdog);

	/*
	 * Disable the watchdog power down counter at boot. Otherwise the power
	 * down counter will pull down the #WDOG interrupt line for one clock
	 * cycle.
	 */
	regmap_write(wdev->regmap, IMX2_WDT_WMCR, 0);

	ret = watchdog_register_device(wdog);
	if (ret) {
		dev_err(&pdev->dev, "cannot register watchdog device\n");
		return ret;
	}

	wdev->restart_handler.notifier_call = imx2_restart_handler;
	wdev->restart_handler.priority = 128;
	ret = register_restart_handler(&wdev->restart_handler);
	if (ret)
		dev_err(&pdev->dev, "cannot register restart handler\n");

	dev_info(&pdev->dev, "timeout %d sec (nowayout=%d)\n",
		 wdog->timeout, nowayout);

	return 0;
}

static int __exit imx2_wdt_remove(struct platform_device *pdev)
{
	struct watchdog_device *wdog = platform_get_drvdata(pdev);
	struct imx2_wdt_device *wdev = watchdog_get_drvdata(wdog);

	unregister_restart_handler(&wdev->restart_handler);

	watchdog_unregister_device(wdog);

	if (imx2_wdt_is_running(wdev)) {
		del_timer_sync(&wdev->timer);
		imx2_wdt_ping(wdog);
		dev_crit(&pdev->dev, "Device removed: Expect reboot!\n");
	}
	return 0;
}

static void imx2_wdt_shutdown(struct platform_device *pdev)
{
	struct watchdog_device *wdog = platform_get_drvdata(pdev);
	struct imx2_wdt_device *wdev = watchdog_get_drvdata(wdog);

	if (imx2_wdt_is_running(wdev)) {
		/*
		 * We are running, we need to delete the timer but will
		 * give max timeout before reboot will take place
		 */
		del_timer_sync(&wdev->timer);
		imx2_wdt_set_timeout(wdog, IMX2_WDT_MAX_TIME);
		imx2_wdt_ping(wdog);
		dev_crit(&pdev->dev, "Device shutdown: Expect reboot!\n");
	}
}

#ifdef CONFIG_PM_SLEEP
/* Disable watchdog if it is active or non-active but still running */
static int imx2_wdt_suspend(struct device *dev)
{
	struct watchdog_device *wdog = dev_get_drvdata(dev);
	struct imx2_wdt_device *wdev = watchdog_get_drvdata(wdog);

	/* The watchdog IP block is running */
	if (imx2_wdt_is_running(wdev)) {
		imx2_wdt_set_timeout(wdog, IMX2_WDT_MAX_TIME);
		imx2_wdt_ping(wdog);

		/* The watchdog is not active */
		if (!watchdog_active(wdog))
			del_timer_sync(&wdev->timer);
	}

	clk_disable_unprepare(wdev->clk);

	return 0;
}

/* Enable watchdog and configure it if necessary */
static int imx2_wdt_resume(struct device *dev)
{
	struct watchdog_device *wdog = dev_get_drvdata(dev);
	struct imx2_wdt_device *wdev = watchdog_get_drvdata(wdog);

	clk_prepare_enable(wdev->clk);

	if (watchdog_active(wdog) && !imx2_wdt_is_running(wdev)) {
		/*
		 * If the watchdog is still active and resumes
		 * from deep sleep state, need to restart the
		 * watchdog again.
		 */
		imx2_wdt_setup(wdog);
		imx2_wdt_set_timeout(wdog, wdog->timeout);
		imx2_wdt_ping(wdog);
	} else if (imx2_wdt_is_running(wdev)) {
		/* Resuming from non-deep sleep state. */
		imx2_wdt_set_timeout(wdog, wdog->timeout);
		imx2_wdt_ping(wdog);
		/*
		 * But the watchdog is not active, then start
		 * the timer again.
		 */
		if (!watchdog_active(wdog))
			mod_timer(&wdev->timer,
				  jiffies + wdog->timeout * HZ / 2);
	}

	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(imx2_wdt_pm_ops, imx2_wdt_suspend,
			 imx2_wdt_resume);

static const struct of_device_id imx2_wdt_dt_ids[] = {
	{ .compatible = "fsl,imx21-wdt", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, imx2_wdt_dt_ids);

static struct platform_driver imx2_wdt_driver = {
	.remove		= __exit_p(imx2_wdt_remove),
	.shutdown	= imx2_wdt_shutdown,
	.driver		= {
		.name	= DRIVER_NAME,
		.pm     = &imx2_wdt_pm_ops,
		.of_match_table = imx2_wdt_dt_ids,
	},
};

module_platform_driver_probe(imx2_wdt_driver, imx2_wdt_probe);

MODULE_AUTHOR("Wolfram Sang");
MODULE_DESCRIPTION("Watchdog driver for IMX2 and later");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:" DRIVER_NAME);

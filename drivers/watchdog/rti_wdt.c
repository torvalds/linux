// SPDX-License-Identifier: GPL-2.0
/*
 * Watchdog driver for the K3 RTI module
 *
 * (c) Copyright 2019-2020 Texas Instruments Inc.
 * All rights reserved.
 */

#include <linux/clk.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/types.h>
#include <linux/watchdog.h>

#define DEFAULT_HEARTBEAT 60

/* Max heartbeat is calculated at 32kHz source clock */
#define MAX_HEARTBEAT	1000

/* Timer register set definition */
#define RTIDWDCTRL	0x90
#define RTIDWDPRLD	0x94
#define RTIWDSTATUS	0x98
#define RTIWDKEY	0x9c
#define RTIDWDCNTR	0xa0
#define RTIWWDRXCTRL	0xa4
#define RTIWWDSIZECTRL	0xa8

#define RTIWWDRX_NMI	0xa

#define RTIWWDSIZE_50P		0x50
#define RTIWWDSIZE_25P		0x500
#define RTIWWDSIZE_12P5		0x5000
#define RTIWWDSIZE_6P25		0x50000
#define RTIWWDSIZE_3P125	0x500000

#define WDENABLE_KEY	0xa98559da

#define WDKEY_SEQ0		0xe51a
#define WDKEY_SEQ1		0xa35c

#define WDT_PRELOAD_SHIFT	13

#define WDT_PRELOAD_MAX		0xfff

#define DWDST			BIT(1)

static int heartbeat = DEFAULT_HEARTBEAT;

/*
 * struct to hold data for each WDT device
 * @base - base io address of WD device
 * @freq - source clock frequency of WDT
 * @wdd  - hold watchdog device as is in WDT core
 */
struct rti_wdt_device {
	void __iomem		*base;
	unsigned long		freq;
	struct watchdog_device	wdd;
};

static int rti_wdt_start(struct watchdog_device *wdd)
{
	u32 timer_margin;
	struct rti_wdt_device *wdt = watchdog_get_drvdata(wdd);

	/* set timeout period */
	timer_margin = (u64)wdd->timeout * wdt->freq;
	timer_margin >>= WDT_PRELOAD_SHIFT;
	if (timer_margin > WDT_PRELOAD_MAX)
		timer_margin = WDT_PRELOAD_MAX;
	writel_relaxed(timer_margin, wdt->base + RTIDWDPRLD);

	/*
	 * RTI only supports a windowed mode, where the watchdog can only
	 * be petted during the open window; not too early or not too late.
	 * The HW configuration options only allow for the open window size
	 * to be 50% or less than that; we obviouly want to configure the open
	 * window as large as possible so we select the 50% option.
	 */
	wdd->min_hw_heartbeat_ms = 500 * wdd->timeout;

	/* Generate NMI when wdt expires */
	writel_relaxed(RTIWWDRX_NMI, wdt->base + RTIWWDRXCTRL);

	/* Open window size 50%; this is the largest window size available */
	writel_relaxed(RTIWWDSIZE_50P, wdt->base + RTIWWDSIZECTRL);

	readl_relaxed(wdt->base + RTIWWDSIZECTRL);

	/* enable watchdog */
	writel_relaxed(WDENABLE_KEY, wdt->base + RTIDWDCTRL);
	return 0;
}

static int rti_wdt_ping(struct watchdog_device *wdd)
{
	struct rti_wdt_device *wdt = watchdog_get_drvdata(wdd);

	/* put watchdog in service state */
	writel_relaxed(WDKEY_SEQ0, wdt->base + RTIWDKEY);
	/* put watchdog in active state */
	writel_relaxed(WDKEY_SEQ1, wdt->base + RTIWDKEY);

	return 0;
}

static int rti_wdt_setup_hw_hb(struct watchdog_device *wdd, u32 wsize)
{
	/*
	 * RTI only supports a windowed mode, where the watchdog can only
	 * be petted during the open window; not too early or not too late.
	 * The HW configuration options only allow for the open window size
	 * to be 50% or less than that.
	 */
	switch (wsize) {
	case RTIWWDSIZE_50P:
		/* 50% open window => 50% min heartbeat */
		wdd->min_hw_heartbeat_ms = 500 * heartbeat;
		break;

	case RTIWWDSIZE_25P:
		/* 25% open window => 75% min heartbeat */
		wdd->min_hw_heartbeat_ms = 750 * heartbeat;
		break;

	case RTIWWDSIZE_12P5:
		/* 12.5% open window => 87.5% min heartbeat */
		wdd->min_hw_heartbeat_ms = 875 * heartbeat;
		break;

	case RTIWWDSIZE_6P25:
		/* 6.5% open window => 93.5% min heartbeat */
		wdd->min_hw_heartbeat_ms = 935 * heartbeat;
		break;

	case RTIWWDSIZE_3P125:
		/* 3.125% open window => 96.9% min heartbeat */
		wdd->min_hw_heartbeat_ms = 969 * heartbeat;
		break;

	default:
		return -EINVAL;
	}

	return 0;
}

static unsigned int rti_wdt_get_timeleft_ms(struct watchdog_device *wdd)
{
	u64 timer_counter;
	u32 val;
	struct rti_wdt_device *wdt = watchdog_get_drvdata(wdd);

	/* if timeout has occurred then return 0 */
	val = readl_relaxed(wdt->base + RTIWDSTATUS);
	if (val & DWDST)
		return 0;

	timer_counter = readl_relaxed(wdt->base + RTIDWDCNTR);

	timer_counter *= 1000;

	do_div(timer_counter, wdt->freq);

	return timer_counter;
}

static unsigned int rti_wdt_get_timeleft(struct watchdog_device *wdd)
{
	return rti_wdt_get_timeleft_ms(wdd) / 1000;
}

static const struct watchdog_info rti_wdt_info = {
	.options = WDIOF_KEEPALIVEPING,
	.identity = "K3 RTI Watchdog",
};

static const struct watchdog_ops rti_wdt_ops = {
	.owner		= THIS_MODULE,
	.start		= rti_wdt_start,
	.ping		= rti_wdt_ping,
	.get_timeleft	= rti_wdt_get_timeleft,
};

static int rti_wdt_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct device *dev = &pdev->dev;
	struct watchdog_device *wdd;
	struct rti_wdt_device *wdt;
	struct clk *clk;
	u32 last_ping = 0;

	wdt = devm_kzalloc(dev, sizeof(*wdt), GFP_KERNEL);
	if (!wdt)
		return -ENOMEM;

	clk = clk_get(dev, NULL);
	if (IS_ERR(clk))
		return dev_err_probe(dev, PTR_ERR(clk), "failed to get clock\n");

	wdt->freq = clk_get_rate(clk);

	clk_put(clk);

	if (!wdt->freq) {
		dev_err(dev, "Failed to get fck rate.\n");
		return -EINVAL;
	}

	/*
	 * If watchdog is running at 32k clock, it is not accurate.
	 * Adjust frequency down in this case so that we don't pet
	 * the watchdog too often.
	 */
	if (wdt->freq < 32768)
		wdt->freq = wdt->freq * 9 / 10;

	pm_runtime_enable(dev);
	ret = pm_runtime_resume_and_get(dev);
	if (ret < 0) {
		pm_runtime_disable(&pdev->dev);
		return dev_err_probe(dev, ret, "runtime pm failed\n");
	}

	platform_set_drvdata(pdev, wdt);

	wdd = &wdt->wdd;
	wdd->info = &rti_wdt_info;
	wdd->ops = &rti_wdt_ops;
	wdd->min_timeout = 1;
	wdd->max_hw_heartbeat_ms = (WDT_PRELOAD_MAX << WDT_PRELOAD_SHIFT) /
		wdt->freq * 1000;
	wdd->parent = dev;

	watchdog_set_drvdata(wdd, wdt);
	watchdog_set_nowayout(wdd, 1);
	watchdog_set_restart_priority(wdd, 128);

	wdt->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(wdt->base)) {
		ret = PTR_ERR(wdt->base);
		goto err_iomap;
	}

	if (readl(wdt->base + RTIDWDCTRL) == WDENABLE_KEY) {
		int preset_heartbeat;
		u32 time_left_ms;
		u64 heartbeat_ms;
		u32 wsize;

		set_bit(WDOG_HW_RUNNING, &wdd->status);
		time_left_ms = rti_wdt_get_timeleft_ms(wdd);
		heartbeat_ms = readl(wdt->base + RTIDWDPRLD);
		heartbeat_ms <<= WDT_PRELOAD_SHIFT;
		heartbeat_ms *= 1000;
		do_div(heartbeat_ms, wdt->freq);
		preset_heartbeat = heartbeat_ms + 500;
		preset_heartbeat /= 1000;
		if (preset_heartbeat != heartbeat)
			dev_warn(dev, "watchdog already running, ignoring heartbeat config!\n");

		heartbeat = preset_heartbeat;

		wsize = readl(wdt->base + RTIWWDSIZECTRL);
		ret = rti_wdt_setup_hw_hb(wdd, wsize);
		if (ret) {
			dev_err(dev, "bad window size.\n");
			goto err_iomap;
		}

		last_ping = heartbeat_ms - time_left_ms;
		if (time_left_ms > heartbeat_ms) {
			dev_warn(dev, "time_left > heartbeat? Assuming last ping just before now.\n");
			last_ping = 0;
		}
	}

	watchdog_init_timeout(wdd, heartbeat, dev);

	ret = watchdog_register_device(wdd);
	if (ret) {
		dev_err(dev, "cannot register watchdog device\n");
		goto err_iomap;
	}

	if (last_ping)
		watchdog_set_last_hw_keepalive(wdd, last_ping);

	return 0;

err_iomap:
	pm_runtime_put_sync(&pdev->dev);
	pm_runtime_disable(&pdev->dev);

	return ret;
}

static void rti_wdt_remove(struct platform_device *pdev)
{
	struct rti_wdt_device *wdt = platform_get_drvdata(pdev);

	watchdog_unregister_device(&wdt->wdd);
	pm_runtime_put(&pdev->dev);
	pm_runtime_disable(&pdev->dev);
}

static const struct of_device_id rti_wdt_of_match[] = {
	{ .compatible = "ti,j7-rti-wdt", },
	{},
};
MODULE_DEVICE_TABLE(of, rti_wdt_of_match);

static struct platform_driver rti_wdt_driver = {
	.driver = {
		.name = "rti-wdt",
		.of_match_table = rti_wdt_of_match,
	},
	.probe = rti_wdt_probe,
	.remove_new = rti_wdt_remove,
};

module_platform_driver(rti_wdt_driver);

MODULE_AUTHOR("Tero Kristo <t-kristo@ti.com>");
MODULE_DESCRIPTION("K3 RTI Watchdog Driver");

module_param(heartbeat, int, 0);
MODULE_PARM_DESC(heartbeat,
		 "Watchdog heartbeat period in seconds from 1 to "
		 __MODULE_STRING(MAX_HEARTBEAT) ", default "
		 __MODULE_STRING(DEFAULT_HEARTBEAT));

MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:rti-wdt");

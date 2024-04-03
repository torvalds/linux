// SPDX-License-Identifier: GPL-2.0-only
/*
 * sunplus Watchdog Driver
 *
 * Copyright (C) 2021 Sunplus Technology Co., Ltd.
 *
 */

#include <linux/clk.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/platform_device.h>
#include <linux/reset.h>
#include <linux/watchdog.h>

#define WDT_CTRL		0x00
#define WDT_CNT			0x04

#define WDT_STOP		0x3877
#define WDT_RESUME		0x4A4B
#define WDT_CLRIRQ		0x7482
#define WDT_UNLOCK		0xAB00
#define WDT_LOCK		0xAB01
#define WDT_CONMAX		0xDEAF

/* TIMEOUT_MAX = ffff0/90kHz =11.65, so longer than 11 seconds will time out. */
#define SP_WDT_MAX_TIMEOUT	11U
#define SP_WDT_DEFAULT_TIMEOUT	10

#define STC_CLK			90000

#define DEVICE_NAME		"sunplus-wdt"

static unsigned int timeout;
module_param(timeout, int, 0);
MODULE_PARM_DESC(timeout, "Watchdog timeout in seconds");

static bool nowayout = WATCHDOG_NOWAYOUT;
module_param(nowayout, bool, 0);
MODULE_PARM_DESC(nowayout, "Watchdog cannot be stopped once started (default="
			__MODULE_STRING(WATCHDOG_NOWAYOUT) ")");

struct sp_wdt_priv {
	struct watchdog_device wdev;
	void __iomem *base;
	struct clk *clk;
	struct reset_control *rstc;
};

static int sp_wdt_restart(struct watchdog_device *wdev,
			  unsigned long action, void *data)
{
	struct sp_wdt_priv *priv = watchdog_get_drvdata(wdev);
	void __iomem *base = priv->base;

	writel(WDT_STOP, base + WDT_CTRL);
	writel(WDT_UNLOCK, base + WDT_CTRL);
	writel(0x0001, base + WDT_CNT);
	writel(WDT_LOCK, base + WDT_CTRL);
	writel(WDT_RESUME, base + WDT_CTRL);

	return 0;
}

static int sp_wdt_ping(struct watchdog_device *wdev)
{
	struct sp_wdt_priv *priv = watchdog_get_drvdata(wdev);
	void __iomem *base = priv->base;
	u32 count;

	if (wdev->timeout > SP_WDT_MAX_TIMEOUT) {
		/* WDT_CONMAX sets the count to the maximum (down-counting). */
		writel(WDT_CONMAX, base + WDT_CTRL);
	} else {
		writel(WDT_UNLOCK, base + WDT_CTRL);
		/*
		 * Watchdog timer is a 20-bit down-counting based on STC_CLK.
		 * This register bits[16:0] is from bit[19:4] of the watchdog
		 * timer counter.
		 */
		count = (wdev->timeout * STC_CLK) >> 4;
		writel(count, base + WDT_CNT);
		writel(WDT_LOCK, base + WDT_CTRL);
	}

	return 0;
}

static int sp_wdt_stop(struct watchdog_device *wdev)
{
	struct sp_wdt_priv *priv = watchdog_get_drvdata(wdev);
	void __iomem *base = priv->base;

	writel(WDT_STOP, base + WDT_CTRL);

	return 0;
}

static int sp_wdt_start(struct watchdog_device *wdev)
{
	struct sp_wdt_priv *priv = watchdog_get_drvdata(wdev);
	void __iomem *base = priv->base;

	writel(WDT_RESUME, base + WDT_CTRL);

	return 0;
}

static unsigned int sp_wdt_get_timeleft(struct watchdog_device *wdev)
{
	struct sp_wdt_priv *priv = watchdog_get_drvdata(wdev);
	void __iomem *base = priv->base;
	u32 val;

	val = readl(base + WDT_CNT);
	val &= 0xffff;
	val = val << 4;

	return val;
}

static const struct watchdog_info sp_wdt_info = {
	.identity	= DEVICE_NAME,
	.options	= WDIOF_SETTIMEOUT |
			  WDIOF_MAGICCLOSE |
			  WDIOF_KEEPALIVEPING,
};

static const struct watchdog_ops sp_wdt_ops = {
	.owner		= THIS_MODULE,
	.start		= sp_wdt_start,
	.stop		= sp_wdt_stop,
	.ping		= sp_wdt_ping,
	.get_timeleft	= sp_wdt_get_timeleft,
	.restart	= sp_wdt_restart,
};

static void sp_reset_control_assert(void *data)
{
	reset_control_assert(data);
}

static int sp_wdt_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct sp_wdt_priv *priv;
	int ret;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->clk = devm_clk_get_enabled(dev, NULL);
	if (IS_ERR(priv->clk))
		return dev_err_probe(dev, PTR_ERR(priv->clk), "Failed to enable clock\n");

	/* The timer and watchdog shared the STC reset */
	priv->rstc = devm_reset_control_get_shared(dev, NULL);
	if (IS_ERR(priv->rstc))
		return dev_err_probe(dev, PTR_ERR(priv->rstc), "Failed to get reset\n");

	reset_control_deassert(priv->rstc);

	ret = devm_add_action_or_reset(dev, sp_reset_control_assert, priv->rstc);
	if (ret)
		return ret;

	priv->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(priv->base))
		return PTR_ERR(priv->base);

	priv->wdev.info = &sp_wdt_info;
	priv->wdev.ops = &sp_wdt_ops;
	priv->wdev.timeout = SP_WDT_DEFAULT_TIMEOUT;
	priv->wdev.max_hw_heartbeat_ms = SP_WDT_MAX_TIMEOUT * 1000;
	priv->wdev.min_timeout = 1;
	priv->wdev.parent = dev;

	watchdog_set_drvdata(&priv->wdev, priv);
	watchdog_init_timeout(&priv->wdev, timeout, dev);
	watchdog_set_nowayout(&priv->wdev, nowayout);
	watchdog_stop_on_reboot(&priv->wdev);
	watchdog_set_restart_priority(&priv->wdev, 128);

	return devm_watchdog_register_device(dev, &priv->wdev);
}

static const struct of_device_id sp_wdt_of_match[] = {
	{.compatible = "sunplus,sp7021-wdt", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, sp_wdt_of_match);

static struct platform_driver sp_wdt_driver = {
	.probe = sp_wdt_probe,
	.driver = {
		   .name = DEVICE_NAME,
		   .of_match_table = sp_wdt_of_match,
	},
};

module_platform_driver(sp_wdt_driver);

MODULE_AUTHOR("Xiantao Hu <xt.hu@cqplus1.com>");
MODULE_DESCRIPTION("Sunplus Watchdog Timer Driver");
MODULE_LICENSE("GPL");

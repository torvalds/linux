// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2020 TOSHIBA CORPORATION
 * Copyright (c) 2020 Toshiba Electronic Devices & Storage Corporation
 * Copyright (c) 2020 Nobuhiro Iwamatsu <nobuhiro1.iwamatsu@toshiba.co.jp>
 */

#include <linux/clk.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/watchdog.h>

#define WDT_CNT			0x00
#define WDT_MIN			0x04
#define WDT_MAX			0x08
#define WDT_CTL			0x0c
#define WDT_CMD			0x10
#define WDT_CMD_CLEAR		0x4352
#define WDT_CMD_START_STOP	0x5354
#define WDT_DIV			0x30

#define VISCONTI_WDT_FREQ	2000000 /* 2MHz */
#define WDT_DEFAULT_TIMEOUT	10U /* in seconds */

static bool nowayout = WATCHDOG_NOWAYOUT;
module_param(nowayout, bool, 0);
MODULE_PARM_DESC(
	nowayout,
	"Watchdog cannot be stopped once started (default=" __MODULE_STRING(WATCHDOG_NOWAYOUT)")");

struct visconti_wdt_priv {
	struct watchdog_device wdev;
	void __iomem *base;
	u32 div;
};

static int visconti_wdt_start(struct watchdog_device *wdev)
{
	struct visconti_wdt_priv *priv = watchdog_get_drvdata(wdev);
	u32 timeout = wdev->timeout * VISCONTI_WDT_FREQ;

	writel(priv->div, priv->base + WDT_DIV);
	writel(0, priv->base + WDT_MIN);
	writel(timeout, priv->base + WDT_MAX);
	writel(0, priv->base + WDT_CTL);
	writel(WDT_CMD_START_STOP, priv->base + WDT_CMD);

	return 0;
}

static int visconti_wdt_stop(struct watchdog_device *wdev)
{
	struct visconti_wdt_priv *priv = watchdog_get_drvdata(wdev);

	writel(1, priv->base + WDT_CTL);
	writel(WDT_CMD_START_STOP, priv->base + WDT_CMD);

	return 0;
}

static int visconti_wdt_ping(struct watchdog_device *wdd)
{
	struct visconti_wdt_priv *priv = watchdog_get_drvdata(wdd);

	writel(WDT_CMD_CLEAR, priv->base + WDT_CMD);

	return 0;
}

static unsigned int visconti_wdt_get_timeleft(struct watchdog_device *wdev)
{
	struct visconti_wdt_priv *priv = watchdog_get_drvdata(wdev);
	u32 timeout = wdev->timeout * VISCONTI_WDT_FREQ;
	u32 cnt = readl(priv->base + WDT_CNT);

	if (timeout <= cnt)
		return 0;
	timeout -= cnt;

	return timeout / VISCONTI_WDT_FREQ;
}

static int visconti_wdt_set_timeout(struct watchdog_device *wdev, unsigned int timeout)
{
	u32 val;
	struct visconti_wdt_priv *priv = watchdog_get_drvdata(wdev);

	wdev->timeout = timeout;
	val = wdev->timeout * VISCONTI_WDT_FREQ;

	/* Clear counter before setting timeout because WDT expires */
	writel(WDT_CMD_CLEAR, priv->base + WDT_CMD);
	writel(val, priv->base + WDT_MAX);

	return 0;
}

static const struct watchdog_info visconti_wdt_info = {
	.options = WDIOF_SETTIMEOUT | WDIOF_MAGICCLOSE | WDIOF_KEEPALIVEPING,
	.identity = "Visconti Watchdog",
};

static const struct watchdog_ops visconti_wdt_ops = {
	.owner		= THIS_MODULE,
	.start		= visconti_wdt_start,
	.stop		= visconti_wdt_stop,
	.ping		= visconti_wdt_ping,
	.get_timeleft	= visconti_wdt_get_timeleft,
	.set_timeout	= visconti_wdt_set_timeout,
};

static int visconti_wdt_probe(struct platform_device *pdev)
{
	struct watchdog_device *wdev;
	struct visconti_wdt_priv *priv;
	struct device *dev = &pdev->dev;
	struct clk *clk;
	int ret;
	unsigned long clk_freq;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(priv->base))
		return PTR_ERR(priv->base);

	clk = devm_clk_get_enabled(dev, NULL);
	if (IS_ERR(clk))
		return dev_err_probe(dev, PTR_ERR(clk), "Could not get clock\n");

	clk_freq = clk_get_rate(clk);
	if (!clk_freq)
		return -EINVAL;

	priv->div = clk_freq / VISCONTI_WDT_FREQ;

	/* Initialize struct watchdog_device. */
	wdev = &priv->wdev;
	wdev->info = &visconti_wdt_info;
	wdev->ops = &visconti_wdt_ops;
	wdev->parent = dev;
	wdev->min_timeout = 1;
	wdev->max_timeout = 0xffffffff / VISCONTI_WDT_FREQ;
	wdev->timeout = min(wdev->max_timeout, WDT_DEFAULT_TIMEOUT);

	watchdog_set_drvdata(wdev, priv);
	watchdog_set_nowayout(wdev, nowayout);
	watchdog_stop_on_unregister(wdev);

	/* This overrides the default timeout only if DT configuration was found */
	ret = watchdog_init_timeout(wdev, 0, dev);
	if (ret)
		dev_warn(dev, "Specified timeout value invalid, using default\n");

	return devm_watchdog_register_device(dev, wdev);
}

static const struct of_device_id visconti_wdt_of_match[] = {
	{ .compatible = "toshiba,visconti-wdt", },
	{}
};
MODULE_DEVICE_TABLE(of, visconti_wdt_of_match);

static struct platform_driver visconti_wdt_driver = {
	.driver = {
			.name = "visconti_wdt",
			.of_match_table = visconti_wdt_of_match,
		},
	.probe = visconti_wdt_probe,
};
module_platform_driver(visconti_wdt_driver);

MODULE_DESCRIPTION("TOSHIBA Visconti Watchdog Driver");
MODULE_AUTHOR("Nobuhiro Iwamatsu <nobuhiro1.iwamatsu@toshiba.co.jp");
MODULE_LICENSE("GPL v2");

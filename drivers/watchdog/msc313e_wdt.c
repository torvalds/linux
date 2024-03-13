// SPDX-License-Identifier: GPL-2.0
/*
 * MStar WDT driver
 *
 * Copyright (C) 2019 - 2021 Daniel Palmer
 * Copyright (C) 2021 Romain Perier
 *
 */

#include <linux/clk.h>
#include <linux/io.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/watchdog.h>

#define REG_WDT_CLR			0x0
#define REG_WDT_MAX_PRD_L		0x10
#define REG_WDT_MAX_PRD_H		0x14

#define MSC313E_WDT_MIN_TIMEOUT		1
#define MSC313E_WDT_DEFAULT_TIMEOUT	30

static unsigned int timeout;

module_param(timeout, int, 0);
MODULE_PARM_DESC(timeout, "Watchdog timeout in seconds");

struct msc313e_wdt_priv {
	void __iomem *base;
	struct watchdog_device wdev;
	struct clk *clk;
};

static int msc313e_wdt_start(struct watchdog_device *wdev)
{
	struct msc313e_wdt_priv *priv = watchdog_get_drvdata(wdev);
	u32 timeout;
	int err;

	err = clk_prepare_enable(priv->clk);
	if (err)
		return err;

	timeout = wdev->timeout * clk_get_rate(priv->clk);
	writew(timeout & 0xffff, priv->base + REG_WDT_MAX_PRD_L);
	writew((timeout >> 16) & 0xffff, priv->base + REG_WDT_MAX_PRD_H);
	writew(1, priv->base + REG_WDT_CLR);
	return 0;
}

static int msc313e_wdt_ping(struct watchdog_device *wdev)
{
	struct msc313e_wdt_priv *priv = watchdog_get_drvdata(wdev);

	writew(1, priv->base + REG_WDT_CLR);
	return 0;
}

static int msc313e_wdt_stop(struct watchdog_device *wdev)
{
	struct msc313e_wdt_priv *priv = watchdog_get_drvdata(wdev);

	writew(0, priv->base + REG_WDT_MAX_PRD_L);
	writew(0, priv->base + REG_WDT_MAX_PRD_H);
	writew(0, priv->base + REG_WDT_CLR);
	clk_disable_unprepare(priv->clk);
	return 0;
}

static int msc313e_wdt_settimeout(struct watchdog_device *wdev, unsigned int new_time)
{
	wdev->timeout = new_time;

	return msc313e_wdt_start(wdev);
}

static const struct watchdog_info msc313e_wdt_ident = {
	.identity = "MSC313e watchdog",
	.options = WDIOF_MAGICCLOSE | WDIOF_KEEPALIVEPING | WDIOF_SETTIMEOUT,
};

static const struct watchdog_ops msc313e_wdt_ops = {
	.owner = THIS_MODULE,
	.start = msc313e_wdt_start,
	.stop = msc313e_wdt_stop,
	.ping = msc313e_wdt_ping,
	.set_timeout = msc313e_wdt_settimeout,
};

static const struct of_device_id msc313e_wdt_of_match[] = {
	{ .compatible = "mstar,msc313e-wdt", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, msc313e_wdt_of_match);

static int msc313e_wdt_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct msc313e_wdt_priv *priv;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(priv->base))
		return PTR_ERR(priv->base);

	priv->clk = devm_clk_get(dev, NULL);
	if (IS_ERR(priv->clk)) {
		dev_err(dev, "No input clock\n");
		return PTR_ERR(priv->clk);
	}

	priv->wdev.info = &msc313e_wdt_ident,
	priv->wdev.ops = &msc313e_wdt_ops,
	priv->wdev.parent = dev;
	priv->wdev.min_timeout = MSC313E_WDT_MIN_TIMEOUT;
	priv->wdev.max_timeout = U32_MAX / clk_get_rate(priv->clk);
	priv->wdev.timeout = MSC313E_WDT_DEFAULT_TIMEOUT;

	/* If the period is non-zero the WDT is running */
	if (readw(priv->base + REG_WDT_MAX_PRD_L) | (readw(priv->base + REG_WDT_MAX_PRD_H) << 16))
		set_bit(WDOG_HW_RUNNING, &priv->wdev.status);

	watchdog_set_drvdata(&priv->wdev, priv);

	watchdog_init_timeout(&priv->wdev, timeout, dev);
	watchdog_stop_on_reboot(&priv->wdev);
	watchdog_stop_on_unregister(&priv->wdev);

	return devm_watchdog_register_device(dev, &priv->wdev);
}

static int __maybe_unused msc313e_wdt_suspend(struct device *dev)
{
	struct msc313e_wdt_priv *priv = dev_get_drvdata(dev);

	if (watchdog_active(&priv->wdev))
		msc313e_wdt_stop(&priv->wdev);

	return 0;
}

static int __maybe_unused msc313e_wdt_resume(struct device *dev)
{
	struct msc313e_wdt_priv *priv = dev_get_drvdata(dev);

	if (watchdog_active(&priv->wdev))
		msc313e_wdt_start(&priv->wdev);

	return 0;
}

static SIMPLE_DEV_PM_OPS(msc313e_wdt_pm_ops, msc313e_wdt_suspend, msc313e_wdt_resume);

static struct platform_driver msc313e_wdt_driver = {
	.driver = {
		.name = "msc313e-wdt",
		.of_match_table = msc313e_wdt_of_match,
		.pm = &msc313e_wdt_pm_ops,
	},
	.probe = msc313e_wdt_probe,
};
module_platform_driver(msc313e_wdt_driver);

MODULE_AUTHOR("Daniel Palmer <daniel@thingy.jp>");
MODULE_DESCRIPTION("Watchdog driver for MStar MSC313e");
MODULE_LICENSE("GPL v2");

/*
 * Watchdog driver for Renesas WDT watchdog
 *
 * Copyright (C) 2015-16 Wolfram Sang, Sang Engineering <wsa@sang-engineering.com>
 * Copyright (C) 2015-16 Renesas Electronics Corporation
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */
#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/watchdog.h>

#define RWTCNT		0
#define RWTCSRA		4
#define RWTCSRA_WOVF	BIT(4)
#define RWTCSRA_WRFLG	BIT(5)
#define RWTCSRA_TME	BIT(7)

#define RWDT_DEFAULT_TIMEOUT 60U

static const unsigned int clk_divs[] = { 1, 4, 16, 32, 64, 128, 1024 };

static bool nowayout = WATCHDOG_NOWAYOUT;
module_param(nowayout, bool, 0);
MODULE_PARM_DESC(nowayout, "Watchdog cannot be stopped once started (default="
				__MODULE_STRING(WATCHDOG_NOWAYOUT) ")");

struct rwdt_priv {
	void __iomem *base;
	struct watchdog_device wdev;
	struct clk *clk;
	unsigned int clks_per_sec;
	u8 cks;
};

static void rwdt_write(struct rwdt_priv *priv, u32 val, unsigned int reg)
{
	if (reg == RWTCNT)
		val |= 0x5a5a0000;
	else
		val |= 0xa5a5a500;

	writel_relaxed(val, priv->base + reg);
}

static int rwdt_init_timeout(struct watchdog_device *wdev)
{
	struct rwdt_priv *priv = watchdog_get_drvdata(wdev);

	rwdt_write(priv, 65536 - wdev->timeout * priv->clks_per_sec, RWTCNT);

	return 0;
}

static int rwdt_start(struct watchdog_device *wdev)
{
	struct rwdt_priv *priv = watchdog_get_drvdata(wdev);

	clk_prepare_enable(priv->clk);

	rwdt_write(priv, priv->cks, RWTCSRA);
	rwdt_init_timeout(wdev);

	while (readb_relaxed(priv->base + RWTCSRA) & RWTCSRA_WRFLG)
		cpu_relax();

	rwdt_write(priv, priv->cks | RWTCSRA_TME, RWTCSRA);

	return 0;
}

static int rwdt_stop(struct watchdog_device *wdev)
{
	struct rwdt_priv *priv = watchdog_get_drvdata(wdev);

	rwdt_write(priv, priv->cks, RWTCSRA);
	clk_disable_unprepare(priv->clk);

	return 0;
}

static unsigned int rwdt_get_timeleft(struct watchdog_device *wdev)
{
	struct rwdt_priv *priv = watchdog_get_drvdata(wdev);
	u16 val = readw_relaxed(priv->base + RWTCNT);

	return DIV_ROUND_CLOSEST(65536 - val, priv->clks_per_sec);
}

static const struct watchdog_info rwdt_ident = {
	.options = WDIOF_MAGICCLOSE | WDIOF_KEEPALIVEPING | WDIOF_SETTIMEOUT,
	.identity = "Renesas WDT Watchdog",
};

static const struct watchdog_ops rwdt_ops = {
	.owner = THIS_MODULE,
	.start = rwdt_start,
	.stop = rwdt_stop,
	.ping = rwdt_init_timeout,
	.get_timeleft = rwdt_get_timeleft,
};

static int rwdt_probe(struct platform_device *pdev)
{
	struct rwdt_priv *priv;
	struct resource *res;
	unsigned long rate;
	unsigned int clks_per_sec;
	int ret, i;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	priv->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(priv->base))
		return PTR_ERR(priv->base);

	priv->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(priv->clk))
		return PTR_ERR(priv->clk);

	rate = clk_get_rate(priv->clk);
	if (!rate)
		return -ENOENT;

	for (i = ARRAY_SIZE(clk_divs) - 1; i >= 0; i--) {
		clks_per_sec = DIV_ROUND_UP(rate, clk_divs[i]);
		if (clks_per_sec) {
			priv->clks_per_sec = clks_per_sec;
			priv->cks = i;
			break;
		}
	}

	if (!clks_per_sec) {
		dev_err(&pdev->dev, "Can't find suitable clock divider\n");
		return -ERANGE;
	}

	pm_runtime_enable(&pdev->dev);
	pm_runtime_get_sync(&pdev->dev);

	priv->wdev.info = &rwdt_ident,
	priv->wdev.ops = &rwdt_ops,
	priv->wdev.parent = &pdev->dev;
	priv->wdev.min_timeout = 1;
	priv->wdev.max_timeout = 65536 / clks_per_sec;
	priv->wdev.timeout = min(priv->wdev.max_timeout, RWDT_DEFAULT_TIMEOUT);

	platform_set_drvdata(pdev, priv);
	watchdog_set_drvdata(&priv->wdev, priv);
	watchdog_set_nowayout(&priv->wdev, nowayout);

	/* This overrides the default timeout only if DT configuration was found */
	ret = watchdog_init_timeout(&priv->wdev, 0, &pdev->dev);
	if (ret)
		dev_warn(&pdev->dev, "Specified timeout value invalid, using default\n");

	ret = watchdog_register_device(&priv->wdev);
	if (ret < 0) {
		pm_runtime_put(&pdev->dev);
		pm_runtime_disable(&pdev->dev);
		return ret;
	}

	return 0;
}

static int rwdt_remove(struct platform_device *pdev)
{
	struct rwdt_priv *priv = platform_get_drvdata(pdev);

	watchdog_unregister_device(&priv->wdev);
	pm_runtime_put(&pdev->dev);
	pm_runtime_disable(&pdev->dev);

	return 0;
}

/*
 * This driver does also fit for R-Car Gen2 (r8a779[0-4]) WDT. However, for SMP
 * to work there, one also needs a RESET (RST) driver which does not exist yet
 * due to HW issues. This needs to be solved before adding compatibles here.
 */
static const struct of_device_id rwdt_ids[] = {
	{ .compatible = "renesas,rcar-gen3-wdt", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, rwdt_ids);

static struct platform_driver rwdt_driver = {
	.driver = {
		.name = "renesas_wdt",
		.of_match_table = rwdt_ids,
	},
	.probe = rwdt_probe,
	.remove = rwdt_remove,
};
module_platform_driver(rwdt_driver);

MODULE_DESCRIPTION("Renesas WDT Watchdog Driver");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Wolfram Sang <wsa@sang-engineering.com>");

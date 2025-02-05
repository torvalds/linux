// SPDX-License-Identifier: GPL-2.0-only
/*
 * Watchdog driver for Technologic Systems TS-72xx based SBCs
 * (TS-7200, TS-7250 and TS-7260). These boards have external
 * glue logic CPLD chip, which includes programmable watchdog
 * timer.
 *
 * Copyright (c) 2009 Mika Westerberg <mika.westerberg@iki.fi>
 *
 * This driver is based on ep93xx_wdt and wm831x_wdt drivers.
 *
 */

#include <linux/platform_device.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/watchdog.h>
#include <linux/io.h>

#define TS72XX_WDT_DEFAULT_TIMEOUT	30

static int timeout;
module_param(timeout, int, 0);
MODULE_PARM_DESC(timeout, "Watchdog timeout in seconds.");

static bool nowayout = WATCHDOG_NOWAYOUT;
module_param(nowayout, bool, 0);
MODULE_PARM_DESC(nowayout, "Disable watchdog shutdown on close");

/* priv->control_reg */
#define TS72XX_WDT_CTRL_DISABLE		0x00
#define TS72XX_WDT_CTRL_250MS		0x01
#define TS72XX_WDT_CTRL_500MS		0x02
#define TS72XX_WDT_CTRL_1SEC		0x03
#define TS72XX_WDT_CTRL_RESERVED	0x04
#define TS72XX_WDT_CTRL_2SEC		0x05
#define TS72XX_WDT_CTRL_4SEC		0x06
#define TS72XX_WDT_CTRL_8SEC		0x07

/* priv->feed_reg */
#define TS72XX_WDT_FEED_VAL		0x05

struct ts72xx_wdt_priv {
	void __iomem	*control_reg;
	void __iomem	*feed_reg;
	struct watchdog_device wdd;
	unsigned char regval;
};

static int ts72xx_wdt_start(struct watchdog_device *wdd)
{
	struct ts72xx_wdt_priv *priv = watchdog_get_drvdata(wdd);

	writeb(TS72XX_WDT_FEED_VAL, priv->feed_reg);
	writeb(priv->regval, priv->control_reg);

	return 0;
}

static int ts72xx_wdt_stop(struct watchdog_device *wdd)
{
	struct ts72xx_wdt_priv *priv = watchdog_get_drvdata(wdd);

	writeb(TS72XX_WDT_FEED_VAL, priv->feed_reg);
	writeb(TS72XX_WDT_CTRL_DISABLE, priv->control_reg);

	return 0;
}

static int ts72xx_wdt_ping(struct watchdog_device *wdd)
{
	struct ts72xx_wdt_priv *priv = watchdog_get_drvdata(wdd);

	writeb(TS72XX_WDT_FEED_VAL, priv->feed_reg);

	return 0;
}

static int ts72xx_wdt_settimeout(struct watchdog_device *wdd, unsigned int to)
{
	struct ts72xx_wdt_priv *priv = watchdog_get_drvdata(wdd);

	if (to == 1) {
		priv->regval = TS72XX_WDT_CTRL_1SEC;
	} else if (to == 2) {
		priv->regval = TS72XX_WDT_CTRL_2SEC;
	} else if (to <= 4) {
		priv->regval = TS72XX_WDT_CTRL_4SEC;
		to = 4;
	} else {
		priv->regval = TS72XX_WDT_CTRL_8SEC;
		if (to <= 8)
			to = 8;
	}

	wdd->timeout = to;

	if (watchdog_active(wdd)) {
		ts72xx_wdt_stop(wdd);
		ts72xx_wdt_start(wdd);
	}

	return 0;
}

static const struct watchdog_info ts72xx_wdt_ident = {
	.options		= WDIOF_KEEPALIVEPING |
				  WDIOF_SETTIMEOUT |
				  WDIOF_MAGICCLOSE,
	.firmware_version	= 1,
	.identity		= "TS-72XX WDT",
};

static const struct watchdog_ops ts72xx_wdt_ops = {
	.owner		= THIS_MODULE,
	.start		= ts72xx_wdt_start,
	.stop		= ts72xx_wdt_stop,
	.ping		= ts72xx_wdt_ping,
	.set_timeout	= ts72xx_wdt_settimeout,
};

static int ts72xx_wdt_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct ts72xx_wdt_priv *priv;
	struct watchdog_device *wdd;
	int ret;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->control_reg = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(priv->control_reg))
		return PTR_ERR(priv->control_reg);

	priv->feed_reg = devm_platform_ioremap_resource(pdev, 1);
	if (IS_ERR(priv->feed_reg))
		return PTR_ERR(priv->feed_reg);

	wdd = &priv->wdd;
	wdd->info = &ts72xx_wdt_ident;
	wdd->ops = &ts72xx_wdt_ops;
	wdd->min_timeout = 1;
	wdd->max_hw_heartbeat_ms = 8000;
	wdd->parent = dev;

	watchdog_set_nowayout(wdd, nowayout);

	wdd->timeout = TS72XX_WDT_DEFAULT_TIMEOUT;
	watchdog_init_timeout(wdd, timeout, dev);

	watchdog_set_drvdata(wdd, priv);

	ret = devm_watchdog_register_device(dev, wdd);
	if (ret)
		return ret;

	dev_info(dev, "TS-72xx Watchdog driver\n");

	return 0;
}

static const struct of_device_id ts72xx_wdt_of_ids[] = {
	{ .compatible = "technologic,ts7200-wdt" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, ts72xx_wdt_of_ids);

static struct platform_driver ts72xx_wdt_driver = {
	.probe		= ts72xx_wdt_probe,
	.driver		= {
		.name	= "ts72xx-wdt",
		.of_match_table = ts72xx_wdt_of_ids,
	},
};

module_platform_driver(ts72xx_wdt_driver);

MODULE_AUTHOR("Mika Westerberg <mika.westerberg@iki.fi>");
MODULE_DESCRIPTION("TS-72xx SBC Watchdog");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:ts72xx-wdt");

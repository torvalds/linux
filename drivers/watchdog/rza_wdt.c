// SPDX-License-Identifier: GPL-2.0
/*
 * Renesas RZ/A Series WDT Driver
 *
 * Copyright (C) 2017 Renesas Electronics America, Inc.
 * Copyright (C) 2017 Chris Brandt
 */

#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/watchdog.h>

#define DEFAULT_TIMEOUT		30

/* Watchdog Timer Registers */
#define WTCSR			0
#define WTCSR_MAGIC		0xA500
#define WTSCR_WT		BIT(6)
#define WTSCR_TME		BIT(5)
#define WTSCR_CKS(i)		(i)

#define WTCNT			2
#define WTCNT_MAGIC		0x5A00

#define WRCSR			4
#define WRCSR_MAGIC		0x5A00
#define WRCSR_RSTE		BIT(6)
#define WRCSR_CLEAR_WOVF	0xA500	/* special value */

/* The maximum CKS register setting value to get the longest timeout */
#define CKS_3BIT		0x7
#define CKS_4BIT		0xF

#define DIVIDER_3BIT		16384	/* Clock divider when CKS = 0x7 */
#define DIVIDER_4BIT		4194304	/* Clock divider when CKS = 0xF */

struct rza_wdt {
	struct watchdog_device wdev;
	void __iomem *base;
	struct clk *clk;
	u8 count;
	u8 cks;
};

static void rza_wdt_calc_timeout(struct rza_wdt *priv, int timeout)
{
	unsigned long rate = clk_get_rate(priv->clk);
	unsigned int ticks;

	if (priv->cks == CKS_4BIT) {
		ticks = DIV_ROUND_UP(timeout * rate, DIVIDER_4BIT);

		/*
		 * Since max_timeout was set in probe, we know that the timeout
		 * value passed will never calculate to a tick value greater
		 * than 256.
		 */
		priv->count = 256 - ticks;

	} else {
		/* Start timer with longest timeout */
		priv->count = 0;
	}

	pr_debug("%s: timeout set to %u (WTCNT=%d)\n", __func__,
		 timeout, priv->count);
}

static int rza_wdt_start(struct watchdog_device *wdev)
{
	struct rza_wdt *priv = watchdog_get_drvdata(wdev);

	/* Stop timer */
	writew(WTCSR_MAGIC | 0, priv->base + WTCSR);

	/* Must dummy read WRCSR:WOVF at least once before clearing */
	readb(priv->base + WRCSR);
	writew(WRCSR_CLEAR_WOVF, priv->base + WRCSR);

	rza_wdt_calc_timeout(priv, wdev->timeout);

	writew(WRCSR_MAGIC | WRCSR_RSTE, priv->base + WRCSR);
	writew(WTCNT_MAGIC | priv->count, priv->base + WTCNT);
	writew(WTCSR_MAGIC | WTSCR_WT | WTSCR_TME |
	       WTSCR_CKS(priv->cks), priv->base + WTCSR);

	return 0;
}

static int rza_wdt_stop(struct watchdog_device *wdev)
{
	struct rza_wdt *priv = watchdog_get_drvdata(wdev);

	writew(WTCSR_MAGIC | 0, priv->base + WTCSR);

	return 0;
}

static int rza_wdt_ping(struct watchdog_device *wdev)
{
	struct rza_wdt *priv = watchdog_get_drvdata(wdev);

	writew(WTCNT_MAGIC | priv->count, priv->base + WTCNT);

	pr_debug("%s: timeout = %u\n", __func__, wdev->timeout);

	return 0;
}

static int rza_set_timeout(struct watchdog_device *wdev, unsigned int timeout)
{
	wdev->timeout = timeout;
	rza_wdt_start(wdev);
	return 0;
}

static int rza_wdt_restart(struct watchdog_device *wdev, unsigned long action,
			    void *data)
{
	struct rza_wdt *priv = watchdog_get_drvdata(wdev);

	/* Stop timer */
	writew(WTCSR_MAGIC | 0, priv->base + WTCSR);

	/* Must dummy read WRCSR:WOVF at least once before clearing */
	readb(priv->base + WRCSR);
	writew(WRCSR_CLEAR_WOVF, priv->base + WRCSR);

	/*
	 * Start timer with fastest clock source and only 1 clock left before
	 * overflow with reset option enabled.
	 */
	writew(WRCSR_MAGIC | WRCSR_RSTE, priv->base + WRCSR);
	writew(WTCNT_MAGIC | 255, priv->base + WTCNT);
	writew(WTCSR_MAGIC | WTSCR_WT | WTSCR_TME, priv->base + WTCSR);

	/*
	 * Actually make sure the above sequence hits hardware before sleeping.
	 */
	wmb();

	/* Wait for WDT overflow (reset) */
	udelay(20);

	return 0;
}

static const struct watchdog_info rza_wdt_ident = {
	.options = WDIOF_MAGICCLOSE | WDIOF_KEEPALIVEPING | WDIOF_SETTIMEOUT,
	.identity = "Renesas RZ/A WDT Watchdog",
};

static const struct watchdog_ops rza_wdt_ops = {
	.owner = THIS_MODULE,
	.start = rza_wdt_start,
	.stop = rza_wdt_stop,
	.ping = rza_wdt_ping,
	.set_timeout = rza_set_timeout,
	.restart = rza_wdt_restart,
};

static int rza_wdt_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct rza_wdt *priv;
	unsigned long rate;
	int ret;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(priv->base))
		return PTR_ERR(priv->base);

	priv->clk = devm_clk_get(dev, NULL);
	if (IS_ERR(priv->clk))
		return PTR_ERR(priv->clk);

	rate = clk_get_rate(priv->clk);
	if (rate < 16384) {
		dev_err(dev, "invalid clock rate (%ld)\n", rate);
		return -ENOENT;
	}

	priv->wdev.info = &rza_wdt_ident;
	priv->wdev.ops = &rza_wdt_ops;
	priv->wdev.parent = dev;

	priv->cks = (u8)(uintptr_t) of_device_get_match_data(dev);
	if (priv->cks == CKS_4BIT) {
		/* Assume slowest clock rate possible (CKS=0xF) */
		priv->wdev.max_timeout = (DIVIDER_4BIT * U8_MAX) / rate;

	} else if (priv->cks == CKS_3BIT) {
		/* Assume slowest clock rate possible (CKS=7) */
		rate /= DIVIDER_3BIT;

		/*
		 * Since the max possible timeout of our 8-bit count
		 * register is less than a second, we must use
		 * max_hw_heartbeat_ms.
		 */
		priv->wdev.max_hw_heartbeat_ms = (1000 * U8_MAX) / rate;
		dev_dbg(dev, "max hw timeout of %dms\n",
			priv->wdev.max_hw_heartbeat_ms);
	}

	priv->wdev.min_timeout = 1;
	priv->wdev.timeout = DEFAULT_TIMEOUT;

	watchdog_init_timeout(&priv->wdev, 0, dev);
	watchdog_set_drvdata(&priv->wdev, priv);

	ret = devm_watchdog_register_device(dev, &priv->wdev);
	if (ret)
		dev_err(dev, "Cannot register watchdog device\n");

	return ret;
}

static const struct of_device_id rza_wdt_of_match[] = {
	{ .compatible = "renesas,r7s9210-wdt",	.data = (void *)CKS_4BIT, },
	{ .compatible = "renesas,rza-wdt",	.data = (void *)CKS_3BIT, },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, rza_wdt_of_match);

static struct platform_driver rza_wdt_driver = {
	.probe = rza_wdt_probe,
	.driver = {
		.name = "rza_wdt",
		.of_match_table = rza_wdt_of_match,
	},
};

module_platform_driver(rza_wdt_driver);

MODULE_DESCRIPTION("Renesas RZ/A WDT Driver");
MODULE_AUTHOR("Chris Brandt <chris.brandt@renesas.com>");
MODULE_LICENSE("GPL v2");

/*
 * Renesas RZ/A Series WDT Driver
 *
 * Copyright (C) 2017 Renesas Electronics America, Inc.
 * Copyright (C) 2017 Chris Brandt
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/of_address.h>
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

struct rza_wdt {
	struct watchdog_device wdev;
	void __iomem *base;
	struct clk *clk;
};

static int rza_wdt_start(struct watchdog_device *wdev)
{
	struct rza_wdt *priv = watchdog_get_drvdata(wdev);

	/* Stop timer */
	writew(WTCSR_MAGIC | 0, priv->base + WTCSR);

	/* Must dummy read WRCSR:WOVF at least once before clearing */
	readb(priv->base + WRCSR);
	writew(WRCSR_CLEAR_WOVF, priv->base + WRCSR);

	/*
	 * Start timer with slowest clock source and reset option enabled.
	 */
	writew(WRCSR_MAGIC | WRCSR_RSTE, priv->base + WRCSR);
	writew(WTCNT_MAGIC | 0, priv->base + WTCNT);
	writew(WTCSR_MAGIC | WTSCR_WT | WTSCR_TME | WTSCR_CKS(7),
	       priv->base + WTCSR);

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

	writew(WTCNT_MAGIC | 0, priv->base + WTCNT);

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
	.restart = rza_wdt_restart,
};

static int rza_wdt_probe(struct platform_device *pdev)
{
	struct rza_wdt *priv;
	struct resource *res;
	unsigned long rate;
	int ret;

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
	if (rate < 16384) {
		dev_err(&pdev->dev, "invalid clock rate (%ld)\n", rate);
		return -ENOENT;
	}

	/* Assume slowest clock rate possible (CKS=7) */
	rate /= 16384;

	priv->wdev.info = &rza_wdt_ident,
	priv->wdev.ops = &rza_wdt_ops,
	priv->wdev.parent = &pdev->dev;

	/*
	 * Since the max possible timeout of our 8-bit count register is less
	 * than a second, we must use max_hw_heartbeat_ms.
	 */
	priv->wdev.max_hw_heartbeat_ms = (1000 * U8_MAX) / rate;
	dev_dbg(&pdev->dev, "max hw timeout of %dms\n",
		 priv->wdev.max_hw_heartbeat_ms);

	priv->wdev.min_timeout = 1;
	priv->wdev.timeout = DEFAULT_TIMEOUT;

	watchdog_init_timeout(&priv->wdev, 0, &pdev->dev);
	watchdog_set_drvdata(&priv->wdev, priv);

	ret = devm_watchdog_register_device(&pdev->dev, &priv->wdev);
	if (ret)
		dev_err(&pdev->dev, "Cannot register watchdog device\n");

	return ret;
}

static const struct of_device_id rza_wdt_of_match[] = {
	{ .compatible = "renesas,rza-wdt", },
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

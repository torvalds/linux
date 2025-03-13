// SPDX-License-Identifier: GPL-2.0
/*
 * Renesas RZ/V2H(P) WDT Watchdog Driver
 *
 * Copyright (C) 2024 Renesas Electronics Corporation.
 */
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/reset.h>
#include <linux/units.h>
#include <linux/watchdog.h>

#define WDTRR			0x00	/* WDT Refresh Register RW, 8  */
#define WDTCR			0x02	/* WDT Control Register RW, 16 */
#define WDTSR			0x04	/* WDT Status Register RW, 16 */
#define WDTRCR			0x06	/* WDT Reset Control Register RW, 8  */

#define WDTCR_TOPS_1024		0x00
#define WDTCR_TOPS_16384	0x03

#define WDTCR_CKS_CLK_1		0x00
#define WDTCR_CKS_CLK_256	0x50

#define WDTCR_RPES_0		0x300
#define WDTCR_RPES_75		0x000

#define WDTCR_RPSS_25		0x00
#define WDTCR_RPSS_100		0x3000

#define WDTRCR_RSTIRQS		BIT(7)

#define MAX_TIMEOUT_CYCLES	16384
#define CLOCK_DIV_BY_256	256

#define WDT_DEFAULT_TIMEOUT	60U

static bool nowayout = WATCHDOG_NOWAYOUT;
module_param(nowayout, bool, 0);
MODULE_PARM_DESC(nowayout, "Watchdog cannot be stopped once started (default="
		 __MODULE_STRING(WATCHDOG_NOWAYOUT) ")");

struct rzv2h_wdt_priv {
	void __iomem *base;
	struct clk *pclk;
	struct clk *oscclk;
	struct reset_control *rstc;
	struct watchdog_device wdev;
};

static int rzv2h_wdt_ping(struct watchdog_device *wdev)
{
	struct rzv2h_wdt_priv *priv = watchdog_get_drvdata(wdev);

	/*
	 * The down-counter is refreshed and starts counting operation on
	 * a write of the values 00h and FFh to the WDTRR register.
	 */
	writeb(0x0, priv->base + WDTRR);
	writeb(0xFF, priv->base + WDTRR);

	return 0;
}

static void rzv2h_wdt_setup(struct watchdog_device *wdev, u16 wdtcr)
{
	struct rzv2h_wdt_priv *priv = watchdog_get_drvdata(wdev);

	/* Configure the timeout, clock division ratio, and window start and end positions. */
	writew(wdtcr, priv->base + WDTCR);

	/* Enable interrupt output to the ICU. */
	writeb(0, priv->base + WDTRCR);

	/* Clear underflow flag and refresh error flag. */
	writew(0, priv->base + WDTSR);
}

static int rzv2h_wdt_start(struct watchdog_device *wdev)
{
	struct rzv2h_wdt_priv *priv = watchdog_get_drvdata(wdev);
	int ret;

	ret = pm_runtime_resume_and_get(wdev->parent);
	if (ret)
		return ret;

	ret = reset_control_deassert(priv->rstc);
	if (ret) {
		pm_runtime_put(wdev->parent);
		return ret;
	}

	/* delay to handle clock halt after de-assert operation */
	udelay(3);

	/*
	 * WDTCR
	 * - CKS[7:4] - Clock Division Ratio Select - 0101b: oscclk/256
	 * - RPSS[13:12] - Window Start Position Select - 11b: 100%
	 * - RPES[9:8] - Window End Position Select - 11b: 0%
	 * - TOPS[1:0] - Timeout Period Select - 11b: 16384 cycles (3FFFh)
	 */
	rzv2h_wdt_setup(wdev, WDTCR_CKS_CLK_256 | WDTCR_RPSS_100 |
			WDTCR_RPES_0 | WDTCR_TOPS_16384);

	/*
	 * Down counting starts after writing the sequence 00h -> FFh to the
	 * WDTRR register. Hence, call the ping operation after loading the counter.
	 */
	rzv2h_wdt_ping(wdev);

	return 0;
}

static int rzv2h_wdt_stop(struct watchdog_device *wdev)
{
	struct rzv2h_wdt_priv *priv = watchdog_get_drvdata(wdev);
	int ret;

	ret = reset_control_assert(priv->rstc);
	if (ret)
		return ret;

	ret = pm_runtime_put(wdev->parent);
	if (ret < 0)
		return ret;

	return 0;
}

static const struct watchdog_info rzv2h_wdt_ident = {
	.options = WDIOF_MAGICCLOSE | WDIOF_KEEPALIVEPING | WDIOF_SETTIMEOUT,
	.identity = "Renesas RZ/V2H WDT Watchdog",
};

static int rzv2h_wdt_restart(struct watchdog_device *wdev,
			     unsigned long action, void *data)
{
	struct rzv2h_wdt_priv *priv = watchdog_get_drvdata(wdev);
	int ret;

	if (!watchdog_active(wdev)) {
		ret = clk_enable(priv->pclk);
		if (ret)
			return ret;

		ret = clk_enable(priv->oscclk);
		if (ret) {
			clk_disable(priv->pclk);
			return ret;
		}

		ret = reset_control_deassert(priv->rstc);
		if (ret) {
			clk_disable(priv->oscclk);
			clk_disable(priv->pclk);
			return ret;
		}
	} else {
		/*
		 * Writing to the WDT Control Register (WDTCR) or WDT Reset
		 * Control Register (WDTRCR) is possible once between the
		 * release from the reset state and the first refresh operation.
		 * Therefore, issue a reset if the watchdog is active.
		 */
		ret = reset_control_reset(priv->rstc);
		if (ret)
			return ret;
	}

	/* delay to handle clock halt after de-assert operation */
	udelay(3);

	/*
	 * WDTCR
	 * - CKS[7:4] - Clock Division Ratio Select - 0000b: oscclk/1
	 * - RPSS[13:12] - Window Start Position Select - 00b: 25%
	 * - RPES[9:8] - Window End Position Select - 00b: 75%
	 * - TOPS[1:0] - Timeout Period Select - 00b: 1024 cycles (03FFh)
	 */
	rzv2h_wdt_setup(wdev, WDTCR_CKS_CLK_1 | WDTCR_RPSS_25 |
			WDTCR_RPES_75 | WDTCR_TOPS_1024);

	rzv2h_wdt_ping(wdev);

	/* wait for underflow to trigger... */
	udelay(5);

	return 0;
}

static const struct watchdog_ops rzv2h_wdt_ops = {
	.owner = THIS_MODULE,
	.start = rzv2h_wdt_start,
	.stop = rzv2h_wdt_stop,
	.ping = rzv2h_wdt_ping,
	.restart = rzv2h_wdt_restart,
};

static int rzv2h_wdt_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct rzv2h_wdt_priv *priv;
	int ret;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(priv->base))
		return PTR_ERR(priv->base);

	priv->pclk = devm_clk_get_prepared(dev, "pclk");
	if (IS_ERR(priv->pclk))
		return dev_err_probe(dev, PTR_ERR(priv->pclk), "no pclk");

	priv->oscclk = devm_clk_get_prepared(dev, "oscclk");
	if (IS_ERR(priv->oscclk))
		return dev_err_probe(dev, PTR_ERR(priv->oscclk), "no oscclk");

	priv->rstc = devm_reset_control_get_exclusive(dev, NULL);
	if (IS_ERR(priv->rstc))
		return dev_err_probe(dev, PTR_ERR(priv->rstc),
				     "failed to get cpg reset");

	priv->wdev.max_hw_heartbeat_ms = (MILLI * MAX_TIMEOUT_CYCLES * CLOCK_DIV_BY_256) /
					 clk_get_rate(priv->oscclk);
	dev_dbg(dev, "max hw timeout of %dms\n", priv->wdev.max_hw_heartbeat_ms);

	ret = devm_pm_runtime_enable(dev);
	if (ret)
		return ret;

	priv->wdev.min_timeout = 1;
	priv->wdev.timeout = WDT_DEFAULT_TIMEOUT;
	priv->wdev.info = &rzv2h_wdt_ident;
	priv->wdev.ops = &rzv2h_wdt_ops;
	priv->wdev.parent = dev;
	watchdog_set_drvdata(&priv->wdev, priv);
	watchdog_set_nowayout(&priv->wdev, nowayout);
	watchdog_stop_on_unregister(&priv->wdev);

	ret = watchdog_init_timeout(&priv->wdev, 0, dev);
	if (ret)
		dev_warn(dev, "Specified timeout invalid, using default");

	return devm_watchdog_register_device(dev, &priv->wdev);
}

static const struct of_device_id rzv2h_wdt_ids[] = {
	{ .compatible = "renesas,r9a09g057-wdt", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, rzv2h_wdt_ids);

static struct platform_driver rzv2h_wdt_driver = {
	.driver = {
		.name = "rzv2h_wdt",
		.of_match_table = rzv2h_wdt_ids,
	},
	.probe = rzv2h_wdt_probe,
};
module_platform_driver(rzv2h_wdt_driver);
MODULE_AUTHOR("Lad Prabhakar <prabhakar.mahadev-lad.rj@bp.renesas.com>");
MODULE_DESCRIPTION("Renesas RZ/V2H(P) WDT Watchdog Driver");
MODULE_LICENSE("GPL");

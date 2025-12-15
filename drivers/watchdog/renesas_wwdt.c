// SPDX-License-Identifier: GPL-2.0
/*
 * Driver for the Renesas Window Watchdog Timer (WWDT)
 *
 * The WWDT can only be setup once after boot. Because we cannot know if this
 * already happened in early boot stages, it is mandated that the firmware
 * configures the watchdog. Linux then adapts according to the given setup.
 * Note that this watchdog reports in the default configuration an overflow to
 * the Error Control Module which then decides further actions. Or the WWDT is
 * configured to generate an interrupt.
 */

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/watchdog.h>

#define WDTA0WDTE	0x00
#define WDTA0RUN	BIT(7)
#define WDTA0_KEY	0x2c

#define WDTA0MD		0x0c
#define WDTA0OVF(x)	FIELD_GET(GENMASK(6, 4), x)
#define WDTA0WIE	BIT(3)
#define WDTA0ERM	BIT(2)
#define WDTA0WS(x)	FIELD_GET(GENMASK(1, 0), x)

struct wwdt_priv {
	void __iomem *base;
	struct watchdog_device wdev;
};

static int wwdt_start(struct watchdog_device *wdev)
{
	struct wwdt_priv *priv = container_of(wdev, struct wwdt_priv, wdev);

	writeb(WDTA0RUN | WDTA0_KEY, priv->base + WDTA0WDTE);
	return 0;
}

static const struct watchdog_info wwdt_ident = {
	.options = WDIOF_KEEPALIVEPING | WDIOF_ALARMONLY,
	.identity = "Renesas Window Watchdog",
};

static const struct watchdog_ops wwdt_ops = {
	.owner = THIS_MODULE,
	.start = wwdt_start,
};

static irqreturn_t wwdt_error_irq(int irq, void *dev_id)
{
	struct device *dev = dev_id;

	dev_warn(dev, "Watchdog timed out\n");
	return IRQ_HANDLED;
}

static irqreturn_t wwdt_pretimeout_irq(int irq, void *dev_id)
{
	struct watchdog_device *wdev = dev_id;

	watchdog_notify_pretimeout(wdev);
	return IRQ_HANDLED;
}

static int wwdt_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct wwdt_priv *priv;
	struct watchdog_device *wdev;
	struct clk *clk;
	unsigned long rate;
	unsigned int interval, window_size;
	int ret;
	u8 val;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(priv->base))
		return PTR_ERR(priv->base);

	clk = devm_clk_get(dev, "cnt");
	if (IS_ERR(clk))
		return PTR_ERR(clk);

	rate = clk_get_rate(clk);
	if (!rate)
		return -EINVAL;

	wdev = &priv->wdev;

	val = readb(priv->base + WDTA0WDTE);
	if (val & WDTA0RUN)
		set_bit(WDOG_HW_RUNNING, &wdev->status);

	val = readb(priv->base + WDTA0MD);
	interval = 1 << (9 + WDTA0OVF(val));
	/* size of the closed(!) window per mille */
	window_size = 250 * (3 - WDTA0WS(val));

	wdev->info = &wwdt_ident;
	wdev->ops = &wwdt_ops;
	wdev->parent = dev;
	wdev->min_hw_heartbeat_ms = window_size * interval / rate;
	wdev->max_hw_heartbeat_ms = 1000 * interval / rate;
	wdev->timeout = DIV_ROUND_UP(wdev->max_hw_heartbeat_ms, 1000);
	watchdog_set_nowayout(wdev, true);

	if (!(val & WDTA0ERM)) {
		ret = platform_get_irq_byname(pdev, "error");
		if (ret < 0)
			return ret;

		ret = devm_request_threaded_irq(dev, ret, NULL, wwdt_error_irq,
						IRQF_ONESHOT, NULL, dev);
		if (ret < 0)
			return ret;
	}

	if (val & WDTA0WIE) {
		ret = platform_get_irq_byname(pdev, "pretimeout");
		if (ret < 0)
			return ret;

		ret = devm_request_threaded_irq(dev, ret, NULL, wwdt_pretimeout_irq,
						IRQF_ONESHOT, NULL, wdev);
		if (ret < 0)
			return ret;
	}

	devm_watchdog_register_device(dev, wdev);

	return 0;
}

static const struct of_device_id renesas_wwdt_ids[] = {
	{ .compatible = "renesas,rcar-gen3-wwdt", },
	{ .compatible = "renesas,rcar-gen4-wwdt", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, renesas_wwdt_ids);

static struct platform_driver renesas_wwdt_driver = {
	.driver = {
		.name = "renesas_wwdt",
		.of_match_table = renesas_wwdt_ids,
	},
	.probe = wwdt_probe,
};
module_platform_driver(renesas_wwdt_driver);

MODULE_DESCRIPTION("Renesas Window Watchdog (WWDT) Driver");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Wolfram Sang <wsa+renesas@sang-engineering.com>");

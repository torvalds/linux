// SPDX-License-Identifier: GPL-2.0
/*
 * Renesas RZ/N1 Watchdog timer.
 * This is a 12-bit timer driver from a (62.5/16384) MHz clock. It can't even
 * cope with 2 seconds.
 *
 * Copyright 2018 Renesas Electronics Europe Ltd.
 *
 * Derived from Ralink RT288x watchdog timer.
 */

#include <linux/clk.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/reboot.h>
#include <linux/watchdog.h>

#define DEFAULT_TIMEOUT		60

#define RZN1_WDT_RETRIGGER			0x0
#define RZN1_WDT_RETRIGGER_RELOAD_VAL		0
#define RZN1_WDT_RETRIGGER_RELOAD_VAL_MASK	0xfff
#define RZN1_WDT_RETRIGGER_PRESCALE		BIT(12)
#define RZN1_WDT_RETRIGGER_ENABLE		BIT(13)
#define RZN1_WDT_RETRIGGER_WDSI			(0x2 << 14)

#define RZN1_WDT_PRESCALER			16384
#define RZN1_WDT_MAX				4095

struct rzn1_watchdog {
	struct watchdog_device		wdtdev;
	void __iomem			*base;
	unsigned long			clk_rate_khz;
};

static inline uint32_t max_heart_beat_ms(unsigned long clk_rate_khz)
{
	return (RZN1_WDT_MAX * RZN1_WDT_PRESCALER) / clk_rate_khz;
}

static inline uint32_t compute_reload_value(uint32_t tick_ms,
					    unsigned long clk_rate_khz)
{
	return (tick_ms * clk_rate_khz) / RZN1_WDT_PRESCALER;
}

static int rzn1_wdt_ping(struct watchdog_device *w)
{
	struct rzn1_watchdog *wdt = watchdog_get_drvdata(w);

	/* Any value retriggers the watchdog */
	writel(0, wdt->base + RZN1_WDT_RETRIGGER);

	return 0;
}

static int rzn1_wdt_start(struct watchdog_device *w)
{
	struct rzn1_watchdog *wdt = watchdog_get_drvdata(w);
	u32 val;

	/*
	 * The hardware allows you to write to this reg only once.
	 * Since this includes the reload value, there is no way to change the
	 * timeout once started. Also note that the WDT clock is half the bus
	 * fabric clock rate, so if the bus fabric clock rate is changed after
	 * the WDT is started, the WDT interval will be wrong.
	 */
	val = RZN1_WDT_RETRIGGER_WDSI;
	val |= RZN1_WDT_RETRIGGER_ENABLE;
	val |= RZN1_WDT_RETRIGGER_PRESCALE;
	val |= compute_reload_value(w->max_hw_heartbeat_ms, wdt->clk_rate_khz);
	writel(val, wdt->base + RZN1_WDT_RETRIGGER);

	return 0;
}

static irqreturn_t rzn1_wdt_irq(int irq, void *_wdt)
{
	pr_crit("RZN1 Watchdog. Initiating system reboot\n");
	emergency_restart();

	return IRQ_HANDLED;
}

static struct watchdog_info rzn1_wdt_info = {
	.identity = "RZ/N1 Watchdog",
	.options = WDIOF_MAGICCLOSE | WDIOF_SETTIMEOUT | WDIOF_KEEPALIVEPING,
};

static const struct watchdog_ops rzn1_wdt_ops = {
	.owner = THIS_MODULE,
	.start = rzn1_wdt_start,
	.ping = rzn1_wdt_ping,
};

static int rzn1_wdt_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct rzn1_watchdog *wdt;
	struct device_node *np = dev->of_node;
	struct clk *clk;
	unsigned long clk_rate;
	int ret;
	int irq;

	wdt = devm_kzalloc(dev, sizeof(*wdt), GFP_KERNEL);
	if (!wdt)
		return -ENOMEM;

	wdt->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(wdt->base))
		return PTR_ERR(wdt->base);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	ret = devm_request_irq(dev, irq, rzn1_wdt_irq, 0,
			       np->name, wdt);
	if (ret) {
		dev_err(dev, "failed to request irq %d\n", irq);
		return ret;
	}

	clk = devm_clk_get_enabled(dev, NULL);
	if (IS_ERR(clk)) {
		dev_err(dev, "failed to get the clock\n");
		return PTR_ERR(clk);
	}

	clk_rate = clk_get_rate(clk);
	if (!clk_rate) {
		dev_err(dev, "failed to get the clock rate\n");
		return -EINVAL;
	}

	wdt->clk_rate_khz = clk_rate / 1000;
	wdt->wdtdev.info = &rzn1_wdt_info;
	wdt->wdtdev.ops = &rzn1_wdt_ops;
	wdt->wdtdev.status = WATCHDOG_NOWAYOUT_INIT_STATUS;
	wdt->wdtdev.parent = dev;
	/*
	 * The period of the watchdog cannot be changed once set
	 * and is limited to a very short period.
	 * Configure it for a 1s period once and for all, and
	 * rely on the heart-beat provided by the watchdog core
	 * to make this usable by the user-space.
	 */
	wdt->wdtdev.max_hw_heartbeat_ms = max_heart_beat_ms(wdt->clk_rate_khz);
	if (wdt->wdtdev.max_hw_heartbeat_ms > 1000)
		wdt->wdtdev.max_hw_heartbeat_ms = 1000;

	wdt->wdtdev.timeout = DEFAULT_TIMEOUT;
	ret = watchdog_init_timeout(&wdt->wdtdev, 0, dev);
	if (ret)
		return ret;

	watchdog_set_drvdata(&wdt->wdtdev, wdt);

	return devm_watchdog_register_device(dev, &wdt->wdtdev);
}


static const struct of_device_id rzn1_wdt_match[] = {
	{ .compatible = "renesas,rzn1-wdt" },
	{},
};
MODULE_DEVICE_TABLE(of, rzn1_wdt_match);

static struct platform_driver rzn1_wdt_driver = {
	.probe		= rzn1_wdt_probe,
	.driver		= {
		.name		= KBUILD_MODNAME,
		.of_match_table	= rzn1_wdt_match,
	},
};

module_platform_driver(rzn1_wdt_driver);

MODULE_DESCRIPTION("Renesas RZ/N1 hardware watchdog");
MODULE_AUTHOR("Phil Edworthy <phil.edworthy@renesas.com>");
MODULE_LICENSE("GPL");

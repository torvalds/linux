// SPDX-License-Identifier: GPL-2.0-only
/*
 * Watchdog driver for Intel Keem Bay non-secure watchdog.
 *
 * Copyright (C) 2020 Intel Corporation
 */

#include <linux/arm-smccc.h>
#include <linux/bits.h>
#include <linux/clk.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/limits.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/platform_device.h>
#include <linux/reboot.h>
#include <linux/watchdog.h>

/* Non-secure watchdog register offsets */
#define TIM_WATCHDOG		0x0
#define TIM_WATCHDOG_INT_THRES	0x4
#define TIM_WDOG_EN		0x8
#define TIM_SAFE		0xc

#define WDT_ISR_MASK		GENMASK(9, 8)
#define WDT_ISR_CLEAR		0x8200ff18
#define WDT_UNLOCK		0xf1d0dead
#define WDT_LOAD_MAX		U32_MAX
#define WDT_LOAD_MIN		1
#define WDT_TIMEOUT		5

static unsigned int timeout = WDT_TIMEOUT;
module_param(timeout, int, 0);
MODULE_PARM_DESC(timeout, "Watchdog timeout period in seconds (default = "
		 __MODULE_STRING(WDT_TIMEOUT) ")");

static bool nowayout = WATCHDOG_NOWAYOUT;
module_param(nowayout, bool, 0);
MODULE_PARM_DESC(nowayout, "Watchdog cannot be stopped once started (default = "
		 __MODULE_STRING(WATCHDOG_NOWAYOUT) ")");

struct keembay_wdt {
	struct watchdog_device	wdd;
	struct clk		*clk;
	unsigned int		rate;
	int			to_irq;
	int			th_irq;
	void __iomem		*base;
};

static inline u32 keembay_wdt_readl(struct keembay_wdt *wdt, u32 offset)
{
	return readl(wdt->base + offset);
}

static inline void keembay_wdt_writel(struct keembay_wdt *wdt, u32 offset, u32 val)
{
	writel(WDT_UNLOCK, wdt->base + TIM_SAFE);
	writel(val, wdt->base + offset);
}

static void keembay_wdt_set_timeout_reg(struct watchdog_device *wdog)
{
	struct keembay_wdt *wdt = watchdog_get_drvdata(wdog);

	keembay_wdt_writel(wdt, TIM_WATCHDOG, wdog->timeout * wdt->rate);
}

static void keembay_wdt_set_pretimeout_reg(struct watchdog_device *wdog)
{
	struct keembay_wdt *wdt = watchdog_get_drvdata(wdog);
	u32 th_val = 0;

	if (wdog->pretimeout)
		th_val = wdog->timeout - wdog->pretimeout;

	keembay_wdt_writel(wdt, TIM_WATCHDOG_INT_THRES, th_val * wdt->rate);
}

static int keembay_wdt_start(struct watchdog_device *wdog)
{
	struct keembay_wdt *wdt = watchdog_get_drvdata(wdog);

	keembay_wdt_set_timeout_reg(wdog);
	keembay_wdt_writel(wdt, TIM_WDOG_EN, 1);

	return 0;
}

static int keembay_wdt_stop(struct watchdog_device *wdog)
{
	struct keembay_wdt *wdt = watchdog_get_drvdata(wdog);

	keembay_wdt_writel(wdt, TIM_WDOG_EN, 0);

	return 0;
}

static int keembay_wdt_ping(struct watchdog_device *wdog)
{
	keembay_wdt_set_timeout_reg(wdog);

	return 0;
}

static int keembay_wdt_set_timeout(struct watchdog_device *wdog, u32 t)
{
	wdog->timeout = t;
	keembay_wdt_set_timeout_reg(wdog);

	return 0;
}

static int keembay_wdt_set_pretimeout(struct watchdog_device *wdog, u32 t)
{
	if (t > wdog->timeout)
		return -EINVAL;

	wdog->pretimeout = t;
	keembay_wdt_set_pretimeout_reg(wdog);

	return 0;
}

static unsigned int keembay_wdt_get_timeleft(struct watchdog_device *wdog)
{
	struct keembay_wdt *wdt = watchdog_get_drvdata(wdog);

	return keembay_wdt_readl(wdt, TIM_WATCHDOG) / wdt->rate;
}

/*
 * SMC call is used to clear the interrupt bits, because the TIM_GEN_CONFIG
 * register is in the secure bank.
 */
static irqreturn_t keembay_wdt_to_isr(int irq, void *dev_id)
{
	struct keembay_wdt *wdt = dev_id;
	struct arm_smccc_res res;

	keembay_wdt_writel(wdt, TIM_WATCHDOG, 1);
	arm_smccc_smc(WDT_ISR_CLEAR, WDT_ISR_MASK, 0, 0, 0, 0, 0, 0, &res);
	dev_crit(wdt->wdd.parent, "Intel Keem Bay non-sec wdt timeout.\n");
	emergency_restart();

	return IRQ_HANDLED;
}

static irqreturn_t keembay_wdt_th_isr(int irq, void *dev_id)
{
	struct keembay_wdt *wdt = dev_id;
	struct arm_smccc_res res;

	arm_smccc_smc(WDT_ISR_CLEAR, WDT_ISR_MASK, 0, 0, 0, 0, 0, 0, &res);
	dev_crit(wdt->wdd.parent, "Intel Keem Bay non-sec wdt pre-timeout.\n");
	watchdog_notify_pretimeout(&wdt->wdd);

	return IRQ_HANDLED;
}

static const struct watchdog_info keembay_wdt_info = {
	.identity	= "Intel Keem Bay Watchdog Timer",
	.options	= WDIOF_SETTIMEOUT |
			  WDIOF_PRETIMEOUT |
			  WDIOF_MAGICCLOSE |
			  WDIOF_KEEPALIVEPING,
};

static const struct watchdog_ops keembay_wdt_ops = {
	.owner		= THIS_MODULE,
	.start		= keembay_wdt_start,
	.stop		= keembay_wdt_stop,
	.ping		= keembay_wdt_ping,
	.set_timeout	= keembay_wdt_set_timeout,
	.set_pretimeout	= keembay_wdt_set_pretimeout,
	.get_timeleft	= keembay_wdt_get_timeleft,
};

static int keembay_wdt_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct keembay_wdt *wdt;
	int ret;

	wdt = devm_kzalloc(dev, sizeof(*wdt), GFP_KERNEL);
	if (!wdt)
		return -ENOMEM;

	wdt->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(wdt->base))
		return PTR_ERR(wdt->base);

	/* we do not need to enable the clock as it is enabled by default */
	wdt->clk = devm_clk_get(dev, NULL);
	if (IS_ERR(wdt->clk))
		return dev_err_probe(dev, PTR_ERR(wdt->clk), "Failed to get clock\n");

	wdt->rate = clk_get_rate(wdt->clk);
	if (!wdt->rate)
		return dev_err_probe(dev, -EINVAL, "Failed to get clock rate\n");

	wdt->th_irq = platform_get_irq_byname(pdev, "threshold");
	if (wdt->th_irq < 0)
		return dev_err_probe(dev, wdt->th_irq, "Failed to get IRQ for threshold\n");

	ret = devm_request_irq(dev, wdt->th_irq, keembay_wdt_th_isr, 0,
			       "keembay-wdt", wdt);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to request IRQ for threshold\n");

	wdt->to_irq = platform_get_irq_byname(pdev, "timeout");
	if (wdt->to_irq < 0)
		return dev_err_probe(dev, wdt->to_irq, "Failed to get IRQ for timeout\n");

	ret = devm_request_irq(dev, wdt->to_irq, keembay_wdt_to_isr, 0,
			       "keembay-wdt", wdt);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to request IRQ for timeout\n");

	wdt->wdd.parent		= dev;
	wdt->wdd.info		= &keembay_wdt_info;
	wdt->wdd.ops		= &keembay_wdt_ops;
	wdt->wdd.min_timeout	= WDT_LOAD_MIN;
	wdt->wdd.max_timeout	= WDT_LOAD_MAX / wdt->rate;
	wdt->wdd.timeout	= WDT_TIMEOUT;

	watchdog_set_drvdata(&wdt->wdd, wdt);
	watchdog_set_nowayout(&wdt->wdd, nowayout);
	watchdog_init_timeout(&wdt->wdd, timeout, dev);
	keembay_wdt_set_timeout(&wdt->wdd, wdt->wdd.timeout);

	ret = devm_watchdog_register_device(dev, &wdt->wdd);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to register watchdog device.\n");

	platform_set_drvdata(pdev, wdt);
	dev_info(dev, "Initial timeout %d sec%s.\n",
		 wdt->wdd.timeout, nowayout ? ", nowayout" : "");

	return 0;
}

static int __maybe_unused keembay_wdt_suspend(struct device *dev)
{
	struct keembay_wdt *wdt = dev_get_drvdata(dev);

	if (watchdog_active(&wdt->wdd))
		return keembay_wdt_stop(&wdt->wdd);

	return 0;
}

static int __maybe_unused keembay_wdt_resume(struct device *dev)
{
	struct keembay_wdt *wdt = dev_get_drvdata(dev);

	if (watchdog_active(&wdt->wdd))
		return keembay_wdt_start(&wdt->wdd);

	return 0;
}

static SIMPLE_DEV_PM_OPS(keembay_wdt_pm_ops, keembay_wdt_suspend,
			 keembay_wdt_resume);

static const struct of_device_id keembay_wdt_match[] = {
	{ .compatible = "intel,keembay-wdt" },
	{ }
};
MODULE_DEVICE_TABLE(of, keembay_wdt_match);

static struct platform_driver keembay_wdt_driver = {
	.probe		= keembay_wdt_probe,
	.driver		= {
		.name		= "keembay_wdt",
		.of_match_table	= keembay_wdt_match,
		.pm		= &keembay_wdt_pm_ops,
	},
};

module_platform_driver(keembay_wdt_driver);

MODULE_DESCRIPTION("Intel Keem Bay SoC watchdog driver");
MODULE_AUTHOR("Wan Ahmad Zainie <wan.ahmad.zainie.wan.mohamad@intel.com");
MODULE_LICENSE("GPL v2");

// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright 2010-2011 Picochip Ltd., Jamie Iles
 * http://www.picochip.com
 *
 * This file implements a driver for the Synopsys DesignWare watchdog device
 * in the many subsystems. The watchdog has 16 different timeout periods
 * and these are a function of the input clock frequency.
 *
 * The DesignWare watchdog cannot be stopped once it has been started so we
 * do not implement a stop function. The watchdog core will continue to send
 * heartbeat requests after the watchdog device has been closed.
 */

#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/of.h>
#include <linux/pm.h>
#include <linux/platform_device.h>
#include <linux/reset.h>
#include <linux/watchdog.h>

#define WDOG_CONTROL_REG_OFFSET		    0x00
#define WDOG_CONTROL_REG_WDT_EN_MASK	    0x01
#define WDOG_CONTROL_REG_RESP_MODE_MASK	    0x02
#define WDOG_TIMEOUT_RANGE_REG_OFFSET	    0x04
#define WDOG_TIMEOUT_RANGE_TOPINIT_SHIFT    4
#define WDOG_CURRENT_COUNT_REG_OFFSET	    0x08
#define WDOG_COUNTER_RESTART_REG_OFFSET     0x0c
#define WDOG_COUNTER_RESTART_KICK_VALUE	    0x76

/* The maximum TOP (timeout period) value that can be set in the watchdog. */
#define DW_WDT_MAX_TOP		15

#define DW_WDT_DEFAULT_SECONDS	30

static bool nowayout = WATCHDOG_NOWAYOUT;
module_param(nowayout, bool, 0);
MODULE_PARM_DESC(nowayout, "Watchdog cannot be stopped once started "
		 "(default=" __MODULE_STRING(WATCHDOG_NOWAYOUT) ")");

struct dw_wdt {
	void __iomem		*regs;
	struct clk		*clk;
	unsigned long		rate;
	struct watchdog_device	wdd;
	struct reset_control	*rst;
	/* Save/restore */
	u32			control;
	u32			timeout;
};

#define to_dw_wdt(wdd)	container_of(wdd, struct dw_wdt, wdd)

static inline int dw_wdt_is_enabled(struct dw_wdt *dw_wdt)
{
	return readl(dw_wdt->regs + WDOG_CONTROL_REG_OFFSET) &
		WDOG_CONTROL_REG_WDT_EN_MASK;
}

static inline int dw_wdt_top_in_seconds(struct dw_wdt *dw_wdt, unsigned top)
{
	/*
	 * There are 16 possible timeout values in 0..15 where the number of
	 * cycles is 2 ^ (16 + i) and the watchdog counts down.
	 */
	return (1U << (16 + top)) / dw_wdt->rate;
}

static int dw_wdt_get_top(struct dw_wdt *dw_wdt)
{
	int top = readl(dw_wdt->regs + WDOG_TIMEOUT_RANGE_REG_OFFSET) & 0xF;

	return dw_wdt_top_in_seconds(dw_wdt, top);
}

static int dw_wdt_ping(struct watchdog_device *wdd)
{
	struct dw_wdt *dw_wdt = to_dw_wdt(wdd);

	writel(WDOG_COUNTER_RESTART_KICK_VALUE, dw_wdt->regs +
	       WDOG_COUNTER_RESTART_REG_OFFSET);

	return 0;
}

static int dw_wdt_set_timeout(struct watchdog_device *wdd, unsigned int top_s)
{
	struct dw_wdt *dw_wdt = to_dw_wdt(wdd);
	int i, top_val = DW_WDT_MAX_TOP;

	/*
	 * Iterate over the timeout values until we find the closest match. We
	 * always look for >=.
	 */
	for (i = 0; i <= DW_WDT_MAX_TOP; ++i)
		if (dw_wdt_top_in_seconds(dw_wdt, i) >= top_s) {
			top_val = i;
			break;
		}

	/*
	 * Set the new value in the watchdog.  Some versions of dw_wdt
	 * have have TOPINIT in the TIMEOUT_RANGE register (as per
	 * CP_WDT_DUAL_TOP in WDT_COMP_PARAMS_1).  On those we
	 * effectively get a pat of the watchdog right here.
	 */
	writel(top_val | top_val << WDOG_TIMEOUT_RANGE_TOPINIT_SHIFT,
	       dw_wdt->regs + WDOG_TIMEOUT_RANGE_REG_OFFSET);

	wdd->timeout = dw_wdt_top_in_seconds(dw_wdt, top_val);

	return 0;
}

static void dw_wdt_arm_system_reset(struct dw_wdt *dw_wdt)
{
	u32 val = readl(dw_wdt->regs + WDOG_CONTROL_REG_OFFSET);

	/* Disable interrupt mode; always perform system reset. */
	val &= ~WDOG_CONTROL_REG_RESP_MODE_MASK;
	/* Enable watchdog. */
	val |= WDOG_CONTROL_REG_WDT_EN_MASK;
	writel(val, dw_wdt->regs + WDOG_CONTROL_REG_OFFSET);
}

static int dw_wdt_start(struct watchdog_device *wdd)
{
	struct dw_wdt *dw_wdt = to_dw_wdt(wdd);

	dw_wdt_set_timeout(wdd, wdd->timeout);
	dw_wdt_arm_system_reset(dw_wdt);

	return 0;
}

static int dw_wdt_stop(struct watchdog_device *wdd)
{
	struct dw_wdt *dw_wdt = to_dw_wdt(wdd);

	if (!dw_wdt->rst) {
		set_bit(WDOG_HW_RUNNING, &wdd->status);
		return 0;
	}

	reset_control_assert(dw_wdt->rst);
	reset_control_deassert(dw_wdt->rst);

	return 0;
}

static int dw_wdt_restart(struct watchdog_device *wdd,
			  unsigned long action, void *data)
{
	struct dw_wdt *dw_wdt = to_dw_wdt(wdd);

	writel(0, dw_wdt->regs + WDOG_TIMEOUT_RANGE_REG_OFFSET);
	if (dw_wdt_is_enabled(dw_wdt))
		writel(WDOG_COUNTER_RESTART_KICK_VALUE,
		       dw_wdt->regs + WDOG_COUNTER_RESTART_REG_OFFSET);
	else
		dw_wdt_arm_system_reset(dw_wdt);

	/* wait for reset to assert... */
	mdelay(500);

	return 0;
}

static unsigned int dw_wdt_get_timeleft(struct watchdog_device *wdd)
{
	struct dw_wdt *dw_wdt = to_dw_wdt(wdd);

	return readl(dw_wdt->regs + WDOG_CURRENT_COUNT_REG_OFFSET) /
		dw_wdt->rate;
}

static const struct watchdog_info dw_wdt_ident = {
	.options	= WDIOF_KEEPALIVEPING | WDIOF_SETTIMEOUT |
			  WDIOF_MAGICCLOSE,
	.identity	= "Synopsys DesignWare Watchdog",
};

static const struct watchdog_ops dw_wdt_ops = {
	.owner		= THIS_MODULE,
	.start		= dw_wdt_start,
	.stop		= dw_wdt_stop,
	.ping		= dw_wdt_ping,
	.set_timeout	= dw_wdt_set_timeout,
	.get_timeleft	= dw_wdt_get_timeleft,
	.restart	= dw_wdt_restart,
};

#ifdef CONFIG_PM_SLEEP
static int dw_wdt_suspend(struct device *dev)
{
	struct dw_wdt *dw_wdt = dev_get_drvdata(dev);

	dw_wdt->control = readl(dw_wdt->regs + WDOG_CONTROL_REG_OFFSET);
	dw_wdt->timeout = readl(dw_wdt->regs + WDOG_TIMEOUT_RANGE_REG_OFFSET);

	clk_disable_unprepare(dw_wdt->clk);

	return 0;
}

static int dw_wdt_resume(struct device *dev)
{
	struct dw_wdt *dw_wdt = dev_get_drvdata(dev);
	int err = clk_prepare_enable(dw_wdt->clk);

	if (err)
		return err;

	writel(dw_wdt->timeout, dw_wdt->regs + WDOG_TIMEOUT_RANGE_REG_OFFSET);
	writel(dw_wdt->control, dw_wdt->regs + WDOG_CONTROL_REG_OFFSET);

	dw_wdt_ping(&dw_wdt->wdd);

	return 0;
}
#endif /* CONFIG_PM_SLEEP */

static SIMPLE_DEV_PM_OPS(dw_wdt_pm_ops, dw_wdt_suspend, dw_wdt_resume);

static int dw_wdt_drv_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct watchdog_device *wdd;
	struct dw_wdt *dw_wdt;
	int ret;

	dw_wdt = devm_kzalloc(dev, sizeof(*dw_wdt), GFP_KERNEL);
	if (!dw_wdt)
		return -ENOMEM;

	dw_wdt->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(dw_wdt->regs))
		return PTR_ERR(dw_wdt->regs);

	dw_wdt->clk = devm_clk_get(dev, NULL);
	if (IS_ERR(dw_wdt->clk))
		return PTR_ERR(dw_wdt->clk);

	ret = clk_prepare_enable(dw_wdt->clk);
	if (ret)
		return ret;

	dw_wdt->rate = clk_get_rate(dw_wdt->clk);
	if (dw_wdt->rate == 0) {
		ret = -EINVAL;
		goto out_disable_clk;
	}

	dw_wdt->rst = devm_reset_control_get_optional_shared(&pdev->dev, NULL);
	if (IS_ERR(dw_wdt->rst)) {
		ret = PTR_ERR(dw_wdt->rst);
		goto out_disable_clk;
	}

	reset_control_deassert(dw_wdt->rst);

	wdd = &dw_wdt->wdd;
	wdd->info = &dw_wdt_ident;
	wdd->ops = &dw_wdt_ops;
	wdd->min_timeout = 1;
	wdd->max_hw_heartbeat_ms =
		dw_wdt_top_in_seconds(dw_wdt, DW_WDT_MAX_TOP) * 1000;
	wdd->parent = dev;

	watchdog_set_drvdata(wdd, dw_wdt);
	watchdog_set_nowayout(wdd, nowayout);
	watchdog_init_timeout(wdd, 0, dev);

	/*
	 * If the watchdog is already running, use its already configured
	 * timeout. Otherwise use the default or the value provided through
	 * devicetree.
	 */
	if (dw_wdt_is_enabled(dw_wdt)) {
		wdd->timeout = dw_wdt_get_top(dw_wdt);
		set_bit(WDOG_HW_RUNNING, &wdd->status);
	} else {
		wdd->timeout = DW_WDT_DEFAULT_SECONDS;
		watchdog_init_timeout(wdd, 0, dev);
	}

	platform_set_drvdata(pdev, dw_wdt);

	watchdog_set_restart_priority(wdd, 128);

	ret = watchdog_register_device(wdd);
	if (ret)
		goto out_disable_clk;

	return 0;

out_disable_clk:
	clk_disable_unprepare(dw_wdt->clk);
	return ret;
}

static int dw_wdt_drv_remove(struct platform_device *pdev)
{
	struct dw_wdt *dw_wdt = platform_get_drvdata(pdev);

	watchdog_unregister_device(&dw_wdt->wdd);
	reset_control_assert(dw_wdt->rst);
	clk_disable_unprepare(dw_wdt->clk);

	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id dw_wdt_of_match[] = {
	{ .compatible = "snps,dw-wdt", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, dw_wdt_of_match);
#endif

static struct platform_driver dw_wdt_driver = {
	.probe		= dw_wdt_drv_probe,
	.remove		= dw_wdt_drv_remove,
	.driver		= {
		.name	= "dw_wdt",
		.of_match_table = of_match_ptr(dw_wdt_of_match),
		.pm	= &dw_wdt_pm_ops,
	},
};

module_platform_driver(dw_wdt_driver);

MODULE_AUTHOR("Jamie Iles");
MODULE_DESCRIPTION("Synopsys DesignWare Watchdog Driver");
MODULE_LICENSE("GPL");

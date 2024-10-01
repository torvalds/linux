// SPDX-License-Identifier: GPL-2.0-only OR MIT
/*
 * Apple SoC Watchdog driver
 *
 * Copyright (C) The Asahi Linux Contributors
 */

#include <linux/bits.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/limits.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/watchdog.h>

/*
 * Apple Watchdog MMIO registers
 *
 * This HW block has three separate watchdogs. WD0 resets the machine
 * to recovery mode and is not very useful for us. WD1 and WD2 trigger a normal
 * machine reset. WD0 additionally supports a configurable interrupt.
 * This information can be used to implement pretimeout support at a later time.
 *
 * APPLE_WDT_WDx_CUR_TIME is a simple counter incremented for each tick of the
 * reference clock. It can also be overwritten to any value.
 * Whenever APPLE_WDT_CTRL_RESET_EN is set in APPLE_WDT_WDx_CTRL and
 * APPLE_WDT_WDx_CUR_TIME >= APPLE_WDT_WDx_BITE_TIME the entire machine is
 * reset.
 * Whenever APPLE_WDT_CTRL_IRQ_EN is set and APPLE_WDTx_WD1_CUR_TIME >=
 * APPLE_WDTx_WD1_BARK_TIME an interrupt is triggered and
 * APPLE_WDT_CTRL_IRQ_STATUS is set. The interrupt can be cleared by writing
 * 1 to APPLE_WDT_CTRL_IRQ_STATUS.
 */
#define APPLE_WDT_WD0_CUR_TIME		0x00
#define APPLE_WDT_WD0_BITE_TIME		0x04
#define APPLE_WDT_WD0_BARK_TIME		0x08
#define APPLE_WDT_WD0_CTRL		0x0c

#define APPLE_WDT_WD1_CUR_TIME		0x10
#define APPLE_WDT_WD1_BITE_TIME		0x14
#define APPLE_WDT_WD1_CTRL		0x1c

#define APPLE_WDT_WD2_CUR_TIME		0x20
#define APPLE_WDT_WD2_BITE_TIME		0x24
#define APPLE_WDT_WD2_CTRL		0x2c

#define APPLE_WDT_CTRL_IRQ_EN		BIT(0)
#define APPLE_WDT_CTRL_IRQ_STATUS	BIT(1)
#define APPLE_WDT_CTRL_RESET_EN		BIT(2)

#define APPLE_WDT_TIMEOUT_DEFAULT	30

struct apple_wdt {
	struct watchdog_device wdd;
	void __iomem *regs;
	unsigned long clk_rate;
};

static struct apple_wdt *to_apple_wdt(struct watchdog_device *wdd)
{
	return container_of(wdd, struct apple_wdt, wdd);
}

static int apple_wdt_start(struct watchdog_device *wdd)
{
	struct apple_wdt *wdt = to_apple_wdt(wdd);

	writel_relaxed(0, wdt->regs + APPLE_WDT_WD1_CUR_TIME);
	writel_relaxed(APPLE_WDT_CTRL_RESET_EN, wdt->regs + APPLE_WDT_WD1_CTRL);

	return 0;
}

static int apple_wdt_stop(struct watchdog_device *wdd)
{
	struct apple_wdt *wdt = to_apple_wdt(wdd);

	writel_relaxed(0, wdt->regs + APPLE_WDT_WD1_CTRL);

	return 0;
}

static int apple_wdt_ping(struct watchdog_device *wdd)
{
	struct apple_wdt *wdt = to_apple_wdt(wdd);

	writel_relaxed(0, wdt->regs + APPLE_WDT_WD1_CUR_TIME);

	return 0;
}

static int apple_wdt_set_timeout(struct watchdog_device *wdd, unsigned int s)
{
	struct apple_wdt *wdt = to_apple_wdt(wdd);

	writel_relaxed(0, wdt->regs + APPLE_WDT_WD1_CUR_TIME);
	writel_relaxed(wdt->clk_rate * s, wdt->regs + APPLE_WDT_WD1_BITE_TIME);

	wdd->timeout = s;

	return 0;
}

static unsigned int apple_wdt_get_timeleft(struct watchdog_device *wdd)
{
	struct apple_wdt *wdt = to_apple_wdt(wdd);
	u32 cur_time, reset_time;

	cur_time = readl_relaxed(wdt->regs + APPLE_WDT_WD1_CUR_TIME);
	reset_time = readl_relaxed(wdt->regs + APPLE_WDT_WD1_BITE_TIME);

	return (reset_time - cur_time) / wdt->clk_rate;
}

static int apple_wdt_restart(struct watchdog_device *wdd, unsigned long mode,
			     void *cmd)
{
	struct apple_wdt *wdt = to_apple_wdt(wdd);

	writel_relaxed(APPLE_WDT_CTRL_RESET_EN, wdt->regs + APPLE_WDT_WD1_CTRL);
	writel_relaxed(0, wdt->regs + APPLE_WDT_WD1_BITE_TIME);
	writel_relaxed(0, wdt->regs + APPLE_WDT_WD1_CUR_TIME);

	/*
	 * Flush writes and then wait for the SoC to reset. Even though the
	 * reset is queued almost immediately experiments have shown that it
	 * can take up to ~20-25ms until the SoC is actually reset. Just wait
	 * 50ms here to be safe.
	 */
	(void)readl(wdt->regs + APPLE_WDT_WD1_CUR_TIME);
	mdelay(50);

	return 0;
}

static struct watchdog_ops apple_wdt_ops = {
	.owner = THIS_MODULE,
	.start = apple_wdt_start,
	.stop = apple_wdt_stop,
	.ping = apple_wdt_ping,
	.set_timeout = apple_wdt_set_timeout,
	.get_timeleft = apple_wdt_get_timeleft,
	.restart = apple_wdt_restart,
};

static struct watchdog_info apple_wdt_info = {
	.identity = "Apple SoC Watchdog",
	.options = WDIOF_MAGICCLOSE | WDIOF_KEEPALIVEPING | WDIOF_SETTIMEOUT,
};

static int apple_wdt_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct apple_wdt *wdt;
	struct clk *clk;
	u32 wdt_ctrl;

	wdt = devm_kzalloc(dev, sizeof(*wdt), GFP_KERNEL);
	if (!wdt)
		return -ENOMEM;

	wdt->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(wdt->regs))
		return PTR_ERR(wdt->regs);

	clk = devm_clk_get_enabled(dev, NULL);
	if (IS_ERR(clk))
		return PTR_ERR(clk);
	wdt->clk_rate = clk_get_rate(clk);
	if (!wdt->clk_rate)
		return -EINVAL;

	platform_set_drvdata(pdev, wdt);

	wdt->wdd.ops = &apple_wdt_ops;
	wdt->wdd.info = &apple_wdt_info;
	wdt->wdd.max_timeout = U32_MAX / wdt->clk_rate;
	wdt->wdd.timeout = APPLE_WDT_TIMEOUT_DEFAULT;

	wdt_ctrl = readl_relaxed(wdt->regs + APPLE_WDT_WD1_CTRL);
	if (wdt_ctrl & APPLE_WDT_CTRL_RESET_EN)
		set_bit(WDOG_HW_RUNNING, &wdt->wdd.status);

	watchdog_init_timeout(&wdt->wdd, 0, dev);
	apple_wdt_set_timeout(&wdt->wdd, wdt->wdd.timeout);
	watchdog_stop_on_unregister(&wdt->wdd);
	watchdog_set_restart_priority(&wdt->wdd, 128);

	return devm_watchdog_register_device(dev, &wdt->wdd);
}

static int apple_wdt_resume(struct device *dev)
{
	struct apple_wdt *wdt = dev_get_drvdata(dev);

	if (watchdog_active(&wdt->wdd) || watchdog_hw_running(&wdt->wdd))
		apple_wdt_start(&wdt->wdd);

	return 0;
}

static int apple_wdt_suspend(struct device *dev)
{
	struct apple_wdt *wdt = dev_get_drvdata(dev);

	if (watchdog_active(&wdt->wdd) || watchdog_hw_running(&wdt->wdd))
		apple_wdt_stop(&wdt->wdd);

	return 0;
}

static DEFINE_SIMPLE_DEV_PM_OPS(apple_wdt_pm_ops, apple_wdt_suspend, apple_wdt_resume);

static const struct of_device_id apple_wdt_of_match[] = {
	{ .compatible = "apple,wdt" },
	{},
};
MODULE_DEVICE_TABLE(of, apple_wdt_of_match);

static struct platform_driver apple_wdt_driver = {
	.driver = {
		.name = "apple-watchdog",
		.of_match_table = apple_wdt_of_match,
		.pm = pm_sleep_ptr(&apple_wdt_pm_ops),
	},
	.probe = apple_wdt_probe,
};
module_platform_driver(apple_wdt_driver);

MODULE_DESCRIPTION("Apple SoC watchdog driver");
MODULE_AUTHOR("Sven Peter <sven@svenpeter.dev>");
MODULE_LICENSE("Dual MIT/GPL");

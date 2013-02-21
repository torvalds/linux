/*
 * drivers/watchdog/orion_wdt.c
 *
 * Watchdog driver for Orion/Kirkwood processors
 *
 * Author: Sylver Bruneau <sylver.bruneau@googlemail.com>
 *
 * This file is licensed under  the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/miscdevice.h>
#include <linux/platform_device.h>
#include <linux/watchdog.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/spinlock.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/of.h>
#include <mach/bridge-regs.h>

/*
 * Watchdog timer block registers.
 */
#define TIMER_CTRL		0x0000
#define WDT_EN			0x0010
#define WDT_VAL			0x0024

#define WDT_MAX_CYCLE_COUNT	0xffffffff
#define WDT_IN_USE		0
#define WDT_OK_TO_CLOSE		1

static bool nowayout = WATCHDOG_NOWAYOUT;
static int heartbeat = -1;		/* module parameter (seconds) */
static unsigned int wdt_max_duration;	/* (seconds) */
static struct clk *clk;
static unsigned int wdt_tclk;
static void __iomem *wdt_reg;
static DEFINE_SPINLOCK(wdt_lock);

static int orion_wdt_ping(struct watchdog_device *wdt_dev)
{
	spin_lock(&wdt_lock);

	/* Reload watchdog duration */
	writel(wdt_tclk * wdt_dev->timeout, wdt_reg + WDT_VAL);

	spin_unlock(&wdt_lock);
	return 0;
}

static int orion_wdt_start(struct watchdog_device *wdt_dev)
{
	u32 reg;

	spin_lock(&wdt_lock);

	/* Set watchdog duration */
	writel(wdt_tclk * wdt_dev->timeout, wdt_reg + WDT_VAL);

	/* Clear watchdog timer interrupt */
	reg = readl(BRIDGE_CAUSE);
	reg &= ~WDT_INT_REQ;
	writel(reg, BRIDGE_CAUSE);

	/* Enable watchdog timer */
	reg = readl(wdt_reg + TIMER_CTRL);
	reg |= WDT_EN;
	writel(reg, wdt_reg + TIMER_CTRL);

	/* Enable reset on watchdog */
	reg = readl(RSTOUTn_MASK);
	reg |= WDT_RESET_OUT_EN;
	writel(reg, RSTOUTn_MASK);

	spin_unlock(&wdt_lock);
	return 0;
}

static int orion_wdt_stop(struct watchdog_device *wdt_dev)
{
	u32 reg;

	spin_lock(&wdt_lock);

	/* Disable reset on watchdog */
	reg = readl(RSTOUTn_MASK);
	reg &= ~WDT_RESET_OUT_EN;
	writel(reg, RSTOUTn_MASK);

	/* Disable watchdog timer */
	reg = readl(wdt_reg + TIMER_CTRL);
	reg &= ~WDT_EN;
	writel(reg, wdt_reg + TIMER_CTRL);

	spin_unlock(&wdt_lock);
	return 0;
}

static unsigned int orion_wdt_get_timeleft(struct watchdog_device *wdt_dev)
{
	unsigned int time_left;

	spin_lock(&wdt_lock);
	time_left = readl(wdt_reg + WDT_VAL) / wdt_tclk;
	spin_unlock(&wdt_lock);

	return time_left;
}

static int orion_wdt_set_timeout(struct watchdog_device *wdt_dev,
				 unsigned int timeout)
{
	wdt_dev->timeout = timeout;
	return 0;
}

static const struct watchdog_info orion_wdt_info = {
	.options = WDIOF_SETTIMEOUT | WDIOF_KEEPALIVEPING | WDIOF_MAGICCLOSE,
	.identity = "Orion Watchdog",
};

static const struct watchdog_ops orion_wdt_ops = {
	.owner = THIS_MODULE,
	.start = orion_wdt_start,
	.stop = orion_wdt_stop,
	.ping = orion_wdt_ping,
	.set_timeout = orion_wdt_set_timeout,
	.get_timeleft = orion_wdt_get_timeleft,
};

static struct watchdog_device orion_wdt = {
	.info = &orion_wdt_info,
	.ops = &orion_wdt_ops,
};

static int orion_wdt_probe(struct platform_device *pdev)
{
	struct resource *res;
	int ret;

	clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(clk)) {
		dev_err(&pdev->dev, "Orion Watchdog missing clock\n");
		return -ENODEV;
	}
	clk_prepare_enable(clk);
	wdt_tclk = clk_get_rate(clk);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -ENODEV;
	wdt_reg = devm_ioremap(&pdev->dev, res->start, resource_size(res));
	if (!wdt_reg)
		return -ENOMEM;

	wdt_max_duration = WDT_MAX_CYCLE_COUNT / wdt_tclk;

	if ((heartbeat < 1) || (heartbeat > wdt_max_duration))
		heartbeat = wdt_max_duration;

	orion_wdt.timeout = heartbeat;
	orion_wdt.min_timeout = 1;
	orion_wdt.max_timeout = wdt_max_duration;

	watchdog_set_nowayout(&orion_wdt, nowayout);
	ret = watchdog_register_device(&orion_wdt);
	if (ret) {
		clk_disable_unprepare(clk);
		return ret;
	}

	pr_info("Initial timeout %d sec%s\n",
		heartbeat, nowayout ? ", nowayout" : "");
	return 0;
}

static int orion_wdt_remove(struct platform_device *pdev)
{
	watchdog_unregister_device(&orion_wdt);
	clk_disable_unprepare(clk);
	return 0;
}

static void orion_wdt_shutdown(struct platform_device *pdev)
{
	orion_wdt_stop(&orion_wdt);
}

static const struct of_device_id orion_wdt_of_match_table[] = {
	{ .compatible = "marvell,orion-wdt", },
	{},
};
MODULE_DEVICE_TABLE(of, orion_wdt_of_match_table);

static struct platform_driver orion_wdt_driver = {
	.probe		= orion_wdt_probe,
	.remove		= orion_wdt_remove,
	.shutdown	= orion_wdt_shutdown,
	.driver		= {
		.owner	= THIS_MODULE,
		.name	= "orion_wdt",
		.of_match_table = of_match_ptr(orion_wdt_of_match_table),
	},
};

module_platform_driver(orion_wdt_driver);

MODULE_AUTHOR("Sylver Bruneau <sylver.bruneau@googlemail.com>");
MODULE_DESCRIPTION("Orion Processor Watchdog");

module_param(heartbeat, int, 0);
MODULE_PARM_DESC(heartbeat, "Initial watchdog heartbeat in seconds");

module_param(nowayout, bool, 0);
MODULE_PARM_DESC(nowayout, "Watchdog cannot be stopped once started (default="
				__MODULE_STRING(WATCHDOG_NOWAYOUT) ")");

MODULE_LICENSE("GPL");
MODULE_ALIAS_MISCDEV(WATCHDOG_MINOR);

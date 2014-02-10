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
#include <linux/platform_device.h>
#include <linux/watchdog.h>
#include <linux/init.h>
#include <linux/io.h>
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

#define WDT_RESET_OUT_EN	BIT(1)
#define WDT_INT_REQ		BIT(3)

static bool nowayout = WATCHDOG_NOWAYOUT;
static int heartbeat = -1;		/* module parameter (seconds) */
static unsigned int wdt_max_duration;	/* (seconds) */
static struct clk *clk;
static unsigned int wdt_tclk;
static void __iomem *wdt_reg;

static int orion_wdt_ping(struct watchdog_device *wdt_dev)
{
	/* Reload watchdog duration */
	writel(wdt_tclk * wdt_dev->timeout, wdt_reg + WDT_VAL);
	return 0;
}

static int orion_wdt_start(struct watchdog_device *wdt_dev)
{
	/* Set watchdog duration */
	writel(wdt_tclk * wdt_dev->timeout, wdt_reg + WDT_VAL);

	/* Clear watchdog timer interrupt */
	writel(~WDT_INT_REQ, BRIDGE_CAUSE);

	/* Enable watchdog timer */
	atomic_io_modify(wdt_reg + TIMER_CTRL, WDT_EN, WDT_EN);

	/* Enable reset on watchdog */
	atomic_io_modify(RSTOUTn_MASK, WDT_RESET_OUT_EN, WDT_RESET_OUT_EN);
	return 0;
}

static int orion_wdt_stop(struct watchdog_device *wdt_dev)
{
	/* Disable reset on watchdog */
	atomic_io_modify(RSTOUTn_MASK, WDT_RESET_OUT_EN, 0);

	/* Disable watchdog timer */
	atomic_io_modify(wdt_reg + TIMER_CTRL, WDT_EN, 0);
	return 0;
}

static int orion_wdt_enabled(void)
{
	bool enabled, running;

	enabled = readl(RSTOUTn_MASK) & WDT_RESET_OUT_EN;
	running = readl(wdt_reg + TIMER_CTRL) & WDT_EN;

	return enabled && running;
}

static unsigned int orion_wdt_get_timeleft(struct watchdog_device *wdt_dev)
{
	return readl(wdt_reg + WDT_VAL) / wdt_tclk;
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
	.min_timeout = 1,
};

static int orion_wdt_probe(struct platform_device *pdev)
{
	struct resource *res;
	int ret;

	clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(clk)) {
		dev_err(&pdev->dev, "Orion Watchdog missing clock\n");
		return PTR_ERR(clk);
	}
	ret = clk_prepare_enable(clk);
	if (ret)
		return ret;
	wdt_tclk = clk_get_rate(clk);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		ret = -ENODEV;
		goto disable_clk;
	}

	wdt_reg = devm_ioremap(&pdev->dev, res->start, resource_size(res));
	if (!wdt_reg) {
		ret = -ENOMEM;
		goto disable_clk;
	}

	wdt_max_duration = WDT_MAX_CYCLE_COUNT / wdt_tclk;

	orion_wdt.timeout = wdt_max_duration;
	orion_wdt.max_timeout = wdt_max_duration;
	watchdog_init_timeout(&orion_wdt, heartbeat, &pdev->dev);

	/*
	 * Let's make sure the watchdog is fully stopped, unless it's
	 * explicitly enabled. This may be the case if the module was
	 * removed and re-insterted, or if the bootloader explicitly
	 * set a running watchdog before booting the kernel.
	 */
	if (!orion_wdt_enabled())
		orion_wdt_stop(&orion_wdt);

	watchdog_set_nowayout(&orion_wdt, nowayout);
	ret = watchdog_register_device(&orion_wdt);
	if (ret)
		goto disable_clk;

	pr_info("Initial timeout %d sec%s\n",
		orion_wdt.timeout, nowayout ? ", nowayout" : "");
	return 0;

disable_clk:
	clk_disable_unprepare(clk);
	return ret;
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
		.of_match_table = orion_wdt_of_match_table,
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
MODULE_ALIAS("platform:orion_wdt");

// SPDX-License-Identifier: GPL-2.0+
/*
 *  Copyright (C) 2015 Mans Rullgard <mans@mansr.com>
 *  SMP86xx/SMP87xx Watchdog driver
 */

#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/platform_device.h>
#include <linux/watchdog.h>

#define DEFAULT_TIMEOUT 30

static bool nowayout = WATCHDOG_NOWAYOUT;
module_param(nowayout, bool, 0);
MODULE_PARM_DESC(nowayout,
		 "Watchdog cannot be stopped once started (default="
		 __MODULE_STRING(WATCHDOG_NOWAYOUT) ")");

static unsigned int timeout;
module_param(timeout, int, 0);
MODULE_PARM_DESC(timeout, "Watchdog timeout");

/*
 * Counter counts down from programmed value.  Reset asserts when
 * the counter reaches 1.
 */
#define WD_COUNTER		0

#define WD_CONFIG		4
#define WD_CONFIG_XTAL_IN	BIT(0)
#define WD_CONFIG_DISABLE	BIT(31)

struct tangox_wdt_device {
	struct watchdog_device wdt;
	void __iomem *base;
	unsigned long clk_rate;
	struct clk *clk;
};

static int tangox_wdt_set_timeout(struct watchdog_device *wdt,
				  unsigned int new_timeout)
{
	wdt->timeout = new_timeout;

	return 0;
}

static int tangox_wdt_start(struct watchdog_device *wdt)
{
	struct tangox_wdt_device *dev = watchdog_get_drvdata(wdt);
	u32 ticks;

	ticks = 1 + wdt->timeout * dev->clk_rate;
	writel(ticks, dev->base + WD_COUNTER);

	return 0;
}

static int tangox_wdt_stop(struct watchdog_device *wdt)
{
	struct tangox_wdt_device *dev = watchdog_get_drvdata(wdt);

	writel(0, dev->base + WD_COUNTER);

	return 0;
}

static unsigned int tangox_wdt_get_timeleft(struct watchdog_device *wdt)
{
	struct tangox_wdt_device *dev = watchdog_get_drvdata(wdt);
	u32 count;

	count = readl(dev->base + WD_COUNTER);

	if (!count)
		return 0;

	return (count - 1) / dev->clk_rate;
}

static const struct watchdog_info tangox_wdt_info = {
	.options  = WDIOF_SETTIMEOUT | WDIOF_KEEPALIVEPING | WDIOF_MAGICCLOSE,
	.identity = "tangox watchdog",
};

static int tangox_wdt_restart(struct watchdog_device *wdt,
			      unsigned long action, void *data)
{
	struct tangox_wdt_device *dev = watchdog_get_drvdata(wdt);

	writel(1, dev->base + WD_COUNTER);

	return 0;
}

static const struct watchdog_ops tangox_wdt_ops = {
	.start		= tangox_wdt_start,
	.stop		= tangox_wdt_stop,
	.set_timeout	= tangox_wdt_set_timeout,
	.get_timeleft	= tangox_wdt_get_timeleft,
	.restart	= tangox_wdt_restart,
};

static int tangox_wdt_probe(struct platform_device *pdev)
{
	struct tangox_wdt_device *dev;
	struct resource *res;
	u32 config;
	int err;

	dev = devm_kzalloc(&pdev->dev, sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	dev->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(dev->base))
		return PTR_ERR(dev->base);

	dev->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(dev->clk))
		return PTR_ERR(dev->clk);

	err = clk_prepare_enable(dev->clk);
	if (err)
		return err;

	dev->clk_rate = clk_get_rate(dev->clk);
	if (!dev->clk_rate) {
		err = -EINVAL;
		goto err;
	}

	dev->wdt.parent = &pdev->dev;
	dev->wdt.info = &tangox_wdt_info;
	dev->wdt.ops = &tangox_wdt_ops;
	dev->wdt.timeout = DEFAULT_TIMEOUT;
	dev->wdt.min_timeout = 1;
	dev->wdt.max_hw_heartbeat_ms = (U32_MAX - 1) / dev->clk_rate;

	watchdog_init_timeout(&dev->wdt, timeout, &pdev->dev);
	watchdog_set_nowayout(&dev->wdt, nowayout);
	watchdog_set_drvdata(&dev->wdt, dev);

	/*
	 * Deactivate counter if disable bit is set to avoid
	 * accidental reset.
	 */
	config = readl(dev->base + WD_CONFIG);
	if (config & WD_CONFIG_DISABLE)
		writel(0, dev->base + WD_COUNTER);

	writel(WD_CONFIG_XTAL_IN, dev->base + WD_CONFIG);

	/*
	 * Mark as active and restart with configured timeout if
	 * already running.
	 */
	if (readl(dev->base + WD_COUNTER)) {
		set_bit(WDOG_HW_RUNNING, &dev->wdt.status);
		tangox_wdt_start(&dev->wdt);
	}

	watchdog_set_restart_priority(&dev->wdt, 128);

	err = watchdog_register_device(&dev->wdt);
	if (err)
		goto err;

	platform_set_drvdata(pdev, dev);

	dev_info(&pdev->dev, "SMP86xx/SMP87xx watchdog registered\n");

	return 0;

 err:
	clk_disable_unprepare(dev->clk);
	return err;
}

static int tangox_wdt_remove(struct platform_device *pdev)
{
	struct tangox_wdt_device *dev = platform_get_drvdata(pdev);

	tangox_wdt_stop(&dev->wdt);
	clk_disable_unprepare(dev->clk);

	watchdog_unregister_device(&dev->wdt);

	return 0;
}

static const struct of_device_id tangox_wdt_dt_ids[] = {
	{ .compatible = "sigma,smp8642-wdt" },
	{ .compatible = "sigma,smp8759-wdt" },
	{ }
};
MODULE_DEVICE_TABLE(of, tangox_wdt_dt_ids);

static struct platform_driver tangox_wdt_driver = {
	.probe	= tangox_wdt_probe,
	.remove	= tangox_wdt_remove,
	.driver	= {
		.name		= "tangox-wdt",
		.of_match_table	= tangox_wdt_dt_ids,
	},
};

module_platform_driver(tangox_wdt_driver);

MODULE_AUTHOR("Mans Rullgard <mans@mansr.com>");
MODULE_DESCRIPTION("SMP86xx/SMP87xx Watchdog driver");
MODULE_LICENSE("GPL");

/*
 *      sunxi Watchdog Driver
 *
 *      Copyright (c) 2013 Carlo Caione
 *                    2012 Henrik Nordstrom
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 *
 *      Based on xen_wdt.c
 *      (c) Copyright 2010 Novell, Inc.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/notifier.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/reboot.h>
#include <linux/types.h>
#include <linux/watchdog.h>

#define WDT_MAX_TIMEOUT         16
#define WDT_MIN_TIMEOUT         1
#define WDT_TIMEOUT_MASK        0x0F

#define WDT_CTRL_RELOAD         ((1 << 0) | (0x0a57 << 1))

#define WDT_MODE_EN             (1 << 0)

#define DRV_NAME		"sunxi-wdt"
#define DRV_VERSION		"1.0"

static bool nowayout = WATCHDOG_NOWAYOUT;
static unsigned int timeout = WDT_MAX_TIMEOUT;

/*
 * This structure stores the register offsets for different variants
 * of Allwinner's watchdog hardware.
 */
struct sunxi_wdt_reg {
	u8 wdt_ctrl;
	u8 wdt_cfg;
	u8 wdt_mode;
	u8 wdt_timeout_shift;
	u8 wdt_reset_mask;
	u8 wdt_reset_val;
};

struct sunxi_wdt_dev {
	struct watchdog_device wdt_dev;
	void __iomem *wdt_base;
	const struct sunxi_wdt_reg *wdt_regs;
	struct notifier_block restart_handler;
};

/*
 * wdt_timeout_map maps the watchdog timer interval value in seconds to
 * the value of the register WDT_MODE at bits .wdt_timeout_shift ~ +3
 *
 * [timeout seconds] = register value
 *
 */

static const int wdt_timeout_map[] = {
	[1] = 0x1,  /* 1s  */
	[2] = 0x2,  /* 2s  */
	[3] = 0x3,  /* 3s  */
	[4] = 0x4,  /* 4s  */
	[5] = 0x5,  /* 5s  */
	[6] = 0x6,  /* 6s  */
	[8] = 0x7,  /* 8s  */
	[10] = 0x8, /* 10s */
	[12] = 0x9, /* 12s */
	[14] = 0xA, /* 14s */
	[16] = 0xB, /* 16s */
};


static int sunxi_restart_handle(struct notifier_block *this, unsigned long mode,
				void *cmd)
{
	struct sunxi_wdt_dev *sunxi_wdt = container_of(this,
						       struct sunxi_wdt_dev,
						       restart_handler);
	void __iomem *wdt_base = sunxi_wdt->wdt_base;
	const struct sunxi_wdt_reg *regs = sunxi_wdt->wdt_regs;
	u32 val;

	/* Set system reset function */
	val = readl(wdt_base + regs->wdt_cfg);
	val &= ~(regs->wdt_reset_mask);
	val |= regs->wdt_reset_val;
	writel(val, wdt_base + regs->wdt_cfg);

	/* Set lowest timeout and enable watchdog */
	val = readl(wdt_base + regs->wdt_mode);
	val &= ~(WDT_TIMEOUT_MASK << regs->wdt_timeout_shift);
	val |= WDT_MODE_EN;
	writel(val, wdt_base + regs->wdt_mode);

	/*
	 * Restart the watchdog. The default (and lowest) interval
	 * value for the watchdog is 0.5s.
	 */
	writel(WDT_CTRL_RELOAD, wdt_base + regs->wdt_ctrl);

	while (1) {
		mdelay(5);
		val = readl(wdt_base + regs->wdt_mode);
		val |= WDT_MODE_EN;
		writel(val, wdt_base + regs->wdt_mode);
	}
	return NOTIFY_DONE;
}

static int sunxi_wdt_ping(struct watchdog_device *wdt_dev)
{
	struct sunxi_wdt_dev *sunxi_wdt = watchdog_get_drvdata(wdt_dev);
	void __iomem *wdt_base = sunxi_wdt->wdt_base;
	const struct sunxi_wdt_reg *regs = sunxi_wdt->wdt_regs;

	writel(WDT_CTRL_RELOAD, wdt_base + regs->wdt_ctrl);

	return 0;
}

static int sunxi_wdt_set_timeout(struct watchdog_device *wdt_dev,
		unsigned int timeout)
{
	struct sunxi_wdt_dev *sunxi_wdt = watchdog_get_drvdata(wdt_dev);
	void __iomem *wdt_base = sunxi_wdt->wdt_base;
	const struct sunxi_wdt_reg *regs = sunxi_wdt->wdt_regs;
	u32 reg;

	if (wdt_timeout_map[timeout] == 0)
		timeout++;

	sunxi_wdt->wdt_dev.timeout = timeout;

	reg = readl(wdt_base + regs->wdt_mode);
	reg &= ~(WDT_TIMEOUT_MASK << regs->wdt_timeout_shift);
	reg |= wdt_timeout_map[timeout] << regs->wdt_timeout_shift;
	writel(reg, wdt_base + regs->wdt_mode);

	sunxi_wdt_ping(wdt_dev);

	return 0;
}

static int sunxi_wdt_stop(struct watchdog_device *wdt_dev)
{
	struct sunxi_wdt_dev *sunxi_wdt = watchdog_get_drvdata(wdt_dev);
	void __iomem *wdt_base = sunxi_wdt->wdt_base;
	const struct sunxi_wdt_reg *regs = sunxi_wdt->wdt_regs;

	writel(0, wdt_base + regs->wdt_mode);

	return 0;
}

static int sunxi_wdt_start(struct watchdog_device *wdt_dev)
{
	u32 reg;
	struct sunxi_wdt_dev *sunxi_wdt = watchdog_get_drvdata(wdt_dev);
	void __iomem *wdt_base = sunxi_wdt->wdt_base;
	const struct sunxi_wdt_reg *regs = sunxi_wdt->wdt_regs;
	int ret;

	ret = sunxi_wdt_set_timeout(&sunxi_wdt->wdt_dev,
			sunxi_wdt->wdt_dev.timeout);
	if (ret < 0)
		return ret;

	/* Set system reset function */
	reg = readl(wdt_base + regs->wdt_cfg);
	reg &= ~(regs->wdt_reset_mask);
	reg |= ~(regs->wdt_reset_val);
	writel(reg, wdt_base + regs->wdt_cfg);

	/* Enable watchdog */
	reg = readl(wdt_base + regs->wdt_mode);
	reg |= WDT_MODE_EN;
	writel(reg, wdt_base + regs->wdt_mode);

	return 0;
}

static const struct watchdog_info sunxi_wdt_info = {
	.identity	= DRV_NAME,
	.options	= WDIOF_SETTIMEOUT |
			  WDIOF_KEEPALIVEPING |
			  WDIOF_MAGICCLOSE,
};

static const struct watchdog_ops sunxi_wdt_ops = {
	.owner		= THIS_MODULE,
	.start		= sunxi_wdt_start,
	.stop		= sunxi_wdt_stop,
	.ping		= sunxi_wdt_ping,
	.set_timeout	= sunxi_wdt_set_timeout,
};

static const struct sunxi_wdt_reg sun4i_wdt_reg = {
	.wdt_ctrl = 0x00,
	.wdt_cfg = 0x04,
	.wdt_mode = 0x04,
	.wdt_timeout_shift = 3,
	.wdt_reset_mask = 0x02,
	.wdt_reset_val = 0x02,
};

static const struct sunxi_wdt_reg sun6i_wdt_reg = {
	.wdt_ctrl = 0x10,
	.wdt_cfg = 0x14,
	.wdt_mode = 0x18,
	.wdt_timeout_shift = 4,
	.wdt_reset_mask = 0x03,
	.wdt_reset_val = 0x01,
};

static const struct of_device_id sunxi_wdt_dt_ids[] = {
	{ .compatible = "allwinner,sun4i-a10-wdt", .data = &sun4i_wdt_reg },
	{ .compatible = "allwinner,sun6i-a31-wdt", .data = &sun6i_wdt_reg },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, sunxi_wdt_dt_ids);

static int sunxi_wdt_probe(struct platform_device *pdev)
{
	struct sunxi_wdt_dev *sunxi_wdt;
	const struct of_device_id *device;
	struct resource *res;
	int err;

	sunxi_wdt = devm_kzalloc(&pdev->dev, sizeof(*sunxi_wdt), GFP_KERNEL);
	if (!sunxi_wdt)
		return -EINVAL;

	platform_set_drvdata(pdev, sunxi_wdt);

	device = of_match_device(sunxi_wdt_dt_ids, &pdev->dev);
	if (!device)
		return -ENODEV;

	sunxi_wdt->wdt_regs = device->data;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	sunxi_wdt->wdt_base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(sunxi_wdt->wdt_base))
		return PTR_ERR(sunxi_wdt->wdt_base);

	sunxi_wdt->wdt_dev.info = &sunxi_wdt_info;
	sunxi_wdt->wdt_dev.ops = &sunxi_wdt_ops;
	sunxi_wdt->wdt_dev.timeout = WDT_MAX_TIMEOUT;
	sunxi_wdt->wdt_dev.max_timeout = WDT_MAX_TIMEOUT;
	sunxi_wdt->wdt_dev.min_timeout = WDT_MIN_TIMEOUT;
	sunxi_wdt->wdt_dev.parent = &pdev->dev;

	watchdog_init_timeout(&sunxi_wdt->wdt_dev, timeout, &pdev->dev);
	watchdog_set_nowayout(&sunxi_wdt->wdt_dev, nowayout);

	watchdog_set_drvdata(&sunxi_wdt->wdt_dev, sunxi_wdt);

	sunxi_wdt_stop(&sunxi_wdt->wdt_dev);

	err = watchdog_register_device(&sunxi_wdt->wdt_dev);
	if (unlikely(err))
		return err;

	sunxi_wdt->restart_handler.notifier_call = sunxi_restart_handle;
	sunxi_wdt->restart_handler.priority = 128;
	err = register_restart_handler(&sunxi_wdt->restart_handler);
	if (err)
		dev_err(&pdev->dev,
			"cannot register restart handler (err=%d)\n", err);

	dev_info(&pdev->dev, "Watchdog enabled (timeout=%d sec, nowayout=%d)",
			sunxi_wdt->wdt_dev.timeout, nowayout);

	return 0;
}

static int sunxi_wdt_remove(struct platform_device *pdev)
{
	struct sunxi_wdt_dev *sunxi_wdt = platform_get_drvdata(pdev);

	unregister_restart_handler(&sunxi_wdt->restart_handler);

	watchdog_unregister_device(&sunxi_wdt->wdt_dev);
	watchdog_set_drvdata(&sunxi_wdt->wdt_dev, NULL);

	return 0;
}

static void sunxi_wdt_shutdown(struct platform_device *pdev)
{
	struct sunxi_wdt_dev *sunxi_wdt = platform_get_drvdata(pdev);

	sunxi_wdt_stop(&sunxi_wdt->wdt_dev);
}

static struct platform_driver sunxi_wdt_driver = {
	.probe		= sunxi_wdt_probe,
	.remove		= sunxi_wdt_remove,
	.shutdown	= sunxi_wdt_shutdown,
	.driver		= {
		.name		= DRV_NAME,
		.of_match_table	= sunxi_wdt_dt_ids,
	},
};

module_platform_driver(sunxi_wdt_driver);

module_param(timeout, uint, 0);
MODULE_PARM_DESC(timeout, "Watchdog heartbeat in seconds");

module_param(nowayout, bool, 0);
MODULE_PARM_DESC(nowayout, "Watchdog cannot be stopped once started "
		"(default=" __MODULE_STRING(WATCHDOG_NOWAYOUT) ")");

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Carlo Caione <carlo.caione@gmail.com>");
MODULE_AUTHOR("Henrik Nordstrom <henrik@henriknordstrom.net>");
MODULE_DESCRIPTION("sunxi WatchDog Timer Driver");
MODULE_VERSION(DRV_VERSION);

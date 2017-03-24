/*
 * Imagination Technologies PowerDown Controller Watchdog Timer.
 *
 * Copyright (c) 2014 Imagination Technologies Ltd.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * Based on drivers/watchdog/sunxi_wdt.c Copyright (c) 2013 Carlo Caione
 *                                                     2012 Henrik Nordstrom
 *
 * Notes
 * -----
 * The timeout value is rounded to the next power of two clock cycles.
 * This is configured using the PDC_WDT_CONFIG register, according to this
 * formula:
 *
 *     timeout = 2^(delay + 1) clock cycles
 *
 * Where 'delay' is the value written in PDC_WDT_CONFIG register.
 *
 * Therefore, the hardware only allows to program watchdog timeouts, expressed
 * as a power of two number of watchdog clock cycles. The current implementation
 * guarantees that the actual watchdog timeout will be _at least_ the value
 * programmed in the imgpdg_wdt driver.
 *
 * The following table shows how the user-configured timeout relates
 * to the actual hardware timeout (watchdog clock @ 40000 Hz):
 *
 * input timeout | WD_DELAY | actual timeout
 * -----------------------------------
 *      10       |   18     |  13 seconds
 *      20       |   19     |  26 seconds
 *      30       |   20     |  52 seconds
 *      60       |   21     |  104 seconds
 *
 * Albeit coarse, this granularity would suffice most watchdog uses.
 * If the platform allows it, the user should be able to change the watchdog
 * clock rate and achieve a finer timeout granularity.
 */

#include <linux/clk.h>
#include <linux/io.h>
#include <linux/log2.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/watchdog.h>

/* registers */
#define PDC_WDT_SOFT_RESET		0x00
#define PDC_WDT_CONFIG			0x04
  #define PDC_WDT_CONFIG_ENABLE		BIT(31)
  #define PDC_WDT_CONFIG_DELAY_MASK	0x1f

#define PDC_WDT_TICKLE1			0x08
#define PDC_WDT_TICKLE1_MAGIC		0xabcd1234
#define PDC_WDT_TICKLE2			0x0c
#define PDC_WDT_TICKLE2_MAGIC		0x4321dcba

#define PDC_WDT_TICKLE_STATUS_MASK	0x7
#define PDC_WDT_TICKLE_STATUS_SHIFT	0
#define PDC_WDT_TICKLE_STATUS_HRESET	0x0  /* Hard reset */
#define PDC_WDT_TICKLE_STATUS_TIMEOUT	0x1  /* Timeout */
#define PDC_WDT_TICKLE_STATUS_TICKLE	0x2  /* Tickled incorrectly */
#define PDC_WDT_TICKLE_STATUS_SRESET	0x3  /* Soft reset */
#define PDC_WDT_TICKLE_STATUS_USER	0x4  /* User reset */

/* Timeout values are in seconds */
#define PDC_WDT_MIN_TIMEOUT		1
#define PDC_WDT_DEF_TIMEOUT		64

static int heartbeat;
module_param(heartbeat, int, 0);
MODULE_PARM_DESC(heartbeat, "Watchdog heartbeats in seconds "
	"(default=" __MODULE_STRING(PDC_WDT_DEF_TIMEOUT) ")");

static bool nowayout = WATCHDOG_NOWAYOUT;
module_param(nowayout, bool, 0);
MODULE_PARM_DESC(nowayout, "Watchdog cannot be stopped once started "
	"(default=" __MODULE_STRING(WATCHDOG_NOWAYOUT) ")");

struct pdc_wdt_dev {
	struct watchdog_device wdt_dev;
	struct clk *wdt_clk;
	struct clk *sys_clk;
	void __iomem *base;
};

static int pdc_wdt_keepalive(struct watchdog_device *wdt_dev)
{
	struct pdc_wdt_dev *wdt = watchdog_get_drvdata(wdt_dev);

	writel(PDC_WDT_TICKLE1_MAGIC, wdt->base + PDC_WDT_TICKLE1);
	writel(PDC_WDT_TICKLE2_MAGIC, wdt->base + PDC_WDT_TICKLE2);

	return 0;
}

static int pdc_wdt_stop(struct watchdog_device *wdt_dev)
{
	unsigned int val;
	struct pdc_wdt_dev *wdt = watchdog_get_drvdata(wdt_dev);

	val = readl(wdt->base + PDC_WDT_CONFIG);
	val &= ~PDC_WDT_CONFIG_ENABLE;
	writel(val, wdt->base + PDC_WDT_CONFIG);

	/* Must tickle to finish the stop */
	pdc_wdt_keepalive(wdt_dev);

	return 0;
}

static void __pdc_wdt_set_timeout(struct pdc_wdt_dev *wdt)
{
	unsigned long clk_rate = clk_get_rate(wdt->wdt_clk);
	unsigned int val;

	val = readl(wdt->base + PDC_WDT_CONFIG) & ~PDC_WDT_CONFIG_DELAY_MASK;
	val |= order_base_2(wdt->wdt_dev.timeout * clk_rate) - 1;
	writel(val, wdt->base + PDC_WDT_CONFIG);
}

static int pdc_wdt_set_timeout(struct watchdog_device *wdt_dev,
			       unsigned int new_timeout)
{
	struct pdc_wdt_dev *wdt = watchdog_get_drvdata(wdt_dev);

	wdt->wdt_dev.timeout = new_timeout;

	__pdc_wdt_set_timeout(wdt);

	return 0;
}

/* Start the watchdog timer (delay should already be set) */
static int pdc_wdt_start(struct watchdog_device *wdt_dev)
{
	unsigned int val;
	struct pdc_wdt_dev *wdt = watchdog_get_drvdata(wdt_dev);

	__pdc_wdt_set_timeout(wdt);

	val = readl(wdt->base + PDC_WDT_CONFIG);
	val |= PDC_WDT_CONFIG_ENABLE;
	writel(val, wdt->base + PDC_WDT_CONFIG);

	return 0;
}

static int pdc_wdt_restart(struct watchdog_device *wdt_dev,
			   unsigned long action, void *data)
{
	struct pdc_wdt_dev *wdt = watchdog_get_drvdata(wdt_dev);

	/* Assert SOFT_RESET */
	writel(0x1, wdt->base + PDC_WDT_SOFT_RESET);

	return 0;
}

static const struct watchdog_info pdc_wdt_info = {
	.identity	= "IMG PDC Watchdog",
	.options	= WDIOF_SETTIMEOUT |
			  WDIOF_KEEPALIVEPING |
			  WDIOF_MAGICCLOSE,
};

static const struct watchdog_ops pdc_wdt_ops = {
	.owner		= THIS_MODULE,
	.start		= pdc_wdt_start,
	.stop		= pdc_wdt_stop,
	.ping		= pdc_wdt_keepalive,
	.set_timeout	= pdc_wdt_set_timeout,
	.restart        = pdc_wdt_restart,
};

static int pdc_wdt_probe(struct platform_device *pdev)
{
	u64 div;
	int ret, val;
	unsigned long clk_rate;
	struct resource *res;
	struct pdc_wdt_dev *pdc_wdt;

	pdc_wdt = devm_kzalloc(&pdev->dev, sizeof(*pdc_wdt), GFP_KERNEL);
	if (!pdc_wdt)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	pdc_wdt->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(pdc_wdt->base))
		return PTR_ERR(pdc_wdt->base);

	pdc_wdt->sys_clk = devm_clk_get(&pdev->dev, "sys");
	if (IS_ERR(pdc_wdt->sys_clk)) {
		dev_err(&pdev->dev, "failed to get the sys clock\n");
		return PTR_ERR(pdc_wdt->sys_clk);
	}

	pdc_wdt->wdt_clk = devm_clk_get(&pdev->dev, "wdt");
	if (IS_ERR(pdc_wdt->wdt_clk)) {
		dev_err(&pdev->dev, "failed to get the wdt clock\n");
		return PTR_ERR(pdc_wdt->wdt_clk);
	}

	ret = clk_prepare_enable(pdc_wdt->sys_clk);
	if (ret) {
		dev_err(&pdev->dev, "could not prepare or enable sys clock\n");
		return ret;
	}

	ret = clk_prepare_enable(pdc_wdt->wdt_clk);
	if (ret) {
		dev_err(&pdev->dev, "could not prepare or enable wdt clock\n");
		goto disable_sys_clk;
	}

	/* We use the clock rate to calculate the max timeout */
	clk_rate = clk_get_rate(pdc_wdt->wdt_clk);
	if (clk_rate == 0) {
		dev_err(&pdev->dev, "failed to get clock rate\n");
		ret = -EINVAL;
		goto disable_wdt_clk;
	}

	if (order_base_2(clk_rate) > PDC_WDT_CONFIG_DELAY_MASK + 1) {
		dev_err(&pdev->dev, "invalid clock rate\n");
		ret = -EINVAL;
		goto disable_wdt_clk;
	}

	if (order_base_2(clk_rate) == 0)
		pdc_wdt->wdt_dev.min_timeout = PDC_WDT_MIN_TIMEOUT + 1;
	else
		pdc_wdt->wdt_dev.min_timeout = PDC_WDT_MIN_TIMEOUT;

	pdc_wdt->wdt_dev.info = &pdc_wdt_info;
	pdc_wdt->wdt_dev.ops = &pdc_wdt_ops;

	div = 1ULL << (PDC_WDT_CONFIG_DELAY_MASK + 1);
	do_div(div, clk_rate);
	pdc_wdt->wdt_dev.max_timeout = div;
	pdc_wdt->wdt_dev.timeout = PDC_WDT_DEF_TIMEOUT;
	pdc_wdt->wdt_dev.parent = &pdev->dev;
	watchdog_set_drvdata(&pdc_wdt->wdt_dev, pdc_wdt);

	watchdog_init_timeout(&pdc_wdt->wdt_dev, heartbeat, &pdev->dev);

	pdc_wdt_stop(&pdc_wdt->wdt_dev);

	/* Find what caused the last reset */
	val = readl(pdc_wdt->base + PDC_WDT_TICKLE1);
	val = (val & PDC_WDT_TICKLE_STATUS_MASK) >> PDC_WDT_TICKLE_STATUS_SHIFT;
	switch (val) {
	case PDC_WDT_TICKLE_STATUS_TICKLE:
	case PDC_WDT_TICKLE_STATUS_TIMEOUT:
		pdc_wdt->wdt_dev.bootstatus |= WDIOF_CARDRESET;
		dev_info(&pdev->dev,
			 "watchdog module last reset due to timeout\n");
		break;
	case PDC_WDT_TICKLE_STATUS_HRESET:
		dev_info(&pdev->dev,
			 "watchdog module last reset due to hard reset\n");
		break;
	case PDC_WDT_TICKLE_STATUS_SRESET:
		dev_info(&pdev->dev,
			 "watchdog module last reset due to soft reset\n");
		break;
	case PDC_WDT_TICKLE_STATUS_USER:
		dev_info(&pdev->dev,
			 "watchdog module last reset due to user reset\n");
		break;
	default:
		dev_info(&pdev->dev,
			 "contains an illegal status code (%08x)\n", val);
		break;
	}

	watchdog_set_nowayout(&pdc_wdt->wdt_dev, nowayout);
	watchdog_set_restart_priority(&pdc_wdt->wdt_dev, 128);

	platform_set_drvdata(pdev, pdc_wdt);

	ret = watchdog_register_device(&pdc_wdt->wdt_dev);
	if (ret)
		goto disable_wdt_clk;

	return 0;

disable_wdt_clk:
	clk_disable_unprepare(pdc_wdt->wdt_clk);
disable_sys_clk:
	clk_disable_unprepare(pdc_wdt->sys_clk);
	return ret;
}

static void pdc_wdt_shutdown(struct platform_device *pdev)
{
	struct pdc_wdt_dev *pdc_wdt = platform_get_drvdata(pdev);

	pdc_wdt_stop(&pdc_wdt->wdt_dev);
}

static int pdc_wdt_remove(struct platform_device *pdev)
{
	struct pdc_wdt_dev *pdc_wdt = platform_get_drvdata(pdev);

	pdc_wdt_stop(&pdc_wdt->wdt_dev);
	watchdog_unregister_device(&pdc_wdt->wdt_dev);
	clk_disable_unprepare(pdc_wdt->wdt_clk);
	clk_disable_unprepare(pdc_wdt->sys_clk);

	return 0;
}

static const struct of_device_id pdc_wdt_match[] = {
	{ .compatible = "img,pdc-wdt" },
	{}
};
MODULE_DEVICE_TABLE(of, pdc_wdt_match);

static struct platform_driver pdc_wdt_driver = {
	.driver = {
		.name = "imgpdc-wdt",
		.of_match_table	= pdc_wdt_match,
	},
	.probe = pdc_wdt_probe,
	.remove = pdc_wdt_remove,
	.shutdown = pdc_wdt_shutdown,
};
module_platform_driver(pdc_wdt_driver);

MODULE_AUTHOR("Jude Abraham <Jude.Abraham@imgtec.com>");
MODULE_AUTHOR("Naidu Tellapati <Naidu.Tellapati@imgtec.com>");
MODULE_DESCRIPTION("Imagination Technologies PDC Watchdog Timer Driver");
MODULE_LICENSE("GPL v2");

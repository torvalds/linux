/*
 * Spreadtrum watchdog driver
 * Copyright (C) 2017 Spreadtrum - http://www.spreadtrum.com
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */

#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/watchdog.h>

#define SPRD_WDT_LOAD_LOW		0x0
#define SPRD_WDT_LOAD_HIGH		0x4
#define SPRD_WDT_CTRL			0x8
#define SPRD_WDT_INT_CLR		0xc
#define SPRD_WDT_INT_RAW		0x10
#define SPRD_WDT_INT_MSK		0x14
#define SPRD_WDT_CNT_LOW		0x18
#define SPRD_WDT_CNT_HIGH		0x1c
#define SPRD_WDT_LOCK			0x20
#define SPRD_WDT_IRQ_LOAD_LOW		0x2c
#define SPRD_WDT_IRQ_LOAD_HIGH		0x30

/* WDT_CTRL */
#define SPRD_WDT_INT_EN_BIT		BIT(0)
#define SPRD_WDT_CNT_EN_BIT		BIT(1)
#define SPRD_WDT_NEW_VER_EN		BIT(2)
#define SPRD_WDT_RST_EN_BIT		BIT(3)

/* WDT_INT_CLR */
#define SPRD_WDT_INT_CLEAR_BIT		BIT(0)
#define SPRD_WDT_RST_CLEAR_BIT		BIT(3)

/* WDT_INT_RAW */
#define SPRD_WDT_INT_RAW_BIT		BIT(0)
#define SPRD_WDT_RST_RAW_BIT		BIT(3)
#define SPRD_WDT_LD_BUSY_BIT		BIT(4)

/* 1s equal to 32768 counter steps */
#define SPRD_WDT_CNT_STEP		32768

#define SPRD_WDT_UNLOCK_KEY		0xe551
#define SPRD_WDT_MIN_TIMEOUT		3
#define SPRD_WDT_MAX_TIMEOUT		60

#define SPRD_WDT_CNT_HIGH_SHIFT		16
#define SPRD_WDT_LOW_VALUE_MASK		GENMASK(15, 0)
#define SPRD_WDT_LOAD_TIMEOUT		1000

struct sprd_wdt {
	void __iomem *base;
	struct watchdog_device wdd;
	struct clk *enable;
	struct clk *rtc_enable;
	int irq;
};

static inline struct sprd_wdt *to_sprd_wdt(struct watchdog_device *wdd)
{
	return container_of(wdd, struct sprd_wdt, wdd);
}

static inline void sprd_wdt_lock(void __iomem *addr)
{
	writel_relaxed(0x0, addr + SPRD_WDT_LOCK);
}

static inline void sprd_wdt_unlock(void __iomem *addr)
{
	writel_relaxed(SPRD_WDT_UNLOCK_KEY, addr + SPRD_WDT_LOCK);
}

static irqreturn_t sprd_wdt_isr(int irq, void *dev_id)
{
	struct sprd_wdt *wdt = (struct sprd_wdt *)dev_id;

	sprd_wdt_unlock(wdt->base);
	writel_relaxed(SPRD_WDT_INT_CLEAR_BIT, wdt->base + SPRD_WDT_INT_CLR);
	sprd_wdt_lock(wdt->base);
	watchdog_notify_pretimeout(&wdt->wdd);
	return IRQ_HANDLED;
}

static u32 sprd_wdt_get_cnt_value(struct sprd_wdt *wdt)
{
	u32 val;

	val = readl_relaxed(wdt->base + SPRD_WDT_CNT_HIGH) <<
		SPRD_WDT_CNT_HIGH_SHIFT;
	val |= readl_relaxed(wdt->base + SPRD_WDT_CNT_LOW) &
		SPRD_WDT_LOW_VALUE_MASK;

	return val;
}

static int sprd_wdt_load_value(struct sprd_wdt *wdt, u32 timeout,
			       u32 pretimeout)
{
	u32 val, delay_cnt = 0;
	u32 tmr_step = timeout * SPRD_WDT_CNT_STEP;
	u32 prtmr_step = pretimeout * SPRD_WDT_CNT_STEP;

	sprd_wdt_unlock(wdt->base);
	writel_relaxed((tmr_step >> SPRD_WDT_CNT_HIGH_SHIFT) &
		      SPRD_WDT_LOW_VALUE_MASK, wdt->base + SPRD_WDT_LOAD_HIGH);
	writel_relaxed((tmr_step & SPRD_WDT_LOW_VALUE_MASK),
		       wdt->base + SPRD_WDT_LOAD_LOW);
	writel_relaxed((prtmr_step >> SPRD_WDT_CNT_HIGH_SHIFT) &
			SPRD_WDT_LOW_VALUE_MASK,
		       wdt->base + SPRD_WDT_IRQ_LOAD_HIGH);
	writel_relaxed(prtmr_step & SPRD_WDT_LOW_VALUE_MASK,
		       wdt->base + SPRD_WDT_IRQ_LOAD_LOW);
	sprd_wdt_lock(wdt->base);

	/*
	 * Waiting the load value operation done,
	 * it needs two or three RTC clock cycles.
	 */
	do {
		val = readl_relaxed(wdt->base + SPRD_WDT_INT_RAW);
		if (!(val & SPRD_WDT_LD_BUSY_BIT))
			break;

		cpu_relax();
	} while (delay_cnt++ < SPRD_WDT_LOAD_TIMEOUT);

	if (delay_cnt >= SPRD_WDT_LOAD_TIMEOUT)
		return -EBUSY;
	return 0;
}

static int sprd_wdt_enable(struct sprd_wdt *wdt)
{
	u32 val;
	int ret;

	ret = clk_prepare_enable(wdt->enable);
	if (ret)
		return ret;
	ret = clk_prepare_enable(wdt->rtc_enable);
	if (ret) {
		clk_disable_unprepare(wdt->enable);
		return ret;
	}

	sprd_wdt_unlock(wdt->base);
	val = readl_relaxed(wdt->base + SPRD_WDT_CTRL);
	val |= SPRD_WDT_NEW_VER_EN;
	writel_relaxed(val, wdt->base + SPRD_WDT_CTRL);
	sprd_wdt_lock(wdt->base);
	return 0;
}

static void sprd_wdt_disable(void *_data)
{
	struct sprd_wdt *wdt = _data;

	sprd_wdt_unlock(wdt->base);
	writel_relaxed(0x0, wdt->base + SPRD_WDT_CTRL);
	sprd_wdt_lock(wdt->base);

	clk_disable_unprepare(wdt->rtc_enable);
	clk_disable_unprepare(wdt->enable);
}

static int sprd_wdt_start(struct watchdog_device *wdd)
{
	struct sprd_wdt *wdt = to_sprd_wdt(wdd);
	u32 val;
	int ret;

	ret = sprd_wdt_load_value(wdt, wdd->timeout, wdd->pretimeout);
	if (ret)
		return ret;

	sprd_wdt_unlock(wdt->base);
	val = readl_relaxed(wdt->base + SPRD_WDT_CTRL);
	val |= SPRD_WDT_CNT_EN_BIT | SPRD_WDT_INT_EN_BIT | SPRD_WDT_RST_EN_BIT;
	writel_relaxed(val, wdt->base + SPRD_WDT_CTRL);
	sprd_wdt_lock(wdt->base);
	set_bit(WDOG_HW_RUNNING, &wdd->status);

	return 0;
}

static int sprd_wdt_stop(struct watchdog_device *wdd)
{
	struct sprd_wdt *wdt = to_sprd_wdt(wdd);
	u32 val;

	sprd_wdt_unlock(wdt->base);
	val = readl_relaxed(wdt->base + SPRD_WDT_CTRL);
	val &= ~(SPRD_WDT_CNT_EN_BIT | SPRD_WDT_RST_EN_BIT |
		SPRD_WDT_INT_EN_BIT);
	writel_relaxed(val, wdt->base + SPRD_WDT_CTRL);
	sprd_wdt_lock(wdt->base);
	return 0;
}

static int sprd_wdt_set_timeout(struct watchdog_device *wdd,
				u32 timeout)
{
	struct sprd_wdt *wdt = to_sprd_wdt(wdd);

	if (timeout == wdd->timeout)
		return 0;

	wdd->timeout = timeout;

	return sprd_wdt_load_value(wdt, timeout, wdd->pretimeout);
}

static int sprd_wdt_set_pretimeout(struct watchdog_device *wdd,
				   u32 new_pretimeout)
{
	struct sprd_wdt *wdt = to_sprd_wdt(wdd);

	if (new_pretimeout < wdd->min_timeout)
		return -EINVAL;

	wdd->pretimeout = new_pretimeout;

	return sprd_wdt_load_value(wdt, wdd->timeout, new_pretimeout);
}

static u32 sprd_wdt_get_timeleft(struct watchdog_device *wdd)
{
	struct sprd_wdt *wdt = to_sprd_wdt(wdd);
	u32 val;

	val = sprd_wdt_get_cnt_value(wdt);
	val = val / SPRD_WDT_CNT_STEP;

	return val;
}

static const struct watchdog_ops sprd_wdt_ops = {
	.owner = THIS_MODULE,
	.start = sprd_wdt_start,
	.stop = sprd_wdt_stop,
	.set_timeout = sprd_wdt_set_timeout,
	.set_pretimeout = sprd_wdt_set_pretimeout,
	.get_timeleft = sprd_wdt_get_timeleft,
};

static const struct watchdog_info sprd_wdt_info = {
	.options = WDIOF_SETTIMEOUT |
		   WDIOF_PRETIMEOUT |
		   WDIOF_MAGICCLOSE |
		   WDIOF_KEEPALIVEPING,
	.identity = "Spreadtrum Watchdog Timer",
};

static int sprd_wdt_probe(struct platform_device *pdev)
{
	struct resource *wdt_res;
	struct sprd_wdt *wdt;
	int ret;

	wdt = devm_kzalloc(&pdev->dev, sizeof(*wdt), GFP_KERNEL);
	if (!wdt)
		return -ENOMEM;

	wdt_res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	wdt->base = devm_ioremap_resource(&pdev->dev, wdt_res);
	if (IS_ERR(wdt->base))
		return PTR_ERR(wdt->base);

	wdt->enable = devm_clk_get(&pdev->dev, "enable");
	if (IS_ERR(wdt->enable)) {
		dev_err(&pdev->dev, "can't get the enable clock\n");
		return PTR_ERR(wdt->enable);
	}

	wdt->rtc_enable = devm_clk_get(&pdev->dev, "rtc_enable");
	if (IS_ERR(wdt->rtc_enable)) {
		dev_err(&pdev->dev, "can't get the rtc enable clock\n");
		return PTR_ERR(wdt->rtc_enable);
	}

	wdt->irq = platform_get_irq(pdev, 0);
	if (wdt->irq < 0) {
		dev_err(&pdev->dev, "failed to get IRQ resource\n");
		return wdt->irq;
	}

	ret = devm_request_irq(&pdev->dev, wdt->irq, sprd_wdt_isr,
			       IRQF_NO_SUSPEND, "sprd-wdt", (void *)wdt);
	if (ret) {
		dev_err(&pdev->dev, "failed to register irq\n");
		return ret;
	}

	wdt->wdd.info = &sprd_wdt_info;
	wdt->wdd.ops = &sprd_wdt_ops;
	wdt->wdd.parent = &pdev->dev;
	wdt->wdd.min_timeout = SPRD_WDT_MIN_TIMEOUT;
	wdt->wdd.max_timeout = SPRD_WDT_MAX_TIMEOUT;
	wdt->wdd.timeout = SPRD_WDT_MAX_TIMEOUT;

	ret = sprd_wdt_enable(wdt);
	if (ret) {
		dev_err(&pdev->dev, "failed to enable wdt\n");
		return ret;
	}
	ret = devm_add_action(&pdev->dev, sprd_wdt_disable, wdt);
	if (ret) {
		sprd_wdt_disable(wdt);
		dev_err(&pdev->dev, "Failed to add wdt disable action\n");
		return ret;
	}

	watchdog_set_nowayout(&wdt->wdd, WATCHDOG_NOWAYOUT);
	watchdog_init_timeout(&wdt->wdd, 0, &pdev->dev);

	ret = devm_watchdog_register_device(&pdev->dev, &wdt->wdd);
	if (ret) {
		sprd_wdt_disable(wdt);
		dev_err(&pdev->dev, "failed to register watchdog\n");
		return ret;
	}
	platform_set_drvdata(pdev, wdt);

	return 0;
}

static int __maybe_unused sprd_wdt_pm_suspend(struct device *dev)
{
	struct watchdog_device *wdd = dev_get_drvdata(dev);
	struct sprd_wdt *wdt = dev_get_drvdata(dev);

	if (watchdog_active(wdd))
		sprd_wdt_stop(&wdt->wdd);
	sprd_wdt_disable(wdt);

	return 0;
}

static int __maybe_unused sprd_wdt_pm_resume(struct device *dev)
{
	struct watchdog_device *wdd = dev_get_drvdata(dev);
	struct sprd_wdt *wdt = dev_get_drvdata(dev);
	int ret;

	ret = sprd_wdt_enable(wdt);
	if (ret)
		return ret;

	if (watchdog_active(wdd)) {
		ret = sprd_wdt_start(&wdt->wdd);
		if (ret) {
			sprd_wdt_disable(wdt);
			return ret;
		}
	}

	return 0;
}

static const struct dev_pm_ops sprd_wdt_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(sprd_wdt_pm_suspend,
				sprd_wdt_pm_resume)
};

static const struct of_device_id sprd_wdt_match_table[] = {
	{ .compatible = "sprd,sp9860-wdt", },
	{},
};
MODULE_DEVICE_TABLE(of, sprd_wdt_match_table);

static struct platform_driver sprd_watchdog_driver = {
	.probe	= sprd_wdt_probe,
	.driver	= {
		.name = "sprd-wdt",
		.of_match_table = sprd_wdt_match_table,
		.pm = &sprd_wdt_pm_ops,
	},
};
module_platform_driver(sprd_watchdog_driver);

MODULE_AUTHOR("Eric Long <eric.long@spreadtrum.com>");
MODULE_DESCRIPTION("Spreadtrum Watchdog Timer Controller Driver");
MODULE_LICENSE("GPL v2");

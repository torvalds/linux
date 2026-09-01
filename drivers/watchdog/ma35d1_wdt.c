// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2026 Nuvoton technology corporation.
 *
 * Author: Zi-Yu Chen <zychennvt@gmail.com>
 */

#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_wakeirq.h>
#include <linux/property.h>
#include <linux/types.h>
#include <linux/watchdog.h>

#define DRV_NAME		"ma35d1-wdt"

#define REG_WDT_CTL		0x00
#define REG_WDT_RSTCNT	0x08

#define TOUTSEL			GENMASK(11, 8)
#define WDTEN			BIT(7)
#define INTEN			BIT(6)
#define WKF				BIT(5)
#define WKEN			BIT(4)
#define IF				BIT(3)
#define RSTF			BIT(2)
#define RSTEN			BIT(1)
#define SYNC			BIT(30)

#define WDT_DEFAULT_TIMEOUT		32
#define RESET_COUNTER			0x00005AA5

static bool nowayout = WATCHDOG_NOWAYOUT;
static unsigned int timeout;

struct ma35d1_wdt_dev {
	struct watchdog_device wdt_dev;
	spinlock_t lock;
	void __iomem *wdt_base;
	struct clk *clk;
	unsigned long clk_rate;
	int irq;
};

static int ma35d1_wdt_wait_sync(struct ma35d1_wdt_dev *ma35d1_wdt)
{
	unsigned int val;

	return readl_relaxed_poll_timeout_atomic(ma35d1_wdt->wdt_base +
							 REG_WDT_CTL, val, !(val & SYNC), 10, 125);
}

/**
 * ma35d1_wdt_stop - Stop the watchdog.
 *
 * @wdt_dev: watchdog device
 *
 * Read the contents of the CTL register, clear the WDTEN bit
 * in the register and set the access key for successful write.
 *
 * Return: 0 on success, negative error otherwise.
 */
static int ma35d1_wdt_stop(struct watchdog_device *wdt_dev)
{
	struct ma35d1_wdt_dev *ma35d1_wdt = watchdog_get_drvdata(wdt_dev);
	unsigned int val;
	int ret;

	guard(spinlock_irqsave)(&ma35d1_wdt->lock);
	val = readl_relaxed(ma35d1_wdt->wdt_base + REG_WDT_CTL);
	val &= ~WDTEN;
	writel_relaxed(val, ma35d1_wdt->wdt_base + REG_WDT_CTL);
	ret = ma35d1_wdt_wait_sync(ma35d1_wdt);
	if (ret) {
		dev_err(wdt_dev->parent, "Wait for WDTEN SYNC timeout!\n");
		return ret;
	}
	return 0;
}

static int ma35d1_wdt_ping(struct watchdog_device *wdt_dev)
{
	struct ma35d1_wdt_dev *ma35d1_wdt = watchdog_get_drvdata(wdt_dev);

	writel_relaxed(RESET_COUNTER, ma35d1_wdt->wdt_base + REG_WDT_RSTCNT);

	return 0;
}

static int ma35d1_wdt_set_timeout(struct watchdog_device *wdt_dev,
				  unsigned int timeout)
{
	struct ma35d1_wdt_dev *ma35d1_wdt = watchdog_get_drvdata(wdt_dev);
	unsigned long target_ticks;
	unsigned int val, i;
	static const uint8_t toutsel_shifts[] = { 4,  6,  8,  10, 12,
						  14, 16, 18, 20 };

	if (timeout < (ma35d1_wdt->wdt_dev.max_hw_heartbeat_ms / 1000)) {
		target_ticks = (unsigned long)timeout * ma35d1_wdt->clk_rate;

		for (i = 0; i < ARRAY_SIZE(toutsel_shifts); i++) {
			if ((1UL << toutsel_shifts[i]) >= target_ticks)
				break;
		}
		/* To avoid truncation errors (0 seconds) during division. */
		wdt_dev->timeout =
			(1UL << toutsel_shifts[i]) / ma35d1_wdt->clk_rate;
		if (wdt_dev->timeout == 0)
			wdt_dev->timeout = 1;

	} else {
		i = ARRAY_SIZE(toutsel_shifts) - 1;
		wdt_dev->timeout = timeout;
	}

	guard(spinlock_irqsave)(&ma35d1_wdt->lock);
	val = readl_relaxed(ma35d1_wdt->wdt_base + REG_WDT_CTL);
	val &= ~TOUTSEL;
	val |= FIELD_PREP(TOUTSEL, i);
	writel_relaxed(val, ma35d1_wdt->wdt_base + REG_WDT_CTL);

	ma35d1_wdt_ping(wdt_dev);
	return 0;
}

static int ma35d1_wdt_start(struct watchdog_device *wdt_dev)
{
	struct ma35d1_wdt_dev *ma35d1_wdt = watchdog_get_drvdata(wdt_dev);
	void __iomem *wdt_base = ma35d1_wdt->wdt_base;
	unsigned int val;
	int ret;

	ret = ma35d1_wdt_set_timeout(wdt_dev, wdt_dev->timeout);
	if (ret < 0)
		return ret;

	guard(spinlock_irqsave)(&ma35d1_wdt->lock);
	val = readl_relaxed(wdt_base + REG_WDT_CTL);
	val |= (WDTEN | RSTEN);

	writel_relaxed(val, wdt_base + REG_WDT_CTL);
	ret = ma35d1_wdt_wait_sync(ma35d1_wdt);
	if (ret) {
		dev_err(wdt_dev->parent, "Wait for WDTEN SYNC timeout!\n");
		return ret;
	}

	writel_relaxed(RESET_COUNTER, wdt_base + REG_WDT_RSTCNT);

	return 0;
}

static const struct watchdog_info ma35d1_wdt_info = {
	.identity = DRV_NAME,
	.options = WDIOF_SETTIMEOUT | WDIOF_KEEPALIVEPING | WDIOF_MAGICCLOSE |
		   WDIOF_CARDRESET,
};

static const struct watchdog_ops ma35d1_wdt_ops = {
	.owner = THIS_MODULE,
	.start = ma35d1_wdt_start,
	.stop = ma35d1_wdt_stop,
	.ping = ma35d1_wdt_ping,
	.set_timeout = ma35d1_wdt_set_timeout,
};

static irqreturn_t ma35d1_wdt_isr(int irq, void *dev_id)
{
	struct ma35d1_wdt_dev *ma35d1_wdt = dev_id;
	unsigned int val;

	/* Clear the flag if set */
	guard(spinlock)(&ma35d1_wdt->lock);
	val = readl_relaxed(ma35d1_wdt->wdt_base + REG_WDT_CTL);
	writel_relaxed(val, ma35d1_wdt->wdt_base + REG_WDT_CTL);

	return IRQ_HANDLED;
}

static int ma35d1_wdt_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct ma35d1_wdt_dev *ma35d1_wdt;
	unsigned long clk_rate, val;
	int ret;

	ma35d1_wdt = devm_kzalloc(dev, sizeof(*ma35d1_wdt), GFP_KERNEL);
	if (!ma35d1_wdt)
		return -ENOMEM;

	spin_lock_init(&ma35d1_wdt->lock);
	platform_set_drvdata(pdev, ma35d1_wdt);

	ma35d1_wdt->wdt_base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(ma35d1_wdt->wdt_base))
		return PTR_ERR(ma35d1_wdt->wdt_base);

	ma35d1_wdt->clk = devm_clk_get_enabled(dev, NULL);
	if (IS_ERR(ma35d1_wdt->clk))
		return dev_err_probe(dev, PTR_ERR(ma35d1_wdt->clk),
				     "Can't get Watchdog clock\n");

	clk_rate = clk_get_rate(ma35d1_wdt->clk);
	if (!clk_rate)
		return -EINVAL;
	ma35d1_wdt->clk_rate = clk_rate;

	ma35d1_wdt->irq = platform_get_irq(pdev, 0);
	if (ma35d1_wdt->irq < 0)
		return dev_err_probe(dev, ma35d1_wdt->irq,
				     "failed to get irq\n");

	ma35d1_wdt->wdt_dev.info = &ma35d1_wdt_info;
	ma35d1_wdt->wdt_dev.ops = &ma35d1_wdt_ops;
	ma35d1_wdt->wdt_dev.timeout = WDT_DEFAULT_TIMEOUT;
	ma35d1_wdt->wdt_dev.min_timeout = 1;
	ma35d1_wdt->wdt_dev.max_hw_heartbeat_ms = (1U << 20) * 1000 / clk_rate;
	ma35d1_wdt->wdt_dev.parent = dev;

	val = readl_relaxed(ma35d1_wdt->wdt_base + REG_WDT_CTL);
	if (val & RSTF) {
		ma35d1_wdt->wdt_dev.bootstatus = WDIOF_CARDRESET;
		writel_relaxed(val, ma35d1_wdt->wdt_base + REG_WDT_CTL);
	}

	if (val & WDTEN)
		set_bit(WDOG_HW_RUNNING, &ma35d1_wdt->wdt_dev.status);

	watchdog_set_drvdata(&ma35d1_wdt->wdt_dev, ma35d1_wdt);
	watchdog_set_nowayout(&ma35d1_wdt->wdt_dev, nowayout);
	watchdog_init_timeout(&ma35d1_wdt->wdt_dev, timeout, &pdev->dev);

	ma35d1_wdt_set_timeout(&ma35d1_wdt->wdt_dev,
			       ma35d1_wdt->wdt_dev.timeout);

	ret = devm_request_irq(dev, ma35d1_wdt->irq, ma35d1_wdt_isr, 0,
			       dev_name(dev), ma35d1_wdt);
	if (ret)
		return dev_err_probe(dev, ret, "cannot claim IRQ %d\n",
				     ma35d1_wdt->irq);

	if (device_property_read_bool(dev, "wakeup-source")) {
		ret = devm_device_init_wakeup(dev);
		if (ret)
			return ret;

		ret = devm_pm_set_wake_irq(dev, ma35d1_wdt->irq);
		if (ret)
			return ret;
	}

	watchdog_stop_on_reboot(&ma35d1_wdt->wdt_dev);
	watchdog_stop_on_unregister(&ma35d1_wdt->wdt_dev);
	ret = devm_watchdog_register_device(dev, &ma35d1_wdt->wdt_dev);
	if (ret)
		return ret;

	return 0;
}

static int ma35d1_wdt_suspend(struct device *dev)
{
	struct ma35d1_wdt_dev *ma35d1_wdt = dev_get_drvdata(dev);

	if (watchdog_active(&ma35d1_wdt->wdt_dev) ||
	    watchdog_hw_running(&ma35d1_wdt->wdt_dev)) {
		u32 val;
		int ret;

		guard(spinlock_irqsave)(&ma35d1_wdt->lock);
		val = readl_relaxed(ma35d1_wdt->wdt_base + REG_WDT_CTL);

		if (device_may_wakeup(dev)) {
			val &= ~RSTEN;
			val |= (INTEN | WKEN);
		} else {
			val &= ~(WDTEN | RSTEN);
		}
		writel_relaxed(val, ma35d1_wdt->wdt_base + REG_WDT_CTL);
		ret = ma35d1_wdt_wait_sync(ma35d1_wdt);
		if (ret) {
			dev_err(dev, "Wait for WDTEN SYNC timeout!\n");
			return ret;
		}
	}

	return 0;
}

static int ma35d1_wdt_resume(struct device *dev)
{
	struct ma35d1_wdt_dev *ma35d1_wdt = dev_get_drvdata(dev);

	if (watchdog_active(&ma35d1_wdt->wdt_dev) ||
	    watchdog_hw_running(&ma35d1_wdt->wdt_dev)) {
		u32 val;
		int ret;

		guard(spinlock_irqsave)(&ma35d1_wdt->lock);
		val = readl_relaxed(ma35d1_wdt->wdt_base + REG_WDT_CTL);

		if (device_may_wakeup(dev)) {
			val |= RSTEN;
			val &= ~(INTEN | WKEN);
		} else {
			val |= (WDTEN | RSTEN);
		}
		writel_relaxed(val, ma35d1_wdt->wdt_base + REG_WDT_CTL);
		writel_relaxed(RESET_COUNTER,
			       ma35d1_wdt->wdt_base + REG_WDT_RSTCNT);
		ret = ma35d1_wdt_wait_sync(ma35d1_wdt);
		if (ret) {
			dev_err(dev, "Wait for WDTEN SYNC timeout!\n");
			return ret;
		}
	}
	return 0;
}

static DEFINE_SIMPLE_DEV_PM_OPS(ma35d1_wdt_pm_ops, ma35d1_wdt_suspend,
				ma35d1_wdt_resume);

static const struct of_device_id ma35d1_wdt_dt_ids[] = {
	{ .compatible = "nuvoton,ma35d1-wdt" },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, ma35d1_wdt_dt_ids);

static struct platform_driver ma35d1_wdt_driver = {
	.probe		= ma35d1_wdt_probe,
	.driver		= {
		.name	= DRV_NAME,
		.pm	= pm_ptr(&ma35d1_wdt_pm_ops),
		.of_match_table = ma35d1_wdt_dt_ids,
	},
};

module_platform_driver(ma35d1_wdt_driver);

module_param(timeout, uint, 0);
MODULE_PARM_DESC(timeout, "Watchdog heartbeat in seconds");

module_param(nowayout, bool, 0);
MODULE_PARM_DESC(
	nowayout,
	"Watchdog cannot be stopped once started (default=" __MODULE_STRING(
		WATCHDOG_NOWAYOUT) ")");

MODULE_AUTHOR("Zi-Yu Chen <zychennvt@gmail.com>");
MODULE_DESCRIPTION("Nuvoton MA35D1 Watchdog Timer Driver");
MODULE_LICENSE("GPL");

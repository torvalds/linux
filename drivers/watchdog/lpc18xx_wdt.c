/*
 * NXP LPC18xx Watchdog Timer (WDT)
 *
 * Copyright (c) 2015 Ariel D'Alessandro <ariel@vanguardiasur.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * Notes
 * -----
 * The Watchdog consists of a fixed divide-by-4 clock pre-scaler and a 24-bit
 * counter which decrements on every clock cycle.
 */

#include <linux/clk.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/watchdog.h>

/* Registers */
#define LPC18XX_WDT_MOD			0x00
#define LPC18XX_WDT_MOD_WDEN		BIT(0)
#define LPC18XX_WDT_MOD_WDRESET		BIT(1)

#define LPC18XX_WDT_TC			0x04
#define LPC18XX_WDT_TC_MIN		0xff
#define LPC18XX_WDT_TC_MAX		0xffffff

#define LPC18XX_WDT_FEED		0x08
#define LPC18XX_WDT_FEED_MAGIC1		0xaa
#define LPC18XX_WDT_FEED_MAGIC2		0x55

#define LPC18XX_WDT_TV			0x0c

/* Clock pre-scaler */
#define LPC18XX_WDT_CLK_DIV		4

/* Timeout values in seconds */
#define LPC18XX_WDT_DEF_TIMEOUT		30U

static int heartbeat;
module_param(heartbeat, int, 0);
MODULE_PARM_DESC(heartbeat, "Watchdog heartbeats in seconds (default="
		 __MODULE_STRING(LPC18XX_WDT_DEF_TIMEOUT) ")");

static bool nowayout = WATCHDOG_NOWAYOUT;
module_param(nowayout, bool, 0);
MODULE_PARM_DESC(nowayout, "Watchdog cannot be stopped once started (default="
		 __MODULE_STRING(WATCHDOG_NOWAYOUT) ")");

struct lpc18xx_wdt_dev {
	struct watchdog_device	wdt_dev;
	struct clk		*reg_clk;
	struct clk		*wdt_clk;
	unsigned long		clk_rate;
	void __iomem		*base;
	struct timer_list	timer;
	spinlock_t		lock;
};

static int lpc18xx_wdt_feed(struct watchdog_device *wdt_dev)
{
	struct lpc18xx_wdt_dev *lpc18xx_wdt = watchdog_get_drvdata(wdt_dev);
	unsigned long flags;

	/*
	 * An abort condition will occur if an interrupt happens during the feed
	 * sequence.
	 */
	spin_lock_irqsave(&lpc18xx_wdt->lock, flags);
	writel(LPC18XX_WDT_FEED_MAGIC1, lpc18xx_wdt->base + LPC18XX_WDT_FEED);
	writel(LPC18XX_WDT_FEED_MAGIC2, lpc18xx_wdt->base + LPC18XX_WDT_FEED);
	spin_unlock_irqrestore(&lpc18xx_wdt->lock, flags);

	return 0;
}

static void lpc18xx_wdt_timer_feed(struct timer_list *t)
{
	struct lpc18xx_wdt_dev *lpc18xx_wdt = from_timer(lpc18xx_wdt, t, timer);
	struct watchdog_device *wdt_dev = &lpc18xx_wdt->wdt_dev;

	lpc18xx_wdt_feed(wdt_dev);

	/* Use safe value (1/2 of real timeout) */
	mod_timer(&lpc18xx_wdt->timer, jiffies +
		  msecs_to_jiffies((wdt_dev->timeout * MSEC_PER_SEC) / 2));
}

/*
 * Since LPC18xx Watchdog cannot be disabled in hardware, we must keep feeding
 * it with a timer until userspace watchdog software takes over.
 */
static int lpc18xx_wdt_stop(struct watchdog_device *wdt_dev)
{
	struct lpc18xx_wdt_dev *lpc18xx_wdt = watchdog_get_drvdata(wdt_dev);

	lpc18xx_wdt_timer_feed(&lpc18xx_wdt->timer);

	return 0;
}

static void __lpc18xx_wdt_set_timeout(struct lpc18xx_wdt_dev *lpc18xx_wdt)
{
	unsigned int val;

	val = DIV_ROUND_UP(lpc18xx_wdt->wdt_dev.timeout * lpc18xx_wdt->clk_rate,
			   LPC18XX_WDT_CLK_DIV);
	writel(val, lpc18xx_wdt->base + LPC18XX_WDT_TC);
}

static int lpc18xx_wdt_set_timeout(struct watchdog_device *wdt_dev,
				   unsigned int new_timeout)
{
	struct lpc18xx_wdt_dev *lpc18xx_wdt = watchdog_get_drvdata(wdt_dev);

	lpc18xx_wdt->wdt_dev.timeout = new_timeout;
	__lpc18xx_wdt_set_timeout(lpc18xx_wdt);

	return 0;
}

static unsigned int lpc18xx_wdt_get_timeleft(struct watchdog_device *wdt_dev)
{
	struct lpc18xx_wdt_dev *lpc18xx_wdt = watchdog_get_drvdata(wdt_dev);
	unsigned int val;

	val = readl(lpc18xx_wdt->base + LPC18XX_WDT_TV);
	return (val * LPC18XX_WDT_CLK_DIV) / lpc18xx_wdt->clk_rate;
}

static int lpc18xx_wdt_start(struct watchdog_device *wdt_dev)
{
	struct lpc18xx_wdt_dev *lpc18xx_wdt = watchdog_get_drvdata(wdt_dev);
	unsigned int val;

	if (timer_pending(&lpc18xx_wdt->timer))
		del_timer(&lpc18xx_wdt->timer);

	val = readl(lpc18xx_wdt->base + LPC18XX_WDT_MOD);
	val |= LPC18XX_WDT_MOD_WDEN;
	val |= LPC18XX_WDT_MOD_WDRESET;
	writel(val, lpc18xx_wdt->base + LPC18XX_WDT_MOD);

	/*
	 * Setting the WDEN bit in the WDMOD register is not sufficient to
	 * enable the Watchdog. A valid feed sequence must be completed after
	 * setting WDEN before the Watchdog is capable of generating a reset.
	 */
	lpc18xx_wdt_feed(wdt_dev);

	return 0;
}

static int lpc18xx_wdt_restart(struct watchdog_device *wdt_dev,
			       unsigned long action, void *data)
{
	struct lpc18xx_wdt_dev *lpc18xx_wdt = watchdog_get_drvdata(wdt_dev);
	unsigned long flags;
	int val;

	/*
	 * Incorrect feed sequence causes immediate watchdog reset if enabled.
	 */
	spin_lock_irqsave(&lpc18xx_wdt->lock, flags);

	val = readl(lpc18xx_wdt->base + LPC18XX_WDT_MOD);
	val |= LPC18XX_WDT_MOD_WDEN;
	val |= LPC18XX_WDT_MOD_WDRESET;
	writel(val, lpc18xx_wdt->base + LPC18XX_WDT_MOD);

	writel(LPC18XX_WDT_FEED_MAGIC1, lpc18xx_wdt->base + LPC18XX_WDT_FEED);
	writel(LPC18XX_WDT_FEED_MAGIC2, lpc18xx_wdt->base + LPC18XX_WDT_FEED);

	writel(LPC18XX_WDT_FEED_MAGIC1, lpc18xx_wdt->base + LPC18XX_WDT_FEED);
	writel(LPC18XX_WDT_FEED_MAGIC1, lpc18xx_wdt->base + LPC18XX_WDT_FEED);

	spin_unlock_irqrestore(&lpc18xx_wdt->lock, flags);

	return 0;
}

static const struct watchdog_info lpc18xx_wdt_info = {
	.identity	= "NXP LPC18xx Watchdog",
	.options	= WDIOF_SETTIMEOUT |
			  WDIOF_KEEPALIVEPING |
			  WDIOF_MAGICCLOSE,
};

static const struct watchdog_ops lpc18xx_wdt_ops = {
	.owner		= THIS_MODULE,
	.start		= lpc18xx_wdt_start,
	.stop		= lpc18xx_wdt_stop,
	.ping		= lpc18xx_wdt_feed,
	.set_timeout	= lpc18xx_wdt_set_timeout,
	.get_timeleft	= lpc18xx_wdt_get_timeleft,
	.restart        = lpc18xx_wdt_restart,
};

static int lpc18xx_wdt_probe(struct platform_device *pdev)
{
	struct lpc18xx_wdt_dev *lpc18xx_wdt;
	struct device *dev = &pdev->dev;
	struct resource *res;
	int ret;

	lpc18xx_wdt = devm_kzalloc(dev, sizeof(*lpc18xx_wdt), GFP_KERNEL);
	if (!lpc18xx_wdt)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	lpc18xx_wdt->base = devm_ioremap_resource(dev, res);
	if (IS_ERR(lpc18xx_wdt->base))
		return PTR_ERR(lpc18xx_wdt->base);

	lpc18xx_wdt->reg_clk = devm_clk_get(dev, "reg");
	if (IS_ERR(lpc18xx_wdt->reg_clk)) {
		dev_err(dev, "failed to get the reg clock\n");
		return PTR_ERR(lpc18xx_wdt->reg_clk);
	}

	lpc18xx_wdt->wdt_clk = devm_clk_get(dev, "wdtclk");
	if (IS_ERR(lpc18xx_wdt->wdt_clk)) {
		dev_err(dev, "failed to get the wdt clock\n");
		return PTR_ERR(lpc18xx_wdt->wdt_clk);
	}

	ret = clk_prepare_enable(lpc18xx_wdt->reg_clk);
	if (ret) {
		dev_err(dev, "could not prepare or enable sys clock\n");
		return ret;
	}

	ret = clk_prepare_enable(lpc18xx_wdt->wdt_clk);
	if (ret) {
		dev_err(dev, "could not prepare or enable wdt clock\n");
		goto disable_reg_clk;
	}

	/* We use the clock rate to calculate timeouts */
	lpc18xx_wdt->clk_rate = clk_get_rate(lpc18xx_wdt->wdt_clk);
	if (lpc18xx_wdt->clk_rate == 0) {
		dev_err(dev, "failed to get clock rate\n");
		ret = -EINVAL;
		goto disable_wdt_clk;
	}

	lpc18xx_wdt->wdt_dev.info = &lpc18xx_wdt_info;
	lpc18xx_wdt->wdt_dev.ops = &lpc18xx_wdt_ops;

	lpc18xx_wdt->wdt_dev.min_timeout = DIV_ROUND_UP(LPC18XX_WDT_TC_MIN *
				LPC18XX_WDT_CLK_DIV, lpc18xx_wdt->clk_rate);

	lpc18xx_wdt->wdt_dev.max_timeout = (LPC18XX_WDT_TC_MAX *
				LPC18XX_WDT_CLK_DIV) / lpc18xx_wdt->clk_rate;

	lpc18xx_wdt->wdt_dev.timeout = min(lpc18xx_wdt->wdt_dev.max_timeout,
					   LPC18XX_WDT_DEF_TIMEOUT);

	spin_lock_init(&lpc18xx_wdt->lock);

	lpc18xx_wdt->wdt_dev.parent = dev;
	watchdog_set_drvdata(&lpc18xx_wdt->wdt_dev, lpc18xx_wdt);

	ret = watchdog_init_timeout(&lpc18xx_wdt->wdt_dev, heartbeat, dev);

	__lpc18xx_wdt_set_timeout(lpc18xx_wdt);

	timer_setup(&lpc18xx_wdt->timer, lpc18xx_wdt_timer_feed, 0);

	watchdog_set_nowayout(&lpc18xx_wdt->wdt_dev, nowayout);
	watchdog_set_restart_priority(&lpc18xx_wdt->wdt_dev, 128);

	platform_set_drvdata(pdev, lpc18xx_wdt);

	ret = watchdog_register_device(&lpc18xx_wdt->wdt_dev);
	if (ret)
		goto disable_wdt_clk;

	return 0;

disable_wdt_clk:
	clk_disable_unprepare(lpc18xx_wdt->wdt_clk);
disable_reg_clk:
	clk_disable_unprepare(lpc18xx_wdt->reg_clk);
	return ret;
}

static void lpc18xx_wdt_shutdown(struct platform_device *pdev)
{
	struct lpc18xx_wdt_dev *lpc18xx_wdt = platform_get_drvdata(pdev);

	lpc18xx_wdt_stop(&lpc18xx_wdt->wdt_dev);
}

static int lpc18xx_wdt_remove(struct platform_device *pdev)
{
	struct lpc18xx_wdt_dev *lpc18xx_wdt = platform_get_drvdata(pdev);

	dev_warn(&pdev->dev, "I quit now, hardware will probably reboot!\n");
	del_timer(&lpc18xx_wdt->timer);

	watchdog_unregister_device(&lpc18xx_wdt->wdt_dev);
	clk_disable_unprepare(lpc18xx_wdt->wdt_clk);
	clk_disable_unprepare(lpc18xx_wdt->reg_clk);

	return 0;
}

static const struct of_device_id lpc18xx_wdt_match[] = {
	{ .compatible = "nxp,lpc1850-wwdt" },
	{}
};
MODULE_DEVICE_TABLE(of, lpc18xx_wdt_match);

static struct platform_driver lpc18xx_wdt_driver = {
	.driver = {
		.name = "lpc18xx-wdt",
		.of_match_table	= lpc18xx_wdt_match,
	},
	.probe = lpc18xx_wdt_probe,
	.remove = lpc18xx_wdt_remove,
	.shutdown = lpc18xx_wdt_shutdown,
};
module_platform_driver(lpc18xx_wdt_driver);

MODULE_AUTHOR("Ariel D'Alessandro <ariel@vanguardiasur.com.ar>");
MODULE_DESCRIPTION("NXP LPC18xx Watchdog Timer Driver");
MODULE_LICENSE("GPL v2");

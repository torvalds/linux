// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * PIC32 watchdog driver
 *
 * Joshua Henderson <joshua.henderson@microchip.com>
 * Copyright (c) 2016, Microchip Technology Inc.
 */
#include <linux/clk.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm.h>
#include <linux/watchdog.h>

#include <asm/mach-pic32/pic32.h>

/* Watchdog Timer Registers */
#define WDTCON_REG		0x00

/* Watchdog Timer Control Register fields */
#define WDTCON_WIN_EN		BIT(0)
#define WDTCON_RMCS_MASK	0x0003
#define WDTCON_RMCS_SHIFT	0x0006
#define WDTCON_RMPS_MASK	0x001F
#define WDTCON_RMPS_SHIFT	0x0008
#define WDTCON_ON		BIT(15)
#define WDTCON_CLR_KEY		0x5743

/* Reset Control Register fields for watchdog */
#define RESETCON_TIMEOUT_IDLE	BIT(2)
#define RESETCON_TIMEOUT_SLEEP	BIT(3)
#define RESETCON_WDT_TIMEOUT	BIT(4)

struct pic32_wdt {
	void __iomem	*regs;
	void __iomem	*rst_base;
	struct clk	*clk;
};

static inline bool pic32_wdt_is_win_enabled(struct pic32_wdt *wdt)
{
	return !!(readl(wdt->regs + WDTCON_REG) & WDTCON_WIN_EN);
}

static inline u32 pic32_wdt_get_post_scaler(struct pic32_wdt *wdt)
{
	u32 v = readl(wdt->regs + WDTCON_REG);

	return (v >> WDTCON_RMPS_SHIFT) & WDTCON_RMPS_MASK;
}

static inline u32 pic32_wdt_get_clk_id(struct pic32_wdt *wdt)
{
	u32 v = readl(wdt->regs + WDTCON_REG);

	return (v >> WDTCON_RMCS_SHIFT) & WDTCON_RMCS_MASK;
}

static int pic32_wdt_bootstatus(struct pic32_wdt *wdt)
{
	u32 v = readl(wdt->rst_base);

	writel(RESETCON_WDT_TIMEOUT, PIC32_CLR(wdt->rst_base));

	return v & RESETCON_WDT_TIMEOUT;
}

static u32 pic32_wdt_get_timeout_secs(struct pic32_wdt *wdt, struct device *dev)
{
	unsigned long rate;
	u32 period, ps, terminal;

	rate = clk_get_rate(wdt->clk);

	dev_dbg(dev, "wdt: clk_id %d, clk_rate %lu (prescale)\n",
		pic32_wdt_get_clk_id(wdt), rate);

	/* default, prescaler of 32 (i.e. div-by-32) is implicit. */
	rate >>= 5;
	if (!rate)
		return 0;

	/* calculate terminal count from postscaler. */
	ps = pic32_wdt_get_post_scaler(wdt);
	terminal = BIT(ps);

	/* find time taken (in secs) to reach terminal count */
	period = terminal / rate;
	dev_dbg(dev,
		"wdt: clk_rate %lu (postscale) / terminal %d, timeout %dsec\n",
		rate, terminal, period);

	return period;
}

static void pic32_wdt_keepalive(struct pic32_wdt *wdt)
{
	/* write key through single half-word */
	writew(WDTCON_CLR_KEY, wdt->regs + WDTCON_REG + 2);
}

static int pic32_wdt_start(struct watchdog_device *wdd)
{
	struct pic32_wdt *wdt = watchdog_get_drvdata(wdd);

	writel(WDTCON_ON, PIC32_SET(wdt->regs + WDTCON_REG));
	pic32_wdt_keepalive(wdt);

	return 0;
}

static int pic32_wdt_stop(struct watchdog_device *wdd)
{
	struct pic32_wdt *wdt = watchdog_get_drvdata(wdd);

	writel(WDTCON_ON, PIC32_CLR(wdt->regs + WDTCON_REG));

	/*
	 * Cannot touch registers in the CPU cycle following clearing the
	 * ON bit.
	 */
	nop();

	return 0;
}

static int pic32_wdt_ping(struct watchdog_device *wdd)
{
	struct pic32_wdt *wdt = watchdog_get_drvdata(wdd);

	pic32_wdt_keepalive(wdt);

	return 0;
}

static const struct watchdog_ops pic32_wdt_fops = {
	.owner		= THIS_MODULE,
	.start		= pic32_wdt_start,
	.stop		= pic32_wdt_stop,
	.ping		= pic32_wdt_ping,
};

static const struct watchdog_info pic32_wdt_ident = {
	.options = WDIOF_KEEPALIVEPING |
			WDIOF_MAGICCLOSE | WDIOF_CARDRESET,
	.identity = "PIC32 Watchdog",
};

static struct watchdog_device pic32_wdd = {
	.info		= &pic32_wdt_ident,
	.ops		= &pic32_wdt_fops,
};

static const struct of_device_id pic32_wdt_dt_ids[] = {
	{ .compatible = "microchip,pic32mzda-wdt", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, pic32_wdt_dt_ids);

static int pic32_wdt_drv_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	int ret;
	struct watchdog_device *wdd = &pic32_wdd;
	struct pic32_wdt *wdt;

	wdt = devm_kzalloc(dev, sizeof(*wdt), GFP_KERNEL);
	if (!wdt)
		return -ENOMEM;

	wdt->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(wdt->regs))
		return PTR_ERR(wdt->regs);

	wdt->rst_base = devm_ioremap(dev, PIC32_BASE_RESET, 0x10);
	if (!wdt->rst_base)
		return -ENOMEM;

	wdt->clk = devm_clk_get_enabled(dev, NULL);
	if (IS_ERR(wdt->clk)) {
		dev_err(dev, "clk not found\n");
		return PTR_ERR(wdt->clk);
	}

	if (pic32_wdt_is_win_enabled(wdt)) {
		dev_err(dev, "windowed-clear mode is not supported.\n");
		return -ENODEV;
	}

	wdd->timeout = pic32_wdt_get_timeout_secs(wdt, dev);
	if (!wdd->timeout) {
		dev_err(dev, "failed to read watchdog register timeout\n");
		return -EINVAL;
	}

	dev_info(dev, "timeout %d\n", wdd->timeout);

	wdd->bootstatus = pic32_wdt_bootstatus(wdt) ? WDIOF_CARDRESET : 0;

	watchdog_set_nowayout(wdd, WATCHDOG_NOWAYOUT);
	watchdog_set_drvdata(wdd, wdt);

	ret = devm_watchdog_register_device(dev, wdd);
	if (ret)
		return ret;

	platform_set_drvdata(pdev, wdd);

	return 0;
}

static struct platform_driver pic32_wdt_driver = {
	.probe		= pic32_wdt_drv_probe,
	.driver		= {
		.name		= "pic32-wdt",
		.of_match_table = of_match_ptr(pic32_wdt_dt_ids),
	}
};

module_platform_driver(pic32_wdt_driver);

MODULE_AUTHOR("Joshua Henderson <joshua.henderson@microchip.com>");
MODULE_DESCRIPTION("Microchip PIC32 Watchdog Timer");
MODULE_LICENSE("GPL");

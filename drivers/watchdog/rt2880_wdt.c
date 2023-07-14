// SPDX-License-Identifier: GPL-2.0-only
/*
 * Ralink RT288x/RT3xxx/MT76xx built-in hardware watchdog timer
 *
 * Copyright (C) 2011 Gabor Juhos <juhosg@openwrt.org>
 * Copyright (C) 2013 John Crispin <john@phrozen.org>
 *
 * This driver was based on: drivers/watchdog/softdog.c
 */

#include <linux/clk.h>
#include <linux/reset.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/watchdog.h>
#include <linux/moduleparam.h>
#include <linux/platform_device.h>
#include <linux/mod_devicetable.h>

#include <asm/mach-ralink/ralink_regs.h>

#define SYSC_RSTSTAT			0x38
#define WDT_RST_CAUSE			BIT(1)

#define RALINK_WDT_TIMEOUT		30
#define RALINK_WDT_PRESCALE		65536

#define TIMER_REG_TMR1LOAD		0x00
#define TIMER_REG_TMR1CTL		0x08

#define TMRSTAT_TMR1RST			BIT(5)

#define TMR1CTL_ENABLE			BIT(7)
#define TMR1CTL_MODE_SHIFT		4
#define TMR1CTL_MODE_MASK		0x3
#define TMR1CTL_MODE_FREE_RUNNING	0x0
#define TMR1CTL_MODE_PERIODIC		0x1
#define TMR1CTL_MODE_TIMEOUT		0x2
#define TMR1CTL_MODE_WDT		0x3
#define TMR1CTL_PRESCALE_MASK		0xf
#define TMR1CTL_PRESCALE_65536		0xf

struct rt2880_wdt_data {
	void __iomem *base;
	unsigned long freq;
	struct clk *clk;
	struct reset_control *rst;
	struct watchdog_device wdt;
};

static bool nowayout = WATCHDOG_NOWAYOUT;
module_param(nowayout, bool, 0);
MODULE_PARM_DESC(nowayout,
		"Watchdog cannot be stopped once started (default="
		__MODULE_STRING(WATCHDOG_NOWAYOUT) ")");

static inline void rt_wdt_w32(void __iomem *base, unsigned int reg, u32 val)
{
	iowrite32(val, base + reg);
}

static inline u32 rt_wdt_r32(void __iomem *base, unsigned int reg)
{
	return ioread32(base + reg);
}

static int rt288x_wdt_ping(struct watchdog_device *w)
{
	struct rt2880_wdt_data *drvdata = watchdog_get_drvdata(w);

	rt_wdt_w32(drvdata->base, TIMER_REG_TMR1LOAD, w->timeout * drvdata->freq);

	return 0;
}

static int rt288x_wdt_start(struct watchdog_device *w)
{
	struct rt2880_wdt_data *drvdata = watchdog_get_drvdata(w);
	u32 t;

	t = rt_wdt_r32(drvdata->base, TIMER_REG_TMR1CTL);
	t &= ~(TMR1CTL_MODE_MASK << TMR1CTL_MODE_SHIFT |
		TMR1CTL_PRESCALE_MASK);
	t |= (TMR1CTL_MODE_WDT << TMR1CTL_MODE_SHIFT |
		TMR1CTL_PRESCALE_65536);
	rt_wdt_w32(drvdata->base, TIMER_REG_TMR1CTL, t);

	rt288x_wdt_ping(w);

	t = rt_wdt_r32(drvdata->base, TIMER_REG_TMR1CTL);
	t |= TMR1CTL_ENABLE;
	rt_wdt_w32(drvdata->base, TIMER_REG_TMR1CTL, t);

	return 0;
}

static int rt288x_wdt_stop(struct watchdog_device *w)
{
	struct rt2880_wdt_data *drvdata = watchdog_get_drvdata(w);
	u32 t;

	rt288x_wdt_ping(w);

	t = rt_wdt_r32(drvdata->base, TIMER_REG_TMR1CTL);
	t &= ~TMR1CTL_ENABLE;
	rt_wdt_w32(drvdata->base, TIMER_REG_TMR1CTL, t);

	return 0;
}

static int rt288x_wdt_set_timeout(struct watchdog_device *w, unsigned int t)
{
	w->timeout = t;
	rt288x_wdt_ping(w);

	return 0;
}

static int rt288x_wdt_bootcause(void)
{
	if (rt_sysc_r32(SYSC_RSTSTAT) & WDT_RST_CAUSE)
		return WDIOF_CARDRESET;

	return 0;
}

static const struct watchdog_info rt288x_wdt_info = {
	.identity = "Ralink Watchdog",
	.options = WDIOF_SETTIMEOUT | WDIOF_KEEPALIVEPING | WDIOF_MAGICCLOSE,
};

static const struct watchdog_ops rt288x_wdt_ops = {
	.owner = THIS_MODULE,
	.start = rt288x_wdt_start,
	.stop = rt288x_wdt_stop,
	.ping = rt288x_wdt_ping,
	.set_timeout = rt288x_wdt_set_timeout,
};

static int rt288x_wdt_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct watchdog_device *wdt;
	struct rt2880_wdt_data *drvdata;
	int ret;

	drvdata = devm_kzalloc(dev, sizeof(*drvdata), GFP_KERNEL);
	if (!drvdata)
		return -ENOMEM;

	drvdata->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(drvdata->base))
		return PTR_ERR(drvdata->base);

	drvdata->clk = devm_clk_get(dev, NULL);
	if (IS_ERR(drvdata->clk))
		return PTR_ERR(drvdata->clk);

	drvdata->rst = devm_reset_control_get_exclusive(dev, NULL);
	if (!IS_ERR(drvdata->rst))
		reset_control_deassert(drvdata->rst);

	drvdata->freq = clk_get_rate(drvdata->clk) / RALINK_WDT_PRESCALE;

	wdt = &drvdata->wdt;
	wdt->info = &rt288x_wdt_info;
	wdt->ops = &rt288x_wdt_ops;
	wdt->min_timeout = 1;
	wdt->max_timeout = (0xfffful / drvdata->freq);
	wdt->parent = dev;
	wdt->bootstatus = rt288x_wdt_bootcause();

	watchdog_init_timeout(wdt, wdt->max_timeout, dev);
	watchdog_set_nowayout(wdt, nowayout);
	watchdog_set_drvdata(wdt, drvdata);

	watchdog_stop_on_reboot(wdt);
	ret = devm_watchdog_register_device(dev, &drvdata->wdt);
	if (!ret)
		dev_info(dev, "Initialized\n");

	return 0;
}

static const struct of_device_id rt288x_wdt_match[] = {
	{ .compatible = "ralink,rt2880-wdt" },
	{},
};
MODULE_DEVICE_TABLE(of, rt288x_wdt_match);

static struct platform_driver rt288x_wdt_driver = {
	.probe		= rt288x_wdt_probe,
	.driver		= {
		.name		= KBUILD_MODNAME,
		.of_match_table	= rt288x_wdt_match,
	},
};

module_platform_driver(rt288x_wdt_driver);

MODULE_DESCRIPTION("MediaTek/Ralink RT288x/RT3xxx hardware watchdog driver");
MODULE_AUTHOR("Gabor Juhos <juhosg@openwrt.org");
MODULE_LICENSE("GPL v2");

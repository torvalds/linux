// SPDX-License-Identifier: GPL-2.0+
/*
 * Watchdog driver for Atmel AT91SAM9x processors.
 *
 * Copyright (C) 2008 Renaud CERRATO r.cerrato@til-technologies.fr
 *
 */

/*
 * The Watchdog Timer Mode Register can be only written to once. If the
 * timeout need to be set from Linux, be sure that the bootstrap or the
 * bootloader doesn't write to this register.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/clk.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/platform_device.h>
#include <linux/reboot.h>
#include <linux/types.h>
#include <linux/watchdog.h>
#include <linux/jiffies.h>
#include <linux/timer.h>
#include <linux/bitops.h>
#include <linux/uaccess.h>
#include <linux/of.h>
#include <linux/of_irq.h>

#include "at91sam9_wdt.h"

#define DRV_NAME "AT91SAM9 Watchdog"

#define wdt_read(wdt, field) \
	readl_relaxed((wdt)->base + (field))
#define wdt_write(wtd, field, val) \
	writel_relaxed((val), (wdt)->base + (field))

/* AT91SAM9 watchdog runs a 12bit counter @ 256Hz,
 * use this to convert a watchdog
 * value from/to milliseconds.
 */
#define ticks_to_hz_rounddown(t)	((((t) + 1) * HZ) >> 8)
#define ticks_to_hz_roundup(t)		(((((t) + 1) * HZ) + 255) >> 8)
#define ticks_to_secs(t)		(((t) + 1) >> 8)
#define secs_to_ticks(s)		((s) ? (((s) << 8) - 1) : 0)

#define WDT_MR_RESET	0x3FFF2FFF

/* Watchdog max counter value in ticks */
#define WDT_COUNTER_MAX_TICKS	0xFFF

/* Watchdog max delta/value in secs */
#define WDT_COUNTER_MAX_SECS	ticks_to_secs(WDT_COUNTER_MAX_TICKS)

/* Hardware timeout in seconds */
#define WDT_HW_TIMEOUT 2

/* Timer heartbeat (500ms) */
#define WDT_TIMEOUT	(HZ/2)

/* User land timeout */
#define WDT_HEARTBEAT 15
static int heartbeat;
module_param(heartbeat, int, 0);
MODULE_PARM_DESC(heartbeat, "Watchdog heartbeats in seconds. "
	"(default = " __MODULE_STRING(WDT_HEARTBEAT) ")");

static bool nowayout = WATCHDOG_NOWAYOUT;
module_param(nowayout, bool, 0);
MODULE_PARM_DESC(nowayout, "Watchdog cannot be stopped once started "
	"(default=" __MODULE_STRING(WATCHDOG_NOWAYOUT) ")");

#define to_wdt(wdd) container_of(wdd, struct at91wdt, wdd)
struct at91wdt {
	struct watchdog_device wdd;
	void __iomem *base;
	unsigned long next_heartbeat;	/* the next_heartbeat for the timer */
	struct timer_list timer;	/* The timer that pings the watchdog */
	u32 mr;
	u32 mr_mask;
	unsigned long heartbeat;	/* WDT heartbeat in jiffies */
	bool nowayout;
	unsigned int irq;
	struct clk *sclk;
};

/* ......................................................................... */

static irqreturn_t wdt_interrupt(int irq, void *dev_id)
{
	struct at91wdt *wdt = (struct at91wdt *)dev_id;

	if (wdt_read(wdt, AT91_WDT_SR)) {
		pr_crit("at91sam9 WDT software reset\n");
		emergency_restart();
		pr_crit("Reboot didn't ?????\n");
	}

	return IRQ_HANDLED;
}

/*
 * Reload the watchdog timer.  (ie, pat the watchdog)
 */
static inline void at91_wdt_reset(struct at91wdt *wdt)
{
	wdt_write(wdt, AT91_WDT_CR, AT91_WDT_KEY | AT91_WDT_WDRSTT);
}

/*
 * Timer tick
 */
static void at91_ping(struct timer_list *t)
{
	struct at91wdt *wdt = from_timer(wdt, t, timer);
	if (time_before(jiffies, wdt->next_heartbeat) ||
	    !watchdog_active(&wdt->wdd)) {
		at91_wdt_reset(wdt);
		mod_timer(&wdt->timer, jiffies + wdt->heartbeat);
	} else {
		pr_crit("I will reset your machine !\n");
	}
}

static int at91_wdt_start(struct watchdog_device *wdd)
{
	struct at91wdt *wdt = to_wdt(wdd);
	/* calculate when the next userspace timeout will be */
	wdt->next_heartbeat = jiffies + wdd->timeout * HZ;
	return 0;
}

static int at91_wdt_stop(struct watchdog_device *wdd)
{
	/* The watchdog timer hardware can not be stopped... */
	return 0;
}

static int at91_wdt_set_timeout(struct watchdog_device *wdd, unsigned int new_timeout)
{
	wdd->timeout = new_timeout;
	return at91_wdt_start(wdd);
}

static int at91_wdt_init(struct platform_device *pdev, struct at91wdt *wdt)
{
	u32 tmp;
	u32 delta;
	u32 value;
	int err;
	u32 mask = wdt->mr_mask;
	unsigned long min_heartbeat = 1;
	unsigned long max_heartbeat;
	struct device *dev = &pdev->dev;

	tmp = wdt_read(wdt, AT91_WDT_MR);
	if ((tmp & mask) != (wdt->mr & mask)) {
		if (tmp == WDT_MR_RESET) {
			wdt_write(wdt, AT91_WDT_MR, wdt->mr);
			tmp = wdt_read(wdt, AT91_WDT_MR);
		}
	}

	if (tmp & AT91_WDT_WDDIS) {
		if (wdt->mr & AT91_WDT_WDDIS)
			return 0;
		dev_err(dev, "watchdog is disabled\n");
		return -EINVAL;
	}

	value = tmp & AT91_WDT_WDV;
	delta = (tmp & AT91_WDT_WDD) >> 16;

	if (delta < value)
		min_heartbeat = ticks_to_hz_roundup(value - delta);

	max_heartbeat = ticks_to_hz_rounddown(value);
	if (!max_heartbeat) {
		dev_err(dev,
			"heartbeat is too small for the system to handle it correctly\n");
		return -EINVAL;
	}

	/*
	 * Try to reset the watchdog counter 4 or 2 times more often than
	 * actually requested, to avoid spurious watchdog reset.
	 * If this is not possible because of the min_heartbeat value, reset
	 * it at the min_heartbeat period.
	 */
	if ((max_heartbeat / 4) >= min_heartbeat)
		wdt->heartbeat = max_heartbeat / 4;
	else if ((max_heartbeat / 2) >= min_heartbeat)
		wdt->heartbeat = max_heartbeat / 2;
	else
		wdt->heartbeat = min_heartbeat;

	if (max_heartbeat < min_heartbeat + 4)
		dev_warn(dev,
			 "min heartbeat and max heartbeat might be too close for the system to handle it correctly\n");

	if ((tmp & AT91_WDT_WDFIEN) && wdt->irq) {
		err = devm_request_irq(dev, wdt->irq, wdt_interrupt,
				       IRQF_SHARED | IRQF_IRQPOLL | IRQF_NO_SUSPEND,
				       pdev->name, wdt);
		if (err)
			return err;
	}

	if ((tmp & wdt->mr_mask) != (wdt->mr & wdt->mr_mask))
		dev_warn(dev,
			 "watchdog already configured differently (mr = %x expecting %x)\n",
			 tmp & wdt->mr_mask, wdt->mr & wdt->mr_mask);

	timer_setup(&wdt->timer, at91_ping, 0);

	/*
	 * Use min_heartbeat the first time to avoid spurious watchdog reset:
	 * we don't know for how long the watchdog counter is running, and
	 *  - resetting it right now might trigger a watchdog fault reset
	 *  - waiting for heartbeat time might lead to a watchdog timeout
	 *    reset
	 */
	mod_timer(&wdt->timer, jiffies + min_heartbeat);

	/* Try to set timeout from device tree first */
	if (watchdog_init_timeout(&wdt->wdd, 0, dev))
		watchdog_init_timeout(&wdt->wdd, heartbeat, dev);
	watchdog_set_nowayout(&wdt->wdd, wdt->nowayout);
	err = watchdog_register_device(&wdt->wdd);
	if (err)
		goto out_stop_timer;

	wdt->next_heartbeat = jiffies + wdt->wdd.timeout * HZ;

	return 0;

out_stop_timer:
	del_timer(&wdt->timer);
	return err;
}

/* ......................................................................... */

static const struct watchdog_info at91_wdt_info = {
	.identity	= DRV_NAME,
	.options	= WDIOF_SETTIMEOUT | WDIOF_KEEPALIVEPING |
						WDIOF_MAGICCLOSE,
};

static const struct watchdog_ops at91_wdt_ops = {
	.owner =	THIS_MODULE,
	.start =	at91_wdt_start,
	.stop =		at91_wdt_stop,
	.set_timeout =	at91_wdt_set_timeout,
};

#if defined(CONFIG_OF)
static int of_at91wdt_init(struct device_node *np, struct at91wdt *wdt)
{
	u32 min = 0;
	u32 max = WDT_COUNTER_MAX_SECS;
	const char *tmp;

	/* Get the interrupts property */
	wdt->irq = irq_of_parse_and_map(np, 0);
	if (!wdt->irq)
		dev_warn(wdt->wdd.parent, "failed to get IRQ from DT\n");

	if (!of_property_read_u32_index(np, "atmel,max-heartbeat-sec", 0,
					&max)) {
		if (!max || max > WDT_COUNTER_MAX_SECS)
			max = WDT_COUNTER_MAX_SECS;

		if (!of_property_read_u32_index(np, "atmel,min-heartbeat-sec",
						0, &min)) {
			if (min >= max)
				min = max - 1;
		}
	}

	min = secs_to_ticks(min);
	max = secs_to_ticks(max);

	wdt->mr_mask = 0x3FFFFFFF;
	wdt->mr = 0;
	if (!of_property_read_string(np, "atmel,watchdog-type", &tmp) &&
	    !strcmp(tmp, "software")) {
		wdt->mr |= AT91_WDT_WDFIEN;
		wdt->mr_mask &= ~AT91_WDT_WDRPROC;
	} else {
		wdt->mr |= AT91_WDT_WDRSTEN;
	}

	if (!of_property_read_string(np, "atmel,reset-type", &tmp) &&
	    !strcmp(tmp, "proc"))
		wdt->mr |= AT91_WDT_WDRPROC;

	if (of_property_read_bool(np, "atmel,disable")) {
		wdt->mr |= AT91_WDT_WDDIS;
		wdt->mr_mask &= AT91_WDT_WDDIS;
	}

	if (of_property_read_bool(np, "atmel,idle-halt"))
		wdt->mr |= AT91_WDT_WDIDLEHLT;

	if (of_property_read_bool(np, "atmel,dbg-halt"))
		wdt->mr |= AT91_WDT_WDDBGHLT;

	wdt->mr |= max | ((max - min) << 16);

	return 0;
}
#else
static inline int of_at91wdt_init(struct device_node *np, struct at91wdt *wdt)
{
	return 0;
}
#endif

static int at91wdt_probe(struct platform_device *pdev)
{
	int err;
	struct at91wdt *wdt;

	wdt = devm_kzalloc(&pdev->dev, sizeof(*wdt), GFP_KERNEL);
	if (!wdt)
		return -ENOMEM;

	wdt->mr = (WDT_HW_TIMEOUT * 256) | AT91_WDT_WDRSTEN | AT91_WDT_WDD |
		  AT91_WDT_WDDBGHLT | AT91_WDT_WDIDLEHLT;
	wdt->mr_mask = 0x3FFFFFFF;
	wdt->nowayout = nowayout;
	wdt->wdd.parent = &pdev->dev;
	wdt->wdd.info = &at91_wdt_info;
	wdt->wdd.ops = &at91_wdt_ops;
	wdt->wdd.timeout = WDT_HEARTBEAT;
	wdt->wdd.min_timeout = 1;
	wdt->wdd.max_timeout = 0xFFFF;

	wdt->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(wdt->base))
		return PTR_ERR(wdt->base);

	wdt->sclk = devm_clk_get_enabled(&pdev->dev, NULL);
	if (IS_ERR(wdt->sclk)) {
		dev_err(&pdev->dev, "Could not enable slow clock\n");
		return PTR_ERR(wdt->sclk);
	}

	if (pdev->dev.of_node) {
		err = of_at91wdt_init(pdev->dev.of_node, wdt);
		if (err)
			return err;
	}

	err = at91_wdt_init(pdev, wdt);
	if (err)
		return err;

	platform_set_drvdata(pdev, wdt);

	pr_info("enabled (heartbeat=%d sec, nowayout=%d)\n",
		wdt->wdd.timeout, wdt->nowayout);

	return 0;
}

static void at91wdt_remove(struct platform_device *pdev)
{
	struct at91wdt *wdt = platform_get_drvdata(pdev);
	watchdog_unregister_device(&wdt->wdd);

	pr_warn("I quit now, hardware will probably reboot!\n");
	del_timer(&wdt->timer);
}

#if defined(CONFIG_OF)
static const struct of_device_id at91_wdt_dt_ids[] = {
	{ .compatible = "atmel,at91sam9260-wdt" },
	{ /* sentinel */ }
};

MODULE_DEVICE_TABLE(of, at91_wdt_dt_ids);
#endif

static struct platform_driver at91wdt_driver = {
	.probe		= at91wdt_probe,
	.remove_new	= at91wdt_remove,
	.driver		= {
		.name	= "at91_wdt",
		.of_match_table = of_match_ptr(at91_wdt_dt_ids),
	},
};
module_platform_driver(at91wdt_driver);

MODULE_AUTHOR("Renaud CERRATO <r.cerrato@til-technologies.fr>");
MODULE_DESCRIPTION("Watchdog driver for Atmel AT91SAM9x processors");
MODULE_LICENSE("GPL");

/*
 * drivers/char/watchdog/pnx4008_wdt.c
 *
 * Watchdog driver for PNX4008 board
 *
 * Authors: Dmitry Chigirev <source@mvista.com>,
 *	    Vitaly Wool <vitalywool@gmail.com>
 * Based on sa1100 driver,
 * Copyright (C) 2000 Oleg Drokin <green@crimea.edu>
 *
 * 2005-2006 (c) MontaVista Software, Inc.
 *
 * (C) 2012 Wolfram Sang, Pengutronix
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2. This program is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/watchdog.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/spinlock.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/of.h>
#include <mach/hardware.h>

/* WatchDog Timer - Chapter 23 Page 207 */

#define DEFAULT_HEARTBEAT 19
#define MAX_HEARTBEAT     60

/* Watchdog timer register set definition */
#define WDTIM_INT(p)     ((p) + 0x0)
#define WDTIM_CTRL(p)    ((p) + 0x4)
#define WDTIM_COUNTER(p) ((p) + 0x8)
#define WDTIM_MCTRL(p)   ((p) + 0xC)
#define WDTIM_MATCH0(p)  ((p) + 0x10)
#define WDTIM_EMR(p)     ((p) + 0x14)
#define WDTIM_PULSE(p)   ((p) + 0x18)
#define WDTIM_RES(p)     ((p) + 0x1C)

/* WDTIM_INT bit definitions */
#define MATCH_INT      1

/* WDTIM_CTRL bit definitions */
#define COUNT_ENAB     1
#define RESET_COUNT    (1 << 1)
#define DEBUG_EN       (1 << 2)

/* WDTIM_MCTRL bit definitions */
#define MR0_INT        1
#undef  RESET_COUNT0
#define RESET_COUNT0   (1 << 2)
#define STOP_COUNT0    (1 << 2)
#define M_RES1         (1 << 3)
#define M_RES2         (1 << 4)
#define RESFRC1        (1 << 5)
#define RESFRC2        (1 << 6)

/* WDTIM_EMR bit definitions */
#define EXT_MATCH0      1
#define MATCH_OUTPUT_HIGH (2 << 4)	/*a MATCH_CTRL setting */

/* WDTIM_RES bit definitions */
#define WDOG_RESET      1	/* read only */

#define WDOG_COUNTER_RATE 13000000	/*the counter clock is 13 MHz fixed */

static bool nowayout = WATCHDOG_NOWAYOUT;
static unsigned int heartbeat = DEFAULT_HEARTBEAT;

static DEFINE_SPINLOCK(io_lock);
static void __iomem	*wdt_base;
struct clk		*wdt_clk;

static int pnx4008_wdt_start(struct watchdog_device *wdd)
{
	spin_lock(&io_lock);

	/* stop counter, initiate counter reset */
	writel(RESET_COUNT, WDTIM_CTRL(wdt_base));
	/*wait for reset to complete. 100% guarantee event */
	while (readl(WDTIM_COUNTER(wdt_base)))
		cpu_relax();
	/* internal and external reset, stop after that */
	writel(M_RES2 | STOP_COUNT0 | RESET_COUNT0, WDTIM_MCTRL(wdt_base));
	/* configure match output */
	writel(MATCH_OUTPUT_HIGH, WDTIM_EMR(wdt_base));
	/* clear interrupt, just in case */
	writel(MATCH_INT, WDTIM_INT(wdt_base));
	/* the longest pulse period 65541/(13*10^6) seconds ~ 5 ms. */
	writel(0xFFFF, WDTIM_PULSE(wdt_base));
	writel(wdd->timeout * WDOG_COUNTER_RATE, WDTIM_MATCH0(wdt_base));
	/*enable counter, stop when debugger active */
	writel(COUNT_ENAB | DEBUG_EN, WDTIM_CTRL(wdt_base));

	spin_unlock(&io_lock);
	return 0;
}

static int pnx4008_wdt_stop(struct watchdog_device *wdd)
{
	spin_lock(&io_lock);

	writel(0, WDTIM_CTRL(wdt_base));	/*stop counter */

	spin_unlock(&io_lock);
	return 0;
}

static int pnx4008_wdt_set_timeout(struct watchdog_device *wdd,
				    unsigned int new_timeout)
{
	wdd->timeout = new_timeout;
	return 0;
}

static const struct watchdog_info pnx4008_wdt_ident = {
	.options = WDIOF_CARDRESET | WDIOF_MAGICCLOSE |
	    WDIOF_SETTIMEOUT | WDIOF_KEEPALIVEPING,
	.identity = "PNX4008 Watchdog",
};

static const struct watchdog_ops pnx4008_wdt_ops = {
	.owner = THIS_MODULE,
	.start = pnx4008_wdt_start,
	.stop = pnx4008_wdt_stop,
	.set_timeout = pnx4008_wdt_set_timeout,
};

static struct watchdog_device pnx4008_wdd = {
	.info = &pnx4008_wdt_ident,
	.ops = &pnx4008_wdt_ops,
	.timeout = DEFAULT_HEARTBEAT,
	.min_timeout = 1,
	.max_timeout = MAX_HEARTBEAT,
};

static int pnx4008_wdt_probe(struct platform_device *pdev)
{
	struct resource *r;
	int ret = 0;

	watchdog_init_timeout(&pnx4008_wdd, heartbeat, &pdev->dev);

	r = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	wdt_base = devm_ioremap_resource(&pdev->dev, r);
	if (IS_ERR(wdt_base))
		return PTR_ERR(wdt_base);

	wdt_clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(wdt_clk))
		return PTR_ERR(wdt_clk);

	ret = clk_enable(wdt_clk);
	if (ret)
		return ret;

	pnx4008_wdd.bootstatus = (readl(WDTIM_RES(wdt_base)) & WDOG_RESET) ?
			WDIOF_CARDRESET : 0;
	watchdog_set_nowayout(&pnx4008_wdd, nowayout);

	pnx4008_wdt_stop(&pnx4008_wdd);	/* disable for now */

	ret = watchdog_register_device(&pnx4008_wdd);
	if (ret < 0) {
		dev_err(&pdev->dev, "cannot register watchdog device\n");
		goto disable_clk;
	}

	dev_info(&pdev->dev, "PNX4008 Watchdog Timer: heartbeat %d sec\n",
		 pnx4008_wdd.timeout);

	return 0;

disable_clk:
	clk_disable(wdt_clk);
	return ret;
}

static int pnx4008_wdt_remove(struct platform_device *pdev)
{
	watchdog_unregister_device(&pnx4008_wdd);

	clk_disable(wdt_clk);

	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id pnx4008_wdt_match[] = {
	{ .compatible = "nxp,pnx4008-wdt" },
	{ }
};
MODULE_DEVICE_TABLE(of, pnx4008_wdt_match);
#endif

static struct platform_driver platform_wdt_driver = {
	.driver = {
		.name = "pnx4008-watchdog",
		.owner	= THIS_MODULE,
		.of_match_table = of_match_ptr(pnx4008_wdt_match),
	},
	.probe = pnx4008_wdt_probe,
	.remove = pnx4008_wdt_remove,
};

module_platform_driver(platform_wdt_driver);

MODULE_AUTHOR("MontaVista Software, Inc. <source@mvista.com>");
MODULE_AUTHOR("Wolfram Sang <w.sang@pengutronix.de>");
MODULE_DESCRIPTION("PNX4008 Watchdog Driver");

module_param(heartbeat, uint, 0);
MODULE_PARM_DESC(heartbeat,
		 "Watchdog heartbeat period in seconds from 1 to "
		 __MODULE_STRING(MAX_HEARTBEAT) ", default "
		 __MODULE_STRING(DEFAULT_HEARTBEAT));

module_param(nowayout, bool, 0);
MODULE_PARM_DESC(nowayout,
		 "Set to 1 to keep watchdog running after device release");

MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:pnx4008-watchdog");

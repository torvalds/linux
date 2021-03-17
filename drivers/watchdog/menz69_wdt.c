// SPDX-License-Identifier: GPL-2.0
/*
 * Watchdog driver for the MEN z069 IP-Core
 *
 * Copyright (C) 2018 Johannes Thumshirn <jth@kernel.org>
 */
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/mcb.h>
#include <linux/module.h>
#include <linux/watchdog.h>

struct men_z069_drv {
	struct watchdog_device wdt;
	void __iomem *base;
	struct resource *mem;
};

#define MEN_Z069_WTR			0x10
#define MEN_Z069_WTR_WDEN		BIT(15)
#define MEN_Z069_WTR_WDET_MASK		0x7fff
#define MEN_Z069_WVR			0x14

#define MEN_Z069_TIMER_FREQ		500 /* 500 Hz */
#define MEN_Z069_WDT_COUNTER_MIN	1
#define MEN_Z069_WDT_COUNTER_MAX	0x7fff
#define MEN_Z069_DEFAULT_TIMEOUT	30

static bool nowayout = WATCHDOG_NOWAYOUT;
module_param(nowayout, bool, 0);
MODULE_PARM_DESC(nowayout, "Watchdog cannot be stopped once started (default="
			    __MODULE_STRING(WATCHDOG_NOWAYOUT) ")");

static int men_z069_wdt_start(struct watchdog_device *wdt)
{
	struct men_z069_drv *drv = watchdog_get_drvdata(wdt);
	u16 val;

	val = readw(drv->base + MEN_Z069_WTR);
	val |= MEN_Z069_WTR_WDEN;
	writew(val, drv->base + MEN_Z069_WTR);

	return 0;
}

static int men_z069_wdt_stop(struct watchdog_device *wdt)
{
	struct men_z069_drv *drv = watchdog_get_drvdata(wdt);
	u16 val;

	val = readw(drv->base + MEN_Z069_WTR);
	val &= ~MEN_Z069_WTR_WDEN;
	writew(val, drv->base + MEN_Z069_WTR);

	return 0;
}

static int men_z069_wdt_ping(struct watchdog_device *wdt)
{
	struct men_z069_drv *drv = watchdog_get_drvdata(wdt);
	u16 val;

	/* The watchdog trigger value toggles between 0x5555 and 0xaaaa */
	val = readw(drv->base + MEN_Z069_WVR);
	val ^= 0xffff;
	writew(val, drv->base + MEN_Z069_WVR);

	return 0;
}

static int men_z069_wdt_set_timeout(struct watchdog_device *wdt,
				    unsigned int timeout)
{
	struct men_z069_drv *drv = watchdog_get_drvdata(wdt);
	u16 reg, val, ena;

	wdt->timeout = timeout;
	val = timeout * MEN_Z069_TIMER_FREQ;

	reg = readw(drv->base + MEN_Z069_WVR);
	ena = reg & MEN_Z069_WTR_WDEN;
	reg = ena | val;
	writew(reg, drv->base + MEN_Z069_WTR);

	return 0;
}

static const struct watchdog_info men_z069_info = {
	.options = WDIOF_SETTIMEOUT | WDIOF_KEEPALIVEPING | WDIOF_MAGICCLOSE,
	.identity = "MEN z069 Watchdog",
};

static const struct watchdog_ops men_z069_ops = {
	.owner = THIS_MODULE,
	.start = men_z069_wdt_start,
	.stop = men_z069_wdt_stop,
	.ping = men_z069_wdt_ping,
	.set_timeout = men_z069_wdt_set_timeout,
};

static struct watchdog_device men_z069_wdt = {
	.info = &men_z069_info,
	.ops = &men_z069_ops,
	.timeout = MEN_Z069_DEFAULT_TIMEOUT,
	.min_timeout = 1,
	.max_timeout = MEN_Z069_WDT_COUNTER_MAX / MEN_Z069_TIMER_FREQ,
};

static int men_z069_probe(struct mcb_device *dev,
			  const struct mcb_device_id *id)
{
	struct men_z069_drv *drv;
	struct resource *mem;

	drv = devm_kzalloc(&dev->dev, sizeof(struct men_z069_drv), GFP_KERNEL);
	if (!drv)
		return -ENOMEM;

	mem = mcb_request_mem(dev, "z069-wdt");
	if (IS_ERR(mem))
		return PTR_ERR(mem);

	drv->base = devm_ioremap(&dev->dev, mem->start, resource_size(mem));
	if (drv->base == NULL)
		goto release_mem;

	drv->mem = mem;

	drv->wdt = men_z069_wdt;
	watchdog_init_timeout(&drv->wdt, 0, &dev->dev);
	watchdog_set_nowayout(&drv->wdt, nowayout);
	watchdog_set_drvdata(&drv->wdt, drv);
	drv->wdt.parent = &dev->dev;
	mcb_set_drvdata(dev, drv);

	return watchdog_register_device(&men_z069_wdt);

release_mem:
	mcb_release_mem(mem);
	return -ENOMEM;
}

static void men_z069_remove(struct mcb_device *dev)
{
	struct men_z069_drv *drv = mcb_get_drvdata(dev);

	watchdog_unregister_device(&drv->wdt);
	mcb_release_mem(drv->mem);
}

static const struct mcb_device_id men_z069_ids[] = {
	{ .device = 0x45 },
	{ }
};
MODULE_DEVICE_TABLE(mcb, men_z069_ids);

static struct mcb_driver men_z069_driver = {
	.driver = {
		.name = "z069-wdt",
		.owner = THIS_MODULE,
	},
	.probe = men_z069_probe,
	.remove = men_z069_remove,
	.id_table = men_z069_ids,
};
module_mcb_driver(men_z069_driver);

MODULE_AUTHOR("Johannes Thumshirn <jth@kernel.org>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("mcb:16z069");
MODULE_IMPORT_NS(MCB);

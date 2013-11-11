/*
 * drivers/char/watchdog/max63xx_wdt.c
 *
 * Driver for max63{69,70,71,72,73,74} watchdog timers
 *
 * Copyright (C) 2009 Marc Zyngier <maz@misterjones.org>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 *
 * This driver assumes the watchdog pins are memory mapped (as it is
 * the case for the Arcom Zeus). Should it be connected over GPIOs or
 * another interface, some abstraction will have to be introduced.
 */

#include <linux/err.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/miscdevice.h>
#include <linux/watchdog.h>
#include <linux/init.h>
#include <linux/bitops.h>
#include <linux/platform_device.h>
#include <linux/spinlock.h>
#include <linux/io.h>
#include <linux/slab.h>

#define DEFAULT_HEARTBEAT 60
#define MAX_HEARTBEAT     60

static unsigned int heartbeat = DEFAULT_HEARTBEAT;
static bool nowayout  = WATCHDOG_NOWAYOUT;

/*
 * Memory mapping: a single byte, 3 first lower bits to select bit 3
 * to ping the watchdog.
 */
#define MAX6369_WDSET	(7 << 0)
#define MAX6369_WDI	(1 << 3)

static DEFINE_SPINLOCK(io_lock);

static int nodelay;
static void __iomem	*wdt_base;

/*
 * The timeout values used are actually the absolute minimum the chip
 * offers. Typical values on my board are slightly over twice as long
 * (10s setting ends up with a 25s timeout), and can be up to 3 times
 * the nominal setting (according to the datasheet). So please take
 * these values with a grain of salt. Same goes for the initial delay
 * "feature". Only max6373/74 have a few settings without this initial
 * delay (selected with the "nodelay" parameter).
 *
 * I also decided to remove from the tables any timeout smaller than a
 * second, as it looked completly overkill...
 */

/* Timeouts in second */
struct max63xx_timeout {
	u8 wdset;
	u8 tdelay;
	u8 twd;
};

static struct max63xx_timeout max6369_table[] = {
	{ 5,  1,  1 },
	{ 6, 10, 10 },
	{ 7, 60, 60 },
	{ },
};

static struct max63xx_timeout max6371_table[] = {
	{ 6, 60,  3 },
	{ 7, 60, 60 },
	{ },
};

static struct max63xx_timeout max6373_table[] = {
	{ 2, 60,  1 },
	{ 5,  0,  1 },
	{ 1,  3,  3 },
	{ 7, 60, 10 },
	{ 6,  0, 10 },
	{ },
};

static struct max63xx_timeout *current_timeout;

static struct max63xx_timeout *
max63xx_select_timeout(struct max63xx_timeout *table, int value)
{
	while (table->twd) {
		if (value <= table->twd) {
			if (nodelay && table->tdelay == 0)
				return table;

			if (!nodelay)
				return table;
		}

		table++;
	}

	return NULL;
}

static int max63xx_wdt_ping(struct watchdog_device *wdd)
{
	u8 val;

	spin_lock(&io_lock);

	val = __raw_readb(wdt_base);

	__raw_writeb(val | MAX6369_WDI, wdt_base);
	__raw_writeb(val & ~MAX6369_WDI, wdt_base);

	spin_unlock(&io_lock);
	return 0;
}

static int max63xx_wdt_start(struct watchdog_device *wdd)
{
	struct max63xx_timeout *entry = watchdog_get_drvdata(wdd);
	u8 val;

	spin_lock(&io_lock);

	val = __raw_readb(wdt_base);
	val &= ~MAX6369_WDSET;
	val |= entry->wdset;
	__raw_writeb(val, wdt_base);

	spin_unlock(&io_lock);

	/* check for a edge triggered startup */
	if (entry->tdelay == 0)
		max63xx_wdt_ping(wdd);
	return 0;
}

static int max63xx_wdt_stop(struct watchdog_device *wdd)
{
	u8 val;

	spin_lock(&io_lock);

	val = __raw_readb(wdt_base);
	val &= ~MAX6369_WDSET;
	val |= 3;
	__raw_writeb(val, wdt_base);

	spin_unlock(&io_lock);
	return 0;
}

static const struct watchdog_info max63xx_wdt_info = {
	.options = WDIOF_KEEPALIVEPING | WDIOF_MAGICCLOSE,
	.identity = "max63xx Watchdog",
};

static const struct watchdog_ops max63xx_wdt_ops = {
	.owner = THIS_MODULE,
	.start = max63xx_wdt_start,
	.stop = max63xx_wdt_stop,
	.ping = max63xx_wdt_ping,
};

static struct watchdog_device max63xx_wdt_dev = {
	.info = &max63xx_wdt_info,
	.ops = &max63xx_wdt_ops,
};

static int max63xx_wdt_probe(struct platform_device *pdev)
{
	struct resource	*wdt_mem;
	struct max63xx_timeout *table;

	table = (struct max63xx_timeout *)pdev->id_entry->driver_data;

	if (heartbeat < 1 || heartbeat > MAX_HEARTBEAT)
		heartbeat = DEFAULT_HEARTBEAT;

	dev_info(&pdev->dev, "requesting %ds heartbeat\n", heartbeat);
	current_timeout = max63xx_select_timeout(table, heartbeat);

	if (!current_timeout) {
		dev_err(&pdev->dev, "unable to satisfy heartbeat request\n");
		return -EINVAL;
	}

	dev_info(&pdev->dev, "using %ds heartbeat with %ds initial delay\n",
		 current_timeout->twd, current_timeout->tdelay);

	heartbeat = current_timeout->twd;

	wdt_mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	wdt_base = devm_ioremap_resource(&pdev->dev, wdt_mem);
	if (IS_ERR(wdt_base))
		return PTR_ERR(wdt_base);

	max63xx_wdt_dev.timeout = heartbeat;
	watchdog_set_nowayout(&max63xx_wdt_dev, nowayout);
	watchdog_set_drvdata(&max63xx_wdt_dev, current_timeout);

	return watchdog_register_device(&max63xx_wdt_dev);
}

static int max63xx_wdt_remove(struct platform_device *pdev)
{
	watchdog_unregister_device(&max63xx_wdt_dev);
	return 0;
}

static struct platform_device_id max63xx_id_table[] = {
	{ "max6369_wdt", (kernel_ulong_t)max6369_table, },
	{ "max6370_wdt", (kernel_ulong_t)max6369_table, },
	{ "max6371_wdt", (kernel_ulong_t)max6371_table, },
	{ "max6372_wdt", (kernel_ulong_t)max6371_table, },
	{ "max6373_wdt", (kernel_ulong_t)max6373_table, },
	{ "max6374_wdt", (kernel_ulong_t)max6373_table, },
	{ },
};
MODULE_DEVICE_TABLE(platform, max63xx_id_table);

static struct platform_driver max63xx_wdt_driver = {
	.probe		= max63xx_wdt_probe,
	.remove		= max63xx_wdt_remove,
	.id_table	= max63xx_id_table,
	.driver		= {
		.name	= "max63xx_wdt",
		.owner	= THIS_MODULE,
	},
};

module_platform_driver(max63xx_wdt_driver);

MODULE_AUTHOR("Marc Zyngier <maz@misterjones.org>");
MODULE_DESCRIPTION("max63xx Watchdog Driver");

module_param(heartbeat, int, 0);
MODULE_PARM_DESC(heartbeat,
		 "Watchdog heartbeat period in seconds from 1 to "
		 __MODULE_STRING(MAX_HEARTBEAT) ", default "
		 __MODULE_STRING(DEFAULT_HEARTBEAT));

module_param(nowayout, bool, 0);
MODULE_PARM_DESC(nowayout, "Watchdog cannot be stopped once started (default="
		 __MODULE_STRING(WATCHDOG_NOWAYOUT) ")");

module_param(nodelay, int, 0);
MODULE_PARM_DESC(nodelay,
		 "Force selection of a timeout setting without initial delay "
		 "(max6373/74 only, default=0)");

MODULE_LICENSE("GPL");
MODULE_ALIAS_MISCDEV(WATCHDOG_MINOR);

// SPDX-License-Identifier: GPL-2.0+
/*
 * Watchdog driver for TQMx86 PLD.
 *
 * The watchdog supports power of 2 timeouts from 1 to 4096sec.
 * Once started, it cannot be stopped.
 *
 * Based on the vendor code written by Vadim V.Vlasov
 * <vvlasov@dev.rtsoft.ru>
 */

#include <linux/io.h>
#include <linux/log2.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/timer.h>
#include <linux/watchdog.h>

/* default timeout (secs) */
#define WDT_TIMEOUT 32

static unsigned int timeout;
module_param(timeout, uint, 0);
MODULE_PARM_DESC(timeout,
	"Watchdog timeout in seconds. (1<=timeout<=4096, default="
				__MODULE_STRING(WDT_TIMEOUT) ")");
struct tqmx86_wdt {
	struct watchdog_device wdd;
	void __iomem *io_base;
};

#define TQMX86_WDCFG	0x00 /* Watchdog Configuration Register */
#define TQMX86_WDCS	0x01 /* Watchdog Config/Status Register */

static int tqmx86_wdt_start(struct watchdog_device *wdd)
{
	struct tqmx86_wdt *priv = watchdog_get_drvdata(wdd);

	iowrite8(0x81, priv->io_base + TQMX86_WDCS);

	return 0;
}

static int tqmx86_wdt_set_timeout(struct watchdog_device *wdd, unsigned int t)
{
	struct tqmx86_wdt *priv = watchdog_get_drvdata(wdd);
	u8 val;

	t = roundup_pow_of_two(t);
	val = ilog2(t) | 0x90;
	val += 3; /* values 0,1,2 correspond to 0.125,0.25,0.5s timeouts */
	iowrite8(val, priv->io_base + TQMX86_WDCFG);

	wdd->timeout = t;

	return 0;
}

static const struct watchdog_info tqmx86_wdt_info = {
	.options	= WDIOF_SETTIMEOUT |
			  WDIOF_KEEPALIVEPING,
	.identity	= "TQMx86 Watchdog",
};

static const struct watchdog_ops tqmx86_wdt_ops = {
	.owner		= THIS_MODULE,
	.start		= tqmx86_wdt_start,
	.set_timeout	= tqmx86_wdt_set_timeout,
};

static int tqmx86_wdt_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct tqmx86_wdt *priv;
	struct resource *res;
	int err;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_IO, 0);
	if (!res)
		return -ENODEV;

	priv->io_base = devm_ioport_map(dev, res->start, resource_size(res));
	if (!priv->io_base)
		return -ENOMEM;

	watchdog_set_drvdata(&priv->wdd, priv);

	priv->wdd.parent = dev;
	priv->wdd.info = &tqmx86_wdt_info;
	priv->wdd.ops = &tqmx86_wdt_ops;
	priv->wdd.min_timeout = 1;
	priv->wdd.max_timeout = 4096;
	priv->wdd.max_hw_heartbeat_ms = 4096*1000;
	priv->wdd.timeout = WDT_TIMEOUT;

	watchdog_init_timeout(&priv->wdd, timeout, dev);
	watchdog_set_nowayout(&priv->wdd, WATCHDOG_NOWAYOUT);

	tqmx86_wdt_set_timeout(&priv->wdd, priv->wdd.timeout);

	err = devm_watchdog_register_device(dev, &priv->wdd);
	if (err)
		return err;

	dev_info(dev, "TQMx86 watchdog\n");

	return 0;
}

static struct platform_driver tqmx86_wdt_driver = {
	.driver		= {
		.name	= "tqmx86-wdt",
	},
	.probe		= tqmx86_wdt_probe,
};

module_platform_driver(tqmx86_wdt_driver);

MODULE_AUTHOR("Andrew Lunn <andrew@lunn.ch>");
MODULE_DESCRIPTION("TQMx86 Watchdog");
MODULE_ALIAS("platform:tqmx86-wdt");
MODULE_LICENSE("GPL");

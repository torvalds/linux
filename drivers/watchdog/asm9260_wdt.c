/*
 * Watchdog driver for Alphascale ASM9260.
 *
 * Copyright (c) 2014 Oleksij Rempel <linux@rempel-privat.de>
 *
 * Licensed under GPLv2 or later.
 */

#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/reboot.h>
#include <linux/reset.h>
#include <linux/watchdog.h>

#define CLOCK_FREQ	1000000

/* Watchdog Mode register */
#define HW_WDMOD			0x00
/* Wake interrupt. Set by HW, can't be cleared. */
#define BM_MOD_WDINT			BIT(3)
/* This bit set if timeout reached. Cleared by SW. */
#define BM_MOD_WDTOF			BIT(2)
/* HW Reset on timeout */
#define BM_MOD_WDRESET			BIT(1)
/* WD enable */
#define BM_MOD_WDEN			BIT(0)

/*
 * Watchdog Timer Constant register
 * Minimal value is 0xff, the meaning of this value
 * depends on used clock: T = WDCLK * (0xff + 1) * 4
 */
#define HW_WDTC				0x04
#define BM_WDTC_MAX(freq)		(0x7fffffff / (freq))

/* Watchdog Feed register */
#define HW_WDFEED			0x08

/* Watchdog Timer Value register */
#define HW_WDTV				0x0c

#define ASM9260_WDT_DEFAULT_TIMEOUT	30

enum asm9260_wdt_mode {
	HW_RESET,
	SW_RESET,
	DEBUG,
};

struct asm9260_wdt_priv {
	struct device		*dev;
	struct watchdog_device	wdd;
	struct clk		*clk;
	struct clk		*clk_ahb;
	struct reset_control	*rst;
	struct notifier_block	restart_handler;

	void __iomem		*iobase;
	int			irq;
	unsigned long		wdt_freq;
	enum asm9260_wdt_mode	mode;
};

static int asm9260_wdt_feed(struct watchdog_device *wdd)
{
	struct asm9260_wdt_priv *priv = watchdog_get_drvdata(wdd);

	iowrite32(0xaa, priv->iobase + HW_WDFEED);
	iowrite32(0x55, priv->iobase + HW_WDFEED);

	return 0;
}

static unsigned int asm9260_wdt_gettimeleft(struct watchdog_device *wdd)
{
	struct asm9260_wdt_priv *priv = watchdog_get_drvdata(wdd);
	u32 counter;

	counter = ioread32(priv->iobase + HW_WDTV);

	return DIV_ROUND_CLOSEST(counter, priv->wdt_freq);
}

static int asm9260_wdt_updatetimeout(struct watchdog_device *wdd)
{
	struct asm9260_wdt_priv *priv = watchdog_get_drvdata(wdd);
	u32 counter;

	counter = wdd->timeout * priv->wdt_freq;

	iowrite32(counter, priv->iobase + HW_WDTC);

	return 0;
}

static int asm9260_wdt_enable(struct watchdog_device *wdd)
{
	struct asm9260_wdt_priv *priv = watchdog_get_drvdata(wdd);
	u32 mode = 0;

	if (priv->mode == HW_RESET)
		mode = BM_MOD_WDRESET;

	iowrite32(BM_MOD_WDEN | mode, priv->iobase + HW_WDMOD);

	asm9260_wdt_updatetimeout(wdd);

	asm9260_wdt_feed(wdd);

	return 0;
}

static int asm9260_wdt_disable(struct watchdog_device *wdd)
{
	struct asm9260_wdt_priv *priv = watchdog_get_drvdata(wdd);

	/* The only way to disable WD is to reset it. */
	reset_control_assert(priv->rst);
	reset_control_deassert(priv->rst);

	return 0;
}

static int asm9260_wdt_settimeout(struct watchdog_device *wdd, unsigned int to)
{
	wdd->timeout = to;
	asm9260_wdt_updatetimeout(wdd);

	return 0;
}

static void asm9260_wdt_sys_reset(struct asm9260_wdt_priv *priv)
{
	/* init WD if it was not started */

	iowrite32(BM_MOD_WDEN | BM_MOD_WDRESET, priv->iobase + HW_WDMOD);

	iowrite32(0xff, priv->iobase + HW_WDTC);
	/* first pass correct sequence */
	asm9260_wdt_feed(&priv->wdd);
	/*
	 * Then write wrong pattern to the feed to trigger reset
	 * ASAP.
	 */
	iowrite32(0xff, priv->iobase + HW_WDFEED);

	mdelay(1000);
}

static irqreturn_t asm9260_wdt_irq(int irq, void *devid)
{
	struct asm9260_wdt_priv *priv = devid;
	u32 stat;

	stat = ioread32(priv->iobase + HW_WDMOD);
	if (!(stat & BM_MOD_WDINT))
		return IRQ_NONE;

	if (priv->mode == DEBUG) {
		dev_info(priv->dev, "Watchdog Timeout. Do nothing.\n");
	} else {
		dev_info(priv->dev, "Watchdog Timeout. Doing SW Reset.\n");
		asm9260_wdt_sys_reset(priv);
	}

	return IRQ_HANDLED;
}

static int asm9260_restart_handler(struct notifier_block *this,
				   unsigned long mode, void *cmd)
{
	struct asm9260_wdt_priv *priv =
		container_of(this, struct asm9260_wdt_priv, restart_handler);

	asm9260_wdt_sys_reset(priv);

	return NOTIFY_DONE;
}

static const struct watchdog_info asm9260_wdt_ident = {
	.options          =     WDIOF_SETTIMEOUT | WDIOF_KEEPALIVEPING
				| WDIOF_MAGICCLOSE,
	.identity         =	"Alphascale asm9260 Watchdog",
};

static struct watchdog_ops asm9260_wdt_ops = {
	.owner		= THIS_MODULE,
	.start		= asm9260_wdt_enable,
	.stop		= asm9260_wdt_disable,
	.get_timeleft	= asm9260_wdt_gettimeleft,
	.ping		= asm9260_wdt_feed,
	.set_timeout	= asm9260_wdt_settimeout,
};

static int asm9260_wdt_get_dt_clks(struct asm9260_wdt_priv *priv)
{
	int err;
	unsigned long clk;

	priv->clk = devm_clk_get(priv->dev, "mod");
	if (IS_ERR(priv->clk)) {
		dev_err(priv->dev, "Failed to get \"mod\" clk\n");
		return PTR_ERR(priv->clk);
	}

	/* configure AHB clock */
	priv->clk_ahb = devm_clk_get(priv->dev, "ahb");
	if (IS_ERR(priv->clk_ahb)) {
		dev_err(priv->dev, "Failed to get \"ahb\" clk\n");
		return PTR_ERR(priv->clk_ahb);
	}

	err = clk_prepare_enable(priv->clk_ahb);
	if (err) {
		dev_err(priv->dev, "Failed to enable ahb_clk!\n");
		return err;
	}

	err = clk_set_rate(priv->clk, CLOCK_FREQ);
	if (err) {
		clk_disable_unprepare(priv->clk_ahb);
		dev_err(priv->dev, "Failed to set rate!\n");
		return err;
	}

	err = clk_prepare_enable(priv->clk);
	if (err) {
		clk_disable_unprepare(priv->clk_ahb);
		dev_err(priv->dev, "Failed to enable clk!\n");
		return err;
	}

	/* wdt has internal divider */
	clk = clk_get_rate(priv->clk);
	if (!clk) {
		clk_disable_unprepare(priv->clk);
		clk_disable_unprepare(priv->clk_ahb);
		dev_err(priv->dev, "Failed, clk is 0!\n");
		return -EINVAL;
	}

	priv->wdt_freq = clk / 2;

	return 0;
}

static void asm9260_wdt_get_dt_mode(struct asm9260_wdt_priv *priv)
{
	const char *tmp;
	int ret;

	/* default mode */
	priv->mode = HW_RESET;

	ret = of_property_read_string(priv->dev->of_node,
				      "alphascale,mode", &tmp);
	if (ret < 0)
		return;

	if (!strcmp(tmp, "hw"))
		priv->mode = HW_RESET;
	else if (!strcmp(tmp, "sw"))
		priv->mode = SW_RESET;
	else if (!strcmp(tmp, "debug"))
		priv->mode = DEBUG;
	else
		dev_warn(priv->dev, "unknown reset-type: %s. Using default \"hw\" mode.",
			 tmp);
}

static int asm9260_wdt_probe(struct platform_device *pdev)
{
	struct asm9260_wdt_priv *priv;
	struct watchdog_device *wdd;
	struct resource *res;
	int ret;
	const char * const mode_name[] = { "hw", "sw", "debug", };

	priv = devm_kzalloc(&pdev->dev, sizeof(struct asm9260_wdt_priv),
			    GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->dev = &pdev->dev;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	priv->iobase = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(priv->iobase))
		return PTR_ERR(priv->iobase);

	ret = asm9260_wdt_get_dt_clks(priv);
	if (ret)
		return ret;

	priv->rst = devm_reset_control_get(&pdev->dev, "wdt_rst");
	if (IS_ERR(priv->rst))
		return PTR_ERR(priv->rst);

	wdd = &priv->wdd;
	wdd->info = &asm9260_wdt_ident;
	wdd->ops = &asm9260_wdt_ops;
	wdd->min_timeout = 1;
	wdd->max_timeout = BM_WDTC_MAX(priv->wdt_freq);
	wdd->parent = &pdev->dev;

	watchdog_set_drvdata(wdd, priv);

	/*
	 * If 'timeout-sec' unspecified in devicetree, assume a 30 second
	 * default, unless the max timeout is less than 30 seconds, then use
	 * the max instead.
	 */
	wdd->timeout = ASM9260_WDT_DEFAULT_TIMEOUT;
	watchdog_init_timeout(wdd, 0, &pdev->dev);

	asm9260_wdt_get_dt_mode(priv);

	if (priv->mode != HW_RESET)
		priv->irq = platform_get_irq(pdev, 0);

	if (priv->irq > 0) {
		/*
		 * Not all supported platforms specify an interrupt for the
		 * watchdog, so let's make it optional.
		 */
		ret = devm_request_irq(&pdev->dev, priv->irq,
				       asm9260_wdt_irq, 0, pdev->name, priv);
		if (ret < 0)
			dev_warn(&pdev->dev, "failed to request IRQ\n");
	}

	ret = watchdog_register_device(wdd);
	if (ret)
		goto clk_off;

	platform_set_drvdata(pdev, priv);

	priv->restart_handler.notifier_call = asm9260_restart_handler;
	priv->restart_handler.priority = 128;
	ret = register_restart_handler(&priv->restart_handler);
	if (ret)
		dev_warn(&pdev->dev, "cannot register restart handler\n");

	dev_info(&pdev->dev, "Watchdog enabled (timeout: %d sec, mode: %s)\n",
		 wdd->timeout, mode_name[priv->mode]);
	return 0;

clk_off:
	clk_disable_unprepare(priv->clk);
	clk_disable_unprepare(priv->clk_ahb);
	return ret;
}

static void asm9260_wdt_shutdown(struct platform_device *pdev)
{
	struct asm9260_wdt_priv *priv = platform_get_drvdata(pdev);

	asm9260_wdt_disable(&priv->wdd);
}

static int asm9260_wdt_remove(struct platform_device *pdev)
{
	struct asm9260_wdt_priv *priv = platform_get_drvdata(pdev);

	asm9260_wdt_disable(&priv->wdd);

	unregister_restart_handler(&priv->restart_handler);

	watchdog_unregister_device(&priv->wdd);

	clk_disable_unprepare(priv->clk);
	clk_disable_unprepare(priv->clk_ahb);

	return 0;
}

static const struct of_device_id asm9260_wdt_of_match[] = {
	{ .compatible = "alphascale,asm9260-wdt"},
	{},
};
MODULE_DEVICE_TABLE(of, asm9260_wdt_of_match);

static struct platform_driver asm9260_wdt_driver = {
	.driver = {
		.name = "asm9260-wdt",
		.of_match_table	= asm9260_wdt_of_match,
	},
	.probe = asm9260_wdt_probe,
	.remove = asm9260_wdt_remove,
	.shutdown = asm9260_wdt_shutdown,
};
module_platform_driver(asm9260_wdt_driver);

MODULE_DESCRIPTION("asm9260 WatchDog Timer Driver");
MODULE_AUTHOR("Oleksij Rempel <linux@rempel-privat.de>");
MODULE_LICENSE("GPL");

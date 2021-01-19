// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2018 Nuvoton Technology corporation.
// Copyright (c) 2018 IBM Corp.

#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/watchdog.h>
#include <linux/regmap.h>
#include <linux/mfd/syscon.h>

/* NPCM7xx GCR module */
#define NPCM7XX_RESSR_OFFSET		0x6C
#define NPCM7XX_INTCR2_OFFSET		0x60

#define NPCM7XX_PORST			BIT(31)
#define NPCM7XX_CORST			BIT(30)
#define NPCM7XX_WD0RST			BIT(29)
#define NPCM7XX_WD1RST			BIT(24)
#define NPCM7XX_WD2RST			BIT(23)
#define NPCM7XX_SWR1RST			BIT(28)
#define NPCM7XX_SWR2RST			BIT(27)
#define NPCM7XX_SWR3RST			BIT(26)
#define NPCM7XX_SWR4RST			BIT(25)

 /* WD register */
#define NPCM_WTCR	0x1C

#define NPCM_WTCLK	(BIT(10) | BIT(11))	/* Clock divider */
#define NPCM_WTE	BIT(7)			/* Enable */
#define NPCM_WTIE	BIT(6)			/* Enable irq */
#define NPCM_WTIS	(BIT(4) | BIT(5))	/* Interval selection */
#define NPCM_WTIF	BIT(3)			/* Interrupt flag*/
#define NPCM_WTRF	BIT(2)			/* Reset flag */
#define NPCM_WTRE	BIT(1)			/* Reset enable */
#define NPCM_WTR	BIT(0)			/* Reset counter */

/*
 * Watchdog timeouts
 *
 * 170     msec:    WTCLK=01 WTIS=00     VAL= 0x400
 * 670     msec:    WTCLK=01 WTIS=01     VAL= 0x410
 * 1360    msec:    WTCLK=10 WTIS=00     VAL= 0x800
 * 2700    msec:    WTCLK=01 WTIS=10     VAL= 0x420
 * 5360    msec:    WTCLK=10 WTIS=01     VAL= 0x810
 * 10700   msec:    WTCLK=01 WTIS=11     VAL= 0x430
 * 21600   msec:    WTCLK=10 WTIS=10     VAL= 0x820
 * 43000   msec:    WTCLK=11 WTIS=00     VAL= 0xC00
 * 85600   msec:    WTCLK=10 WTIS=11     VAL= 0x830
 * 172000  msec:    WTCLK=11 WTIS=01     VAL= 0xC10
 * 687000  msec:    WTCLK=11 WTIS=10     VAL= 0xC20
 * 2750000 msec:    WTCLK=11 WTIS=11     VAL= 0xC30
 */

struct npcm_wdt {
	struct watchdog_device	wdd;
	void __iomem		*reg;
	u32			card_reset;
	u32			ext1_reset;
	u32			ext2_reset;
};

static inline struct npcm_wdt *to_npcm_wdt(struct watchdog_device *wdd)
{
	return container_of(wdd, struct npcm_wdt, wdd);
}

static int npcm_wdt_ping(struct watchdog_device *wdd)
{
	struct npcm_wdt *wdt = to_npcm_wdt(wdd);
	u32 val;

	val = readl(wdt->reg);
	writel(val | NPCM_WTR, wdt->reg);

	return 0;
}

static int npcm_wdt_start(struct watchdog_device *wdd)
{
	struct npcm_wdt *wdt = to_npcm_wdt(wdd);
	u32 val;

	if (wdd->timeout < 2)
		val = 0x800;
	else if (wdd->timeout < 3)
		val = 0x420;
	else if (wdd->timeout < 6)
		val = 0x810;
	else if (wdd->timeout < 11)
		val = 0x430;
	else if (wdd->timeout < 22)
		val = 0x820;
	else if (wdd->timeout < 44)
		val = 0xC00;
	else if (wdd->timeout < 87)
		val = 0x830;
	else if (wdd->timeout < 173)
		val = 0xC10;
	else if (wdd->timeout < 688)
		val = 0xC20;
	else
		val = 0xC30;

	val |= NPCM_WTRE | NPCM_WTE | NPCM_WTR | NPCM_WTIE;

	writel(val, wdt->reg);

	return 0;
}

static int npcm_wdt_stop(struct watchdog_device *wdd)
{
	struct npcm_wdt *wdt = to_npcm_wdt(wdd);

	writel(0, wdt->reg);

	return 0;
}

static int npcm_wdt_set_timeout(struct watchdog_device *wdd,
				unsigned int timeout)
{
	if (timeout < 2)
		wdd->timeout = 1;
	else if (timeout < 3)
		wdd->timeout = 2;
	else if (timeout < 6)
		wdd->timeout = 5;
	else if (timeout < 11)
		wdd->timeout = 10;
	else if (timeout < 22)
		wdd->timeout = 21;
	else if (timeout < 44)
		wdd->timeout = 43;
	else if (timeout < 87)
		wdd->timeout = 86;
	else if (timeout < 173)
		wdd->timeout = 172;
	else if (timeout < 688)
		wdd->timeout = 687;
	else
		wdd->timeout = 2750;

	if (watchdog_active(wdd))
		npcm_wdt_start(wdd);

	return 0;
}

static irqreturn_t npcm_wdt_interrupt(int irq, void *data)
{
	struct npcm_wdt *wdt = data;

	watchdog_notify_pretimeout(&wdt->wdd);

	return IRQ_HANDLED;
}

static int npcm_wdt_restart(struct watchdog_device *wdd,
			    unsigned long action, void *data)
{
	struct npcm_wdt *wdt = to_npcm_wdt(wdd);

	writel(NPCM_WTR | NPCM_WTRE | NPCM_WTE, wdt->reg);
	udelay(1000);

	return 0;
}

static bool npcm_is_running(struct watchdog_device *wdd)
{
	struct npcm_wdt *wdt = to_npcm_wdt(wdd);

	return readl(wdt->reg) & NPCM_WTE;
}

static const struct watchdog_info npcm_wdt_info = {
	.identity	= KBUILD_MODNAME,
	.options	= WDIOF_SETTIMEOUT
			| WDIOF_KEEPALIVEPING
			| WDIOF_MAGICCLOSE,
};

static const struct watchdog_ops npcm_wdt_ops = {
	.owner = THIS_MODULE,
	.start = npcm_wdt_start,
	.stop = npcm_wdt_stop,
	.ping = npcm_wdt_ping,
	.set_timeout = npcm_wdt_set_timeout,
	.restart = npcm_wdt_restart,
};

static void npcm_get_reset_status(struct npcm_wdt *wdt, struct device *dev)
{
	struct regmap *gcr_regmap;
	u32 rstval;

	gcr_regmap = syscon_regmap_lookup_by_phandle(dev->of_node, "syscon");
	if (IS_ERR(gcr_regmap)) {
		dev_warn(dev, "Failed to find gcr syscon, WD reset status not supported\n");
		return;
	}

	regmap_read(gcr_regmap, NPCM7XX_RESSR_OFFSET, &rstval);
	if (!rstval) {
		regmap_read(gcr_regmap, NPCM7XX_INTCR2_OFFSET, &rstval);
		rstval = ~rstval;
	}

	if (rstval & wdt->card_reset)
		wdt->wdd.bootstatus |= WDIOF_CARDRESET;
	if (rstval & wdt->ext1_reset)
		wdt->wdd.bootstatus |= WDIOF_EXTERN1;
	if (rstval & wdt->ext2_reset)
		wdt->wdd.bootstatus |= WDIOF_EXTERN2;
}

static u32 npcm_wdt_reset_type(const char *reset_type)
{
	if (!strcmp(reset_type, "porst"))
		return NPCM7XX_PORST;
	else if (!strcmp(reset_type, "corst"))
		return NPCM7XX_CORST;
	else if (!strcmp(reset_type, "wd0"))
		return NPCM7XX_WD0RST;
	else if (!strcmp(reset_type, "wd1"))
		return NPCM7XX_WD1RST;
	else if (!strcmp(reset_type, "wd2"))
		return NPCM7XX_WD2RST;
	else if (!strcmp(reset_type, "sw1"))
		return NPCM7XX_SWR1RST;
	else if (!strcmp(reset_type, "sw2"))
		return NPCM7XX_SWR2RST;
	else if (!strcmp(reset_type, "sw3"))
		return NPCM7XX_SWR3RST;
	else if (!strcmp(reset_type, "sw4"))
		return NPCM7XX_SWR4RST;

	return 0;
}

static int npcm_wdt_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	const char *card_reset_type;
	const char *ext1_reset_type;
	const char *ext2_reset_type;
	struct npcm_wdt *wdt;
	u32 priority;
	int irq;
	int ret;

	wdt = devm_kzalloc(&pdev->dev, sizeof(*wdt), GFP_KERNEL);
	if (!wdt)
		return -ENOMEM;

	wdt->reg = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(wdt->reg))
		return PTR_ERR(wdt->reg);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	if (of_property_read_u32(pdev->dev.of_node, "nuvoton,restart-priority",
				 &priority))
		watchdog_set_restart_priority(&wdt->wdd, 128);
	else
		watchdog_set_restart_priority(&wdt->wdd, priority);

	ret = of_property_read_string(pdev->dev.of_node,
				      "nuvoton,card-reset-type",
				      &card_reset_type);
	if (ret) {
		wdt->card_reset = NPCM7XX_PORST;
	} else {
		wdt->card_reset = npcm_wdt_reset_type(card_reset_type);
		if (!wdt->card_reset)
			wdt->card_reset = NPCM7XX_PORST;
	}

	ret = of_property_read_string(pdev->dev.of_node,
				      "nuvoton,ext1-reset-type",
				      &ext1_reset_type);
	if (ret) {
		wdt->ext1_reset = NPCM7XX_WD0RST;
	} else {
		wdt->ext1_reset = npcm_wdt_reset_type(ext1_reset_type);
		if (!wdt->ext1_reset)
			wdt->ext1_reset = NPCM7XX_WD0RST;
	}

	ret = of_property_read_string(pdev->dev.of_node,
				      "nuvoton,ext2-reset-type",
				      &ext2_reset_type);
	if (ret) {
		wdt->ext2_reset = NPCM7XX_SWR1RST;
	} else {
		wdt->ext2_reset = npcm_wdt_reset_type(ext2_reset_type);
		if (!wdt->ext2_reset)
			wdt->ext2_reset = NPCM7XX_SWR1RST;
	}

	wdt->wdd.info = &npcm_wdt_info;
	wdt->wdd.ops = &npcm_wdt_ops;
	wdt->wdd.min_timeout = 1;
	wdt->wdd.max_timeout = 2750;
	wdt->wdd.parent = dev;

	wdt->wdd.timeout = 86;
	watchdog_init_timeout(&wdt->wdd, 0, dev);

	/* Ensure timeout is able to be represented by the hardware */
	npcm_wdt_set_timeout(&wdt->wdd, wdt->wdd.timeout);

	if (npcm_is_running(&wdt->wdd)) {
		/* Restart with the default or device-tree specified timeout */
		npcm_wdt_start(&wdt->wdd);
		set_bit(WDOG_HW_RUNNING, &wdt->wdd.status);
	}

	npcm_get_reset_status(wdt, dev);
	ret = devm_request_irq(dev, irq, npcm_wdt_interrupt, 0, "watchdog",
			       wdt);
	if (ret)
		return ret;

	ret = devm_watchdog_register_device(dev, &wdt->wdd);
	if (ret)
		return ret;

	dev_info(dev, "NPCM watchdog driver enabled\n");

	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id npcm_wdt_match[] = {
	{.compatible = "nuvoton,wpcm450-wdt"},
	{.compatible = "nuvoton,npcm750-wdt"},
	{},
};
MODULE_DEVICE_TABLE(of, npcm_wdt_match);
#endif

static struct platform_driver npcm_wdt_driver = {
	.probe		= npcm_wdt_probe,
	.driver		= {
		.name	= "npcm-wdt",
		.of_match_table = of_match_ptr(npcm_wdt_match),
	},
};
module_platform_driver(npcm_wdt_driver);

MODULE_AUTHOR("Joel Stanley");
MODULE_DESCRIPTION("Watchdog driver for NPCM");
MODULE_LICENSE("GPL v2");

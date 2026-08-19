// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2018 Nuvoton Technology corporation.
// Copyright (c) 2018 IBM Corp.

#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/watchdog.h>

#define NPCM_WTCR	0x1C

/* NPCM GCR module */
#define NPCM_RESSR_OFFSET		0x6C
#define NPCM_INTCR2_OFFSET		0x60
#define NPCM7XX_SCRPAD2_OFFSET		0x84
#define NPCM8XX_SCRPAD10_OFFSET		0xE28

#define NPCM_PORST			BIT(31)
#define NPCM_CORST			BIT(30)
#define NPCM_WD0RST			BIT(29)
#define NPCM_SWR1RST			BIT(28)
#define NPCM_SWR2RST			BIT(27)
#define NPCM_SWR3RST			BIT(26)
#define NPCM_SWR4RST			BIT(25)
#define NPCM_WD1RST			BIT(24)
#define NPCM_WD2RST			BIT(23)
#define NPCM_RST			GENMASK(31, 23)
#define NPCM8XX_TIP_RESET		BIT(25) /* Replaces SWRST4 on NPCM8xx */

/* Per-instance mapping of MMIO base address to its RESSR/INTCR2 reset bit. */
struct npcm_wdt_rst_map {
	phys_addr_t	base;
	u32		rst_bit;
};

struct npcm_wdt_status_map {
	u32	rst_bit;
	u32	wdiof_flag;
};

struct npcm_wdt_data {
	const struct npcm_wdt_rst_map		*rst_map;
	unsigned int				rst_map_size;
	const struct npcm_wdt_status_map	*status_map;
	unsigned int				status_map_size;
};

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
	struct watchdog_device  wdd;
	void __iomem		*reg;
	struct clk		*clk;
};

static const struct npcm_wdt_rst_map npcm750_rst_map[] = {
	{ 0xf000801c, NPCM_WD0RST },
	{ 0xf000901c, NPCM_WD1RST },
	{ 0xf000a01c, NPCM_WD2RST },
};

static const struct npcm_wdt_status_map npcm750_status_map[] = {
	{ NPCM_PORST, WDIOF_OVERHEAT },
	{ NPCM_CORST, WDIOF_FANFAULT },
	{ NPCM_SWR1RST, WDIOF_EXTERN1 },
	{ NPCM_SWR2RST, WDIOF_EXTERN2 },
	{ NPCM_SWR3RST, WDIOF_POWERUNDER },
	{ NPCM_SWR4RST, WDIOF_POWEROVER },
};

static const struct npcm_wdt_data __maybe_unused npcm750_data = {
	.rst_map = npcm750_rst_map,
	.rst_map_size = ARRAY_SIZE(npcm750_rst_map),
	.status_map = npcm750_status_map,
	.status_map_size = ARRAY_SIZE(npcm750_status_map),
};

static const struct npcm_wdt_rst_map npcm845_rst_map[] = {
	{ 0xf000801c, NPCM_WD0RST },
	{ 0xf000901c, NPCM_WD1RST },
	{ 0xf000a01c, NPCM_WD2RST },
};

static const struct npcm_wdt_status_map npcm845_status_map[] = {
	{ NPCM_PORST, WDIOF_OVERHEAT },
	{ NPCM_CORST, WDIOF_FANFAULT },
	{ NPCM_SWR1RST, WDIOF_EXTERN1 },
	{ NPCM_SWR2RST, WDIOF_EXTERN2 },
	{ NPCM_SWR3RST, WDIOF_POWERUNDER },
	{ NPCM8XX_TIP_RESET, WDIOF_POWEROVER },
};

static const struct npcm_wdt_data __maybe_unused npcm845_data = {
	.rst_map = npcm845_rst_map,
	.rst_map_size = ARRAY_SIZE(npcm845_rst_map),
	.status_map = npcm845_status_map,
	.status_map_size = ARRAY_SIZE(npcm845_status_map),
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

	clk_prepare_enable(wdt->clk);

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

	clk_disable_unprepare(wdt->clk);

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

	/* For reset, we start the WDT clock and leave it running. */
	clk_prepare_enable(wdt->clk);

	writel(NPCM_WTR | NPCM_WTRE | NPCM_WTE, wdt->reg);
	udelay(1000);

	return 0;
}

static bool npcm_is_running(struct watchdog_device *wdd)
{
	struct npcm_wdt *wdt = to_npcm_wdt(wdd);

	return readl(wdt->reg) & NPCM_WTE;
}

static void npcm_get_reset_status(struct npcm_wdt *wdt, struct device *dev,
				  const struct npcm_wdt_data *data,
				  resource_size_t start)
{
	struct regmap *gcr_regmap;
	u32 rstval = 0;
	unsigned int i;
	int ret;

	if (!data)
		return;

	gcr_regmap = syscon_regmap_lookup_by_phandle(dev->of_node,
						     "nuvoton,sysgcr");
	if (IS_ERR(gcr_regmap)) {
		dev_warn(dev,
			 "Failed to find nuvoton,sysgcr, WD reset status not supported\n");
		return;
	}

	if (of_device_is_compatible(dev->of_node, "nuvoton,npcm845-wdt")) {
		ret = regmap_read(gcr_regmap, NPCM_INTCR2_OFFSET, &rstval);
		if (ret) {
			dev_warn(dev, "Failed to read INTCR2 reset status: %d\n",
				 ret);
			return;
		}

		if (rstval & NPCM_RST) {
			ret = regmap_write(gcr_regmap, NPCM_INTCR2_OFFSET,
					   rstval & ~NPCM_RST);
			if (ret) {
				dev_warn(dev,
					 "Failed to clear INTCR2 reset status: %d\n",
					 ret);
				return;
			}

			ret = regmap_write(gcr_regmap, NPCM8XX_SCRPAD10_OFFSET,
					   rstval);
			if (ret) {
				dev_warn(dev,
					 "Failed to cache reset status in SCRPAD10: %d\n",
					 ret);
				return;
			}
		} else {
			ret = regmap_read(gcr_regmap, NPCM8XX_SCRPAD10_OFFSET,
					  &rstval);
			if (ret) {
				dev_warn(dev,
					 "Failed to read cached reset status from SCRPAD10: %d\n",
					 ret);
				return;
			}
		}
	} else if (of_device_is_compatible(dev->of_node, "nuvoton,npcm750-wdt")) {
		ret = regmap_read(gcr_regmap, NPCM_RESSR_OFFSET, &rstval);
		if (ret) {
			dev_warn(dev, "Failed to read RESSR reset status: %d\n",
				 ret);
			return;
		}

		if (rstval & NPCM_RST) {
			ret = regmap_write(gcr_regmap, NPCM_RESSR_OFFSET,
					   rstval & ~NPCM_RST);
			if (ret) {
				dev_warn(dev, "Failed to clear RESSR reset status: %d\n", ret);
				return;
			}

			ret = regmap_write(gcr_regmap, NPCM7XX_SCRPAD2_OFFSET,
					   rstval);
			if (ret) {
				dev_warn(dev,
					 "Failed to cache reset status in SCRPAD2: %d\n", ret);
				return;
			}
		} else {
			ret = regmap_read(gcr_regmap, NPCM7XX_SCRPAD2_OFFSET,
					  &rstval);
			if (ret) {
				dev_warn(dev,
					 "Failed to read cached reset status from SCRPAD2: %d\n",
					 ret);
				return;
			}
		}
	}

	for (i = 0; i < data->status_map_size; i++) {
		if (rstval & data->status_map[i].rst_bit)
			wdt->wdd.bootstatus |= data->status_map[i].wdiof_flag;
	}

	for (i = 0; i < data->rst_map_size; i++) {
		if (data->rst_map[i].base == start &&
		    rstval & data->rst_map[i].rst_bit) {
			wdt->wdd.bootstatus |= WDIOF_CARDRESET;
			break;
		}
	}
}

static const struct watchdog_info npcm_wdt_info = {
	.identity	= KBUILD_MODNAME,
	.options	= WDIOF_SETTIMEOUT
			| WDIOF_KEEPALIVEPING
			| WDIOF_MAGICCLOSE,
};

static const struct watchdog_info npcm_wdt_rst_info = {
	.identity	= KBUILD_MODNAME,
	.options	= WDIOF_SETTIMEOUT
			| WDIOF_KEEPALIVEPING
			| WDIOF_MAGICCLOSE
			| WDIOF_CARDRESET
			| WDIOF_OVERHEAT
			| WDIOF_FANFAULT
			| WDIOF_EXTERN1
			| WDIOF_EXTERN2
			| WDIOF_POWERUNDER
			| WDIOF_POWEROVER,
};

static const struct watchdog_ops npcm_wdt_ops = {
	.owner = THIS_MODULE,
	.start = npcm_wdt_start,
	.stop = npcm_wdt_stop,
	.ping = npcm_wdt_ping,
	.set_timeout = npcm_wdt_set_timeout,
	.restart = npcm_wdt_restart,
};

static int npcm_wdt_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	const struct npcm_wdt_data *data = device_get_match_data(dev);
	struct resource *res;
	struct npcm_wdt *wdt;
	resource_size_t start;
	int irq;
	int ret;

	wdt = devm_kzalloc(dev, sizeof(*wdt), GFP_KERNEL);
	if (!wdt)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -EINVAL;

	wdt->reg = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(wdt->reg))
		return PTR_ERR(wdt->reg);

	start = res->start;

	wdt->clk = devm_clk_get_optional(&pdev->dev, NULL);
	if (IS_ERR(wdt->clk))
		return PTR_ERR(wdt->clk);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	wdt->wdd.info = data ? &npcm_wdt_rst_info : &npcm_wdt_info;
	wdt->wdd.ops = &npcm_wdt_ops;
	wdt->wdd.min_timeout = 1;
	wdt->wdd.max_timeout = 2750;
	wdt->wdd.parent = dev;

	wdt->wdd.timeout = 86;
	watchdog_init_timeout(&wdt->wdd, 0, dev);

	/* Ensure timeout is able to be represented by the hardware */
	npcm_wdt_set_timeout(&wdt->wdd, wdt->wdd.timeout);

	npcm_get_reset_status(wdt, dev, data, start);

	if (npcm_is_running(&wdt->wdd)) {
		/* Restart with the default or device-tree specified timeout */
		npcm_wdt_start(&wdt->wdd);
		set_bit(WDOG_HW_RUNNING, &wdt->wdd.status);
	}

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
	{.compatible = "nuvoton,npcm750-wdt", .data = &npcm750_data},
	{.compatible = "nuvoton,npcm845-wdt", .data = &npcm845_data},
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

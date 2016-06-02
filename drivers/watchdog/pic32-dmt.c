/*
 * PIC32 deadman timer driver
 *
 * Purna Chandra Mandal <purna.mandal@microchip.com>
 * Copyright (c) 2016, Microchip Technology Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */
#include <linux/clk.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/pm.h>
#include <linux/watchdog.h>

#include <asm/mach-pic32/pic32.h>

/* Deadman Timer Regs */
#define DMTCON_REG	0x00
#define DMTPRECLR_REG	0x10
#define DMTCLR_REG	0x20
#define DMTSTAT_REG	0x30
#define DMTCNT_REG	0x40
#define DMTPSCNT_REG	0x60
#define DMTPSINTV_REG	0x70

/* Deadman Timer Regs fields */
#define DMT_ON			BIT(15)
#define DMT_STEP1_KEY		BIT(6)
#define DMT_STEP2_KEY		BIT(3)
#define DMTSTAT_WINOPN		BIT(0)
#define DMTSTAT_EVENT		BIT(5)
#define DMTSTAT_BAD2		BIT(6)
#define DMTSTAT_BAD1		BIT(7)

/* Reset Control Register fields for watchdog */
#define RESETCON_DMT_TIMEOUT	BIT(5)

struct pic32_dmt {
	void __iomem	*regs;
	struct clk	*clk;
};

static inline void dmt_enable(struct pic32_dmt *dmt)
{
	writel(DMT_ON, PIC32_SET(dmt->regs + DMTCON_REG));
}

static inline void dmt_disable(struct pic32_dmt *dmt)
{
	writel(DMT_ON, PIC32_CLR(dmt->regs + DMTCON_REG));
	/*
	 * Cannot touch registers in the CPU cycle following clearing the
	 * ON bit.
	 */
	nop();
}

static inline int dmt_bad_status(struct pic32_dmt *dmt)
{
	u32 val;

	val = readl(dmt->regs + DMTSTAT_REG);
	val &= (DMTSTAT_BAD1 | DMTSTAT_BAD2 | DMTSTAT_EVENT);
	if (val)
		return -EAGAIN;

	return 0;
}

static inline int dmt_keepalive(struct pic32_dmt *dmt)
{
	u32 v;
	u32 timeout = 500;

	/* set pre-clear key */
	writel(DMT_STEP1_KEY << 8, dmt->regs + DMTPRECLR_REG);

	/* wait for DMT window to open */
	while (--timeout) {
		v = readl(dmt->regs + DMTSTAT_REG) & DMTSTAT_WINOPN;
		if (v == DMTSTAT_WINOPN)
			break;
	}

	/* apply key2 */
	writel(DMT_STEP2_KEY, dmt->regs + DMTCLR_REG);

	/* check whether keys are latched correctly */
	return dmt_bad_status(dmt);
}

static inline u32 pic32_dmt_get_timeout_secs(struct pic32_dmt *dmt)
{
	unsigned long rate;

	rate = clk_get_rate(dmt->clk);
	if (rate)
		return readl(dmt->regs + DMTPSCNT_REG) / rate;

	return 0;
}

static inline u32 pic32_dmt_bootstatus(struct pic32_dmt *dmt)
{
	u32 v;
	void __iomem *rst_base;

	rst_base = ioremap(PIC32_BASE_RESET, 0x10);
	if (!rst_base)
		return 0;

	v = readl(rst_base);

	writel(RESETCON_DMT_TIMEOUT, PIC32_CLR(rst_base));

	iounmap(rst_base);
	return v & RESETCON_DMT_TIMEOUT;
}

static int pic32_dmt_start(struct watchdog_device *wdd)
{
	struct pic32_dmt *dmt = watchdog_get_drvdata(wdd);

	dmt_enable(dmt);
	return dmt_keepalive(dmt);
}

static int pic32_dmt_stop(struct watchdog_device *wdd)
{
	struct pic32_dmt *dmt = watchdog_get_drvdata(wdd);

	dmt_disable(dmt);

	return 0;
}

static int pic32_dmt_ping(struct watchdog_device *wdd)
{
	struct pic32_dmt *dmt = watchdog_get_drvdata(wdd);

	return dmt_keepalive(dmt);
}

static const struct watchdog_ops pic32_dmt_fops = {
	.owner		= THIS_MODULE,
	.start		= pic32_dmt_start,
	.stop		= pic32_dmt_stop,
	.ping		= pic32_dmt_ping,
};

static const struct watchdog_info pic32_dmt_ident = {
	.options	= WDIOF_KEEPALIVEPING |
			  WDIOF_MAGICCLOSE,
	.identity	= "PIC32 Deadman Timer",
};

static struct watchdog_device pic32_dmt_wdd = {
	.info		= &pic32_dmt_ident,
	.ops		= &pic32_dmt_fops,
};

static int pic32_dmt_probe(struct platform_device *pdev)
{
	int ret;
	struct pic32_dmt *dmt;
	struct resource *mem;
	struct watchdog_device *wdd = &pic32_dmt_wdd;

	dmt = devm_kzalloc(&pdev->dev, sizeof(*dmt), GFP_KERNEL);
	if (IS_ERR(dmt))
		return PTR_ERR(dmt);

	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	dmt->regs = devm_ioremap_resource(&pdev->dev, mem);
	if (IS_ERR(dmt->regs))
		return PTR_ERR(dmt->regs);

	dmt->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(dmt->clk)) {
		dev_err(&pdev->dev, "clk not found\n");
		return PTR_ERR(dmt->clk);
	}

	ret = clk_prepare_enable(dmt->clk);
	if (ret)
		return ret;

	wdd->timeout = pic32_dmt_get_timeout_secs(dmt);
	if (!wdd->timeout) {
		dev_err(&pdev->dev,
			"failed to read watchdog register timeout\n");
		ret = -EINVAL;
		goto out_disable_clk;
	}

	dev_info(&pdev->dev, "timeout %d\n", wdd->timeout);

	wdd->bootstatus = pic32_dmt_bootstatus(dmt) ? WDIOF_CARDRESET : 0;

	watchdog_set_nowayout(wdd, WATCHDOG_NOWAYOUT);
	watchdog_set_drvdata(wdd, dmt);

	ret = watchdog_register_device(wdd);
	if (ret) {
		dev_err(&pdev->dev, "watchdog register failed, err %d\n", ret);
		goto out_disable_clk;
	}

	platform_set_drvdata(pdev, wdd);
	return 0;

out_disable_clk:
	clk_disable_unprepare(dmt->clk);
	return ret;
}

static int pic32_dmt_remove(struct platform_device *pdev)
{
	struct watchdog_device *wdd = platform_get_drvdata(pdev);
	struct pic32_dmt *dmt = watchdog_get_drvdata(wdd);

	watchdog_unregister_device(wdd);
	clk_disable_unprepare(dmt->clk);

	return 0;
}

static const struct of_device_id pic32_dmt_of_ids[] = {
	{ .compatible = "microchip,pic32mzda-dmt",},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, pic32_dmt_of_ids);

static struct platform_driver pic32_dmt_driver = {
	.probe		= pic32_dmt_probe,
	.remove		= pic32_dmt_remove,
	.driver		= {
		.name		= "pic32-dmt",
		.owner		= THIS_MODULE,
		.of_match_table = of_match_ptr(pic32_dmt_of_ids),
	}
};

module_platform_driver(pic32_dmt_driver);

MODULE_AUTHOR("Purna Chandra Mandal <purna.mandal@microchip.com>");
MODULE_DESCRIPTION("Microchip PIC32 DMT Driver");
MODULE_LICENSE("GPL");

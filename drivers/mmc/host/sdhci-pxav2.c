/*
 * Copyright (C) 2010 Marvell International Ltd.
 *		Zhangfei Gao <zhangfei.gao@marvell.com>
 *		Kevin Wang <dwang4@marvell.com>
 *		Jun Nie <njun@marvell.com>
 *		Qiming Wu <wuqm@marvell.com>
 *		Philip Rakity <prakity@marvell.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/err.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/module.h>
#include <linux/io.h>
#include <linux/gpio.h>
#include <linux/mmc/card.h>
#include <linux/mmc/host.h>
#include <linux/platform_data/pxa_sdhci.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/of_device.h>

#include "sdhci.h"
#include "sdhci-pltfm.h"

#define SD_FIFO_PARAM		0xe0
#define DIS_PAD_SD_CLK_GATE	0x0400 /* Turn on/off Dynamic SD Clock Gating */
#define CLK_GATE_ON		0x0200 /* Disable/enable Clock Gate */
#define CLK_GATE_CTL		0x0100 /* Clock Gate Control */
#define CLK_GATE_SETTING_BITS	(DIS_PAD_SD_CLK_GATE | \
		CLK_GATE_ON | CLK_GATE_CTL)

#define SD_CLOCK_BURST_SIZE_SETUP	0xe6
#define SDCLK_SEL_SHIFT		8
#define SDCLK_SEL_MASK		0x3
#define SDCLK_DELAY_SHIFT	10
#define SDCLK_DELAY_MASK	0x3c

#define SD_CE_ATA_2		0xea
#define MMC_CARD		0x1000
#define MMC_WIDTH		0x0100

static void pxav2_set_private_registers(struct sdhci_host *host, u8 mask)
{
	struct platform_device *pdev = to_platform_device(mmc_dev(host->mmc));
	struct sdhci_pxa_platdata *pdata = pdev->dev.platform_data;

	if (mask == SDHCI_RESET_ALL) {
		u16 tmp = 0;

		/*
		 * tune timing of read data/command when crc error happen
		 * no performance impact
		 */
		if (pdata && pdata->clk_delay_sel == 1) {
			tmp = readw(host->ioaddr + SD_CLOCK_BURST_SIZE_SETUP);

			tmp &= ~(SDCLK_DELAY_MASK << SDCLK_DELAY_SHIFT);
			tmp |= (pdata->clk_delay_cycles & SDCLK_DELAY_MASK)
				<< SDCLK_DELAY_SHIFT;
			tmp &= ~(SDCLK_SEL_MASK << SDCLK_SEL_SHIFT);
			tmp |= (1 & SDCLK_SEL_MASK) << SDCLK_SEL_SHIFT;

			writew(tmp, host->ioaddr + SD_CLOCK_BURST_SIZE_SETUP);
		}

		if (pdata && (pdata->flags & PXA_FLAG_ENABLE_CLOCK_GATING)) {
			tmp = readw(host->ioaddr + SD_FIFO_PARAM);
			tmp &= ~CLK_GATE_SETTING_BITS;
			writew(tmp, host->ioaddr + SD_FIFO_PARAM);
		} else {
			tmp = readw(host->ioaddr + SD_FIFO_PARAM);
			tmp &= ~CLK_GATE_SETTING_BITS;
			tmp |= CLK_GATE_SETTING_BITS;
			writew(tmp, host->ioaddr + SD_FIFO_PARAM);
		}
	}
}

static int pxav2_mmc_set_width(struct sdhci_host *host, int width)
{
	u8 ctrl;
	u16 tmp;

	ctrl = readb(host->ioaddr + SDHCI_HOST_CONTROL);
	tmp = readw(host->ioaddr + SD_CE_ATA_2);
	if (width == MMC_BUS_WIDTH_8) {
		ctrl &= ~SDHCI_CTRL_4BITBUS;
		tmp |= MMC_CARD | MMC_WIDTH;
	} else {
		tmp &= ~(MMC_CARD | MMC_WIDTH);
		if (width == MMC_BUS_WIDTH_4)
			ctrl |= SDHCI_CTRL_4BITBUS;
		else
			ctrl &= ~SDHCI_CTRL_4BITBUS;
	}
	writew(tmp, host->ioaddr + SD_CE_ATA_2);
	writeb(ctrl, host->ioaddr + SDHCI_HOST_CONTROL);

	return 0;
}

static const struct sdhci_ops pxav2_sdhci_ops = {
	.get_max_clock = sdhci_pltfm_clk_get_max_clock,
	.platform_reset_exit = pxav2_set_private_registers,
	.platform_bus_width = pxav2_mmc_set_width,
};

#ifdef CONFIG_OF
static const struct of_device_id sdhci_pxav2_of_match[] = {
	{
		.compatible = "mrvl,pxav2-mmc",
	},
	{},
};
MODULE_DEVICE_TABLE(of, sdhci_pxav2_of_match);

static struct sdhci_pxa_platdata *pxav2_get_mmc_pdata(struct device *dev)
{
	struct sdhci_pxa_platdata *pdata;
	struct device_node *np = dev->of_node;
	u32 bus_width;
	u32 clk_delay_cycles;

	pdata = devm_kzalloc(dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return NULL;

	if (of_find_property(np, "non-removable", NULL))
		pdata->flags |= PXA_FLAG_CARD_PERMANENT;

	of_property_read_u32(np, "bus-width", &bus_width);
	if (bus_width == 8)
		pdata->flags |= PXA_FLAG_SD_8_BIT_CAPABLE_SLOT;

	of_property_read_u32(np, "mrvl,clk-delay-cycles", &clk_delay_cycles);
	if (clk_delay_cycles > 0) {
		pdata->clk_delay_sel = 1;
		pdata->clk_delay_cycles = clk_delay_cycles;
	}

	return pdata;
}
#else
static inline struct sdhci_pxa_platdata *pxav2_get_mmc_pdata(struct device *dev)
{
	return NULL;
}
#endif

static int sdhci_pxav2_probe(struct platform_device *pdev)
{
	struct sdhci_pltfm_host *pltfm_host;
	struct sdhci_pxa_platdata *pdata = pdev->dev.platform_data;
	struct device *dev = &pdev->dev;
	struct sdhci_host *host = NULL;
	struct sdhci_pxa *pxa = NULL;
	const struct of_device_id *match;

	int ret;
	struct clk *clk;

	pxa = kzalloc(sizeof(struct sdhci_pxa), GFP_KERNEL);
	if (!pxa)
		return -ENOMEM;

	host = sdhci_pltfm_init(pdev, NULL);
	if (IS_ERR(host)) {
		kfree(pxa);
		return PTR_ERR(host);
	}
	pltfm_host = sdhci_priv(host);
	pltfm_host->priv = pxa;

	clk = clk_get(dev, "PXA-SDHCLK");
	if (IS_ERR(clk)) {
		dev_err(dev, "failed to get io clock\n");
		ret = PTR_ERR(clk);
		goto err_clk_get;
	}
	pltfm_host->clk = clk;
	clk_prepare_enable(clk);

	host->quirks = SDHCI_QUIRK_BROKEN_ADMA
		| SDHCI_QUIRK_BROKEN_TIMEOUT_VAL
		| SDHCI_QUIRK_CAP_CLOCK_BASE_BROKEN;

	match = of_match_device(of_match_ptr(sdhci_pxav2_of_match), &pdev->dev);
	if (match) {
		pdata = pxav2_get_mmc_pdata(dev);
	}
	if (pdata) {
		if (pdata->flags & PXA_FLAG_CARD_PERMANENT) {
			/* on-chip device */
			host->quirks |= SDHCI_QUIRK_BROKEN_CARD_DETECTION;
			host->mmc->caps |= MMC_CAP_NONREMOVABLE;
		}

		/* If slot design supports 8 bit data, indicate this to MMC. */
		if (pdata->flags & PXA_FLAG_SD_8_BIT_CAPABLE_SLOT)
			host->mmc->caps |= MMC_CAP_8_BIT_DATA;

		if (pdata->quirks)
			host->quirks |= pdata->quirks;
		if (pdata->host_caps)
			host->mmc->caps |= pdata->host_caps;
		if (pdata->pm_caps)
			host->mmc->pm_caps |= pdata->pm_caps;
	}

	host->ops = &pxav2_sdhci_ops;

	ret = sdhci_add_host(host);
	if (ret) {
		dev_err(&pdev->dev, "failed to add host\n");
		goto err_add_host;
	}

	platform_set_drvdata(pdev, host);

	return 0;

err_add_host:
	clk_disable_unprepare(clk);
	clk_put(clk);
err_clk_get:
	sdhci_pltfm_free(pdev);
	kfree(pxa);
	return ret;
}

static int sdhci_pxav2_remove(struct platform_device *pdev)
{
	struct sdhci_host *host = platform_get_drvdata(pdev);
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_pxa *pxa = pltfm_host->priv;

	sdhci_remove_host(host, 1);

	clk_disable_unprepare(pltfm_host->clk);
	clk_put(pltfm_host->clk);
	sdhci_pltfm_free(pdev);
	kfree(pxa);

	platform_set_drvdata(pdev, NULL);

	return 0;
}

static struct platform_driver sdhci_pxav2_driver = {
	.driver		= {
		.name	= "sdhci-pxav2",
		.owner	= THIS_MODULE,
#ifdef CONFIG_OF
		.of_match_table = sdhci_pxav2_of_match,
#endif
		.pm	= SDHCI_PLTFM_PMOPS,
	},
	.probe		= sdhci_pxav2_probe,
	.remove		= sdhci_pxav2_remove,
};

module_platform_driver(sdhci_pxav2_driver);

MODULE_DESCRIPTION("SDHCI driver for pxav2");
MODULE_AUTHOR("Marvell International Ltd.");
MODULE_LICENSE("GPL v2");


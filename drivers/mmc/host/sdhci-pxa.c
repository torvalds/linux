/* linux/drivers/mmc/host/sdhci-pxa.c
 *
 * Copyright (C) 2010 Marvell International Ltd.
 *		Zhangfei Gao <zhangfei.gao@marvell.com>
 *		Kevin Wang <dwang4@marvell.com>
 *		Mingwei Wang <mwwang@marvell.com>
 *		Philip Rakity <prakity@marvell.com>
 *		Mark Brown <markb@marvell.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

/* Supports:
 * SDHCI support for MMP2/PXA910/PXA168
 *
 * Refer to sdhci-s3c.c.
 */

#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/mmc/host.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/err.h>
#include <plat/sdhci.h>
#include "sdhci.h"

#define DRIVER_NAME	"sdhci-pxa"

#define SD_FIFO_PARAM		0x104
#define DIS_PAD_SD_CLK_GATE	0x400

struct sdhci_pxa {
	struct sdhci_host		*host;
	struct sdhci_pxa_platdata	*pdata;
	struct clk			*clk;
	struct resource			*res;

	u8 clk_enable;
};

/*****************************************************************************\
 *                                                                           *
 * SDHCI core callbacks                                                      *
 *                                                                           *
\*****************************************************************************/
static void set_clock(struct sdhci_host *host, unsigned int clock)
{
	struct sdhci_pxa *pxa = sdhci_priv(host);
	u32 tmp = 0;

	if (clock == 0) {
		if (pxa->clk_enable) {
			clk_disable(pxa->clk);
			pxa->clk_enable = 0;
		}
	} else {
		if (0 == pxa->clk_enable) {
			if (pxa->pdata->flags & PXA_FLAG_DISABLE_CLOCK_GATING) {
				tmp = readl(host->ioaddr + SD_FIFO_PARAM);
				tmp |= DIS_PAD_SD_CLK_GATE;
				writel(tmp, host->ioaddr + SD_FIFO_PARAM);
			}
			clk_enable(pxa->clk);
			pxa->clk_enable = 1;
		}
	}
}

static int set_uhs_signaling(struct sdhci_host *host, unsigned int uhs)
{
	u16 ctrl_2;

	/*
	 * Set V18_EN -- UHS modes do not work without this.
	 * does not change signaling voltage
	 */
	ctrl_2 = sdhci_readw(host, SDHCI_HOST_CONTROL2);

	/* Select Bus Speed Mode for host */
	ctrl_2 &= ~SDHCI_CTRL_UHS_MASK;
	switch (uhs) {
	case MMC_TIMING_UHS_SDR12:
		ctrl_2 |= SDHCI_CTRL_UHS_SDR12;
		break;
	case MMC_TIMING_UHS_SDR25:
		ctrl_2 |= SDHCI_CTRL_UHS_SDR25;
		break;
	case MMC_TIMING_UHS_SDR50:
		ctrl_2 |= SDHCI_CTRL_UHS_SDR50 | SDHCI_CTRL_VDD_180;
		break;
	case MMC_TIMING_UHS_SDR104:
		ctrl_2 |= SDHCI_CTRL_UHS_SDR104 | SDHCI_CTRL_VDD_180;
		break;
	case MMC_TIMING_UHS_DDR50:
		ctrl_2 |= SDHCI_CTRL_UHS_DDR50 | SDHCI_CTRL_VDD_180;
		break;
	}

	sdhci_writew(host, ctrl_2, SDHCI_HOST_CONTROL2);
	pr_debug("%s:%s uhs = %d, ctrl_2 = %04X\n",
		__func__, mmc_hostname(host->mmc), uhs, ctrl_2);

	return 0;
}

static struct sdhci_ops sdhci_pxa_ops = {
	.set_uhs_signaling = set_uhs_signaling,
	.set_clock = set_clock,
};

/*****************************************************************************\
 *                                                                           *
 * Device probing/removal                                                    *
 *                                                                           *
\*****************************************************************************/

static int __devinit sdhci_pxa_probe(struct platform_device *pdev)
{
	struct sdhci_pxa_platdata *pdata = pdev->dev.platform_data;
	struct device *dev = &pdev->dev;
	struct sdhci_host *host = NULL;
	struct resource *iomem = NULL;
	struct sdhci_pxa *pxa = NULL;
	int ret, irq;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(dev, "no irq specified\n");
		return irq;
	}

	iomem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!iomem) {
		dev_err(dev, "no memory specified\n");
		return -ENOENT;
	}

	host = sdhci_alloc_host(&pdev->dev, sizeof(struct sdhci_pxa));
	if (IS_ERR(host)) {
		dev_err(dev, "failed to alloc host\n");
		return PTR_ERR(host);
	}

	pxa = sdhci_priv(host);
	pxa->host = host;
	pxa->pdata = pdata;
	pxa->clk_enable = 0;

	pxa->clk = clk_get(dev, "PXA-SDHCLK");
	if (IS_ERR(pxa->clk)) {
		dev_err(dev, "failed to get io clock\n");
		ret = PTR_ERR(pxa->clk);
		goto out;
	}

	pxa->res = request_mem_region(iomem->start, resource_size(iomem),
				      mmc_hostname(host->mmc));
	if (!pxa->res) {
		dev_err(&pdev->dev, "cannot request region\n");
		ret = -EBUSY;
		goto out;
	}

	host->ioaddr = ioremap(iomem->start, resource_size(iomem));
	if (!host->ioaddr) {
		dev_err(&pdev->dev, "failed to remap registers\n");
		ret = -ENOMEM;
		goto out;
	}

	host->hw_name = "MMC";
	host->ops = &sdhci_pxa_ops;
	host->irq = irq;
	host->quirks = SDHCI_QUIRK_BROKEN_ADMA
		| SDHCI_QUIRK_BROKEN_TIMEOUT_VAL
		| SDHCI_QUIRK_32BIT_DMA_ADDR
		| SDHCI_QUIRK_32BIT_DMA_SIZE
		| SDHCI_QUIRK_32BIT_ADMA_SIZE
		| SDHCI_QUIRK_NO_ENDATTR_IN_NOPDESC;

	if (pdata->quirks)
		host->quirks |= pdata->quirks;

	/* enable 1/8V DDR capable */
	host->mmc->caps |= MMC_CAP_1_8V_DDR;

	/* If slot design supports 8 bit data, indicate this to MMC. */
	if (pdata->flags & PXA_FLAG_SD_8_BIT_CAPABLE_SLOT)
		host->mmc->caps |= MMC_CAP_8_BIT_DATA;

	ret = sdhci_add_host(host);
	if (ret) {
		dev_err(&pdev->dev, "failed to add host\n");
		goto out;
	}

	if (pxa->pdata->max_speed)
		host->mmc->f_max = pxa->pdata->max_speed;

	platform_set_drvdata(pdev, host);

	return 0;
out:
	if (host) {
		clk_put(pxa->clk);
		if (host->ioaddr)
			iounmap(host->ioaddr);
		if (pxa->res)
			release_mem_region(pxa->res->start,
					   resource_size(pxa->res));
		sdhci_free_host(host);
	}

	return ret;
}

static int __devexit sdhci_pxa_remove(struct platform_device *pdev)
{
	struct sdhci_host *host = platform_get_drvdata(pdev);
	struct sdhci_pxa *pxa = sdhci_priv(host);
	int dead = 0;
	u32 scratch;

	if (host) {
		scratch = readl(host->ioaddr + SDHCI_INT_STATUS);
		if (scratch == (u32)-1)
			dead = 1;

		sdhci_remove_host(host, dead);

		if (host->ioaddr)
			iounmap(host->ioaddr);
		if (pxa->res)
			release_mem_region(pxa->res->start,
					   resource_size(pxa->res));
		if (pxa->clk_enable) {
			clk_disable(pxa->clk);
			pxa->clk_enable = 0;
		}
		clk_put(pxa->clk);

		sdhci_free_host(host);
		platform_set_drvdata(pdev, NULL);
	}

	return 0;
}

#ifdef CONFIG_PM
static int sdhci_pxa_suspend(struct platform_device *dev, pm_message_t state)
{
	struct sdhci_host *host = platform_get_drvdata(dev);

	return sdhci_suspend_host(host, state);
}

static int sdhci_pxa_resume(struct platform_device *dev)
{
	struct sdhci_host *host = platform_get_drvdata(dev);

	return sdhci_resume_host(host);
}
#else
#define sdhci_pxa_suspend	NULL
#define sdhci_pxa_resume	NULL
#endif

static struct platform_driver sdhci_pxa_driver = {
	.probe		= sdhci_pxa_probe,
	.remove		= __devexit_p(sdhci_pxa_remove),
	.suspend	= sdhci_pxa_suspend,
	.resume		= sdhci_pxa_resume,
	.driver		= {
		.name	= DRIVER_NAME,
		.owner	= THIS_MODULE,
	},
};

/*****************************************************************************\
 *                                                                           *
 * Driver init/exit                                                          *
 *                                                                           *
\*****************************************************************************/

static int __init sdhci_pxa_init(void)
{
	return platform_driver_register(&sdhci_pxa_driver);
}

static void __exit sdhci_pxa_exit(void)
{
	platform_driver_unregister(&sdhci_pxa_driver);
}

module_init(sdhci_pxa_init);
module_exit(sdhci_pxa_exit);

MODULE_DESCRIPTION("SDH controller driver for PXA168/PXA910/MMP2");
MODULE_AUTHOR("Zhangfei Gao <zhangfei.gao@marvell.com>");
MODULE_LICENSE("GPL v2");

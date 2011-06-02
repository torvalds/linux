/*
 * Freescale eSDHC controller driver.
 *
 * Copyright (c) 2007 Freescale Semiconductor, Inc.
 * Copyright (c) 2009 MontaVista Software, Inc.
 *
 * Authors: Xiaobo Xie <X.Xie@freescale.com>
 *	    Anton Vorontsov <avorontsov@ru.mvista.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 */

#include <linux/io.h>
#include <linux/delay.h>
#include <linux/mmc/host.h>
#include "sdhci-pltfm.h"
#include "sdhci-esdhc.h"

static u16 esdhc_readw(struct sdhci_host *host, int reg)
{
	u16 ret;

	if (unlikely(reg == SDHCI_HOST_VERSION))
		ret = in_be16(host->ioaddr + reg);
	else
		ret = sdhci_be32bs_readw(host, reg);
	return ret;
}

static void esdhc_writew(struct sdhci_host *host, u16 val, int reg)
{
	if (reg == SDHCI_BLOCK_SIZE) {
		/*
		 * Two last DMA bits are reserved, and first one is used for
		 * non-standard blksz of 4096 bytes that we don't support
		 * yet. So clear the DMA boundary bits.
		 */
		val &= ~SDHCI_MAKE_BLKSZ(0x7, 0);
	}
	sdhci_be32bs_writew(host, val, reg);
}

static void esdhc_writeb(struct sdhci_host *host, u8 val, int reg)
{
	/* Prevent SDHCI core from writing reserved bits (e.g. HISPD). */
	if (reg == SDHCI_HOST_CONTROL)
		val &= ~ESDHC_HOST_CONTROL_RES;
	sdhci_be32bs_writeb(host, val, reg);
}

static int esdhc_of_enable_dma(struct sdhci_host *host)
{
	setbits32(host->ioaddr + ESDHC_DMA_SYSCTL, ESDHC_DMA_SNOOP);
	return 0;
}

static unsigned int esdhc_of_get_max_clock(struct sdhci_host *host)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);

	return pltfm_host->clock;
}

static unsigned int esdhc_of_get_min_clock(struct sdhci_host *host)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);

	return pltfm_host->clock / 256 / 16;
}

static struct sdhci_ops sdhci_esdhc_ops = {
	.read_l = sdhci_be32bs_readl,
	.read_w = esdhc_readw,
	.read_b = sdhci_be32bs_readb,
	.write_l = sdhci_be32bs_writel,
	.write_w = esdhc_writew,
	.write_b = esdhc_writeb,
	.set_clock = esdhc_set_clock,
	.enable_dma = esdhc_of_enable_dma,
	.get_max_clock = esdhc_of_get_max_clock,
	.get_min_clock = esdhc_of_get_min_clock,
};

static struct sdhci_pltfm_data sdhci_esdhc_pdata = {
	/* card detection could be handled via GPIO */
	.quirks = ESDHC_DEFAULT_QUIRKS | SDHCI_QUIRK_BROKEN_CARD_DETECTION
		| SDHCI_QUIRK_NO_CARD_NO_RESET,
	.ops = &sdhci_esdhc_ops,
};

static int __devinit sdhci_esdhc_probe(struct platform_device *pdev)
{
	return sdhci_pltfm_register(pdev, &sdhci_esdhc_pdata);
}

static int __devexit sdhci_esdhc_remove(struct platform_device *pdev)
{
	return sdhci_pltfm_unregister(pdev);
}

static const struct of_device_id sdhci_esdhc_of_match[] = {
	{ .compatible = "fsl,mpc8379-esdhc" },
	{ .compatible = "fsl,mpc8536-esdhc" },
	{ .compatible = "fsl,esdhc" },
	{ }
};
MODULE_DEVICE_TABLE(of, sdhci_esdhc_of_match);

static struct platform_driver sdhci_esdhc_driver = {
	.driver = {
		.name = "sdhci-esdhc",
		.owner = THIS_MODULE,
		.of_match_table = sdhci_esdhc_of_match,
	},
	.probe = sdhci_esdhc_probe,
	.remove = __devexit_p(sdhci_esdhc_remove),
#ifdef CONFIG_PM
	.suspend = sdhci_pltfm_suspend,
	.resume = sdhci_pltfm_resume,
#endif
};

static int __init sdhci_esdhc_init(void)
{
	return platform_driver_register(&sdhci_esdhc_driver);
}
module_init(sdhci_esdhc_init);

static void __exit sdhci_esdhc_exit(void)
{
	platform_driver_unregister(&sdhci_esdhc_driver);
}
module_exit(sdhci_esdhc_exit);

MODULE_DESCRIPTION("SDHCI OF driver for Freescale MPC eSDHC");
MODULE_AUTHOR("Xiaobo Xie <X.Xie@freescale.com>, "
	      "Anton Vorontsov <avorontsov@ru.mvista.com>");
MODULE_LICENSE("GPL v2");

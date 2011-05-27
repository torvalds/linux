/*
 * Copyright 2010 MontaVista Software, LLC.
 *
 * Author: Anton Vorontsov <avorontsov@ru.mvista.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _DRIVERS_MMC_SDHCI_PLTFM_H
#define _DRIVERS_MMC_SDHCI_PLTFM_H

#include <linux/clk.h>
#include <linux/types.h>
#include <linux/platform_device.h>
#include <linux/mmc/sdhci-pltfm.h>
#include <linux/mmc/sdhci.h>

struct sdhci_pltfm_host {
	struct clk *clk;
	void *priv; /* to handle quirks across io-accessor calls */

	/* migrate from sdhci_of_host */
	unsigned int clock;
	u16 xfer_mode_shadow;
};

#ifdef CONFIG_MMC_SDHCI_BIG_ENDIAN_32BIT_BYTE_SWAPPER
extern u32 sdhci_be32bs_readl(struct sdhci_host *host, int reg);
extern u16 sdhci_be32bs_readw(struct sdhci_host *host, int reg);
extern u8 sdhci_be32bs_readb(struct sdhci_host *host, int reg);
extern void sdhci_be32bs_writel(struct sdhci_host *host, u32 val, int reg);
extern void sdhci_be32bs_writew(struct sdhci_host *host, u16 val, int reg);
extern void sdhci_be32bs_writeb(struct sdhci_host *host, u8 val, int reg);
#endif

extern void sdhci_get_of_property(struct platform_device *pdev);

extern struct sdhci_host *sdhci_pltfm_init(struct platform_device *pdev,
					   struct sdhci_pltfm_data *pdata);
extern void sdhci_pltfm_free(struct platform_device *pdev);

extern int sdhci_pltfm_register(struct platform_device *pdev,
				struct sdhci_pltfm_data *pdata);
extern int sdhci_pltfm_unregister(struct platform_device *pdev);

#ifdef CONFIG_PM
extern int sdhci_pltfm_suspend(struct platform_device *dev, pm_message_t state);
extern int sdhci_pltfm_resume(struct platform_device *dev);
#endif

#endif /* _DRIVERS_MMC_SDHCI_PLTFM_H */

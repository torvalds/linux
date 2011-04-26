/* linux/arch/arm/mach-exynos4/setup-sdhci.c
 *
 * Copyright (c) 2010-2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * EXYNOS4 - Helper functions for settign up SDHCI device(s) (HSMMC)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/io.h>

#include <linux/mmc/card.h>
#include <linux/mmc/host.h>

#include <plat/regs-sdhci.h>

/* clock sources for the mmc bus clock, order as for the ctrl2[5..4] */

char *exynos4_hsmmc_clksrcs[4] = {
	[0] = NULL,
	[1] = NULL,
	[2] = "sclk_mmc",	/* mmc_bus */
	[3] = NULL,
};

void exynos4_setup_sdhci_cfg_card(struct platform_device *dev, void __iomem *r,
				  struct mmc_ios *ios, struct mmc_card *card)
{
	u32 ctrl2, ctrl3;

	/* don't need to alter anything according to card-type */

	ctrl2 = readl(r + S3C_SDHCI_CONTROL2);

	/* select base clock source to HCLK */

	ctrl2 &= S3C_SDHCI_CTRL2_SELBASECLK_MASK;

	/*
	 * clear async mode, enable conflict mask, rx feedback ctrl, SD
	 * clk hold and no use debounce count
	 */

	ctrl2 |= (S3C64XX_SDHCI_CTRL2_ENSTAASYNCCLR |
		  S3C64XX_SDHCI_CTRL2_ENCMDCNFMSK |
		  S3C_SDHCI_CTRL2_ENFBCLKRX |
		  S3C_SDHCI_CTRL2_DFCNT_NONE |
		  S3C_SDHCI_CTRL2_ENCLKOUTHOLD);

	/* Tx and Rx feedback clock delay control */

	if (ios->clock < 25 * 1000000)
		ctrl3 = (S3C_SDHCI_CTRL3_FCSEL3 |
			 S3C_SDHCI_CTRL3_FCSEL2 |
			 S3C_SDHCI_CTRL3_FCSEL1 |
			 S3C_SDHCI_CTRL3_FCSEL0);
	else
		ctrl3 = (S3C_SDHCI_CTRL3_FCSEL1 | S3C_SDHCI_CTRL3_FCSEL0);

	writel(ctrl2, r + S3C_SDHCI_CONTROL2);
	writel(ctrl3, r + S3C_SDHCI_CONTROL3);
}

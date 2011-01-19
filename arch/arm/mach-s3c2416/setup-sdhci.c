/* linux/arch/arm/mach-s3c2416/setup-sdhci.c
 *
 * Copyright 2010 Promwad Innovation Company
 *	Yauhen Kharuzhy <yauhen.kharuzhy@promwad.com>
 *
 * S3C2416 - Helper functions for settign up SDHCI device(s) (HSMMC)
 *
 * Based on mach-s3c64xx/setup-sdhci.c
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
#include <plat/sdhci.h>

/* clock sources for the mmc bus clock, order as for the ctrl2[5..4] */

char *s3c2416_hsmmc_clksrcs[4] = {
	[0] = "hsmmc",
	[1] = "hsmmc",
	[2] = "hsmmc-if",
	/* [3] = "48m", - note not successfully used yet */
};

void s3c2416_setup_sdhci_cfg_card(struct platform_device *dev,
				  void __iomem *r,
				  struct mmc_ios *ios,
				  struct mmc_card *card)
{
	u32 ctrl2, ctrl3;

	ctrl2 = __raw_readl(r + S3C_SDHCI_CONTROL2);
	ctrl2 &= S3C_SDHCI_CTRL2_SELBASECLK_MASK;
	ctrl2 |= (S3C64XX_SDHCI_CTRL2_ENSTAASYNCCLR |
		  S3C64XX_SDHCI_CTRL2_ENCMDCNFMSK |
		  S3C_SDHCI_CTRL2_ENFBCLKRX |
		  S3C_SDHCI_CTRL2_DFCNT_NONE |
		  S3C_SDHCI_CTRL2_ENCLKOUTHOLD);

	if (ios->clock < 25 * 1000000)
		ctrl3 = (S3C_SDHCI_CTRL3_FCSEL3 |
			 S3C_SDHCI_CTRL3_FCSEL2 |
			 S3C_SDHCI_CTRL3_FCSEL1 |
			 S3C_SDHCI_CTRL3_FCSEL0);
	else
		ctrl3 = (S3C_SDHCI_CTRL3_FCSEL1 | S3C_SDHCI_CTRL3_FCSEL0);

	__raw_writel(ctrl2, r + S3C_SDHCI_CONTROL2);
	__raw_writel(ctrl3, r + S3C_SDHCI_CONTROL3);
}

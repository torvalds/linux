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

#include <linux/types.h>

/* clock sources for the mmc bus clock, order as for the ctrl2[5..4] */

char *s3c2416_hsmmc_clksrcs[4] = {
	[0] = "hsmmc",
	[1] = "hsmmc",
	[2] = "hsmmc-if",
	/* [3] = "48m", - note not successfully used yet */
};

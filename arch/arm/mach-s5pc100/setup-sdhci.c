/* linux/arch/arm/mach-s5pc100/setup-sdhci.c
 *
 * Copyright 2008 Samsung Electronics
 *
 * S5PC100 - Helper functions for settign up SDHCI device(s) (HSMMC)
 *
 * Based on mach-s3c6410/setup-sdhci.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/types.h>

/* clock sources for the mmc bus clock, order as for the ctrl2[5..4] */

char *s5pc100_hsmmc_clksrcs[4] = {
	[0] = "hsmmc",		/* HCLK */
	/* [1] = "hsmmc",	- duplicate HCLK entry */
	[2] = "sclk_mmc",	/* mmc_bus */
	/* [3] = "48m",		- note not successfully used yet */
};

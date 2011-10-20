/* linux/arch/arm/mach-s3c64xx/setup-sdhci.c
 *
 * Copyright 2008 Simtec Electronics
 * Copyright 2008 Simtec Electronics
 *	Ben Dooks <ben@simtec.co.uk>
 *	http://armlinux.simtec.co.uk/
 *
 * S3C6400/S3C6410 - Helper functions for settign up SDHCI device(s) (HSMMC)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/types.h>

/* clock sources for the mmc bus clock, order as for the ctrl2[5..4] */

char *s3c64xx_hsmmc_clksrcs[4] = {
	[0] = "hsmmc",
	[1] = "hsmmc",
	[2] = "mmc_bus",
	/* [3] = "48m", - note not successfully used yet */
};

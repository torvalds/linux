/* linux/arch/arm/mach-s5pv210/setup-sdhci.c
 *
 * Copyright (c) 2009-2010 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * S5PV210 - Helper functions for settign up SDHCI device(s) (HSMMC)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/types.h>

/* clock sources for the mmc bus clock, order as for the ctrl2[5..4] */

char *s5pv210_hsmmc_clksrcs[4] = {
	[0] = "hsmmc",		/* HCLK */
	/* [1] = "hsmmc",	- duplicate HCLK entry */
	[2] = "sclk_mmc",	/* mmc_bus */
	/* [3] = NULL,		- reserved */
};

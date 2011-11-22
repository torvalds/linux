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

#include <linux/types.h>

/* clock sources for the mmc bus clock, order as for the ctrl2[5..4] */

char *exynos4_hsmmc_clksrcs[4] = {
	[0] = NULL,
	[1] = NULL,
	[2] = "sclk_mmc",	/* mmc_bus */
	[3] = NULL,
};

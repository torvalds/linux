/* linux/arch/arm/mach-exynos/setup-mshci.c
 *
 * Copyright (c) 2010-2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * EXYNOS4 - Helper functions for settign up MSHCI device(s)
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

#include <plat/mshci.h>

/* clock sources for the mmc bus clock, order as for the ctrl2[5..4] */

char *exynos4_mshci_clksrcs[1] = {
	[0] = "sclk_dwmci",	/* sclk for mshc */
};

void exynos4_setup_mshci_cfg_card(struct platform_device *dev,
				    void __iomem *r,
				    struct mmc_ios *ios,
				    struct mmc_card *card)
{
	/* still now, It dose not have something to do on booting time*/
}


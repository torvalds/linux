/* linux/arch/arm/plat-pxa/include/plat/sdhci.h
 *
 * Copyright 2010 Marvell
 *	Zhangfei Gao <zhangfei.gao@marvell.com>
 *
 * PXA Platform - SDHCI platform data definitions
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __PLAT_PXA_SDHCI_H
#define __PLAT_PXA_SDHCI_H

/* pxa specific flag */
/* Require clock free running */
#define PXA_FLAG_DISABLE_CLOCK_GATING (1<<0)

/*
 * struct pxa_sdhci_platdata() - Platform device data for PXA SDHCI
 * @max_speed: the maximum speed supported
 * @quirks: quirks of specific device
 * @flags: flags for platform requirement
 */
struct sdhci_pxa_platdata {
	unsigned int	max_speed;
	unsigned int	quirks;
	unsigned int	flags;
};

#endif /* __PLAT_PXA_SDHCI_H */

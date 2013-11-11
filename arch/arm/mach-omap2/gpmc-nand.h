/*
 *  arch/arm/mach-omap2/gpmc-nand.h
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 */

#ifndef	__OMAP2_GPMC_NAND_H
#define	__OMAP2_GPMC_NAND_H

#include "gpmc.h"
#include <linux/platform_data/mtd-nand-omap2.h>

#if IS_ENABLED(CONFIG_MTD_NAND_OMAP2)
extern int gpmc_nand_init(struct omap_nand_platform_data *d,
			  struct gpmc_timings *gpmc_t);
#else
static inline int gpmc_nand_init(struct omap_nand_platform_data *d,
				 struct gpmc_timings *gpmc_t)
{
	return 0;
}
#endif

#endif

/*
 * arch/arm/plat-omap/include/mach/onenand.h
 *
 * Copyright (C) 2006 Nokia Corporation
 * Author: Juha Yrjola
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>

#define ONENAND_SYNC_READ	(1 << 0)
#define ONENAND_SYNC_READWRITE	(1 << 1)

struct omap_onenand_platform_data {
	int			cs;
	int			gpio_irq;
	struct mtd_partition	*parts;
	int			nr_parts;
	int                     (*onenand_setup)(void __iomem *, int freq);
	int			dma_channel;
	u8			flags;
};

#define ONENAND_MAX_PARTITIONS 8

#if defined(CONFIG_MTD_ONENAND_OMAP2) || \
	defined(CONFIG_MTD_ONENAND_OMAP2_MODULE)

extern void gpmc_onenand_init(struct omap_onenand_platform_data *d);

#else

#define board_onenand_data	NULL

static inline void gpmc_onenand_init(struct omap_onenand_platform_data *d)
{
}

#endif

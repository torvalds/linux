/*
 * arch/arm/plat-omap/include/mach/nand.h
 *
 * Copyright (C) 2006 Micron Technology Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/mtd/partitions.h>

struct omap_nand_platform_data {
	unsigned int		options;
	int			cs;
	int			gpio_irq;
	struct mtd_partition	*parts;
	struct gpmc_timings	*gpmc_t;
	int			nr_parts;
	int			(*nand_setup)(void);
	int			(*dev_ready)(struct omap_nand_platform_data *);
	int			dma_channel;
	unsigned long		phys_base;
	void __iomem		*gpmc_cs_baseaddr;
	void __iomem		*gpmc_baseaddr;
	int			devsize;
};

/* size (4 KiB) for IO mapping */
#define	NAND_IO_SIZE	SZ_4K

#if defined(CONFIG_MTD_NAND_OMAP2) || defined(CONFIG_MTD_NAND_OMAP2_MODULE)
extern int gpmc_nand_init(struct omap_nand_platform_data *d);
#else
static inline int gpmc_nand_init(struct omap_nand_platform_data *d)
{
	return 0;
}
#endif

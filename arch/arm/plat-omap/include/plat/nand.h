/*
 * arch/arm/plat-omap/include/mach/nand.h
 *
 * Copyright (C) 2006 Micron Technology Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <plat/gpmc.h>
#include <linux/mtd/partitions.h>

enum nand_io {
	NAND_OMAP_PREFETCH_POLLED = 0,	/* prefetch polled mode, default */
	NAND_OMAP_POLLED,		/* polled mode, without prefetch */
	NAND_OMAP_PREFETCH_DMA,		/* prefetch enabled sDMA mode */
	NAND_OMAP_PREFETCH_IRQ		/* prefetch enabled irq mode */
};

struct omap_nand_platform_data {
	int			cs;
	struct mtd_partition	*parts;
	struct gpmc_timings	*gpmc_t;
	int			nr_parts;
	bool			dev_ready;
	enum nand_io		xfer_type;
	int			devsize;
	enum omap_ecc           ecc_opt;
	struct gpmc_nand_regs	reg;
};

/* minimum size for IO mapping */
#define	NAND_IO_SIZE	4

#if defined(CONFIG_MTD_NAND_OMAP2) || defined(CONFIG_MTD_NAND_OMAP2_MODULE)
extern int gpmc_nand_init(struct omap_nand_platform_data *d);
#else
static inline int gpmc_nand_init(struct omap_nand_platform_data *d)
{
	return 0;
}
#endif

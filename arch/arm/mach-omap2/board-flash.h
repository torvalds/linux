/*
 *  board-sdp.h
 *
 *  Information structures for SDP-specific board config data
 *
 *  Copyright (C) 2009 Nokia Corporation
 *  Copyright (C) 2009 Texas Instruments
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <plat/gpmc.h>

#define PDC_NOR		1
#define PDC_NAND	2
#define PDC_ONENAND	3
#define DBG_MPDB	4

struct flash_partitions {
	struct mtd_partition *parts;
	int nr_parts;
};

#if defined(CONFIG_MTD_NAND_OMAP2) || \
		defined(CONFIG_MTD_NAND_OMAP2_MODULE) || \
		defined(CONFIG_MTD_ONENAND_OMAP2) || \
		defined(CONFIG_MTD_ONENAND_OMAP2_MODULE)
extern void board_flash_init(struct flash_partitions [],
				char chip_sel[][GPMC_CS_NUM], int nand_type);
#else
static inline void board_flash_init(struct flash_partitions part[],
				char chip_sel[][GPMC_CS_NUM], int nand_type)
{
}
#endif

#if defined(CONFIG_MTD_NAND_OMAP2) || \
		defined(CONFIG_MTD_NAND_OMAP2_MODULE)
extern void board_nand_init(struct mtd_partition *nand_parts,
					u8 nr_parts, u8 cs, int nand_type);
#else
static inline void board_nand_init(struct mtd_partition *nand_parts,
					u8 nr_parts, u8 cs, int nand_type)
{
}
#endif

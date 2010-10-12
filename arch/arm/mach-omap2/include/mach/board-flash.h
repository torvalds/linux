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

extern void board_flash_init(struct flash_partitions [],
				char chip_sel[][GPMC_CS_NUM]);

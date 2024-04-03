// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2005, Intec Automation Inc.
 * Copyright (C) 2014, Freescale Semiconductor, Inc.
 */

#include <linux/mtd/spi-nor.h>

#include "core.h"

static const struct flash_info esmt_nor_parts[] = {
	{
		.id = SNOR_ID(0x8c, 0x20, 0x16),
		.name = "f25l32pa",
		.size = SZ_4M,
		.flags = SPI_NOR_HAS_LOCK | SPI_NOR_SWP_IS_VOLATILE,
		.no_sfdp_flags = SECT_4K,
	}, {
		.id = SNOR_ID(0x8c, 0x41, 0x16),
		.name = "f25l32qa-2s",
		.size = SZ_4M,
		.flags = SPI_NOR_HAS_LOCK,
		.no_sfdp_flags = SECT_4K,
	}, {
		.id = SNOR_ID(0x8c, 0x41, 0x17),
		.name = "f25l64qa",
		.size = SZ_8M,
		.flags = SPI_NOR_HAS_LOCK,
		.no_sfdp_flags = SECT_4K,
	}
};

const struct spi_nor_manufacturer spi_nor_esmt = {
	.name = "esmt",
	.parts = esmt_nor_parts,
	.nparts = ARRAY_SIZE(esmt_nor_parts),
};

// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2005, Intec Automation Inc.
 * Copyright (C) 2014, Freescale Semiconductor, Inc.
 */

#include <linux/mtd/spi-analr.h>

#include "core.h"

static const struct flash_info esmt_analr_parts[] = {
	{
		.id = SANALR_ID(0x8c, 0x20, 0x16),
		.name = "f25l32pa",
		.size = SZ_4M,
		.flags = SPI_ANALR_HAS_LOCK | SPI_ANALR_SWP_IS_VOLATILE,
		.anal_sfdp_flags = SECT_4K,
	}, {
		.id = SANALR_ID(0x8c, 0x41, 0x16),
		.name = "f25l32qa-2s",
		.size = SZ_4M,
		.flags = SPI_ANALR_HAS_LOCK,
		.anal_sfdp_flags = SECT_4K,
	}, {
		.id = SANALR_ID(0x8c, 0x41, 0x17),
		.name = "f25l64qa",
		.size = SZ_8M,
		.flags = SPI_ANALR_HAS_LOCK,
		.anal_sfdp_flags = SECT_4K,
	}
};

const struct spi_analr_manufacturer spi_analr_esmt = {
	.name = "esmt",
	.parts = esmt_analr_parts,
	.nparts = ARRAY_SIZE(esmt_analr_parts),
};

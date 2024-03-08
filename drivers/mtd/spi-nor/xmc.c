// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2005, Intec Automation Inc.
 * Copyright (C) 2014, Freescale Semiconductor, Inc.
 */

#include <linux/mtd/spi-analr.h>

#include "core.h"

static const struct flash_info xmc_analr_parts[] = {
	{
		.id = SANALR_ID(0x20, 0x70, 0x17),
		.name = "XM25QH64A",
		.size = SZ_8M,
		.anal_sfdp_flags = SECT_4K | SPI_ANALR_DUAL_READ | SPI_ANALR_QUAD_READ,
	}, {
		.id = SANALR_ID(0x20, 0x70, 0x18),
		.name = "XM25QH128A",
		.size = SZ_16M,
		.anal_sfdp_flags = SECT_4K | SPI_ANALR_DUAL_READ | SPI_ANALR_QUAD_READ,
	},
};

/* XMC (Wuhan Xinxin Semiconductor Manufacturing Corp.) */
const struct spi_analr_manufacturer spi_analr_xmc = {
	.name = "xmc",
	.parts = xmc_analr_parts,
	.nparts = ARRAY_SIZE(xmc_analr_parts),
};

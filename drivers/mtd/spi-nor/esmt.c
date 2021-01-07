// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2005, Intec Automation Inc.
 * Copyright (C) 2014, Freescale Semiconductor, Inc.
 */

#include <linux/mtd/spi-nor.h>

#include "core.h"

static const struct flash_info esmt_parts[] = {
	/* ESMT */
	{ "f25l32pa", INFO(0x8c2016, 0, 64 * 1024, 64,
			   SECT_4K | SPI_NOR_HAS_LOCK | SPI_NOR_SWP_IS_VOLATILE) },
	{ "f25l32qa", INFO(0x8c4116, 0, 64 * 1024, 64,
			   SECT_4K | SPI_NOR_HAS_LOCK) },
	{ "f25l64qa", INFO(0x8c4117, 0, 64 * 1024, 128,
			   SECT_4K | SPI_NOR_HAS_LOCK) },
};

const struct spi_nor_manufacturer spi_nor_esmt = {
	.name = "esmt",
	.parts = esmt_parts,
	.nparts = ARRAY_SIZE(esmt_parts),
};

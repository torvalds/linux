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
			   SECT_4K | SPI_NOR_HAS_LOCK) },
	{ "f25l32qa", INFO(0x8c4116, 0, 64 * 1024, 64,
			   SECT_4K | SPI_NOR_HAS_LOCK) },
	{ "f25l64qa", INFO(0x8c4117, 0, 64 * 1024, 128,
			   SECT_4K | SPI_NOR_HAS_LOCK) },
	{ "en25qh128",
		INFO(0x1c7018, 0, 64 * 1024, 256, SECT_4K) },
	{ "en25s64",
		INFO(0x1c3817, 0, 64 * 1024, 128, SECT_4K) },
	{ "en25qh256a",
		INFO(0x1c7019, 0, 64 * 1024, 512, SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ | SPI_NOR_4B_OPCODES) },
	{ "en25qx256a",
		INFO(0x1c7119, 0, 64 * 1024, 512, SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ | SPI_NOR_4B_OPCODES) },
	{ "en25qx128a",
		INFO(0x1c7118, 0, 64 * 1024, 256, SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ) },
};

const struct spi_nor_manufacturer spi_nor_esmt = {
	.name = "esmt",
	.parts = esmt_parts,
	.nparts = ARRAY_SIZE(esmt_parts),
};

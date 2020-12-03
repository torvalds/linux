// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2005, Intec Automation Inc.
 * Copyright (C) 2014, Freescale Semiconductor, Inc.
 */

#include <linux/mtd/spi-nor.h>

#include "core.h"

static const struct flash_info atmel_parts[] = {
	/* Atmel -- some are (confusingly) marketed as "DataFlash" */
	{ "at25fs010",  INFO(0x1f6601, 0, 32 * 1024,   4, SECT_4K | SPI_NOR_HAS_LOCK) },
	{ "at25fs040",  INFO(0x1f6604, 0, 64 * 1024,   8, SECT_4K | SPI_NOR_HAS_LOCK) },

	{ "at25df041a", INFO(0x1f4401, 0, 64 * 1024,   8, SECT_4K | SPI_NOR_HAS_LOCK) },
	{ "at25df321",  INFO(0x1f4700, 0, 64 * 1024,  64, SECT_4K | SPI_NOR_HAS_LOCK) },
	{ "at25df321a", INFO(0x1f4701, 0, 64 * 1024,  64, SECT_4K | SPI_NOR_HAS_LOCK) },
	{ "at25df641",  INFO(0x1f4800, 0, 64 * 1024, 128, SECT_4K | SPI_NOR_HAS_LOCK) },

	{ "at25sl321",	INFO(0x1f4216, 0, 64 * 1024, 64,
			     SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ) },

	{ "at26f004",   INFO(0x1f0400, 0, 64 * 1024,  8, SECT_4K) },
	{ "at26df081a", INFO(0x1f4501, 0, 64 * 1024, 16, SECT_4K | SPI_NOR_HAS_LOCK) },
	{ "at26df161a", INFO(0x1f4601, 0, 64 * 1024, 32, SECT_4K | SPI_NOR_HAS_LOCK) },
	{ "at26df321",  INFO(0x1f4700, 0, 64 * 1024, 64, SECT_4K | SPI_NOR_HAS_LOCK) },

	{ "at45db081d", INFO(0x1f2500, 0, 64 * 1024, 16, SECT_4K) },
};

const struct spi_nor_manufacturer spi_nor_atmel = {
	.name = "atmel",
	.parts = atmel_parts,
	.nparts = ARRAY_SIZE(atmel_parts),
};

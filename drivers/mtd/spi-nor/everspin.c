// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2005, Intec Automation Inc.
 * Copyright (C) 2014, Freescale Semiconductor, Inc.
 */

#include <linux/mtd/spi-nor.h>

#include "core.h"

static const struct flash_info everspin_nor_parts[] = {
	{
		.name = "mr25h128",
		.size = SZ_16K,
		.sector_size = SZ_16K,
		.addr_nbytes = 2,
		.flags = SPI_NOR_NO_ERASE | SPI_NOR_NO_FR,
	}, {
		.name = "mr25h256",
		.size = SZ_32K,
		.sector_size = SZ_32K,
		.addr_nbytes = 2,
		.flags = SPI_NOR_NO_ERASE | SPI_NOR_NO_FR,
	}, {
		.name = "mr25h10",
		.size = SZ_128K,
		.sector_size = SZ_128K,
		.flags = SPI_NOR_NO_ERASE | SPI_NOR_NO_FR,
	}, {
		.name = "mr25h40",
		.size = SZ_512K,
		.sector_size = SZ_512K,
		.flags = SPI_NOR_NO_ERASE | SPI_NOR_NO_FR,
	}
};

const struct spi_nor_manufacturer spi_nor_everspin = {
	.name = "everspin",
	.parts = everspin_nor_parts,
	.nparts = ARRAY_SIZE(everspin_nor_parts),
};

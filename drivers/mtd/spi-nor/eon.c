// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2005, Intec Automation Inc.
 * Copyright (C) 2014, Freescale Semiconductor, Inc.
 */

#include <linux/mtd/spi-nor.h>

#include "core.h"

static const struct flash_info eon_parts[] = {
	/* EON -- en25xxx */
	{ "en25f32",    INFO(0x1c3116, 0, 64 * 1024,   64)
		NO_SFDP_FLAGS(SECT_4K) },
	{ "en25p32",    INFO(0x1c2016, 0, 64 * 1024,   64) },
	{ "en25q32b",   INFO(0x1c3016, 0, 64 * 1024,   64) },
	{ "en25p64",    INFO(0x1c2017, 0, 64 * 1024,  128) },
	{ "en25q64",    INFO(0x1c3017, 0, 64 * 1024,  128)
		NO_SFDP_FLAGS(SECT_4K) },
	{ "en25q80a",   INFO(0x1c3014, 0, 64 * 1024,   16)
		NO_SFDP_FLAGS(SECT_4K | SPI_NOR_DUAL_READ) },
	{ "en25qh16",   INFO(0x1c7015, 0, 64 * 1024,   32)
		NO_SFDP_FLAGS(SECT_4K | SPI_NOR_DUAL_READ) },
	{ "en25qh32",   INFO(0x1c7016, 0, 64 * 1024,   64) },
	{ "en25qh64",   INFO(0x1c7017, 0, 64 * 1024,  128)
		NO_SFDP_FLAGS(SECT_4K | SPI_NOR_DUAL_READ) },
	{ "en25qh128",  INFO(0x1c7018, 0, 64 * 1024,  256) },
	{ "en25qh256",  INFO(0x1c7019, 0, 64 * 1024,  512) },
	{ "en25s64",	INFO(0x1c3817, 0, 64 * 1024,  128)
		NO_SFDP_FLAGS(SECT_4K) },
};

const struct spi_nor_manufacturer spi_nor_eon = {
	.name = "eon",
	.parts = eon_parts,
	.nparts = ARRAY_SIZE(eon_parts),
};

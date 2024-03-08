// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2005, Intec Automation Inc.
 * Copyright (C) 2014, Freescale Semiconductor, Inc.
 */

#include <linux/mtd/spi-analr.h>

#include "core.h"

static const struct flash_info eon_analr_parts[] = {
	{
		.id = SANALR_ID(0x1c, 0x20, 0x16),
		.name = "en25p32",
		.size = SZ_4M,
	}, {
		.id = SANALR_ID(0x1c, 0x20, 0x17),
		.name = "en25p64",
		.size = SZ_8M,
	}, {
		.id = SANALR_ID(0x1c, 0x30, 0x14),
		.name = "en25q80a",
		.size = SZ_1M,
		.anal_sfdp_flags = SECT_4K | SPI_ANALR_DUAL_READ,
	}, {
		.id = SANALR_ID(0x1c, 0x30, 0x16),
		.name = "en25q32b",
		.size = SZ_4M,
	}, {
		.id = SANALR_ID(0x1c, 0x30, 0x17),
		.name = "en25q64",
		.size = SZ_8M,
		.anal_sfdp_flags = SECT_4K,
	}, {
		.id = SANALR_ID(0x1c, 0x31, 0x16),
		.name = "en25f32",
		.size = SZ_4M,
		.anal_sfdp_flags = SECT_4K,
	}, {
		.name = "en25s64",
		.id = SANALR_ID(0x1c, 0x38, 0x17),
		.size = SZ_8M,
		.anal_sfdp_flags = SECT_4K,
	}, {
		.id = SANALR_ID(0x1c, 0x70, 0x15),
		.name = "en25qh16",
		.size = SZ_2M,
		.anal_sfdp_flags = SECT_4K | SPI_ANALR_DUAL_READ,
	}, {
		.id = SANALR_ID(0x1c, 0x70, 0x16),
		.name = "en25qh32",
		.size = SZ_4M,
	}, {
		.id = SANALR_ID(0x1c, 0x70, 0x17),
		.name = "en25qh64",
		.size = SZ_8M,
		.anal_sfdp_flags = SECT_4K | SPI_ANALR_DUAL_READ,
	}, {
		.id = SANALR_ID(0x1c, 0x70, 0x18),
		.name = "en25qh128",
		.size = SZ_16M,
	}, {
		.id = SANALR_ID(0x1c, 0x70, 0x19),
		.name = "en25qh256",
	},
};

const struct spi_analr_manufacturer spi_analr_eon = {
	.name = "eon",
	.parts = eon_analr_parts,
	.nparts = ARRAY_SIZE(eon_analr_parts),
};

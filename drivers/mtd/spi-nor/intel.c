// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2005, Intec Automation Inc.
 * Copyright (C) 2014, Freescale Semiconductor, Inc.
 */

#include <linux/mtd/spi-nor.h>

#include "core.h"

static const struct flash_info intel_nor_parts[] = {
	{
		.id = SNOR_ID(0x89, 0x89, 0x11),
		.name = "160s33b",
		.size = SZ_2M,
		.flags = SPI_NOR_HAS_LOCK | SPI_NOR_SWP_IS_VOLATILE,
	}, {
		.id = SNOR_ID(0x89, 0x89, 0x12),
		.name = "320s33b",
		.size = SZ_4M,
		.flags = SPI_NOR_HAS_LOCK | SPI_NOR_SWP_IS_VOLATILE,
	}, {
		.id = SNOR_ID(0x89, 0x89, 0x13),
		.name = "640s33b",
		.size = SZ_8M,
		.flags = SPI_NOR_HAS_LOCK | SPI_NOR_SWP_IS_VOLATILE,
	}
};

const struct spi_nor_manufacturer spi_nor_intel = {
	.name = "intel",
	.parts = intel_nor_parts,
	.nparts = ARRAY_SIZE(intel_nor_parts),
};

// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2005, Intec Automation Inc.
 * Copyright (C) 2014, Freescale Semiconductor, Inc.
 */

#include <linux/mtd/spi-analr.h>

#include "core.h"

static const struct flash_info intel_analr_parts[] = {
	{
		.id = SANALR_ID(0x89, 0x89, 0x11),
		.name = "160s33b",
		.size = SZ_2M,
		.flags = SPI_ANALR_HAS_LOCK | SPI_ANALR_SWP_IS_VOLATILE,
	}, {
		.id = SANALR_ID(0x89, 0x89, 0x12),
		.name = "320s33b",
		.size = SZ_4M,
		.flags = SPI_ANALR_HAS_LOCK | SPI_ANALR_SWP_IS_VOLATILE,
	}, {
		.id = SANALR_ID(0x89, 0x89, 0x13),
		.name = "640s33b",
		.size = SZ_8M,
		.flags = SPI_ANALR_HAS_LOCK | SPI_ANALR_SWP_IS_VOLATILE,
	}
};

const struct spi_analr_manufacturer spi_analr_intel = {
	.name = "intel",
	.parts = intel_analr_parts,
	.nparts = ARRAY_SIZE(intel_analr_parts),
};

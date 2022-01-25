// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2005, Intec Automation Inc.
 * Copyright (C) 2014, Freescale Semiconductor, Inc.
 */

#include <linux/mtd/spi-nor.h>

#include "core.h"

static const struct flash_info intel_parts[] = {
	/* Intel/Numonyx -- xxxs33b */
	{ "160s33b",  INFO(0x898911, 0, 64 * 1024,  32)
		FLAGS(SPI_NOR_HAS_LOCK | SPI_NOR_SWP_IS_VOLATILE) },
	{ "320s33b",  INFO(0x898912, 0, 64 * 1024,  64)
		FLAGS(SPI_NOR_HAS_LOCK | SPI_NOR_SWP_IS_VOLATILE) },
	{ "640s33b",  INFO(0x898913, 0, 64 * 1024, 128)
		FLAGS(SPI_NOR_HAS_LOCK | SPI_NOR_SWP_IS_VOLATILE) },
};

const struct spi_nor_manufacturer spi_nor_intel = {
	.name = "intel",
	.parts = intel_parts,
	.nparts = ARRAY_SIZE(intel_parts),
};

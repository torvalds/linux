// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2005, Intec Automation Inc.
 * Copyright (C) 2014, Freescale Semiconductor, Inc.
 */

#include <linux/mtd/spi-nor.h>

#include "core.h"

static const struct flash_info everspin_parts[] = {
	/* Everspin */
	{ "mr25h128", CAT25_INFO(16 * 1024, 1, 256, 2) },
	{ "mr25h256", CAT25_INFO(32 * 1024, 1, 256, 2) },
	{ "mr25h10",  CAT25_INFO(128 * 1024, 1, 256, 3) },
	{ "mr25h40",  CAT25_INFO(512 * 1024, 1, 256, 3) },
};

const struct spi_nor_manufacturer spi_nor_everspin = {
	.name = "everspin",
	.parts = everspin_parts,
	.nparts = ARRAY_SIZE(everspin_parts),
};

// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2005, Intec Automation Inc.
 * Copyright (C) 2014, Freescale Semiconductor, Inc.
 */

#include <linux/mtd/spi-nor.h>

#include "core.h"

static const struct flash_info catalyst_parts[] = {
	/* Catalyst / On Semiconductor -- non-JEDEC */
	{ "cat25c11", CAT25_INFO(16, 8, 16, 1) },
	{ "cat25c03", CAT25_INFO(32, 8, 16, 2) },
	{ "cat25c09", CAT25_INFO(128, 8, 32, 2) },
	{ "cat25c17", CAT25_INFO(256, 8, 32, 2) },
	{ "cat25128", CAT25_INFO(2048, 8, 64, 2) },
};

const struct spi_nor_manufacturer spi_nor_catalyst = {
	.name = "catalyst",
	.parts = catalyst_parts,
	.nparts = ARRAY_SIZE(catalyst_parts),
};

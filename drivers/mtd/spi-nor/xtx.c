// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2024, ASPEED Tech Inc.
 */

#include <linux/mtd/spi-nor.h>

#include "core.h"

static const struct flash_info xtx_parts[] = {
	/* XMC (Wuhan Xinxin Semiconductor Manufacturing Corp.) */
	{ "xt25w512b", INFO(0x0b651a, 0, 64 * 1024, 1024,
			    SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ) },
	{ "xt25w01gb", INFO(0x0b651b, 0, 64 * 1024, 2048,
			     SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ) },
};

const struct spi_nor_manufacturer spi_nor_xtx = {
	.name = "xtx",
	.parts = xtx_parts,
	.nparts = ARRAY_SIZE(xtx_parts),
};

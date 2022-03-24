// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2022 Rockchip Electronics Co., Ltd.
 */

#include <linux/mtd/spi-nor.h>

#include "core.h"

static const struct flash_info fmsh_parts[] = {
	{ "FM25Q64A", INFO(0xA14017, 0, 64 * 1024, 128,
			    SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ) },
	{ "FM25Q128A", INFO(0xA14018, 0, 64 * 1024, 256,
			     SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ) },
};

const struct spi_nor_manufacturer spi_nor_fmsh = {
	.name = "fmsh",
	.parts = fmsh_parts,
	.nparts = ARRAY_SIZE(fmsh_parts),
};

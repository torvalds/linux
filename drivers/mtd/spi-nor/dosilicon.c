// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2022 Rockchip Electronics Co., Ltd.
 */

#include <linux/mtd/spi-nor.h>

#include "core.h"

static const struct flash_info dosilicon_parts[] = {
	{ "FM25Q64A", INFO(0xf83217, 0, 64 * 1024, 128,
			   SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ) },
	{ "FM25M4AA", INFO(0xf84218, 0, 64 * 1024, 256,
			   SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ) },
	{ "FM25M64C", INFO(0xf84317, 0, 64 * 1024, 128,
			   SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ) },
};

const struct spi_nor_manufacturer spi_nor_dosilicon = {
	.name = "dosilicon",
	.parts = dosilicon_parts,
	.nparts = ARRAY_SIZE(dosilicon_parts),
};

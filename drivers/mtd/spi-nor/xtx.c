// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2021 Rockchip Electronics Co., Ltd.
 */

#include <linux/mtd/spi-nor.h>

#include "core.h"

static const struct flash_info xtx_parts[] = {
	{ "XT25F64F", INFO(0x0b4017, 0, 64 * 1024, 128, SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ) },
	{ "XT25F128B", INFO(0x0b4018, 0, 64 * 1024, 256, SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ) },
	{ "xt25q64d", INFO(0x0b6017, 0, 64 * 1024, 128, SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ) },
	{ "xt25q128d", INFO(0x0b6018, 0, 64 * 1024, 256, SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ) },
	{ "xt25f256b", INFO(0x0b4019, 0, 64 * 1024, 512, SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ) },
};

const struct spi_nor_manufacturer spi_nor_xtx = {
	.name = "xtx",
	.parts = xtx_parts,
	.nparts = ARRAY_SIZE(xtx_parts),
};

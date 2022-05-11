// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2021 Rockchip Electronics Co., Ltd.
 */

#include <linux/mtd/spi-nor.h>

#include "core.h"

static const struct flash_info puya_parts[] = {
	{ "PY25Q128HA", INFO(0x852018, 0, 64 * 1024, 256,
			     SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ) },
	{ "P25Q64H", INFO(0x856017, 0, 64 * 1024, 128,
			  SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ) },
	{ "P25Q128H", INFO(0x856018, 0, 64 * 1024, 256,
			   SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ) },
};

const struct spi_nor_manufacturer spi_nor_puya = {
	.name = "puya",
	.parts = puya_parts,
	.nparts = ARRAY_SIZE(puya_parts),
};

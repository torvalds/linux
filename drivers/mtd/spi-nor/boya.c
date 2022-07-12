// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2022 Rockchip Electronics Co., Ltd.
 */

#include <linux/mtd/spi-nor.h>

#include "core.h"

static const struct flash_info boya_parts[] = {
	{ "BY25Q256FSEIG", INFO(0x684919, 0, 64 * 1024, 512, SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ | SPI_NOR_4B_OPCODES) },
};

const struct spi_nor_manufacturer spi_nor_boya = {
	.name = "boya",
	.parts = boya_parts,
	.nparts = ARRAY_SIZE(boya_parts),
};

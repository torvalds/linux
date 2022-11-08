// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2022 Rockchip Electronics Co., Ltd.
 */

#include <linux/mtd/spi-nor.h>

#include "core.h"

static const struct flash_info normem_parts[] = {
	{ "NM25Q128EVB", INFO(0x522118, 0, 64 * 1024, 256, SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ) },
};

static void normem_default_init(struct spi_nor *nor)
{
	nor->params->quad_enable = spi_nor_sr1_bit6_quad_enable;
}

static const struct spi_nor_fixups normem_fixups = {
	.default_init = normem_default_init,
};

const struct spi_nor_manufacturer spi_nor_normem = {
	.name = "normem",
	.parts = normem_parts,
	.nparts = ARRAY_SIZE(normem_parts),
	.fixups = &normem_fixups,
};

// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2005, Intec Automation Inc.
 * Copyright (C) 2014, Freescale Semiconductor, Inc.
 */

#include <linux/mtd/spi-nor.h>

#include "core.h"

static void gd25q256_default_init(struct spi_nor *nor)
{
	/* use Quad page program */
	nor->params->hwcaps.mask |= SNOR_HWCAPS_PP_1_1_4;
	spi_nor_set_pp_settings(&nor->params->page_programs[SNOR_CMD_PP_1_1_4],
				SPINOR_OP_PP_1_1_4, SNOR_PROTO_1_1_4);
}

static struct spi_nor_fixups gd25q256_fixups = {
	.default_init = gd25q256_default_init,
};

static const struct flash_info gigadevice_parts[] = {
	{ "gd25q16", INFO(0xc84015, 0, 64 * 1024,  32,
			  SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ |
			  SPI_NOR_HAS_LOCK | SPI_NOR_HAS_TB) },
	{ "gd25q32", INFO(0xc84016, 0, 64 * 1024,  64,
			  SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ |
			  SPI_NOR_HAS_LOCK | SPI_NOR_HAS_TB) },
	{ "gd25lq32", INFO(0xc86016, 0, 64 * 1024, 64,
			   SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ |
			   SPI_NOR_HAS_LOCK | SPI_NOR_HAS_TB) },
	{ "gd25q64", INFO(0xc84017, 0, 64 * 1024, 128,
			  SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ |
			  SPI_NOR_HAS_LOCK | SPI_NOR_HAS_TB) },
	{ "gd25lq64c", INFO(0xc86017, 0, 64 * 1024, 128,
			    SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ |
			    SPI_NOR_HAS_LOCK | SPI_NOR_HAS_TB) },
	{ "gd25lq128d", INFO(0xc86018, 0, 64 * 1024, 256,
			     SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ | SPI_NOR_SKIP_SFDP |
			     SPI_NOR_HAS_LOCK | SPI_NOR_HAS_TB) },
	{ "gd25q128", INFO(0xc84018, 0, 64 * 1024, 256,
			   SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ |
			   SPI_NOR_HAS_LOCK | SPI_NOR_HAS_TB) },
	{ "gd25q256", INFO(0xc84019, 0, 64 * 1024, 512,
			   SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ |
			   SPI_NOR_4B_OPCODES | SPI_NOR_HAS_LOCK |
			   SPI_NOR_HAS_TB | SPI_NOR_TB_SR_BIT6)
		.fixups = &gd25q256_fixups },
};

const struct spi_nor_manufacturer spi_nor_gigadevice = {
	.name = "gigadevice",
	.parts = gigadevice_parts,
	.nparts = ARRAY_SIZE(gigadevice_parts),
	.fixups = &gd25q256_fixups,
};

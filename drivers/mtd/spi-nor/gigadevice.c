// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2005, Intec Automation Inc.
 * Copyright (C) 2014, Freescale Semiconductor, Inc.
 */

#include <linux/mtd/spi-nor.h>

#include "core.h"

static int
gd25q256_post_bfpt(struct spi_nor *nor,
		   const struct sfdp_parameter_header *bfpt_header,
		   const struct sfdp_bfpt *bfpt)
{
	/*
	 * GD25Q256C supports the first version of JESD216 which does not define
	 * the Quad Enable methods. Overwrite the default Quad Enable method.
	 *
	 * GD25Q256 GENERATION | SFDP MAJOR VERSION | SFDP MINOR VERSION
	 *      GD25Q256C      | SFDP_JESD216_MAJOR | SFDP_JESD216_MINOR
	 *      GD25Q256D      | SFDP_JESD216_MAJOR | SFDP_JESD216B_MINOR
	 *      GD25Q256E      | SFDP_JESD216_MAJOR | SFDP_JESD216B_MINOR
	 */
	if (bfpt_header->major == SFDP_JESD216_MAJOR &&
	    bfpt_header->minor == SFDP_JESD216_MINOR)
		nor->params->quad_enable = spi_nor_sr1_bit6_quad_enable;

	return 0;
}

static const struct spi_nor_fixups gd25q256_fixups = {
	.post_bfpt = gd25q256_post_bfpt,
};

static const struct flash_info gigadevice_nor_parts[] = {
	{ "gd25q16", INFO(0xc84015, 0, 64 * 1024,  32)
		FLAGS(SPI_NOR_HAS_LOCK | SPI_NOR_HAS_TB)
		NO_SFDP_FLAGS(SECT_4K | SPI_NOR_DUAL_READ |
			      SPI_NOR_QUAD_READ) },
	{ "gd25q32", INFO(0xc84016, 0, 64 * 1024,  64)
		FLAGS(SPI_NOR_HAS_LOCK | SPI_NOR_HAS_TB)
		NO_SFDP_FLAGS(SECT_4K | SPI_NOR_DUAL_READ |
			      SPI_NOR_QUAD_READ) },
	{ "gd25lq32", INFO(0xc86016, 0, 64 * 1024, 64)
		FLAGS(SPI_NOR_HAS_LOCK | SPI_NOR_HAS_TB)
		NO_SFDP_FLAGS(SECT_4K | SPI_NOR_DUAL_READ |
			      SPI_NOR_QUAD_READ) },
	{ "gd25q64", INFO(0xc84017, 0, 64 * 1024, 128)
		FLAGS(SPI_NOR_HAS_LOCK | SPI_NOR_HAS_TB)
		NO_SFDP_FLAGS(SECT_4K | SPI_NOR_DUAL_READ |
			      SPI_NOR_QUAD_READ) },
	{ "gd25lq64c", INFO(0xc86017, 0, 64 * 1024, 128)
		FLAGS(SPI_NOR_HAS_LOCK | SPI_NOR_HAS_TB)
		NO_SFDP_FLAGS(SECT_4K | SPI_NOR_DUAL_READ |
			      SPI_NOR_QUAD_READ) },
	{ "gd25lq128d", INFO(0xc86018, 0, 64 * 1024, 256)
		FLAGS(SPI_NOR_HAS_LOCK | SPI_NOR_HAS_TB)
		NO_SFDP_FLAGS(SECT_4K | SPI_NOR_DUAL_READ |
			      SPI_NOR_QUAD_READ) },
	{ "gd25q128", INFO(0xc84018, 0, 64 * 1024, 256)
		FLAGS(SPI_NOR_HAS_LOCK | SPI_NOR_HAS_TB)
		NO_SFDP_FLAGS(SECT_4K | SPI_NOR_DUAL_READ |
			      SPI_NOR_QUAD_READ) },
	{ "gd25q256", INFO(0xc84019, 0, 64 * 1024, 512)
		PARSE_SFDP
		FLAGS(SPI_NOR_HAS_LOCK | SPI_NOR_HAS_TB | SPI_NOR_TB_SR_BIT6)
		FIXUP_FLAGS(SPI_NOR_4B_OPCODES)
		.fixups = &gd25q256_fixups },
	{ "gd55b01gf", INFO(0xc8401b, 0, 64 * 1024, 2048)
		PARSE_SFDP
		FLAGS(SPI_NOR_HAS_LOCK | SPI_NOR_HAS_TB) },
	{ "gd55b02gf", INFO(0xc8401c, 0, 64 * 1024, 4096)
		PARSE_SFDP
		FLAGS(SPI_NOR_HAS_LOCK | SPI_NOR_HAS_TB) },
	{ "gd25b512m", INFO(0xc8471a, 0, 64 * 1024, 1024)
		PARSE_SFDP
		FLAGS(SPI_NOR_HAS_LOCK | SPI_NOR_HAS_TB) },
	{ "gd25b512m", INFO(0xc8471a, 0, 64 * 1024, 1024)
		PARSE_SFDP
		FLAGS(SPI_NOR_HAS_LOCK | SPI_NOR_HAS_TB) },
	{ "gd55b01ge", INFO(0xc8471b, 0, 64 * 1024, 2048)
		PARSE_SFDP
		FLAGS(SPI_NOR_HAS_LOCK | SPI_NOR_HAS_TB) },
	{ "gd55b02ge", INFO(0xc8471c, 0, 64 * 1024, 4096)
		PARSE_SFDP
		FLAGS(SPI_NOR_HAS_LOCK | SPI_NOR_HAS_TB) },
};

const struct spi_nor_manufacturer spi_nor_gigadevice = {
	.name = "gigadevice",
	.parts = gigadevice_nor_parts,
	.nparts = ARRAY_SIZE(gigadevice_nor_parts),
};

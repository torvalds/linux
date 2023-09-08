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
	{
		.id = SNOR_ID(0xc8, 0x40, 0x15),
		.name = "gd25q16",
		.size = SZ_2M,
		.flags = SPI_NOR_HAS_LOCK | SPI_NOR_HAS_TB,
		.no_sfdp_flags = SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ,
	}, {
		.id = SNOR_ID(0xc8, 0x40, 0x16),
		.name = "gd25q32",
		.size = SZ_4M,
		.flags = SPI_NOR_HAS_LOCK | SPI_NOR_HAS_TB,
		.no_sfdp_flags = SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ,
	}, {
		.id = SNOR_ID(0xc8, 0x40, 0x17),
		.name = "gd25q64",
		.size = SZ_8M,
		.flags = SPI_NOR_HAS_LOCK | SPI_NOR_HAS_TB,
		.no_sfdp_flags = SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ,
	}, {
		.id = SNOR_ID(0xc8, 0x40, 0x18),
		.name = "gd25q128",
		.size = SZ_16M,
		.flags = SPI_NOR_HAS_LOCK | SPI_NOR_HAS_TB,
		.no_sfdp_flags = SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ,
	}, {
		.id = SNOR_ID(0xc8, 0x40, 0x19),
		.name = "gd25q256",
		.flags = SPI_NOR_HAS_LOCK | SPI_NOR_HAS_TB | SPI_NOR_TB_SR_BIT6,
		.fixups = &gd25q256_fixups,
		.fixup_flags = SPI_NOR_4B_OPCODES,
	}, {
		.id = SNOR_ID(0xc8, 0x60, 0x16),
		.name = "gd25lq32",
		.size = SZ_4M,
		.flags = SPI_NOR_HAS_LOCK | SPI_NOR_HAS_TB,
		.no_sfdp_flags = SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ,
	}, {
		.id = SNOR_ID(0xc8, 0x60, 0x17),
		.name = "gd25lq64c",
		.size = SZ_8M,
		.flags = SPI_NOR_HAS_LOCK | SPI_NOR_HAS_TB,
		.no_sfdp_flags = SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ,
	}, {
		.id = SNOR_ID(0xc8, 0x60, 0x18),
		.name = "gd25lq128d",
		.size = SZ_16M,
		.flags = SPI_NOR_HAS_LOCK | SPI_NOR_HAS_TB,
		.no_sfdp_flags = SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ,
	},
};

const struct spi_nor_manufacturer spi_nor_gigadevice = {
	.name = "gigadevice",
	.parts = gigadevice_nor_parts,
	.nparts = ARRAY_SIZE(gigadevice_nor_parts),
};

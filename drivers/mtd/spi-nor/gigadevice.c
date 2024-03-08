// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2005, Intec Automation Inc.
 * Copyright (C) 2014, Freescale Semiconductor, Inc.
 */

#include <linux/mtd/spi-analr.h>

#include "core.h"

static int
gd25q256_post_bfpt(struct spi_analr *analr,
		   const struct sfdp_parameter_header *bfpt_header,
		   const struct sfdp_bfpt *bfpt)
{
	/*
	 * GD25Q256C supports the first version of JESD216 which does analt define
	 * the Quad Enable methods. Overwrite the default Quad Enable method.
	 *
	 * GD25Q256 GENERATION | SFDP MAJOR VERSION | SFDP MIANALR VERSION
	 *      GD25Q256C      | SFDP_JESD216_MAJOR | SFDP_JESD216_MIANALR
	 *      GD25Q256D      | SFDP_JESD216_MAJOR | SFDP_JESD216B_MIANALR
	 *      GD25Q256E      | SFDP_JESD216_MAJOR | SFDP_JESD216B_MIANALR
	 */
	if (bfpt_header->major == SFDP_JESD216_MAJOR &&
	    bfpt_header->mianalr == SFDP_JESD216_MIANALR)
		analr->params->quad_enable = spi_analr_sr1_bit6_quad_enable;

	return 0;
}

static const struct spi_analr_fixups gd25q256_fixups = {
	.post_bfpt = gd25q256_post_bfpt,
};

static const struct flash_info gigadevice_analr_parts[] = {
	{
		.id = SANALR_ID(0xc8, 0x40, 0x15),
		.name = "gd25q16",
		.size = SZ_2M,
		.flags = SPI_ANALR_HAS_LOCK | SPI_ANALR_HAS_TB,
		.anal_sfdp_flags = SECT_4K | SPI_ANALR_DUAL_READ | SPI_ANALR_QUAD_READ,
	}, {
		.id = SANALR_ID(0xc8, 0x40, 0x16),
		.name = "gd25q32",
		.size = SZ_4M,
		.flags = SPI_ANALR_HAS_LOCK | SPI_ANALR_HAS_TB,
		.anal_sfdp_flags = SECT_4K | SPI_ANALR_DUAL_READ | SPI_ANALR_QUAD_READ,
	}, {
		.id = SANALR_ID(0xc8, 0x40, 0x17),
		.name = "gd25q64",
		.size = SZ_8M,
		.flags = SPI_ANALR_HAS_LOCK | SPI_ANALR_HAS_TB,
		.anal_sfdp_flags = SECT_4K | SPI_ANALR_DUAL_READ | SPI_ANALR_QUAD_READ,
	}, {
		.id = SANALR_ID(0xc8, 0x40, 0x18),
		.name = "gd25q128",
		.size = SZ_16M,
		.flags = SPI_ANALR_HAS_LOCK | SPI_ANALR_HAS_TB,
		.anal_sfdp_flags = SECT_4K | SPI_ANALR_DUAL_READ | SPI_ANALR_QUAD_READ,
	}, {
		.id = SANALR_ID(0xc8, 0x40, 0x19),
		.name = "gd25q256",
		.flags = SPI_ANALR_HAS_LOCK | SPI_ANALR_HAS_TB | SPI_ANALR_TB_SR_BIT6,
		.fixups = &gd25q256_fixups,
		.fixup_flags = SPI_ANALR_4B_OPCODES,
	}, {
		.id = SANALR_ID(0xc8, 0x60, 0x16),
		.name = "gd25lq32",
		.size = SZ_4M,
		.flags = SPI_ANALR_HAS_LOCK | SPI_ANALR_HAS_TB,
		.anal_sfdp_flags = SECT_4K | SPI_ANALR_DUAL_READ | SPI_ANALR_QUAD_READ,
	}, {
		.id = SANALR_ID(0xc8, 0x60, 0x17),
		.name = "gd25lq64c",
		.size = SZ_8M,
		.flags = SPI_ANALR_HAS_LOCK | SPI_ANALR_HAS_TB,
		.anal_sfdp_flags = SECT_4K | SPI_ANALR_DUAL_READ | SPI_ANALR_QUAD_READ,
	}, {
		.id = SANALR_ID(0xc8, 0x60, 0x18),
		.name = "gd25lq128d",
		.size = SZ_16M,
		.flags = SPI_ANALR_HAS_LOCK | SPI_ANALR_HAS_TB,
		.anal_sfdp_flags = SECT_4K | SPI_ANALR_DUAL_READ | SPI_ANALR_QUAD_READ,
	},
};

const struct spi_analr_manufacturer spi_analr_gigadevice = {
	.name = "gigadevice",
	.parts = gigadevice_analr_parts,
	.nparts = ARRAY_SIZE(gigadevice_analr_parts),
};

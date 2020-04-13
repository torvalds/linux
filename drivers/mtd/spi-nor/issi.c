// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2005, Intec Automation Inc.
 * Copyright (C) 2014, Freescale Semiconductor, Inc.
 */

#include <linux/mtd/spi-nor.h>

#include "core.h"

static int
is25lp256_post_bfpt_fixups(struct spi_nor *nor,
			   const struct sfdp_parameter_header *bfpt_header,
			   const struct sfdp_bfpt *bfpt,
			   struct spi_nor_flash_parameter *params)
{
	/*
	 * IS25LP256 supports 4B opcodes, but the BFPT advertises a
	 * BFPT_DWORD1_ADDRESS_BYTES_3_ONLY address width.
	 * Overwrite the address width advertised by the BFPT.
	 */
	if ((bfpt->dwords[BFPT_DWORD(1)] & BFPT_DWORD1_ADDRESS_BYTES_MASK) ==
		BFPT_DWORD1_ADDRESS_BYTES_3_ONLY)
		nor->addr_width = 4;

	return 0;
}

static struct spi_nor_fixups is25lp256_fixups = {
	.post_bfpt = is25lp256_post_bfpt_fixups,
};

static const struct flash_info issi_parts[] = {
	/* ISSI */
	{ "is25cd512",  INFO(0x7f9d20, 0, 32 * 1024,   2, SECT_4K) },
	{ "is25lq040b", INFO(0x9d4013, 0, 64 * 1024,   8,
			     SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ) },
	{ "is25lp016d", INFO(0x9d6015, 0, 64 * 1024,  32,
			     SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ) },
	{ "is25lp080d", INFO(0x9d6014, 0, 64 * 1024,  16,
			     SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ) },
	{ "is25lp032",  INFO(0x9d6016, 0, 64 * 1024,  64,
			     SECT_4K | SPI_NOR_DUAL_READ) },
	{ "is25lp064",  INFO(0x9d6017, 0, 64 * 1024, 128,
			     SECT_4K | SPI_NOR_DUAL_READ) },
	{ "is25lp128",  INFO(0x9d6018, 0, 64 * 1024, 256,
			     SECT_4K | SPI_NOR_DUAL_READ) },
	{ "is25lp256",  INFO(0x9d6019, 0, 64 * 1024, 512,
			     SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ |
			     SPI_NOR_4B_OPCODES)
		.fixups = &is25lp256_fixups },
	{ "is25wp032",  INFO(0x9d7016, 0, 64 * 1024,  64,
			     SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ) },
	{ "is25wp064",  INFO(0x9d7017, 0, 64 * 1024, 128,
			     SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ) },
	{ "is25wp128",  INFO(0x9d7018, 0, 64 * 1024, 256,
			     SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ) },
	{ "is25wp256", INFO(0x9d7019, 0, 64 * 1024, 512,
			    SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ |
			    SPI_NOR_4B_OPCODES)
		.fixups = &is25lp256_fixups },

	/* PMC */
	{ "pm25lv512",   INFO(0,        0, 32 * 1024,    2, SECT_4K_PMC) },
	{ "pm25lv010",   INFO(0,        0, 32 * 1024,    4, SECT_4K_PMC) },
	{ "pm25lq032",   INFO(0x7f9d46, 0, 64 * 1024,   64, SECT_4K) },
};

static void issi_default_init(struct spi_nor *nor)
{
	nor->params->quad_enable = spi_nor_sr1_bit6_quad_enable;
}

static const struct spi_nor_fixups issi_fixups = {
	.default_init = issi_default_init,
};

const struct spi_nor_manufacturer spi_nor_issi = {
	.name = "issi",
	.parts = issi_parts,
	.nparts = ARRAY_SIZE(issi_parts),
	.fixups = &issi_fixups,
};

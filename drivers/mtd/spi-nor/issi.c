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
			   const struct sfdp_bfpt *bfpt)
{
	/*
	 * IS25LP256 supports 4B opcodes, but the BFPT advertises
	 * BFPT_DWORD1_ADDRESS_BYTES_3_ONLY.
	 * Overwrite the number of address bytes advertised by the BFPT.
	 */
	if ((bfpt->dwords[BFPT_DWORD(1)] & BFPT_DWORD1_ADDRESS_BYTES_MASK) ==
		BFPT_DWORD1_ADDRESS_BYTES_3_ONLY)
		nor->params->addr_nbytes = 4;

	return 0;
}

static const struct spi_nor_fixups is25lp256_fixups = {
	.post_bfpt = is25lp256_post_bfpt_fixups,
};

static void pm25lv_nor_late_init(struct spi_nor *nor)
{
	struct spi_nor_erase_map *map = &nor->params->erase_map;
	int i;

	/* The PM25LV series has a different 4k sector erase opcode */
	for (i = 0; i < SNOR_ERASE_TYPE_MAX; i++)
		if (map->erase_type[i].size == 4096)
			map->erase_type[i].opcode = SPINOR_OP_BE_4K_PMC;
}

static const struct spi_nor_fixups pm25lv_nor_fixups = {
	.late_init = pm25lv_nor_late_init,
};

static const struct flash_info issi_nor_parts[] = {
	/* ISSI */
	{ "is25cd512",  INFO(0x7f9d20, 0, 32 * 1024,   2)
		NO_SFDP_FLAGS(SECT_4K) },
	{ "is25lq040b", INFO(0x9d4013, 0, 64 * 1024,   8)
		NO_SFDP_FLAGS(SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ) },
	{ "is25lp016d", INFO(0x9d6015, 0, 64 * 1024,  32)
		NO_SFDP_FLAGS(SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ) },
	{ "is25lp080d", INFO(0x9d6014, 0, 64 * 1024,  16)
		NO_SFDP_FLAGS(SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ) },
	{ "is25lp032",  INFO(0x9d6016, 0, 64 * 1024,  64)
		NO_SFDP_FLAGS(SECT_4K | SPI_NOR_DUAL_READ) },
	{ "is25lp064",  INFO(0x9d6017, 0, 64 * 1024, 128)
		NO_SFDP_FLAGS(SECT_4K | SPI_NOR_DUAL_READ) },
	{ "is25lp128",  INFO(0x9d6018, 0, 64 * 1024, 256)
		NO_SFDP_FLAGS(SECT_4K | SPI_NOR_DUAL_READ) },
	{ "is25lp256",  INFO(0x9d6019, 0, 64 * 1024, 512)
		PARSE_SFDP
		FIXUP_FLAGS(SPI_NOR_4B_OPCODES)
		.fixups = &is25lp256_fixups },
	{ "is25wp032",  INFO(0x9d7016, 0, 64 * 1024,  64)
		NO_SFDP_FLAGS(SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ) },
	{ "is25wp064",  INFO(0x9d7017, 0, 64 * 1024, 128)
		NO_SFDP_FLAGS(SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ) },
	{ "is25wp128",  INFO(0x9d7018, 0, 64 * 1024, 256)
		NO_SFDP_FLAGS(SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ) },
	{ "is25wp256", INFO(0x9d7019, 0, 64 * 1024, 512)
		NO_SFDP_FLAGS(SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ)
		FIXUP_FLAGS(SPI_NOR_4B_OPCODES)
		FLAGS(SPI_NOR_QUAD_PP)
		.fixups = &is25lp256_fixups },

	/* PMC */
	{ "pm25lv512",   INFO(0,        0, 32 * 1024,    2)
		NO_SFDP_FLAGS(SECT_4K)
		.fixups = &pm25lv_nor_fixups
	},
	{ "pm25lv010",   INFO(0,        0, 32 * 1024,    4)
		NO_SFDP_FLAGS(SECT_4K)
		.fixups = &pm25lv_nor_fixups
	},
	{ "pm25lq032",   INFO(0x7f9d46, 0, 64 * 1024,   64)
		NO_SFDP_FLAGS(SECT_4K) },
};

static void issi_nor_default_init(struct spi_nor *nor)
{
	nor->params->quad_enable = spi_nor_sr1_bit6_quad_enable;
}

static const struct spi_nor_fixups issi_fixups = {
	.default_init = issi_nor_default_init,
};

const struct spi_nor_manufacturer spi_nor_issi = {
	.name = "issi",
	.parts = issi_nor_parts,
	.nparts = ARRAY_SIZE(issi_nor_parts),
	.fixups = &issi_fixups,
};

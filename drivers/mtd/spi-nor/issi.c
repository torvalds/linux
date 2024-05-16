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
	if ((bfpt->dwords[SFDP_DWORD(1)] & BFPT_DWORD1_ADDRESS_BYTES_MASK) ==
		BFPT_DWORD1_ADDRESS_BYTES_3_ONLY)
		nor->params->addr_nbytes = 4;

	return 0;
}

static const struct spi_nor_fixups is25lp256_fixups = {
	.post_bfpt = is25lp256_post_bfpt_fixups,
};

static int pm25lv_nor_late_init(struct spi_nor *nor)
{
	struct spi_nor_erase_map *map = &nor->params->erase_map;
	int i;

	/* The PM25LV series has a different 4k sector erase opcode */
	for (i = 0; i < SNOR_ERASE_TYPE_MAX; i++)
		if (map->erase_type[i].size == 4096)
			map->erase_type[i].opcode = SPINOR_OP_BE_4K_PMC;

	return 0;
}

static const struct spi_nor_fixups pm25lv_nor_fixups = {
	.late_init = pm25lv_nor_late_init,
};

static const struct flash_info issi_nor_parts[] = {
	{
		.name = "pm25lv512",
		.sector_size = SZ_32K,
		.size = SZ_64K,
		.no_sfdp_flags = SECT_4K,
		.fixups = &pm25lv_nor_fixups
	}, {
		.name = "pm25lv010",
		.sector_size = SZ_32K,
		.size = SZ_128K,
		.no_sfdp_flags = SECT_4K,
		.fixups = &pm25lv_nor_fixups
	}, {
		.id = SNOR_ID(0x7f, 0x9d, 0x20),
		.name = "is25cd512",
		.sector_size = SZ_32K,
		.size = SZ_64K,
		.no_sfdp_flags = SECT_4K,
	}, {
		.id = SNOR_ID(0x7f, 0x9d, 0x46),
		.name = "pm25lq032",
		.size = SZ_4M,
		.no_sfdp_flags = SECT_4K,
	}, {
		.id = SNOR_ID(0x9d, 0x40, 0x13),
		.name = "is25lq040b",
		.size = SZ_512K,
		.no_sfdp_flags = SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ,
	}, {
		.id = SNOR_ID(0x9d, 0x60, 0x14),
		.name = "is25lp080d",
		.size = SZ_1M,
		.no_sfdp_flags = SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ,
	}, {
		.id = SNOR_ID(0x9d, 0x60, 0x15),
		.name = "is25lp016d",
		.size = SZ_2M,
		.no_sfdp_flags = SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ,
	}, {
		.id = SNOR_ID(0x9d, 0x60, 0x16),
		.name = "is25lp032",
		.size = SZ_4M,
		.no_sfdp_flags = SECT_4K | SPI_NOR_DUAL_READ,
	}, {
		.id = SNOR_ID(0x9d, 0x60, 0x17),
		.name = "is25lp064",
		.size = SZ_8M,
		.no_sfdp_flags = SECT_4K | SPI_NOR_DUAL_READ,
	}, {
		.id = SNOR_ID(0x9d, 0x60, 0x18),
		.name = "is25lp128",
		.size = SZ_16M,
		.no_sfdp_flags = SECT_4K | SPI_NOR_DUAL_READ,
	}, {
		.id = SNOR_ID(0x9d, 0x60, 0x19),
		.name = "is25lp256",
		.fixups = &is25lp256_fixups,
		.fixup_flags = SPI_NOR_4B_OPCODES,
	}, {
		.id = SNOR_ID(0x9d, 0x70, 0x16),
		.name = "is25wp032",
		.size = SZ_4M,
		.no_sfdp_flags = SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ,
	}, {
		.id = SNOR_ID(0x9d, 0x70, 0x17),
		.size = SZ_8M,
		.name = "is25wp064",
		.no_sfdp_flags = SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ,
	}, {
		.id = SNOR_ID(0x9d, 0x70, 0x18),
		.name = "is25wp128",
		.size = SZ_16M,
		.no_sfdp_flags = SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ,
	}, {
		.id = SNOR_ID(0x9d, 0x70, 0x19),
		.name = "is25wp256",
		.flags = SPI_NOR_QUAD_PP,
		.fixups = &is25lp256_fixups,
		.fixup_flags = SPI_NOR_4B_OPCODES,
	}
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

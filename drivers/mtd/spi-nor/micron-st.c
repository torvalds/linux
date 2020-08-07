// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2005, Intec Automation Inc.
 * Copyright (C) 2014, Freescale Semiconductor, Inc.
 */

#include <linux/mtd/spi-nor.h>

#include "core.h"

static const struct flash_info micron_parts[] = {
	{ "mt35xu512aba", INFO(0x2c5b1a, 0, 128 * 1024, 512,
			       SECT_4K | USE_FSR | SPI_NOR_OCTAL_READ |
			       SPI_NOR_4B_OPCODES) },
	{ "mt35xu02g", INFO(0x2c5b1c, 0, 128 * 1024, 2048,
			    SECT_4K | USE_FSR | SPI_NOR_OCTAL_READ |
			    SPI_NOR_4B_OPCODES) },
};

static const struct flash_info st_parts[] = {
	{ "n25q016a",	 INFO(0x20bb15, 0, 64 * 1024,   32,
			      SECT_4K | SPI_NOR_QUAD_READ) },
	{ "n25q032",	 INFO(0x20ba16, 0, 64 * 1024,   64,
			      SPI_NOR_QUAD_READ) },
	{ "n25q032a",	 INFO(0x20bb16, 0, 64 * 1024,   64,
			      SPI_NOR_QUAD_READ) },
	{ "n25q064",     INFO(0x20ba17, 0, 64 * 1024,  128,
			      SECT_4K | SPI_NOR_QUAD_READ) },
	{ "n25q064a",    INFO(0x20bb17, 0, 64 * 1024,  128,
			      SECT_4K | SPI_NOR_QUAD_READ) },
	{ "n25q128a11",  INFO(0x20bb18, 0, 64 * 1024,  256,
			      SECT_4K | USE_FSR | SPI_NOR_QUAD_READ) },
	{ "n25q128a13",  INFO(0x20ba18, 0, 64 * 1024,  256,
			      SECT_4K | USE_FSR | SPI_NOR_QUAD_READ) },
	{ "mt25ql256a",  INFO6(0x20ba19, 0x104400, 64 * 1024,  512,
			       SECT_4K | USE_FSR | SPI_NOR_DUAL_READ |
			       SPI_NOR_QUAD_READ | SPI_NOR_4B_OPCODES) },
	{ "n25q256a",    INFO(0x20ba19, 0, 64 * 1024,  512, SECT_4K |
			      USE_FSR | SPI_NOR_DUAL_READ |
			      SPI_NOR_QUAD_READ) },
	{ "mt25qu256a",  INFO6(0x20bb19, 0x104400, 64 * 1024,  512,
			       SECT_4K | USE_FSR | SPI_NOR_DUAL_READ |
			       SPI_NOR_QUAD_READ | SPI_NOR_4B_OPCODES) },
	{ "n25q256ax1",  INFO(0x20bb19, 0, 64 * 1024,  512,
			      SECT_4K | USE_FSR | SPI_NOR_QUAD_READ) },
	{ "mt25ql512a",  INFO6(0x20ba20, 0x104400, 64 * 1024, 1024,
			       SECT_4K | USE_FSR | SPI_NOR_DUAL_READ |
			       SPI_NOR_QUAD_READ | SPI_NOR_4B_OPCODES) },
	{ "n25q512ax3",  INFO(0x20ba20, 0, 64 * 1024, 1024,
			      SECT_4K | USE_FSR | SPI_NOR_QUAD_READ |
			      SPI_NOR_HAS_LOCK | SPI_NOR_HAS_TB |
			      SPI_NOR_4BIT_BP | SPI_NOR_BP3_SR_BIT6) },
	{ "mt25qu512a",  INFO6(0x20bb20, 0x104400, 64 * 1024, 1024,
			       SECT_4K | USE_FSR | SPI_NOR_DUAL_READ |
			       SPI_NOR_QUAD_READ | SPI_NOR_4B_OPCODES) },
	{ "n25q512a",    INFO(0x20bb20, 0, 64 * 1024, 1024,
			      SECT_4K | USE_FSR | SPI_NOR_QUAD_READ |
			      SPI_NOR_HAS_LOCK | SPI_NOR_HAS_TB |
			      SPI_NOR_4BIT_BP | SPI_NOR_BP3_SR_BIT6) },
	{ "n25q00",      INFO(0x20ba21, 0, 64 * 1024, 2048,
			      SECT_4K | USE_FSR | SPI_NOR_QUAD_READ |
			      NO_CHIP_ERASE) },
	{ "n25q00a",     INFO(0x20bb21, 0, 64 * 1024, 2048,
			      SECT_4K | USE_FSR | SPI_NOR_QUAD_READ |
			      NO_CHIP_ERASE) },
	{ "mt25ql02g",   INFO(0x20ba22, 0, 64 * 1024, 4096,
			      SECT_4K | USE_FSR | SPI_NOR_QUAD_READ |
			      NO_CHIP_ERASE) },
	{ "mt25qu02g",   INFO(0x20bb22, 0, 64 * 1024, 4096,
			      SECT_4K | USE_FSR | SPI_NOR_QUAD_READ |
			      NO_CHIP_ERASE) },

	{ "m25p05",  INFO(0x202010,  0,  32 * 1024,   2, 0) },
	{ "m25p10",  INFO(0x202011,  0,  32 * 1024,   4, 0) },
	{ "m25p20",  INFO(0x202012,  0,  64 * 1024,   4, 0) },
	{ "m25p40",  INFO(0x202013,  0,  64 * 1024,   8, 0) },
	{ "m25p80",  INFO(0x202014,  0,  64 * 1024,  16, 0) },
	{ "m25p16",  INFO(0x202015,  0,  64 * 1024,  32, 0) },
	{ "m25p32",  INFO(0x202016,  0,  64 * 1024,  64, 0) },
	{ "m25p64",  INFO(0x202017,  0,  64 * 1024, 128, 0) },
	{ "m25p128", INFO(0x202018,  0, 256 * 1024,  64, 0) },

	{ "m25p05-nonjedec",  INFO(0, 0,  32 * 1024,   2, 0) },
	{ "m25p10-nonjedec",  INFO(0, 0,  32 * 1024,   4, 0) },
	{ "m25p20-nonjedec",  INFO(0, 0,  64 * 1024,   4, 0) },
	{ "m25p40-nonjedec",  INFO(0, 0,  64 * 1024,   8, 0) },
	{ "m25p80-nonjedec",  INFO(0, 0,  64 * 1024,  16, 0) },
	{ "m25p16-nonjedec",  INFO(0, 0,  64 * 1024,  32, 0) },
	{ "m25p32-nonjedec",  INFO(0, 0,  64 * 1024,  64, 0) },
	{ "m25p64-nonjedec",  INFO(0, 0,  64 * 1024, 128, 0) },
	{ "m25p128-nonjedec", INFO(0, 0, 256 * 1024,  64, 0) },

	{ "m45pe10", INFO(0x204011,  0, 64 * 1024,    2, 0) },
	{ "m45pe80", INFO(0x204014,  0, 64 * 1024,   16, 0) },
	{ "m45pe16", INFO(0x204015,  0, 64 * 1024,   32, 0) },

	{ "m25pe20", INFO(0x208012,  0, 64 * 1024,  4,       0) },
	{ "m25pe80", INFO(0x208014,  0, 64 * 1024, 16,       0) },
	{ "m25pe16", INFO(0x208015,  0, 64 * 1024, 32, SECT_4K) },

	{ "m25px16",    INFO(0x207115,  0, 64 * 1024, 32, SECT_4K) },
	{ "m25px32",    INFO(0x207116,  0, 64 * 1024, 64, SECT_4K) },
	{ "m25px32-s0", INFO(0x207316,  0, 64 * 1024, 64, SECT_4K) },
	{ "m25px32-s1", INFO(0x206316,  0, 64 * 1024, 64, SECT_4K) },
	{ "m25px64",    INFO(0x207117,  0, 64 * 1024, 128, 0) },
	{ "m25px80",    INFO(0x207114,  0, 64 * 1024, 16, 0) },
};

/**
 * st_micron_set_4byte_addr_mode() - Set 4-byte address mode for ST and Micron
 * flashes.
 * @nor:	pointer to 'struct spi_nor'.
 * @enable:	true to enter the 4-byte address mode, false to exit the 4-byte
 *		address mode.
 *
 * Return: 0 on success, -errno otherwise.
 */
static int st_micron_set_4byte_addr_mode(struct spi_nor *nor, bool enable)
{
	int ret;

	ret = spi_nor_write_enable(nor);
	if (ret)
		return ret;

	ret = spi_nor_set_4byte_addr_mode(nor, enable);
	if (ret)
		return ret;

	return spi_nor_write_disable(nor);
}

static void micron_st_default_init(struct spi_nor *nor)
{
	nor->flags |= SNOR_F_HAS_LOCK;
	nor->flags &= ~SNOR_F_HAS_16BIT_SR;
	nor->params->quad_enable = NULL;
	nor->params->set_4byte_addr_mode = st_micron_set_4byte_addr_mode;
}

static const struct spi_nor_fixups micron_st_fixups = {
	.default_init = micron_st_default_init,
};

const struct spi_nor_manufacturer spi_nor_micron = {
	.name = "micron",
	.parts = micron_parts,
	.nparts = ARRAY_SIZE(micron_parts),
	.fixups = &micron_st_fixups,
};

const struct spi_nor_manufacturer spi_nor_st = {
	.name = "st",
	.parts = st_parts,
	.nparts = ARRAY_SIZE(st_parts),
	.fixups = &micron_st_fixups,
};

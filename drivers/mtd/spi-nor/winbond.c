// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2005, Intec Automation Inc.
 * Copyright (C) 2014, Freescale Semiconductor, Inc.
 */

#include <linux/mtd/spi-nor.h>

#include "core.h"

static const struct flash_info winbond_parts[] = {
	/* Winbond -- w25x "blocks" are 64K, "sectors" are 4KiB */
	{ "w25x05", INFO(0xef3010, 0, 64 * 1024,  1,  SECT_4K) },
	{ "w25x10", INFO(0xef3011, 0, 64 * 1024,  2,  SECT_4K) },
	{ "w25x20", INFO(0xef3012, 0, 64 * 1024,  4,  SECT_4K) },
	{ "w25x40", INFO(0xef3013, 0, 64 * 1024,  8,  SECT_4K) },
	{ "w25x80", INFO(0xef3014, 0, 64 * 1024,  16, SECT_4K) },
	{ "w25x16", INFO(0xef3015, 0, 64 * 1024,  32, SECT_4K) },
	{ "w25q16dw", INFO(0xef6015, 0, 64 * 1024,  32,
			   SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ |
			   SPI_NOR_HAS_LOCK | SPI_NOR_HAS_TB) },
	{ "w25x32", INFO(0xef3016, 0, 64 * 1024,  64, SECT_4K) },
	{ "w25q16jv-im/jm", INFO(0xef7015, 0, 64 * 1024,  32,
				 SECT_4K | SPI_NOR_DUAL_READ |
				 SPI_NOR_QUAD_READ | SPI_NOR_HAS_LOCK |
				 SPI_NOR_HAS_TB) },
	{ "w25q20cl", INFO(0xef4012, 0, 64 * 1024,  4, SECT_4K) },
	{ "w25q20bw", INFO(0xef5012, 0, 64 * 1024,  4, SECT_4K) },
	{ "w25q20ew", INFO(0xef6012, 0, 64 * 1024,  4, SECT_4K) },
	{ "w25q32", INFO(0xef4016, 0, 64 * 1024,  64, SECT_4K) },
	{ "w25q32dw", INFO(0xef6016, 0, 64 * 1024,  64,
			   SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ |
			   SPI_NOR_HAS_LOCK | SPI_NOR_HAS_TB) },
	{ "w25q32jv", INFO(0xef7016, 0, 64 * 1024,  64,
			   SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ |
			   SPI_NOR_HAS_LOCK | SPI_NOR_HAS_TB)
	},
	{ "w25q32jwm", INFO(0xef8016, 0, 64 * 1024,  64,
			    SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ |
			    SPI_NOR_HAS_LOCK | SPI_NOR_HAS_TB) },
	{ "w25x64", INFO(0xef3017, 0, 64 * 1024, 128, SECT_4K) },
	{ "w25q64", INFO(0xef4017, 0, 64 * 1024, 128, SECT_4K) },
	{ "w25q64dw", INFO(0xef6017, 0, 64 * 1024, 128,
			   SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ |
			   SPI_NOR_HAS_LOCK | SPI_NOR_HAS_TB) },
	{ "w25q128fw", INFO(0xef6018, 0, 64 * 1024, 256,
			    SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ |
			    SPI_NOR_HAS_LOCK | SPI_NOR_HAS_TB) },
	{ "w25q128jv", INFO(0xef7018, 0, 64 * 1024, 256,
			    SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ |
			    SPI_NOR_HAS_LOCK | SPI_NOR_HAS_TB) },
	{ "w25q80", INFO(0xef5014, 0, 64 * 1024,  16, SECT_4K) },
	{ "w25q80bl", INFO(0xef4014, 0, 64 * 1024,  16, SECT_4K) },
	{ "w25q128", INFO(0xef4018, 0, 64 * 1024, 256, SECT_4K) },
	{ "w25q256", INFO(0xef4019, 0, 64 * 1024, 512,
			  SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ |
			  SPI_NOR_4B_OPCODES) },
	{ "w25q256jvm", INFO(0xef7019, 0, 64 * 1024, 512,
			     SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ) },
	{ "w25q256jw", INFO(0xef6019, 0, 64 * 1024, 512,
			     SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ) },
	{ "w25m512jv", INFO(0xef7119, 0, 64 * 1024, 1024,
			    SECT_4K | SPI_NOR_QUAD_READ | SPI_NOR_DUAL_READ) },
};

/**
 * winbond_set_4byte_addr_mode() - Set 4-byte address mode for Winbond flashes.
 * @nor:	pointer to 'struct spi_nor'.
 * @enable:	true to enter the 4-byte address mode, false to exit the 4-byte
 *		address mode.
 *
 * Return: 0 on success, -errno otherwise.
 */
static int winbond_set_4byte_addr_mode(struct spi_nor *nor, bool enable)
{
	int ret;

	ret = spi_nor_set_4byte_addr_mode(nor, enable);
	if (ret || enable)
		return ret;

	/*
	 * On Winbond W25Q256FV, leaving 4byte mode causes the Extended Address
	 * Register to be set to 1, so all 3-byte-address reads come from the
	 * second 16M. We must clear the register to enable normal behavior.
	 */
	ret = spi_nor_write_enable(nor);
	if (ret)
		return ret;

	ret = spi_nor_write_ear(nor, 0);
	if (ret)
		return ret;

	return spi_nor_write_disable(nor);
}

static void winbond_default_init(struct spi_nor *nor)
{
	nor->params->set_4byte_addr_mode = winbond_set_4byte_addr_mode;
}

static const struct spi_nor_fixups winbond_fixups = {
	.default_init = winbond_default_init,
};

const struct spi_nor_manufacturer spi_nor_winbond = {
	.name = "winbond",
	.parts = winbond_parts,
	.nparts = ARRAY_SIZE(winbond_parts),
	.fixups = &winbond_fixups,
};

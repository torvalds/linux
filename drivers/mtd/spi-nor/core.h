/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2005, Intec Automation Inc.
 * Copyright (C) 2014, Freescale Semiconductor, Inc.
 */

#ifndef __LINUX_MTD_SPI_NOR_INTERNAL_H
#define __LINUX_MTD_SPI_NOR_INTERNAL_H

#include "sfdp.h"

int spi_nor_sr1_bit6_quad_enable(struct spi_nor *nor);
int spi_nor_sr2_bit1_quad_enable(struct spi_nor *nor);
int spi_nor_sr2_bit7_quad_enable(struct spi_nor *nor);

ssize_t spi_nor_read_data(struct spi_nor *nor, loff_t from, size_t len,
			  u8 *buf);

int spi_nor_hwcaps_read2cmd(u32 hwcaps);
u8 spi_nor_convert_3to4_read(u8 opcode);
void spi_nor_set_pp_settings(struct spi_nor_pp_command *pp, u8 opcode,
			     enum spi_nor_protocol proto);

void spi_nor_set_erase_type(struct spi_nor_erase_type *erase, u32 size,
			    u8 opcode);
struct spi_nor_erase_region *
spi_nor_region_next(struct spi_nor_erase_region *region);
void spi_nor_init_uniform_erase_map(struct spi_nor_erase_map *map,
				    u8 erase_mask, u64 flash_size);

int spi_nor_post_bfpt_fixups(struct spi_nor *nor,
			     const struct sfdp_parameter_header *bfpt_header,
			     const struct sfdp_bfpt *bfpt,
			     struct spi_nor_flash_parameter *params);

#endif /* __LINUX_MTD_SPI_NOR_INTERNAL_H */

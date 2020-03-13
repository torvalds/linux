/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2005, Intec Automation Inc.
 * Copyright (C) 2014, Freescale Semiconductor, Inc.
 */

#ifndef __LINUX_MTD_SPI_NOR_INTERNAL_H
#define __LINUX_MTD_SPI_NOR_INTERNAL_H

#include "sfdp.h"

#define SPI_NOR_MAX_ID_LEN	6

/**
 * struct spi_nor_fixups - SPI NOR fixup hooks
 * @default_init: called after default flash parameters init. Used to tweak
 *                flash parameters when information provided by the flash_info
 *                table is incomplete or wrong.
 * @post_bfpt: called after the BFPT table has been parsed
 * @post_sfdp: called after SFDP has been parsed (is also called for SPI NORs
 *             that do not support RDSFDP). Typically used to tweak various
 *             parameters that could not be extracted by other means (i.e.
 *             when information provided by the SFDP/flash_info tables are
 *             incomplete or wrong).
 *
 * Those hooks can be used to tweak the SPI NOR configuration when the SFDP
 * table is broken or not available.
 */
struct spi_nor_fixups {
	void (*default_init)(struct spi_nor *nor);
	int (*post_bfpt)(struct spi_nor *nor,
			 const struct sfdp_parameter_header *bfpt_header,
			 const struct sfdp_bfpt *bfpt,
			 struct spi_nor_flash_parameter *params);
	void (*post_sfdp)(struct spi_nor *nor);
};

struct flash_info {
	char		*name;

	/*
	 * This array stores the ID bytes.
	 * The first three bytes are the JEDIC ID.
	 * JEDEC ID zero means "no ID" (mostly older chips).
	 */
	u8		id[SPI_NOR_MAX_ID_LEN];
	u8		id_len;

	/* The size listed here is what works with SPINOR_OP_SE, which isn't
	 * necessarily called a "sector" by the vendor.
	 */
	unsigned	sector_size;
	u16		n_sectors;

	u16		page_size;
	u16		addr_width;

	u32		flags;
#define SECT_4K			BIT(0)	/* SPINOR_OP_BE_4K works uniformly */
#define SPI_NOR_NO_ERASE	BIT(1)	/* No erase command needed */
#define SST_WRITE		BIT(2)	/* use SST byte programming */
#define SPI_NOR_NO_FR		BIT(3)	/* Can't do fastread */
#define SECT_4K_PMC		BIT(4)	/* SPINOR_OP_BE_4K_PMC works uniformly */
#define SPI_NOR_DUAL_READ	BIT(5)	/* Flash supports Dual Read */
#define SPI_NOR_QUAD_READ	BIT(6)	/* Flash supports Quad Read */
#define USE_FSR			BIT(7)	/* use flag status register */
#define SPI_NOR_HAS_LOCK	BIT(8)	/* Flash supports lock/unlock via SR */
#define SPI_NOR_HAS_TB		BIT(9)	/*
					 * Flash SR has Top/Bottom (TB) protect
					 * bit. Must be used with
					 * SPI_NOR_HAS_LOCK.
					 */
#define SPI_NOR_XSR_RDY		BIT(10)	/*
					 * S3AN flashes have specific opcode to
					 * read the status register.
					 * Flags SPI_NOR_XSR_RDY and SPI_S3AN
					 * use the same bit as one implies the
					 * other, but we will get rid of
					 * SPI_S3AN soon.
					 */
#define	SPI_S3AN		BIT(10)	/*
					 * Xilinx Spartan 3AN In-System Flash
					 * (MFR cannot be used for probing
					 * because it has the same value as
					 * ATMEL flashes)
					 */
#define SPI_NOR_4B_OPCODES	BIT(11)	/*
					 * Use dedicated 4byte address op codes
					 * to support memory size above 128Mib.
					 */
#define NO_CHIP_ERASE		BIT(12) /* Chip does not support chip erase */
#define SPI_NOR_SKIP_SFDP	BIT(13)	/* Skip parsing of SFDP tables */
#define USE_CLSR		BIT(14)	/* use CLSR command */
#define SPI_NOR_OCTAL_READ	BIT(15)	/* Flash supports Octal Read */
#define SPI_NOR_TB_SR_BIT6	BIT(16)	/*
					 * Top/Bottom (TB) is bit 6 of
					 * status register. Must be used with
					 * SPI_NOR_HAS_TB.
					 */

	/* Part specific fixup hooks. */
	const struct spi_nor_fixups *fixups;
};

/* Used when the "_ext_id" is two bytes at most */
#define INFO(_jedec_id, _ext_id, _sector_size, _n_sectors, _flags)	\
		.id = {							\
			((_jedec_id) >> 16) & 0xff,			\
			((_jedec_id) >> 8) & 0xff,			\
			(_jedec_id) & 0xff,				\
			((_ext_id) >> 8) & 0xff,			\
			(_ext_id) & 0xff,				\
			},						\
		.id_len = (!(_jedec_id) ? 0 : (3 + ((_ext_id) ? 2 : 0))),	\
		.sector_size = (_sector_size),				\
		.n_sectors = (_n_sectors),				\
		.page_size = 256,					\
		.flags = (_flags),

#define INFO6(_jedec_id, _ext_id, _sector_size, _n_sectors, _flags)	\
		.id = {							\
			((_jedec_id) >> 16) & 0xff,			\
			((_jedec_id) >> 8) & 0xff,			\
			(_jedec_id) & 0xff,				\
			((_ext_id) >> 16) & 0xff,			\
			((_ext_id) >> 8) & 0xff,			\
			(_ext_id) & 0xff,				\
			},						\
		.id_len = 6,						\
		.sector_size = (_sector_size),				\
		.n_sectors = (_n_sectors),				\
		.page_size = 256,					\
		.flags = (_flags),

#define CAT25_INFO(_sector_size, _n_sectors, _page_size, _addr_width, _flags)	\
		.sector_size = (_sector_size),				\
		.n_sectors = (_n_sectors),				\
		.page_size = (_page_size),				\
		.addr_width = (_addr_width),				\
		.flags = (_flags),

#define S3AN_INFO(_jedec_id, _n_sectors, _page_size)			\
		.id = {							\
			((_jedec_id) >> 16) & 0xff,			\
			((_jedec_id) >> 8) & 0xff,			\
			(_jedec_id) & 0xff				\
			},						\
		.id_len = 3,						\
		.sector_size = (8*_page_size),				\
		.n_sectors = (_n_sectors),				\
		.page_size = _page_size,				\
		.addr_width = 3,					\
		.flags = SPI_NOR_NO_FR | SPI_S3AN,

int spi_nor_write_enable(struct spi_nor *nor);
int spi_nor_write_disable(struct spi_nor *nor);
int spi_nor_set_4byte_addr_mode(struct spi_nor *nor, bool enable);
int spi_nor_write_ear(struct spi_nor *nor, u8 ear);
int spi_nor_wait_till_ready(struct spi_nor *nor);
int spi_nor_lock_and_prep(struct spi_nor *nor);
void spi_nor_unlock_and_unprep(struct spi_nor *nor);
int spi_nor_sr1_bit6_quad_enable(struct spi_nor *nor);
int spi_nor_sr2_bit1_quad_enable(struct spi_nor *nor);
int spi_nor_sr2_bit7_quad_enable(struct spi_nor *nor);

int spi_nor_xread_sr(struct spi_nor *nor, u8 *sr);
ssize_t spi_nor_read_data(struct spi_nor *nor, loff_t from, size_t len,
			  u8 *buf);
ssize_t spi_nor_write_data(struct spi_nor *nor, loff_t to, size_t len,
			   const u8 *buf);

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

static struct spi_nor __maybe_unused *mtd_to_spi_nor(struct mtd_info *mtd)
{
	return mtd->priv;
}

#endif /* __LINUX_MTD_SPI_NOR_INTERNAL_H */

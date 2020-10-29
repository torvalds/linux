/* SPDX-License-Identifier: GPL-2.0 */

/* Copyright (c) 2018 Rockchip Electronics Co. Ltd. */

#ifndef _SFC_NAND_MTD_H
#define _SFC_NAND_MTD_H

#define CONFIG_MTD_NAND_BBT_USING_FLASH

#ifndef nand_bbt_block_status
/* BBT related functions */
enum nand_bbt_block_status {
	NAND_BBT_BLOCK_STATUS_UNKNOWN,
	NAND_BBT_BLOCK_GOOD,
	NAND_BBT_BLOCK_WORN,
	NAND_BBT_BLOCK_RESERVED,
	NAND_BBT_BLOCK_FACTORY_BAD,
	NAND_BBT_BLOCK_NUM_STATUS,
};
#endif

/* nand_bbt option */
#define NANDDEV_BBT_USE_FLASH		BIT(0)
#define NANDDEV_BBT_SCANNED		BIT(1)

/* The maximum number of blocks to scan for a bbt */
#define NANDDEV_BBT_SCAN_MAXBLOCKS	4

struct snand_bbt {
	unsigned long *cache;
	unsigned int option;
	unsigned int version;
};

struct snand_mtd_dev {
	struct SFNAND_DEV *snand;
	struct mutex	*lock; /* to lock this object */
	struct mtd_info mtd;
	u8 *dma_buf;
	struct snand_bbt bbt;
};

static inline unsigned int snanddev_neraseblocks(const struct snand_mtd_dev *nand)
{
	unsigned int ret = nand->mtd.size >> nand->mtd.erasesize_shift;

	return ret;
}

static inline bool snanddev_bbt_is_initialized(struct snand_mtd_dev *nand)
{
	return !!nand->bbt.cache;
}

static inline unsigned int snanddev_bbt_pos_to_entry(struct snand_mtd_dev *nand,
						     const loff_t pos)
{
	return (unsigned int)(pos >> nand->mtd.erasesize_shift);
}

static inline struct mtd_info *snanddev_to_mtd(struct snand_mtd_dev *nand)
{
	return &nand->mtd;
}

static inline struct snand_mtd_dev *mtd_to_snanddev(struct mtd_info *mtd)
{
	return mtd->priv;
}

int snanddev_bbt_init(struct snand_mtd_dev *nand);
void snanddev_bbt_cleanup(struct snand_mtd_dev *nand);
int snanddev_bbt_update(struct snand_mtd_dev *nand);
int snanddev_bbt_get_block_status(const struct snand_mtd_dev *nand,
				  unsigned int entry);
int snanddev_bbt_set_block_status(struct snand_mtd_dev *nand, unsigned int entry,
				  enum nand_bbt_block_status status);

int sfc_nand_isbad_mtd(struct mtd_info *mtd, loff_t ofs);
int sfc_nand_erase_mtd(struct mtd_info *mtd, u32 addr);

#endif

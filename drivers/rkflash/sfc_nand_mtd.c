// SPDX-License-Identifier: GPL-2.0

/* Copyright (c) 2018 Rockchip Electronics Co. Ltd. */

#include <linux/kernel.h>
#include <linux/mtd/cfi.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/slab.h>
#include <linux/string.h>

#include "rkflash_blk.h"
#include "rkflash_debug.h"
#include "sfc_nand.h"
#include "sfc_nand_mtd.h"

#ifdef CONFIG_RK_SFC_NAND_MTD

static struct mtd_partition nand_parts[MAX_PART_COUNT];

static inline struct snand_mtd_dev *mtd_to_priv(struct mtd_info *ptr_mtd)
{
	return (struct snand_mtd_dev *)((char *)ptr_mtd -
		offsetof(struct snand_mtd_dev, mtd));
}

int sfc_nand_erase_mtd(struct mtd_info *mtd, u32 addr)
{
	int ret;

	ret = sfc_nand_erase_block(0, addr >> mtd->writesize_shift);
	if (ret) {
		rkflash_print_error("%s fail ret= %d\n", __func__, ret);
		ret = -EIO;
	}

	return ret;
}

static int sfc_nand_write_mtd(struct mtd_info *mtd, loff_t to,
			      struct mtd_oob_ops *ops)
{
	struct snand_mtd_dev *p_dev = mtd_to_priv(mtd);
	u8 *data = (u8 *)ops->datbuf;
	size_t remaining = ops->len;
	u32 ret = 0;

	rkflash_print_dio("%s addr= %llx len= %x\n", __func__, to, (u32)remaining);
	if ((to + remaining) > mtd->size || to & mtd->writesize_mask ||
	    remaining & mtd->writesize_mask || ops->ooblen) {
		rkflash_print_error("%s input error, %llx %x\n", __func__, to, (u32)remaining);

		return -EINVAL;
	}

	ops->retlen = 0;
	while (remaining) {
		memcpy(p_dev->dma_buf, data, mtd->writesize);
		memset(p_dev->dma_buf + mtd->writesize, 0xff, mtd->oobsize);
		ret = sfc_nand_prog_page_raw(0, to >> mtd->writesize_shift,
					     (u32 *)p_dev->dma_buf);
		if (ret != SFC_OK) {
			rkflash_print_error("%s addr %llx ret= %d\n",
					    __func__, to, ret);
			ret = -EIO;
			break;
		}

		data += mtd->writesize;
		ops->retlen += mtd->writesize;
		remaining -= mtd->writesize;
		to += mtd->writesize;
	}

	return ret;
}

static int sfc_nand_read_mtd(struct mtd_info *mtd, loff_t from,
			     struct mtd_oob_ops *ops)
{
	u8 *data = (u8 *)ops->datbuf;
	size_t remaining = ops->len;
	u32 ret = 0;
	bool ecc_failed = false;
	size_t page, off, real_size;
	int max_bitflips = 0;

	rkflash_print_dio("%s addr= %llx len= %x\n", __func__, from, (u32)remaining);
	if ((from + remaining) > mtd->size || ops->ooblen) {
		rkflash_print_error("%s input error, from= %llx len= %x oob= %x\n",
				    __func__, from, (u32)remaining, (u32)ops->ooblen);

		return -EINVAL;
	}

	ops->retlen = 0;
	while (remaining) {
		page = from >> mtd->writesize_shift;
		off = from & mtd->writesize_mask;
		real_size = min_t(u32, remaining, mtd->writesize - off);

		ret = sfc_nand_read(page, (u32 *)data, off, real_size);
		if (ret == SFC_NAND_HW_ERROR) {
			rkflash_print_error("%s addr %llx ret= %d\n",
					    __func__, from, ret);
			ret = -EIO;
			break;
		} else if (ret == SFC_NAND_ECC_ERROR) {
			rkflash_print_error("%s addr %llx ret= %d\n",
					    __func__, from, ret);
			ecc_failed = true;
			mtd->ecc_stats.failed++;
		} else if (ret == SFC_NAND_ECC_REFRESH) {
			rkflash_print_dio("%s addr %llx ret= %d\n",
					  __func__, from, ret);
			mtd->ecc_stats.corrected += 1;
			max_bitflips = 1;
		}

		ret = 0;
		data += real_size;
		ops->retlen += real_size;
		remaining -= real_size;
		from += real_size;
	}

	if (ecc_failed && !ret)
		ret = -EBADMSG;

	return ret ? ret : max_bitflips;
}

int sfc_nand_isbad_mtd(struct mtd_info *mtd, loff_t ofs)
{
	int ret;
	struct snand_mtd_dev *p_dev = mtd_to_priv(mtd);

	rkflash_print_dio("%s %llx\n", __func__, ofs);
	if (ofs & mtd->writesize_mask) {
		rkflash_print_error("%s %llx input error\n", __func__, ofs);

		return -EINVAL;
	}

	if (snanddev_bbt_is_initialized(p_dev)) {
		unsigned int entry;
		int status;

		entry = snanddev_bbt_pos_to_entry(p_dev, ofs);
		status = snanddev_bbt_get_block_status(p_dev, entry);
		/* Lazy block status retrieval */
		if (status == NAND_BBT_BLOCK_STATUS_UNKNOWN) {
			if ((int)sfc_nand_check_bad_block(0, ofs >> mtd->writesize_shift))
				status = NAND_BBT_BLOCK_FACTORY_BAD;
			else
				status = NAND_BBT_BLOCK_GOOD;

			snanddev_bbt_set_block_status(p_dev, entry, status);
		}

		if (status == NAND_BBT_BLOCK_WORN ||
		    status == NAND_BBT_BLOCK_FACTORY_BAD)
			return true;

		return false;
	}

	ret = (int)sfc_nand_check_bad_block(0, ofs >> mtd->writesize_shift);
	if (ret)
		pr_err("%s %llx is bad block\n", __func__, ofs);

	return ret;
}

static int sfc_nand_markbad_mtd(struct mtd_info *mtd, loff_t ofs)
{
	u32 ret;
	struct snand_mtd_dev *p_dev = mtd_to_priv(mtd);
	unsigned int entry;

	rkflash_print_error("%s %llx\n", __func__, ofs);
	if (ofs & mtd->erasesize_mask) {
		rkflash_print_error("%s %llx input error\n", __func__, ofs);

		return -EINVAL;
	}

	if (sfc_nand_isbad_mtd(mtd, ofs))
		return 0;

	/* Erase block before marking it bad. */
	ret = sfc_nand_erase_block(0, ofs >> mtd->writesize_shift);
	if (ret)
		rkflash_print_error("%s erase fail ofs 0x%llx ret=%d\n",
				    __func__, ofs, ret);

	/* Mark bad. */
	ret = sfc_nand_mark_bad_block(0, ofs >> mtd->writesize_shift);
	if (ret)
		rkflash_print_error("%s mark fail ofs 0x%llx ret=%d\n",
				    __func__, ofs, ret);

	if (!snanddev_bbt_is_initialized(p_dev))
		goto out;

	entry = snanddev_bbt_pos_to_entry(p_dev, ofs);
	ret = snanddev_bbt_set_block_status(p_dev, entry, NAND_BBT_BLOCK_WORN);
	if (ret)
		goto out;

	ret = snanddev_bbt_update(p_dev);
out:
	/* Mark bad recheck */
	if (sfc_nand_check_bad_block(0, ofs >> mtd->writesize_shift)) {
		mtd->ecc_stats.badblocks++;
		ret = 0;
	} else {
		rkflash_print_error("%s recheck fail ofs 0x%llx ret=%d\n",
				    __func__, ofs, ret);
		ret = -EIO;
	}

	return ret;
}

static int sfc_erase_mtd(struct mtd_info *mtd, struct erase_info *instr)
{
	struct snand_mtd_dev *p_dev = mtd_to_priv(mtd);
	struct snand_mtd_dev *nand = mtd_to_snanddev(mtd);
	u64 addr, remaining;
	int ret = 0;

	mutex_lock(p_dev->lock);
	addr = instr->addr;
	remaining = instr->len;
	rkflash_print_dio("%s addr= %llx len= %llx\n", __func__, addr, remaining);
	if ((addr + remaining) > mtd->size || addr & mtd->erasesize_mask) {
		ret = -EINVAL;
		goto out;
	}

	while (remaining) {
		ret = snanddev_bbt_get_block_status(nand, addr >> mtd->erasesize_shift);
		if (ret == NAND_BBT_BLOCK_WORN ||
		    ret == NAND_BBT_BLOCK_FACTORY_BAD) {
			rkflash_print_error("attempt to erase a bad/reserved block @%llx\n",
					    addr >> mtd->erasesize_shift);
			addr += mtd->erasesize;
			remaining -= mtd->erasesize;
			continue;
		}

		ret = sfc_nand_erase_mtd(mtd, addr);
		if (ret) {
			rkflash_print_error("%s fail addr 0x%llx ret=%d\n",
					    __func__, addr, ret);
			instr->fail_addr = addr;

			ret = -EIO;
			goto out;
		}

		addr += mtd->erasesize;
		remaining -= mtd->erasesize;
	}

out:
	mutex_unlock(p_dev->lock);

	return ret;
}

static int sfc_write_mtd(struct mtd_info *mtd, loff_t to, size_t len,
			 size_t *retlen, const u_char *buf)
{
	int ret;
	struct snand_mtd_dev *p_dev = mtd_to_priv(mtd);
	struct mtd_oob_ops ops;

	mutex_lock(p_dev->lock);
	memset(&ops, 0, sizeof(struct mtd_oob_ops));
	ops.datbuf = (u8 *)buf;
	ops.len = len;
	ret = sfc_nand_write_mtd(mtd, to, &ops);
	*retlen = ops.retlen;
	mutex_unlock(p_dev->lock);

	return ret;
}

static int sfc_read_mtd(struct mtd_info *mtd, loff_t from, size_t len,
			size_t *retlen, u_char *buf)
{
	int ret;
	struct snand_mtd_dev *p_dev = mtd_to_priv(mtd);
	struct mtd_oob_ops ops;

	mutex_lock(p_dev->lock);
	memset(&ops, 0, sizeof(struct mtd_oob_ops));
	ops.datbuf = buf;
	ops.len = len;
	ret = sfc_nand_read_mtd(mtd, from, &ops);
	*retlen = ops.retlen;
	mutex_unlock(p_dev->lock);

	return ret;
}

static int sfc_isbad_mtd(struct mtd_info *mtd, loff_t ofs)
{
	int ret;
	struct snand_mtd_dev *p_dev = mtd_to_priv(mtd);

	mutex_lock(p_dev->lock);
	ret = sfc_nand_isbad_mtd(mtd, ofs);
	mutex_unlock(p_dev->lock);

	return ret;
}

static int sfc_markbad_mtd(struct mtd_info *mtd, loff_t ofs)
{
	u32 ret;
	struct snand_mtd_dev *p_dev = mtd_to_priv(mtd);

	mutex_lock(p_dev->lock);
	ret = sfc_nand_markbad_mtd(mtd, ofs);
	mutex_unlock(p_dev->lock);

	return ret;
}

/*
 * if not support rk_partition and partition is confirmed, you can define
 * strust def_nand_part by adding new partition like following example:
 *	{"u-boot", 0x1000 * 512, 0x2000 * 512},
 * Note.
 * 1. New partition format {name. size, offset}
 * 2. Unit:Byte
 * 3. Last partition 'size' can be set 0xFFFFFFFFF to fully user left space.
 */
static struct mtd_partition def_nand_part[] = {};

int sfc_nand_mtd_init(struct SFNAND_DEV *p_dev, struct mutex *lock)
{
	int ret, i, part_num = 0;
	int capacity;
	struct snand_mtd_dev *nand = kzalloc(sizeof(*nand), GFP_KERNEL);

	if (!nand) {
		rkflash_print_error("%s %d alloc failed\n", __func__, __LINE__);
		return -ENOMEM;
	}

	nand->snand = p_dev;
	capacity = (1 << p_dev->capacity) << 9;
	nand->mtd.name = "spi-nand0";
	nand->mtd.type = MTD_NANDFLASH;
	nand->mtd.writesize = p_dev->page_size * SFC_NAND_SECTOR_SIZE;
	nand->mtd.flags = MTD_CAP_NANDFLASH;
	nand->mtd.size = capacity;
	nand->mtd._erase = sfc_erase_mtd;
	nand->mtd._read = sfc_read_mtd;
	nand->mtd._write = sfc_write_mtd;
	nand->mtd._block_isbad = sfc_isbad_mtd;
	nand->mtd._block_markbad = sfc_markbad_mtd;
	nand->mtd.oobsize = 16 * p_dev->page_size;
	nand->mtd.bitflip_threshold = 2;
	nand->mtd.erasesize = p_dev->block_size * SFC_NAND_SECTOR_SIZE;
	nand->mtd.writebufsize = p_dev->page_size * SFC_NAND_SECTOR_SIZE;
	nand->mtd.erasesize_shift = ffs(nand->mtd.erasesize) - 1;
	nand->mtd.erasesize_mask = (1 << nand->mtd.erasesize_shift) - 1;
	nand->mtd.writesize_shift = ffs(nand->mtd.writesize) - 1;
	nand->mtd.writesize_mask = (1 << nand->mtd.writesize_shift) - 1;
	nand->mtd.bitflip_threshold = 1;
	nand->mtd.priv = nand;
	nand->lock = lock;
	nand->dma_buf = kmalloc(SFC_NAND_PAGE_MAX_SIZE, GFP_KERNEL | GFP_DMA);
	if (!nand->dma_buf) {
		rkflash_print_error("%s dma_buf alloc failed\n", __func__);
		ret = -ENOMEM;
		goto error_out;
	}

	nand->bbt.option |= NANDDEV_BBT_USE_FLASH;
	ret = snanddev_bbt_init(nand);
	if (ret) {
		rkflash_print_error("snanddev_bbt_init failed, ret= %d\n", ret);
		return ret;
	}

	part_num = ARRAY_SIZE(def_nand_part);
	for (i = 0; i < part_num; i++) {
		nand_parts[i].name =
			kstrdup(def_nand_part[i].name,
				GFP_KERNEL);
		if (def_nand_part[i].size == 0xFFFFFFFF)
			def_nand_part[i].size = capacity -
				def_nand_part[i].offset;
		nand_parts[i].offset =
			def_nand_part[i].offset;
		nand_parts[i].size =
			def_nand_part[i].size;
		nand_parts[i].mask_flags = 0;
	}

	ret = mtd_device_register(&nand->mtd, nand_parts, part_num);
	if (ret) {
		pr_err("%s register mtd fail %d\n", __func__, ret);
	} else {
		pr_info("%s register mtd succuss\n", __func__);

		return 0;
	}

	kfree(nand->dma_buf);
error_out:
	kfree(nand);

	return ret;
}

#endif

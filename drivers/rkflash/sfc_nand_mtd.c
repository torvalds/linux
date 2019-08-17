// SPDX-License-Identifier: GPL-2.0

/* Copyright (c) 2018 Rockchip Electronics Co. Ltd. */

#include <linux/kernel.h>
#include <linux/mtd/cfi.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/slab.h>
#include <linux/string.h>

#include "sfc.h"
#include "sfc_nand.h"
#include "rkflash_blk.h"
#include "rkflash_debug.h"

static struct mtd_partition nand_parts[MAX_PART_COUNT];
static DEFINE_MUTEX(sfc_nand_mtd_lock);

static inline struct SFNAND_DEV *mtd_to_sfc(struct mtd_info *ptr_mtd)
{
	return (struct SFNAND_DEV *)((char *)ptr_mtd -
		offsetof(struct SFNAND_DEV, mtd));
}

static int sfc_erase_mtd(struct mtd_info *mtd, struct erase_info *instr)
{
	int ret;
	u32 addr, len;

	addr = instr->addr;
	len = instr->len;
	rkflash_print_dio("%s addr= %x len= %x\n",
			  __func__, addr, len);

	if ((addr + len) > mtd->size || addr & mtd->erasesize_mask)
		return -EINVAL;

	mutex_lock(&sfc_nand_mtd_lock);
	while (len) {
		ret = sfc_nand_erase_block(0, addr >> mtd->writesize_shift);
		if (ret) {
			rkflash_print_dio("sfc_nand_erase addr 0x%x ret=%d\n",
					  addr, ret);
			instr->state = MTD_ERASE_FAILED;
			mutex_unlock(&sfc_nand_mtd_lock);
			return -EIO;
		}

		addr += mtd->erasesize;
		len -= mtd->erasesize;
	}

	mutex_unlock(&sfc_nand_mtd_lock);

	instr->state = MTD_ERASE_DONE;

	return 0;
}

static int sfc_write_mtd(struct mtd_info *mtd, loff_t to, size_t len,
			 size_t *retlen, const u_char *buf)
{
	u32 ret = 0;
	u8 *data = (u8 *)buf;
	u32 spare[2];
	u32 page_addr, page_num, page_size;

	rkflash_print_dio("%s addr= %llx len= %x\n", __func__, to, (u32)len);
	if ((to + len) > mtd->size || to & mtd->writesize_mask)
		return -EINVAL;

	mutex_lock(&sfc_nand_mtd_lock);
	page_size = mtd->writesize;
	page_addr = (u32)to / page_size;
	page_num = len / page_size;
	spare[0] = 0xffffffff;
	while (page_num > 0) {
		ret = sfc_nand_prog_page(0, page_addr, (u32 *)data, spare);
		if (ret != SFC_OK) {
			rkflash_print_dio("%s addr %x ret= %d\n",
					  __func__, page_addr, ret);
			break;
		}

		*retlen += page_size;
		page_num--;
		page_addr++;
		data += page_size;
	}
	mutex_unlock(&sfc_nand_mtd_lock);

	return 0;
}

static int sfc_read_mtd(struct mtd_info *mtd, loff_t from, size_t len,
			size_t *retlen, u_char *buf)
{
	u32 ret = 0;
	u8 *data = (u8 *)buf;
	u32 spare[2];
	u32 page_size;
	bool ecc_failed = false;
	int max_bitflips = 0;
	u32 addr, off, real_size, remaing;
	struct SFNAND_DEV *p_dev = mtd_to_sfc(mtd);

	rkflash_print_dio("%s addr= %llx len= %x\n", __func__, from, (u32)len);
	if ((from + len) > mtd->size)
		return -EINVAL;

	mutex_lock(&sfc_nand_mtd_lock);
	page_size = mtd->writesize;
	*retlen = 0;
	addr = (u32)from;
	remaing = (u32)len;

	while (remaing) {
		memset(p_dev->dma_buf, 0xa5, SFC_NAND_PAGE_MAX_SIZE);
		ret = sfc_nand_read_page(0, addr / page_size, (u32 *)p_dev->dma_buf, spare);
		if (ret == SFC_NAND_HW_ERROR)
			break;

		if (ret == SFC_NAND_ECC_ERROR) {
			rkflash_print_dio("%s addr %x ret= %d\n",
					  __func__, addr, ret);
			mtd->ecc_stats.failed++;
			ecc_failed = true;
			ret = 0;
		} else if (ret == SFC_NAND_ECC_REFRESH) {
			rkflash_print_dio("%s addr %x ret= %d\n",
					  __func__, addr, ret);
			mtd->ecc_stats.corrected += 1;
			max_bitflips = 1;
		}

		off = addr & mtd->writesize_mask;
		real_size = min_t(u32, remaing, page_size);
		if (addr / page_size != (addr + real_size - 1) / page_size)
			real_size = page_size - addr % page_size;
		memcpy(data, p_dev->dma_buf + off, real_size);
		*retlen += real_size;
		remaing -= real_size;
		addr += real_size;
		data += real_size;
	}
	mutex_unlock(&sfc_nand_mtd_lock);

	if (ecc_failed && !ret)
		ret = -EBADMSG;

	return ret ? (-EIO) : max_bitflips;
}

static int sfc_isbad_mtd(struct mtd_info *mtd, loff_t ofs)
{
	int ret;

	rkflash_print_dio("%s %llx\n", __func__, ofs);
	if (ofs & mtd->writesize_mask)
		return -EINVAL;

	mutex_lock(&sfc_nand_mtd_lock);
	ret = (int)sfc_nand_check_bad_block(0, ofs >> mtd->writesize_shift);
	if (ret)
		pr_err("%s %llx is bad block\n", __func__, ofs);
	mutex_unlock(&sfc_nand_mtd_lock);

	return ret;
}

static int sfc_markbad_mtd(struct mtd_info *mtd, loff_t ofs)
{
	u32 ret;

	rkflash_print_dio("%s %llx\n", __func__, ofs);
	if (ofs & mtd->erasesize_mask)
		return -EINVAL;

	if (sfc_isbad_mtd(mtd, ofs))
		return 0;

	mutex_lock(&sfc_nand_mtd_lock);
	/* Erase block before marking it bad. */
	ret = sfc_nand_erase_block(0, ofs >> mtd->writesize_shift);
	if (ret)
		goto out;
	/* Mark bad. */
	ret = sfc_nand_mark_bad_block(0, ofs >> mtd->writesize_shift);
	if (ret)
		goto out;

out:
	mutex_unlock(&sfc_nand_mtd_lock);
	if (!ret) {
		mtd->ecc_stats.badblocks++;
		ret = -EIO;
	}

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

int sfc_nand_mtd_init(struct SFNAND_DEV *p_dev)
{
	int ret, i, part_num = 0;
	int capacity;

	capacity = (1 << p_dev->capacity) << 9;
	p_dev->mtd.name = "spi-nand0";
	p_dev->mtd.type = MTD_NANDFLASH;
	p_dev->mtd.writesize = p_dev->page_size * SFC_NAND_SECTOR_SIZE;
	p_dev->mtd.flags = MTD_CAP_NANDFLASH;
	p_dev->mtd.size = capacity;
	p_dev->mtd._erase = sfc_erase_mtd;
	p_dev->mtd._read = sfc_read_mtd;
	p_dev->mtd._write = sfc_write_mtd;
	p_dev->mtd._block_isbad = sfc_isbad_mtd;
	p_dev->mtd._block_markbad = sfc_markbad_mtd;
	p_dev->mtd.erasesize = p_dev->block_size * SFC_NAND_SECTOR_SIZE;
	p_dev->mtd.writebufsize = p_dev->page_size * SFC_NAND_SECTOR_SIZE;
	p_dev->mtd.erasesize_shift = ffs(p_dev->mtd.erasesize) - 1;
	p_dev->mtd.erasesize_mask = (1 << p_dev->mtd.erasesize_shift) - 1;
	p_dev->mtd.writesize_shift = ffs(p_dev->mtd.writesize) - 1;
	p_dev->mtd.writesize_mask = (1 << p_dev->mtd.writesize_shift) - 1;
	p_dev->mtd.bitflip_threshold = 1;
	p_dev->dma_buf = kmalloc(SFC_NAND_PAGE_MAX_SIZE, GFP_KERNEL | GFP_DMA);

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

	ret = mtd_device_register(&p_dev->mtd, nand_parts, part_num);
	if (ret) {
		kfree(p_dev->dma_buf);
		pr_err("%s register mtd fail %d\n", __func__, ret);
	} else {
		pr_info("%s register mtd succuss\n", __func__);
	}

	return ret;
}


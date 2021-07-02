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

struct snor_mtd_dev {
	struct SFNOR_DEV *snor;
	struct mutex	*lock; /* to lock this object */
	struct mtd_info mtd;
	u8 *dma_buf;
};

static struct mtd_partition nor_parts[MAX_PART_COUNT];

#define SFC_NOR_MTD_DMA_MAX 8192

static inline struct snor_mtd_dev *mtd_to_priv(struct mtd_info *ptr_mtd)
{
	return (struct snor_mtd_dev *)((char *)ptr_mtd -
		offsetof(struct snor_mtd_dev, mtd));
}

static int sfc_erase_mtd(struct mtd_info *mtd, struct erase_info *instr)
{
	int ret;
	struct snor_mtd_dev *p_dev = mtd_to_priv(mtd);
	u32 addr, len;
	u32 rem;

	addr = instr->addr;
	len = instr->len;
	rkflash_print_dio("%s addr= %x len= %x\n",
			  __func__, addr, len);

	if ((addr + len) > mtd->size)
		return -EINVAL;

	div_u64_rem(instr->len, mtd->erasesize, &rem);
	if (rem)
		return -EINVAL;

	mutex_lock(p_dev->lock);

	if (len == p_dev->mtd.size) {
		ret = snor_erase(p_dev->snor, 0, ERASE_CHIP);
		if (ret) {
			rkflash_print_error("snor_erase CHIP 0x%x ret=%d\n",
					    addr, ret);
			instr->fail_addr = addr;
			mutex_unlock(p_dev->lock);
			return -EIO;
		}
	} else {
		while (len > 0) {
			ret = snor_erase(p_dev->snor, addr, ERASE_BLOCK64K);
			if (ret) {
				rkflash_print_error("snor_erase 0x%x ret=%d\n",
						    addr, ret);
				instr->fail_addr = addr;
				mutex_unlock(p_dev->lock);
				return -EIO;
			}
			addr += mtd->erasesize;
			len -= mtd->erasesize;
		}
	}

	mutex_unlock(p_dev->lock);

	return 0;
}

static int sfc_write_mtd(struct mtd_info *mtd, loff_t to, size_t len,
			 size_t *retlen, const u_char *buf)
{
	int status;
	u32 addr, size, chunk, padding;
	u32 page_align;
	struct snor_mtd_dev *p_dev = mtd_to_priv(mtd);

	rkflash_print_dio("%s addr= %llx len= %x\n", __func__, to, (u32)len);
	if ((to + len) > mtd->size)
		return -EINVAL;

	mutex_lock(p_dev->lock);

	addr = to;
	size = len;

	while (size > 0) {
		page_align = addr & (NOR_PAGE_SIZE - 1);
		chunk = size;
		if (chunk > (NOR_PAGE_SIZE - page_align))
			chunk = NOR_PAGE_SIZE - page_align;
		memcpy(p_dev->dma_buf, buf, chunk);
		padding = 0;
		if (chunk < NOR_PAGE_SIZE) {
			/* 4 bytes algin */
			padding = ((chunk + 3) & 0xFFFC) - chunk;
			memset(p_dev->dma_buf + chunk, 0xFF, padding);
		}
		status = snor_prog_page(p_dev->snor, addr, p_dev->dma_buf,
					chunk + padding);
		if (status != SFC_OK) {
			rkflash_print_error("snor_prog_page %x ret= %d\n",
					    addr, status);
			*retlen = len - size;
			mutex_unlock(p_dev->lock);
			return status;
		}

		size -= chunk;
		addr += chunk;
		buf += chunk;
	}
	*retlen = len;
	mutex_unlock(p_dev->lock);

	return 0;
}

static int sfc_read_mtd(struct mtd_info *mtd, loff_t from, size_t len,
			size_t *retlen, u_char *buf)
{
	u32 addr, size, chunk;
	u8 *p_buf =  (u8 *)buf;
	int ret = SFC_OK;
	struct snor_mtd_dev *p_dev = mtd_to_priv(mtd);

	rkflash_print_dio("%s addr= %llx len= %x\n", __func__, from, (u32)len);
	if ((from + len) > mtd->size)
		return -EINVAL;

	mutex_lock(p_dev->lock);

	addr = from;
	size = len;

	while (size > 0) {
		chunk = (size < SFC_NOR_MTD_DMA_MAX) ? size : SFC_NOR_MTD_DMA_MAX;
		ret = snor_read_data(p_dev->snor, addr, p_dev->dma_buf, chunk);
		if (ret != SFC_OK) {
			rkflash_print_error("snor_read_data %x ret=%d\n", addr, ret);
			*retlen = len - size;
			mutex_unlock(p_dev->lock);
			return ret;
		}
		memcpy(p_buf, p_dev->dma_buf, chunk);
		size -= chunk;
		addr += chunk;
		p_buf += chunk;
	}

	*retlen = len;
	mutex_unlock(p_dev->lock);
	return 0;
}

/*
 * if not support rk_partition and partition is confirmed, you can define
 * strust def_nor_part by adding new partition like following example:
 *	{"u-boot", 0x1000 * 512, 0x2000 * 512},
 * Note.
 * 1. New partition format {name. size, offset}
 * 2. Unit:Byte
 * 3. Last partition 'size' can be set 0xFFFFFFFFF to fully user left space.
 */
struct mtd_partition def_nor_part[] = {};

int sfc_nor_mtd_init(struct SFNOR_DEV *p_dev, struct mutex *lock)
{
	int ret, i, part_num = 0;
	int capacity;
	struct STRUCT_PART_INFO *g_part;  /* size 2KB */
	struct snor_mtd_dev *priv_dev = kzalloc(sizeof(*priv_dev), GFP_KERNEL);

	if (!priv_dev) {
		rkflash_print_error("%s %d alloc failed\n", __func__, __LINE__);
		return -ENOMEM;
	}

	priv_dev->snor = p_dev;
	capacity = p_dev->capacity;
	priv_dev->mtd.name = "sfc_nor";
	priv_dev->mtd.type = MTD_NORFLASH;
	priv_dev->mtd.writesize = 1;
	priv_dev->mtd.flags = MTD_CAP_NORFLASH;
	/* see snor_write */
	priv_dev->mtd.size = (u64)capacity << 9;
	priv_dev->mtd._erase = sfc_erase_mtd;
	priv_dev->mtd._read = sfc_read_mtd;
	priv_dev->mtd._write = sfc_write_mtd;
	priv_dev->mtd.erasesize = p_dev->blk_size << 9;
	priv_dev->mtd.writebufsize = NOR_PAGE_SIZE;
	priv_dev->lock = lock;
	priv_dev->dma_buf = (u8 *)__get_free_pages(GFP_KERNEL | GFP_DMA32, get_order(SFC_NOR_MTD_DMA_MAX));
	if (!priv_dev->dma_buf) {
		rkflash_print_error("%s %d alloc failed\n", __func__, __LINE__);
		ret = -ENOMEM;
		goto error_out;
	}

	g_part = kmalloc(sizeof(*g_part), GFP_KERNEL);
	if (!g_part) {
		ret = -ENOMEM;
		goto error_out;
	}
	part_num = 0;
	if (snor_read(p_dev, 0, 4, g_part) == 4) {
		if (g_part->hdr.ui_fw_tag == RK_PARTITION_TAG) {
			part_num = g_part->hdr.ui_part_entry_count;
			for (i = 0; i < part_num; i++) {
				nor_parts[i].name =
					kstrdup(g_part->part[i].sz_name,
						GFP_KERNEL);
				if (g_part->part[i].ui_pt_sz == 0xFFFFFFFF)
					g_part->part[i].ui_pt_sz = capacity -
						g_part->part[i].ui_pt_off;
				nor_parts[i].offset =
					(u64)g_part->part[i].ui_pt_off << 9;
				nor_parts[i].size =
					(u64)g_part->part[i].ui_pt_sz << 9;
				nor_parts[i].mask_flags = 0;
			}
		} else {
			part_num = ARRAY_SIZE(def_nor_part);
			for (i = 0; i < part_num; i++) {
				nor_parts[i].name =
					kstrdup(def_nor_part[i].name,
						GFP_KERNEL);
				if (def_nor_part[i].size == 0xFFFFFFFF)
					def_nor_part[i].size = (capacity << 9) -
						def_nor_part[i].offset;
				nor_parts[i].offset =
					def_nor_part[i].offset;
				nor_parts[i].size =
					def_nor_part[i].size;
				nor_parts[i].mask_flags = 0;
			}
		}
	}
	kfree(g_part);
	ret = mtd_device_register(&priv_dev->mtd, nor_parts, part_num);
	if (ret) {
		pr_err("%s register mtd fail %d\n", __func__, ret);
	} else {
		pr_info("%s register mtd succuss\n", __func__);

		return 0;
	}

	free_pages((unsigned long)priv_dev->dma_buf, get_order(SFC_NOR_MTD_DMA_MAX));
error_out:
	kfree(priv_dev);

	return ret;
}

// SPDX-License-Identifier: GPL-2.0

/* Copyright (c) 2018 Rockchip Electronics Co. Ltd. */

#include <linux/kernel.h>
#include <linux/mtd/cfi.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/slab.h>
#include <linux/string.h>

#include "sfc_nor.h"
#include "rkflash_blk.h"
#include "rkflash_debug.h"

static struct mtd_partition nor_parts[MAX_PART_COUNT];

static inline struct SFNOR_DEV *mtd_to_sfc(struct mtd_info *ptr_mtd)
{
	return (struct SFNOR_DEV *)((char *)ptr_mtd -
		offsetof(struct SFNOR_DEV, mtd));
}

static int sfc_erase_mtd(struct mtd_info *mtd, struct erase_info *instr)
{
	int ret;
	struct SFNOR_DEV *p_dev = mtd_to_sfc(mtd);
	u32 addr, len;
	u32 rem;

	if ((instr->addr + instr->len) > p_dev->capacity << 9)
		return -EINVAL;

	div_u64_rem(instr->len, mtd->erasesize, &rem);
	if (rem)
		return -EINVAL;

	mutex_lock(&p_dev->lock);

	addr = instr->addr;
	len = instr->len;

	if (len == p_dev->mtd.size) {
		ret = snor_erase(p_dev, 0, CMD_CHIP_ERASE);
		if (ret) {
			PRINT_SFC_E("snor_erase CHIP 0x%x ret=%d\n",
				    addr, ret);
			instr->state = MTD_ERASE_FAILED;
			mutex_unlock(&p_dev->lock);
			return -EIO;
		}
	} else {
		while (len > 0) {
			ret = snor_erase(p_dev, addr, ERASE_BLOCK64K);
			if (ret) {
				PRINT_SFC_E("snor_erase 0x%x ret=%d\n",
					    addr, ret);
				instr->state = MTD_ERASE_FAILED;
				mutex_unlock(&p_dev->lock);
				return -EIO;
			}
			addr += mtd->erasesize;
			len -= mtd->erasesize;
		}
	}

	mutex_unlock(&p_dev->lock);

	instr->state = MTD_ERASE_DONE;
	mtd_erase_callback(instr);

	return 0;
}

static int sfc_write_mtd(struct mtd_info *mtd, loff_t to, size_t len,
			 size_t *retlen, const u_char *buf)
{
	int status;
	u32 addr, size, chunk, padding;
	u32 page_align;
	struct SFNOR_DEV *p_dev = mtd_to_sfc(mtd);

	if ((to + len) > p_dev->capacity << 9)
		return -EINVAL;

	mutex_lock(&p_dev->lock);

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
		status = snor_prog_page(p_dev, addr, p_dev->dma_buf,
					chunk + padding);
		if (status != SFC_OK) {
			PRINT_SFC_E("snor_prog_page %x ret= %d\n",
				    addr, status);
			*retlen = len - size;
			mutex_unlock(&p_dev->lock);
			return status;
		}

		size -= chunk;
		addr += chunk;
		buf += chunk;
	}
	*retlen = len;
	mutex_unlock(&p_dev->lock);

	return 0;
}

static int sfc_read_mtd(struct mtd_info *mtd, loff_t from, size_t len,
			size_t *retlen, u_char *buf)
{
	u32 addr, size, chunk;
	u8 *p_buf =  (u8 *)buf;
	int ret = SFC_OK;

	struct SFNOR_DEV *p_dev = mtd_to_sfc(mtd);

	if ((from + len) > p_dev->capacity << 9)
		return -EINVAL;

	mutex_lock(&p_dev->lock);

	addr = from;
	size = len;

	while (size > 0) {
		chunk = (size < NOR_PAGE_SIZE) ? size : NOR_PAGE_SIZE;
		ret = snor_read_data(p_dev, addr, p_dev->dma_buf, chunk);
		if (ret != SFC_OK) {
			PRINT_SFC_E("snor_read_data %x ret=%d\n", addr, ret);
			*retlen = len - size;
			mutex_unlock(&p_dev->lock);
			return ret;
		}
		memcpy(p_buf, p_dev->dma_buf, chunk);
		size -= chunk;
		addr += chunk;
		p_buf += chunk;
	}

	*retlen = len;
	mutex_unlock(&p_dev->lock);
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

int sfc_nor_mtd_init(struct SFNOR_DEV *p_dev)
{
	int ret, i, part_num = 0;
	int capacity;
	struct STRUCT_PART_INFO *g_part;  /* size 2KB */

	capacity = p_dev->capacity;
	p_dev->mtd.name = "sfc_nor";
	p_dev->mtd.type = MTD_NORFLASH;
	p_dev->mtd.writesize = 1;
	p_dev->mtd.flags = MTD_CAP_NORFLASH;
	/* see snor_write */
	p_dev->mtd.size = capacity << 9;
	p_dev->mtd._erase = sfc_erase_mtd;
	p_dev->mtd._read = sfc_read_mtd;
	p_dev->mtd._write = sfc_write_mtd;
	p_dev->mtd.erasesize = g_spi_flash_info->block_size << 9;
	p_dev->mtd.writebufsize = NOR_PAGE_SIZE;

	p_dev->dma_buf = kmalloc(NOR_PAGE_SIZE, GFP_KERNEL | GFP_DMA);
	if (!p_dev->dma_buf) {
		PRINT_SFC_E("kmalloc size=0x%x failed\n", NOR_PAGE_SIZE);
		ret = -ENOMEM;
		goto out;
	}

	g_part = kmalloc(sizeof(*g_part), GFP_KERNEL | GFP_DMA);
	if (!g_part) {
		ret = -ENOMEM;
		goto free_dma_buf;
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
	ret = mtd_device_register(&p_dev->mtd, nor_parts, part_num);
	if (ret != 0)
		goto free_dma_buf;
	return ret;

free_dma_buf:
	kfree(p_dev->dma_buf);
out:
	return ret;
}


// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Copyright (C) 2012-2013 Samsung Electronics Co., Ltd.
 */

#include <linux/types.h>
#include <linux/buffer_head.h>
#include <linux/fs.h>
#include <linux/mutex.h>
#include <linux/blkdev.h>
#include <linux/slab.h>
#include "exfat.h"

static void __set_sb_dirty(struct super_block *sb)
{
	struct exfat_sb_info *sbi = EXFAT_SB(sb);

	sbi->s_dirt = 1;
}

static u8 name_buf[MAX_PATH_LENGTH * MAX_CHARSET_SIZE];

static u8 free_bit[] = {
	0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 4, 0, 1, 0, 2, /*   0 ~  19 */
	0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 5, 0, 1, 0, 2, 0, 1, 0, 3, /*  20 ~  39 */
	0, 1, 0, 2, 0, 1, 0, 4, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, /*  40 ~  59 */
	0, 1, 0, 6, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 4, /*  60 ~  79 */
	0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 5, 0, 1, 0, 2, /*  80 ~  99 */
	0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 4, 0, 1, 0, 2, 0, 1, 0, 3, /* 100 ~ 119 */
	0, 1, 0, 2, 0, 1, 0, 7, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, /* 120 ~ 139 */
	0, 1, 0, 4, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 5, /* 140 ~ 159 */
	0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 4, 0, 1, 0, 2, /* 160 ~ 179 */
	0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 6, 0, 1, 0, 2, 0, 1, 0, 3, /* 180 ~ 199 */
	0, 1, 0, 2, 0, 1, 0, 4, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, /* 200 ~ 219 */
	0, 1, 0, 5, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 4, /* 220 ~ 239 */
	0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0                 /* 240 ~ 254 */
};

static u8 used_bit[] = {
	0, 1, 1, 2, 1, 2, 2, 3, 1, 2, 2, 3, 2, 3, 3, 4, 1, 2, 2, 3, /*   0 ~  19 */
	2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5, 1, 2, 2, 3, 2, 3, 3, 4, /*  20 ~  39 */
	2, 3, 3, 4, 3, 4, 4, 5, 2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, /*  40 ~  59 */
	4, 5, 5, 6, 1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5, /*  60 ~  79 */
	2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6, 2, 3, 3, 4, /*  80 ~  99 */
	3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6, 3, 4, 4, 5, 4, 5, 5, 6, /* 100 ~ 119 */
	4, 5, 5, 6, 5, 6, 6, 7, 1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, /* 120 ~ 139 */
	3, 4, 4, 5, 2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6, /* 140 ~ 159 */
	2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6, 3, 4, 4, 5, /* 160 ~ 179 */
	4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7, 2, 3, 3, 4, 3, 4, 4, 5, /* 180 ~ 199 */
	3, 4, 4, 5, 4, 5, 5, 6, 3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, /* 200 ~ 219 */
	5, 6, 6, 7, 3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7, /* 220 ~ 239 */
	4, 5, 5, 6, 5, 6, 6, 7, 5, 6, 6, 7, 6, 7, 7, 8              /* 240 ~ 255 */
};

#define BITMAP_LOC(v)           ((v) >> 3)
#define BITMAP_SHIFT(v)         ((v) & 0x07)

static inline s32 exfat_bitmap_test(u8 *bitmap, int i)
{
	u8 data;

	data = bitmap[BITMAP_LOC(i)];
	if ((data >> BITMAP_SHIFT(i)) & 0x01)
		return 1;
	return 0;
}

static inline void exfat_bitmap_set(u8 *bitmap, int i)
{
	bitmap[BITMAP_LOC(i)] |= (0x01 << BITMAP_SHIFT(i));
}

static inline void exfat_bitmap_clear(u8 *bitmap, int i)
{
	bitmap[BITMAP_LOC(i)] &= ~(0x01 << BITMAP_SHIFT(i));
}

/*
 *  File System Management Functions
 */

void fs_set_vol_flags(struct super_block *sb, u32 new_flag)
{
	struct pbr_sector_t *p_pbr;
	struct bpbex_t *p_bpb;
	struct fs_info_t *p_fs = &(EXFAT_SB(sb)->fs_info);

	if (p_fs->vol_flag == new_flag)
		return;

	p_fs->vol_flag = new_flag;

	if (!p_fs->pbr_bh) {
		if (sector_read(sb, p_fs->PBR_sector,
				&p_fs->pbr_bh, 1) != 0)
			return;
	}

	p_pbr = (struct pbr_sector_t *)p_fs->pbr_bh->b_data;
	p_bpb = (struct bpbex_t *)p_pbr->bpb;
	SET16(p_bpb->vol_flags, (u16)new_flag);

	/* XXX duyoung
	 * what can we do here? (cuz fs_set_vol_flags() is void)
	 */
	if ((new_flag == VOL_DIRTY) && (!buffer_dirty(p_fs->pbr_bh)))
		sector_write(sb, p_fs->PBR_sector, p_fs->pbr_bh, 1);
	else
		sector_write(sb, p_fs->PBR_sector, p_fs->pbr_bh, 0);
}

void fs_error(struct super_block *sb)
{
	struct exfat_mount_options *opts = &EXFAT_SB(sb)->options;

	if (opts->errors == EXFAT_ERRORS_PANIC) {
		panic("[EXFAT] Filesystem panic from previous error\n");
	} else if ((opts->errors == EXFAT_ERRORS_RO) && !sb_rdonly(sb)) {
		sb->s_flags |= SB_RDONLY;
		pr_err("[EXFAT] Filesystem has been set read-only\n");
	}
}

/*
 *  Cluster Management Functions
 */

static s32 clear_cluster(struct super_block *sb, u32 clu)
{
	sector_t s, n;
	s32 ret = 0;
	struct buffer_head *tmp_bh = NULL;
	struct fs_info_t *p_fs = &(EXFAT_SB(sb)->fs_info);
	struct bd_info_t *p_bd = &(EXFAT_SB(sb)->bd_info);

	if (clu == CLUSTER_32(0)) { /* FAT16 root_dir */
		s = p_fs->root_start_sector;
		n = p_fs->data_start_sector;
	} else {
		s = START_SECTOR(clu);
		n = s + p_fs->sectors_per_clu;
	}

	for (; s < n; s++) {
		ret = sector_read(sb, s, &tmp_bh, 0);
		if (ret != 0)
			return ret;

		memset((char *)tmp_bh->b_data, 0x0, p_bd->sector_size);
		ret = sector_write(sb, s, tmp_bh, 0);
		if (ret != 0)
			break;
	}

	brelse(tmp_bh);
	return ret;
}

static s32 set_alloc_bitmap(struct super_block *sb, u32 clu)
{
	int i, b;
	sector_t sector;
	struct fs_info_t *p_fs = &(EXFAT_SB(sb)->fs_info);
	struct bd_info_t *p_bd = &(EXFAT_SB(sb)->bd_info);

	i = clu >> (p_bd->sector_size_bits + 3);
	b = clu & ((p_bd->sector_size << 3) - 1);

	sector = START_SECTOR(p_fs->map_clu) + i;

	exfat_bitmap_set((u8 *)p_fs->vol_amap[i]->b_data, b);

	return sector_write(sb, sector, p_fs->vol_amap[i], 0);
}

static s32 clr_alloc_bitmap(struct super_block *sb, u32 clu)
{
	int i, b;
	sector_t sector;
#ifdef CONFIG_EXFAT_DISCARD
	struct exfat_sb_info *sbi = EXFAT_SB(sb);
	struct exfat_mount_options *opts = &sbi->options;
	int ret;
#endif /* CONFIG_EXFAT_DISCARD */
	struct fs_info_t *p_fs = &(EXFAT_SB(sb)->fs_info);
	struct bd_info_t *p_bd = &(EXFAT_SB(sb)->bd_info);

	i = clu >> (p_bd->sector_size_bits + 3);
	b = clu & ((p_bd->sector_size << 3) - 1);

	sector = START_SECTOR(p_fs->map_clu) + i;

	exfat_bitmap_clear((u8 *)p_fs->vol_amap[i]->b_data, b);

#ifdef CONFIG_EXFAT_DISCARD
	if (opts->discard) {
		ret = sb_issue_discard(sb, START_SECTOR(clu),
				       (1 << p_fs->sectors_per_clu_bits),
				       GFP_NOFS, 0);
		if (ret == -EOPNOTSUPP) {
			pr_warn("discard not supported by device, disabling");
			opts->discard = 0;
		} else {
			return ret;
		}
	}
#endif /* CONFIG_EXFAT_DISCARD */

	return sector_write(sb, sector, p_fs->vol_amap[i], 0);
}

static u32 test_alloc_bitmap(struct super_block *sb, u32 clu)
{
	int i, map_i, map_b;
	u32 clu_base, clu_free;
	u8 k, clu_mask;
	struct fs_info_t *p_fs = &(EXFAT_SB(sb)->fs_info);
	struct bd_info_t *p_bd = &(EXFAT_SB(sb)->bd_info);

	clu_base = (clu & ~(0x7)) + 2;
	clu_mask = (1 << (clu - clu_base + 2)) - 1;

	map_i = clu >> (p_bd->sector_size_bits + 3);
	map_b = (clu >> 3) & p_bd->sector_size_mask;

	for (i = 2; i < p_fs->num_clusters; i += 8) {
		k = *(((u8 *)p_fs->vol_amap[map_i]->b_data) + map_b);
		if (clu_mask > 0) {
			k |= clu_mask;
			clu_mask = 0;
		}
		if (k < 0xFF) {
			clu_free = clu_base + free_bit[k];
			if (clu_free < p_fs->num_clusters)
				return clu_free;
		}
		clu_base += 8;

		if (((++map_b) >= p_bd->sector_size) ||
		    (clu_base >= p_fs->num_clusters)) {
			if ((++map_i) >= p_fs->map_sectors) {
				clu_base = 2;
				map_i = 0;
			}
			map_b = 0;
		}
	}

	return CLUSTER_32(~0);
}

static s32 exfat_alloc_cluster(struct super_block *sb, s32 num_alloc,
			struct chain_t *p_chain)
{
	s32 num_clusters = 0;
	u32 hint_clu, new_clu, last_clu = CLUSTER_32(~0);
	struct fs_info_t *p_fs = &(EXFAT_SB(sb)->fs_info);

	hint_clu = p_chain->dir;
	if (hint_clu == CLUSTER_32(~0)) {
		hint_clu = test_alloc_bitmap(sb, p_fs->clu_srch_ptr - 2);
		if (hint_clu == CLUSTER_32(~0))
			return 0;
	} else if (hint_clu >= p_fs->num_clusters) {
		hint_clu = 2;
		p_chain->flags = 0x01;
	}

	__set_sb_dirty(sb);

	p_chain->dir = CLUSTER_32(~0);

	while ((new_clu = test_alloc_bitmap(sb, hint_clu - 2)) != CLUSTER_32(~0)) {
		if (new_clu != hint_clu) {
			if (p_chain->flags == 0x03) {
				exfat_chain_cont_cluster(sb, p_chain->dir,
							 num_clusters);
				p_chain->flags = 0x01;
			}
		}

		if (set_alloc_bitmap(sb, new_clu - 2) != 0)
			return -EIO;

		num_clusters++;

		if (p_chain->flags == 0x01) {
			if (exfat_fat_write(sb, new_clu, CLUSTER_32(~0)) < 0)
				return -EIO;
		}

		if (p_chain->dir == CLUSTER_32(~0)) {
			p_chain->dir = new_clu;
		} else {
			if (p_chain->flags == 0x01) {
				if (exfat_fat_write(sb, last_clu, new_clu) < 0)
					return -EIO;
			}
		}
		last_clu = new_clu;

		if ((--num_alloc) == 0) {
			p_fs->clu_srch_ptr = hint_clu;
			if (p_fs->used_clusters != UINT_MAX)
				p_fs->used_clusters += num_clusters;

			p_chain->size += num_clusters;
			return num_clusters;
		}

		hint_clu = new_clu + 1;
		if (hint_clu >= p_fs->num_clusters) {
			hint_clu = 2;

			if (p_chain->flags == 0x03) {
				exfat_chain_cont_cluster(sb, p_chain->dir,
							 num_clusters);
				p_chain->flags = 0x01;
			}
		}
	}

	p_fs->clu_srch_ptr = hint_clu;
	if (p_fs->used_clusters != UINT_MAX)
		p_fs->used_clusters += num_clusters;

	p_chain->size += num_clusters;
	return num_clusters;
}

static void exfat_free_cluster(struct super_block *sb, struct chain_t *p_chain,
			s32 do_relse)
{
	s32 num_clusters = 0;
	u32 clu;
	struct fs_info_t *p_fs = &(EXFAT_SB(sb)->fs_info);
	int i;
	sector_t sector;

	if ((p_chain->dir == CLUSTER_32(0)) || (p_chain->dir == CLUSTER_32(~0)))
		return;

	if (p_chain->size <= 0) {
		pr_err("[EXFAT] free_cluster : skip free-req clu:%u, because of zero-size truncation\n",
		       p_chain->dir);
		return;
	}

	__set_sb_dirty(sb);
	clu = p_chain->dir;

	if (p_chain->flags == 0x03) {
		do {
			if (do_relse) {
				sector = START_SECTOR(clu);
				for (i = 0; i < p_fs->sectors_per_clu; i++)
					exfat_buf_release(sb, sector + i);
			}

			if (clr_alloc_bitmap(sb, clu - 2) != 0)
				break;
			clu++;

			num_clusters++;
		} while (num_clusters < p_chain->size);
	} else {
		do {
			if (p_fs->dev_ejected)
				break;

			if (do_relse) {
				sector = START_SECTOR(clu);
				for (i = 0; i < p_fs->sectors_per_clu; i++)
					exfat_buf_release(sb, sector + i);
			}

			if (clr_alloc_bitmap(sb, clu - 2) != 0)
				break;

			if (exfat_fat_read(sb, clu, &clu) == -1)
				break;
			num_clusters++;
		} while ((clu != CLUSTER_32(0)) && (clu != CLUSTER_32(~0)));
	}

	if (p_fs->used_clusters != UINT_MAX)
		p_fs->used_clusters -= num_clusters;
}

static u32 find_last_cluster(struct super_block *sb, struct chain_t *p_chain)
{
	u32 clu, next;
	struct fs_info_t *p_fs = &(EXFAT_SB(sb)->fs_info);

	clu = p_chain->dir;

	if (p_chain->flags == 0x03) {
		clu += p_chain->size - 1;
	} else {
		while ((exfat_fat_read(sb, clu, &next) == 0) &&
		       (next != CLUSTER_32(~0))) {
			if (p_fs->dev_ejected)
				break;
			clu = next;
		}
	}

	return clu;
}

s32 count_num_clusters(struct super_block *sb, struct chain_t *p_chain)
{
	int i, count = 0;
	u32 clu;
	struct fs_info_t *p_fs = &(EXFAT_SB(sb)->fs_info);

	if ((p_chain->dir == CLUSTER_32(0)) || (p_chain->dir == CLUSTER_32(~0)))
		return 0;

	clu = p_chain->dir;

	if (p_chain->flags == 0x03) {
		count = p_chain->size;
	} else {
		for (i = 2; i < p_fs->num_clusters; i++) {
			count++;
			if (exfat_fat_read(sb, clu, &clu) != 0)
				return 0;
			if (clu == CLUSTER_32(~0))
				break;
		}
	}

	return count;
}

static s32 exfat_count_used_clusters(struct super_block *sb)
{
	int i, map_i, map_b, count = 0;
	u8 k;
	struct fs_info_t *p_fs = &(EXFAT_SB(sb)->fs_info);
	struct bd_info_t *p_bd = &(EXFAT_SB(sb)->bd_info);

	map_i = 0;
	map_b = 0;

	for (i = 2; i < p_fs->num_clusters; i += 8) {
		k = *(((u8 *)p_fs->vol_amap[map_i]->b_data) + map_b);
		count += used_bit[k];

		if ((++map_b) >= p_bd->sector_size) {
			map_i++;
			map_b = 0;
		}
	}

	return count;
}

void exfat_chain_cont_cluster(struct super_block *sb, u32 chain, s32 len)
{
	if (len == 0)
		return;

	while (len > 1) {
		if (exfat_fat_write(sb, chain, chain + 1) < 0)
			break;
		chain++;
		len--;
	}
	exfat_fat_write(sb, chain, CLUSTER_32(~0));
}

/*
 *  Allocation Bitmap Management Functions
 */

s32 load_alloc_bitmap(struct super_block *sb)
{
	int i, j, ret;
	u32 map_size;
	u32 type;
	sector_t sector;
	struct chain_t clu;
	struct bmap_dentry_t *ep;
	struct fs_info_t *p_fs = &(EXFAT_SB(sb)->fs_info);
	struct bd_info_t *p_bd = &(EXFAT_SB(sb)->bd_info);

	clu.dir = p_fs->root_dir;
	clu.flags = 0x01;

	while (clu.dir != CLUSTER_32(~0)) {
		if (p_fs->dev_ejected)
			break;

		for (i = 0; i < p_fs->dentries_per_clu; i++) {
			ep = (struct bmap_dentry_t *)get_entry_in_dir(sb, &clu,
								      i, NULL);
			if (!ep)
				return -ENOENT;

			type = p_fs->fs_func->get_entry_type((struct dentry_t *)ep);

			if (type == TYPE_UNUSED)
				break;
			if (type != TYPE_BITMAP)
				continue;

			if (ep->flags == 0x0) {
				p_fs->map_clu  = GET32_A(ep->start_clu);
				map_size = (u32)GET64_A(ep->size);

				p_fs->map_sectors = ((map_size - 1) >> p_bd->sector_size_bits) + 1;

				p_fs->vol_amap = kmalloc_array(p_fs->map_sectors,
							       sizeof(struct buffer_head *),
							       GFP_KERNEL);
				if (!p_fs->vol_amap)
					return -ENOMEM;

				sector = START_SECTOR(p_fs->map_clu);

				for (j = 0; j < p_fs->map_sectors; j++) {
					p_fs->vol_amap[j] = NULL;
					ret = sector_read(sb, sector + j, &p_fs->vol_amap[j], 1);
					if (ret != 0) {
						/*  release all buffers and free vol_amap */
						i = 0;
						while (i < j)
							brelse(p_fs->vol_amap[i++]);

						kfree(p_fs->vol_amap);
						p_fs->vol_amap = NULL;
						return ret;
					}
				}

				p_fs->pbr_bh = NULL;
				return 0;
			}
		}

		if (exfat_fat_read(sb, clu.dir, &clu.dir) != 0)
			return -EIO;
	}

	return -EFSCORRUPTED;
}

void free_alloc_bitmap(struct super_block *sb)
{
	int i;
	struct fs_info_t *p_fs = &(EXFAT_SB(sb)->fs_info);

	brelse(p_fs->pbr_bh);

	for (i = 0; i < p_fs->map_sectors; i++)
		__brelse(p_fs->vol_amap[i]);

	kfree(p_fs->vol_amap);
	p_fs->vol_amap = NULL;
}

void sync_alloc_bitmap(struct super_block *sb)
{
	int i;
	struct fs_info_t *p_fs = &(EXFAT_SB(sb)->fs_info);

	if (!p_fs->vol_amap)
		return;

	for (i = 0; i < p_fs->map_sectors; i++)
		sync_dirty_buffer(p_fs->vol_amap[i]);
}

/*
 *  Upcase table Management Functions
 */
static s32 __load_upcase_table(struct super_block *sb, sector_t sector,
			       u32 num_sectors, u32 utbl_checksum)
{
	int i, ret = -EINVAL;
	u32 j;
	struct fs_info_t *p_fs = &(EXFAT_SB(sb)->fs_info);
	struct bd_info_t *p_bd = &(EXFAT_SB(sb)->bd_info);
	struct buffer_head *tmp_bh = NULL;
	sector_t end_sector = num_sectors + sector;

	bool	skip = false;
	u32	index = 0;
	u16	uni = 0;
	u16 **upcase_table;

	u32 checksum = 0;

	upcase_table = kmalloc_array(UTBL_COL_COUNT, sizeof(u16 *), GFP_KERNEL);
	p_fs->vol_utbl = upcase_table;
	if (!upcase_table)
		return -ENOMEM;
	memset(upcase_table, 0, UTBL_COL_COUNT * sizeof(u16 *));

	while (sector < end_sector) {
		ret = sector_read(sb, sector, &tmp_bh, 1);
		if (ret != 0) {
			pr_debug("sector read (0x%llX)fail\n",
				 (unsigned long long)sector);
			goto error;
		}
		sector++;

		for (i = 0; i < p_bd->sector_size && index <= 0xFFFF; i += 2) {
			uni = GET16(((u8 *)tmp_bh->b_data) + i);

			checksum = ((checksum & 1) ? 0x80000000 : 0) +
				   (checksum >> 1) + *(((u8 *)tmp_bh->b_data) +
						       i);
			checksum = ((checksum & 1) ? 0x80000000 : 0) +
				   (checksum >> 1) + *(((u8 *)tmp_bh->b_data) +
						       (i + 1));

			if (skip) {
				pr_debug("skip from 0x%X ", index);
				index += uni;
				pr_debug("to 0x%X (amount of 0x%X)\n",
					 index, uni);
				skip = false;
			} else if (uni == index) {
				index++;
			} else if (uni == 0xFFFF) {
				skip = true;
			} else { /* uni != index , uni != 0xFFFF */
				u16 col_index = get_col_index(index);

				if (!upcase_table[col_index]) {
					pr_debug("alloc = 0x%X\n", col_index);
					upcase_table[col_index] = kmalloc_array(UTBL_ROW_COUNT,
						sizeof(u16), GFP_KERNEL);
					if (!upcase_table[col_index]) {
						ret = -ENOMEM;
						goto error;
					}

					for (j = 0; j < UTBL_ROW_COUNT; j++)
						upcase_table[col_index][j] = (col_index << LOW_INDEX_BIT) | j;
				}

				upcase_table[col_index][get_row_index(index)] = uni;
				index++;
			}
		}
	}
	if (index >= 0xFFFF && utbl_checksum == checksum) {
		if (tmp_bh)
			brelse(tmp_bh);
		return 0;
	}
	ret = -EINVAL;
error:
	if (tmp_bh)
		brelse(tmp_bh);
	free_upcase_table(sb);
	return ret;
}

static s32 __load_default_upcase_table(struct super_block *sb)
{
	int i, ret = -EINVAL;
	u32 j;
	struct fs_info_t *p_fs = &(EXFAT_SB(sb)->fs_info);

	bool	skip = false;
	u32	index = 0;
	u16	uni = 0;
	u16 **upcase_table;

	upcase_table = kmalloc_array(UTBL_COL_COUNT, sizeof(u16 *), GFP_KERNEL);
	p_fs->vol_utbl = upcase_table;
	if (!upcase_table)
		return -ENOMEM;
	memset(upcase_table, 0, UTBL_COL_COUNT * sizeof(u16 *));

	for (i = 0; index <= 0xFFFF && i < NUM_UPCASE * 2; i += 2) {
		uni = GET16(uni_upcase + i);
		if (skip) {
			pr_debug("skip from 0x%X ", index);
			index += uni;
			pr_debug("to 0x%X (amount of 0x%X)\n", index, uni);
			skip = false;
		} else if (uni == index) {
			index++;
		} else if (uni == 0xFFFF) {
			skip = true;
		} else { /* uni != index , uni != 0xFFFF */
			u16 col_index = get_col_index(index);

			if (!upcase_table[col_index]) {
				pr_debug("alloc = 0x%X\n", col_index);
				upcase_table[col_index] = kmalloc_array(UTBL_ROW_COUNT,
									sizeof(u16),
									GFP_KERNEL);
				if (!upcase_table[col_index]) {
					ret = -ENOMEM;
					goto error;
				}

				for (j = 0; j < UTBL_ROW_COUNT; j++)
					upcase_table[col_index][j] = (col_index << LOW_INDEX_BIT) | j;
			}

			upcase_table[col_index][get_row_index(index)] = uni;
			index++;
		}
	}

	if (index >= 0xFFFF)
		return 0;

error:
	/* FATAL error: default upcase table has error */
	free_upcase_table(sb);
	return ret;
}

s32 load_upcase_table(struct super_block *sb)
{
	int i;
	u32 tbl_clu, tbl_size;
	sector_t sector;
	u32 type, num_sectors;
	struct chain_t clu;
	struct case_dentry_t *ep;
	struct fs_info_t *p_fs = &(EXFAT_SB(sb)->fs_info);
	struct bd_info_t *p_bd = &(EXFAT_SB(sb)->bd_info);

	clu.dir = p_fs->root_dir;
	clu.flags = 0x01;

	if (p_fs->dev_ejected)
		return -EIO;

	while (clu.dir != CLUSTER_32(~0)) {
		for (i = 0; i < p_fs->dentries_per_clu; i++) {
			ep = (struct case_dentry_t *)get_entry_in_dir(sb, &clu,
								      i, NULL);
			if (!ep)
				return -ENOENT;

			type = p_fs->fs_func->get_entry_type((struct dentry_t *)ep);

			if (type == TYPE_UNUSED)
				break;
			if (type != TYPE_UPCASE)
				continue;

			tbl_clu  = GET32_A(ep->start_clu);
			tbl_size = (u32)GET64_A(ep->size);

			sector = START_SECTOR(tbl_clu);
			num_sectors = ((tbl_size - 1) >> p_bd->sector_size_bits) + 1;
			if (__load_upcase_table(sb, sector, num_sectors,
						GET32_A(ep->checksum)) != 0)
				break;
			return 0;
		}
		if (exfat_fat_read(sb, clu.dir, &clu.dir) != 0)
			return -EIO;
	}
	/* load default upcase table */
	return __load_default_upcase_table(sb);
}

void free_upcase_table(struct super_block *sb)
{
	u32 i;
	struct fs_info_t *p_fs = &(EXFAT_SB(sb)->fs_info);
	u16 **upcase_table;

	upcase_table = p_fs->vol_utbl;
	for (i = 0; i < UTBL_COL_COUNT; i++)
		kfree(upcase_table[i]);

	kfree(p_fs->vol_utbl);
	p_fs->vol_utbl = NULL;
}

/*
 *  Directory Entry Management Functions
 */

static u32 exfat_get_entry_type(struct dentry_t *p_entry)
{
	struct file_dentry_t *ep = (struct file_dentry_t *)p_entry;

	if (ep->type == 0x0) {
		return TYPE_UNUSED;
	} else if (ep->type < 0x80) {
		return TYPE_DELETED;
	} else if (ep->type == 0x80) {
		return TYPE_INVALID;
	} else if (ep->type < 0xA0) {
		if (ep->type == 0x81) {
			return TYPE_BITMAP;
		} else if (ep->type == 0x82) {
			return TYPE_UPCASE;
		} else if (ep->type == 0x83) {
			return TYPE_VOLUME;
		} else if (ep->type == 0x85) {
			if (GET16_A(ep->attr) & ATTR_SUBDIR)
				return TYPE_DIR;
			else
				return TYPE_FILE;
		}
		return TYPE_CRITICAL_PRI;
	} else if (ep->type < 0xC0) {
		if (ep->type == 0xA0)
			return TYPE_GUID;
		else if (ep->type == 0xA1)
			return TYPE_PADDING;
		else if (ep->type == 0xA2)
			return TYPE_ACLTAB;
		return TYPE_BENIGN_PRI;
	} else if (ep->type < 0xE0) {
		if (ep->type == 0xC0)
			return TYPE_STREAM;
		else if (ep->type == 0xC1)
			return TYPE_EXTEND;
		else if (ep->type == 0xC2)
			return TYPE_ACL;
		return TYPE_CRITICAL_SEC;
	}

	return TYPE_BENIGN_SEC;
}

static void exfat_set_entry_type(struct dentry_t *p_entry, u32 type)
{
	struct file_dentry_t *ep = (struct file_dentry_t *)p_entry;

	if (type == TYPE_UNUSED) {
		ep->type = 0x0;
	} else if (type == TYPE_DELETED) {
		ep->type &= ~0x80;
	} else if (type == TYPE_STREAM) {
		ep->type = 0xC0;
	} else if (type == TYPE_EXTEND) {
		ep->type = 0xC1;
	} else if (type == TYPE_BITMAP) {
		ep->type = 0x81;
	} else if (type == TYPE_UPCASE) {
		ep->type = 0x82;
	} else if (type == TYPE_VOLUME) {
		ep->type = 0x83;
	} else if (type == TYPE_DIR) {
		ep->type = 0x85;
		SET16_A(ep->attr, ATTR_SUBDIR);
	} else if (type == TYPE_FILE) {
		ep->type = 0x85;
		SET16_A(ep->attr, ATTR_ARCHIVE);
	} else if (type == TYPE_SYMLINK) {
		ep->type = 0x85;
		SET16_A(ep->attr, ATTR_ARCHIVE | ATTR_SYMLINK);
	}
}

static u32 exfat_get_entry_attr(struct dentry_t *p_entry)
{
	struct file_dentry_t *ep = (struct file_dentry_t *)p_entry;

	return (u32)GET16_A(ep->attr);
}

static void exfat_set_entry_attr(struct dentry_t *p_entry, u32 attr)
{
	struct file_dentry_t *ep = (struct file_dentry_t *)p_entry;

	SET16_A(ep->attr, (u16)attr);
}

static u8 exfat_get_entry_flag(struct dentry_t *p_entry)
{
	struct strm_dentry_t *ep = (struct strm_dentry_t *)p_entry;

	return ep->flags;
}

static void exfat_set_entry_flag(struct dentry_t *p_entry, u8 flags)
{
	struct strm_dentry_t *ep = (struct strm_dentry_t *)p_entry;

	ep->flags = flags;
}

static u32 exfat_get_entry_clu0(struct dentry_t *p_entry)
{
	struct strm_dentry_t *ep = (struct strm_dentry_t *)p_entry;

	return GET32_A(ep->start_clu);
}

static void exfat_set_entry_clu0(struct dentry_t *p_entry, u32 start_clu)
{
	struct strm_dentry_t *ep = (struct strm_dentry_t *)p_entry;

	SET32_A(ep->start_clu, start_clu);
}

static u64 exfat_get_entry_size(struct dentry_t *p_entry)
{
	struct strm_dentry_t *ep = (struct strm_dentry_t *)p_entry;

	return GET64_A(ep->valid_size);
}

static void exfat_set_entry_size(struct dentry_t *p_entry, u64 size)
{
	struct strm_dentry_t *ep = (struct strm_dentry_t *)p_entry;

	SET64_A(ep->valid_size, size);
	SET64_A(ep->size, size);
}

static void exfat_get_entry_time(struct dentry_t *p_entry, struct timestamp_t *tp,
			  u8 mode)
{
	u16 t = 0x00, d = 0x21;
	struct file_dentry_t *ep = (struct file_dentry_t *)p_entry;

	switch (mode) {
	case TM_CREATE:
		t = GET16_A(ep->create_time);
		d = GET16_A(ep->create_date);
		break;
	case TM_MODIFY:
		t = GET16_A(ep->modify_time);
		d = GET16_A(ep->modify_date);
		break;
	case TM_ACCESS:
		t = GET16_A(ep->access_time);
		d = GET16_A(ep->access_date);
		break;
	}

	tp->sec  = (t & 0x001F) << 1;
	tp->min  = (t >> 5) & 0x003F;
	tp->hour = (t >> 11);
	tp->day  = (d & 0x001F);
	tp->mon  = (d >> 5) & 0x000F;
	tp->year = (d >> 9);
}

static void exfat_set_entry_time(struct dentry_t *p_entry, struct timestamp_t *tp,
			  u8 mode)
{
	u16 t, d;
	struct file_dentry_t *ep = (struct file_dentry_t *)p_entry;

	t = (tp->hour << 11) | (tp->min << 5) | (tp->sec >> 1);
	d = (tp->year <<  9) | (tp->mon << 5) |  tp->day;

	switch (mode) {
	case TM_CREATE:
		SET16_A(ep->create_time, t);
		SET16_A(ep->create_date, d);
		break;
	case TM_MODIFY:
		SET16_A(ep->modify_time, t);
		SET16_A(ep->modify_date, d);
		break;
	case TM_ACCESS:
		SET16_A(ep->access_time, t);
		SET16_A(ep->access_date, d);
		break;
	}
}

static void init_file_entry(struct file_dentry_t *ep, u32 type)
{
	struct timestamp_t tm, *tp;

	exfat_set_entry_type((struct dentry_t *)ep, type);

	tp = tm_current(&tm);
	exfat_set_entry_time((struct dentry_t *)ep, tp, TM_CREATE);
	exfat_set_entry_time((struct dentry_t *)ep, tp, TM_MODIFY);
	exfat_set_entry_time((struct dentry_t *)ep, tp, TM_ACCESS);
	ep->create_time_ms = 0;
	ep->modify_time_ms = 0;
	ep->access_time_ms = 0;
}

static void init_strm_entry(struct strm_dentry_t *ep, u8 flags, u32 start_clu, u64 size)
{
	exfat_set_entry_type((struct dentry_t *)ep, TYPE_STREAM);
	ep->flags = flags;
	SET32_A(ep->start_clu, start_clu);
	SET64_A(ep->valid_size, size);
	SET64_A(ep->size, size);
}

static void init_name_entry(struct name_dentry_t *ep, u16 *uniname)
{
	int i;

	exfat_set_entry_type((struct dentry_t *)ep, TYPE_EXTEND);
	ep->flags = 0x0;

	for (i = 0; i < 30; i++, i++) {
		SET16_A(ep->unicode_0_14 + i, *uniname);
		if (*uniname == 0x0)
			break;
		uniname++;
	}
}

static s32 exfat_init_dir_entry(struct super_block *sb, struct chain_t *p_dir,
			 s32 entry, u32 type, u32 start_clu, u64 size)
{
	sector_t sector;
	u8 flags;
	struct file_dentry_t *file_ep;
	struct strm_dentry_t *strm_ep;

	flags = (type == TYPE_FILE) ? 0x01 : 0x03;

	/* we cannot use get_entry_set_in_dir here because file ep is not initialized yet */
	file_ep = (struct file_dentry_t *)get_entry_in_dir(sb, p_dir, entry,
							   &sector);
	if (!file_ep)
		return -ENOENT;

	strm_ep = (struct strm_dentry_t *)get_entry_in_dir(sb, p_dir, entry + 1,
							   &sector);
	if (!strm_ep)
		return -ENOENT;

	init_file_entry(file_ep, type);
	exfat_buf_modify(sb, sector);

	init_strm_entry(strm_ep, flags, start_clu, size);
	exfat_buf_modify(sb, sector);

	return 0;
}

static s32 exfat_init_ext_entry(struct super_block *sb, struct chain_t *p_dir,
				s32 entry, s32 num_entries,
				struct uni_name_t *p_uniname,
				struct dos_name_t *p_dosname)
{
	int i;
	sector_t sector;
	u16 *uniname = p_uniname->name;
	struct file_dentry_t *file_ep;
	struct strm_dentry_t *strm_ep;
	struct name_dentry_t *name_ep;

	file_ep = (struct file_dentry_t *)get_entry_in_dir(sb, p_dir, entry,
							   &sector);
	if (!file_ep)
		return -ENOENT;

	file_ep->num_ext = (u8)(num_entries - 1);
	exfat_buf_modify(sb, sector);

	strm_ep = (struct strm_dentry_t *)get_entry_in_dir(sb, p_dir, entry + 1,
							   &sector);
	if (!strm_ep)
		return -ENOENT;

	strm_ep->name_len = p_uniname->name_len;
	SET16_A(strm_ep->name_hash, p_uniname->name_hash);
	exfat_buf_modify(sb, sector);

	for (i = 2; i < num_entries; i++) {
		name_ep = (struct name_dentry_t *)get_entry_in_dir(sb, p_dir,
								   entry + i,
								   &sector);
		if (!name_ep)
			return -ENOENT;

		init_name_entry(name_ep, uniname);
		exfat_buf_modify(sb, sector);
		uniname += 15;
	}

	update_dir_checksum(sb, p_dir, entry);

	return 0;
}

static void exfat_delete_dir_entry(struct super_block *sb, struct chain_t *p_dir,
			    s32 entry, s32 order, s32 num_entries)
{
	int i;
	sector_t sector;
	struct dentry_t *ep;
	struct fs_info_t *p_fs = &(EXFAT_SB(sb)->fs_info);

	for (i = order; i < num_entries; i++) {
		ep = get_entry_in_dir(sb, p_dir, entry + i, &sector);
		if (!ep)
			return;

		p_fs->fs_func->set_entry_type(ep, TYPE_DELETED);
		exfat_buf_modify(sb, sector);
	}
}

void update_dir_checksum(struct super_block *sb, struct chain_t *p_dir,
			 s32 entry)
{
	int i, num_entries;
	sector_t sector;
	u16 chksum;
	struct file_dentry_t *file_ep;
	struct dentry_t *ep;

	file_ep = (struct file_dentry_t *)get_entry_in_dir(sb, p_dir, entry,
							   &sector);
	if (!file_ep)
		return;

	exfat_buf_lock(sb, sector);

	num_entries = (s32)file_ep->num_ext + 1;
	chksum = calc_checksum_2byte((void *)file_ep, DENTRY_SIZE, 0,
				     CS_DIR_ENTRY);

	for (i = 1; i < num_entries; i++) {
		ep = get_entry_in_dir(sb, p_dir, entry + i, NULL);
		if (!ep) {
			exfat_buf_unlock(sb, sector);
			return;
		}

		chksum = calc_checksum_2byte((void *)ep, DENTRY_SIZE, chksum,
					     CS_DEFAULT);
	}

	SET16_A(file_ep->checksum, chksum);
	exfat_buf_modify(sb, sector);
	exfat_buf_unlock(sb, sector);
}

static s32 __write_partial_entries_in_entry_set(struct super_block *sb,
						struct entry_set_cache_t *es,
						sector_t sec, s32 off, u32 count)
{
	s32 num_entries, buf_off = (off - es->offset);
	u32 remaining_byte_in_sector, copy_entries;
	struct fs_info_t *p_fs = &(EXFAT_SB(sb)->fs_info);
	struct bd_info_t *p_bd = &(EXFAT_SB(sb)->bd_info);
	u32 clu;
	u8 *buf, *esbuf = (u8 *)&es->__buf;

	pr_debug("%s entered es %p sec %llu off %d count %d\n",
		 __func__, es, (unsigned long long)sec, off, count);
	num_entries = count;

	while (num_entries) {
		/* white per sector base */
		remaining_byte_in_sector = (1 << p_bd->sector_size_bits) - off;
		copy_entries = min_t(s32,
				     remaining_byte_in_sector >> DENTRY_SIZE_BITS,
				     num_entries);
		buf = exfat_buf_getblk(sb, sec);
		if (!buf)
			goto err_out;
		pr_debug("es->buf %p buf_off %u\n", esbuf, buf_off);
		pr_debug("copying %d entries from %p to sector %llu\n",
			 copy_entries, (esbuf + buf_off),
			 (unsigned long long)sec);
		memcpy(buf + off, esbuf + buf_off,
		       copy_entries << DENTRY_SIZE_BITS);
		exfat_buf_modify(sb, sec);
		num_entries -= copy_entries;

		if (num_entries) {
			/* get next sector */
			if (IS_LAST_SECTOR_IN_CLUSTER(sec)) {
				clu = GET_CLUSTER_FROM_SECTOR(sec);
				if (es->alloc_flag == 0x03) {
					clu++;
				} else {
					if (exfat_fat_read(sb, clu, &clu) == -1)
						goto err_out;
				}
				sec = START_SECTOR(clu);
			} else {
				sec++;
			}
			off = 0;
			buf_off += copy_entries << DENTRY_SIZE_BITS;
		}
	}

	pr_debug("%s exited successfully\n", __func__);
	return 0;
err_out:
	pr_debug("%s failed\n", __func__);
	return -EINVAL;
}

/* write back all entries in entry set */
static s32 write_whole_entry_set(struct super_block *sb, struct entry_set_cache_t *es)
{
	return __write_partial_entries_in_entry_set(sb, es, es->sector,
						    es->offset,
						    es->num_entries);
}

void update_dir_checksum_with_entry_set(struct super_block *sb,
					struct entry_set_cache_t *es)
{
	struct dentry_t *ep;
	u16 chksum = 0;
	s32 chksum_type = CS_DIR_ENTRY, i;

	ep = (struct dentry_t *)&es->__buf;
	for (i = 0; i < es->num_entries; i++) {
		pr_debug("%s ep %p\n", __func__, ep);
		chksum = calc_checksum_2byte((void *)ep, DENTRY_SIZE, chksum,
					     chksum_type);
		ep++;
		chksum_type = CS_DEFAULT;
	}

	ep = (struct dentry_t *)&es->__buf;
	SET16_A(((struct file_dentry_t *)ep)->checksum, chksum);
	write_whole_entry_set(sb, es);
}

static s32 _walk_fat_chain(struct super_block *sb, struct chain_t *p_dir,
			   s32 byte_offset, u32 *clu)
{
	struct fs_info_t *p_fs = &(EXFAT_SB(sb)->fs_info);
	s32 clu_offset;
	u32 cur_clu;

	clu_offset = byte_offset >> p_fs->cluster_size_bits;
	cur_clu = p_dir->dir;

	if (p_dir->flags == 0x03) {
		cur_clu += clu_offset;
	} else {
		while (clu_offset > 0) {
			if (exfat_fat_read(sb, cur_clu, &cur_clu) == -1)
				return -EIO;
			clu_offset--;
		}
	}

	if (clu)
		*clu = cur_clu;
	return 0;
}

static s32 find_location(struct super_block *sb, struct chain_t *p_dir, s32 entry,
		  sector_t *sector, s32 *offset)
{
	s32 off, ret;
	u32 clu = 0;
	struct fs_info_t *p_fs = &(EXFAT_SB(sb)->fs_info);
	struct bd_info_t *p_bd = &(EXFAT_SB(sb)->bd_info);

	off = entry << DENTRY_SIZE_BITS;

	if (p_dir->dir == CLUSTER_32(0)) { /* FAT16 root_dir */
		*offset = off & p_bd->sector_size_mask;
		*sector = off >> p_bd->sector_size_bits;
		*sector += p_fs->root_start_sector;
	} else {
		ret = _walk_fat_chain(sb, p_dir, off, &clu);
		if (ret != 0)
			return ret;

		/* byte offset in cluster */
		off &= p_fs->cluster_size - 1;

		/* byte offset in sector    */
		*offset = off & p_bd->sector_size_mask;

		/* sector offset in cluster */
		*sector = off >> p_bd->sector_size_bits;
		*sector += START_SECTOR(clu);
	}
	return 0;
}

struct dentry_t *get_entry_in_dir(struct super_block *sb, struct chain_t *p_dir,
				  s32 entry, sector_t *sector)
{
	s32 off;
	sector_t sec;
	u8 *buf;

	if (find_location(sb, p_dir, entry, &sec, &off) != 0)
		return NULL;

	buf = exfat_buf_getblk(sb, sec);

	if (!buf)
		return NULL;

	if (sector)
		*sector = sec;
	return (struct dentry_t *)(buf + off);
}

/* returns a set of dentries for a file or dir.
 * Note that this is a copy (dump) of dentries so that user should call write_entry_set()
 * to apply changes made in this entry set to the real device.
 * in:
 *   sb+p_dir+entry: indicates a file/dir
 *   type:  specifies how many dentries should be included.
 * out:
 *   file_ep: will point the first dentry(= file dentry) on success
 * return:
 *   pointer of entry set on success,
 *   NULL on failure.
 */

#define ES_MODE_STARTED				0
#define ES_MODE_GET_FILE_ENTRY			1
#define ES_MODE_GET_STRM_ENTRY			2
#define ES_MODE_GET_NAME_ENTRY			3
#define ES_MODE_GET_CRITICAL_SEC_ENTRY		4
struct entry_set_cache_t *get_entry_set_in_dir(struct super_block *sb,
					       struct chain_t *p_dir, s32 entry,
					       u32 type,
					       struct dentry_t **file_ep)
{
	s32 off, ret, byte_offset;
	u32 clu = 0;
	sector_t sec;
	u32 entry_type;
	struct fs_info_t *p_fs = &(EXFAT_SB(sb)->fs_info);
	struct bd_info_t *p_bd = &(EXFAT_SB(sb)->bd_info);
	struct entry_set_cache_t *es = NULL;
	struct dentry_t *ep, *pos;
	u8 *buf;
	u8 num_entries;
	s32 mode = ES_MODE_STARTED;
	size_t bufsize;

	pr_debug("%s entered p_dir dir %u flags %x size %d\n",
		 __func__, p_dir->dir, p_dir->flags, p_dir->size);

	byte_offset = entry << DENTRY_SIZE_BITS;
	ret = _walk_fat_chain(sb, p_dir, byte_offset, &clu);
	if (ret != 0)
		return NULL;

	/* byte offset in cluster */
	byte_offset &= p_fs->cluster_size - 1;

	/* byte offset in sector    */
	off = byte_offset & p_bd->sector_size_mask;

	/* sector offset in cluster */
	sec = byte_offset >> p_bd->sector_size_bits;
	sec += START_SECTOR(clu);

	buf = exfat_buf_getblk(sb, sec);
	if (!buf)
		goto err_out;

	ep = (struct dentry_t *)(buf + off);
	entry_type = p_fs->fs_func->get_entry_type(ep);

	if ((entry_type != TYPE_FILE) && (entry_type != TYPE_DIR))
		goto err_out;

	if (type == ES_ALL_ENTRIES)
		num_entries = ((struct file_dentry_t *)ep)->num_ext + 1;
	else
		num_entries = type;

	bufsize = offsetof(struct entry_set_cache_t, __buf) + (num_entries) *
		  sizeof(struct dentry_t);
	pr_debug("%s: trying to kmalloc %zx bytes for %d entries\n", __func__,
		 bufsize, num_entries);
	es = kmalloc(bufsize, GFP_KERNEL);
	if (!es)
		goto err_out;

	es->num_entries = num_entries;
	es->sector = sec;
	es->offset = off;
	es->alloc_flag = p_dir->flags;

	pos = (struct dentry_t *)&es->__buf;

	while (num_entries) {
		/*
		 * instead of copying whole sector, we will check every entry.
		 * this will provide minimum stablity and consistency.
		 */
		entry_type = p_fs->fs_func->get_entry_type(ep);

		if ((entry_type == TYPE_UNUSED) || (entry_type == TYPE_DELETED))
			goto err_out;

		switch (mode) {
		case ES_MODE_STARTED:
			if ((entry_type == TYPE_FILE) || (entry_type == TYPE_DIR))
				mode = ES_MODE_GET_FILE_ENTRY;
			else
				goto err_out;
			break;
		case ES_MODE_GET_FILE_ENTRY:
			if (entry_type == TYPE_STREAM)
				mode = ES_MODE_GET_STRM_ENTRY;
			else
				goto err_out;
			break;
		case ES_MODE_GET_STRM_ENTRY:
			if (entry_type == TYPE_EXTEND)
				mode = ES_MODE_GET_NAME_ENTRY;
			else
				goto err_out;
			break;
		case ES_MODE_GET_NAME_ENTRY:
			if (entry_type == TYPE_EXTEND)
				break;
			else if (entry_type == TYPE_STREAM)
				goto err_out;
			else if (entry_type & TYPE_CRITICAL_SEC)
				mode = ES_MODE_GET_CRITICAL_SEC_ENTRY;
			else
				goto err_out;
			break;
		case ES_MODE_GET_CRITICAL_SEC_ENTRY:
			if ((entry_type == TYPE_EXTEND) ||
			    (entry_type == TYPE_STREAM))
				goto err_out;
			else if ((entry_type & TYPE_CRITICAL_SEC) !=
				 TYPE_CRITICAL_SEC)
				goto err_out;
			break;
		}

		memcpy(pos, ep, sizeof(struct dentry_t));

		if (--num_entries == 0)
			break;

		if (((off + DENTRY_SIZE) & p_bd->sector_size_mask) <
		    (off &  p_bd->sector_size_mask)) {
			/* get the next sector */
			if (IS_LAST_SECTOR_IN_CLUSTER(sec)) {
				if (es->alloc_flag == 0x03) {
					clu++;
				} else {
					if (exfat_fat_read(sb, clu, &clu) == -1)
						goto err_out;
				}
				sec = START_SECTOR(clu);
			} else {
				sec++;
			}
			buf = exfat_buf_getblk(sb, sec);
			if (!buf)
				goto err_out;
			off = 0;
			ep = (struct dentry_t *)(buf);
		} else {
			ep++;
			off += DENTRY_SIZE;
		}
		pos++;
	}

	if (file_ep)
		*file_ep = (struct dentry_t *)&es->__buf;

	pr_debug("%s exiting es %p sec %llu offset %d flags %d, num_entries %u buf ptr %p\n",
		 __func__, es, (unsigned long long)es->sector, es->offset,
		 es->alloc_flag, es->num_entries, &es->__buf);
	return es;
err_out:
	pr_debug("%s exited NULL (es %p)\n", __func__, es);
	kfree(es);
	return NULL;
}

void release_entry_set(struct entry_set_cache_t *es)
{
	pr_debug("%s es=%p\n", __func__, es);
	kfree(es);
}

/* search EMPTY CONTINUOUS "num_entries" entries */
static s32 search_deleted_or_unused_entry(struct super_block *sb,
				   struct chain_t *p_dir, s32 num_entries)
{
	int i, dentry, num_empty = 0;
	s32 dentries_per_clu;
	u32 type;
	struct chain_t clu;
	struct dentry_t *ep;
	struct fs_info_t *p_fs = &(EXFAT_SB(sb)->fs_info);

	if (p_dir->dir == CLUSTER_32(0)) /* FAT16 root_dir */
		dentries_per_clu = p_fs->dentries_in_root;
	else
		dentries_per_clu = p_fs->dentries_per_clu;

	if (p_fs->hint_uentry.dir == p_dir->dir) {
		if (p_fs->hint_uentry.entry == -1)
			return -1;

		clu.dir = p_fs->hint_uentry.clu.dir;
		clu.size = p_fs->hint_uentry.clu.size;
		clu.flags = p_fs->hint_uentry.clu.flags;

		dentry = p_fs->hint_uentry.entry;
	} else {
		p_fs->hint_uentry.entry = -1;

		clu.dir = p_dir->dir;
		clu.size = p_dir->size;
		clu.flags = p_dir->flags;

		dentry = 0;
	}

	while (clu.dir != CLUSTER_32(~0)) {
		if (p_fs->dev_ejected)
			break;

		if (p_dir->dir == CLUSTER_32(0)) /* FAT16 root_dir */
			i = dentry % dentries_per_clu;
		else
			i = dentry & (dentries_per_clu - 1);

		for (; i < dentries_per_clu; i++, dentry++) {
			ep = get_entry_in_dir(sb, &clu, i, NULL);
			if (!ep)
				return -1;

			type = p_fs->fs_func->get_entry_type(ep);

			if (type == TYPE_UNUSED) {
				num_empty++;
				if (p_fs->hint_uentry.entry == -1) {
					p_fs->hint_uentry.dir = p_dir->dir;
					p_fs->hint_uentry.entry = dentry;

					p_fs->hint_uentry.clu.dir = clu.dir;
					p_fs->hint_uentry.clu.size = clu.size;
					p_fs->hint_uentry.clu.flags = clu.flags;
				}
			} else if (type == TYPE_DELETED) {
				num_empty++;
			} else {
				num_empty = 0;
			}

			if (num_empty >= num_entries) {
				p_fs->hint_uentry.dir = CLUSTER_32(~0);
				p_fs->hint_uentry.entry = -1;

				if (p_fs->vol_type == EXFAT)
					return dentry - (num_entries - 1);
				else
					return dentry;
			}
		}

		if (p_dir->dir == CLUSTER_32(0))
			break; /* FAT16 root_dir */

		if (clu.flags == 0x03) {
			if ((--clu.size) > 0)
				clu.dir++;
			else
				clu.dir = CLUSTER_32(~0);
		} else {
			if (exfat_fat_read(sb, clu.dir, &clu.dir) != 0)
				return -1;
		}
	}

	return -1;
}

static s32 find_empty_entry(struct inode *inode, struct chain_t *p_dir, s32 num_entries)
{
	s32 ret, dentry;
	u32 last_clu;
	sector_t sector;
	u64 size = 0;
	struct chain_t clu;
	struct dentry_t *ep = NULL;
	struct super_block *sb = inode->i_sb;
	struct fs_info_t *p_fs = &(EXFAT_SB(sb)->fs_info);
	struct file_id_t *fid = &(EXFAT_I(inode)->fid);

	if (p_dir->dir == CLUSTER_32(0)) /* FAT16 root_dir */
		return search_deleted_or_unused_entry(sb, p_dir, num_entries);

	while ((dentry = search_deleted_or_unused_entry(sb, p_dir, num_entries)) < 0) {
		if (p_fs->dev_ejected)
			break;

		if (p_dir->dir != p_fs->root_dir)
			size = i_size_read(inode);

		last_clu = find_last_cluster(sb, p_dir);
		clu.dir = last_clu + 1;
		clu.size = 0;
		clu.flags = p_dir->flags;

		/* (1) allocate a cluster */
		ret = p_fs->fs_func->alloc_cluster(sb, 1, &clu);
		if (ret < 1)
			return -EIO;

		if (clear_cluster(sb, clu.dir) != 0)
			return -EIO;

		/* (2) append to the FAT chain */
		if (clu.flags != p_dir->flags) {
			exfat_chain_cont_cluster(sb, p_dir->dir, p_dir->size);
			p_dir->flags = 0x01;
			p_fs->hint_uentry.clu.flags = 0x01;
		}
		if (clu.flags == 0x01)
			if (exfat_fat_write(sb, last_clu, clu.dir) < 0)
				return -EIO;

		if (p_fs->hint_uentry.entry == -1) {
			p_fs->hint_uentry.dir = p_dir->dir;
			p_fs->hint_uentry.entry = p_dir->size << (p_fs->cluster_size_bits - DENTRY_SIZE_BITS);

			p_fs->hint_uentry.clu.dir = clu.dir;
			p_fs->hint_uentry.clu.size = 0;
			p_fs->hint_uentry.clu.flags = clu.flags;
		}
		p_fs->hint_uentry.clu.size++;
		p_dir->size++;

		/* (3) update the directory entry */
		if (p_dir->dir != p_fs->root_dir) {
			size += p_fs->cluster_size;

			ep = get_entry_in_dir(sb, &fid->dir,
					      fid->entry + 1, &sector);
			if (!ep)
				return -ENOENT;
			p_fs->fs_func->set_entry_size(ep, size);
			p_fs->fs_func->set_entry_flag(ep, p_dir->flags);
			exfat_buf_modify(sb, sector);

			update_dir_checksum(sb, &fid->dir,
					    fid->entry);
		}

		i_size_write(inode, i_size_read(inode) + p_fs->cluster_size);
		EXFAT_I(inode)->mmu_private += p_fs->cluster_size;
		EXFAT_I(inode)->fid.size += p_fs->cluster_size;
		EXFAT_I(inode)->fid.flags = p_dir->flags;
		inode->i_blocks += 1 << (p_fs->cluster_size_bits - 9);
	}

	return dentry;
}

static s32 extract_uni_name_from_name_entry(struct name_dentry_t *ep, u16 *uniname,
				     s32 order)
{
	int i, len = 0;

	for (i = 0; i < 30; i += 2) {
		*uniname = GET16_A(ep->unicode_0_14 + i);
		if (*uniname == 0x0)
			return len;
		uniname++;
		len++;
	}

	*uniname = 0x0;
	return len;
}

/* return values of exfat_find_dir_entry()
 * >= 0 : return dir entiry position with the name in dir
 * -1 : (root dir, ".") it is the root dir itself
 * -2 : entry with the name does not exist
 */
static s32 exfat_find_dir_entry(struct super_block *sb, struct chain_t *p_dir,
			 struct uni_name_t *p_uniname, s32 num_entries,
			 struct dos_name_t *p_dosname, u32 type)
{
	int i = 0, dentry = 0, num_ext_entries = 0, len, step;
	s32 order = 0;
	bool is_feasible_entry = false;
	s32 dentries_per_clu, num_empty = 0;
	u32 entry_type;
	u16 entry_uniname[16], *uniname = NULL, unichar;
	struct chain_t clu;
	struct dentry_t *ep;
	struct file_dentry_t *file_ep;
	struct strm_dentry_t *strm_ep;
	struct name_dentry_t *name_ep;
	struct fs_info_t *p_fs = &(EXFAT_SB(sb)->fs_info);

	if (p_dir->dir == p_fs->root_dir) {
		if ((!nls_uniname_cmp(sb, p_uniname->name,
				      (u16 *)UNI_CUR_DIR_NAME)) ||
			(!nls_uniname_cmp(sb, p_uniname->name,
					  (u16 *)UNI_PAR_DIR_NAME)))
			return -1; // special case, root directory itself
	}

	if (p_dir->dir == CLUSTER_32(0)) /* FAT16 root_dir */
		dentries_per_clu = p_fs->dentries_in_root;
	else
		dentries_per_clu = p_fs->dentries_per_clu;

	clu.dir = p_dir->dir;
	clu.size = p_dir->size;
	clu.flags = p_dir->flags;

	p_fs->hint_uentry.dir = p_dir->dir;
	p_fs->hint_uentry.entry = -1;

	while (clu.dir != CLUSTER_32(~0)) {
		if (p_fs->dev_ejected)
			break;

		while (i < dentries_per_clu) {
			ep = get_entry_in_dir(sb, &clu, i, NULL);
			if (!ep)
				return -2;

			entry_type = p_fs->fs_func->get_entry_type(ep);
			step = 1;

			if ((entry_type == TYPE_UNUSED) || (entry_type == TYPE_DELETED)) {
				is_feasible_entry = false;

				if (p_fs->hint_uentry.entry == -1) {
					num_empty++;

					if (num_empty == 1) {
						p_fs->hint_uentry.clu.dir = clu.dir;
						p_fs->hint_uentry.clu.size = clu.size;
						p_fs->hint_uentry.clu.flags = clu.flags;
					}
					if ((num_empty >= num_entries) || (entry_type == TYPE_UNUSED))
						p_fs->hint_uentry.entry = dentry - (num_empty - 1);
				}

				if (entry_type == TYPE_UNUSED)
					return -2;
			} else {
				num_empty = 0;

				if ((entry_type == TYPE_FILE) || (entry_type == TYPE_DIR)) {
					file_ep = (struct file_dentry_t *)ep;
					if ((type == TYPE_ALL) || (type == entry_type)) {
						num_ext_entries = file_ep->num_ext;
						is_feasible_entry = true;
					} else {
						is_feasible_entry = false;
						step = file_ep->num_ext + 1;
					}
				} else if (entry_type == TYPE_STREAM) {
					if (is_feasible_entry) {
						strm_ep = (struct strm_dentry_t *)ep;
						if (p_uniname->name_hash == GET16_A(strm_ep->name_hash) &&
						    p_uniname->name_len == strm_ep->name_len) {
							order = 1;
						} else {
							is_feasible_entry = false;
							step = num_ext_entries;
						}
					}
				} else if (entry_type == TYPE_EXTEND) {
					if (is_feasible_entry) {
						name_ep = (struct name_dentry_t *)ep;

						if ((++order) == 2)
							uniname = p_uniname->name;
						else
							uniname += 15;

						len = extract_uni_name_from_name_entry(name_ep,
								entry_uniname, order);

						unichar = *(uniname + len);
						*(uniname + len) = 0x0;

						if (nls_uniname_cmp(sb, uniname, entry_uniname)) {
							is_feasible_entry = false;
							step = num_ext_entries - order + 1;
						} else if (order == num_ext_entries) {
							p_fs->hint_uentry.dir = CLUSTER_32(~0);
							p_fs->hint_uentry.entry = -1;
							return dentry - (num_ext_entries);
						}

						*(uniname + len) = unichar;
					}
				} else {
					is_feasible_entry = false;
				}
			}

			i += step;
			dentry += step;
		}

		i -= dentries_per_clu;

		if (p_dir->dir == CLUSTER_32(0))
			break; /* FAT16 root_dir */

		if (clu.flags == 0x03) {
			if ((--clu.size) > 0)
				clu.dir++;
			else
				clu.dir = CLUSTER_32(~0);
		} else {
			if (exfat_fat_read(sb, clu.dir, &clu.dir) != 0)
				return -2;
		}
	}

	return -2;
}

static s32 exfat_count_ext_entries(struct super_block *sb, struct chain_t *p_dir,
			    s32 entry, struct dentry_t *p_entry)
{
	int i, count = 0;
	u32 type;
	struct file_dentry_t *file_ep = (struct file_dentry_t *)p_entry;
	struct dentry_t *ext_ep;
	struct fs_info_t *p_fs = &(EXFAT_SB(sb)->fs_info);

	for (i = 0, entry++; i < file_ep->num_ext; i++, entry++) {
		ext_ep = get_entry_in_dir(sb, p_dir, entry, NULL);
		if (!ext_ep)
			return -1;

		type = p_fs->fs_func->get_entry_type(ext_ep);
		if ((type == TYPE_EXTEND) || (type == TYPE_STREAM))
			count++;
		else
			return count;
	}

	return count;
}

s32 count_dos_name_entries(struct super_block *sb, struct chain_t *p_dir,
			   u32 type)
{
	int i, count = 0;
	s32 dentries_per_clu;
	u32 entry_type;
	struct chain_t clu;
	struct dentry_t *ep;
	struct fs_info_t *p_fs = &(EXFAT_SB(sb)->fs_info);

	if (p_dir->dir == CLUSTER_32(0)) /* FAT16 root_dir */
		dentries_per_clu = p_fs->dentries_in_root;
	else
		dentries_per_clu = p_fs->dentries_per_clu;

	clu.dir = p_dir->dir;
	clu.size = p_dir->size;
	clu.flags = p_dir->flags;

	while (clu.dir != CLUSTER_32(~0)) {
		if (p_fs->dev_ejected)
			break;

		for (i = 0; i < dentries_per_clu; i++) {
			ep = get_entry_in_dir(sb, &clu, i, NULL);
			if (!ep)
				return -ENOENT;

			entry_type = p_fs->fs_func->get_entry_type(ep);

			if (entry_type == TYPE_UNUSED)
				return count;
			if (!(type & TYPE_CRITICAL_PRI) &&
			    !(type & TYPE_BENIGN_PRI))
				continue;

			if ((type == TYPE_ALL) || (type == entry_type))
				count++;
		}

		if (p_dir->dir == CLUSTER_32(0))
			break; /* FAT16 root_dir */

		if (clu.flags == 0x03) {
			if ((--clu.size) > 0)
				clu.dir++;
			else
				clu.dir = CLUSTER_32(~0);
		} else {
			if (exfat_fat_read(sb, clu.dir, &clu.dir) != 0)
				return -EIO;
		}
	}

	return count;
}

bool is_dir_empty(struct super_block *sb, struct chain_t *p_dir)
{
	int i, count = 0;
	s32 dentries_per_clu;
	u32 type;
	struct chain_t clu;
	struct dentry_t *ep;
	struct fs_info_t *p_fs = &(EXFAT_SB(sb)->fs_info);

	if (p_dir->dir == CLUSTER_32(0)) /* FAT16 root_dir */
		dentries_per_clu = p_fs->dentries_in_root;
	else
		dentries_per_clu = p_fs->dentries_per_clu;

	clu.dir = p_dir->dir;
	clu.size = p_dir->size;
	clu.flags = p_dir->flags;

	while (clu.dir != CLUSTER_32(~0)) {
		if (p_fs->dev_ejected)
			break;

		for (i = 0; i < dentries_per_clu; i++) {
			ep = get_entry_in_dir(sb, &clu, i, NULL);
			if (!ep)
				break;

			type = p_fs->fs_func->get_entry_type(ep);

			if (type == TYPE_UNUSED)
				return true;
			if ((type != TYPE_FILE) && (type != TYPE_DIR))
				continue;

			if (p_dir->dir == CLUSTER_32(0)) /* FAT16 root_dir */
				return false;

			if (p_fs->vol_type == EXFAT)
				return false;
			if ((p_dir->dir == p_fs->root_dir) || ((++count) > 2))
				return false;
		}

		if (p_dir->dir == CLUSTER_32(0))
			break; /* FAT16 root_dir */

		if (clu.flags == 0x03) {
			if ((--clu.size) > 0)
				clu.dir++;
			else
				clu.dir = CLUSTER_32(~0);
		}
		if (exfat_fat_read(sb, clu.dir, &clu.dir) != 0)
			break;
	}

	return true;
}

/*
 *  Name Conversion Functions
 */

/* input  : dir, uni_name
 * output : num_of_entry, dos_name(format : aaaaaa~1.bbb)
 */
s32 get_num_entries_and_dos_name(struct super_block *sb, struct chain_t *p_dir,
				 struct uni_name_t *p_uniname, s32 *entries,
				 struct dos_name_t *p_dosname)
{
	s32 num_entries;
	struct fs_info_t *p_fs = &(EXFAT_SB(sb)->fs_info);

	num_entries = p_fs->fs_func->calc_num_entries(p_uniname);
	if (num_entries == 0)
		return -EINVAL;

	*entries = num_entries;

	return 0;
}

static void exfat_get_uni_name_from_ext_entry(struct super_block *sb,
				       struct chain_t *p_dir, s32 entry,
				       u16 *uniname)
{
	int i;
	struct dentry_t *ep;
	struct entry_set_cache_t *es;
	struct fs_info_t *p_fs = &(EXFAT_SB(sb)->fs_info);

	es = get_entry_set_in_dir(sb, p_dir, entry, ES_ALL_ENTRIES, &ep);
	if (!es || es->num_entries < 3) {
		if (es)
			release_entry_set(es);
		return;
	}

	ep += 2;

	/*
	 * First entry  : file entry
	 * Second entry : stream-extension entry
	 * Third entry  : first file-name entry
	 * So, the index of first file-name dentry should start from 2.
	 */
	for (i = 2; i < es->num_entries; i++, ep++) {
		if (p_fs->fs_func->get_entry_type(ep) == TYPE_EXTEND)
			extract_uni_name_from_name_entry((struct name_dentry_t *)
							 ep, uniname, i);
		else
			goto out;
		uniname += 15;
	}

out:
	release_entry_set(es);
}

static s32 exfat_calc_num_entries(struct uni_name_t *p_uniname)
{
	s32 len;

	len = p_uniname->name_len;
	if (len == 0)
		return 0;

	/* 1 file entry + 1 stream entry + name entries */
	return (len - 1) / 15 + 3;
}

u16 calc_checksum_2byte(void *data, s32 len, u16 chksum, s32 type)
{
	int i;
	u8 *c = (u8 *)data;

	switch (type) {
	case CS_DIR_ENTRY:
		for (i = 0; i < len; i++, c++) {
			if ((i == 2) || (i == 3))
				continue;
			chksum = (((chksum & 1) << 15) |
				  ((chksum & 0xFFFE) >> 1)) + (u16)*c;
		}
		break;
	default
			:
		for (i = 0; i < len; i++, c++)
			chksum = (((chksum & 1) << 15) |
				  ((chksum & 0xFFFE) >> 1)) + (u16)*c;
	}

	return chksum;
}

/*
 *  Name Resolution Functions
 */

/* return values of resolve_path()
 * > 0 : return the length of the path
 * < 0 : return error
 */
s32 resolve_path(struct inode *inode, char *path, struct chain_t *p_dir,
		 struct uni_name_t *p_uniname)
{
	bool lossy = false;
	struct super_block *sb = inode->i_sb;
	struct fs_info_t *p_fs = &(EXFAT_SB(sb)->fs_info);
	struct file_id_t *fid = &(EXFAT_I(inode)->fid);

	if (strscpy(name_buf, path, sizeof(name_buf)) < 0)
		return -EINVAL;

	nls_cstring_to_uniname(sb, p_uniname, name_buf, &lossy);
	if (lossy)
		return -EINVAL;

	fid->size = i_size_read(inode);

	p_dir->dir = fid->start_clu;
	p_dir->size = (s32)(fid->size >> p_fs->cluster_size_bits);
	p_dir->flags = fid->flags;

	return 0;
}

/*
 *  File Operation Functions
 */
static struct fs_func exfat_fs_func = {
	.alloc_cluster = exfat_alloc_cluster,
	.free_cluster = exfat_free_cluster,
	.count_used_clusters = exfat_count_used_clusters,

	.init_dir_entry = exfat_init_dir_entry,
	.init_ext_entry = exfat_init_ext_entry,
	.find_dir_entry = exfat_find_dir_entry,
	.delete_dir_entry = exfat_delete_dir_entry,
	.get_uni_name_from_ext_entry = exfat_get_uni_name_from_ext_entry,
	.count_ext_entries = exfat_count_ext_entries,
	.calc_num_entries = exfat_calc_num_entries,

	.get_entry_type = exfat_get_entry_type,
	.set_entry_type = exfat_set_entry_type,
	.get_entry_attr = exfat_get_entry_attr,
	.set_entry_attr = exfat_set_entry_attr,
	.get_entry_flag = exfat_get_entry_flag,
	.set_entry_flag = exfat_set_entry_flag,
	.get_entry_clu0 = exfat_get_entry_clu0,
	.set_entry_clu0 = exfat_set_entry_clu0,
	.get_entry_size = exfat_get_entry_size,
	.set_entry_size = exfat_set_entry_size,
	.get_entry_time = exfat_get_entry_time,
	.set_entry_time = exfat_set_entry_time,
};

s32 exfat_mount(struct super_block *sb, struct pbr_sector_t *p_pbr)
{
	struct bpbex_t *p_bpb = (struct bpbex_t *)p_pbr->bpb;
	struct fs_info_t *p_fs = &(EXFAT_SB(sb)->fs_info);
	struct bd_info_t *p_bd = &(EXFAT_SB(sb)->bd_info);

	if (p_bpb->num_fats == 0)
		return -EFSCORRUPTED;

	p_fs->sectors_per_clu = 1 << p_bpb->sectors_per_clu_bits;
	p_fs->sectors_per_clu_bits = p_bpb->sectors_per_clu_bits;
	p_fs->cluster_size_bits = p_fs->sectors_per_clu_bits +
				  p_bd->sector_size_bits;
	p_fs->cluster_size = 1 << p_fs->cluster_size_bits;

	p_fs->num_FAT_sectors = GET32(p_bpb->fat_length);

	p_fs->FAT1_start_sector = p_fs->PBR_sector + GET32(p_bpb->fat_offset);
	if (p_bpb->num_fats == 1)
		p_fs->FAT2_start_sector = p_fs->FAT1_start_sector;
	else
		p_fs->FAT2_start_sector = p_fs->FAT1_start_sector +
					  p_fs->num_FAT_sectors;

	p_fs->root_start_sector = p_fs->PBR_sector + GET32(p_bpb->clu_offset);
	p_fs->data_start_sector = p_fs->root_start_sector;

	p_fs->num_sectors = GET64(p_bpb->vol_length);
	p_fs->num_clusters = GET32(p_bpb->clu_count) + 2;
	/* because the cluster index starts with 2 */

	p_fs->vol_type = EXFAT;
	p_fs->vol_id = GET32(p_bpb->vol_serial);

	p_fs->root_dir = GET32(p_bpb->root_cluster);
	p_fs->dentries_in_root = 0;
	p_fs->dentries_per_clu = 1 << (p_fs->cluster_size_bits -
				       DENTRY_SIZE_BITS);

	p_fs->vol_flag = (u32)GET16(p_bpb->vol_flags);
	p_fs->clu_srch_ptr = 2;
	p_fs->used_clusters = UINT_MAX;

	p_fs->fs_func = &exfat_fs_func;

	return 0;
}

s32 create_dir(struct inode *inode, struct chain_t *p_dir,
	       struct uni_name_t *p_uniname, struct file_id_t *fid)
{
	s32 ret, dentry, num_entries;
	u64 size;
	struct chain_t clu;
	struct dos_name_t dos_name;
	struct super_block *sb = inode->i_sb;
	struct fs_info_t *p_fs = &(EXFAT_SB(sb)->fs_info);
	struct fs_func *fs_func = p_fs->fs_func;

	ret = get_num_entries_and_dos_name(sb, p_dir, p_uniname, &num_entries,
					   &dos_name);
	if (ret)
		return ret;

	/* find_empty_entry must be called before alloc_cluster */
	dentry = find_empty_entry(inode, p_dir, num_entries);
	if (dentry < 0)
		return -ENOSPC;

	clu.dir = CLUSTER_32(~0);
	clu.size = 0;
	clu.flags = (p_fs->vol_type == EXFAT) ? 0x03 : 0x01;

	/* (1) allocate a cluster */
	ret = fs_func->alloc_cluster(sb, 1, &clu);
	if (ret < 0)
		return ret;
	else if (ret == 0)
		return -ENOSPC;

	ret = clear_cluster(sb, clu.dir);
	if (ret != 0)
		return ret;

	size = p_fs->cluster_size;

	/* (2) update the directory entry */
	/* make sub-dir entry in parent directory */
	ret = fs_func->init_dir_entry(sb, p_dir, dentry, TYPE_DIR, clu.dir,
				      size);
	if (ret != 0)
		return ret;

	ret = fs_func->init_ext_entry(sb, p_dir, dentry, num_entries, p_uniname,
				      &dos_name);
	if (ret != 0)
		return ret;

	fid->dir.dir = p_dir->dir;
	fid->dir.size = p_dir->size;
	fid->dir.flags = p_dir->flags;
	fid->entry = dentry;

	fid->attr = ATTR_SUBDIR;
	fid->flags = (p_fs->vol_type == EXFAT) ? 0x03 : 0x01;
	fid->size = size;
	fid->start_clu = clu.dir;

	fid->type = TYPE_DIR;
	fid->rwoffset = 0;
	fid->hint_last_off = -1;

	return 0;
}

s32 create_file(struct inode *inode, struct chain_t *p_dir,
		struct uni_name_t *p_uniname, u8 mode, struct file_id_t *fid)
{
	s32 ret, dentry, num_entries;
	struct dos_name_t dos_name;
	struct super_block *sb = inode->i_sb;
	struct fs_info_t *p_fs = &(EXFAT_SB(sb)->fs_info);
	struct fs_func *fs_func = p_fs->fs_func;

	ret = get_num_entries_and_dos_name(sb, p_dir, p_uniname, &num_entries,
					   &dos_name);
	if (ret)
		return ret;

	/* find_empty_entry must be called before alloc_cluster() */
	dentry = find_empty_entry(inode, p_dir, num_entries);
	if (dentry < 0)
		return -ENOSPC;

	/* (1) update the directory entry */
	/* fill the dos name directory entry information of the created file.
	 * the first cluster is not determined yet. (0)
	 */
	ret = fs_func->init_dir_entry(sb, p_dir, dentry, TYPE_FILE | mode,
				      CLUSTER_32(0), 0);
	if (ret != 0)
		return ret;

	ret = fs_func->init_ext_entry(sb, p_dir, dentry, num_entries, p_uniname,
				      &dos_name);
	if (ret != 0)
		return ret;

	fid->dir.dir = p_dir->dir;
	fid->dir.size = p_dir->size;
	fid->dir.flags = p_dir->flags;
	fid->entry = dentry;

	fid->attr = ATTR_ARCHIVE | mode;
	fid->flags = (p_fs->vol_type == EXFAT) ? 0x03 : 0x01;
	fid->size = 0;
	fid->start_clu = CLUSTER_32(~0);

	fid->type = TYPE_FILE;
	fid->rwoffset = 0;
	fid->hint_last_off = -1;

	return 0;
}

void remove_file(struct inode *inode, struct chain_t *p_dir, s32 entry)
{
	s32 num_entries;
	sector_t sector;
	struct dentry_t *ep;
	struct super_block *sb = inode->i_sb;
	struct fs_info_t *p_fs = &(EXFAT_SB(sb)->fs_info);
	struct fs_func *fs_func = p_fs->fs_func;

	ep = get_entry_in_dir(sb, p_dir, entry, &sector);
	if (!ep)
		return;

	exfat_buf_lock(sb, sector);

	/* exfat_buf_lock() before call count_ext_entries() */
	num_entries = fs_func->count_ext_entries(sb, p_dir, entry, ep);
	if (num_entries < 0) {
		exfat_buf_unlock(sb, sector);
		return;
	}
	num_entries++;

	exfat_buf_unlock(sb, sector);

	/* (1) update the directory entry */
	fs_func->delete_dir_entry(sb, p_dir, entry, 0, num_entries);
}

s32 exfat_rename_file(struct inode *inode, struct chain_t *p_dir, s32 oldentry,
		      struct uni_name_t *p_uniname, struct file_id_t *fid)
{
	s32 ret, newentry = -1, num_old_entries, num_new_entries;
	sector_t sector_old, sector_new;
	struct dos_name_t dos_name;
	struct dentry_t *epold, *epnew;
	struct super_block *sb = inode->i_sb;
	struct fs_info_t *p_fs = &(EXFAT_SB(sb)->fs_info);
	struct fs_func *fs_func = p_fs->fs_func;

	epold = get_entry_in_dir(sb, p_dir, oldentry, &sector_old);
	if (!epold)
		return -ENOENT;

	exfat_buf_lock(sb, sector_old);

	/* exfat_buf_lock() before call count_ext_entries() */
	num_old_entries = fs_func->count_ext_entries(sb, p_dir, oldentry,
						     epold);
	if (num_old_entries < 0) {
		exfat_buf_unlock(sb, sector_old);
		return -ENOENT;
	}
	num_old_entries++;

	ret = get_num_entries_and_dos_name(sb, p_dir, p_uniname,
					   &num_new_entries, &dos_name);
	if (ret) {
		exfat_buf_unlock(sb, sector_old);
		return ret;
	}

	if (num_old_entries < num_new_entries) {
		newentry = find_empty_entry(inode, p_dir, num_new_entries);
		if (newentry < 0) {
			exfat_buf_unlock(sb, sector_old);
			return -ENOSPC;
		}

		epnew = get_entry_in_dir(sb, p_dir, newentry, &sector_new);
		if (!epnew) {
			exfat_buf_unlock(sb, sector_old);
			return -ENOENT;
		}

		memcpy((void *)epnew, (void *)epold, DENTRY_SIZE);
		if (fs_func->get_entry_type(epnew) == TYPE_FILE) {
			fs_func->set_entry_attr(epnew,
						fs_func->get_entry_attr(epnew) |
						ATTR_ARCHIVE);
			fid->attr |= ATTR_ARCHIVE;
		}
		exfat_buf_modify(sb, sector_new);
		exfat_buf_unlock(sb, sector_old);

		epold = get_entry_in_dir(sb, p_dir, oldentry + 1,
					 &sector_old);
		exfat_buf_lock(sb, sector_old);
		epnew = get_entry_in_dir(sb, p_dir, newentry + 1,
					 &sector_new);

		if (!epold || !epnew) {
			exfat_buf_unlock(sb, sector_old);
			return -ENOENT;
		}

		memcpy((void *)epnew, (void *)epold, DENTRY_SIZE);
		exfat_buf_modify(sb, sector_new);
		exfat_buf_unlock(sb, sector_old);

		ret = fs_func->init_ext_entry(sb, p_dir, newentry,
					      num_new_entries, p_uniname,
					      &dos_name);
		if (ret != 0)
			return ret;

		fs_func->delete_dir_entry(sb, p_dir, oldentry, 0,
					  num_old_entries);
		fid->entry = newentry;
	} else {
		if (fs_func->get_entry_type(epold) == TYPE_FILE) {
			fs_func->set_entry_attr(epold,
						fs_func->get_entry_attr(epold) |
						ATTR_ARCHIVE);
			fid->attr |= ATTR_ARCHIVE;
		}
		exfat_buf_modify(sb, sector_old);
		exfat_buf_unlock(sb, sector_old);

		ret = fs_func->init_ext_entry(sb, p_dir, oldentry,
					      num_new_entries, p_uniname,
					      &dos_name);
		if (ret != 0)
			return ret;

		fs_func->delete_dir_entry(sb, p_dir, oldentry, num_new_entries,
					  num_old_entries);
	}

	return 0;
}

s32 move_file(struct inode *inode, struct chain_t *p_olddir, s32 oldentry,
	      struct chain_t *p_newdir, struct uni_name_t *p_uniname,
	      struct file_id_t *fid)
{
	s32 ret, newentry, num_new_entries, num_old_entries;
	sector_t sector_mov, sector_new;
	struct dos_name_t dos_name;
	struct dentry_t *epmov, *epnew;
	struct super_block *sb = inode->i_sb;
	struct fs_info_t *p_fs = &(EXFAT_SB(sb)->fs_info);
	struct fs_func *fs_func = p_fs->fs_func;

	epmov = get_entry_in_dir(sb, p_olddir, oldentry, &sector_mov);
	if (!epmov)
		return -ENOENT;

	/* check if the source and target directory is the same */
	if (fs_func->get_entry_type(epmov) == TYPE_DIR &&
	    fs_func->get_entry_clu0(epmov) == p_newdir->dir)
		return -EINVAL;

	exfat_buf_lock(sb, sector_mov);

	/* exfat_buf_lock() before call count_ext_entries() */
	num_old_entries = fs_func->count_ext_entries(sb, p_olddir, oldentry,
						     epmov);
	if (num_old_entries < 0) {
		exfat_buf_unlock(sb, sector_mov);
		return -ENOENT;
	}
	num_old_entries++;

	ret = get_num_entries_and_dos_name(sb, p_newdir, p_uniname,
					   &num_new_entries, &dos_name);
	if (ret) {
		exfat_buf_unlock(sb, sector_mov);
		return ret;
	}

	newentry = find_empty_entry(inode, p_newdir, num_new_entries);
	if (newentry < 0) {
		exfat_buf_unlock(sb, sector_mov);
		return -ENOSPC;
	}

	epnew = get_entry_in_dir(sb, p_newdir, newentry, &sector_new);
	if (!epnew) {
		exfat_buf_unlock(sb, sector_mov);
		return -ENOENT;
	}

	memcpy((void *)epnew, (void *)epmov, DENTRY_SIZE);
	if (fs_func->get_entry_type(epnew) == TYPE_FILE) {
		fs_func->set_entry_attr(epnew, fs_func->get_entry_attr(epnew) |
					ATTR_ARCHIVE);
		fid->attr |= ATTR_ARCHIVE;
	}
	exfat_buf_modify(sb, sector_new);
	exfat_buf_unlock(sb, sector_mov);

	epmov = get_entry_in_dir(sb, p_olddir, oldentry + 1,
				 &sector_mov);
	exfat_buf_lock(sb, sector_mov);
	epnew = get_entry_in_dir(sb, p_newdir, newentry + 1,
				 &sector_new);
	if (!epmov || !epnew) {
		exfat_buf_unlock(sb, sector_mov);
		return -ENOENT;
	}

	memcpy((void *)epnew, (void *)epmov, DENTRY_SIZE);
	exfat_buf_modify(sb, sector_new);
	exfat_buf_unlock(sb, sector_mov);

	ret = fs_func->init_ext_entry(sb, p_newdir, newentry, num_new_entries,
				      p_uniname, &dos_name);
	if (ret != 0)
		return ret;

	fs_func->delete_dir_entry(sb, p_olddir, oldentry, 0, num_old_entries);

	fid->dir.dir = p_newdir->dir;
	fid->dir.size = p_newdir->size;
	fid->dir.flags = p_newdir->flags;

	fid->entry = newentry;

	return 0;
}

/*
 *  Sector Read/Write Functions
 */

int sector_read(struct super_block *sb, sector_t sec, struct buffer_head **bh,
		bool read)
{
	s32 ret = -EIO;
	struct fs_info_t *p_fs = &(EXFAT_SB(sb)->fs_info);

	if ((sec >= (p_fs->PBR_sector + p_fs->num_sectors)) &&
	    (p_fs->num_sectors > 0)) {
		pr_err("[EXFAT] %s: out of range error! (sec = %llu)\n",
		       __func__, (unsigned long long)sec);
		fs_error(sb);
		return ret;
	}

	if (!p_fs->dev_ejected) {
		ret = exfat_bdev_read(sb, sec, bh, 1, read);
		if (ret != 0)
			p_fs->dev_ejected = 1;
	}

	return ret;
}

int sector_write(struct super_block *sb, sector_t sec, struct buffer_head *bh,
		 bool sync)
{
	s32 ret = -EIO;
	struct fs_info_t *p_fs = &(EXFAT_SB(sb)->fs_info);

	if (sec >= (p_fs->PBR_sector + p_fs->num_sectors) &&
	    (p_fs->num_sectors > 0)) {
		pr_err("[EXFAT] %s: out of range error! (sec = %llu)\n",
		       __func__, (unsigned long long)sec);
		fs_error(sb);
		return ret;
	}

	if (!bh) {
		pr_err("[EXFAT] %s: bh is NULL!\n", __func__);
		fs_error(sb);
		return ret;
	}

	if (!p_fs->dev_ejected) {
		ret = exfat_bdev_write(sb, sec, bh, 1, sync);
		if (ret != 0)
			p_fs->dev_ejected = 1;
	}

	return ret;
}

int multi_sector_read(struct super_block *sb, sector_t sec,
		      struct buffer_head **bh, s32 num_secs, bool read)
{
	s32 ret = -EIO;
	struct fs_info_t *p_fs = &(EXFAT_SB(sb)->fs_info);

	if (((sec + num_secs) > (p_fs->PBR_sector + p_fs->num_sectors)) &&
	    (p_fs->num_sectors > 0)) {
		pr_err("[EXFAT] %s: out of range error! (sec = %llu, num_secs = %d)\n",
		       __func__, (unsigned long long)sec, num_secs);
		fs_error(sb);
		return ret;
	}

	if (!p_fs->dev_ejected) {
		ret = exfat_bdev_read(sb, sec, bh, num_secs, read);
		if (ret != 0)
			p_fs->dev_ejected = 1;
	}

	return ret;
}

int multi_sector_write(struct super_block *sb, sector_t sec,
		       struct buffer_head *bh, s32 num_secs, bool sync)
{
	s32 ret = -EIO;
	struct fs_info_t *p_fs = &(EXFAT_SB(sb)->fs_info);

	if ((sec + num_secs) > (p_fs->PBR_sector + p_fs->num_sectors) &&
	    (p_fs->num_sectors > 0)) {
		pr_err("[EXFAT] %s: out of range error! (sec = %llu, num_secs = %d)\n",
		       __func__, (unsigned long long)sec, num_secs);
		fs_error(sb);
		return ret;
	}
	if (!bh) {
		pr_err("[EXFAT] %s: bh is NULL!\n", __func__);
		fs_error(sb);
		return ret;
	}

	if (!p_fs->dev_ejected) {
		ret = exfat_bdev_write(sb, sec, bh, num_secs, sync);
		if (ret != 0)
			p_fs->dev_ejected = 1;
	}

	return ret;
}

// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Copyright (C) 2012-2013 Samsung Electronics Co., Ltd.
 */

#include <linux/buffer_head.h>
#include <linux/fs.h>
#include <linux/mutex.h>
#include "exfat.h"

#define LOCKBIT		0x01
#define DIRTYBIT	0x02

/* Local variables */
static DEFINE_SEMAPHORE(f_sem);
static DEFINE_SEMAPHORE(b_sem);

static struct buf_cache_t *FAT_cache_find(struct super_block *sb, sector_t sec)
{
	s32 off;
	struct buf_cache_t *bp, *hp;
	struct fs_info_t *p_fs = &(EXFAT_SB(sb)->fs_info);

	off = (sec +
	       (sec >> p_fs->sectors_per_clu_bits)) & (FAT_CACHE_HASH_SIZE - 1);

	hp = &p_fs->FAT_cache_hash_list[off];
	for (bp = hp->hash_next; bp != hp; bp = bp->hash_next) {
		if ((bp->drv == p_fs->drv) && (bp->sec == sec)) {
			WARN(!bp->buf_bh,
			     "[EXFAT] FAT_cache has no bh. It will make system panic.\n");

			touch_buffer(bp->buf_bh);
			return bp;
		}
	}
	return NULL;
}

static void push_to_mru(struct buf_cache_t *bp, struct buf_cache_t *list)
{
	bp->next = list->next;
	bp->prev = list;
	list->next->prev = bp;
	list->next = bp;
}

static void push_to_lru(struct buf_cache_t *bp, struct buf_cache_t *list)
{
	bp->prev = list->prev;
	bp->next = list;
	list->prev->next = bp;
	list->prev = bp;
}

static void move_to_mru(struct buf_cache_t *bp, struct buf_cache_t *list)
{
	bp->prev->next = bp->next;
	bp->next->prev = bp->prev;
	push_to_mru(bp, list);
}

static void move_to_lru(struct buf_cache_t *bp, struct buf_cache_t *list)
{
	bp->prev->next = bp->next;
	bp->next->prev = bp->prev;
	push_to_lru(bp, list);
}

static struct buf_cache_t *FAT_cache_get(struct super_block *sb, sector_t sec)
{
	struct buf_cache_t *bp;
	struct fs_info_t *p_fs = &(EXFAT_SB(sb)->fs_info);

	bp = p_fs->FAT_cache_lru_list.prev;

	move_to_mru(bp, &p_fs->FAT_cache_lru_list);
	return bp;
}

static void FAT_cache_insert_hash(struct super_block *sb,
				  struct buf_cache_t *bp)
{
	s32 off;
	struct buf_cache_t *hp;
	struct fs_info_t *p_fs;

	p_fs = &(EXFAT_SB(sb)->fs_info);
	off = (bp->sec +
	       (bp->sec >> p_fs->sectors_per_clu_bits)) &
		(FAT_CACHE_HASH_SIZE - 1);

	hp = &p_fs->FAT_cache_hash_list[off];
	bp->hash_next = hp->hash_next;
	bp->hash_prev = hp;
	hp->hash_next->hash_prev = bp;
	hp->hash_next = bp;
}

static void FAT_cache_remove_hash(struct buf_cache_t *bp)
{
	(bp->hash_prev)->hash_next = bp->hash_next;
	(bp->hash_next)->hash_prev = bp->hash_prev;
}

static void buf_cache_insert_hash(struct super_block *sb,
				  struct buf_cache_t *bp)
{
	s32 off;
	struct buf_cache_t *hp;
	struct fs_info_t *p_fs;

	p_fs = &(EXFAT_SB(sb)->fs_info);
	off = (bp->sec +
	       (bp->sec >> p_fs->sectors_per_clu_bits)) &
		(BUF_CACHE_HASH_SIZE - 1);

	hp = &p_fs->buf_cache_hash_list[off];
	bp->hash_next = hp->hash_next;
	bp->hash_prev = hp;
	hp->hash_next->hash_prev = bp;
	hp->hash_next = bp;
}

static void buf_cache_remove_hash(struct buf_cache_t *bp)
{
	(bp->hash_prev)->hash_next = bp->hash_next;
	(bp->hash_next)->hash_prev = bp->hash_prev;
}

void buf_init(struct super_block *sb)
{
	struct fs_info_t *p_fs = &(EXFAT_SB(sb)->fs_info);

	int i;

	/* LRU list */
	p_fs->FAT_cache_lru_list.next = &p_fs->FAT_cache_lru_list;
	p_fs->FAT_cache_lru_list.prev = &p_fs->FAT_cache_lru_list;

	for (i = 0; i < FAT_CACHE_SIZE; i++) {
		p_fs->FAT_cache_array[i].drv = -1;
		p_fs->FAT_cache_array[i].sec = ~0;
		p_fs->FAT_cache_array[i].flag = 0;
		p_fs->FAT_cache_array[i].buf_bh = NULL;
		p_fs->FAT_cache_array[i].prev = NULL;
		p_fs->FAT_cache_array[i].next = NULL;
		push_to_mru(&p_fs->FAT_cache_array[i],
			    &p_fs->FAT_cache_lru_list);
	}

	p_fs->buf_cache_lru_list.next = &p_fs->buf_cache_lru_list;
	p_fs->buf_cache_lru_list.prev = &p_fs->buf_cache_lru_list;

	for (i = 0; i < BUF_CACHE_SIZE; i++) {
		p_fs->buf_cache_array[i].drv = -1;
		p_fs->buf_cache_array[i].sec = ~0;
		p_fs->buf_cache_array[i].flag = 0;
		p_fs->buf_cache_array[i].buf_bh = NULL;
		p_fs->buf_cache_array[i].prev = NULL;
		p_fs->buf_cache_array[i].next = NULL;
		push_to_mru(&p_fs->buf_cache_array[i],
			    &p_fs->buf_cache_lru_list);
	}

	/* HASH list */
	for (i = 0; i < FAT_CACHE_HASH_SIZE; i++) {
		p_fs->FAT_cache_hash_list[i].drv = -1;
		p_fs->FAT_cache_hash_list[i].sec = ~0;
		p_fs->FAT_cache_hash_list[i].hash_next =
			&p_fs->FAT_cache_hash_list[i];
		p_fs->FAT_cache_hash_list[i].hash_prev =
			&p_fs->FAT_cache_hash_list[i];
	}

	for (i = 0; i < FAT_CACHE_SIZE; i++)
		FAT_cache_insert_hash(sb, &p_fs->FAT_cache_array[i]);

	for (i = 0; i < BUF_CACHE_HASH_SIZE; i++) {
		p_fs->buf_cache_hash_list[i].drv = -1;
		p_fs->buf_cache_hash_list[i].sec = ~0;
		p_fs->buf_cache_hash_list[i].hash_next =
			&p_fs->buf_cache_hash_list[i];
		p_fs->buf_cache_hash_list[i].hash_prev =
			&p_fs->buf_cache_hash_list[i];
	}

	for (i = 0; i < BUF_CACHE_SIZE; i++)
		buf_cache_insert_hash(sb, &p_fs->buf_cache_array[i]);
}

void buf_shutdown(struct super_block *sb)
{
}

static int __FAT_read(struct super_block *sb, u32 loc, u32 *content)
{
	s32 off;
	u32 _content;
	sector_t sec;
	u8 *fat_sector, *fat_entry;
	struct fs_info_t *p_fs = &(EXFAT_SB(sb)->fs_info);
	struct bd_info_t *p_bd = &(EXFAT_SB(sb)->bd_info);

	if (p_fs->vol_type == FAT12) {
		sec = p_fs->FAT1_start_sector +
			((loc + (loc >> 1)) >> p_bd->sector_size_bits);
		off = (loc + (loc >> 1)) & p_bd->sector_size_mask;

		if (off == (p_bd->sector_size - 1)) {
			fat_sector = FAT_getblk(sb, sec);
			if (!fat_sector)
				return -1;

			_content = (u32)fat_sector[off];

			fat_sector = FAT_getblk(sb, ++sec);
			if (!fat_sector)
				return -1;

			_content |= (u32)fat_sector[0] << 8;
		} else {
			fat_sector = FAT_getblk(sb, sec);
			if (!fat_sector)
				return -1;

			fat_entry = &fat_sector[off];
			_content = GET16(fat_entry);
		}

		if (loc & 1)
			_content >>= 4;

		_content &= 0x00000FFF;

		if (_content >= CLUSTER_16(0x0FF8)) {
			*content = CLUSTER_32(~0);
			return 0;
		}
		*content = CLUSTER_32(_content);
		return 0;
	} else if (p_fs->vol_type == FAT16) {
		sec = p_fs->FAT1_start_sector +
			(loc >> (p_bd->sector_size_bits - 1));
		off = (loc << 1) & p_bd->sector_size_mask;

		fat_sector = FAT_getblk(sb, sec);
		if (!fat_sector)
			return -1;

		fat_entry = &fat_sector[off];

		_content = GET16_A(fat_entry);

		_content &= 0x0000FFFF;

		if (_content >= CLUSTER_16(0xFFF8)) {
			*content = CLUSTER_32(~0);
			return 0;
		}
		*content = CLUSTER_32(_content);
		return 0;
	} else if (p_fs->vol_type == FAT32) {
		sec = p_fs->FAT1_start_sector +
			(loc >> (p_bd->sector_size_bits - 2));
		off = (loc << 2) & p_bd->sector_size_mask;

		fat_sector = FAT_getblk(sb, sec);
		if (!fat_sector)
			return -1;

		fat_entry = &fat_sector[off];

		_content = GET32_A(fat_entry);

		_content &= 0x0FFFFFFF;

		if (_content >= CLUSTER_32(0x0FFFFFF8)) {
			*content = CLUSTER_32(~0);
			return 0;
		}
		*content = CLUSTER_32(_content);
		return 0;
	} else if (p_fs->vol_type == EXFAT) {
		sec = p_fs->FAT1_start_sector +
			(loc >> (p_bd->sector_size_bits - 2));
		off = (loc << 2) & p_bd->sector_size_mask;

		fat_sector = FAT_getblk(sb, sec);
		if (!fat_sector)
			return -1;

		fat_entry = &fat_sector[off];
		_content = GET32_A(fat_entry);

		if (_content >= CLUSTER_32(0xFFFFFFF8)) {
			*content = CLUSTER_32(~0);
			return 0;
		}
		*content = CLUSTER_32(_content);
		return 0;
	}

	/* Unknown volume type, throw in the towel and go home */
	*content = CLUSTER_32(~0);
	return 0;
}

/* in : sb, loc
 * out: content
 * returns 0 on success
 *            -1 on error
 */
int FAT_read(struct super_block *sb, u32 loc, u32 *content)
{
	s32 ret;

	down(&f_sem);
	ret = __FAT_read(sb, loc, content);
	up(&f_sem);

	return ret;
}

static s32 __FAT_write(struct super_block *sb, u32 loc, u32 content)
{
	s32 off;
	sector_t sec;
	u8 *fat_sector, *fat_entry;
	struct fs_info_t *p_fs = &(EXFAT_SB(sb)->fs_info);
	struct bd_info_t *p_bd = &(EXFAT_SB(sb)->bd_info);

	if (p_fs->vol_type == FAT12) {
		content &= 0x00000FFF;

		sec = p_fs->FAT1_start_sector +
			((loc + (loc >> 1)) >> p_bd->sector_size_bits);
		off = (loc + (loc >> 1)) & p_bd->sector_size_mask;

		fat_sector = FAT_getblk(sb, sec);
		if (!fat_sector)
			return -1;

		if (loc & 1) { /* odd */
			content <<= 4;

			if (off == (p_bd->sector_size - 1)) {
				fat_sector[off] = (u8)(content |
						       (fat_sector[off] &
							0x0F));
				FAT_modify(sb, sec);

				fat_sector = FAT_getblk(sb, ++sec);
				if (!fat_sector)
					return -1;

				fat_sector[0] = (u8)(content >> 8);
			} else {
				fat_entry = &fat_sector[off];
				content |= GET16(fat_entry) & 0x000F;

				SET16(fat_entry, content);
			}
		} else { /* even */
			fat_sector[off] = (u8)(content);

			if (off == (p_bd->sector_size - 1)) {
				fat_sector[off] = (u8)(content);
				FAT_modify(sb, sec);

				fat_sector = FAT_getblk(sb, ++sec);
				if (!fat_sector)
					return -1;
				fat_sector[0] = (u8)((fat_sector[0] & 0xF0) |
						     (content >> 8));
			} else {
				fat_entry = &fat_sector[off];
				content |= GET16(fat_entry) & 0xF000;

				SET16(fat_entry, content);
			}
		}
	}

	else if (p_fs->vol_type == FAT16) {
		content &= 0x0000FFFF;

		sec = p_fs->FAT1_start_sector + (loc >>
						 (p_bd->sector_size_bits - 1));
		off = (loc << 1) & p_bd->sector_size_mask;

		fat_sector = FAT_getblk(sb, sec);
		if (!fat_sector)
			return -1;

		fat_entry = &fat_sector[off];

		SET16_A(fat_entry, content);
	} else if (p_fs->vol_type == FAT32) {
		content &= 0x0FFFFFFF;

		sec = p_fs->FAT1_start_sector + (loc >>
						 (p_bd->sector_size_bits - 2));
		off = (loc << 2) & p_bd->sector_size_mask;

		fat_sector = FAT_getblk(sb, sec);
		if (!fat_sector)
			return -1;

		fat_entry = &fat_sector[off];

		content |= GET32_A(fat_entry) & 0xF0000000;

		SET32_A(fat_entry, content);
	} else { /* p_fs->vol_type == EXFAT */
		sec = p_fs->FAT1_start_sector + (loc >>
						 (p_bd->sector_size_bits - 2));
		off = (loc << 2) & p_bd->sector_size_mask;

		fat_sector = FAT_getblk(sb, sec);
		if (!fat_sector)
			return -1;

		fat_entry = &fat_sector[off];

		SET32_A(fat_entry, content);
	}

	FAT_modify(sb, sec);
	return 0;
}

int FAT_write(struct super_block *sb, u32 loc, u32 content)
{
	s32 ret;

	down(&f_sem);
	ret = __FAT_write(sb, loc, content);
	up(&f_sem);

	return ret;
}

u8 *FAT_getblk(struct super_block *sb, sector_t sec)
{
	struct buf_cache_t *bp;
	struct fs_info_t *p_fs = &(EXFAT_SB(sb)->fs_info);

	bp = FAT_cache_find(sb, sec);
	if (bp) {
		move_to_mru(bp, &p_fs->FAT_cache_lru_list);
		return bp->buf_bh->b_data;
	}

	bp = FAT_cache_get(sb, sec);

	FAT_cache_remove_hash(bp);

	bp->drv = p_fs->drv;
	bp->sec = sec;
	bp->flag = 0;

	FAT_cache_insert_hash(sb, bp);

	if (sector_read(sb, sec, &bp->buf_bh, 1) != FFS_SUCCESS) {
		FAT_cache_remove_hash(bp);
		bp->drv = -1;
		bp->sec = ~0;
		bp->flag = 0;
		bp->buf_bh = NULL;

		move_to_lru(bp, &p_fs->FAT_cache_lru_list);
		return NULL;
	}

	return bp->buf_bh->b_data;
}

void FAT_modify(struct super_block *sb, sector_t sec)
{
	struct buf_cache_t *bp;

	bp = FAT_cache_find(sb, sec);
	if (bp)
		sector_write(sb, sec, bp->buf_bh, 0);
}

void FAT_release_all(struct super_block *sb)
{
	struct buf_cache_t *bp;
	struct fs_info_t *p_fs = &(EXFAT_SB(sb)->fs_info);

	down(&f_sem);

	bp = p_fs->FAT_cache_lru_list.next;
	while (bp != &p_fs->FAT_cache_lru_list) {
		if (bp->drv == p_fs->drv) {
			bp->drv = -1;
			bp->sec = ~0;
			bp->flag = 0;

			if (bp->buf_bh) {
				__brelse(bp->buf_bh);
				bp->buf_bh = NULL;
			}
		}
		bp = bp->next;
	}

	up(&f_sem);
}

void FAT_sync(struct super_block *sb)
{
	struct buf_cache_t *bp;
	struct fs_info_t *p_fs = &(EXFAT_SB(sb)->fs_info);

	down(&f_sem);

	bp = p_fs->FAT_cache_lru_list.next;
	while (bp != &p_fs->FAT_cache_lru_list) {
		if ((bp->drv == p_fs->drv) && (bp->flag & DIRTYBIT)) {
			sync_dirty_buffer(bp->buf_bh);
			bp->flag &= ~(DIRTYBIT);
		}
		bp = bp->next;
	}

	up(&f_sem);
}

static struct buf_cache_t *buf_cache_find(struct super_block *sb, sector_t sec)
{
	s32 off;
	struct buf_cache_t *bp, *hp;
	struct fs_info_t *p_fs = &(EXFAT_SB(sb)->fs_info);

	off = (sec + (sec >> p_fs->sectors_per_clu_bits)) &
		(BUF_CACHE_HASH_SIZE - 1);

	hp = &p_fs->buf_cache_hash_list[off];
	for (bp = hp->hash_next; bp != hp; bp = bp->hash_next) {
		if ((bp->drv == p_fs->drv) && (bp->sec == sec)) {
			touch_buffer(bp->buf_bh);
			return bp;
		}
	}
	return NULL;
}

static struct buf_cache_t *buf_cache_get(struct super_block *sb, sector_t sec)
{
	struct buf_cache_t *bp;
	struct fs_info_t *p_fs = &(EXFAT_SB(sb)->fs_info);

	bp = p_fs->buf_cache_lru_list.prev;
	while (bp->flag & LOCKBIT)
		bp = bp->prev;

	move_to_mru(bp, &p_fs->buf_cache_lru_list);
	return bp;
}

static u8 *__buf_getblk(struct super_block *sb, sector_t sec)
{
	struct buf_cache_t *bp;
	struct fs_info_t *p_fs = &(EXFAT_SB(sb)->fs_info);

	bp = buf_cache_find(sb, sec);
	if (bp) {
		move_to_mru(bp, &p_fs->buf_cache_lru_list);
		return bp->buf_bh->b_data;
	}

	bp = buf_cache_get(sb, sec);

	buf_cache_remove_hash(bp);

	bp->drv = p_fs->drv;
	bp->sec = sec;
	bp->flag = 0;

	buf_cache_insert_hash(sb, bp);

	if (sector_read(sb, sec, &bp->buf_bh, 1) != FFS_SUCCESS) {
		buf_cache_remove_hash(bp);
		bp->drv = -1;
		bp->sec = ~0;
		bp->flag = 0;
		bp->buf_bh = NULL;

		move_to_lru(bp, &p_fs->buf_cache_lru_list);
		return NULL;
	}

	return bp->buf_bh->b_data;
}

u8 *buf_getblk(struct super_block *sb, sector_t sec)
{
	u8 *buf;

	down(&b_sem);
	buf = __buf_getblk(sb, sec);
	up(&b_sem);

	return buf;
}

void buf_modify(struct super_block *sb, sector_t sec)
{
	struct buf_cache_t *bp;

	down(&b_sem);

	bp = buf_cache_find(sb, sec);
	if (likely(bp))
		sector_write(sb, sec, bp->buf_bh, 0);

	WARN(!bp, "[EXFAT] failed to find buffer_cache(sector:%llu).\n",
	     (unsigned long long)sec);

	up(&b_sem);
}

void buf_lock(struct super_block *sb, sector_t sec)
{
	struct buf_cache_t *bp;

	down(&b_sem);

	bp = buf_cache_find(sb, sec);
	if (likely(bp))
		bp->flag |= LOCKBIT;

	WARN(!bp, "[EXFAT] failed to find buffer_cache(sector:%llu).\n",
	     (unsigned long long)sec);

	up(&b_sem);
}

void buf_unlock(struct super_block *sb, sector_t sec)
{
	struct buf_cache_t *bp;

	down(&b_sem);

	bp = buf_cache_find(sb, sec);
	if (likely(bp))
		bp->flag &= ~(LOCKBIT);

	WARN(!bp, "[EXFAT] failed to find buffer_cache(sector:%llu).\n",
	     (unsigned long long)sec);

	up(&b_sem);
}

void buf_release(struct super_block *sb, sector_t sec)
{
	struct buf_cache_t *bp;
	struct fs_info_t *p_fs = &(EXFAT_SB(sb)->fs_info);

	down(&b_sem);

	bp = buf_cache_find(sb, sec);
	if (likely(bp)) {
		bp->drv = -1;
		bp->sec = ~0;
		bp->flag = 0;

		if (bp->buf_bh) {
			__brelse(bp->buf_bh);
			bp->buf_bh = NULL;
		}

		move_to_lru(bp, &p_fs->buf_cache_lru_list);
	}

	up(&b_sem);
}

void buf_release_all(struct super_block *sb)
{
	struct buf_cache_t *bp;
	struct fs_info_t *p_fs = &(EXFAT_SB(sb)->fs_info);

	down(&b_sem);

	bp = p_fs->buf_cache_lru_list.next;
	while (bp != &p_fs->buf_cache_lru_list) {
		if (bp->drv == p_fs->drv) {
			bp->drv = -1;
			bp->sec = ~0;
			bp->flag = 0;

			if (bp->buf_bh) {
				__brelse(bp->buf_bh);
				bp->buf_bh = NULL;
			}
		}
		bp = bp->next;
	}

	up(&b_sem);
}

void buf_sync(struct super_block *sb)
{
	struct buf_cache_t *bp;
	struct fs_info_t *p_fs = &(EXFAT_SB(sb)->fs_info);

	down(&b_sem);

	bp = p_fs->buf_cache_lru_list.next;
	while (bp != &p_fs->buf_cache_lru_list) {
		if ((bp->drv == p_fs->drv) && (bp->flag & DIRTYBIT)) {
			sync_dirty_buffer(bp->buf_bh);
			bp->flag &= ~(DIRTYBIT);
		}
		bp = bp->next;
	}

	up(&b_sem);
}

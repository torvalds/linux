// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Copyright (C) 2012-2013 Samsung Electronics Co., Ltd.
 */

#include <linux/buffer_head.h>
#include <linux/fs.h>
#include <linux/mutex.h>
#include "exfat.h"

/* Local variables */
static DEFINE_MUTEX(f_mutex);
static DEFINE_MUTEX(b_mutex);

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

void exfat_buf_init(struct super_block *sb)
{
	struct fs_info_t *p_fs = &(EXFAT_SB(sb)->fs_info);

	int i;

	/* LRU list */
	p_fs->FAT_cache_lru_list.next = &p_fs->FAT_cache_lru_list;
	p_fs->FAT_cache_lru_list.prev = &p_fs->FAT_cache_lru_list;

	for (i = 0; i < FAT_CACHE_SIZE; i++) {
		p_fs->FAT_cache_array[i].drv = -1;
		p_fs->FAT_cache_array[i].sec = ~0;
		p_fs->FAT_cache_array[i].locked = false;
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
		p_fs->buf_cache_array[i].locked = false;
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

void exfat_buf_shutdown(struct super_block *sb)
{
}

static int __exfat_fat_read(struct super_block *sb, u32 loc, u32 *content)
{
	s32 off;
	u32 _content;
	sector_t sec;
	u8 *fat_sector, *fat_entry;
	struct fs_info_t *p_fs = &(EXFAT_SB(sb)->fs_info);
	struct bd_info_t *p_bd = &(EXFAT_SB(sb)->bd_info);

	sec = p_fs->FAT1_start_sector +
		(loc >> (p_bd->sector_size_bits - 2));
	off = (loc << 2) & p_bd->sector_size_mask;

	fat_sector = exfat_fat_getblk(sb, sec);
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

/* in : sb, loc
 * out: content
 * returns 0 on success
 *            -1 on error
 */
int exfat_fat_read(struct super_block *sb, u32 loc, u32 *content)
{
	s32 ret;

	mutex_lock(&f_mutex);
	ret = __exfat_fat_read(sb, loc, content);
	mutex_unlock(&f_mutex);

	return ret;
}

static s32 __exfat_fat_write(struct super_block *sb, u32 loc, u32 content)
{
	s32 off;
	sector_t sec;
	u8 *fat_sector, *fat_entry;
	struct fs_info_t *p_fs = &(EXFAT_SB(sb)->fs_info);
	struct bd_info_t *p_bd = &(EXFAT_SB(sb)->bd_info);

	sec = p_fs->FAT1_start_sector + (loc >>
					 (p_bd->sector_size_bits - 2));
	off = (loc << 2) & p_bd->sector_size_mask;

	fat_sector = exfat_fat_getblk(sb, sec);
	if (!fat_sector)
		return -1;

	fat_entry = &fat_sector[off];

	SET32_A(fat_entry, content);

	exfat_fat_modify(sb, sec);
	return 0;
}

int exfat_fat_write(struct super_block *sb, u32 loc, u32 content)
{
	s32 ret;

	mutex_lock(&f_mutex);
	ret = __exfat_fat_write(sb, loc, content);
	mutex_unlock(&f_mutex);

	return ret;
}

u8 *exfat_fat_getblk(struct super_block *sb, sector_t sec)
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
	bp->locked = false;

	FAT_cache_insert_hash(sb, bp);

	if (sector_read(sb, sec, &bp->buf_bh, 1) != 0) {
		FAT_cache_remove_hash(bp);
		bp->drv = -1;
		bp->sec = ~0;
		bp->locked = false;
		bp->buf_bh = NULL;

		move_to_lru(bp, &p_fs->FAT_cache_lru_list);
		return NULL;
	}

	return bp->buf_bh->b_data;
}

void exfat_fat_modify(struct super_block *sb, sector_t sec)
{
	struct buf_cache_t *bp;

	bp = FAT_cache_find(sb, sec);
	if (bp)
		sector_write(sb, sec, bp->buf_bh, 0);
}

void exfat_fat_release_all(struct super_block *sb)
{
	struct buf_cache_t *bp;
	struct fs_info_t *p_fs = &(EXFAT_SB(sb)->fs_info);

	mutex_lock(&f_mutex);

	bp = p_fs->FAT_cache_lru_list.next;
	while (bp != &p_fs->FAT_cache_lru_list) {
		if (bp->drv == p_fs->drv) {
			bp->drv = -1;
			bp->sec = ~0;
			bp->locked = false;

			if (bp->buf_bh) {
				__brelse(bp->buf_bh);
				bp->buf_bh = NULL;
			}
		}
		bp = bp->next;
	}

	mutex_unlock(&f_mutex);
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
	while (bp->locked)
		bp = bp->prev;

	move_to_mru(bp, &p_fs->buf_cache_lru_list);
	return bp;
}

static u8 *__exfat_buf_getblk(struct super_block *sb, sector_t sec)
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
	bp->locked = false;

	buf_cache_insert_hash(sb, bp);

	if (sector_read(sb, sec, &bp->buf_bh, 1) != 0) {
		buf_cache_remove_hash(bp);
		bp->drv = -1;
		bp->sec = ~0;
		bp->locked = false;
		bp->buf_bh = NULL;

		move_to_lru(bp, &p_fs->buf_cache_lru_list);
		return NULL;
	}

	return bp->buf_bh->b_data;
}

u8 *exfat_buf_getblk(struct super_block *sb, sector_t sec)
{
	u8 *buf;

	mutex_lock(&b_mutex);
	buf = __exfat_buf_getblk(sb, sec);
	mutex_unlock(&b_mutex);

	return buf;
}

void exfat_buf_modify(struct super_block *sb, sector_t sec)
{
	struct buf_cache_t *bp;

	mutex_lock(&b_mutex);

	bp = buf_cache_find(sb, sec);
	if (likely(bp))
		sector_write(sb, sec, bp->buf_bh, 0);

	WARN(!bp, "[EXFAT] failed to find buffer_cache(sector:%llu).\n",
	     (unsigned long long)sec);

	mutex_unlock(&b_mutex);
}

void exfat_buf_lock(struct super_block *sb, sector_t sec)
{
	struct buf_cache_t *bp;

	mutex_lock(&b_mutex);

	bp = buf_cache_find(sb, sec);
	if (likely(bp))
		bp->locked = true;

	WARN(!bp, "[EXFAT] failed to find buffer_cache(sector:%llu).\n",
	     (unsigned long long)sec);

	mutex_unlock(&b_mutex);
}

void exfat_buf_unlock(struct super_block *sb, sector_t sec)
{
	struct buf_cache_t *bp;

	mutex_lock(&b_mutex);

	bp = buf_cache_find(sb, sec);
	if (likely(bp))
		bp->locked = false;

	WARN(!bp, "[EXFAT] failed to find buffer_cache(sector:%llu).\n",
	     (unsigned long long)sec);

	mutex_unlock(&b_mutex);
}

void exfat_buf_release(struct super_block *sb, sector_t sec)
{
	struct buf_cache_t *bp;
	struct fs_info_t *p_fs = &(EXFAT_SB(sb)->fs_info);

	mutex_lock(&b_mutex);

	bp = buf_cache_find(sb, sec);
	if (likely(bp)) {
		bp->drv = -1;
		bp->sec = ~0;
		bp->locked = false;

		if (bp->buf_bh) {
			__brelse(bp->buf_bh);
			bp->buf_bh = NULL;
		}

		move_to_lru(bp, &p_fs->buf_cache_lru_list);
	}

	mutex_unlock(&b_mutex);
}

void exfat_buf_release_all(struct super_block *sb)
{
	struct buf_cache_t *bp;
	struct fs_info_t *p_fs = &(EXFAT_SB(sb)->fs_info);

	mutex_lock(&b_mutex);

	bp = p_fs->buf_cache_lru_list.next;
	while (bp != &p_fs->buf_cache_lru_list) {
		if (bp->drv == p_fs->drv) {
			bp->drv = -1;
			bp->sec = ~0;
			bp->locked = false;

			if (bp->buf_bh) {
				__brelse(bp->buf_bh);
				bp->buf_bh = NULL;
			}
		}
		bp = bp->next;
	}

	mutex_unlock(&b_mutex);
}

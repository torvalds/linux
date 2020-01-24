// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Copyright (C) 2012-2013 Samsung Electronics Co., Ltd.
 */

#include <linux/blkdev.h>
#include <linux/buffer_head.h>
#include <linux/fs.h>
#include "exfat.h"

void exfat_bdev_open(struct super_block *sb)
{
	struct bd_info_t *p_bd = &(EXFAT_SB(sb)->bd_info);

	if (p_bd->opened)
		return;

	p_bd->sector_size      = bdev_logical_block_size(sb->s_bdev);
	p_bd->sector_size_bits = ilog2(p_bd->sector_size);
	p_bd->sector_size_mask = p_bd->sector_size - 1;
	p_bd->num_sectors      = i_size_read(sb->s_bdev->bd_inode) >>
				 p_bd->sector_size_bits;
	p_bd->opened = true;
}

void exfat_bdev_close(struct super_block *sb)
{
	struct bd_info_t *p_bd = &(EXFAT_SB(sb)->bd_info);

	p_bd->opened = false;
}

int exfat_bdev_read(struct super_block *sb, sector_t secno, struct buffer_head **bh,
	      u32 num_secs, bool read)
{
	struct bd_info_t *p_bd = &(EXFAT_SB(sb)->bd_info);
	struct fs_info_t *p_fs = &(EXFAT_SB(sb)->fs_info);
#ifdef CONFIG_EXFAT_KERNEL_DEBUG
	struct exfat_sb_info *sbi = EXFAT_SB(sb);
	long flags = sbi->debug_flags;

	if (flags & EXFAT_DEBUGFLAGS_ERROR_RW)
		return -EIO;
#endif /* CONFIG_EXFAT_KERNEL_DEBUG */

	if (!p_bd->opened)
		return -ENODEV;

	if (*bh)
		__brelse(*bh);

	if (read)
		*bh = __bread(sb->s_bdev, secno,
			      num_secs << p_bd->sector_size_bits);
	else
		*bh = __getblk(sb->s_bdev, secno,
			       num_secs << p_bd->sector_size_bits);

	if (*bh)
		return 0;

	WARN(!p_fs->dev_ejected,
	     "[EXFAT] No bh, device seems wrong or to be ejected.\n");

	return -EIO;
}

int exfat_bdev_write(struct super_block *sb, sector_t secno, struct buffer_head *bh,
	       u32 num_secs, bool sync)
{
	s32 count;
	struct buffer_head *bh2;
	struct bd_info_t *p_bd = &(EXFAT_SB(sb)->bd_info);
	struct fs_info_t *p_fs = &(EXFAT_SB(sb)->fs_info);
#ifdef CONFIG_EXFAT_KERNEL_DEBUG
	struct exfat_sb_info *sbi = EXFAT_SB(sb);
	long flags = sbi->debug_flags;

	if (flags & EXFAT_DEBUGFLAGS_ERROR_RW)
		return -EIO;
#endif /* CONFIG_EXFAT_KERNEL_DEBUG */

	if (!p_bd->opened)
		return -ENODEV;

	if (secno == bh->b_blocknr) {
		lock_buffer(bh);
		set_buffer_uptodate(bh);
		mark_buffer_dirty(bh);
		unlock_buffer(bh);
		if (sync && (sync_dirty_buffer(bh) != 0))
			return -EIO;
	} else {
		count = num_secs << p_bd->sector_size_bits;

		bh2 = __getblk(sb->s_bdev, secno, count);
		if (!bh2)
			goto no_bh;

		lock_buffer(bh2);
		memcpy(bh2->b_data, bh->b_data, count);
		set_buffer_uptodate(bh2);
		mark_buffer_dirty(bh2);
		unlock_buffer(bh2);
		if (sync && (sync_dirty_buffer(bh2) != 0)) {
			__brelse(bh2);
			goto no_bh;
		}
		__brelse(bh2);
	}

	return 0;

no_bh:
	WARN(!p_fs->dev_ejected,
	     "[EXFAT] No bh, device seems wrong or to be ejected.\n");

	return -EIO;
}

int exfat_bdev_sync(struct super_block *sb)
{
	struct bd_info_t *p_bd = &(EXFAT_SB(sb)->bd_info);
#ifdef CONFIG_EXFAT_KERNEL_DEBUG
	struct exfat_sb_info *sbi = EXFAT_SB(sb);
	long flags = sbi->debug_flags;

	if (flags & EXFAT_DEBUGFLAGS_ERROR_RW)
		return -EIO;
#endif /* CONFIG_EXFAT_KERNEL_DEBUG */

	if (!p_bd->opened)
		return -ENODEV;

	return sync_blockdev(sb->s_bdev);
}

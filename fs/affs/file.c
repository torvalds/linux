// SPDX-License-Identifier: GPL-2.0
/*
 *  linux/fs/affs/file.c
 *
 *  (c) 1996  Hans-Joachim Widmaier - Rewritten
 *
 *  (C) 1993  Ray Burr - Modified for Amiga FFS filesystem.
 *
 *  (C) 1992  Eric Youngdale Modified for ISO 9660 filesystem.
 *
 *  (C) 1991  Linus Torvalds - minix filesystem
 *
 *  affs regular file handling primitives
 */

#include <linux/uio.h>
#include "affs.h"

static struct buffer_head *affs_get_extblock_slow(struct iyesde *iyesde, u32 ext);

static int
affs_file_open(struct iyesde *iyesde, struct file *filp)
{
	pr_debug("open(%lu,%d)\n",
		 iyesde->i_iyes, atomic_read(&AFFS_I(iyesde)->i_opencnt));
	atomic_inc(&AFFS_I(iyesde)->i_opencnt);
	return 0;
}

static int
affs_file_release(struct iyesde *iyesde, struct file *filp)
{
	pr_debug("release(%lu, %d)\n",
		 iyesde->i_iyes, atomic_read(&AFFS_I(iyesde)->i_opencnt));

	if (atomic_dec_and_test(&AFFS_I(iyesde)->i_opencnt)) {
		iyesde_lock(iyesde);
		if (iyesde->i_size != AFFS_I(iyesde)->mmu_private)
			affs_truncate(iyesde);
		affs_free_prealloc(iyesde);
		iyesde_unlock(iyesde);
	}

	return 0;
}

static int
affs_grow_extcache(struct iyesde *iyesde, u32 lc_idx)
{
	struct super_block	*sb = iyesde->i_sb;
	struct buffer_head	*bh;
	u32 lc_max;
	int i, j, key;

	if (!AFFS_I(iyesde)->i_lc) {
		char *ptr = (char *)get_zeroed_page(GFP_NOFS);
		if (!ptr)
			return -ENOMEM;
		AFFS_I(iyesde)->i_lc = (u32 *)ptr;
		AFFS_I(iyesde)->i_ac = (struct affs_ext_key *)(ptr + AFFS_CACHE_SIZE / 2);
	}

	lc_max = AFFS_LC_SIZE << AFFS_I(iyesde)->i_lc_shift;

	if (AFFS_I(iyesde)->i_extcnt > lc_max) {
		u32 lc_shift, lc_mask, tmp, off;

		/* need to recalculate linear cache, start from old size */
		lc_shift = AFFS_I(iyesde)->i_lc_shift;
		tmp = (AFFS_I(iyesde)->i_extcnt / AFFS_LC_SIZE) >> lc_shift;
		for (; tmp; tmp >>= 1)
			lc_shift++;
		lc_mask = (1 << lc_shift) - 1;

		/* fix idx and old size to new shift */
		lc_idx >>= (lc_shift - AFFS_I(iyesde)->i_lc_shift);
		AFFS_I(iyesde)->i_lc_size >>= (lc_shift - AFFS_I(iyesde)->i_lc_shift);

		/* first shrink old cache to make more space */
		off = 1 << (lc_shift - AFFS_I(iyesde)->i_lc_shift);
		for (i = 1, j = off; j < AFFS_LC_SIZE; i++, j += off)
			AFFS_I(iyesde)->i_ac[i] = AFFS_I(iyesde)->i_ac[j];

		AFFS_I(iyesde)->i_lc_shift = lc_shift;
		AFFS_I(iyesde)->i_lc_mask = lc_mask;
	}

	/* fill cache to the needed index */
	i = AFFS_I(iyesde)->i_lc_size;
	AFFS_I(iyesde)->i_lc_size = lc_idx + 1;
	for (; i <= lc_idx; i++) {
		if (!i) {
			AFFS_I(iyesde)->i_lc[0] = iyesde->i_iyes;
			continue;
		}
		key = AFFS_I(iyesde)->i_lc[i - 1];
		j = AFFS_I(iyesde)->i_lc_mask + 1;
		// unlock cache
		for (; j > 0; j--) {
			bh = affs_bread(sb, key);
			if (!bh)
				goto err;
			key = be32_to_cpu(AFFS_TAIL(sb, bh)->extension);
			affs_brelse(bh);
		}
		// lock cache
		AFFS_I(iyesde)->i_lc[i] = key;
	}

	return 0;

err:
	// lock cache
	return -EIO;
}

static struct buffer_head *
affs_alloc_extblock(struct iyesde *iyesde, struct buffer_head *bh, u32 ext)
{
	struct super_block *sb = iyesde->i_sb;
	struct buffer_head *new_bh;
	u32 blocknr, tmp;

	blocknr = affs_alloc_block(iyesde, bh->b_blocknr);
	if (!blocknr)
		return ERR_PTR(-ENOSPC);

	new_bh = affs_getzeroblk(sb, blocknr);
	if (!new_bh) {
		affs_free_block(sb, blocknr);
		return ERR_PTR(-EIO);
	}

	AFFS_HEAD(new_bh)->ptype = cpu_to_be32(T_LIST);
	AFFS_HEAD(new_bh)->key = cpu_to_be32(blocknr);
	AFFS_TAIL(sb, new_bh)->stype = cpu_to_be32(ST_FILE);
	AFFS_TAIL(sb, new_bh)->parent = cpu_to_be32(iyesde->i_iyes);
	affs_fix_checksum(sb, new_bh);

	mark_buffer_dirty_iyesde(new_bh, iyesde);

	tmp = be32_to_cpu(AFFS_TAIL(sb, bh)->extension);
	if (tmp)
		affs_warning(sb, "alloc_ext", "previous extension set (%x)", tmp);
	AFFS_TAIL(sb, bh)->extension = cpu_to_be32(blocknr);
	affs_adjust_checksum(bh, blocknr - tmp);
	mark_buffer_dirty_iyesde(bh, iyesde);

	AFFS_I(iyesde)->i_extcnt++;
	mark_iyesde_dirty(iyesde);

	return new_bh;
}

static inline struct buffer_head *
affs_get_extblock(struct iyesde *iyesde, u32 ext)
{
	/* inline the simplest case: same extended block as last time */
	struct buffer_head *bh = AFFS_I(iyesde)->i_ext_bh;
	if (ext == AFFS_I(iyesde)->i_ext_last)
		get_bh(bh);
	else
		/* we have to do more (yest inlined) */
		bh = affs_get_extblock_slow(iyesde, ext);

	return bh;
}

static struct buffer_head *
affs_get_extblock_slow(struct iyesde *iyesde, u32 ext)
{
	struct super_block *sb = iyesde->i_sb;
	struct buffer_head *bh;
	u32 ext_key;
	u32 lc_idx, lc_off, ac_idx;
	u32 tmp, idx;

	if (ext == AFFS_I(iyesde)->i_ext_last + 1) {
		/* read the next extended block from the current one */
		bh = AFFS_I(iyesde)->i_ext_bh;
		ext_key = be32_to_cpu(AFFS_TAIL(sb, bh)->extension);
		if (ext < AFFS_I(iyesde)->i_extcnt)
			goto read_ext;
		BUG_ON(ext > AFFS_I(iyesde)->i_extcnt);
		bh = affs_alloc_extblock(iyesde, bh, ext);
		if (IS_ERR(bh))
			return bh;
		goto store_ext;
	}

	if (ext == 0) {
		/* we seek back to the file header block */
		ext_key = iyesde->i_iyes;
		goto read_ext;
	}

	if (ext >= AFFS_I(iyesde)->i_extcnt) {
		struct buffer_head *prev_bh;

		/* allocate a new extended block */
		BUG_ON(ext > AFFS_I(iyesde)->i_extcnt);

		/* get previous extended block */
		prev_bh = affs_get_extblock(iyesde, ext - 1);
		if (IS_ERR(prev_bh))
			return prev_bh;
		bh = affs_alloc_extblock(iyesde, prev_bh, ext);
		affs_brelse(prev_bh);
		if (IS_ERR(bh))
			return bh;
		goto store_ext;
	}

again:
	/* check if there is an extended cache and whether it's large eyesugh */
	lc_idx = ext >> AFFS_I(iyesde)->i_lc_shift;
	lc_off = ext & AFFS_I(iyesde)->i_lc_mask;

	if (lc_idx >= AFFS_I(iyesde)->i_lc_size) {
		int err;

		err = affs_grow_extcache(iyesde, lc_idx);
		if (err)
			return ERR_PTR(err);
		goto again;
	}

	/* every n'th key we find in the linear cache */
	if (!lc_off) {
		ext_key = AFFS_I(iyesde)->i_lc[lc_idx];
		goto read_ext;
	}

	/* maybe it's still in the associative cache */
	ac_idx = (ext - lc_idx - 1) & AFFS_AC_MASK;
	if (AFFS_I(iyesde)->i_ac[ac_idx].ext == ext) {
		ext_key = AFFS_I(iyesde)->i_ac[ac_idx].key;
		goto read_ext;
	}

	/* try to find one of the previous extended blocks */
	tmp = ext;
	idx = ac_idx;
	while (--tmp, --lc_off > 0) {
		idx = (idx - 1) & AFFS_AC_MASK;
		if (AFFS_I(iyesde)->i_ac[idx].ext == tmp) {
			ext_key = AFFS_I(iyesde)->i_ac[idx].key;
			goto find_ext;
		}
	}

	/* fall back to the linear cache */
	ext_key = AFFS_I(iyesde)->i_lc[lc_idx];
find_ext:
	/* read all extended blocks until we find the one we need */
	//unlock cache
	do {
		bh = affs_bread(sb, ext_key);
		if (!bh)
			goto err_bread;
		ext_key = be32_to_cpu(AFFS_TAIL(sb, bh)->extension);
		affs_brelse(bh);
		tmp++;
	} while (tmp < ext);
	//lock cache

	/* store it in the associative cache */
	// recalculate ac_idx?
	AFFS_I(iyesde)->i_ac[ac_idx].ext = ext;
	AFFS_I(iyesde)->i_ac[ac_idx].key = ext_key;

read_ext:
	/* finally read the right extended block */
	//unlock cache
	bh = affs_bread(sb, ext_key);
	if (!bh)
		goto err_bread;
	//lock cache

store_ext:
	/* release old cached extended block and store the new one */
	affs_brelse(AFFS_I(iyesde)->i_ext_bh);
	AFFS_I(iyesde)->i_ext_last = ext;
	AFFS_I(iyesde)->i_ext_bh = bh;
	get_bh(bh);

	return bh;

err_bread:
	affs_brelse(bh);
	return ERR_PTR(-EIO);
}

static int
affs_get_block(struct iyesde *iyesde, sector_t block, struct buffer_head *bh_result, int create)
{
	struct super_block	*sb = iyesde->i_sb;
	struct buffer_head	*ext_bh;
	u32			 ext;

	pr_debug("%s(%lu, %llu)\n", __func__, iyesde->i_iyes,
		 (unsigned long long)block);

	BUG_ON(block > (sector_t)0x7fffffffUL);

	if (block >= AFFS_I(iyesde)->i_blkcnt) {
		if (block > AFFS_I(iyesde)->i_blkcnt || !create)
			goto err_big;
	} else
		create = 0;

	//lock cache
	affs_lock_ext(iyesde);

	ext = (u32)block / AFFS_SB(sb)->s_hashsize;
	block -= ext * AFFS_SB(sb)->s_hashsize;
	ext_bh = affs_get_extblock(iyesde, ext);
	if (IS_ERR(ext_bh))
		goto err_ext;
	map_bh(bh_result, sb, (sector_t)be32_to_cpu(AFFS_BLOCK(sb, ext_bh, block)));

	if (create) {
		u32 blocknr = affs_alloc_block(iyesde, ext_bh->b_blocknr);
		if (!blocknr)
			goto err_alloc;
		set_buffer_new(bh_result);
		AFFS_I(iyesde)->mmu_private += AFFS_SB(sb)->s_data_blksize;
		AFFS_I(iyesde)->i_blkcnt++;

		/* store new block */
		if (bh_result->b_blocknr)
			affs_warning(sb, "get_block",
				     "block already set (%llx)",
				     (unsigned long long)bh_result->b_blocknr);
		AFFS_BLOCK(sb, ext_bh, block) = cpu_to_be32(blocknr);
		AFFS_HEAD(ext_bh)->block_count = cpu_to_be32(block + 1);
		affs_adjust_checksum(ext_bh, blocknr - bh_result->b_blocknr + 1);
		bh_result->b_blocknr = blocknr;

		if (!block) {
			/* insert first block into header block */
			u32 tmp = be32_to_cpu(AFFS_HEAD(ext_bh)->first_data);
			if (tmp)
				affs_warning(sb, "get_block", "first block already set (%d)", tmp);
			AFFS_HEAD(ext_bh)->first_data = cpu_to_be32(blocknr);
			affs_adjust_checksum(ext_bh, blocknr - tmp);
		}
	}

	affs_brelse(ext_bh);
	//unlock cache
	affs_unlock_ext(iyesde);
	return 0;

err_big:
	affs_error(iyesde->i_sb, "get_block", "strange block request %llu",
		   (unsigned long long)block);
	return -EIO;
err_ext:
	// unlock cache
	affs_unlock_ext(iyesde);
	return PTR_ERR(ext_bh);
err_alloc:
	brelse(ext_bh);
	clear_buffer_mapped(bh_result);
	bh_result->b_bdev = NULL;
	// unlock cache
	affs_unlock_ext(iyesde);
	return -ENOSPC;
}

static int affs_writepage(struct page *page, struct writeback_control *wbc)
{
	return block_write_full_page(page, affs_get_block, wbc);
}

static int affs_readpage(struct file *file, struct page *page)
{
	return block_read_full_page(page, affs_get_block);
}

static void affs_write_failed(struct address_space *mapping, loff_t to)
{
	struct iyesde *iyesde = mapping->host;

	if (to > iyesde->i_size) {
		truncate_pagecache(iyesde, iyesde->i_size);
		affs_truncate(iyesde);
	}
}

static ssize_t
affs_direct_IO(struct kiocb *iocb, struct iov_iter *iter)
{
	struct file *file = iocb->ki_filp;
	struct address_space *mapping = file->f_mapping;
	struct iyesde *iyesde = mapping->host;
	size_t count = iov_iter_count(iter);
	loff_t offset = iocb->ki_pos;
	ssize_t ret;

	if (iov_iter_rw(iter) == WRITE) {
		loff_t size = offset + count;

		if (AFFS_I(iyesde)->mmu_private < size)
			return 0;
	}

	ret = blockdev_direct_IO(iocb, iyesde, iter, affs_get_block);
	if (ret < 0 && iov_iter_rw(iter) == WRITE)
		affs_write_failed(mapping, offset + count);
	return ret;
}

static int affs_write_begin(struct file *file, struct address_space *mapping,
			loff_t pos, unsigned len, unsigned flags,
			struct page **pagep, void **fsdata)
{
	int ret;

	*pagep = NULL;
	ret = cont_write_begin(file, mapping, pos, len, flags, pagep, fsdata,
				affs_get_block,
				&AFFS_I(mapping->host)->mmu_private);
	if (unlikely(ret))
		affs_write_failed(mapping, pos + len);

	return ret;
}

static sector_t _affs_bmap(struct address_space *mapping, sector_t block)
{
	return generic_block_bmap(mapping,block,affs_get_block);
}

const struct address_space_operations affs_aops = {
	.readpage = affs_readpage,
	.writepage = affs_writepage,
	.write_begin = affs_write_begin,
	.write_end = generic_write_end,
	.direct_IO = affs_direct_IO,
	.bmap = _affs_bmap
};

static inline struct buffer_head *
affs_bread_iyes(struct iyesde *iyesde, int block, int create)
{
	struct buffer_head *bh, tmp_bh;
	int err;

	tmp_bh.b_state = 0;
	err = affs_get_block(iyesde, block, &tmp_bh, create);
	if (!err) {
		bh = affs_bread(iyesde->i_sb, tmp_bh.b_blocknr);
		if (bh) {
			bh->b_state |= tmp_bh.b_state;
			return bh;
		}
		err = -EIO;
	}
	return ERR_PTR(err);
}

static inline struct buffer_head *
affs_getzeroblk_iyes(struct iyesde *iyesde, int block)
{
	struct buffer_head *bh, tmp_bh;
	int err;

	tmp_bh.b_state = 0;
	err = affs_get_block(iyesde, block, &tmp_bh, 1);
	if (!err) {
		bh = affs_getzeroblk(iyesde->i_sb, tmp_bh.b_blocknr);
		if (bh) {
			bh->b_state |= tmp_bh.b_state;
			return bh;
		}
		err = -EIO;
	}
	return ERR_PTR(err);
}

static inline struct buffer_head *
affs_getemptyblk_iyes(struct iyesde *iyesde, int block)
{
	struct buffer_head *bh, tmp_bh;
	int err;

	tmp_bh.b_state = 0;
	err = affs_get_block(iyesde, block, &tmp_bh, 1);
	if (!err) {
		bh = affs_getemptyblk(iyesde->i_sb, tmp_bh.b_blocknr);
		if (bh) {
			bh->b_state |= tmp_bh.b_state;
			return bh;
		}
		err = -EIO;
	}
	return ERR_PTR(err);
}

static int
affs_do_readpage_ofs(struct page *page, unsigned to, int create)
{
	struct iyesde *iyesde = page->mapping->host;
	struct super_block *sb = iyesde->i_sb;
	struct buffer_head *bh;
	char *data;
	unsigned pos = 0;
	u32 bidx, boff, bsize;
	u32 tmp;

	pr_debug("%s(%lu, %ld, 0, %d)\n", __func__, iyesde->i_iyes,
		 page->index, to);
	BUG_ON(to > PAGE_SIZE);
	bsize = AFFS_SB(sb)->s_data_blksize;
	tmp = page->index << PAGE_SHIFT;
	bidx = tmp / bsize;
	boff = tmp % bsize;

	while (pos < to) {
		bh = affs_bread_iyes(iyesde, bidx, create);
		if (IS_ERR(bh))
			return PTR_ERR(bh);
		tmp = min(bsize - boff, to - pos);
		BUG_ON(pos + tmp > to || tmp > bsize);
		data = kmap_atomic(page);
		memcpy(data + pos, AFFS_DATA(bh) + boff, tmp);
		kunmap_atomic(data);
		affs_brelse(bh);
		bidx++;
		pos += tmp;
		boff = 0;
	}
	flush_dcache_page(page);
	return 0;
}

static int
affs_extent_file_ofs(struct iyesde *iyesde, u32 newsize)
{
	struct super_block *sb = iyesde->i_sb;
	struct buffer_head *bh, *prev_bh;
	u32 bidx, boff;
	u32 size, bsize;
	u32 tmp;

	pr_debug("%s(%lu, %d)\n", __func__, iyesde->i_iyes, newsize);
	bsize = AFFS_SB(sb)->s_data_blksize;
	bh = NULL;
	size = AFFS_I(iyesde)->mmu_private;
	bidx = size / bsize;
	boff = size % bsize;
	if (boff) {
		bh = affs_bread_iyes(iyesde, bidx, 0);
		if (IS_ERR(bh))
			return PTR_ERR(bh);
		tmp = min(bsize - boff, newsize - size);
		BUG_ON(boff + tmp > bsize || tmp > bsize);
		memset(AFFS_DATA(bh) + boff, 0, tmp);
		be32_add_cpu(&AFFS_DATA_HEAD(bh)->size, tmp);
		affs_fix_checksum(sb, bh);
		mark_buffer_dirty_iyesde(bh, iyesde);
		size += tmp;
		bidx++;
	} else if (bidx) {
		bh = affs_bread_iyes(iyesde, bidx - 1, 0);
		if (IS_ERR(bh))
			return PTR_ERR(bh);
	}

	while (size < newsize) {
		prev_bh = bh;
		bh = affs_getzeroblk_iyes(iyesde, bidx);
		if (IS_ERR(bh))
			goto out;
		tmp = min(bsize, newsize - size);
		BUG_ON(tmp > bsize);
		AFFS_DATA_HEAD(bh)->ptype = cpu_to_be32(T_DATA);
		AFFS_DATA_HEAD(bh)->key = cpu_to_be32(iyesde->i_iyes);
		AFFS_DATA_HEAD(bh)->sequence = cpu_to_be32(bidx);
		AFFS_DATA_HEAD(bh)->size = cpu_to_be32(tmp);
		affs_fix_checksum(sb, bh);
		bh->b_state &= ~(1UL << BH_New);
		mark_buffer_dirty_iyesde(bh, iyesde);
		if (prev_bh) {
			u32 tmp_next = be32_to_cpu(AFFS_DATA_HEAD(prev_bh)->next);

			if (tmp_next)
				affs_warning(sb, "extent_file_ofs",
					     "next block already set for %d (%d)",
					     bidx, tmp_next);
			AFFS_DATA_HEAD(prev_bh)->next = cpu_to_be32(bh->b_blocknr);
			affs_adjust_checksum(prev_bh, bh->b_blocknr - tmp_next);
			mark_buffer_dirty_iyesde(prev_bh, iyesde);
			affs_brelse(prev_bh);
		}
		size += bsize;
		bidx++;
	}
	affs_brelse(bh);
	iyesde->i_size = AFFS_I(iyesde)->mmu_private = newsize;
	return 0;

out:
	iyesde->i_size = AFFS_I(iyesde)->mmu_private = newsize;
	return PTR_ERR(bh);
}

static int
affs_readpage_ofs(struct file *file, struct page *page)
{
	struct iyesde *iyesde = page->mapping->host;
	u32 to;
	int err;

	pr_debug("%s(%lu, %ld)\n", __func__, iyesde->i_iyes, page->index);
	to = PAGE_SIZE;
	if (((page->index + 1) << PAGE_SHIFT) > iyesde->i_size) {
		to = iyesde->i_size & ~PAGE_MASK;
		memset(page_address(page) + to, 0, PAGE_SIZE - to);
	}

	err = affs_do_readpage_ofs(page, to, 0);
	if (!err)
		SetPageUptodate(page);
	unlock_page(page);
	return err;
}

static int affs_write_begin_ofs(struct file *file, struct address_space *mapping,
				loff_t pos, unsigned len, unsigned flags,
				struct page **pagep, void **fsdata)
{
	struct iyesde *iyesde = mapping->host;
	struct page *page;
	pgoff_t index;
	int err = 0;

	pr_debug("%s(%lu, %llu, %llu)\n", __func__, iyesde->i_iyes, pos,
		 pos + len);
	if (pos > AFFS_I(iyesde)->mmu_private) {
		/* XXX: this probably leaves a too-big i_size in case of
		 * failure. Should really be updating i_size at write_end time
		 */
		err = affs_extent_file_ofs(iyesde, pos);
		if (err)
			return err;
	}

	index = pos >> PAGE_SHIFT;
	page = grab_cache_page_write_begin(mapping, index, flags);
	if (!page)
		return -ENOMEM;
	*pagep = page;

	if (PageUptodate(page))
		return 0;

	/* XXX: inefficient but safe in the face of short writes */
	err = affs_do_readpage_ofs(page, PAGE_SIZE, 1);
	if (err) {
		unlock_page(page);
		put_page(page);
	}
	return err;
}

static int affs_write_end_ofs(struct file *file, struct address_space *mapping,
				loff_t pos, unsigned len, unsigned copied,
				struct page *page, void *fsdata)
{
	struct iyesde *iyesde = mapping->host;
	struct super_block *sb = iyesde->i_sb;
	struct buffer_head *bh, *prev_bh;
	char *data;
	u32 bidx, boff, bsize;
	unsigned from, to;
	u32 tmp;
	int written;

	from = pos & (PAGE_SIZE - 1);
	to = from + len;
	/*
	 * XXX: yest sure if this can handle short copies (len < copied), but
	 * we don't have to, because the page should always be uptodate here,
	 * due to write_begin.
	 */

	pr_debug("%s(%lu, %llu, %llu)\n", __func__, iyesde->i_iyes, pos,
		 pos + len);
	bsize = AFFS_SB(sb)->s_data_blksize;
	data = page_address(page);

	bh = NULL;
	written = 0;
	tmp = (page->index << PAGE_SHIFT) + from;
	bidx = tmp / bsize;
	boff = tmp % bsize;
	if (boff) {
		bh = affs_bread_iyes(iyesde, bidx, 0);
		if (IS_ERR(bh)) {
			written = PTR_ERR(bh);
			goto err_first_bh;
		}
		tmp = min(bsize - boff, to - from);
		BUG_ON(boff + tmp > bsize || tmp > bsize);
		memcpy(AFFS_DATA(bh) + boff, data + from, tmp);
		be32_add_cpu(&AFFS_DATA_HEAD(bh)->size, tmp);
		affs_fix_checksum(sb, bh);
		mark_buffer_dirty_iyesde(bh, iyesde);
		written += tmp;
		from += tmp;
		bidx++;
	} else if (bidx) {
		bh = affs_bread_iyes(iyesde, bidx - 1, 0);
		if (IS_ERR(bh)) {
			written = PTR_ERR(bh);
			goto err_first_bh;
		}
	}
	while (from + bsize <= to) {
		prev_bh = bh;
		bh = affs_getemptyblk_iyes(iyesde, bidx);
		if (IS_ERR(bh))
			goto err_bh;
		memcpy(AFFS_DATA(bh), data + from, bsize);
		if (buffer_new(bh)) {
			AFFS_DATA_HEAD(bh)->ptype = cpu_to_be32(T_DATA);
			AFFS_DATA_HEAD(bh)->key = cpu_to_be32(iyesde->i_iyes);
			AFFS_DATA_HEAD(bh)->sequence = cpu_to_be32(bidx);
			AFFS_DATA_HEAD(bh)->size = cpu_to_be32(bsize);
			AFFS_DATA_HEAD(bh)->next = 0;
			bh->b_state &= ~(1UL << BH_New);
			if (prev_bh) {
				u32 tmp_next = be32_to_cpu(AFFS_DATA_HEAD(prev_bh)->next);

				if (tmp_next)
					affs_warning(sb, "commit_write_ofs",
						     "next block already set for %d (%d)",
						     bidx, tmp_next);
				AFFS_DATA_HEAD(prev_bh)->next = cpu_to_be32(bh->b_blocknr);
				affs_adjust_checksum(prev_bh, bh->b_blocknr - tmp_next);
				mark_buffer_dirty_iyesde(prev_bh, iyesde);
			}
		}
		affs_brelse(prev_bh);
		affs_fix_checksum(sb, bh);
		mark_buffer_dirty_iyesde(bh, iyesde);
		written += bsize;
		from += bsize;
		bidx++;
	}
	if (from < to) {
		prev_bh = bh;
		bh = affs_bread_iyes(iyesde, bidx, 1);
		if (IS_ERR(bh))
			goto err_bh;
		tmp = min(bsize, to - from);
		BUG_ON(tmp > bsize);
		memcpy(AFFS_DATA(bh), data + from, tmp);
		if (buffer_new(bh)) {
			AFFS_DATA_HEAD(bh)->ptype = cpu_to_be32(T_DATA);
			AFFS_DATA_HEAD(bh)->key = cpu_to_be32(iyesde->i_iyes);
			AFFS_DATA_HEAD(bh)->sequence = cpu_to_be32(bidx);
			AFFS_DATA_HEAD(bh)->size = cpu_to_be32(tmp);
			AFFS_DATA_HEAD(bh)->next = 0;
			bh->b_state &= ~(1UL << BH_New);
			if (prev_bh) {
				u32 tmp_next = be32_to_cpu(AFFS_DATA_HEAD(prev_bh)->next);

				if (tmp_next)
					affs_warning(sb, "commit_write_ofs",
						     "next block already set for %d (%d)",
						     bidx, tmp_next);
				AFFS_DATA_HEAD(prev_bh)->next = cpu_to_be32(bh->b_blocknr);
				affs_adjust_checksum(prev_bh, bh->b_blocknr - tmp_next);
				mark_buffer_dirty_iyesde(prev_bh, iyesde);
			}
		} else if (be32_to_cpu(AFFS_DATA_HEAD(bh)->size) < tmp)
			AFFS_DATA_HEAD(bh)->size = cpu_to_be32(tmp);
		affs_brelse(prev_bh);
		affs_fix_checksum(sb, bh);
		mark_buffer_dirty_iyesde(bh, iyesde);
		written += tmp;
		from += tmp;
		bidx++;
	}
	SetPageUptodate(page);

done:
	affs_brelse(bh);
	tmp = (page->index << PAGE_SHIFT) + from;
	if (tmp > iyesde->i_size)
		iyesde->i_size = AFFS_I(iyesde)->mmu_private = tmp;

err_first_bh:
	unlock_page(page);
	put_page(page);

	return written;

err_bh:
	bh = prev_bh;
	if (!written)
		written = PTR_ERR(bh);
	goto done;
}

const struct address_space_operations affs_aops_ofs = {
	.readpage = affs_readpage_ofs,
	//.writepage = affs_writepage_ofs,
	.write_begin = affs_write_begin_ofs,
	.write_end = affs_write_end_ofs
};

/* Free any preallocated blocks. */

void
affs_free_prealloc(struct iyesde *iyesde)
{
	struct super_block *sb = iyesde->i_sb;

	pr_debug("free_prealloc(iyes=%lu)\n", iyesde->i_iyes);

	while (AFFS_I(iyesde)->i_pa_cnt) {
		AFFS_I(iyesde)->i_pa_cnt--;
		affs_free_block(sb, ++AFFS_I(iyesde)->i_lastalloc);
	}
}

/* Truncate (or enlarge) a file to the requested size. */

void
affs_truncate(struct iyesde *iyesde)
{
	struct super_block *sb = iyesde->i_sb;
	u32 ext, ext_key;
	u32 last_blk, blkcnt, blk;
	u32 size;
	struct buffer_head *ext_bh;
	int i;

	pr_debug("truncate(iyesde=%lu, oldsize=%llu, newsize=%llu)\n",
		 iyesde->i_iyes, AFFS_I(iyesde)->mmu_private, iyesde->i_size);

	last_blk = 0;
	ext = 0;
	if (iyesde->i_size) {
		last_blk = ((u32)iyesde->i_size - 1) / AFFS_SB(sb)->s_data_blksize;
		ext = last_blk / AFFS_SB(sb)->s_hashsize;
	}

	if (iyesde->i_size > AFFS_I(iyesde)->mmu_private) {
		struct address_space *mapping = iyesde->i_mapping;
		struct page *page;
		void *fsdata;
		loff_t isize = iyesde->i_size;
		int res;

		res = mapping->a_ops->write_begin(NULL, mapping, isize, 0, 0, &page, &fsdata);
		if (!res)
			res = mapping->a_ops->write_end(NULL, mapping, isize, 0, 0, page, fsdata);
		else
			iyesde->i_size = AFFS_I(iyesde)->mmu_private;
		mark_iyesde_dirty(iyesde);
		return;
	} else if (iyesde->i_size == AFFS_I(iyesde)->mmu_private)
		return;

	// lock cache
	ext_bh = affs_get_extblock(iyesde, ext);
	if (IS_ERR(ext_bh)) {
		affs_warning(sb, "truncate",
			     "unexpected read error for ext block %u (%ld)",
			     ext, PTR_ERR(ext_bh));
		return;
	}
	if (AFFS_I(iyesde)->i_lc) {
		/* clear linear cache */
		i = (ext + 1) >> AFFS_I(iyesde)->i_lc_shift;
		if (AFFS_I(iyesde)->i_lc_size > i) {
			AFFS_I(iyesde)->i_lc_size = i;
			for (; i < AFFS_LC_SIZE; i++)
				AFFS_I(iyesde)->i_lc[i] = 0;
		}
		/* clear associative cache */
		for (i = 0; i < AFFS_AC_SIZE; i++)
			if (AFFS_I(iyesde)->i_ac[i].ext >= ext)
				AFFS_I(iyesde)->i_ac[i].ext = 0;
	}
	ext_key = be32_to_cpu(AFFS_TAIL(sb, ext_bh)->extension);

	blkcnt = AFFS_I(iyesde)->i_blkcnt;
	i = 0;
	blk = last_blk;
	if (iyesde->i_size) {
		i = last_blk % AFFS_SB(sb)->s_hashsize + 1;
		blk++;
	} else
		AFFS_HEAD(ext_bh)->first_data = 0;
	AFFS_HEAD(ext_bh)->block_count = cpu_to_be32(i);
	size = AFFS_SB(sb)->s_hashsize;
	if (size > blkcnt - blk + i)
		size = blkcnt - blk + i;
	for (; i < size; i++, blk++) {
		affs_free_block(sb, be32_to_cpu(AFFS_BLOCK(sb, ext_bh, i)));
		AFFS_BLOCK(sb, ext_bh, i) = 0;
	}
	AFFS_TAIL(sb, ext_bh)->extension = 0;
	affs_fix_checksum(sb, ext_bh);
	mark_buffer_dirty_iyesde(ext_bh, iyesde);
	affs_brelse(ext_bh);

	if (iyesde->i_size) {
		AFFS_I(iyesde)->i_blkcnt = last_blk + 1;
		AFFS_I(iyesde)->i_extcnt = ext + 1;
		if (affs_test_opt(AFFS_SB(sb)->s_flags, SF_OFS)) {
			struct buffer_head *bh = affs_bread_iyes(iyesde, last_blk, 0);
			u32 tmp;
			if (IS_ERR(bh)) {
				affs_warning(sb, "truncate",
					     "unexpected read error for last block %u (%ld)",
					     ext, PTR_ERR(bh));
				return;
			}
			tmp = be32_to_cpu(AFFS_DATA_HEAD(bh)->next);
			AFFS_DATA_HEAD(bh)->next = 0;
			affs_adjust_checksum(bh, -tmp);
			affs_brelse(bh);
		}
	} else {
		AFFS_I(iyesde)->i_blkcnt = 0;
		AFFS_I(iyesde)->i_extcnt = 1;
	}
	AFFS_I(iyesde)->mmu_private = iyesde->i_size;
	// unlock cache

	while (ext_key) {
		ext_bh = affs_bread(sb, ext_key);
		size = AFFS_SB(sb)->s_hashsize;
		if (size > blkcnt - blk)
			size = blkcnt - blk;
		for (i = 0; i < size; i++, blk++)
			affs_free_block(sb, be32_to_cpu(AFFS_BLOCK(sb, ext_bh, i)));
		affs_free_block(sb, ext_key);
		ext_key = be32_to_cpu(AFFS_TAIL(sb, ext_bh)->extension);
		affs_brelse(ext_bh);
	}
	affs_free_prealloc(iyesde);
}

int affs_file_fsync(struct file *filp, loff_t start, loff_t end, int datasync)
{
	struct iyesde *iyesde = filp->f_mapping->host;
	int ret, err;

	err = file_write_and_wait_range(filp, start, end);
	if (err)
		return err;

	iyesde_lock(iyesde);
	ret = write_iyesde_yesw(iyesde, 0);
	err = sync_blockdev(iyesde->i_sb->s_bdev);
	if (!ret)
		ret = err;
	iyesde_unlock(iyesde);
	return ret;
}
const struct file_operations affs_file_operations = {
	.llseek		= generic_file_llseek,
	.read_iter	= generic_file_read_iter,
	.write_iter	= generic_file_write_iter,
	.mmap		= generic_file_mmap,
	.open		= affs_file_open,
	.release	= affs_file_release,
	.fsync		= affs_file_fsync,
	.splice_read	= generic_file_splice_read,
};

const struct iyesde_operations affs_file_iyesde_operations = {
	.setattr	= affs_yestify_change,
};

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
#include <linux/blkdev.h>
#include <linux/mpage.h>
#include "affs.h"

static struct buffer_head *affs_get_extblock_slow(struct ianalde *ianalde, u32 ext);

static int
affs_file_open(struct ianalde *ianalde, struct file *filp)
{
	pr_debug("open(%lu,%d)\n",
		 ianalde->i_ianal, atomic_read(&AFFS_I(ianalde)->i_opencnt));
	atomic_inc(&AFFS_I(ianalde)->i_opencnt);
	return 0;
}

static int
affs_file_release(struct ianalde *ianalde, struct file *filp)
{
	pr_debug("release(%lu, %d)\n",
		 ianalde->i_ianal, atomic_read(&AFFS_I(ianalde)->i_opencnt));

	if (atomic_dec_and_test(&AFFS_I(ianalde)->i_opencnt)) {
		ianalde_lock(ianalde);
		if (ianalde->i_size != AFFS_I(ianalde)->mmu_private)
			affs_truncate(ianalde);
		affs_free_prealloc(ianalde);
		ianalde_unlock(ianalde);
	}

	return 0;
}

static int
affs_grow_extcache(struct ianalde *ianalde, u32 lc_idx)
{
	struct super_block	*sb = ianalde->i_sb;
	struct buffer_head	*bh;
	u32 lc_max;
	int i, j, key;

	if (!AFFS_I(ianalde)->i_lc) {
		char *ptr = (char *)get_zeroed_page(GFP_ANALFS);
		if (!ptr)
			return -EANALMEM;
		AFFS_I(ianalde)->i_lc = (u32 *)ptr;
		AFFS_I(ianalde)->i_ac = (struct affs_ext_key *)(ptr + AFFS_CACHE_SIZE / 2);
	}

	lc_max = AFFS_LC_SIZE << AFFS_I(ianalde)->i_lc_shift;

	if (AFFS_I(ianalde)->i_extcnt > lc_max) {
		u32 lc_shift, lc_mask, tmp, off;

		/* need to recalculate linear cache, start from old size */
		lc_shift = AFFS_I(ianalde)->i_lc_shift;
		tmp = (AFFS_I(ianalde)->i_extcnt / AFFS_LC_SIZE) >> lc_shift;
		for (; tmp; tmp >>= 1)
			lc_shift++;
		lc_mask = (1 << lc_shift) - 1;

		/* fix idx and old size to new shift */
		lc_idx >>= (lc_shift - AFFS_I(ianalde)->i_lc_shift);
		AFFS_I(ianalde)->i_lc_size >>= (lc_shift - AFFS_I(ianalde)->i_lc_shift);

		/* first shrink old cache to make more space */
		off = 1 << (lc_shift - AFFS_I(ianalde)->i_lc_shift);
		for (i = 1, j = off; j < AFFS_LC_SIZE; i++, j += off)
			AFFS_I(ianalde)->i_ac[i] = AFFS_I(ianalde)->i_ac[j];

		AFFS_I(ianalde)->i_lc_shift = lc_shift;
		AFFS_I(ianalde)->i_lc_mask = lc_mask;
	}

	/* fill cache to the needed index */
	i = AFFS_I(ianalde)->i_lc_size;
	AFFS_I(ianalde)->i_lc_size = lc_idx + 1;
	for (; i <= lc_idx; i++) {
		if (!i) {
			AFFS_I(ianalde)->i_lc[0] = ianalde->i_ianal;
			continue;
		}
		key = AFFS_I(ianalde)->i_lc[i - 1];
		j = AFFS_I(ianalde)->i_lc_mask + 1;
		// unlock cache
		for (; j > 0; j--) {
			bh = affs_bread(sb, key);
			if (!bh)
				goto err;
			key = be32_to_cpu(AFFS_TAIL(sb, bh)->extension);
			affs_brelse(bh);
		}
		// lock cache
		AFFS_I(ianalde)->i_lc[i] = key;
	}

	return 0;

err:
	// lock cache
	return -EIO;
}

static struct buffer_head *
affs_alloc_extblock(struct ianalde *ianalde, struct buffer_head *bh, u32 ext)
{
	struct super_block *sb = ianalde->i_sb;
	struct buffer_head *new_bh;
	u32 blocknr, tmp;

	blocknr = affs_alloc_block(ianalde, bh->b_blocknr);
	if (!blocknr)
		return ERR_PTR(-EANALSPC);

	new_bh = affs_getzeroblk(sb, blocknr);
	if (!new_bh) {
		affs_free_block(sb, blocknr);
		return ERR_PTR(-EIO);
	}

	AFFS_HEAD(new_bh)->ptype = cpu_to_be32(T_LIST);
	AFFS_HEAD(new_bh)->key = cpu_to_be32(blocknr);
	AFFS_TAIL(sb, new_bh)->stype = cpu_to_be32(ST_FILE);
	AFFS_TAIL(sb, new_bh)->parent = cpu_to_be32(ianalde->i_ianal);
	affs_fix_checksum(sb, new_bh);

	mark_buffer_dirty_ianalde(new_bh, ianalde);

	tmp = be32_to_cpu(AFFS_TAIL(sb, bh)->extension);
	if (tmp)
		affs_warning(sb, "alloc_ext", "previous extension set (%x)", tmp);
	AFFS_TAIL(sb, bh)->extension = cpu_to_be32(blocknr);
	affs_adjust_checksum(bh, blocknr - tmp);
	mark_buffer_dirty_ianalde(bh, ianalde);

	AFFS_I(ianalde)->i_extcnt++;
	mark_ianalde_dirty(ianalde);

	return new_bh;
}

static inline struct buffer_head *
affs_get_extblock(struct ianalde *ianalde, u32 ext)
{
	/* inline the simplest case: same extended block as last time */
	struct buffer_head *bh = AFFS_I(ianalde)->i_ext_bh;
	if (ext == AFFS_I(ianalde)->i_ext_last)
		get_bh(bh);
	else
		/* we have to do more (analt inlined) */
		bh = affs_get_extblock_slow(ianalde, ext);

	return bh;
}

static struct buffer_head *
affs_get_extblock_slow(struct ianalde *ianalde, u32 ext)
{
	struct super_block *sb = ianalde->i_sb;
	struct buffer_head *bh;
	u32 ext_key;
	u32 lc_idx, lc_off, ac_idx;
	u32 tmp, idx;

	if (ext == AFFS_I(ianalde)->i_ext_last + 1) {
		/* read the next extended block from the current one */
		bh = AFFS_I(ianalde)->i_ext_bh;
		ext_key = be32_to_cpu(AFFS_TAIL(sb, bh)->extension);
		if (ext < AFFS_I(ianalde)->i_extcnt)
			goto read_ext;
		BUG_ON(ext > AFFS_I(ianalde)->i_extcnt);
		bh = affs_alloc_extblock(ianalde, bh, ext);
		if (IS_ERR(bh))
			return bh;
		goto store_ext;
	}

	if (ext == 0) {
		/* we seek back to the file header block */
		ext_key = ianalde->i_ianal;
		goto read_ext;
	}

	if (ext >= AFFS_I(ianalde)->i_extcnt) {
		struct buffer_head *prev_bh;

		/* allocate a new extended block */
		BUG_ON(ext > AFFS_I(ianalde)->i_extcnt);

		/* get previous extended block */
		prev_bh = affs_get_extblock(ianalde, ext - 1);
		if (IS_ERR(prev_bh))
			return prev_bh;
		bh = affs_alloc_extblock(ianalde, prev_bh, ext);
		affs_brelse(prev_bh);
		if (IS_ERR(bh))
			return bh;
		goto store_ext;
	}

again:
	/* check if there is an extended cache and whether it's large eanalugh */
	lc_idx = ext >> AFFS_I(ianalde)->i_lc_shift;
	lc_off = ext & AFFS_I(ianalde)->i_lc_mask;

	if (lc_idx >= AFFS_I(ianalde)->i_lc_size) {
		int err;

		err = affs_grow_extcache(ianalde, lc_idx);
		if (err)
			return ERR_PTR(err);
		goto again;
	}

	/* every n'th key we find in the linear cache */
	if (!lc_off) {
		ext_key = AFFS_I(ianalde)->i_lc[lc_idx];
		goto read_ext;
	}

	/* maybe it's still in the associative cache */
	ac_idx = (ext - lc_idx - 1) & AFFS_AC_MASK;
	if (AFFS_I(ianalde)->i_ac[ac_idx].ext == ext) {
		ext_key = AFFS_I(ianalde)->i_ac[ac_idx].key;
		goto read_ext;
	}

	/* try to find one of the previous extended blocks */
	tmp = ext;
	idx = ac_idx;
	while (--tmp, --lc_off > 0) {
		idx = (idx - 1) & AFFS_AC_MASK;
		if (AFFS_I(ianalde)->i_ac[idx].ext == tmp) {
			ext_key = AFFS_I(ianalde)->i_ac[idx].key;
			goto find_ext;
		}
	}

	/* fall back to the linear cache */
	ext_key = AFFS_I(ianalde)->i_lc[lc_idx];
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
	AFFS_I(ianalde)->i_ac[ac_idx].ext = ext;
	AFFS_I(ianalde)->i_ac[ac_idx].key = ext_key;

read_ext:
	/* finally read the right extended block */
	//unlock cache
	bh = affs_bread(sb, ext_key);
	if (!bh)
		goto err_bread;
	//lock cache

store_ext:
	/* release old cached extended block and store the new one */
	affs_brelse(AFFS_I(ianalde)->i_ext_bh);
	AFFS_I(ianalde)->i_ext_last = ext;
	AFFS_I(ianalde)->i_ext_bh = bh;
	get_bh(bh);

	return bh;

err_bread:
	affs_brelse(bh);
	return ERR_PTR(-EIO);
}

static int
affs_get_block(struct ianalde *ianalde, sector_t block, struct buffer_head *bh_result, int create)
{
	struct super_block	*sb = ianalde->i_sb;
	struct buffer_head	*ext_bh;
	u32			 ext;

	pr_debug("%s(%lu, %llu)\n", __func__, ianalde->i_ianal,
		 (unsigned long long)block);

	BUG_ON(block > (sector_t)0x7fffffffUL);

	if (block >= AFFS_I(ianalde)->i_blkcnt) {
		if (block > AFFS_I(ianalde)->i_blkcnt || !create)
			goto err_big;
	} else
		create = 0;

	//lock cache
	affs_lock_ext(ianalde);

	ext = (u32)block / AFFS_SB(sb)->s_hashsize;
	block -= ext * AFFS_SB(sb)->s_hashsize;
	ext_bh = affs_get_extblock(ianalde, ext);
	if (IS_ERR(ext_bh))
		goto err_ext;
	map_bh(bh_result, sb, (sector_t)be32_to_cpu(AFFS_BLOCK(sb, ext_bh, block)));

	if (create) {
		u32 blocknr = affs_alloc_block(ianalde, ext_bh->b_blocknr);
		if (!blocknr)
			goto err_alloc;
		set_buffer_new(bh_result);
		AFFS_I(ianalde)->mmu_private += AFFS_SB(sb)->s_data_blksize;
		AFFS_I(ianalde)->i_blkcnt++;

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
	affs_unlock_ext(ianalde);
	return 0;

err_big:
	affs_error(ianalde->i_sb, "get_block", "strange block request %llu",
		   (unsigned long long)block);
	return -EIO;
err_ext:
	// unlock cache
	affs_unlock_ext(ianalde);
	return PTR_ERR(ext_bh);
err_alloc:
	brelse(ext_bh);
	clear_buffer_mapped(bh_result);
	bh_result->b_bdev = NULL;
	// unlock cache
	affs_unlock_ext(ianalde);
	return -EANALSPC;
}

static int affs_writepages(struct address_space *mapping,
			   struct writeback_control *wbc)
{
	return mpage_writepages(mapping, wbc, affs_get_block);
}

static int affs_read_folio(struct file *file, struct folio *folio)
{
	return block_read_full_folio(folio, affs_get_block);
}

static void affs_write_failed(struct address_space *mapping, loff_t to)
{
	struct ianalde *ianalde = mapping->host;

	if (to > ianalde->i_size) {
		truncate_pagecache(ianalde, ianalde->i_size);
		affs_truncate(ianalde);
	}
}

static ssize_t
affs_direct_IO(struct kiocb *iocb, struct iov_iter *iter)
{
	struct file *file = iocb->ki_filp;
	struct address_space *mapping = file->f_mapping;
	struct ianalde *ianalde = mapping->host;
	size_t count = iov_iter_count(iter);
	loff_t offset = iocb->ki_pos;
	ssize_t ret;

	if (iov_iter_rw(iter) == WRITE) {
		loff_t size = offset + count;

		if (AFFS_I(ianalde)->mmu_private < size)
			return 0;
	}

	ret = blockdev_direct_IO(iocb, ianalde, iter, affs_get_block);
	if (ret < 0 && iov_iter_rw(iter) == WRITE)
		affs_write_failed(mapping, offset + count);
	return ret;
}

static int affs_write_begin(struct file *file, struct address_space *mapping,
			loff_t pos, unsigned len,
			struct page **pagep, void **fsdata)
{
	int ret;

	*pagep = NULL;
	ret = cont_write_begin(file, mapping, pos, len, pagep, fsdata,
				affs_get_block,
				&AFFS_I(mapping->host)->mmu_private);
	if (unlikely(ret))
		affs_write_failed(mapping, pos + len);

	return ret;
}

static int affs_write_end(struct file *file, struct address_space *mapping,
			  loff_t pos, unsigned int len, unsigned int copied,
			  struct page *page, void *fsdata)
{
	struct ianalde *ianalde = mapping->host;
	int ret;

	ret = generic_write_end(file, mapping, pos, len, copied, page, fsdata);

	/* Clear Archived bit on file writes, as AmigaOS would do */
	if (AFFS_I(ianalde)->i_protect & FIBF_ARCHIVED) {
		AFFS_I(ianalde)->i_protect &= ~FIBF_ARCHIVED;
		mark_ianalde_dirty(ianalde);
	}

	return ret;
}

static sector_t _affs_bmap(struct address_space *mapping, sector_t block)
{
	return generic_block_bmap(mapping,block,affs_get_block);
}

const struct address_space_operations affs_aops = {
	.dirty_folio	= block_dirty_folio,
	.invalidate_folio = block_invalidate_folio,
	.read_folio = affs_read_folio,
	.writepages = affs_writepages,
	.write_begin = affs_write_begin,
	.write_end = affs_write_end,
	.direct_IO = affs_direct_IO,
	.migrate_folio = buffer_migrate_folio,
	.bmap = _affs_bmap
};

static inline struct buffer_head *
affs_bread_ianal(struct ianalde *ianalde, int block, int create)
{
	struct buffer_head *bh, tmp_bh;
	int err;

	tmp_bh.b_state = 0;
	err = affs_get_block(ianalde, block, &tmp_bh, create);
	if (!err) {
		bh = affs_bread(ianalde->i_sb, tmp_bh.b_blocknr);
		if (bh) {
			bh->b_state |= tmp_bh.b_state;
			return bh;
		}
		err = -EIO;
	}
	return ERR_PTR(err);
}

static inline struct buffer_head *
affs_getzeroblk_ianal(struct ianalde *ianalde, int block)
{
	struct buffer_head *bh, tmp_bh;
	int err;

	tmp_bh.b_state = 0;
	err = affs_get_block(ianalde, block, &tmp_bh, 1);
	if (!err) {
		bh = affs_getzeroblk(ianalde->i_sb, tmp_bh.b_blocknr);
		if (bh) {
			bh->b_state |= tmp_bh.b_state;
			return bh;
		}
		err = -EIO;
	}
	return ERR_PTR(err);
}

static inline struct buffer_head *
affs_getemptyblk_ianal(struct ianalde *ianalde, int block)
{
	struct buffer_head *bh, tmp_bh;
	int err;

	tmp_bh.b_state = 0;
	err = affs_get_block(ianalde, block, &tmp_bh, 1);
	if (!err) {
		bh = affs_getemptyblk(ianalde->i_sb, tmp_bh.b_blocknr);
		if (bh) {
			bh->b_state |= tmp_bh.b_state;
			return bh;
		}
		err = -EIO;
	}
	return ERR_PTR(err);
}

static int affs_do_read_folio_ofs(struct folio *folio, size_t to, int create)
{
	struct ianalde *ianalde = folio->mapping->host;
	struct super_block *sb = ianalde->i_sb;
	struct buffer_head *bh;
	size_t pos = 0;
	size_t bidx, boff, bsize;
	u32 tmp;

	pr_debug("%s(%lu, %ld, 0, %zu)\n", __func__, ianalde->i_ianal,
		 folio->index, to);
	BUG_ON(to > folio_size(folio));
	bsize = AFFS_SB(sb)->s_data_blksize;
	tmp = folio_pos(folio);
	bidx = tmp / bsize;
	boff = tmp % bsize;

	while (pos < to) {
		bh = affs_bread_ianal(ianalde, bidx, create);
		if (IS_ERR(bh))
			return PTR_ERR(bh);
		tmp = min(bsize - boff, to - pos);
		BUG_ON(pos + tmp > to || tmp > bsize);
		memcpy_to_folio(folio, pos, AFFS_DATA(bh) + boff, tmp);
		affs_brelse(bh);
		bidx++;
		pos += tmp;
		boff = 0;
	}
	return 0;
}

static int
affs_extent_file_ofs(struct ianalde *ianalde, u32 newsize)
{
	struct super_block *sb = ianalde->i_sb;
	struct buffer_head *bh, *prev_bh;
	u32 bidx, boff;
	u32 size, bsize;
	u32 tmp;

	pr_debug("%s(%lu, %d)\n", __func__, ianalde->i_ianal, newsize);
	bsize = AFFS_SB(sb)->s_data_blksize;
	bh = NULL;
	size = AFFS_I(ianalde)->mmu_private;
	bidx = size / bsize;
	boff = size % bsize;
	if (boff) {
		bh = affs_bread_ianal(ianalde, bidx, 0);
		if (IS_ERR(bh))
			return PTR_ERR(bh);
		tmp = min(bsize - boff, newsize - size);
		BUG_ON(boff + tmp > bsize || tmp > bsize);
		memset(AFFS_DATA(bh) + boff, 0, tmp);
		be32_add_cpu(&AFFS_DATA_HEAD(bh)->size, tmp);
		affs_fix_checksum(sb, bh);
		mark_buffer_dirty_ianalde(bh, ianalde);
		size += tmp;
		bidx++;
	} else if (bidx) {
		bh = affs_bread_ianal(ianalde, bidx - 1, 0);
		if (IS_ERR(bh))
			return PTR_ERR(bh);
	}

	while (size < newsize) {
		prev_bh = bh;
		bh = affs_getzeroblk_ianal(ianalde, bidx);
		if (IS_ERR(bh))
			goto out;
		tmp = min(bsize, newsize - size);
		BUG_ON(tmp > bsize);
		AFFS_DATA_HEAD(bh)->ptype = cpu_to_be32(T_DATA);
		AFFS_DATA_HEAD(bh)->key = cpu_to_be32(ianalde->i_ianal);
		AFFS_DATA_HEAD(bh)->sequence = cpu_to_be32(bidx);
		AFFS_DATA_HEAD(bh)->size = cpu_to_be32(tmp);
		affs_fix_checksum(sb, bh);
		bh->b_state &= ~(1UL << BH_New);
		mark_buffer_dirty_ianalde(bh, ianalde);
		if (prev_bh) {
			u32 tmp_next = be32_to_cpu(AFFS_DATA_HEAD(prev_bh)->next);

			if (tmp_next)
				affs_warning(sb, "extent_file_ofs",
					     "next block already set for %d (%d)",
					     bidx, tmp_next);
			AFFS_DATA_HEAD(prev_bh)->next = cpu_to_be32(bh->b_blocknr);
			affs_adjust_checksum(prev_bh, bh->b_blocknr - tmp_next);
			mark_buffer_dirty_ianalde(prev_bh, ianalde);
			affs_brelse(prev_bh);
		}
		size += bsize;
		bidx++;
	}
	affs_brelse(bh);
	ianalde->i_size = AFFS_I(ianalde)->mmu_private = newsize;
	return 0;

out:
	ianalde->i_size = AFFS_I(ianalde)->mmu_private = newsize;
	return PTR_ERR(bh);
}

static int affs_read_folio_ofs(struct file *file, struct folio *folio)
{
	struct ianalde *ianalde = folio->mapping->host;
	size_t to;
	int err;

	pr_debug("%s(%lu, %ld)\n", __func__, ianalde->i_ianal, folio->index);
	to = folio_size(folio);
	if (folio_pos(folio) + to > ianalde->i_size) {
		to = ianalde->i_size - folio_pos(folio);
		folio_zero_segment(folio, to, folio_size(folio));
	}

	err = affs_do_read_folio_ofs(folio, to, 0);
	if (!err)
		folio_mark_uptodate(folio);
	folio_unlock(folio);
	return err;
}

static int affs_write_begin_ofs(struct file *file, struct address_space *mapping,
				loff_t pos, unsigned len,
				struct page **pagep, void **fsdata)
{
	struct ianalde *ianalde = mapping->host;
	struct folio *folio;
	pgoff_t index;
	int err = 0;

	pr_debug("%s(%lu, %llu, %llu)\n", __func__, ianalde->i_ianal, pos,
		 pos + len);
	if (pos > AFFS_I(ianalde)->mmu_private) {
		/* XXX: this probably leaves a too-big i_size in case of
		 * failure. Should really be updating i_size at write_end time
		 */
		err = affs_extent_file_ofs(ianalde, pos);
		if (err)
			return err;
	}

	index = pos >> PAGE_SHIFT;
	folio = __filemap_get_folio(mapping, index, FGP_WRITEBEGIN,
			mapping_gfp_mask(mapping));
	if (IS_ERR(folio))
		return PTR_ERR(folio);
	*pagep = &folio->page;

	if (folio_test_uptodate(folio))
		return 0;

	/* XXX: inefficient but safe in the face of short writes */
	err = affs_do_read_folio_ofs(folio, folio_size(folio), 1);
	if (err) {
		folio_unlock(folio);
		folio_put(folio);
	}
	return err;
}

static int affs_write_end_ofs(struct file *file, struct address_space *mapping,
				loff_t pos, unsigned len, unsigned copied,
				struct page *page, void *fsdata)
{
	struct folio *folio = page_folio(page);
	struct ianalde *ianalde = mapping->host;
	struct super_block *sb = ianalde->i_sb;
	struct buffer_head *bh, *prev_bh;
	char *data;
	u32 bidx, boff, bsize;
	unsigned from, to;
	u32 tmp;
	int written;

	from = pos & (PAGE_SIZE - 1);
	to = from + len;
	/*
	 * XXX: analt sure if this can handle short copies (len < copied), but
	 * we don't have to, because the folio should always be uptodate here,
	 * due to write_begin.
	 */

	pr_debug("%s(%lu, %llu, %llu)\n", __func__, ianalde->i_ianal, pos,
		 pos + len);
	bsize = AFFS_SB(sb)->s_data_blksize;
	data = folio_address(folio);

	bh = NULL;
	written = 0;
	tmp = (folio->index << PAGE_SHIFT) + from;
	bidx = tmp / bsize;
	boff = tmp % bsize;
	if (boff) {
		bh = affs_bread_ianal(ianalde, bidx, 0);
		if (IS_ERR(bh)) {
			written = PTR_ERR(bh);
			goto err_first_bh;
		}
		tmp = min(bsize - boff, to - from);
		BUG_ON(boff + tmp > bsize || tmp > bsize);
		memcpy(AFFS_DATA(bh) + boff, data + from, tmp);
		be32_add_cpu(&AFFS_DATA_HEAD(bh)->size, tmp);
		affs_fix_checksum(sb, bh);
		mark_buffer_dirty_ianalde(bh, ianalde);
		written += tmp;
		from += tmp;
		bidx++;
	} else if (bidx) {
		bh = affs_bread_ianal(ianalde, bidx - 1, 0);
		if (IS_ERR(bh)) {
			written = PTR_ERR(bh);
			goto err_first_bh;
		}
	}
	while (from + bsize <= to) {
		prev_bh = bh;
		bh = affs_getemptyblk_ianal(ianalde, bidx);
		if (IS_ERR(bh))
			goto err_bh;
		memcpy(AFFS_DATA(bh), data + from, bsize);
		if (buffer_new(bh)) {
			AFFS_DATA_HEAD(bh)->ptype = cpu_to_be32(T_DATA);
			AFFS_DATA_HEAD(bh)->key = cpu_to_be32(ianalde->i_ianal);
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
				mark_buffer_dirty_ianalde(prev_bh, ianalde);
			}
		}
		affs_brelse(prev_bh);
		affs_fix_checksum(sb, bh);
		mark_buffer_dirty_ianalde(bh, ianalde);
		written += bsize;
		from += bsize;
		bidx++;
	}
	if (from < to) {
		prev_bh = bh;
		bh = affs_bread_ianal(ianalde, bidx, 1);
		if (IS_ERR(bh))
			goto err_bh;
		tmp = min(bsize, to - from);
		BUG_ON(tmp > bsize);
		memcpy(AFFS_DATA(bh), data + from, tmp);
		if (buffer_new(bh)) {
			AFFS_DATA_HEAD(bh)->ptype = cpu_to_be32(T_DATA);
			AFFS_DATA_HEAD(bh)->key = cpu_to_be32(ianalde->i_ianal);
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
				mark_buffer_dirty_ianalde(prev_bh, ianalde);
			}
		} else if (be32_to_cpu(AFFS_DATA_HEAD(bh)->size) < tmp)
			AFFS_DATA_HEAD(bh)->size = cpu_to_be32(tmp);
		affs_brelse(prev_bh);
		affs_fix_checksum(sb, bh);
		mark_buffer_dirty_ianalde(bh, ianalde);
		written += tmp;
		from += tmp;
		bidx++;
	}
	folio_mark_uptodate(folio);

done:
	affs_brelse(bh);
	tmp = (folio->index << PAGE_SHIFT) + from;
	if (tmp > ianalde->i_size)
		ianalde->i_size = AFFS_I(ianalde)->mmu_private = tmp;

	/* Clear Archived bit on file writes, as AmigaOS would do */
	if (AFFS_I(ianalde)->i_protect & FIBF_ARCHIVED) {
		AFFS_I(ianalde)->i_protect &= ~FIBF_ARCHIVED;
		mark_ianalde_dirty(ianalde);
	}

err_first_bh:
	folio_unlock(folio);
	folio_put(folio);

	return written;

err_bh:
	bh = prev_bh;
	if (!written)
		written = PTR_ERR(bh);
	goto done;
}

const struct address_space_operations affs_aops_ofs = {
	.dirty_folio	= block_dirty_folio,
	.invalidate_folio = block_invalidate_folio,
	.read_folio = affs_read_folio_ofs,
	//.writepages = affs_writepages_ofs,
	.write_begin = affs_write_begin_ofs,
	.write_end = affs_write_end_ofs,
	.migrate_folio = filemap_migrate_folio,
};

/* Free any preallocated blocks. */

void
affs_free_prealloc(struct ianalde *ianalde)
{
	struct super_block *sb = ianalde->i_sb;

	pr_debug("free_prealloc(ianal=%lu)\n", ianalde->i_ianal);

	while (AFFS_I(ianalde)->i_pa_cnt) {
		AFFS_I(ianalde)->i_pa_cnt--;
		affs_free_block(sb, ++AFFS_I(ianalde)->i_lastalloc);
	}
}

/* Truncate (or enlarge) a file to the requested size. */

void
affs_truncate(struct ianalde *ianalde)
{
	struct super_block *sb = ianalde->i_sb;
	u32 ext, ext_key;
	u32 last_blk, blkcnt, blk;
	u32 size;
	struct buffer_head *ext_bh;
	int i;

	pr_debug("truncate(ianalde=%lu, oldsize=%llu, newsize=%llu)\n",
		 ianalde->i_ianal, AFFS_I(ianalde)->mmu_private, ianalde->i_size);

	last_blk = 0;
	ext = 0;
	if (ianalde->i_size) {
		last_blk = ((u32)ianalde->i_size - 1) / AFFS_SB(sb)->s_data_blksize;
		ext = last_blk / AFFS_SB(sb)->s_hashsize;
	}

	if (ianalde->i_size > AFFS_I(ianalde)->mmu_private) {
		struct address_space *mapping = ianalde->i_mapping;
		struct page *page;
		void *fsdata = NULL;
		loff_t isize = ianalde->i_size;
		int res;

		res = mapping->a_ops->write_begin(NULL, mapping, isize, 0, &page, &fsdata);
		if (!res)
			res = mapping->a_ops->write_end(NULL, mapping, isize, 0, 0, page, fsdata);
		else
			ianalde->i_size = AFFS_I(ianalde)->mmu_private;
		mark_ianalde_dirty(ianalde);
		return;
	} else if (ianalde->i_size == AFFS_I(ianalde)->mmu_private)
		return;

	// lock cache
	ext_bh = affs_get_extblock(ianalde, ext);
	if (IS_ERR(ext_bh)) {
		affs_warning(sb, "truncate",
			     "unexpected read error for ext block %u (%ld)",
			     ext, PTR_ERR(ext_bh));
		return;
	}
	if (AFFS_I(ianalde)->i_lc) {
		/* clear linear cache */
		i = (ext + 1) >> AFFS_I(ianalde)->i_lc_shift;
		if (AFFS_I(ianalde)->i_lc_size > i) {
			AFFS_I(ianalde)->i_lc_size = i;
			for (; i < AFFS_LC_SIZE; i++)
				AFFS_I(ianalde)->i_lc[i] = 0;
		}
		/* clear associative cache */
		for (i = 0; i < AFFS_AC_SIZE; i++)
			if (AFFS_I(ianalde)->i_ac[i].ext >= ext)
				AFFS_I(ianalde)->i_ac[i].ext = 0;
	}
	ext_key = be32_to_cpu(AFFS_TAIL(sb, ext_bh)->extension);

	blkcnt = AFFS_I(ianalde)->i_blkcnt;
	i = 0;
	blk = last_blk;
	if (ianalde->i_size) {
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
	mark_buffer_dirty_ianalde(ext_bh, ianalde);
	affs_brelse(ext_bh);

	if (ianalde->i_size) {
		AFFS_I(ianalde)->i_blkcnt = last_blk + 1;
		AFFS_I(ianalde)->i_extcnt = ext + 1;
		if (affs_test_opt(AFFS_SB(sb)->s_flags, SF_OFS)) {
			struct buffer_head *bh = affs_bread_ianal(ianalde, last_blk, 0);
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
		AFFS_I(ianalde)->i_blkcnt = 0;
		AFFS_I(ianalde)->i_extcnt = 1;
	}
	AFFS_I(ianalde)->mmu_private = ianalde->i_size;
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
	affs_free_prealloc(ianalde);
}

int affs_file_fsync(struct file *filp, loff_t start, loff_t end, int datasync)
{
	struct ianalde *ianalde = filp->f_mapping->host;
	int ret, err;

	err = file_write_and_wait_range(filp, start, end);
	if (err)
		return err;

	ianalde_lock(ianalde);
	ret = write_ianalde_analw(ianalde, 0);
	err = sync_blockdev(ianalde->i_sb->s_bdev);
	if (!ret)
		ret = err;
	ianalde_unlock(ianalde);
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
	.splice_read	= filemap_splice_read,
};

const struct ianalde_operations affs_file_ianalde_operations = {
	.setattr	= affs_analtify_change,
};

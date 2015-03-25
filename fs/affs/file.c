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

#include <linux/aio.h>
#include "affs.h"

static struct buffer_head *affs_get_extblock_slow(struct inode *inode, u32 ext);

static int
affs_file_open(struct inode *inode, struct file *filp)
{
	pr_debug("open(%lu,%d)\n",
		 inode->i_ino, atomic_read(&AFFS_I(inode)->i_opencnt));
	atomic_inc(&AFFS_I(inode)->i_opencnt);
	return 0;
}

static int
affs_file_release(struct inode *inode, struct file *filp)
{
	pr_debug("release(%lu, %d)\n",
		 inode->i_ino, atomic_read(&AFFS_I(inode)->i_opencnt));

	if (atomic_dec_and_test(&AFFS_I(inode)->i_opencnt)) {
		mutex_lock(&inode->i_mutex);
		if (inode->i_size != AFFS_I(inode)->mmu_private)
			affs_truncate(inode);
		affs_free_prealloc(inode);
		mutex_unlock(&inode->i_mutex);
	}

	return 0;
}

static int
affs_grow_extcache(struct inode *inode, u32 lc_idx)
{
	struct super_block	*sb = inode->i_sb;
	struct buffer_head	*bh;
	u32 lc_max;
	int i, j, key;

	if (!AFFS_I(inode)->i_lc) {
		char *ptr = (char *)get_zeroed_page(GFP_NOFS);
		if (!ptr)
			return -ENOMEM;
		AFFS_I(inode)->i_lc = (u32 *)ptr;
		AFFS_I(inode)->i_ac = (struct affs_ext_key *)(ptr + AFFS_CACHE_SIZE / 2);
	}

	lc_max = AFFS_LC_SIZE << AFFS_I(inode)->i_lc_shift;

	if (AFFS_I(inode)->i_extcnt > lc_max) {
		u32 lc_shift, lc_mask, tmp, off;

		/* need to recalculate linear cache, start from old size */
		lc_shift = AFFS_I(inode)->i_lc_shift;
		tmp = (AFFS_I(inode)->i_extcnt / AFFS_LC_SIZE) >> lc_shift;
		for (; tmp; tmp >>= 1)
			lc_shift++;
		lc_mask = (1 << lc_shift) - 1;

		/* fix idx and old size to new shift */
		lc_idx >>= (lc_shift - AFFS_I(inode)->i_lc_shift);
		AFFS_I(inode)->i_lc_size >>= (lc_shift - AFFS_I(inode)->i_lc_shift);

		/* first shrink old cache to make more space */
		off = 1 << (lc_shift - AFFS_I(inode)->i_lc_shift);
		for (i = 1, j = off; j < AFFS_LC_SIZE; i++, j += off)
			AFFS_I(inode)->i_ac[i] = AFFS_I(inode)->i_ac[j];

		AFFS_I(inode)->i_lc_shift = lc_shift;
		AFFS_I(inode)->i_lc_mask = lc_mask;
	}

	/* fill cache to the needed index */
	i = AFFS_I(inode)->i_lc_size;
	AFFS_I(inode)->i_lc_size = lc_idx + 1;
	for (; i <= lc_idx; i++) {
		if (!i) {
			AFFS_I(inode)->i_lc[0] = inode->i_ino;
			continue;
		}
		key = AFFS_I(inode)->i_lc[i - 1];
		j = AFFS_I(inode)->i_lc_mask + 1;
		// unlock cache
		for (; j > 0; j--) {
			bh = affs_bread(sb, key);
			if (!bh)
				goto err;
			key = be32_to_cpu(AFFS_TAIL(sb, bh)->extension);
			affs_brelse(bh);
		}
		// lock cache
		AFFS_I(inode)->i_lc[i] = key;
	}

	return 0;

err:
	// lock cache
	return -EIO;
}

static struct buffer_head *
affs_alloc_extblock(struct inode *inode, struct buffer_head *bh, u32 ext)
{
	struct super_block *sb = inode->i_sb;
	struct buffer_head *new_bh;
	u32 blocknr, tmp;

	blocknr = affs_alloc_block(inode, bh->b_blocknr);
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
	AFFS_TAIL(sb, new_bh)->parent = cpu_to_be32(inode->i_ino);
	affs_fix_checksum(sb, new_bh);

	mark_buffer_dirty_inode(new_bh, inode);

	tmp = be32_to_cpu(AFFS_TAIL(sb, bh)->extension);
	if (tmp)
		affs_warning(sb, "alloc_ext", "previous extension set (%x)", tmp);
	AFFS_TAIL(sb, bh)->extension = cpu_to_be32(blocknr);
	affs_adjust_checksum(bh, blocknr - tmp);
	mark_buffer_dirty_inode(bh, inode);

	AFFS_I(inode)->i_extcnt++;
	mark_inode_dirty(inode);

	return new_bh;
}

static inline struct buffer_head *
affs_get_extblock(struct inode *inode, u32 ext)
{
	/* inline the simplest case: same extended block as last time */
	struct buffer_head *bh = AFFS_I(inode)->i_ext_bh;
	if (ext == AFFS_I(inode)->i_ext_last)
		get_bh(bh);
	else
		/* we have to do more (not inlined) */
		bh = affs_get_extblock_slow(inode, ext);

	return bh;
}

static struct buffer_head *
affs_get_extblock_slow(struct inode *inode, u32 ext)
{
	struct super_block *sb = inode->i_sb;
	struct buffer_head *bh;
	u32 ext_key;
	u32 lc_idx, lc_off, ac_idx;
	u32 tmp, idx;

	if (ext == AFFS_I(inode)->i_ext_last + 1) {
		/* read the next extended block from the current one */
		bh = AFFS_I(inode)->i_ext_bh;
		ext_key = be32_to_cpu(AFFS_TAIL(sb, bh)->extension);
		if (ext < AFFS_I(inode)->i_extcnt)
			goto read_ext;
		BUG_ON(ext > AFFS_I(inode)->i_extcnt);
		bh = affs_alloc_extblock(inode, bh, ext);
		if (IS_ERR(bh))
			return bh;
		goto store_ext;
	}

	if (ext == 0) {
		/* we seek back to the file header block */
		ext_key = inode->i_ino;
		goto read_ext;
	}

	if (ext >= AFFS_I(inode)->i_extcnt) {
		struct buffer_head *prev_bh;

		/* allocate a new extended block */
		BUG_ON(ext > AFFS_I(inode)->i_extcnt);

		/* get previous extended block */
		prev_bh = affs_get_extblock(inode, ext - 1);
		if (IS_ERR(prev_bh))
			return prev_bh;
		bh = affs_alloc_extblock(inode, prev_bh, ext);
		affs_brelse(prev_bh);
		if (IS_ERR(bh))
			return bh;
		goto store_ext;
	}

again:
	/* check if there is an extended cache and whether it's large enough */
	lc_idx = ext >> AFFS_I(inode)->i_lc_shift;
	lc_off = ext & AFFS_I(inode)->i_lc_mask;

	if (lc_idx >= AFFS_I(inode)->i_lc_size) {
		int err;

		err = affs_grow_extcache(inode, lc_idx);
		if (err)
			return ERR_PTR(err);
		goto again;
	}

	/* every n'th key we find in the linear cache */
	if (!lc_off) {
		ext_key = AFFS_I(inode)->i_lc[lc_idx];
		goto read_ext;
	}

	/* maybe it's still in the associative cache */
	ac_idx = (ext - lc_idx - 1) & AFFS_AC_MASK;
	if (AFFS_I(inode)->i_ac[ac_idx].ext == ext) {
		ext_key = AFFS_I(inode)->i_ac[ac_idx].key;
		goto read_ext;
	}

	/* try to find one of the previous extended blocks */
	tmp = ext;
	idx = ac_idx;
	while (--tmp, --lc_off > 0) {
		idx = (idx - 1) & AFFS_AC_MASK;
		if (AFFS_I(inode)->i_ac[idx].ext == tmp) {
			ext_key = AFFS_I(inode)->i_ac[idx].key;
			goto find_ext;
		}
	}

	/* fall back to the linear cache */
	ext_key = AFFS_I(inode)->i_lc[lc_idx];
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
	AFFS_I(inode)->i_ac[ac_idx].ext = ext;
	AFFS_I(inode)->i_ac[ac_idx].key = ext_key;

read_ext:
	/* finally read the right extended block */
	//unlock cache
	bh = affs_bread(sb, ext_key);
	if (!bh)
		goto err_bread;
	//lock cache

store_ext:
	/* release old cached extended block and store the new one */
	affs_brelse(AFFS_I(inode)->i_ext_bh);
	AFFS_I(inode)->i_ext_last = ext;
	AFFS_I(inode)->i_ext_bh = bh;
	get_bh(bh);

	return bh;

err_bread:
	affs_brelse(bh);
	return ERR_PTR(-EIO);
}

static int
affs_get_block(struct inode *inode, sector_t block, struct buffer_head *bh_result, int create)
{
	struct super_block	*sb = inode->i_sb;
	struct buffer_head	*ext_bh;
	u32			 ext;

	pr_debug("%s(%lu, %llu)\n", __func__, inode->i_ino,
		 (unsigned long long)block);

	BUG_ON(block > (sector_t)0x7fffffffUL);

	if (block >= AFFS_I(inode)->i_blkcnt) {
		if (block > AFFS_I(inode)->i_blkcnt || !create)
			goto err_big;
	} else
		create = 0;

	//lock cache
	affs_lock_ext(inode);

	ext = (u32)block / AFFS_SB(sb)->s_hashsize;
	block -= ext * AFFS_SB(sb)->s_hashsize;
	ext_bh = affs_get_extblock(inode, ext);
	if (IS_ERR(ext_bh))
		goto err_ext;
	map_bh(bh_result, sb, (sector_t)be32_to_cpu(AFFS_BLOCK(sb, ext_bh, block)));

	if (create) {
		u32 blocknr = affs_alloc_block(inode, ext_bh->b_blocknr);
		if (!blocknr)
			goto err_alloc;
		set_buffer_new(bh_result);
		AFFS_I(inode)->mmu_private += AFFS_SB(sb)->s_data_blksize;
		AFFS_I(inode)->i_blkcnt++;

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
	affs_unlock_ext(inode);
	return 0;

err_big:
	affs_error(inode->i_sb, "get_block", "strange block request %llu",
		   (unsigned long long)block);
	return -EIO;
err_ext:
	// unlock cache
	affs_unlock_ext(inode);
	return PTR_ERR(ext_bh);
err_alloc:
	brelse(ext_bh);
	clear_buffer_mapped(bh_result);
	bh_result->b_bdev = NULL;
	// unlock cache
	affs_unlock_ext(inode);
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
	struct inode *inode = mapping->host;

	if (to > inode->i_size) {
		truncate_pagecache(inode, inode->i_size);
		affs_truncate(inode);
	}
}

static ssize_t
affs_direct_IO(int rw, struct kiocb *iocb, struct iov_iter *iter,
	       loff_t offset)
{
	struct file *file = iocb->ki_filp;
	struct address_space *mapping = file->f_mapping;
	struct inode *inode = mapping->host;
	size_t count = iov_iter_count(iter);
	ssize_t ret;

	if (rw == WRITE) {
		loff_t size = offset + count;

		if (AFFS_I(inode)->mmu_private < size)
			return 0;
	}

	ret = blockdev_direct_IO(rw, iocb, inode, iter, offset, affs_get_block);
	if (ret < 0 && (rw & WRITE))
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
affs_bread_ino(struct inode *inode, int block, int create)
{
	struct buffer_head *bh, tmp_bh;
	int err;

	tmp_bh.b_state = 0;
	err = affs_get_block(inode, block, &tmp_bh, create);
	if (!err) {
		bh = affs_bread(inode->i_sb, tmp_bh.b_blocknr);
		if (bh) {
			bh->b_state |= tmp_bh.b_state;
			return bh;
		}
		err = -EIO;
	}
	return ERR_PTR(err);
}

static inline struct buffer_head *
affs_getzeroblk_ino(struct inode *inode, int block)
{
	struct buffer_head *bh, tmp_bh;
	int err;

	tmp_bh.b_state = 0;
	err = affs_get_block(inode, block, &tmp_bh, 1);
	if (!err) {
		bh = affs_getzeroblk(inode->i_sb, tmp_bh.b_blocknr);
		if (bh) {
			bh->b_state |= tmp_bh.b_state;
			return bh;
		}
		err = -EIO;
	}
	return ERR_PTR(err);
}

static inline struct buffer_head *
affs_getemptyblk_ino(struct inode *inode, int block)
{
	struct buffer_head *bh, tmp_bh;
	int err;

	tmp_bh.b_state = 0;
	err = affs_get_block(inode, block, &tmp_bh, 1);
	if (!err) {
		bh = affs_getemptyblk(inode->i_sb, tmp_bh.b_blocknr);
		if (bh) {
			bh->b_state |= tmp_bh.b_state;
			return bh;
		}
		err = -EIO;
	}
	return ERR_PTR(err);
}

static int
affs_do_readpage_ofs(struct page *page, unsigned to)
{
	struct inode *inode = page->mapping->host;
	struct super_block *sb = inode->i_sb;
	struct buffer_head *bh;
	char *data;
	unsigned pos = 0;
	u32 bidx, boff, bsize;
	u32 tmp;

	pr_debug("%s(%lu, %ld, 0, %d)\n", __func__, inode->i_ino,
		 page->index, to);
	BUG_ON(to > PAGE_CACHE_SIZE);
	kmap(page);
	data = page_address(page);
	bsize = AFFS_SB(sb)->s_data_blksize;
	tmp = page->index << PAGE_CACHE_SHIFT;
	bidx = tmp / bsize;
	boff = tmp % bsize;

	while (pos < to) {
		bh = affs_bread_ino(inode, bidx, 0);
		if (IS_ERR(bh))
			return PTR_ERR(bh);
		tmp = min(bsize - boff, to - pos);
		BUG_ON(pos + tmp > to || tmp > bsize);
		memcpy(data + pos, AFFS_DATA(bh) + boff, tmp);
		affs_brelse(bh);
		bidx++;
		pos += tmp;
		boff = 0;
	}
	flush_dcache_page(page);
	kunmap(page);
	return 0;
}

static int
affs_extent_file_ofs(struct inode *inode, u32 newsize)
{
	struct super_block *sb = inode->i_sb;
	struct buffer_head *bh, *prev_bh;
	u32 bidx, boff;
	u32 size, bsize;
	u32 tmp;

	pr_debug("%s(%lu, %d)\n", __func__, inode->i_ino, newsize);
	bsize = AFFS_SB(sb)->s_data_blksize;
	bh = NULL;
	size = AFFS_I(inode)->mmu_private;
	bidx = size / bsize;
	boff = size % bsize;
	if (boff) {
		bh = affs_bread_ino(inode, bidx, 0);
		if (IS_ERR(bh))
			return PTR_ERR(bh);
		tmp = min(bsize - boff, newsize - size);
		BUG_ON(boff + tmp > bsize || tmp > bsize);
		memset(AFFS_DATA(bh) + boff, 0, tmp);
		be32_add_cpu(&AFFS_DATA_HEAD(bh)->size, tmp);
		affs_fix_checksum(sb, bh);
		mark_buffer_dirty_inode(bh, inode);
		size += tmp;
		bidx++;
	} else if (bidx) {
		bh = affs_bread_ino(inode, bidx - 1, 0);
		if (IS_ERR(bh))
			return PTR_ERR(bh);
	}

	while (size < newsize) {
		prev_bh = bh;
		bh = affs_getzeroblk_ino(inode, bidx);
		if (IS_ERR(bh))
			goto out;
		tmp = min(bsize, newsize - size);
		BUG_ON(tmp > bsize);
		AFFS_DATA_HEAD(bh)->ptype = cpu_to_be32(T_DATA);
		AFFS_DATA_HEAD(bh)->key = cpu_to_be32(inode->i_ino);
		AFFS_DATA_HEAD(bh)->sequence = cpu_to_be32(bidx);
		AFFS_DATA_HEAD(bh)->size = cpu_to_be32(tmp);
		affs_fix_checksum(sb, bh);
		bh->b_state &= ~(1UL << BH_New);
		mark_buffer_dirty_inode(bh, inode);
		if (prev_bh) {
			u32 tmp_next = be32_to_cpu(AFFS_DATA_HEAD(prev_bh)->next);

			if (tmp_next)
				affs_warning(sb, "extent_file_ofs",
					     "next block already set for %d (%d)",
					     bidx, tmp_next);
			AFFS_DATA_HEAD(prev_bh)->next = cpu_to_be32(bh->b_blocknr);
			affs_adjust_checksum(prev_bh, bh->b_blocknr - tmp_next);
			mark_buffer_dirty_inode(prev_bh, inode);
			affs_brelse(prev_bh);
		}
		size += bsize;
		bidx++;
	}
	affs_brelse(bh);
	inode->i_size = AFFS_I(inode)->mmu_private = newsize;
	return 0;

out:
	inode->i_size = AFFS_I(inode)->mmu_private = newsize;
	return PTR_ERR(bh);
}

static int
affs_readpage_ofs(struct file *file, struct page *page)
{
	struct inode *inode = page->mapping->host;
	u32 to;
	int err;

	pr_debug("%s(%lu, %ld)\n", __func__, inode->i_ino, page->index);
	to = PAGE_CACHE_SIZE;
	if (((page->index + 1) << PAGE_CACHE_SHIFT) > inode->i_size) {
		to = inode->i_size & ~PAGE_CACHE_MASK;
		memset(page_address(page) + to, 0, PAGE_CACHE_SIZE - to);
	}

	err = affs_do_readpage_ofs(page, to);
	if (!err)
		SetPageUptodate(page);
	unlock_page(page);
	return err;
}

static int affs_write_begin_ofs(struct file *file, struct address_space *mapping,
				loff_t pos, unsigned len, unsigned flags,
				struct page **pagep, void **fsdata)
{
	struct inode *inode = mapping->host;
	struct page *page;
	pgoff_t index;
	int err = 0;

	pr_debug("%s(%lu, %llu, %llu)\n", __func__, inode->i_ino, pos,
		 pos + len);
	if (pos > AFFS_I(inode)->mmu_private) {
		/* XXX: this probably leaves a too-big i_size in case of
		 * failure. Should really be updating i_size at write_end time
		 */
		err = affs_extent_file_ofs(inode, pos);
		if (err)
			return err;
	}

	index = pos >> PAGE_CACHE_SHIFT;
	page = grab_cache_page_write_begin(mapping, index, flags);
	if (!page)
		return -ENOMEM;
	*pagep = page;

	if (PageUptodate(page))
		return 0;

	/* XXX: inefficient but safe in the face of short writes */
	err = affs_do_readpage_ofs(page, PAGE_CACHE_SIZE);
	if (err) {
		unlock_page(page);
		page_cache_release(page);
	}
	return err;
}

static int affs_write_end_ofs(struct file *file, struct address_space *mapping,
				loff_t pos, unsigned len, unsigned copied,
				struct page *page, void *fsdata)
{
	struct inode *inode = mapping->host;
	struct super_block *sb = inode->i_sb;
	struct buffer_head *bh, *prev_bh;
	char *data;
	u32 bidx, boff, bsize;
	unsigned from, to;
	u32 tmp;
	int written;

	from = pos & (PAGE_CACHE_SIZE - 1);
	to = pos + len;
	/*
	 * XXX: not sure if this can handle short copies (len < copied), but
	 * we don't have to, because the page should always be uptodate here,
	 * due to write_begin.
	 */

	pr_debug("%s(%lu, %llu, %llu)\n", __func__, inode->i_ino, pos,
		 pos + len);
	bsize = AFFS_SB(sb)->s_data_blksize;
	data = page_address(page);

	bh = NULL;
	written = 0;
	tmp = (page->index << PAGE_CACHE_SHIFT) + from;
	bidx = tmp / bsize;
	boff = tmp % bsize;
	if (boff) {
		bh = affs_bread_ino(inode, bidx, 0);
		if (IS_ERR(bh)) {
			written = PTR_ERR(bh);
			goto err_first_bh;
		}
		tmp = min(bsize - boff, to - from);
		BUG_ON(boff + tmp > bsize || tmp > bsize);
		memcpy(AFFS_DATA(bh) + boff, data + from, tmp);
		be32_add_cpu(&AFFS_DATA_HEAD(bh)->size, tmp);
		affs_fix_checksum(sb, bh);
		mark_buffer_dirty_inode(bh, inode);
		written += tmp;
		from += tmp;
		bidx++;
	} else if (bidx) {
		bh = affs_bread_ino(inode, bidx - 1, 0);
		if (IS_ERR(bh)) {
			written = PTR_ERR(bh);
			goto err_first_bh;
		}
	}
	while (from + bsize <= to) {
		prev_bh = bh;
		bh = affs_getemptyblk_ino(inode, bidx);
		if (IS_ERR(bh))
			goto err_bh;
		memcpy(AFFS_DATA(bh), data + from, bsize);
		if (buffer_new(bh)) {
			AFFS_DATA_HEAD(bh)->ptype = cpu_to_be32(T_DATA);
			AFFS_DATA_HEAD(bh)->key = cpu_to_be32(inode->i_ino);
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
				mark_buffer_dirty_inode(prev_bh, inode);
			}
		}
		affs_brelse(prev_bh);
		affs_fix_checksum(sb, bh);
		mark_buffer_dirty_inode(bh, inode);
		written += bsize;
		from += bsize;
		bidx++;
	}
	if (from < to) {
		prev_bh = bh;
		bh = affs_bread_ino(inode, bidx, 1);
		if (IS_ERR(bh))
			goto err_bh;
		tmp = min(bsize, to - from);
		BUG_ON(tmp > bsize);
		memcpy(AFFS_DATA(bh), data + from, tmp);
		if (buffer_new(bh)) {
			AFFS_DATA_HEAD(bh)->ptype = cpu_to_be32(T_DATA);
			AFFS_DATA_HEAD(bh)->key = cpu_to_be32(inode->i_ino);
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
				mark_buffer_dirty_inode(prev_bh, inode);
			}
		} else if (be32_to_cpu(AFFS_DATA_HEAD(bh)->size) < tmp)
			AFFS_DATA_HEAD(bh)->size = cpu_to_be32(tmp);
		affs_brelse(prev_bh);
		affs_fix_checksum(sb, bh);
		mark_buffer_dirty_inode(bh, inode);
		written += tmp;
		from += tmp;
		bidx++;
	}
	SetPageUptodate(page);

done:
	affs_brelse(bh);
	tmp = (page->index << PAGE_CACHE_SHIFT) + from;
	if (tmp > inode->i_size)
		inode->i_size = AFFS_I(inode)->mmu_private = tmp;

err_first_bh:
	unlock_page(page);
	page_cache_release(page);

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
affs_free_prealloc(struct inode *inode)
{
	struct super_block *sb = inode->i_sb;

	pr_debug("free_prealloc(ino=%lu)\n", inode->i_ino);

	while (AFFS_I(inode)->i_pa_cnt) {
		AFFS_I(inode)->i_pa_cnt--;
		affs_free_block(sb, ++AFFS_I(inode)->i_lastalloc);
	}
}

/* Truncate (or enlarge) a file to the requested size. */

void
affs_truncate(struct inode *inode)
{
	struct super_block *sb = inode->i_sb;
	u32 ext, ext_key;
	u32 last_blk, blkcnt, blk;
	u32 size;
	struct buffer_head *ext_bh;
	int i;

	pr_debug("truncate(inode=%lu, oldsize=%llu, newsize=%llu)\n",
		 inode->i_ino, AFFS_I(inode)->mmu_private, inode->i_size);

	last_blk = 0;
	ext = 0;
	if (inode->i_size) {
		last_blk = ((u32)inode->i_size - 1) / AFFS_SB(sb)->s_data_blksize;
		ext = last_blk / AFFS_SB(sb)->s_hashsize;
	}

	if (inode->i_size > AFFS_I(inode)->mmu_private) {
		struct address_space *mapping = inode->i_mapping;
		struct page *page;
		void *fsdata;
		loff_t isize = inode->i_size;
		int res;

		res = mapping->a_ops->write_begin(NULL, mapping, isize, 0, 0, &page, &fsdata);
		if (!res)
			res = mapping->a_ops->write_end(NULL, mapping, isize, 0, 0, page, fsdata);
		else
			inode->i_size = AFFS_I(inode)->mmu_private;
		mark_inode_dirty(inode);
		return;
	} else if (inode->i_size == AFFS_I(inode)->mmu_private)
		return;

	// lock cache
	ext_bh = affs_get_extblock(inode, ext);
	if (IS_ERR(ext_bh)) {
		affs_warning(sb, "truncate",
			     "unexpected read error for ext block %u (%ld)",
			     ext, PTR_ERR(ext_bh));
		return;
	}
	if (AFFS_I(inode)->i_lc) {
		/* clear linear cache */
		i = (ext + 1) >> AFFS_I(inode)->i_lc_shift;
		if (AFFS_I(inode)->i_lc_size > i) {
			AFFS_I(inode)->i_lc_size = i;
			for (; i < AFFS_LC_SIZE; i++)
				AFFS_I(inode)->i_lc[i] = 0;
		}
		/* clear associative cache */
		for (i = 0; i < AFFS_AC_SIZE; i++)
			if (AFFS_I(inode)->i_ac[i].ext >= ext)
				AFFS_I(inode)->i_ac[i].ext = 0;
	}
	ext_key = be32_to_cpu(AFFS_TAIL(sb, ext_bh)->extension);

	blkcnt = AFFS_I(inode)->i_blkcnt;
	i = 0;
	blk = last_blk;
	if (inode->i_size) {
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
	mark_buffer_dirty_inode(ext_bh, inode);
	affs_brelse(ext_bh);

	if (inode->i_size) {
		AFFS_I(inode)->i_blkcnt = last_blk + 1;
		AFFS_I(inode)->i_extcnt = ext + 1;
		if (AFFS_SB(sb)->s_flags & SF_OFS) {
			struct buffer_head *bh = affs_bread_ino(inode, last_blk, 0);
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
		AFFS_I(inode)->i_blkcnt = 0;
		AFFS_I(inode)->i_extcnt = 1;
	}
	AFFS_I(inode)->mmu_private = inode->i_size;
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
	affs_free_prealloc(inode);
}

int affs_file_fsync(struct file *filp, loff_t start, loff_t end, int datasync)
{
	struct inode *inode = filp->f_mapping->host;
	int ret, err;

	err = filemap_write_and_wait_range(inode->i_mapping, start, end);
	if (err)
		return err;

	mutex_lock(&inode->i_mutex);
	ret = write_inode_now(inode, 0);
	err = sync_blockdev(inode->i_sb->s_bdev);
	if (!ret)
		ret = err;
	mutex_unlock(&inode->i_mutex);
	return ret;
}
const struct file_operations affs_file_operations = {
	.llseek		= generic_file_llseek,
	.read		= new_sync_read,
	.read_iter	= generic_file_read_iter,
	.write		= new_sync_write,
	.write_iter	= generic_file_write_iter,
	.mmap		= generic_file_mmap,
	.open		= affs_file_open,
	.release	= affs_file_release,
	.fsync		= affs_file_fsync,
	.splice_read	= generic_file_splice_read,
};

const struct inode_operations affs_file_inode_operations = {
	.setattr	= affs_notify_change,
};

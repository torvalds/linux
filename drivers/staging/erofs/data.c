// SPDX-License-Identifier: GPL-2.0
/*
 * linux/drivers/staging/erofs/data.c
 *
 * Copyright (C) 2017-2018 HUAWEI, Inc.
 *             http://www.huawei.com/
 * Created by Gao Xiang <gaoxiang25@huawei.com>
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of the Linux
 * distribution for more details.
 */
#include "internal.h"
#include <linux/prefetch.h>

#include <trace/events/erofs.h>

static inline void read_endio(struct bio *bio)
{
	int i;
	struct bio_vec *bvec;
	const blk_status_t err = bio->bi_status;

	bio_for_each_segment_all(bvec, bio, i) {
		struct page *page = bvec->bv_page;

		/* page is already locked */
		BUG_ON(PageUptodate(page));

		if (unlikely(err))
			SetPageError(page);
		else
			SetPageUptodate(page);

		unlock_page(page);
		/* page could be reclaimed now */
	}
	bio_put(bio);
}

static void __submit_bio(struct bio *bio, unsigned op, unsigned op_flags)
{
	bio_set_op_attrs(bio, op, op_flags);
	submit_bio(bio);
}

static struct bio *prepare_bio(struct super_block *sb,
	erofs_blk_t blkaddr, unsigned nr_pages)
{
	struct bio *bio = bio_alloc(GFP_NOIO | __GFP_NOFAIL, nr_pages);

	BUG_ON(bio == NULL);

	bio->bi_end_io = read_endio;
	bio_set_dev(bio, sb->s_bdev);
	bio->bi_iter.bi_sector = blkaddr << LOG_SECTORS_PER_BLOCK;

	return bio;
}

/* prio -- true is used for dir */
struct page *erofs_get_meta_page(struct super_block *sb,
	erofs_blk_t blkaddr, bool prio)
{
	struct inode *bd_inode = sb->s_bdev->bd_inode;
	struct address_space *mapping = bd_inode->i_mapping;
	struct page *page;

repeat:
	page = find_or_create_page(mapping, blkaddr,
	/*
	 * Prefer looping in the allocator rather than here,
	 * at least that code knows what it's doing.
	 */
		mapping_gfp_constraint(mapping, ~__GFP_FS) | __GFP_NOFAIL);

	BUG_ON(!page || !PageLocked(page));

	if (!PageUptodate(page)) {
		struct bio *bio;
		int err;

		bio = prepare_bio(sb, blkaddr, 1);
		err = bio_add_page(bio, page, PAGE_SIZE, 0);
		BUG_ON(err != PAGE_SIZE);

		__submit_bio(bio, REQ_OP_READ,
			REQ_META | (prio ? REQ_PRIO : 0));

		lock_page(page);

		/* the page has been truncated by others? */
		if (unlikely(page->mapping != mapping)) {
			unlock_page(page);
			put_page(page);
			goto repeat;
		}

		/* more likely a read error */
		if (unlikely(!PageUptodate(page))) {
			unlock_page(page);
			put_page(page);

			page = ERR_PTR(-EIO);
		}
	}
	return page;
}

static int erofs_map_blocks_flatmode(struct inode *inode,
	struct erofs_map_blocks *map,
	int flags)
{
	erofs_blk_t nblocks, lastblk;
	u64 offset = map->m_la;
	struct erofs_vnode *vi = EROFS_V(inode);

	trace_erofs_map_blocks_flatmode_enter(inode, map, flags);
	BUG_ON(is_inode_layout_compression(inode));

	nblocks = DIV_ROUND_UP(inode->i_size, PAGE_SIZE);
	lastblk = nblocks - is_inode_layout_inline(inode);

	if (unlikely(offset >= inode->i_size)) {
		/* leave out-of-bound access unmapped */
		map->m_flags = 0;
		map->m_plen = 0;
		goto out;
	}

	/* there is no hole in flatmode */
	map->m_flags = EROFS_MAP_MAPPED;

	if (offset < blknr_to_addr(lastblk)) {
		map->m_pa = blknr_to_addr(vi->raw_blkaddr) + map->m_la;
		map->m_plen = blknr_to_addr(lastblk) - offset;
	} else if (is_inode_layout_inline(inode)) {
		/* 2 - inode inline B: inode, [xattrs], inline last blk... */
		struct erofs_sb_info *sbi = EROFS_SB(inode->i_sb);

		map->m_pa = iloc(sbi, vi->nid) + vi->inode_isize +
			vi->xattr_isize + erofs_blkoff(map->m_la);
		map->m_plen = inode->i_size - offset;

		/* inline data should locate in one meta block */
		BUG_ON(erofs_blkoff(map->m_pa) + map->m_plen > PAGE_SIZE);
		map->m_flags |= EROFS_MAP_META;
	} else {
		errln("internal error @ nid: %llu (size %llu), m_la 0x%llx",
			vi->nid, inode->i_size, map->m_la);
		BUG();
	}

out:
	map->m_llen = map->m_plen;
	trace_erofs_map_blocks_flatmode_exit(inode, map, flags, 0);
	return 0;
}

int erofs_map_blocks(struct inode *inode,
	struct erofs_map_blocks *map, int flags)
{
	if (unlikely(is_inode_layout_compression(inode)))
		return -ENOTSUPP;

	return erofs_map_blocks_flatmode(inode, map, flags);
}

static inline struct bio *erofs_read_raw_page(
	struct bio *bio,
	struct address_space *mapping,
	struct page *page,
	erofs_off_t *last_block,
	unsigned nblocks,
	bool ra)
{
	struct inode *inode = mapping->host;
	erofs_off_t current_block = (erofs_off_t)page->index;
	int err;

	BUG_ON(!nblocks);

	if (PageUptodate(page)) {
		err = 0;
		goto has_updated;
	}

	if (cleancache_get_page(page) == 0) {
		err = 0;
		SetPageUptodate(page);
		goto has_updated;
	}

	/* note that for readpage case, bio also equals to NULL */
	if (bio != NULL &&
		/* not continuous */
		*last_block + 1 != current_block) {
submit_bio_retry:
		__submit_bio(bio, REQ_OP_READ, 0);
		bio = NULL;
	}

	if (bio == NULL) {
		struct erofs_map_blocks map = {
			.m_la = blknr_to_addr(current_block),
		};

		err = erofs_map_blocks(inode, &map, EROFS_GET_BLOCKS_RAW);
		if (unlikely(err))
			goto err_out;

		/* zero out the holed page */
		if (unlikely(!(map.m_flags & EROFS_MAP_MAPPED))) {
			zero_user_segment(page, 0, PAGE_SIZE);
			SetPageUptodate(page);

			/* imply err = 0, see erofs_map_blocks */
			goto has_updated;
		}

		/* for RAW access mode, m_plen must be equal to m_llen */
		BUG_ON(map.m_plen != map.m_llen);

		/* deal with inline page */
		if (map.m_flags & EROFS_MAP_META) {
			void *vsrc, *vto;
			struct page *ipage;

			BUG_ON(map.m_plen > PAGE_SIZE);

			ipage = erofs_get_meta_page(inode->i_sb,
				erofs_blknr(map.m_pa), 0);

			if (IS_ERR(ipage)) {
				err = PTR_ERR(ipage);
				goto err_out;
			}

			vsrc = kmap_atomic(ipage);
			vto = kmap_atomic(page);
			memcpy(vto, vsrc + erofs_blkoff(map.m_pa), map.m_plen);
			memset(vto + map.m_plen, 0, PAGE_SIZE - map.m_plen);
			kunmap_atomic(vto);
			kunmap_atomic(vsrc);
			flush_dcache_page(page);

			SetPageUptodate(page);
			/* TODO: could we unlock the page earlier? */
			unlock_page(ipage);
			put_page(ipage);

			/* imply err = 0, see erofs_map_blocks */
			goto has_updated;
		}

		/* pa must be block-aligned for raw reading */
		BUG_ON(erofs_blkoff(map.m_pa) != 0);

		/* max # of continuous pages */
		if (nblocks > DIV_ROUND_UP(map.m_plen, PAGE_SIZE))
			nblocks = DIV_ROUND_UP(map.m_plen, PAGE_SIZE);
		if (nblocks > BIO_MAX_PAGES)
			nblocks = BIO_MAX_PAGES;

		bio = prepare_bio(inode->i_sb, erofs_blknr(map.m_pa), nblocks);
	}

	err = bio_add_page(bio, page, PAGE_SIZE, 0);
	/* out of the extent or bio is full */
	if (err < PAGE_SIZE)
		goto submit_bio_retry;

	*last_block = current_block;

	/* shift in advance in case of it followed by too many gaps */
	if (unlikely(bio->bi_vcnt >= bio->bi_max_vecs)) {
		/* err should reassign to 0 after submitting */
		err = 0;
		goto submit_bio_out;
	}

	return bio;

err_out:
	/* for sync reading, set page error immediately */
	if (!ra) {
		SetPageError(page);
		ClearPageUptodate(page);
	}
has_updated:
	unlock_page(page);

	/* if updated manually, continuous pages has a gap */
	if (bio != NULL)
submit_bio_out:
		__submit_bio(bio, REQ_OP_READ, 0);

	return unlikely(err) ? ERR_PTR(err) : NULL;
}

/*
 * since we dont have write or truncate flows, so no inode
 * locking needs to be held at the moment.
 */
static int erofs_raw_access_readpage(struct file *file, struct page *page)
{
	erofs_off_t last_block;
	struct bio *bio;

	trace_erofs_readpage(page, true);

	bio = erofs_read_raw_page(NULL, page->mapping,
		page, &last_block, 1, false);

	if (IS_ERR(bio))
		return PTR_ERR(bio);

	BUG_ON(bio != NULL);	/* since we have only one bio -- must be NULL */
	return 0;
}

static int erofs_raw_access_readpages(struct file *filp,
	struct address_space *mapping,
	struct list_head *pages, unsigned nr_pages)
{
	erofs_off_t last_block;
	struct bio *bio = NULL;
	gfp_t gfp = readahead_gfp_mask(mapping);
	struct page *page = list_last_entry(pages, struct page, lru);

	trace_erofs_readpages(mapping->host, page, nr_pages, true);

	for (; nr_pages; --nr_pages) {
		page = list_entry(pages->prev, struct page, lru);

		prefetchw(&page->flags);
		list_del(&page->lru);

		if (!add_to_page_cache_lru(page, mapping, page->index, gfp)) {
			bio = erofs_read_raw_page(bio, mapping, page,
				&last_block, nr_pages, true);

			/* all the page errors are ignored when readahead */
			if (IS_ERR(bio)) {
				pr_err("%s, readahead error at page %lu of nid %llu\n",
					__func__, page->index,
					EROFS_V(mapping->host)->nid);

				bio = NULL;
			}
		}

		/* pages could still be locked */
		put_page(page);
	}
	BUG_ON(!list_empty(pages));

	/* the rare case (end in gaps) */
	if (unlikely(bio != NULL))
		__submit_bio(bio, REQ_OP_READ, 0);
	return 0;
}

/* for uncompressed (aligned) files and raw access for other files */
const struct address_space_operations erofs_raw_access_aops = {
	.readpage = erofs_raw_access_readpage,
	.readpages = erofs_raw_access_readpages,
};


/*
 * GPL HEADER START
 *
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 only,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License version 2 for more details (a copy is included
 * in the LICENSE file that accompanied this code).
 *
 * You should have received a copy of the GNU General Public License
 * version 2 along with this program; If not, see
 * http://www.gnu.org/licenses/gpl-2.0.html
 *
 * GPL HEADER END
 */
/*
 * Copyright (c) 2002, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright (c) 2011, 2015, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 *
 * lustre/llite/rw.c
 *
 * Lustre Lite I/O page cache routines shared by different kernel revs
 */

#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/stat.h>
#include <linux/errno.h>
#include <linux/unistd.h>
#include <linux/writeback.h>
#include <linux/uaccess.h>

#include <linux/fs.h>
#include <linux/pagemap.h>
/* current_is_kswapd() */
#include <linux/swap.h>
#include <linux/bvec.h>

#define DEBUG_SUBSYSTEM S_LLITE

#include <obd_cksum.h>
#include "llite_internal.h"

static void ll_ra_stats_inc_sbi(struct ll_sb_info *sbi, enum ra_stat which);

/**
 * Get readahead pages from the filesystem readahead pool of the client for a
 * thread.
 *
 * /param sbi superblock for filesystem readahead state ll_ra_info
 * /param ria per-thread readahead state
 * /param pages number of pages requested for readahead for the thread.
 *
 * WARNING: This algorithm is used to reduce contention on sbi->ll_lock.
 * It should work well if the ra_max_pages is much greater than the single
 * file's read-ahead window, and not too many threads contending for
 * these readahead pages.
 *
 * TODO: There may be a 'global sync problem' if many threads are trying
 * to get an ra budget that is larger than the remaining readahead pages
 * and reach here at exactly the same time. They will compute /a ret to
 * consume the remaining pages, but will fail at atomic_add_return() and
 * get a zero ra window, although there is still ra space remaining. - Jay
 */
static unsigned long ll_ra_count_get(struct ll_sb_info *sbi,
				     struct ra_io_arg *ria,
				     unsigned long pages, unsigned long min)
{
	struct ll_ra_info *ra = &sbi->ll_ra_info;
	long ret;

	/* If read-ahead pages left are less than 1M, do not do read-ahead,
	 * otherwise it will form small read RPC(< 1M), which hurt server
	 * performance a lot.
	 */
	ret = min(ra->ra_max_pages - atomic_read(&ra->ra_cur_pages), pages);
	if (ret < 0 || ret < min_t(long, PTLRPC_MAX_BRW_PAGES, pages)) {
		ret = 0;
		goto out;
	}

	if (atomic_add_return(ret, &ra->ra_cur_pages) > ra->ra_max_pages) {
		atomic_sub(ret, &ra->ra_cur_pages);
		ret = 0;
	}

out:
	if (ret < min) {
		/* override ra limit for maximum performance */
		atomic_add(min - ret, &ra->ra_cur_pages);
		ret = min;
	}
	return ret;
}

void ll_ra_count_put(struct ll_sb_info *sbi, unsigned long len)
{
	struct ll_ra_info *ra = &sbi->ll_ra_info;

	atomic_sub(len, &ra->ra_cur_pages);
}

static void ll_ra_stats_inc_sbi(struct ll_sb_info *sbi, enum ra_stat which)
{
	LASSERTF(which < _NR_RA_STAT, "which: %u\n", which);
	lprocfs_counter_incr(sbi->ll_ra_stats, which);
}

void ll_ra_stats_inc(struct inode *inode, enum ra_stat which)
{
	struct ll_sb_info *sbi = ll_i2sbi(inode);

	ll_ra_stats_inc_sbi(sbi, which);
}

#define RAS_CDEBUG(ras) \
	CDEBUG(D_READA,						      \
	       "lrp %lu cr %lu cp %lu ws %lu wl %lu nra %lu rpc %lu "	     \
	       "r %lu ri %lu csr %lu sf %lu sp %lu sl %lu\n",		     \
	       ras->ras_last_readpage, ras->ras_consecutive_requests,	\
	       ras->ras_consecutive_pages, ras->ras_window_start,	    \
	       ras->ras_window_len, ras->ras_next_readahead,		 \
	       ras->ras_rpc_size,					     \
	       ras->ras_requests, ras->ras_request_index,		    \
	       ras->ras_consecutive_stride_requests, ras->ras_stride_offset, \
	       ras->ras_stride_pages, ras->ras_stride_length)

static int index_in_window(unsigned long index, unsigned long point,
			   unsigned long before, unsigned long after)
{
	unsigned long start = point - before, end = point + after;

	if (start > point)
		start = 0;
	if (end < point)
		end = ~0;

	return start <= index && index <= end;
}

void ll_ras_enter(struct file *f)
{
	struct ll_file_data *fd = LUSTRE_FPRIVATE(f);
	struct ll_readahead_state *ras = &fd->fd_ras;

	spin_lock(&ras->ras_lock);
	ras->ras_requests++;
	ras->ras_request_index = 0;
	ras->ras_consecutive_requests++;
	spin_unlock(&ras->ras_lock);
}

/**
 * Initiates read-ahead of a page with given index.
 *
 * \retval +ve:	page was already uptodate so it will be skipped
 *		from being added;
 * \retval -ve:	page wasn't added to \a queue for error;
 * \retval   0:	page was added into \a queue for read ahead.
 */
static int ll_read_ahead_page(const struct lu_env *env, struct cl_io *io,
			      struct cl_page_list *queue, pgoff_t index)
{
	enum ra_stat which = _NR_RA_STAT; /* keep gcc happy */
	struct cl_object *clob = io->ci_obj;
	struct inode *inode = vvp_object_inode(clob);
	const char *msg = NULL;
	struct cl_page *page;
	struct vvp_page *vpg;
	struct page *vmpage;
	int rc = 0;

	vmpage = grab_cache_page_nowait(inode->i_mapping, index);
	if (!vmpage) {
		which = RA_STAT_FAILED_GRAB_PAGE;
		msg = "g_c_p_n failed";
		rc = -EBUSY;
		goto out;
	}

	/* Check if vmpage was truncated or reclaimed */
	if (vmpage->mapping != inode->i_mapping) {
		which = RA_STAT_WRONG_GRAB_PAGE;
		msg = "g_c_p_n returned invalid page";
		rc = -EBUSY;
		goto out;
	}

	page = cl_page_find(env, clob, vmpage->index, vmpage, CPT_CACHEABLE);
	if (IS_ERR(page)) {
		which = RA_STAT_FAILED_GRAB_PAGE;
		msg = "cl_page_find failed";
		rc = PTR_ERR(page);
		goto out;
	}

	lu_ref_add(&page->cp_reference, "ra", current);
	cl_page_assume(env, io, page);
	vpg = cl2vvp_page(cl_object_page_slice(clob, page));
	if (!vpg->vpg_defer_uptodate && !PageUptodate(vmpage)) {
		vpg->vpg_defer_uptodate = 1;
		vpg->vpg_ra_used = 0;
		cl_page_list_add(queue, page);
	} else {
		/* skip completed pages */
		cl_page_unassume(env, io, page);
		/* This page is already uptodate, returning a positive number
		 * to tell the callers about this
		 */
		rc = 1;
	}

	lu_ref_del(&page->cp_reference, "ra", current);
	cl_page_put(env, page);
out:
	if (vmpage) {
		if (rc)
			unlock_page(vmpage);
		put_page(vmpage);
	}
	if (msg) {
		ll_ra_stats_inc(inode, which);
		CDEBUG(D_READA, "%s\n", msg);
	}
	return rc;
}

#define RIA_DEBUG(ria)						       \
	CDEBUG(D_READA, "rs %lu re %lu ro %lu rl %lu rp %lu\n",       \
	ria->ria_start, ria->ria_end, ria->ria_stoff, ria->ria_length,\
	ria->ria_pages)

static inline int stride_io_mode(struct ll_readahead_state *ras)
{
	return ras->ras_consecutive_stride_requests > 1;
}

/* The function calculates how much pages will be read in
 * [off, off + length], in such stride IO area,
 * stride_offset = st_off, stride_length = st_len,
 * stride_pages = st_pgs
 *
 *   |------------------|*****|------------------|*****|------------|*****|....
 * st_off
 *   |--- st_pgs     ---|
 *   |-----     st_len   -----|
 *
 *	      How many pages it should read in such pattern
 *	      |-------------------------------------------------------------|
 *	      off
 *	      |<------		  length		      ------->|
 *
 *	  =   |<----->|  +  |-------------------------------------| +   |---|
 *	     start_left		 st_pgs * i		    end_left
 */
static unsigned long
stride_pg_count(pgoff_t st_off, unsigned long st_len, unsigned long st_pgs,
		unsigned long off, unsigned long length)
{
	__u64 start = off > st_off ? off - st_off : 0;
	__u64 end = off + length > st_off ? off + length - st_off : 0;
	unsigned long start_left = 0;
	unsigned long end_left = 0;
	unsigned long pg_count;

	if (st_len == 0 || length == 0 || end == 0)
		return length;

	start_left = do_div(start, st_len);
	if (start_left < st_pgs)
		start_left = st_pgs - start_left;
	else
		start_left = 0;

	end_left = do_div(end, st_len);
	if (end_left > st_pgs)
		end_left = st_pgs;

	CDEBUG(D_READA, "start %llu, end %llu start_left %lu end_left %lu\n",
	       start, end, start_left, end_left);

	if (start == end)
		pg_count = end_left - (st_pgs - start_left);
	else
		pg_count = start_left + st_pgs * (end - start - 1) + end_left;

	CDEBUG(D_READA, "st_off %lu, st_len %lu st_pgs %lu off %lu length %lu pgcount %lu\n",
	       st_off, st_len, st_pgs, off, length, pg_count);

	return pg_count;
}

static int ria_page_count(struct ra_io_arg *ria)
{
	__u64 length = ria->ria_end >= ria->ria_start ?
		       ria->ria_end - ria->ria_start + 1 : 0;

	return stride_pg_count(ria->ria_stoff, ria->ria_length,
			       ria->ria_pages, ria->ria_start,
			       length);
}

static unsigned long ras_align(struct ll_readahead_state *ras,
			       unsigned long index,
			       unsigned long *remainder)
{
	unsigned long rem = index % ras->ras_rpc_size;

	if (remainder)
		*remainder = rem;
	return index - rem;
}

/*Check whether the index is in the defined ra-window */
static int ras_inside_ra_window(unsigned long idx, struct ra_io_arg *ria)
{
	/* If ria_length == ria_pages, it means non-stride I/O mode,
	 * idx should always inside read-ahead window in this case
	 * For stride I/O mode, just check whether the idx is inside
	 * the ria_pages.
	 */
	return ria->ria_length == 0 || ria->ria_length == ria->ria_pages ||
	       (idx >= ria->ria_stoff && (idx - ria->ria_stoff) %
		ria->ria_length < ria->ria_pages);
}

static unsigned long
ll_read_ahead_pages(const struct lu_env *env, struct cl_io *io,
		    struct cl_page_list *queue, struct ll_readahead_state *ras,
		    struct ra_io_arg *ria)
{
	struct cl_read_ahead ra = { 0 };
	unsigned long ra_end = 0;
	bool stride_ria;
	pgoff_t page_idx;
	int rc;

	LASSERT(ria);
	RIA_DEBUG(ria);

	stride_ria = ria->ria_length > ria->ria_pages && ria->ria_pages > 0;
	for (page_idx = ria->ria_start;
	     page_idx <= ria->ria_end && ria->ria_reserved > 0; page_idx++) {
		if (ras_inside_ra_window(page_idx, ria)) {
			if (!ra.cra_end || ra.cra_end < page_idx) {
				unsigned long end;

				cl_read_ahead_release(env, &ra);

				rc = cl_io_read_ahead(env, io, page_idx, &ra);
				if (rc < 0)
					break;

				CDEBUG(D_READA, "idx: %lu, ra: %lu, rpc: %lu\n",
				       page_idx, ra.cra_end, ra.cra_rpc_size);
				LASSERTF(ra.cra_end >= page_idx,
					 "object: %p, indcies %lu / %lu\n",
					 io->ci_obj, ra.cra_end, page_idx);
				/*
				 * update read ahead RPC size.
				 * NB: it's racy but doesn't matter
				 */
				if (ras->ras_rpc_size > ra.cra_rpc_size &&
				    ra.cra_rpc_size > 0)
					ras->ras_rpc_size = ra.cra_rpc_size;
				/* trim it to align with optimal RPC size */
				end = ras_align(ras, ria->ria_end + 1, NULL);
				if (end > 0 && !ria->ria_eof)
					ria->ria_end = end - 1;
				if (ria->ria_end < ria->ria_end_min)
					ria->ria_end = ria->ria_end_min;
				if (ria->ria_end > ra.cra_end)
					ria->ria_end = ra.cra_end;
			}

			/* If the page is inside the read-ahead window */
			rc = ll_read_ahead_page(env, io, queue, page_idx);
			if (rc < 0)
				break;

			ra_end = page_idx;
			if (!rc)
				ria->ria_reserved--;
		} else if (stride_ria) {
			/* If it is not in the read-ahead window, and it is
			 * read-ahead mode, then check whether it should skip
			 * the stride gap
			 */
			pgoff_t offset;
			/* FIXME: This assertion only is valid when it is for
			 * forward read-ahead, it will be fixed when backward
			 * read-ahead is implemented
			 */
			LASSERTF(page_idx >= ria->ria_stoff, "Invalid page_idx %lu rs %lu re %lu ro %lu rl %lu rp %lu\n",
				 page_idx,
				 ria->ria_start, ria->ria_end, ria->ria_stoff,
				 ria->ria_length, ria->ria_pages);
			offset = page_idx - ria->ria_stoff;
			offset = offset % (ria->ria_length);
			if (offset > ria->ria_pages) {
				page_idx += ria->ria_length - offset;
				CDEBUG(D_READA, "i %lu skip %lu\n", page_idx,
				       ria->ria_length - offset);
				continue;
			}
		}
	}
	cl_read_ahead_release(env, &ra);

	return ra_end;
}

static int ll_readahead(const struct lu_env *env, struct cl_io *io,
			struct cl_page_list *queue,
			struct ll_readahead_state *ras, bool hit)
{
	struct vvp_io *vio = vvp_env_io(env);
	struct ll_thread_info *lti = ll_env_info(env);
	struct cl_attr *attr = vvp_env_thread_attr(env);
	unsigned long len, mlen = 0;
	pgoff_t ra_end, start = 0, end = 0;
	struct inode *inode;
	struct ra_io_arg *ria = &lti->lti_ria;
	struct cl_object *clob;
	int ret = 0;
	__u64 kms;

	clob = io->ci_obj;
	inode = vvp_object_inode(clob);

	memset(ria, 0, sizeof(*ria));

	cl_object_attr_lock(clob);
	ret = cl_object_attr_get(env, clob, attr);
	cl_object_attr_unlock(clob);

	if (ret != 0)
		return ret;
	kms = attr->cat_kms;
	if (kms == 0) {
		ll_ra_stats_inc(inode, RA_STAT_ZERO_LEN);
		return 0;
	}

	spin_lock(&ras->ras_lock);

	/**
	 * Note: other thread might rollback the ras_next_readahead,
	 * if it can not get the full size of prepared pages, see the
	 * end of this function. For stride read ahead, it needs to
	 * make sure the offset is no less than ras_stride_offset,
	 * so that stride read ahead can work correctly.
	 */
	if (stride_io_mode(ras))
		start = max(ras->ras_next_readahead, ras->ras_stride_offset);
	else
		start = ras->ras_next_readahead;

	if (ras->ras_window_len > 0)
		end = ras->ras_window_start + ras->ras_window_len - 1;

	/* Enlarge the RA window to encompass the full read */
	if (vio->vui_ra_valid &&
	    end < vio->vui_ra_start + vio->vui_ra_count - 1)
		end = vio->vui_ra_start + vio->vui_ra_count - 1;

	if (end) {
		unsigned long end_index;

		/* Truncate RA window to end of file */
		end_index = (unsigned long)((kms - 1) >> PAGE_SHIFT);
		if (end_index <= end) {
			end = end_index;
			ria->ria_eof = true;
		}

		ras->ras_next_readahead = max(end, end + 1);
		RAS_CDEBUG(ras);
	}
	ria->ria_start = start;
	ria->ria_end = end;
	/* If stride I/O mode is detected, get stride window*/
	if (stride_io_mode(ras)) {
		ria->ria_stoff = ras->ras_stride_offset;
		ria->ria_length = ras->ras_stride_length;
		ria->ria_pages = ras->ras_stride_pages;
	}
	spin_unlock(&ras->ras_lock);

	if (end == 0) {
		ll_ra_stats_inc(inode, RA_STAT_ZERO_WINDOW);
		return 0;
	}
	len = ria_page_count(ria);
	if (len == 0) {
		ll_ra_stats_inc(inode, RA_STAT_ZERO_WINDOW);
		return 0;
	}

	CDEBUG(D_READA, DFID ": ria: %lu/%lu, bead: %lu/%lu, hit: %d\n",
	       PFID(lu_object_fid(&clob->co_lu)),
	       ria->ria_start, ria->ria_end,
	       vio->vui_ra_valid ? vio->vui_ra_start : 0,
	       vio->vui_ra_valid ? vio->vui_ra_count : 0,
	       hit);

	/* at least to extend the readahead window to cover current read */
	if (!hit && vio->vui_ra_valid &&
	    vio->vui_ra_start + vio->vui_ra_count > ria->ria_start) {
		unsigned long remainder;

		/* to the end of current read window. */
		mlen = vio->vui_ra_start + vio->vui_ra_count - ria->ria_start;
		/* trim to RPC boundary */
		ras_align(ras, ria->ria_start, &remainder);
		mlen = min(mlen, ras->ras_rpc_size - remainder);
		ria->ria_end_min = ria->ria_start + mlen;
	}

	ria->ria_reserved = ll_ra_count_get(ll_i2sbi(inode), ria, len, mlen);
	if (ria->ria_reserved < len)
		ll_ra_stats_inc(inode, RA_STAT_MAX_IN_FLIGHT);

	CDEBUG(D_READA, "reserved pages %lu/%lu/%lu, ra_cur %d, ra_max %lu\n",
	       ria->ria_reserved, len, mlen,
	       atomic_read(&ll_i2sbi(inode)->ll_ra_info.ra_cur_pages),
	       ll_i2sbi(inode)->ll_ra_info.ra_max_pages);

	ra_end = ll_read_ahead_pages(env, io, queue, ras, ria);

	if (ria->ria_reserved)
		ll_ra_count_put(ll_i2sbi(inode), ria->ria_reserved);

	if (ra_end == end && ra_end == (kms >> PAGE_SHIFT))
		ll_ra_stats_inc(inode, RA_STAT_EOF);

	/* if we didn't get to the end of the region we reserved from
	 * the ras we need to go back and update the ras so that the
	 * next read-ahead tries from where we left off.  we only do so
	 * if the region we failed to issue read-ahead on is still ahead
	 * of the app and behind the next index to start read-ahead from
	 */
	CDEBUG(D_READA, "ra_end = %lu end = %lu stride end = %lu pages = %d\n",
	       ra_end, end, ria->ria_end, ret);

	if (ra_end > 0 && ra_end != end) {
		ll_ra_stats_inc(inode, RA_STAT_FAILED_REACH_END);
		spin_lock(&ras->ras_lock);
		if (ra_end <= ras->ras_next_readahead &&
		    index_in_window(ra_end, ras->ras_window_start, 0,
				    ras->ras_window_len)) {
			ras->ras_next_readahead = ra_end + 1;
			RAS_CDEBUG(ras);
		}
		spin_unlock(&ras->ras_lock);
	}

	return ret;
}

static void ras_set_start(struct inode *inode, struct ll_readahead_state *ras,
			  unsigned long index)
{
	ras->ras_window_start = ras_align(ras, index, NULL);
}

/* called with the ras_lock held or from places where it doesn't matter */
static void ras_reset(struct inode *inode, struct ll_readahead_state *ras,
		      unsigned long index)
{
	ras->ras_last_readpage = index;
	ras->ras_consecutive_requests = 0;
	ras->ras_consecutive_pages = 0;
	ras->ras_window_len = 0;
	ras_set_start(inode, ras, index);
	ras->ras_next_readahead = max(ras->ras_window_start, index + 1);

	RAS_CDEBUG(ras);
}

/* called with the ras_lock held or from places where it doesn't matter */
static void ras_stride_reset(struct ll_readahead_state *ras)
{
	ras->ras_consecutive_stride_requests = 0;
	ras->ras_stride_length = 0;
	ras->ras_stride_pages = 0;
	RAS_CDEBUG(ras);
}

void ll_readahead_init(struct inode *inode, struct ll_readahead_state *ras)
{
	spin_lock_init(&ras->ras_lock);
	ras->ras_rpc_size = PTLRPC_MAX_BRW_PAGES;
	ras_reset(inode, ras, 0);
	ras->ras_requests = 0;
}

/*
 * Check whether the read request is in the stride window.
 * If it is in the stride window, return 1, otherwise return 0.
 */
static int index_in_stride_window(struct ll_readahead_state *ras,
				  unsigned long index)
{
	unsigned long stride_gap;

	if (ras->ras_stride_length == 0 || ras->ras_stride_pages == 0 ||
	    ras->ras_stride_pages == ras->ras_stride_length)
		return 0;

	stride_gap = index - ras->ras_last_readpage - 1;

	/* If it is contiguous read */
	if (stride_gap == 0)
		return ras->ras_consecutive_pages + 1 <= ras->ras_stride_pages;

	/* Otherwise check the stride by itself */
	return (ras->ras_stride_length - ras->ras_stride_pages) == stride_gap &&
		ras->ras_consecutive_pages == ras->ras_stride_pages;
}

static void ras_update_stride_detector(struct ll_readahead_state *ras,
				       unsigned long index)
{
	unsigned long stride_gap = index - ras->ras_last_readpage - 1;

	if ((stride_gap != 0 || ras->ras_consecutive_stride_requests == 0) &&
	    !stride_io_mode(ras)) {
		ras->ras_stride_pages = ras->ras_consecutive_pages;
		ras->ras_stride_length = ras->ras_consecutive_pages +
					 stride_gap;
	}
	LASSERT(ras->ras_request_index == 0);
	LASSERT(ras->ras_consecutive_stride_requests == 0);

	if (index <= ras->ras_last_readpage) {
		/*Reset stride window for forward read*/
		ras_stride_reset(ras);
		return;
	}

	ras->ras_stride_pages = ras->ras_consecutive_pages;
	ras->ras_stride_length = stride_gap + ras->ras_consecutive_pages;

	RAS_CDEBUG(ras);
}

/* Stride Read-ahead window will be increased inc_len according to
 * stride I/O pattern
 */
static void ras_stride_increase_window(struct ll_readahead_state *ras,
				       struct ll_ra_info *ra,
				       unsigned long inc_len)
{
	unsigned long left, step, window_len;
	unsigned long stride_len;

	LASSERT(ras->ras_stride_length > 0);
	LASSERTF(ras->ras_window_start + ras->ras_window_len
		 >= ras->ras_stride_offset, "window_start %lu, window_len %lu stride_offset %lu\n",
		 ras->ras_window_start,
		 ras->ras_window_len, ras->ras_stride_offset);

	stride_len = ras->ras_window_start + ras->ras_window_len -
		     ras->ras_stride_offset;

	left = stride_len % ras->ras_stride_length;
	window_len = ras->ras_window_len - left;

	if (left < ras->ras_stride_pages)
		left += inc_len;
	else
		left = ras->ras_stride_pages + inc_len;

	LASSERT(ras->ras_stride_pages != 0);

	step = left / ras->ras_stride_pages;
	left %= ras->ras_stride_pages;

	window_len += step * ras->ras_stride_length + left;

	if (stride_pg_count(ras->ras_stride_offset, ras->ras_stride_length,
			    ras->ras_stride_pages, ras->ras_stride_offset,
			    window_len) <= ra->ra_max_pages_per_file)
		ras->ras_window_len = window_len;

	RAS_CDEBUG(ras);
}

static void ras_increase_window(struct inode *inode,
				struct ll_readahead_state *ras,
				struct ll_ra_info *ra)
{
	/* The stretch of ra-window should be aligned with max rpc_size
	 * but current clio architecture does not support retrieve such
	 * information from lower layer. FIXME later
	 */
	if (stride_io_mode(ras)) {
		ras_stride_increase_window(ras, ra, ras->ras_rpc_size);
	} else {
		unsigned long wlen;

		wlen = min(ras->ras_window_len + ras->ras_rpc_size,
			   ra->ra_max_pages_per_file);
		ras->ras_window_len = ras_align(ras, wlen, NULL);
	}
}

static void ras_update(struct ll_sb_info *sbi, struct inode *inode,
		       struct ll_readahead_state *ras, unsigned long index,
		       enum ras_update_flags flags)
{
	struct ll_ra_info *ra = &sbi->ll_ra_info;
	int zero = 0, stride_detect = 0, ra_miss = 0;
	bool hit = flags & LL_RAS_HIT;

	spin_lock(&ras->ras_lock);

	if (!hit)
		CDEBUG(D_READA, DFID " pages at %lu miss.\n",
		       PFID(ll_inode2fid(inode)), index);

	ll_ra_stats_inc_sbi(sbi, hit ? RA_STAT_HIT : RA_STAT_MISS);

	/* reset the read-ahead window in two cases.  First when the app seeks
	 * or reads to some other part of the file.  Secondly if we get a
	 * read-ahead miss that we think we've previously issued.  This can
	 * be a symptom of there being so many read-ahead pages that the VM is
	 * reclaiming it before we get to it.
	 */
	if (!index_in_window(index, ras->ras_last_readpage, 8, 8)) {
		zero = 1;
		ll_ra_stats_inc_sbi(sbi, RA_STAT_DISTANT_READPAGE);
	} else if (!hit && ras->ras_window_len &&
		   index < ras->ras_next_readahead &&
		   index_in_window(index, ras->ras_window_start, 0,
				   ras->ras_window_len)) {
		ra_miss = 1;
		ll_ra_stats_inc_sbi(sbi, RA_STAT_MISS_IN_WINDOW);
	}

	/* On the second access to a file smaller than the tunable
	 * ra_max_read_ahead_whole_pages trigger RA on all pages in the
	 * file up to ra_max_pages_per_file.  This is simply a best effort
	 * and only occurs once per open file.  Normal RA behavior is reverted
	 * to for subsequent IO.  The mmap case does not increment
	 * ras_requests and thus can never trigger this behavior.
	 */
	if (ras->ras_requests >= 2 && !ras->ras_request_index) {
		__u64 kms_pages;

		kms_pages = (i_size_read(inode) + PAGE_SIZE - 1) >>
			    PAGE_SHIFT;

		CDEBUG(D_READA, "kmsp %llu mwp %lu mp %lu\n", kms_pages,
		       ra->ra_max_read_ahead_whole_pages, ra->ra_max_pages_per_file);

		if (kms_pages &&
		    kms_pages <= ra->ra_max_read_ahead_whole_pages) {
			ras->ras_window_start = 0;
			ras->ras_next_readahead = index + 1;
			ras->ras_window_len = min(ra->ra_max_pages_per_file,
				ra->ra_max_read_ahead_whole_pages);
			goto out_unlock;
		}
	}
	if (zero) {
		/* check whether it is in stride I/O mode*/
		if (!index_in_stride_window(ras, index)) {
			if (ras->ras_consecutive_stride_requests == 0 &&
			    ras->ras_request_index == 0) {
				ras_update_stride_detector(ras, index);
				ras->ras_consecutive_stride_requests++;
			} else {
				ras_stride_reset(ras);
			}
			ras_reset(inode, ras, index);
			ras->ras_consecutive_pages++;
			goto out_unlock;
		} else {
			ras->ras_consecutive_pages = 0;
			ras->ras_consecutive_requests = 0;
			if (++ras->ras_consecutive_stride_requests > 1)
				stride_detect = 1;
			RAS_CDEBUG(ras);
		}
	} else {
		if (ra_miss) {
			if (index_in_stride_window(ras, index) &&
			    stride_io_mode(ras)) {
				if (index != ras->ras_last_readpage + 1)
					ras->ras_consecutive_pages = 0;
				ras_reset(inode, ras, index);

				/* If stride-RA hit cache miss, the stride
				 * detector will not be reset to avoid the
				 * overhead of redetecting read-ahead mode,
				 * but on the condition that the stride window
				 * is still intersect with normal sequential
				 * read-ahead window.
				 */
				if (ras->ras_window_start <
				    ras->ras_stride_offset)
					ras_stride_reset(ras);
				RAS_CDEBUG(ras);
			} else {
				/* Reset both stride window and normal RA
				 * window
				 */
				ras_reset(inode, ras, index);
				ras->ras_consecutive_pages++;
				ras_stride_reset(ras);
				goto out_unlock;
			}
		} else if (stride_io_mode(ras)) {
			/* If this is contiguous read but in stride I/O mode
			 * currently, check whether stride step still is valid,
			 * if invalid, it will reset the stride ra window
			 */
			if (!index_in_stride_window(ras, index)) {
				/* Shrink stride read-ahead window to be zero */
				ras_stride_reset(ras);
				ras->ras_window_len = 0;
				ras->ras_next_readahead = index;
			}
		}
	}
	ras->ras_consecutive_pages++;
	ras->ras_last_readpage = index;
	ras_set_start(inode, ras, index);

	if (stride_io_mode(ras)) {
		/* Since stride readahead is sensitive to the offset
		 * of read-ahead, so we use original offset here,
		 * instead of ras_window_start, which is RPC aligned
		 */
		ras->ras_next_readahead = max(index, ras->ras_next_readahead);
		ras->ras_window_start = max(ras->ras_stride_offset,
					    ras->ras_window_start);
	} else {
		if (ras->ras_next_readahead < ras->ras_window_start)
			ras->ras_next_readahead = ras->ras_window_start;
		if (!hit)
			ras->ras_next_readahead = index + 1;
	}
	RAS_CDEBUG(ras);

	/* Trigger RA in the mmap case where ras_consecutive_requests
	 * is not incremented and thus can't be used to trigger RA
	 */
	if (ras->ras_consecutive_pages >= 4 && flags & LL_RAS_MMAP) {
		ras_increase_window(inode, ras, ra);
		/*
		 * reset consecutive pages so that the readahead window can
		 * grow gradually.
		 */
		ras->ras_consecutive_pages = 0;
		goto out_unlock;
	}

	/* Initially reset the stride window offset to next_readahead*/
	if (ras->ras_consecutive_stride_requests == 2 && stride_detect) {
		/**
		 * Once stride IO mode is detected, next_readahead should be
		 * reset to make sure next_readahead > stride offset
		 */
		ras->ras_next_readahead = max(index, ras->ras_next_readahead);
		ras->ras_stride_offset = index;
		ras->ras_window_start = max(index, ras->ras_window_start);
	}

	/* The initial ras_window_len is set to the request size.  To avoid
	 * uselessly reading and discarding pages for random IO the window is
	 * only increased once per consecutive request received. */
	if ((ras->ras_consecutive_requests > 1 || stride_detect) &&
	    !ras->ras_request_index)
		ras_increase_window(inode, ras, ra);
out_unlock:
	RAS_CDEBUG(ras);
	ras->ras_request_index++;
	spin_unlock(&ras->ras_lock);
}

int ll_writepage(struct page *vmpage, struct writeback_control *wbc)
{
	struct inode	       *inode = vmpage->mapping->host;
	struct ll_inode_info   *lli   = ll_i2info(inode);
	struct lu_env	  *env;
	struct cl_io	   *io;
	struct cl_page	 *page;
	struct cl_object       *clob;
	bool redirtied = false;
	bool unlocked = false;
	int result;
	u16 refcheck;

	LASSERT(PageLocked(vmpage));
	LASSERT(!PageWriteback(vmpage));

	LASSERT(ll_i2dtexp(inode));

	env = cl_env_get(&refcheck);
	if (IS_ERR(env)) {
		result = PTR_ERR(env);
		goto out;
	}

	clob  = ll_i2info(inode)->lli_clob;
	LASSERT(clob);

	io = vvp_env_thread_io(env);
	io->ci_obj = clob;
	io->ci_ignore_layout = 1;
	result = cl_io_init(env, io, CIT_MISC, clob);
	if (result == 0) {
		page = cl_page_find(env, clob, vmpage->index,
				    vmpage, CPT_CACHEABLE);
		if (!IS_ERR(page)) {
			lu_ref_add(&page->cp_reference, "writepage",
				   current);
			cl_page_assume(env, io, page);
			result = cl_page_flush(env, io, page);
			if (result != 0) {
				/*
				 * Re-dirty page on error so it retries write,
				 * but not in case when IO has actually
				 * occurred and completed with an error.
				 */
				if (!PageError(vmpage)) {
					redirty_page_for_writepage(wbc, vmpage);
					result = 0;
					redirtied = true;
				}
			}
			cl_page_disown(env, io, page);
			unlocked = true;
			lu_ref_del(&page->cp_reference,
				   "writepage", current);
			cl_page_put(env, page);
		} else {
			result = PTR_ERR(page);
		}
	}
	cl_io_fini(env, io);

	if (redirtied && wbc->sync_mode == WB_SYNC_ALL) {
		loff_t offset = cl_offset(clob, vmpage->index);

		/* Flush page failed because the extent is being written out.
		 * Wait for the write of extent to be finished to avoid
		 * breaking kernel which assumes ->writepage should mark
		 * PageWriteback or clean the page.
		 */
		result = cl_sync_file_range(inode, offset,
					    offset + PAGE_SIZE - 1,
					    CL_FSYNC_LOCAL, 1);
		if (result > 0) {
			/* actually we may have written more than one page.
			 * decreasing this page because the caller will count
			 * it.
			 */
			wbc->nr_to_write -= result - 1;
			result = 0;
		}
	}

	cl_env_put(env, &refcheck);
	goto out;

out:
	if (result < 0) {
		if (!lli->lli_async_rc)
			lli->lli_async_rc = result;
		SetPageError(vmpage);
		if (!unlocked)
			unlock_page(vmpage);
	}
	return result;
}

int ll_writepages(struct address_space *mapping, struct writeback_control *wbc)
{
	struct inode *inode = mapping->host;
	struct ll_sb_info *sbi = ll_i2sbi(inode);
	loff_t start;
	loff_t end;
	enum cl_fsync_mode mode;
	int range_whole = 0;
	int result;
	int ignore_layout = 0;

	if (wbc->range_cyclic) {
		start = mapping->writeback_index << PAGE_SHIFT;
		end = OBD_OBJECT_EOF;
	} else {
		start = wbc->range_start;
		end = wbc->range_end;
		if (end == LLONG_MAX) {
			end = OBD_OBJECT_EOF;
			range_whole = start == 0;
		}
	}

	mode = CL_FSYNC_NONE;
	if (wbc->sync_mode == WB_SYNC_ALL)
		mode = CL_FSYNC_LOCAL;

	if (sbi->ll_umounting)
		/* if the mountpoint is being umounted, all pages have to be
		 * evicted to avoid hitting LBUG when truncate_inode_pages()
		 * is called later on.
		 */
		ignore_layout = 1;

	if (!ll_i2info(inode)->lli_clob)
		return 0;

	result = cl_sync_file_range(inode, start, end, mode, ignore_layout);
	if (result > 0) {
		wbc->nr_to_write -= result;
		result = 0;
	}

	if (wbc->range_cyclic || (range_whole && wbc->nr_to_write > 0)) {
		if (end == OBD_OBJECT_EOF)
			mapping->writeback_index = 0;
		else
			mapping->writeback_index = (end >> PAGE_SHIFT) + 1;
	}
	return result;
}

struct ll_cl_context *ll_cl_find(struct file *file)
{
	struct ll_file_data *fd = LUSTRE_FPRIVATE(file);
	struct ll_cl_context *lcc;
	struct ll_cl_context *found = NULL;

	read_lock(&fd->fd_lock);
	list_for_each_entry(lcc, &fd->fd_lccs, lcc_list) {
		if (lcc->lcc_cookie == current) {
			found = lcc;
			break;
		}
	}
	read_unlock(&fd->fd_lock);

	return found;
}

void ll_cl_add(struct file *file, const struct lu_env *env, struct cl_io *io)
{
	struct ll_file_data *fd = LUSTRE_FPRIVATE(file);
	struct ll_cl_context *lcc = &ll_env_info(env)->lti_io_ctx;

	memset(lcc, 0, sizeof(*lcc));
	INIT_LIST_HEAD(&lcc->lcc_list);
	lcc->lcc_cookie = current;
	lcc->lcc_env = env;
	lcc->lcc_io = io;

	write_lock(&fd->fd_lock);
	list_add(&lcc->lcc_list, &fd->fd_lccs);
	write_unlock(&fd->fd_lock);
}

void ll_cl_remove(struct file *file, const struct lu_env *env)
{
	struct ll_file_data *fd = LUSTRE_FPRIVATE(file);
	struct ll_cl_context *lcc = &ll_env_info(env)->lti_io_ctx;

	write_lock(&fd->fd_lock);
	list_del_init(&lcc->lcc_list);
	write_unlock(&fd->fd_lock);
}

static int ll_io_read_page(const struct lu_env *env, struct cl_io *io,
			   struct cl_page *page)
{
	struct inode *inode = vvp_object_inode(page->cp_obj);
	struct ll_file_data *fd = vvp_env_io(env)->vui_fd;
	struct ll_readahead_state *ras = &fd->fd_ras;
	struct cl_2queue *queue  = &io->ci_queue;
	struct ll_sb_info *sbi = ll_i2sbi(inode);
	struct vvp_page *vpg;
	bool uptodate;
	int rc = 0;

	vpg = cl2vvp_page(cl_object_page_slice(page->cp_obj, page));
	uptodate = vpg->vpg_defer_uptodate;

	if (sbi->ll_ra_info.ra_max_pages_per_file > 0 &&
	    sbi->ll_ra_info.ra_max_pages > 0) {
		struct vvp_io *vio = vvp_env_io(env);
		enum ras_update_flags flags = 0;

		if (uptodate)
			flags |= LL_RAS_HIT;
		if (!vio->vui_ra_valid)
			flags |= LL_RAS_MMAP;
		ras_update(sbi, inode, ras, vvp_index(vpg), flags);
	}

	cl_2queue_init(queue);
	if (uptodate) {
		vpg->vpg_ra_used = 1;
		cl_page_export(env, page, 1);
		cl_page_disown(env, io, page);
	} else {
		cl_page_list_add(&queue->c2_qin, page);
	}

	if (sbi->ll_ra_info.ra_max_pages_per_file > 0 &&
	    sbi->ll_ra_info.ra_max_pages > 0) {
		int rc2;

		rc2 = ll_readahead(env, io, &queue->c2_qin, ras,
				   uptodate);
		CDEBUG(D_READA, DFID "%d pages read ahead at %lu\n",
		       PFID(ll_inode2fid(inode)), rc2, vvp_index(vpg));
	}

	if (queue->c2_qin.pl_nr > 0)
		rc = cl_io_submit_rw(env, io, CRT_READ, queue);

	/*
	 * Unlock unsent pages in case of error.
	 */
	cl_page_list_disown(env, io, &queue->c2_qin);
	cl_2queue_fini(env, queue);

	return rc;
}

int ll_readpage(struct file *file, struct page *vmpage)
{
	struct cl_object *clob = ll_i2info(file_inode(file))->lli_clob;
	struct ll_cl_context *lcc;
	const struct lu_env  *env;
	struct cl_io   *io;
	struct cl_page *page;
	int result;

	lcc = ll_cl_find(file);
	if (!lcc) {
		unlock_page(vmpage);
		return -EIO;
	}

	env = lcc->lcc_env;
	io = lcc->lcc_io;
	LASSERT(io->ci_state == CIS_IO_GOING);
	page = cl_page_find(env, clob, vmpage->index, vmpage, CPT_CACHEABLE);
	if (!IS_ERR(page)) {
		LASSERT(page->cp_type == CPT_CACHEABLE);
		if (likely(!PageUptodate(vmpage))) {
			cl_page_assume(env, io, page);
			result = ll_io_read_page(env, io, page);
		} else {
			/* Page from a non-object file. */
			unlock_page(vmpage);
			result = 0;
		}
		cl_page_put(env, page);
	} else {
		unlock_page(vmpage);
		result = PTR_ERR(page);
	}
	return result;
}

int ll_page_sync_io(const struct lu_env *env, struct cl_io *io,
		    struct cl_page *page, enum cl_req_type crt)
{
	struct cl_2queue  *queue;
	int result;

	LASSERT(io->ci_type == CIT_READ || io->ci_type == CIT_WRITE);

	queue = &io->ci_queue;
	cl_2queue_init_page(queue, page);

	result = cl_io_submit_sync(env, io, crt, queue, 0);
	LASSERT(cl_page_is_owned(page, io));

	if (crt == CRT_READ)
		/*
		 * in CRT_WRITE case page is left locked even in case of
		 * error.
		 */
		cl_page_list_disown(env, io, &queue->c2_qin);
	cl_2queue_fini(env, queue);

	return result;
}

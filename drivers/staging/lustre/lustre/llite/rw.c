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
 * http://www.sun.com/software/products/lustre/docs/GPLv2.pdf
 *
 * Please contact Sun Microsystems, Inc., 4150 Network Circle, Santa Clara,
 * CA 95054 USA or visit www.sun.com if you need additional information or
 * have any questions.
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

#define DEBUG_SUBSYSTEM S_LLITE

#include "../include/lustre_lite.h"
#include "../include/obd_cksum.h"
#include "llite_internal.h"
#include "../include/linux/lustre_compat25.h"

/**
 * Finalizes cl-data before exiting typical address_space operation. Dual to
 * ll_cl_init().
 */
static void ll_cl_fini(struct ll_cl_context *lcc)
{
	struct lu_env  *env  = lcc->lcc_env;
	struct cl_io   *io   = lcc->lcc_io;
	struct cl_page *page = lcc->lcc_page;

	LASSERT(lcc->lcc_cookie == current);
	LASSERT(env);

	if (page) {
		lu_ref_del(&page->cp_reference, "cl_io", io);
		cl_page_put(env, page);
	}

	cl_env_put(env, &lcc->lcc_refcheck);
}

/**
 * Initializes common cl-data at the typical address_space operation entry
 * point.
 */
static struct ll_cl_context *ll_cl_init(struct file *file,
					struct page *vmpage, int create)
{
	struct ll_cl_context *lcc;
	struct lu_env    *env;
	struct cl_io     *io;
	struct cl_object *clob;
	struct ccc_io    *cio;

	int refcheck;
	int result = 0;

	clob = ll_i2info(vmpage->mapping->host)->lli_clob;
	LASSERT(clob);

	env = cl_env_get(&refcheck);
	if (IS_ERR(env))
		return ERR_CAST(env);

	lcc = &vvp_env_info(env)->vti_io_ctx;
	memset(lcc, 0, sizeof(*lcc));
	lcc->lcc_env = env;
	lcc->lcc_refcheck = refcheck;
	lcc->lcc_cookie = current;

	cio = ccc_env_io(env);
	io = cio->cui_cl.cis_io;
	if (!io && create) {
		struct inode *inode = vmpage->mapping->host;
		loff_t pos;

		if (inode_trylock(inode)) {
			inode_unlock((inode));

			/* this is too bad. Someone is trying to write the
			 * page w/o holding inode mutex. This means we can
			 * add dirty pages into cache during truncate
			 */
			CERROR("Proc %s is dirtying page w/o inode lock, this will break truncate\n",
			       current->comm);
			dump_stack();
			LBUG();
			return ERR_PTR(-EIO);
		}

		/*
		 * Loop-back driver calls ->prepare_write().
		 * methods directly, bypassing file system ->write() operation,
		 * so cl_io has to be created here.
		 */
		io = ccc_env_thread_io(env);
		ll_io_init(io, file, 1);

		/* No lock at all for this kind of IO - we can't do it because
		 * we have held page lock, it would cause deadlock.
		 * XXX: This causes poor performance to loop device - One page
		 *      per RPC.
		 *      In order to get better performance, users should use
		 *      lloop driver instead.
		 */
		io->ci_lockreq = CILR_NEVER;

		pos = vmpage->index << PAGE_CACHE_SHIFT;

		/* Create a temp IO to serve write. */
		result = cl_io_rw_init(env, io, CIT_WRITE, pos, PAGE_CACHE_SIZE);
		if (result == 0) {
			cio->cui_fd = LUSTRE_FPRIVATE(file);
			cio->cui_iter = NULL;
			result = cl_io_iter_init(env, io);
			if (result == 0) {
				result = cl_io_lock(env, io);
				if (result == 0)
					result = cl_io_start(env, io);
			}
		} else
			result = io->ci_result;
	}

	lcc->lcc_io = io;
	if (!io)
		result = -EIO;
	if (result == 0) {
		struct cl_page   *page;

		LASSERT(io->ci_state == CIS_IO_GOING);
		LASSERT(cio->cui_fd == LUSTRE_FPRIVATE(file));
		page = cl_page_find(env, clob, vmpage->index, vmpage,
				    CPT_CACHEABLE);
		if (!IS_ERR(page)) {
			lcc->lcc_page = page;
			lu_ref_add(&page->cp_reference, "cl_io", io);
			result = 0;
		} else
			result = PTR_ERR(page);
	}
	if (result) {
		ll_cl_fini(lcc);
		lcc = ERR_PTR(result);
	}

	CDEBUG(D_VFSTRACE, "%lu@"DFID" -> %d %p %p\n",
	       vmpage->index, PFID(lu_object_fid(&clob->co_lu)), result,
	       env, io);
	return lcc;
}

static struct ll_cl_context *ll_cl_get(void)
{
	struct ll_cl_context *lcc;
	struct lu_env *env;
	int refcheck;

	env = cl_env_get(&refcheck);
	LASSERT(!IS_ERR(env));
	lcc = &vvp_env_info(env)->vti_io_ctx;
	LASSERT(env == lcc->lcc_env);
	LASSERT(current == lcc->lcc_cookie);
	cl_env_put(env, &refcheck);

	/* env has got in ll_cl_init, so it is still usable. */
	return lcc;
}

/**
 * ->prepare_write() address space operation called by generic_file_write()
 * for every page during write.
 */
int ll_prepare_write(struct file *file, struct page *vmpage, unsigned from,
		     unsigned to)
{
	struct ll_cl_context *lcc;
	int result;

	lcc = ll_cl_init(file, vmpage, 1);
	if (!IS_ERR(lcc)) {
		struct lu_env  *env = lcc->lcc_env;
		struct cl_io   *io  = lcc->lcc_io;
		struct cl_page *page = lcc->lcc_page;

		cl_page_assume(env, io, page);

		result = cl_io_prepare_write(env, io, page, from, to);
		if (result == 0) {
			/*
			 * Add a reference, so that page is not evicted from
			 * the cache until ->commit_write() is called.
			 */
			cl_page_get(page);
			lu_ref_add(&page->cp_reference, "prepare_write",
				   current);
		} else {
			cl_page_unassume(env, io, page);
			ll_cl_fini(lcc);
		}
		/* returning 0 in prepare assumes commit must be called
		 * afterwards
		 */
	} else {
		result = PTR_ERR(lcc);
	}
	return result;
}

int ll_commit_write(struct file *file, struct page *vmpage, unsigned from,
		    unsigned to)
{
	struct ll_cl_context *lcc;
	struct lu_env    *env;
	struct cl_io     *io;
	struct cl_page   *page;
	int result = 0;

	lcc  = ll_cl_get();
	env  = lcc->lcc_env;
	page = lcc->lcc_page;
	io   = lcc->lcc_io;

	LASSERT(cl_page_is_owned(page, io));
	LASSERT(from <= to);
	if (from != to) /* handle short write case. */
		result = cl_io_commit_write(env, io, page, from, to);
	if (cl_page_is_owned(page, io))
		cl_page_unassume(env, io, page);

	/*
	 * Release reference acquired by ll_prepare_write().
	 */
	lu_ref_del(&page->cp_reference, "prepare_write", current);
	cl_page_put(env, page);
	ll_cl_fini(lcc);
	return result;
}

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
				     unsigned long pages)
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

	/* If the non-strided (ria_pages == 0) readahead window
	 * (ria_start + ret) has grown across an RPC boundary, then trim
	 * readahead size by the amount beyond the RPC so it ends on an
	 * RPC boundary. If the readahead window is already ending on
	 * an RPC boundary (beyond_rpc == 0), or smaller than a full
	 * RPC (beyond_rpc < ret) the readahead size is unchanged.
	 * The (beyond_rpc != 0) check is skipped since the conditional
	 * branch is more expensive than subtracting zero from the result.
	 *
	 * Strided read is left unaligned to avoid small fragments beyond
	 * the RPC boundary from needing an extra read RPC.
	 */
	if (ria->ria_pages == 0) {
		long beyond_rpc = (ria->ria_start + ret) % PTLRPC_MAX_BRW_PAGES;

		if (/* beyond_rpc != 0 && */ beyond_rpc < ret)
			ret -= beyond_rpc;
	}

	if (atomic_add_return(ret, &ra->ra_cur_pages) > ra->ra_max_pages) {
		atomic_sub(ret, &ra->ra_cur_pages);
		ret = 0;
	}

out:
	return ret;
}

void ll_ra_count_put(struct ll_sb_info *sbi, unsigned long len)
{
	struct ll_ra_info *ra = &sbi->ll_ra_info;

	atomic_sub(len, &ra->ra_cur_pages);
}

static void ll_ra_stats_inc_sbi(struct ll_sb_info *sbi, enum ra_stat which)
{
	LASSERTF(which >= 0 && which < _NR_RA_STAT, "which: %u\n", which);
	lprocfs_counter_incr(sbi->ll_ra_stats, which);
}

void ll_ra_stats_inc(struct address_space *mapping, enum ra_stat which)
{
	struct ll_sb_info *sbi = ll_i2sbi(mapping->host);

	ll_ra_stats_inc_sbi(sbi, which);
}

#define RAS_CDEBUG(ras) \
	CDEBUG(D_READA,						      \
	       "lrp %lu cr %lu cp %lu ws %lu wl %lu nra %lu r %lu ri %lu"    \
	       "csr %lu sf %lu sp %lu sl %lu\n",			    \
	       ras->ras_last_readpage, ras->ras_consecutive_requests,	\
	       ras->ras_consecutive_pages, ras->ras_window_start,	    \
	       ras->ras_window_len, ras->ras_next_readahead,		 \
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

static struct ll_readahead_state *ll_ras_get(struct file *f)
{
	struct ll_file_data       *fd;

	fd = LUSTRE_FPRIVATE(f);
	return &fd->fd_ras;
}

void ll_ra_read_in(struct file *f, struct ll_ra_read *rar)
{
	struct ll_readahead_state *ras;

	ras = ll_ras_get(f);

	spin_lock(&ras->ras_lock);
	ras->ras_requests++;
	ras->ras_request_index = 0;
	ras->ras_consecutive_requests++;
	rar->lrr_reader = current;

	list_add(&rar->lrr_linkage, &ras->ras_read_beads);
	spin_unlock(&ras->ras_lock);
}

void ll_ra_read_ex(struct file *f, struct ll_ra_read *rar)
{
	struct ll_readahead_state *ras;

	ras = ll_ras_get(f);

	spin_lock(&ras->ras_lock);
	list_del_init(&rar->lrr_linkage);
	spin_unlock(&ras->ras_lock);
}

static int cl_read_ahead_page(const struct lu_env *env, struct cl_io *io,
			      struct cl_page_list *queue, struct cl_page *page,
			      struct page *vmpage)
{
	struct ccc_page *cp;
	int	      rc;

	rc = 0;
	cl_page_assume(env, io, page);
	lu_ref_add(&page->cp_reference, "ra", current);
	cp = cl2ccc_page(cl_page_at(page, &vvp_device_type));
	if (!cp->cpg_defer_uptodate && !PageUptodate(vmpage)) {
		rc = cl_page_is_under_lock(env, io, page);
		if (rc == -EBUSY) {
			cp->cpg_defer_uptodate = 1;
			cp->cpg_ra_used = 0;
			cl_page_list_add(queue, page);
			rc = 1;
		} else {
			cl_page_delete(env, page);
			rc = -ENOLCK;
		}
	} else {
		/* skip completed pages */
		cl_page_unassume(env, io, page);
	}
	lu_ref_del(&page->cp_reference, "ra", current);
	cl_page_put(env, page);
	return rc;
}

/**
 * Initiates read-ahead of a page with given index.
 *
 * \retval     +ve: page was added to \a queue.
 *
 * \retval -ENOLCK: there is no extent lock for this part of a file, stop
 *		  read-ahead.
 *
 * \retval  -ve, 0: page wasn't added to \a queue for other reason.
 */
static int ll_read_ahead_page(const struct lu_env *env, struct cl_io *io,
			      struct cl_page_list *queue,
			      pgoff_t index, struct address_space *mapping)
{
	struct page      *vmpage;
	struct cl_object *clob  = ll_i2info(mapping->host)->lli_clob;
	struct cl_page   *page;
	enum ra_stat      which = _NR_RA_STAT; /* keep gcc happy */
	int	       rc    = 0;
	const char       *msg   = NULL;

	vmpage = grab_cache_page_nowait(mapping, index);
	if (vmpage) {
		/* Check if vmpage was truncated or reclaimed */
		if (vmpage->mapping == mapping) {
			page = cl_page_find(env, clob, vmpage->index,
					    vmpage, CPT_CACHEABLE);
			if (!IS_ERR(page)) {
				rc = cl_read_ahead_page(env, io, queue,
							page, vmpage);
				if (rc == -ENOLCK) {
					which = RA_STAT_FAILED_MATCH;
					msg   = "lock match failed";
				}
			} else {
				which = RA_STAT_FAILED_GRAB_PAGE;
				msg   = "cl_page_find failed";
			}
		} else {
			which = RA_STAT_WRONG_GRAB_PAGE;
			msg   = "g_c_p_n returned invalid page";
		}
		if (rc != 1)
			unlock_page(vmpage);
		page_cache_release(vmpage);
	} else {
		which = RA_STAT_FAILED_GRAB_PAGE;
		msg   = "g_c_p_n failed";
	}
	if (msg) {
		ll_ra_stats_inc(mapping, which);
		CDEBUG(D_READA, "%s\n", msg);
	}
	return rc;
}

#define RIA_DEBUG(ria)						       \
	CDEBUG(D_READA, "rs %lu re %lu ro %lu rl %lu rp %lu\n",       \
	ria->ria_start, ria->ria_end, ria->ria_stoff, ria->ria_length,\
	ria->ria_pages)

/* Limit this to the blocksize instead of PTLRPC_BRW_MAX_SIZE, since we don't
 * know what the actual RPC size is.  If this needs to change, it makes more
 * sense to tune the i_blkbits value for the file based on the OSTs it is
 * striped over, rather than having a constant value for all files here.
 */

/* RAS_INCREASE_STEP should be (1UL << (inode->i_blkbits - PAGE_CACHE_SHIFT)).
 * Temporarily set RAS_INCREASE_STEP to 1MB. After 4MB RPC is enabled
 * by default, this should be adjusted corresponding with max_read_ahead_mb
 * and max_read_ahead_per_file_mb otherwise the readahead budget can be used
 * up quickly which will affect read performance significantly. See LU-2816
 */
#define RAS_INCREASE_STEP(inode) (ONE_MB_BRW_SIZE >> PAGE_CACHE_SHIFT)

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

static int ll_read_ahead_pages(const struct lu_env *env,
			       struct cl_io *io, struct cl_page_list *queue,
			       struct ra_io_arg *ria,
			       unsigned long *reserved_pages,
			       struct address_space *mapping,
			       unsigned long *ra_end)
{
	int rc, count = 0, stride_ria;
	unsigned long page_idx;

	LASSERT(ria);
	RIA_DEBUG(ria);

	stride_ria = ria->ria_length > ria->ria_pages && ria->ria_pages > 0;
	for (page_idx = ria->ria_start;
	     page_idx <= ria->ria_end && *reserved_pages > 0; page_idx++) {
		if (ras_inside_ra_window(page_idx, ria)) {
			/* If the page is inside the read-ahead window*/
			rc = ll_read_ahead_page(env, io, queue,
						page_idx, mapping);
			if (rc == 1) {
				(*reserved_pages)--;
				count++;
			} else if (rc == -ENOLCK)
				break;
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
			LASSERTF(page_idx > ria->ria_stoff, "Invalid page_idx %lu rs %lu re %lu ro %lu rl %lu rp %lu\n",
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
	*ra_end = page_idx;
	return count;
}

int ll_readahead(const struct lu_env *env, struct cl_io *io,
		 struct ll_readahead_state *ras, struct address_space *mapping,
		 struct cl_page_list *queue, int flags)
{
	struct vvp_io *vio = vvp_env_io(env);
	struct vvp_thread_info *vti = vvp_env_info(env);
	struct cl_attr *attr = ccc_env_thread_attr(env);
	unsigned long start = 0, end = 0, reserved;
	unsigned long ra_end, len;
	struct inode *inode;
	struct ll_ra_read *bead;
	struct ra_io_arg *ria = &vti->vti_ria;
	struct ll_inode_info *lli;
	struct cl_object *clob;
	int ret = 0;
	__u64 kms;

	inode = mapping->host;
	lli = ll_i2info(inode);
	clob = lli->lli_clob;

	memset(ria, 0, sizeof(*ria));

	cl_object_attr_lock(clob);
	ret = cl_object_attr_get(env, clob, attr);
	cl_object_attr_unlock(clob);

	if (ret != 0)
		return ret;
	kms = attr->cat_kms;
	if (kms == 0) {
		ll_ra_stats_inc(mapping, RA_STAT_ZERO_LEN);
		return 0;
	}

	spin_lock(&ras->ras_lock);
	if (vio->cui_ra_window_set)
		bead = &vio->cui_bead;
	else
		bead = NULL;

	/* Enlarge the RA window to encompass the full read */
	if (bead && ras->ras_window_start + ras->ras_window_len <
	    bead->lrr_start + bead->lrr_count) {
		ras->ras_window_len = bead->lrr_start + bead->lrr_count -
				      ras->ras_window_start;
	}
	/* Reserve a part of the read-ahead window that we'll be issuing */
	if (ras->ras_window_len) {
		start = ras->ras_next_readahead;
		end = ras->ras_window_start + ras->ras_window_len - 1;
	}
	if (end != 0) {
		unsigned long rpc_boundary;
		/*
		 * Align RA window to an optimal boundary.
		 *
		 * XXX This would be better to align to cl_max_pages_per_rpc
		 * instead of PTLRPC_MAX_BRW_PAGES, because the RPC size may
		 * be aligned to the RAID stripe size in the future and that
		 * is more important than the RPC size.
		 */
		/* Note: we only trim the RPC, instead of extending the RPC
		 * to the boundary, so to avoid reading too much pages during
		 * random reading.
		 */
		rpc_boundary = (end + 1) & (~(PTLRPC_MAX_BRW_PAGES - 1));
		if (rpc_boundary > 0)
			rpc_boundary--;

		if (rpc_boundary  > start)
			end = rpc_boundary;

		/* Truncate RA window to end of file */
		end = min(end, (unsigned long)((kms - 1) >> PAGE_CACHE_SHIFT));

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
		ll_ra_stats_inc(mapping, RA_STAT_ZERO_WINDOW);
		return 0;
	}
	len = ria_page_count(ria);
	if (len == 0)
		return 0;

	reserved = ll_ra_count_get(ll_i2sbi(inode), ria, len);
	if (reserved < len)
		ll_ra_stats_inc(mapping, RA_STAT_MAX_IN_FLIGHT);

	CDEBUG(D_READA, "reserved page %lu ra_cur %d ra_max %lu\n", reserved,
	       atomic_read(&ll_i2sbi(inode)->ll_ra_info.ra_cur_pages),
	       ll_i2sbi(inode)->ll_ra_info.ra_max_pages);

	ret = ll_read_ahead_pages(env, io, queue,
				  ria, &reserved, mapping, &ra_end);

	if (reserved != 0)
		ll_ra_count_put(ll_i2sbi(inode), reserved);

	if (ra_end == end + 1 && ra_end == (kms >> PAGE_CACHE_SHIFT))
		ll_ra_stats_inc(mapping, RA_STAT_EOF);

	/* if we didn't get to the end of the region we reserved from
	 * the ras we need to go back and update the ras so that the
	 * next read-ahead tries from where we left off.  we only do so
	 * if the region we failed to issue read-ahead on is still ahead
	 * of the app and behind the next index to start read-ahead from
	 */
	CDEBUG(D_READA, "ra_end %lu end %lu stride end %lu\n",
	       ra_end, end, ria->ria_end);

	if (ra_end != end + 1) {
		spin_lock(&ras->ras_lock);
		if (ra_end < ras->ras_next_readahead &&
		    index_in_window(ra_end, ras->ras_window_start, 0,
				    ras->ras_window_len)) {
			ras->ras_next_readahead = ra_end;
			RAS_CDEBUG(ras);
		}
		spin_unlock(&ras->ras_lock);
	}

	return ret;
}

static void ras_set_start(struct inode *inode, struct ll_readahead_state *ras,
			  unsigned long index)
{
	ras->ras_window_start = index & (~(RAS_INCREASE_STEP(inode) - 1));
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
	ras->ras_next_readahead = max(ras->ras_window_start, index);

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
	ras_reset(inode, ras, 0);
	ras->ras_requests = 0;
	INIT_LIST_HEAD(&ras->ras_read_beads);
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

	if (!stride_io_mode(ras) && (stride_gap != 0 ||
	    ras->ras_consecutive_stride_requests == 0)) {
		ras->ras_stride_pages = ras->ras_consecutive_pages;
		ras->ras_stride_length = stride_gap+ras->ras_consecutive_pages;
	}
	LASSERT(ras->ras_request_index == 0);
	LASSERT(ras->ras_consecutive_stride_requests == 0);

	if (index <= ras->ras_last_readpage) {
		/*Reset stride window for forward read*/
		ras_stride_reset(ras);
		return;
	}

	ras->ras_stride_pages = ras->ras_consecutive_pages;
	ras->ras_stride_length = stride_gap+ras->ras_consecutive_pages;

	RAS_CDEBUG(ras);
	return;
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
	if (stride_io_mode(ras))
		ras_stride_increase_window(ras, ra, RAS_INCREASE_STEP(inode));
	else
		ras->ras_window_len = min(ras->ras_window_len +
					  RAS_INCREASE_STEP(inode),
					  ra->ra_max_pages_per_file);
}

void ras_update(struct ll_sb_info *sbi, struct inode *inode,
		struct ll_readahead_state *ras, unsigned long index,
		unsigned hit)
{
	struct ll_ra_info *ra = &sbi->ll_ra_info;
	int zero = 0, stride_detect = 0, ra_miss = 0;

	spin_lock(&ras->ras_lock);

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
	if (ras->ras_requests == 2 && !ras->ras_request_index) {
		__u64 kms_pages;

		kms_pages = (i_size_read(inode) + PAGE_CACHE_SIZE - 1) >>
			    PAGE_CACHE_SHIFT;

		CDEBUG(D_READA, "kmsp %llu mwp %lu mp %lu\n", kms_pages,
		       ra->ra_max_read_ahead_whole_pages, ra->ra_max_pages_per_file);

		if (kms_pages &&
		    kms_pages <= ra->ra_max_read_ahead_whole_pages) {
			ras->ras_window_start = 0;
			ras->ras_last_readpage = 0;
			ras->ras_next_readahead = 0;
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
				/*If stride-RA hit cache miss, the stride dector
				 *will not be reset to avoid the overhead of
				 *redetecting read-ahead mode
				 */
				if (index != ras->ras_last_readpage + 1)
					ras->ras_consecutive_pages = 0;
				ras_reset(inode, ras, index);
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

	if (stride_io_mode(ras))
		/* Since stride readahead is sensitive to the offset
		 * of read-ahead, so we use original offset here,
		 * instead of ras_window_start, which is RPC aligned
		 */
		ras->ras_next_readahead = max(index, ras->ras_next_readahead);
	else
		ras->ras_next_readahead = max(ras->ras_window_start,
					      ras->ras_next_readahead);
	RAS_CDEBUG(ras);

	/* Trigger RA in the mmap case where ras_consecutive_requests
	 * is not incremented and thus can't be used to trigger RA
	 */
	if (!ras->ras_window_len && ras->ras_consecutive_pages == 4) {
		ras->ras_window_len = RAS_INCREASE_STEP(inode);
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
		ras->ras_window_len = RAS_INCREASE_STEP(inode);
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
	return;
}

int ll_writepage(struct page *vmpage, struct writeback_control *wbc)
{
	struct inode	       *inode = vmpage->mapping->host;
	struct ll_inode_info   *lli   = ll_i2info(inode);
	struct lu_env	  *env;
	struct cl_io	   *io;
	struct cl_page	 *page;
	struct cl_object       *clob;
	struct cl_env_nest      nest;
	bool redirtied = false;
	bool unlocked = false;
	int result;

	LASSERT(PageLocked(vmpage));
	LASSERT(!PageWriteback(vmpage));

	LASSERT(ll_i2dtexp(inode));

	env = cl_env_nested_get(&nest);
	if (IS_ERR(env)) {
		result = PTR_ERR(env);
		goto out;
	}

	clob  = ll_i2info(inode)->lli_clob;
	LASSERT(clob);

	io = ccc_env_thread_io(env);
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
					    offset + PAGE_CACHE_SIZE - 1,
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

	cl_env_nested_put(&nest, env);
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
		start = mapping->writeback_index << PAGE_CACHE_SHIFT;
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
	result = cl_sync_file_range(inode, start, end, mode, ignore_layout);
	if (result > 0) {
		wbc->nr_to_write -= result;
		result = 0;
	 }

	if (wbc->range_cyclic || (range_whole && wbc->nr_to_write > 0)) {
		if (end == OBD_OBJECT_EOF)
			end = i_size_read(inode);
		mapping->writeback_index = (end >> PAGE_CACHE_SHIFT) + 1;
	}
	return result;
}

int ll_readpage(struct file *file, struct page *vmpage)
{
	struct ll_cl_context *lcc;
	int result;

	lcc = ll_cl_init(file, vmpage, 0);
	if (!IS_ERR(lcc)) {
		struct lu_env  *env  = lcc->lcc_env;
		struct cl_io   *io   = lcc->lcc_io;
		struct cl_page *page = lcc->lcc_page;

		LASSERT(page->cp_type == CPT_CACHEABLE);
		if (likely(!PageUptodate(vmpage))) {
			cl_page_assume(env, io, page);
			result = cl_io_read_page(env, io, page);
		} else {
			/* Page from a non-object file. */
			unlock_page(vmpage);
			result = 0;
		}
		ll_cl_fini(lcc);
	} else {
		unlock_page(vmpage);
		result = PTR_ERR(lcc);
	}
	return result;
}

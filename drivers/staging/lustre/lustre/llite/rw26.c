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
 * Copyright (c) 2003, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright (c) 2011, 2012, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 *
 * lustre/lustre/llite/rw26.c
 *
 * Lustre Lite I/O page cache routines for the 2.5/2.6 kernel version
 */

#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/stat.h>
#include <linux/errno.h>
#include <linux/unistd.h>
#include <linux/uaccess.h>

#include <linux/migrate.h>
#include <linux/fs.h>
#include <linux/buffer_head.h>
#include <linux/mpage.h>
#include <linux/writeback.h>
#include <linux/pagemap.h>

#define DEBUG_SUBSYSTEM S_LLITE

#include "../include/lustre_lite.h"
#include "llite_internal.h"
#include "../include/linux/lustre_compat25.h"

/**
 * Implements Linux VM address_space::invalidatepage() method. This method is
 * called when the page is truncate from a file, either as a result of
 * explicit truncate, or when inode is removed from memory (as a result of
 * final iput(), umount, or memory pressure induced icache shrinking).
 *
 * [0, offset] bytes of the page remain valid (this is for a case of not-page
 * aligned truncate). Lustre leaves partially truncated page in the cache,
 * relying on struct inode::i_size to limit further accesses.
 */
static void ll_invalidatepage(struct page *vmpage, unsigned int offset,
			      unsigned int length)
{
	struct inode     *inode;
	struct lu_env    *env;
	struct cl_page   *page;
	struct cl_object *obj;

	int refcheck;

	LASSERT(PageLocked(vmpage));
	LASSERT(!PageWriteback(vmpage));

	/*
	 * It is safe to not check anything in invalidatepage/releasepage
	 * below because they are run with page locked and all our io is
	 * happening with locked page too
	 */
	if (offset == 0 && length == PAGE_CACHE_SIZE) {
		env = cl_env_get(&refcheck);
		if (!IS_ERR(env)) {
			inode = vmpage->mapping->host;
			obj = ll_i2info(inode)->lli_clob;
			if (obj != NULL) {
				page = cl_vmpage_page(vmpage, obj);
				if (page != NULL) {
					lu_ref_add(&page->cp_reference,
						   "delete", vmpage);
					cl_page_delete(env, page);
					lu_ref_del(&page->cp_reference,
						   "delete", vmpage);
					cl_page_put(env, page);
				}
			} else
				LASSERT(vmpage->private == 0);
			cl_env_put(env, &refcheck);
		}
	}
}

#ifdef HAVE_RELEASEPAGE_WITH_INT
#define RELEASEPAGE_ARG_TYPE int
#else
#define RELEASEPAGE_ARG_TYPE gfp_t
#endif
static int ll_releasepage(struct page *vmpage, RELEASEPAGE_ARG_TYPE gfp_mask)
{
	struct cl_env_nest nest;
	struct lu_env     *env;
	struct cl_object  *obj;
	struct cl_page    *page;
	struct address_space *mapping;
	int result;

	LASSERT(PageLocked(vmpage));
	if (PageWriteback(vmpage) || PageDirty(vmpage))
		return 0;

	mapping = vmpage->mapping;
	if (mapping == NULL)
		return 1;

	obj = ll_i2info(mapping->host)->lli_clob;
	if (obj == NULL)
		return 1;

	/* 1 for page allocator, 1 for cl_page and 1 for page cache */
	if (page_count(vmpage) > 3)
		return 0;

	/* TODO: determine what gfp should be used by @gfp_mask. */
	env = cl_env_nested_get(&nest);
	if (IS_ERR(env))
		/* If we can't allocate an env we won't call cl_page_put()
		 * later on which further means it's impossible to drop
		 * page refcount by cl_page, so ask kernel to not free
		 * this page. */
		return 0;

	page = cl_vmpage_page(vmpage, obj);
	result = page == NULL;
	if (page != NULL) {
		if (!cl_page_in_use(page)) {
			result = 1;
			cl_page_delete(env, page);
		}
		cl_page_put(env, page);
	}
	cl_env_nested_put(&nest, env);
	return result;
}

static int ll_set_page_dirty(struct page *vmpage)
{
#if 0
	struct cl_page    *page = vvp_vmpage_page_transient(vmpage);
	struct vvp_object *obj  = cl_inode2vvp(vmpage->mapping->host);
	struct vvp_page   *cpg;

	/*
	 * XXX should page method be called here?
	 */
	LASSERT(&obj->co_cl == page->cp_obj);
	cpg = cl2vvp_page(cl_page_at(page, &vvp_device_type));
	/*
	 * XXX cannot do much here, because page is possibly not locked:
	 * sys_munmap()->...
	 *     ->unmap_page_range()->zap_pte_range()->set_page_dirty().
	 */
	vvp_write_pending(obj, cpg);
#endif
	return __set_page_dirty_nobuffers(vmpage);
}

#define MAX_DIRECTIO_SIZE (2*1024*1024*1024UL)

static inline int ll_get_user_pages(int rw, unsigned long user_addr,
				    size_t size, struct page ***pages,
				    int *max_pages)
{
	int result = -ENOMEM;

	/* set an arbitrary limit to prevent arithmetic overflow */
	if (size > MAX_DIRECTIO_SIZE) {
		*pages = NULL;
		return -EFBIG;
	}

	*max_pages = (user_addr + size + PAGE_CACHE_SIZE - 1) >> PAGE_CACHE_SHIFT;
	*max_pages -= user_addr >> PAGE_CACHE_SHIFT;

	*pages = libcfs_kvzalloc(*max_pages * sizeof(**pages), GFP_NOFS);
	if (*pages) {
		result = get_user_pages_fast(user_addr, *max_pages,
					     (rw == READ), *pages);
		if (unlikely(result <= 0))
			kvfree(*pages);
	}

	return result;
}

/*  ll_free_user_pages - tear down page struct array
 *  @pages: array of page struct pointers underlying target buffer */
static void ll_free_user_pages(struct page **pages, int npages, int do_dirty)
{
	int i;

	for (i = 0; i < npages; i++) {
		if (do_dirty)
			set_page_dirty_lock(pages[i]);
		page_cache_release(pages[i]);
	}
	kvfree(pages);
}

ssize_t ll_direct_rw_pages(const struct lu_env *env, struct cl_io *io,
			   int rw, struct inode *inode,
			   struct ll_dio_pages *pv)
{
	struct cl_page    *clp;
	struct cl_2queue  *queue;
	struct cl_object  *obj = io->ci_obj;
	int i;
	ssize_t rc = 0;
	loff_t file_offset  = pv->ldp_start_offset;
	long size	   = pv->ldp_size;
	int page_count      = pv->ldp_nr;
	struct page **pages = pv->ldp_pages;
	long page_size      = cl_page_size(obj);
	bool do_io;
	int  io_pages       = 0;

	queue = &io->ci_queue;
	cl_2queue_init(queue);
	for (i = 0; i < page_count; i++) {
		if (pv->ldp_offsets)
		    file_offset = pv->ldp_offsets[i];

		LASSERT(!(file_offset & (page_size - 1)));
		clp = cl_page_find(env, obj, cl_index(obj, file_offset),
				   pv->ldp_pages[i], CPT_TRANSIENT);
		if (IS_ERR(clp)) {
			rc = PTR_ERR(clp);
			break;
		}

		rc = cl_page_own(env, io, clp);
		if (rc) {
			LASSERT(clp->cp_state == CPS_FREEING);
			cl_page_put(env, clp);
			break;
		}

		do_io = true;

		/* check the page type: if the page is a host page, then do
		 * write directly */
		if (clp->cp_type == CPT_CACHEABLE) {
			struct page *vmpage = cl_page_vmpage(env, clp);
			struct page *src_page;
			struct page *dst_page;
			void       *src;
			void       *dst;

			src_page = (rw == WRITE) ? pages[i] : vmpage;
			dst_page = (rw == WRITE) ? vmpage : pages[i];

			src = kmap_atomic(src_page);
			dst = kmap_atomic(dst_page);
			memcpy(dst, src, min(page_size, size));
			kunmap_atomic(dst);
			kunmap_atomic(src);

			/* make sure page will be added to the transfer by
			 * cl_io_submit()->...->vvp_page_prep_write(). */
			if (rw == WRITE)
				set_page_dirty(vmpage);

			if (rw == READ) {
				/* do not issue the page for read, since it
				 * may reread a ra page which has NOT uptodate
				 * bit set. */
				cl_page_disown(env, io, clp);
				do_io = false;
			}
		}

		if (likely(do_io)) {
			cl_2queue_add(queue, clp);

			/*
			 * Set page clip to tell transfer formation engine
			 * that page has to be sent even if it is beyond KMS.
			 */
			cl_page_clip(env, clp, 0, min(size, page_size));

			++io_pages;
		}

		/* drop the reference count for cl_page_find */
		cl_page_put(env, clp);
		size -= page_size;
		file_offset += page_size;
	}

	if (rc == 0 && io_pages) {
		rc = cl_io_submit_sync(env, io,
				       rw == READ ? CRT_READ : CRT_WRITE,
				       queue, 0);
	}
	if (rc == 0)
		rc = pv->ldp_size;

	cl_2queue_discard(env, io, queue);
	cl_2queue_disown(env, io, queue);
	cl_2queue_fini(env, queue);
	return rc;
}
EXPORT_SYMBOL(ll_direct_rw_pages);

static ssize_t ll_direct_IO_26_seg(const struct lu_env *env, struct cl_io *io,
				   int rw, struct inode *inode,
				   struct address_space *mapping,
				   size_t size, loff_t file_offset,
				   struct page **pages, int page_count)
{
    struct ll_dio_pages pvec = { .ldp_pages	= pages,
				 .ldp_nr	   = page_count,
				 .ldp_size	 = size,
				 .ldp_offsets      = NULL,
				 .ldp_start_offset = file_offset
			       };

    return ll_direct_rw_pages(env, io, rw, inode, &pvec);
}

#ifdef KMALLOC_MAX_SIZE
#define MAX_MALLOC KMALLOC_MAX_SIZE
#else
#define MAX_MALLOC (128 * 1024)
#endif

/* This is the maximum size of a single O_DIRECT request, based on the
 * kmalloc limit.  We need to fit all of the brw_page structs, each one
 * representing PAGE_SIZE worth of user data, into a single buffer, and
 * then truncate this to be a full-sized RPC.  For 4kB PAGE_SIZE this is
 * up to 22MB for 128kB kmalloc and up to 682MB for 4MB kmalloc. */
#define MAX_DIO_SIZE ((MAX_MALLOC / sizeof(struct brw_page) * PAGE_CACHE_SIZE) & \
		      ~(DT_MAX_BRW_SIZE - 1))
static ssize_t ll_direct_IO_26(struct kiocb *iocb, struct iov_iter *iter,
			       loff_t file_offset)
{
	struct lu_env *env;
	struct cl_io *io;
	struct file *file = iocb->ki_filp;
	struct inode *inode = file->f_mapping->host;
	struct ccc_object *obj = cl_inode2ccc(inode);
	ssize_t count = iov_iter_count(iter);
	ssize_t tot_bytes = 0, result = 0;
	struct ll_inode_info *lli = ll_i2info(inode);
	long size = MAX_DIO_SIZE;
	int refcheck;

	if (!lli->lli_has_smd)
		return -EBADF;

	/* Check EOF by ourselves */
	if (iov_iter_rw(iter) == READ && file_offset >= i_size_read(inode))
		return 0;

	/* FIXME: io smaller than PAGE_SIZE is broken on ia64 ??? */
	if ((file_offset & ~CFS_PAGE_MASK) || (count & ~CFS_PAGE_MASK))
		return -EINVAL;

	CDEBUG(D_VFSTRACE,
	       "VFS Op:inode=%lu/%u(%p), size=%zd (max %lu), offset=%lld=%llx, pages %zd (max %lu)\n",
	       inode->i_ino, inode->i_generation, inode, count, MAX_DIO_SIZE,
	       file_offset, file_offset, count >> PAGE_CACHE_SHIFT,
	       MAX_DIO_SIZE >> PAGE_CACHE_SHIFT);

	/* Check that all user buffers are aligned as well */
	if (iov_iter_alignment(iter) & ~CFS_PAGE_MASK)
		return -EINVAL;

	env = cl_env_get(&refcheck);
	LASSERT(!IS_ERR(env));
	io = ccc_env_io(env)->cui_cl.cis_io;
	LASSERT(io != NULL);

	/* 0. Need locking between buffered and direct access. and race with
	 *    size changing by concurrent truncates and writes.
	 * 1. Need inode mutex to operate transient pages.
	 */
	if (iov_iter_rw(iter) == READ)
		mutex_lock(&inode->i_mutex);

	LASSERT(obj->cob_transient_pages == 0);
	while (iov_iter_count(iter)) {
		struct page **pages;
		size_t offs;

		count = min_t(size_t, iov_iter_count(iter), size);
		if (iov_iter_rw(iter) == READ) {
			if (file_offset >= i_size_read(inode))
				break;
			if (file_offset + count > i_size_read(inode))
				count = i_size_read(inode) - file_offset;
		}

		result = iov_iter_get_pages_alloc(iter, &pages, count, &offs);
		if (likely(result > 0)) {
			int n = DIV_ROUND_UP(result + offs, PAGE_SIZE);

			result = ll_direct_IO_26_seg(env, io, iov_iter_rw(iter),
						     inode, file->f_mapping,
						     result, file_offset, pages,
						     n);
			ll_free_user_pages(pages, n, iov_iter_rw(iter) == READ);
		}
		if (unlikely(result <= 0)) {
			/* If we can't allocate a large enough buffer
			 * for the request, shrink it to a smaller
			 * PAGE_SIZE multiple and try again.
			 * We should always be able to kmalloc for a
			 * page worth of page pointers = 4MB on i386. */
			if (result == -ENOMEM &&
			    size > (PAGE_CACHE_SIZE / sizeof(*pages)) *
				   PAGE_CACHE_SIZE) {
				size = ((((size / 2) - 1) |
					 ~CFS_PAGE_MASK) + 1) &
					CFS_PAGE_MASK;
				CDEBUG(D_VFSTRACE, "DIO size now %lu\n",
				       size);
				continue;
			}

			goto out;
		}
		iov_iter_advance(iter, result);
		tot_bytes += result;
		file_offset += result;
	}
out:
	LASSERT(obj->cob_transient_pages == 0);
	if (iov_iter_rw(iter) == READ)
		mutex_unlock(&inode->i_mutex);

	if (tot_bytes > 0) {
		if (iov_iter_rw(iter) == WRITE) {
			struct lov_stripe_md *lsm;

			lsm = ccc_inode_lsm_get(inode);
			LASSERT(lsm != NULL);
			lov_stripe_lock(lsm);
			obd_adjust_kms(ll_i2dtexp(inode), lsm, file_offset, 0);
			lov_stripe_unlock(lsm);
			ccc_inode_lsm_put(inode, lsm);
		}
	}

	cl_env_put(env, &refcheck);
	return tot_bytes ? : result;
}

static int ll_write_begin(struct file *file, struct address_space *mapping,
			 loff_t pos, unsigned len, unsigned flags,
			 struct page **pagep, void **fsdata)
{
	pgoff_t index = pos >> PAGE_CACHE_SHIFT;
	struct page *page;
	int rc;
	unsigned from = pos & (PAGE_CACHE_SIZE - 1);

	page = grab_cache_page_write_begin(mapping, index, flags);
	if (!page)
		return -ENOMEM;

	*pagep = page;

	rc = ll_prepare_write(file, page, from, from + len);
	if (rc) {
		unlock_page(page);
		page_cache_release(page);
	}
	return rc;
}

static int ll_write_end(struct file *file, struct address_space *mapping,
			loff_t pos, unsigned len, unsigned copied,
			struct page *page, void *fsdata)
{
	unsigned from = pos & (PAGE_CACHE_SIZE - 1);
	int rc;

	rc = ll_commit_write(file, page, from, from + copied);
	unlock_page(page);
	page_cache_release(page);

	return rc ?: copied;
}

#ifdef CONFIG_MIGRATION
static int ll_migratepage(struct address_space *mapping,
			 struct page *newpage, struct page *page,
			 enum migrate_mode mode
		)
{
	/* Always fail page migration until we have a proper implementation */
	return -EIO;
}
#endif

const struct address_space_operations ll_aops = {
	.readpage	= ll_readpage,
	.direct_IO      = ll_direct_IO_26,
	.writepage      = ll_writepage,
	.writepages     = ll_writepages,
	.set_page_dirty = ll_set_page_dirty,
	.write_begin    = ll_write_begin,
	.write_end      = ll_write_end,
	.invalidatepage = ll_invalidatepage,
	.releasepage    = (void *)ll_releasepage,
#ifdef CONFIG_MIGRATION
	.migratepage    = ll_migratepage,
#endif
};

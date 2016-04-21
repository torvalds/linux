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
 * Copyright (c) 2008, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright (c) 2011, 2015, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 *
 * Implementation of cl_page for VVP layer.
 *
 *   Author: Nikita Danilov <nikita.danilov@sun.com>
 *   Author: Jinshan Xiong <jinshan.xiong@whamcloud.com>
 */

#define DEBUG_SUBSYSTEM S_LLITE

#include "../include/obd.h"
#include "../include/lustre_lite.h"

#include "vvp_internal.h"

/*****************************************************************************
 *
 * Page operations.
 *
 */

static void vvp_page_fini_common(struct ccc_page *cp)
{
	struct page *vmpage = cp->cpg_page;

	LASSERT(vmpage);
	put_page(vmpage);
}

static void vvp_page_fini(const struct lu_env *env,
			  struct cl_page_slice *slice)
{
	struct ccc_page *cp = cl2ccc_page(slice);
	struct page *vmpage  = cp->cpg_page;

	/*
	 * vmpage->private was already cleared when page was moved into
	 * VPG_FREEING state.
	 */
	LASSERT((struct cl_page *)vmpage->private != slice->cpl_page);
	vvp_page_fini_common(cp);
}

static int vvp_page_own(const struct lu_env *env,
			const struct cl_page_slice *slice, struct cl_io *io,
			int nonblock)
{
	struct ccc_page *vpg    = cl2ccc_page(slice);
	struct page      *vmpage = vpg->cpg_page;

	LASSERT(vmpage);
	if (nonblock) {
		if (!trylock_page(vmpage))
			return -EAGAIN;

		if (unlikely(PageWriteback(vmpage))) {
			unlock_page(vmpage);
			return -EAGAIN;
		}

		return 0;
	}

	lock_page(vmpage);
	wait_on_page_writeback(vmpage);
	return 0;
}

static void vvp_page_assume(const struct lu_env *env,
			    const struct cl_page_slice *slice,
			    struct cl_io *unused)
{
	struct page *vmpage = cl2vm_page(slice);

	LASSERT(vmpage);
	LASSERT(PageLocked(vmpage));
	wait_on_page_writeback(vmpage);
}

static void vvp_page_unassume(const struct lu_env *env,
			      const struct cl_page_slice *slice,
			      struct cl_io *unused)
{
	struct page *vmpage = cl2vm_page(slice);

	LASSERT(vmpage);
	LASSERT(PageLocked(vmpage));
}

static void vvp_page_disown(const struct lu_env *env,
			    const struct cl_page_slice *slice, struct cl_io *io)
{
	struct page *vmpage = cl2vm_page(slice);

	LASSERT(vmpage);
	LASSERT(PageLocked(vmpage));

	unlock_page(cl2vm_page(slice));
}

static void vvp_page_discard(const struct lu_env *env,
			     const struct cl_page_slice *slice,
			     struct cl_io *unused)
{
	struct page	   *vmpage  = cl2vm_page(slice);
	struct address_space *mapping;
	struct ccc_page      *cpg     = cl2ccc_page(slice);

	LASSERT(vmpage);
	LASSERT(PageLocked(vmpage));

	mapping = vmpage->mapping;

	if (cpg->cpg_defer_uptodate && !cpg->cpg_ra_used)
		ll_ra_stats_inc(mapping, RA_STAT_DISCARDED);

	/*
	 * truncate_complete_page() calls
	 * a_ops->invalidatepage()->cl_page_delete()->vvp_page_delete().
	 */
	truncate_complete_page(mapping, vmpage);
}

static int vvp_page_unmap(const struct lu_env *env,
			  const struct cl_page_slice *slice,
			  struct cl_io *unused)
{
	struct page *vmpage = cl2vm_page(slice);
	__u64       offset;

	LASSERT(vmpage);
	LASSERT(PageLocked(vmpage));

	offset = vmpage->index << PAGE_SHIFT;

	/*
	 * XXX is it safe to call this with the page lock held?
	 */
	ll_teardown_mmaps(vmpage->mapping, offset, offset + PAGE_SIZE);
	return 0;
}

static void vvp_page_delete(const struct lu_env *env,
			    const struct cl_page_slice *slice)
{
	struct page       *vmpage = cl2vm_page(slice);
	struct inode     *inode  = vmpage->mapping->host;
	struct cl_object *obj    = slice->cpl_obj;

	LASSERT(PageLocked(vmpage));
	LASSERT((struct cl_page *)vmpage->private == slice->cpl_page);
	LASSERT(inode == ccc_object_inode(obj));

	vvp_write_complete(cl2ccc(obj), cl2ccc_page(slice));
	ClearPagePrivate(vmpage);
	vmpage->private = 0;
	/*
	 * Reference from vmpage to cl_page is removed, but the reference back
	 * is still here. It is removed later in vvp_page_fini().
	 */
}

static void vvp_page_export(const struct lu_env *env,
			    const struct cl_page_slice *slice,
			    int uptodate)
{
	struct page *vmpage = cl2vm_page(slice);

	LASSERT(vmpage);
	LASSERT(PageLocked(vmpage));
	if (uptodate)
		SetPageUptodate(vmpage);
	else
		ClearPageUptodate(vmpage);
}

static int vvp_page_is_vmlocked(const struct lu_env *env,
				const struct cl_page_slice *slice)
{
	return PageLocked(cl2vm_page(slice)) ? -EBUSY : -ENODATA;
}

static int vvp_page_prep_read(const struct lu_env *env,
			      const struct cl_page_slice *slice,
			      struct cl_io *unused)
{
	/* Skip the page already marked as PG_uptodate. */
	return PageUptodate(cl2vm_page(slice)) ? -EALREADY : 0;
}

static int vvp_page_prep_write(const struct lu_env *env,
			       const struct cl_page_slice *slice,
			       struct cl_io *unused)
{
	struct page *vmpage = cl2vm_page(slice);
	struct cl_page *pg = slice->cpl_page;

	LASSERT(PageLocked(vmpage));
	LASSERT(!PageDirty(vmpage));

	/* ll_writepage path is not a sync write, so need to set page writeback
	 * flag
	 */
	if (!pg->cp_sync_io)
		set_page_writeback(vmpage);

	vvp_write_pending(cl2ccc(slice->cpl_obj), cl2ccc_page(slice));

	return 0;
}

/**
 * Handles page transfer errors at VM level.
 *
 * This takes inode as a separate argument, because inode on which error is to
 * be set can be different from \a vmpage inode in case of direct-io.
 */
static void vvp_vmpage_error(struct inode *inode, struct page *vmpage, int ioret)
{
	struct ccc_object *obj = cl_inode2ccc(inode);

	if (ioret == 0) {
		ClearPageError(vmpage);
		obj->cob_discard_page_warned = 0;
	} else {
		SetPageError(vmpage);
		if (ioret == -ENOSPC)
			set_bit(AS_ENOSPC, &inode->i_mapping->flags);
		else
			set_bit(AS_EIO, &inode->i_mapping->flags);

		if ((ioret == -ESHUTDOWN || ioret == -EINTR) &&
		    obj->cob_discard_page_warned == 0) {
			obj->cob_discard_page_warned = 1;
			ll_dirty_page_discard_warn(vmpage, ioret);
		}
	}
}

static void vvp_page_completion_read(const struct lu_env *env,
				     const struct cl_page_slice *slice,
				     int ioret)
{
	struct ccc_page *cp     = cl2ccc_page(slice);
	struct page      *vmpage = cp->cpg_page;
	struct cl_page  *page   = cl_page_top(slice->cpl_page);
	struct inode    *inode  = ccc_object_inode(page->cp_obj);

	LASSERT(PageLocked(vmpage));
	CL_PAGE_HEADER(D_PAGE, env, page, "completing READ with %d\n", ioret);

	if (cp->cpg_defer_uptodate)
		ll_ra_count_put(ll_i2sbi(inode), 1);

	if (ioret == 0)  {
		if (!cp->cpg_defer_uptodate)
			cl_page_export(env, page, 1);
	} else
		cp->cpg_defer_uptodate = 0;

	if (!page->cp_sync_io)
		unlock_page(vmpage);
}

static void vvp_page_completion_write(const struct lu_env *env,
				      const struct cl_page_slice *slice,
				      int ioret)
{
	struct ccc_page *cp     = cl2ccc_page(slice);
	struct cl_page  *pg     = slice->cpl_page;
	struct page      *vmpage = cp->cpg_page;

	CL_PAGE_HEADER(D_PAGE, env, pg, "completing WRITE with %d\n", ioret);

	/*
	 * TODO: Actually it makes sense to add the page into oap pending
	 * list again and so that we don't need to take the page out from
	 * SoM write pending list, if we just meet a recoverable error,
	 * -ENOMEM, etc.
	 * To implement this, we just need to return a non zero value in
	 * ->cpo_completion method. The underlying transfer should be notified
	 * and then re-add the page into pending transfer queue.  -jay
	 */

	cp->cpg_write_queued = 0;
	vvp_write_complete(cl2ccc(slice->cpl_obj), cp);

	if (pg->cp_sync_io) {
		LASSERT(PageLocked(vmpage));
		LASSERT(!PageWriteback(vmpage));
	} else {
		LASSERT(PageWriteback(vmpage));
		/*
		 * Only mark the page error only when it's an async write
		 * because applications won't wait for IO to finish.
		 */
		vvp_vmpage_error(ccc_object_inode(pg->cp_obj), vmpage, ioret);

		end_page_writeback(vmpage);
	}
}

/**
 * Implements cl_page_operations::cpo_make_ready() method.
 *
 * This is called to yank a page from the transfer cache and to send it out as
 * a part of transfer. This function try-locks the page. If try-lock failed,
 * page is owned by some concurrent IO, and should be skipped (this is bad,
 * but hopefully rare situation, as it usually results in transfer being
 * shorter than possible).
 *
 * \retval 0      success, page can be placed into transfer
 *
 * \retval -EAGAIN page is either used by concurrent IO has been
 * truncated. Skip it.
 */
static int vvp_page_make_ready(const struct lu_env *env,
			       const struct cl_page_slice *slice)
{
	struct page *vmpage = cl2vm_page(slice);
	struct cl_page *pg = slice->cpl_page;
	int result = 0;

	lock_page(vmpage);
	if (clear_page_dirty_for_io(vmpage)) {
		LASSERT(pg->cp_state == CPS_CACHED);
		/* This actually clears the dirty bit in the radix tree. */
		set_page_writeback(vmpage);
		vvp_write_pending(cl2ccc(slice->cpl_obj), cl2ccc_page(slice));
		CL_PAGE_HEADER(D_PAGE, env, pg, "readied\n");
	} else if (pg->cp_state == CPS_PAGEOUT) {
		/* is it possible for osc_flush_async_page() to already
		 * make it ready?
		 */
		result = -EALREADY;
	} else {
		CL_PAGE_DEBUG(D_ERROR, env, pg, "Unexpecting page state %d.\n",
			      pg->cp_state);
		LBUG();
	}
	unlock_page(vmpage);
	return result;
}

static int vvp_page_print(const struct lu_env *env,
			  const struct cl_page_slice *slice,
			  void *cookie, lu_printer_t printer)
{
	struct ccc_page *vp = cl2ccc_page(slice);
	struct page      *vmpage = vp->cpg_page;

	(*printer)(env, cookie, LUSTRE_VVP_NAME "-page@%p(%d:%d:%d) vm@%p ",
		   vp, vp->cpg_defer_uptodate, vp->cpg_ra_used,
		   vp->cpg_write_queued, vmpage);
	if (vmpage) {
		(*printer)(env, cookie, "%lx %d:%d %lx %lu %slru",
			   (long)vmpage->flags, page_count(vmpage),
			   page_mapcount(vmpage), vmpage->private,
			   page_index(vmpage),
			   list_empty(&vmpage->lru) ? "not-" : "");
	}
	(*printer)(env, cookie, "\n");
	return 0;
}

static const struct cl_page_operations vvp_page_ops = {
	.cpo_own	   = vvp_page_own,
	.cpo_assume	= vvp_page_assume,
	.cpo_unassume      = vvp_page_unassume,
	.cpo_disown	= vvp_page_disown,
	.cpo_vmpage	= ccc_page_vmpage,
	.cpo_discard       = vvp_page_discard,
	.cpo_delete	= vvp_page_delete,
	.cpo_unmap	 = vvp_page_unmap,
	.cpo_export	= vvp_page_export,
	.cpo_is_vmlocked   = vvp_page_is_vmlocked,
	.cpo_fini	  = vvp_page_fini,
	.cpo_print	 = vvp_page_print,
	.cpo_is_under_lock = ccc_page_is_under_lock,
	.io = {
		[CRT_READ] = {
			.cpo_prep	= vvp_page_prep_read,
			.cpo_completion  = vvp_page_completion_read,
			.cpo_make_ready  = ccc_fail,
		},
		[CRT_WRITE] = {
			.cpo_prep	= vvp_page_prep_write,
			.cpo_completion  = vvp_page_completion_write,
			.cpo_make_ready  = vvp_page_make_ready,
		}
	}
};

static void vvp_transient_page_verify(const struct cl_page *page)
{
	struct inode *inode = ccc_object_inode(page->cp_obj);

	LASSERT(!inode_trylock(inode));
}

static int vvp_transient_page_own(const struct lu_env *env,
				  const struct cl_page_slice *slice,
				  struct cl_io *unused, int nonblock)
{
	vvp_transient_page_verify(slice->cpl_page);
	return 0;
}

static void vvp_transient_page_assume(const struct lu_env *env,
				      const struct cl_page_slice *slice,
				      struct cl_io *unused)
{
	vvp_transient_page_verify(slice->cpl_page);
}

static void vvp_transient_page_unassume(const struct lu_env *env,
					const struct cl_page_slice *slice,
					struct cl_io *unused)
{
	vvp_transient_page_verify(slice->cpl_page);
}

static void vvp_transient_page_disown(const struct lu_env *env,
				      const struct cl_page_slice *slice,
				      struct cl_io *unused)
{
	vvp_transient_page_verify(slice->cpl_page);
}

static void vvp_transient_page_discard(const struct lu_env *env,
				       const struct cl_page_slice *slice,
				       struct cl_io *unused)
{
	struct cl_page *page = slice->cpl_page;

	vvp_transient_page_verify(slice->cpl_page);

	/*
	 * For transient pages, remove it from the radix tree.
	 */
	cl_page_delete(env, page);
}

static int vvp_transient_page_is_vmlocked(const struct lu_env *env,
					  const struct cl_page_slice *slice)
{
	struct inode    *inode = ccc_object_inode(slice->cpl_obj);
	int	locked;

	locked = !inode_trylock(inode);
	if (!locked)
		inode_unlock(inode);
	return locked ? -EBUSY : -ENODATA;
}

static void
vvp_transient_page_completion(const struct lu_env *env,
			      const struct cl_page_slice *slice,
			      int ioret)
{
	vvp_transient_page_verify(slice->cpl_page);
}

static void vvp_transient_page_fini(const struct lu_env *env,
				    struct cl_page_slice *slice)
{
	struct ccc_page *cp = cl2ccc_page(slice);
	struct cl_page *clp = slice->cpl_page;
	struct ccc_object *clobj = cl2ccc(clp->cp_obj);

	vvp_page_fini_common(cp);
	LASSERT(!inode_trylock(clobj->cob_inode));
	clobj->cob_transient_pages--;
}

static const struct cl_page_operations vvp_transient_page_ops = {
	.cpo_own	   = vvp_transient_page_own,
	.cpo_assume	= vvp_transient_page_assume,
	.cpo_unassume      = vvp_transient_page_unassume,
	.cpo_disown	= vvp_transient_page_disown,
	.cpo_discard       = vvp_transient_page_discard,
	.cpo_vmpage	= ccc_page_vmpage,
	.cpo_fini	  = vvp_transient_page_fini,
	.cpo_is_vmlocked   = vvp_transient_page_is_vmlocked,
	.cpo_print	 = vvp_page_print,
	.cpo_is_under_lock = ccc_page_is_under_lock,
	.io = {
		[CRT_READ] = {
			.cpo_prep	= ccc_transient_page_prep,
			.cpo_completion  = vvp_transient_page_completion,
		},
		[CRT_WRITE] = {
			.cpo_prep	= ccc_transient_page_prep,
			.cpo_completion  = vvp_transient_page_completion,
		}
	}
};

int vvp_page_init(const struct lu_env *env, struct cl_object *obj,
		  struct cl_page *page, struct page *vmpage)
{
	struct ccc_page *cpg = cl_object_page_slice(obj, page);

	CLOBINVRNT(env, obj, ccc_object_invariant(obj));

	cpg->cpg_page = vmpage;
	get_page(vmpage);

	INIT_LIST_HEAD(&cpg->cpg_pending_linkage);
	if (page->cp_type == CPT_CACHEABLE) {
		SetPagePrivate(vmpage);
		vmpage->private = (unsigned long)page;
		cl_page_slice_add(page, &cpg->cpg_cl, obj, &vvp_page_ops);
	} else {
		struct ccc_object *clobj = cl2ccc(obj);

		LASSERT(!inode_trylock(clobj->cob_inode));
		cl_page_slice_add(page, &cpg->cpg_cl, obj,
				  &vvp_transient_page_ops);
		clobj->cob_transient_pages++;
	}
	return 0;
}

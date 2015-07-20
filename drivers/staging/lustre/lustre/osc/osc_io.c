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
 * Copyright (c) 2011, 2012, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 *
 * Implementation of cl_io for OSC layer.
 *
 *   Author: Nikita Danilov <nikita.danilov@sun.com>
 *   Author: Jinshan Xiong <jinshan.xiong@whamcloud.com>
 */

#define DEBUG_SUBSYSTEM S_OSC

#include "osc_cl_internal.h"

/** \addtogroup osc
 *  @{
 */

/*****************************************************************************
 *
 * Type conversions.
 *
 */

static struct osc_req *cl2osc_req(const struct cl_req_slice *slice)
{
	LINVRNT(slice->crs_dev->cd_lu_dev.ld_type == &osc_device_type);
	return container_of0(slice, struct osc_req, or_cl);
}

static struct osc_io *cl2osc_io(const struct lu_env *env,
				const struct cl_io_slice *slice)
{
	struct osc_io *oio = container_of0(slice, struct osc_io, oi_cl);

	LINVRNT(oio == osc_env_io(env));
	return oio;
}

static struct osc_page *osc_cl_page_osc(struct cl_page *page)
{
	const struct cl_page_slice *slice;

	slice = cl_page_at(page, &osc_device_type);
	LASSERT(slice != NULL);

	return cl2osc_page(slice);
}


/*****************************************************************************
 *
 * io operations.
 *
 */

static void osc_io_fini(const struct lu_env *env, const struct cl_io_slice *io)
{
}

/**
 * An implementation of cl_io_operations::cio_io_submit() method for osc
 * layer. Iterates over pages in the in-queue, prepares each for io by calling
 * cl_page_prep() and then either submits them through osc_io_submit_page()
 * or, if page is already submitted, changes osc flags through
 * osc_set_async_flags().
 */
static int osc_io_submit(const struct lu_env *env,
			 const struct cl_io_slice *ios,
			 enum cl_req_type crt, struct cl_2queue *queue)
{
	struct cl_page *page;
	struct cl_page *tmp;
	struct client_obd *cli = NULL;
	struct osc_object *osc = NULL; /* to keep gcc happy */
	struct osc_page *opg;
	struct cl_io *io;
	LIST_HEAD(list);

	struct cl_page_list *qin = &queue->c2_qin;
	struct cl_page_list *qout = &queue->c2_qout;
	int queued = 0;
	int result = 0;
	int cmd;
	int brw_flags;
	int max_pages;

	LASSERT(qin->pl_nr > 0);

	CDEBUG(D_CACHE, "%d %d\n", qin->pl_nr, crt);

	osc = cl2osc(ios->cis_obj);
	cli = osc_cli(osc);
	max_pages = cli->cl_max_pages_per_rpc;

	cmd = crt == CRT_WRITE ? OBD_BRW_WRITE : OBD_BRW_READ;
	brw_flags = osc_io_srvlock(cl2osc_io(env, ios)) ? OBD_BRW_SRVLOCK : 0;

	/*
	 * NOTE: here @page is a top-level page. This is done to avoid
	 *       creation of sub-page-list.
	 */
	cl_page_list_for_each_safe(page, tmp, qin) {
		struct osc_async_page *oap;

		/* Top level IO. */
		io = page->cp_owner;
		LASSERT(io != NULL);

		opg = osc_cl_page_osc(page);
		oap = &opg->ops_oap;
		LASSERT(osc == oap->oap_obj);

		if (!list_empty(&oap->oap_pending_item) ||
		    !list_empty(&oap->oap_rpc_item)) {
			CDEBUG(D_CACHE, "Busy oap %p page %p for submit.\n",
			       oap, opg);
			result = -EBUSY;
			break;
		}

		result = cl_page_prep(env, io, page, crt);
		if (result != 0) {
			LASSERT(result < 0);
			if (result != -EALREADY)
				break;
			/*
			 * Handle -EALREADY error: for read case, the page is
			 * already in UPTODATE state; for write, the page
			 * is not dirty.
			 */
			result = 0;
			continue;
		}

		cl_page_list_move(qout, qin, page);
		oap->oap_async_flags = ASYNC_URGENT|ASYNC_READY;
		oap->oap_async_flags |= ASYNC_COUNT_STABLE;

		osc_page_submit(env, opg, crt, brw_flags);
		list_add_tail(&oap->oap_pending_item, &list);
		if (++queued == max_pages) {
			queued = 0;
			result = osc_queue_sync_pages(env, osc, &list, cmd,
						      brw_flags);
			if (result < 0)
				break;
		}
	}

	if (queued > 0)
		result = osc_queue_sync_pages(env, osc, &list, cmd, brw_flags);

	CDEBUG(D_INFO, "%d/%d %d\n", qin->pl_nr, qout->pl_nr, result);
	return qout->pl_nr > 0 ? 0 : result;
}

static void osc_page_touch_at(const struct lu_env *env,
			      struct cl_object *obj, pgoff_t idx, unsigned to)
{
	struct lov_oinfo *loi = cl2osc(obj)->oo_oinfo;
	struct cl_attr *attr = &osc_env_info(env)->oti_attr;
	int valid;
	__u64 kms;

	/* offset within stripe */
	kms = cl_offset(obj, idx) + to;

	cl_object_attr_lock(obj);
	/*
	 * XXX old code used
	 *
	 *	 ll_inode_size_lock(inode, 0); lov_stripe_lock(lsm);
	 *
	 * here
	 */
	CDEBUG(D_INODE, "stripe KMS %sincreasing %llu->%llu %llu\n",
	       kms > loi->loi_kms ? "" : "not ", loi->loi_kms, kms,
	       loi->loi_lvb.lvb_size);

	valid = 0;
	if (kms > loi->loi_kms) {
		attr->cat_kms = kms;
		valid |= CAT_KMS;
	}
	if (kms > loi->loi_lvb.lvb_size) {
		attr->cat_size = kms;
		valid |= CAT_SIZE;
	}
	cl_object_attr_set(env, obj, attr, valid);
	cl_object_attr_unlock(obj);
}

/**
 * This is called when a page is accessed within file in a way that creates
 * new page, if one were missing (i.e., if there were a hole at that place in
 * the file, or accessed page is beyond the current file size). Examples:
 * ->commit_write() and ->nopage() methods.
 *
 * Expand stripe KMS if necessary.
 */
static void osc_page_touch(const struct lu_env *env,
			   struct osc_page *opage, unsigned to)
{
	struct cl_page *page = opage->ops_cl.cpl_page;
	struct cl_object *obj = opage->ops_cl.cpl_obj;

	osc_page_touch_at(env, obj, page->cp_index, to);
}

/**
 * Implements cl_io_operations::cio_prepare_write() method for osc layer.
 *
 * \retval -EIO transfer initiated against this osc will most likely fail
 * \retval 0    transfer initiated against this osc will most likely succeed.
 *
 * The reason for this check is to immediately return an error to the caller
 * in the case of a deactivated import. Note, that import can be deactivated
 * later, while pages, dirtied by this IO, are still in the cache, but this is
 * irrelevant, because that would still return an error to the application (if
 * it does fsync), but many applications don't do fsync because of performance
 * issues, and we wanted to return an -EIO at write time to notify the
 * application.
 */
static int osc_io_prepare_write(const struct lu_env *env,
				const struct cl_io_slice *ios,
				const struct cl_page_slice *slice,
				unsigned from, unsigned to)
{
	struct osc_device *dev = lu2osc_dev(slice->cpl_obj->co_lu.lo_dev);
	struct obd_import *imp = class_exp2cliimp(dev->od_exp);
	struct osc_io *oio = cl2osc_io(env, ios);
	int result = 0;

	/*
	 * This implements OBD_BRW_CHECK logic from old client.
	 */

	if (imp == NULL || imp->imp_invalid)
		result = -EIO;
	if (result == 0 && oio->oi_lockless)
		/* this page contains `invalid' data, but who cares?
		 * nobody can access the invalid data.
		 * in osc_io_commit_write(), we're going to write exact
		 * [from, to) bytes of this page to OST. -jay */
		cl_page_export(env, slice->cpl_page, 1);

	return result;
}

static int osc_io_commit_write(const struct lu_env *env,
			       const struct cl_io_slice *ios,
			       const struct cl_page_slice *slice,
			       unsigned from, unsigned to)
{
	struct osc_io *oio = cl2osc_io(env, ios);
	struct osc_page *opg = cl2osc_page(slice);
	struct osc_object *obj = cl2osc(opg->ops_cl.cpl_obj);
	struct osc_async_page *oap = &opg->ops_oap;

	LASSERT(to > 0);
	/*
	 * XXX instead of calling osc_page_touch() here and in
	 * osc_io_fault_start() it might be more logical to introduce
	 * cl_page_touch() method, that generic cl_io_commit_write() and page
	 * fault code calls.
	 */
	osc_page_touch(env, cl2osc_page(slice), to);
	if (!client_is_remote(osc_export(obj)) &&
	    capable(CFS_CAP_SYS_RESOURCE))
		oap->oap_brw_flags |= OBD_BRW_NOQUOTA;

	if (oio->oi_lockless)
		/* see osc_io_prepare_write() for lockless io handling. */
		cl_page_clip(env, slice->cpl_page, from, to);

	return 0;
}

static int osc_io_fault_start(const struct lu_env *env,
			      const struct cl_io_slice *ios)
{
	struct cl_io *io;
	struct cl_fault_io *fio;

	io = ios->cis_io;
	fio = &io->u.ci_fault;
	CDEBUG(D_INFO, "%lu %d %d\n",
	       fio->ft_index, fio->ft_writable, fio->ft_nob);
	/*
	 * If mapping is writeable, adjust kms to cover this page,
	 * but do not extend kms beyond actual file size.
	 * See bug 10919.
	 */
	if (fio->ft_writable)
		osc_page_touch_at(env, ios->cis_obj,
				  fio->ft_index, fio->ft_nob);
	return 0;
}

static int osc_async_upcall(void *a, int rc)
{
	struct osc_async_cbargs *args = a;

	args->opc_rc = rc;
	complete(&args->opc_sync);
	return 0;
}

/**
 * Checks that there are no pages being written in the extent being truncated.
 */
static int trunc_check_cb(const struct lu_env *env, struct cl_io *io,
			  struct cl_page *page, void *cbdata)
{
	const struct cl_page_slice *slice;
	struct osc_page *ops;
	struct osc_async_page *oap;
	__u64 start = *(__u64 *)cbdata;

	slice = cl_page_at(page, &osc_device_type);
	LASSERT(slice != NULL);
	ops = cl2osc_page(slice);
	oap = &ops->ops_oap;

	if (oap->oap_cmd & OBD_BRW_WRITE &&
	    !list_empty(&oap->oap_pending_item))
		CL_PAGE_DEBUG(D_ERROR, env, page, "exists %llu/%s.\n",
				start, current->comm);

	{
		struct page *vmpage = cl_page_vmpage(env, page);

		if (PageLocked(vmpage))
			CDEBUG(D_CACHE, "page %p index %lu locked for %d.\n",
			       ops, page->cp_index,
			       (oap->oap_cmd & OBD_BRW_RWMASK));
	}

	return CLP_GANG_OKAY;
}

static void osc_trunc_check(const struct lu_env *env, struct cl_io *io,
			    struct osc_io *oio, __u64 size)
{
	struct cl_object *clob;
	int partial;
	pgoff_t start;

	clob = oio->oi_cl.cis_obj;
	start = cl_index(clob, size);
	partial = cl_offset(clob, start) < size;

	/*
	 * Complain if there are pages in the truncated region.
	 */
	cl_page_gang_lookup(env, clob, io, start + partial, CL_PAGE_EOF,
			    trunc_check_cb, (void *)&size);
}

static int osc_io_setattr_start(const struct lu_env *env,
				const struct cl_io_slice *slice)
{
	struct cl_io *io = slice->cis_io;
	struct osc_io *oio = cl2osc_io(env, slice);
	struct cl_object *obj = slice->cis_obj;
	struct lov_oinfo *loi = cl2osc(obj)->oo_oinfo;
	struct cl_attr *attr = &osc_env_info(env)->oti_attr;
	struct obdo *oa = &oio->oi_oa;
	struct osc_async_cbargs *cbargs = &oio->oi_cbarg;
	__u64 size = io->u.ci_setattr.sa_attr.lvb_size;
	unsigned int ia_valid = io->u.ci_setattr.sa_valid;
	int result = 0;
	struct obd_info oinfo = { { { 0 } } };

	/* truncate cache dirty pages first */
	if (cl_io_is_trunc(io))
		result = osc_cache_truncate_start(env, oio, cl2osc(obj), size);

	if (result == 0 && oio->oi_lockless == 0) {
		cl_object_attr_lock(obj);
		result = cl_object_attr_get(env, obj, attr);
		if (result == 0) {
			struct ost_lvb *lvb = &io->u.ci_setattr.sa_attr;
			unsigned int cl_valid = 0;

			if (ia_valid & ATTR_SIZE) {
				attr->cat_size = attr->cat_kms = size;
				cl_valid = CAT_SIZE | CAT_KMS;
			}
			if (ia_valid & ATTR_MTIME_SET) {
				attr->cat_mtime = lvb->lvb_mtime;
				cl_valid |= CAT_MTIME;
			}
			if (ia_valid & ATTR_ATIME_SET) {
				attr->cat_atime = lvb->lvb_atime;
				cl_valid |= CAT_ATIME;
			}
			if (ia_valid & ATTR_CTIME_SET) {
				attr->cat_ctime = lvb->lvb_ctime;
				cl_valid |= CAT_CTIME;
			}
			result = cl_object_attr_set(env, obj, attr, cl_valid);
		}
		cl_object_attr_unlock(obj);
	}
	memset(oa, 0, sizeof(*oa));
	if (result == 0) {
		oa->o_oi = loi->loi_oi;
		oa->o_mtime = attr->cat_mtime;
		oa->o_atime = attr->cat_atime;
		oa->o_ctime = attr->cat_ctime;
		oa->o_valid = OBD_MD_FLID | OBD_MD_FLGROUP | OBD_MD_FLATIME |
			OBD_MD_FLCTIME | OBD_MD_FLMTIME;
		if (ia_valid & ATTR_SIZE) {
			oa->o_size = size;
			oa->o_blocks = OBD_OBJECT_EOF;
			oa->o_valid |= OBD_MD_FLSIZE | OBD_MD_FLBLOCKS;

			if (oio->oi_lockless) {
				oa->o_flags = OBD_FL_SRVLOCK;
				oa->o_valid |= OBD_MD_FLFLAGS;
			}
		} else {
			LASSERT(oio->oi_lockless == 0);
		}

		oinfo.oi_oa = oa;
		oinfo.oi_capa = io->u.ci_setattr.sa_capa;
		init_completion(&cbargs->opc_sync);

		if (ia_valid & ATTR_SIZE)
			result = osc_punch_base(osc_export(cl2osc(obj)),
						&oinfo, osc_async_upcall,
						cbargs, PTLRPCD_SET);
		else
			result = osc_setattr_async_base(osc_export(cl2osc(obj)),
							&oinfo, NULL,
							osc_async_upcall,
							cbargs, PTLRPCD_SET);
		cbargs->opc_rpc_sent = result == 0;
	}
	return result;
}

static void osc_io_setattr_end(const struct lu_env *env,
			       const struct cl_io_slice *slice)
{
	struct cl_io *io = slice->cis_io;
	struct osc_io *oio = cl2osc_io(env, slice);
	struct cl_object *obj = slice->cis_obj;
	struct osc_async_cbargs *cbargs = &oio->oi_cbarg;
	int result = 0;

	if (cbargs->opc_rpc_sent) {
		wait_for_completion(&cbargs->opc_sync);
		result = io->ci_result = cbargs->opc_rc;
	}
	if (result == 0) {
		if (oio->oi_lockless) {
			/* lockless truncate */
			struct osc_device *osd = lu2osc_dev(obj->co_lu.lo_dev);

			LASSERT(cl_io_is_trunc(io));
			/* XXX: Need a lock. */
			osd->od_stats.os_lockless_truncates++;
		}
	}

	if (cl_io_is_trunc(io)) {
		__u64 size = io->u.ci_setattr.sa_attr.lvb_size;

		osc_trunc_check(env, io, oio, size);
		if (oio->oi_trunc != NULL) {
			osc_cache_truncate_end(env, oio, cl2osc(obj));
			oio->oi_trunc = NULL;
		}
	}
}

static int osc_io_read_start(const struct lu_env *env,
			     const struct cl_io_slice *slice)
{
	struct cl_object *obj = slice->cis_obj;
	struct cl_attr *attr = &osc_env_info(env)->oti_attr;
	int rc = 0;

	if (!slice->cis_io->ci_noatime) {
		cl_object_attr_lock(obj);
		attr->cat_atime = LTIME_S(CURRENT_TIME);
		rc = cl_object_attr_set(env, obj, attr, CAT_ATIME);
		cl_object_attr_unlock(obj);
	}
	return rc;
}

static int osc_io_write_start(const struct lu_env *env,
			      const struct cl_io_slice *slice)
{
	struct cl_object *obj = slice->cis_obj;
	struct cl_attr *attr = &osc_env_info(env)->oti_attr;
	int rc = 0;

	OBD_FAIL_TIMEOUT(OBD_FAIL_OSC_DELAY_SETTIME, 1);
	cl_object_attr_lock(obj);
	attr->cat_mtime = attr->cat_ctime = LTIME_S(CURRENT_TIME);
	rc = cl_object_attr_set(env, obj, attr, CAT_MTIME | CAT_CTIME);
	cl_object_attr_unlock(obj);

	return rc;
}

static int osc_fsync_ost(const struct lu_env *env, struct osc_object *obj,
			 struct cl_fsync_io *fio)
{
	struct osc_io *oio = osc_env_io(env);
	struct obdo *oa = &oio->oi_oa;
	struct obd_info *oinfo = &oio->oi_info;
	struct lov_oinfo *loi = obj->oo_oinfo;
	struct osc_async_cbargs *cbargs = &oio->oi_cbarg;
	int rc = 0;

	memset(oa, 0, sizeof(*oa));
	oa->o_oi = loi->loi_oi;
	oa->o_valid = OBD_MD_FLID | OBD_MD_FLGROUP;

	/* reload size abd blocks for start and end of sync range */
	oa->o_size = fio->fi_start;
	oa->o_blocks = fio->fi_end;
	oa->o_valid |= OBD_MD_FLSIZE | OBD_MD_FLBLOCKS;

	obdo_set_parent_fid(oa, fio->fi_fid);

	memset(oinfo, 0, sizeof(*oinfo));
	oinfo->oi_oa = oa;
	oinfo->oi_capa = fio->fi_capa;
	init_completion(&cbargs->opc_sync);

	rc = osc_sync_base(osc_export(obj), oinfo, osc_async_upcall, cbargs,
			   PTLRPCD_SET);
	return rc;
}

static int osc_io_fsync_start(const struct lu_env *env,
			      const struct cl_io_slice *slice)
{
	struct cl_io *io = slice->cis_io;
	struct cl_fsync_io *fio = &io->u.ci_fsync;
	struct cl_object *obj = slice->cis_obj;
	struct osc_object *osc = cl2osc(obj);
	pgoff_t start = cl_index(obj, fio->fi_start);
	pgoff_t end = cl_index(obj, fio->fi_end);
	int result = 0;

	if (fio->fi_end == OBD_OBJECT_EOF)
		end = CL_PAGE_EOF;

	result = osc_cache_writeback_range(env, osc, start, end, 0,
					   fio->fi_mode == CL_FSYNC_DISCARD);
	if (result > 0) {
		fio->fi_nr_written += result;
		result = 0;
	}
	if (fio->fi_mode == CL_FSYNC_ALL) {
		int rc;

		/* we have to wait for writeback to finish before we can
		 * send OST_SYNC RPC. This is bad because it causes extents
		 * to be written osc by osc. However, we usually start
		 * writeback before CL_FSYNC_ALL so this won't have any real
		 * problem. */
		rc = osc_cache_wait_range(env, osc, start, end);
		if (result == 0)
			result = rc;
		rc = osc_fsync_ost(env, osc, fio);
		if (result == 0)
			result = rc;
	}

	return result;
}

static void osc_io_fsync_end(const struct lu_env *env,
			     const struct cl_io_slice *slice)
{
	struct cl_fsync_io *fio = &slice->cis_io->u.ci_fsync;
	struct cl_object *obj = slice->cis_obj;
	pgoff_t start = cl_index(obj, fio->fi_start);
	pgoff_t end = cl_index(obj, fio->fi_end);
	int result = 0;

	if (fio->fi_mode == CL_FSYNC_LOCAL) {
		result = osc_cache_wait_range(env, cl2osc(obj), start, end);
	} else if (fio->fi_mode == CL_FSYNC_ALL) {
		struct osc_io *oio = cl2osc_io(env, slice);
		struct osc_async_cbargs *cbargs = &oio->oi_cbarg;

		wait_for_completion(&cbargs->opc_sync);
		if (result == 0)
			result = cbargs->opc_rc;
	}
	slice->cis_io->ci_result = result;
}

static void osc_io_end(const struct lu_env *env,
		       const struct cl_io_slice *slice)
{
	struct osc_io *oio = cl2osc_io(env, slice);

	if (oio->oi_active) {
		osc_extent_release(env, oio->oi_active);
		oio->oi_active = NULL;
	}
}

static const struct cl_io_operations osc_io_ops = {
	.op = {
		[CIT_READ] = {
			.cio_start  = osc_io_read_start,
			.cio_fini   = osc_io_fini
		},
		[CIT_WRITE] = {
			.cio_start  = osc_io_write_start,
			.cio_end    = osc_io_end,
			.cio_fini   = osc_io_fini
		},
		[CIT_SETATTR] = {
			.cio_start  = osc_io_setattr_start,
			.cio_end    = osc_io_setattr_end
		},
		[CIT_FAULT] = {
			.cio_start  = osc_io_fault_start,
			.cio_end    = osc_io_end,
			.cio_fini   = osc_io_fini
		},
		[CIT_FSYNC] = {
			.cio_start  = osc_io_fsync_start,
			.cio_end    = osc_io_fsync_end,
			.cio_fini   = osc_io_fini
		},
		[CIT_MISC] = {
			.cio_fini   = osc_io_fini
		}
	},
	.req_op = {
		 [CRT_READ] = {
			 .cio_submit    = osc_io_submit
		 },
		 [CRT_WRITE] = {
			 .cio_submit    = osc_io_submit
		 }
	 },
	.cio_prepare_write = osc_io_prepare_write,
	.cio_commit_write  = osc_io_commit_write
};

/*****************************************************************************
 *
 * Transfer operations.
 *
 */

static int osc_req_prep(const struct lu_env *env,
			const struct cl_req_slice *slice)
{
	return 0;
}

static void osc_req_completion(const struct lu_env *env,
			       const struct cl_req_slice *slice, int ioret)
{
	struct osc_req *or;

	or = cl2osc_req(slice);
	OBD_SLAB_FREE_PTR(or, osc_req_kmem);
}

/**
 * Implementation of struct cl_req_operations::cro_attr_set() for osc
 * layer. osc is responsible for struct obdo::o_id and struct obdo::o_seq
 * fields.
 */
static void osc_req_attr_set(const struct lu_env *env,
			     const struct cl_req_slice *slice,
			     const struct cl_object *obj,
			     struct cl_req_attr *attr, u64 flags)
{
	struct lov_oinfo *oinfo;
	struct cl_req *clerq;
	struct cl_page *apage; /* _some_ page in @clerq */
	struct cl_lock *lock;  /* _some_ lock protecting @apage */
	struct osc_lock *olck;
	struct osc_page *opg;
	struct obdo *oa;
	struct ost_lvb *lvb;

	oinfo = cl2osc(obj)->oo_oinfo;
	lvb = &oinfo->loi_lvb;
	oa = attr->cra_oa;

	if ((flags & OBD_MD_FLMTIME) != 0) {
		oa->o_mtime = lvb->lvb_mtime;
		oa->o_valid |= OBD_MD_FLMTIME;
	}
	if ((flags & OBD_MD_FLATIME) != 0) {
		oa->o_atime = lvb->lvb_atime;
		oa->o_valid |= OBD_MD_FLATIME;
	}
	if ((flags & OBD_MD_FLCTIME) != 0) {
		oa->o_ctime = lvb->lvb_ctime;
		oa->o_valid |= OBD_MD_FLCTIME;
	}
	if (flags & OBD_MD_FLGROUP) {
		ostid_set_seq(&oa->o_oi, ostid_seq(&oinfo->loi_oi));
		oa->o_valid |= OBD_MD_FLGROUP;
	}
	if (flags & OBD_MD_FLID) {
		ostid_set_id(&oa->o_oi, ostid_id(&oinfo->loi_oi));
		oa->o_valid |= OBD_MD_FLID;
	}
	if (flags & OBD_MD_FLHANDLE) {
		clerq = slice->crs_req;
		LASSERT(!list_empty(&clerq->crq_pages));
		apage = container_of(clerq->crq_pages.next,
				     struct cl_page, cp_flight);
		opg = osc_cl_page_osc(apage);
		apage = opg->ops_cl.cpl_page; /* now apage is a sub-page */
		lock = cl_lock_at_page(env, apage->cp_obj, apage, NULL, 1, 1);
		if (lock == NULL) {
			struct cl_object_header *head;
			struct cl_lock *scan;

			head = cl_object_header(apage->cp_obj);
			list_for_each_entry(scan, &head->coh_locks,
						cll_linkage)
				CL_LOCK_DEBUG(D_ERROR, env, scan,
					      "no cover page!\n");
			CL_PAGE_DEBUG(D_ERROR, env, apage,
				      "dump uncover page!\n");
			dump_stack();
			LBUG();
		}

		olck = osc_lock_at(lock);
		LASSERT(olck != NULL);
		LASSERT(ergo(opg->ops_srvlock, olck->ols_lock == NULL));
		/* check for lockless io. */
		if (olck->ols_lock != NULL) {
			oa->o_handle = olck->ols_lock->l_remote_handle;
			oa->o_valid |= OBD_MD_FLHANDLE;
		}
		cl_lock_put(env, lock);
	}
}

static const struct cl_req_operations osc_req_ops = {
	.cro_prep       = osc_req_prep,
	.cro_attr_set   = osc_req_attr_set,
	.cro_completion = osc_req_completion
};


int osc_io_init(const struct lu_env *env,
		struct cl_object *obj, struct cl_io *io)
{
	struct osc_io *oio = osc_env_io(env);

	CL_IO_SLICE_CLEAN(oio, oi_cl);
	cl_io_slice_add(io, &oio->oi_cl, obj, &osc_io_ops);
	return 0;
}

int osc_req_init(const struct lu_env *env, struct cl_device *dev,
		 struct cl_req *req)
{
	struct osc_req *or;
	int result;

	OBD_SLAB_ALLOC_PTR_GFP(or, osc_req_kmem, GFP_NOFS);
	if (or != NULL) {
		cl_req_slice_add(req, &or->or_cl, dev, &osc_req_ops);
		result = 0;
	} else
		result = -ENOMEM;
	return result;
}

/** @} osc */

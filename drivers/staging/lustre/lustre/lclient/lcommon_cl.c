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
 * cl code shared between vvp and liblustre (and other Lustre clients in the
 * future).
 *
 *   Author: Nikita Danilov <nikita.danilov@sun.com>
 */

#define DEBUG_SUBSYSTEM S_LLITE

#include "../../include/linux/libcfs/libcfs.h"
# include <linux/fs.h>
# include <linux/sched.h>
# include <linux/mm.h>
# include <linux/quotaops.h>
# include <linux/highmem.h>
# include <linux/pagemap.h>
# include <linux/rbtree.h>

#include "../include/obd.h"
#include "../include/obd_support.h"
#include "../include/lustre_fid.h"
#include "../include/lustre_lite.h"
#include "../include/lustre_dlm.h"
#include "../include/lustre_ver.h"
#include "../include/lustre_mdc.h"
#include "../include/cl_object.h"

#include "../include/lclient.h"

#include "../llite/llite_internal.h"

static const struct cl_req_operations ccc_req_ops;

/*
 * ccc_ prefix stands for "Common Client Code".
 */

static struct kmem_cache *ccc_lock_kmem;
static struct kmem_cache *ccc_object_kmem;
static struct kmem_cache *ccc_thread_kmem;
static struct kmem_cache *ccc_session_kmem;
static struct kmem_cache *ccc_req_kmem;

static struct lu_kmem_descr ccc_caches[] = {
	{
		.ckd_cache = &ccc_lock_kmem,
		.ckd_name  = "ccc_lock_kmem",
		.ckd_size  = sizeof(struct ccc_lock)
	},
	{
		.ckd_cache = &ccc_object_kmem,
		.ckd_name  = "ccc_object_kmem",
		.ckd_size  = sizeof(struct ccc_object)
	},
	{
		.ckd_cache = &ccc_thread_kmem,
		.ckd_name  = "ccc_thread_kmem",
		.ckd_size  = sizeof(struct ccc_thread_info),
	},
	{
		.ckd_cache = &ccc_session_kmem,
		.ckd_name  = "ccc_session_kmem",
		.ckd_size  = sizeof(struct ccc_session)
	},
	{
		.ckd_cache = &ccc_req_kmem,
		.ckd_name  = "ccc_req_kmem",
		.ckd_size  = sizeof(struct ccc_req)
	},
	{
		.ckd_cache = NULL
	}
};

/*****************************************************************************
 *
 * Vvp device and device type functions.
 *
 */

void *ccc_key_init(const struct lu_context *ctx, struct lu_context_key *key)
{
	struct ccc_thread_info *info;

	OBD_SLAB_ALLOC_PTR_GFP(info, ccc_thread_kmem, GFP_NOFS);
	if (info == NULL)
		info = ERR_PTR(-ENOMEM);
	return info;
}

void ccc_key_fini(const struct lu_context *ctx,
			 struct lu_context_key *key, void *data)
{
	struct ccc_thread_info *info = data;

	OBD_SLAB_FREE_PTR(info, ccc_thread_kmem);
}

void *ccc_session_key_init(const struct lu_context *ctx,
				  struct lu_context_key *key)
{
	struct ccc_session *session;

	OBD_SLAB_ALLOC_PTR_GFP(session, ccc_session_kmem, GFP_NOFS);
	if (session == NULL)
		session = ERR_PTR(-ENOMEM);
	return session;
}

void ccc_session_key_fini(const struct lu_context *ctx,
				 struct lu_context_key *key, void *data)
{
	struct ccc_session *session = data;

	OBD_SLAB_FREE_PTR(session, ccc_session_kmem);
}

struct lu_context_key ccc_key = {
	.lct_tags = LCT_CL_THREAD,
	.lct_init = ccc_key_init,
	.lct_fini = ccc_key_fini
};

struct lu_context_key ccc_session_key = {
	.lct_tags = LCT_SESSION,
	.lct_init = ccc_session_key_init,
	.lct_fini = ccc_session_key_fini
};


/* type constructor/destructor: ccc_type_{init,fini,start,stop}(). */
/* LU_TYPE_INIT_FINI(ccc, &ccc_key, &ccc_session_key); */

int ccc_device_init(const struct lu_env *env, struct lu_device *d,
			   const char *name, struct lu_device *next)
{
	struct ccc_device  *vdv;
	int rc;

	vdv = lu2ccc_dev(d);
	vdv->cdv_next = lu2cl_dev(next);

	LASSERT(d->ld_site != NULL && next->ld_type != NULL);
	next->ld_site = d->ld_site;
	rc = next->ld_type->ldt_ops->ldto_device_init(
			env, next, next->ld_type->ldt_name, NULL);
	if (rc == 0) {
		lu_device_get(next);
		lu_ref_add(&next->ld_reference, "lu-stack", &lu_site_init);
	}
	return rc;
}

struct lu_device *ccc_device_fini(const struct lu_env *env,
					 struct lu_device *d)
{
	return cl2lu_dev(lu2ccc_dev(d)->cdv_next);
}

struct lu_device *ccc_device_alloc(const struct lu_env *env,
				   struct lu_device_type *t,
				   struct lustre_cfg *cfg,
				   const struct lu_device_operations *luops,
				   const struct cl_device_operations *clops)
{
	struct ccc_device *vdv;
	struct lu_device  *lud;
	struct cl_site    *site;
	int rc;

	OBD_ALLOC_PTR(vdv);
	if (vdv == NULL)
		return ERR_PTR(-ENOMEM);

	lud = &vdv->cdv_cl.cd_lu_dev;
	cl_device_init(&vdv->cdv_cl, t);
	ccc2lu_dev(vdv)->ld_ops = luops;
	vdv->cdv_cl.cd_ops = clops;

	OBD_ALLOC_PTR(site);
	if (site != NULL) {
		rc = cl_site_init(site, &vdv->cdv_cl);
		if (rc == 0)
			rc = lu_site_init_finish(&site->cs_lu);
		else {
			LASSERT(lud->ld_site == NULL);
			CERROR("Cannot init lu_site, rc %d.\n", rc);
			OBD_FREE_PTR(site);
		}
	} else
		rc = -ENOMEM;
	if (rc != 0) {
		ccc_device_free(env, lud);
		lud = ERR_PTR(rc);
	}
	return lud;
}

struct lu_device *ccc_device_free(const struct lu_env *env,
					 struct lu_device *d)
{
	struct ccc_device *vdv  = lu2ccc_dev(d);
	struct cl_site    *site = lu2cl_site(d->ld_site);
	struct lu_device  *next = cl2lu_dev(vdv->cdv_next);

	if (d->ld_site != NULL) {
		cl_site_fini(site);
		OBD_FREE_PTR(site);
	}
	cl_device_fini(lu2cl_dev(d));
	OBD_FREE_PTR(vdv);
	return next;
}

int ccc_req_init(const struct lu_env *env, struct cl_device *dev,
			struct cl_req *req)
{
	struct ccc_req *vrq;
	int result;

	OBD_SLAB_ALLOC_PTR_GFP(vrq, ccc_req_kmem, GFP_NOFS);
	if (vrq != NULL) {
		cl_req_slice_add(req, &vrq->crq_cl, dev, &ccc_req_ops);
		result = 0;
	} else
		result = -ENOMEM;
	return result;
}

/**
 * An `emergency' environment used by ccc_inode_fini() when cl_env_get()
 * fails. Access to this environment is serialized by ccc_inode_fini_guard
 * mutex.
 */
static struct lu_env *ccc_inode_fini_env;

/**
 * A mutex serializing calls to slp_inode_fini() under extreme memory
 * pressure, when environments cannot be allocated.
 */
static DEFINE_MUTEX(ccc_inode_fini_guard);
static int dummy_refcheck;

int ccc_global_init(struct lu_device_type *device_type)
{
	int result;

	result = lu_kmem_init(ccc_caches);
	if (result)
		return result;

	result = lu_device_type_init(device_type);
	if (result)
		goto out_kmem;

	ccc_inode_fini_env = cl_env_alloc(&dummy_refcheck,
					  LCT_REMEMBER|LCT_NOREF);
	if (IS_ERR(ccc_inode_fini_env)) {
		result = PTR_ERR(ccc_inode_fini_env);
		goto out_device;
	}

	ccc_inode_fini_env->le_ctx.lc_cookie = 0x4;
	return 0;
out_device:
	lu_device_type_fini(device_type);
out_kmem:
	lu_kmem_fini(ccc_caches);
	return result;
}

void ccc_global_fini(struct lu_device_type *device_type)
{
	if (ccc_inode_fini_env != NULL) {
		cl_env_put(ccc_inode_fini_env, &dummy_refcheck);
		ccc_inode_fini_env = NULL;
	}
	lu_device_type_fini(device_type);
	lu_kmem_fini(ccc_caches);
}

/*****************************************************************************
 *
 * Object operations.
 *
 */

struct lu_object *ccc_object_alloc(const struct lu_env *env,
				   const struct lu_object_header *unused,
				   struct lu_device *dev,
				   const struct cl_object_operations *clops,
				   const struct lu_object_operations *luops)
{
	struct ccc_object *vob;
	struct lu_object  *obj;

	OBD_SLAB_ALLOC_PTR_GFP(vob, ccc_object_kmem, GFP_NOFS);
	if (vob != NULL) {
		struct cl_object_header *hdr;

		obj = ccc2lu(vob);
		hdr = &vob->cob_header;
		cl_object_header_init(hdr);
		lu_object_init(obj, &hdr->coh_lu, dev);
		lu_object_add_top(&hdr->coh_lu, obj);

		vob->cob_cl.co_ops = clops;
		obj->lo_ops = luops;
	} else
		obj = NULL;
	return obj;
}

int ccc_object_init0(const struct lu_env *env,
			    struct ccc_object *vob,
			    const struct cl_object_conf *conf)
{
	vob->cob_inode = conf->coc_inode;
	vob->cob_transient_pages = 0;
	cl_object_page_init(&vob->cob_cl, sizeof(struct ccc_page));
	return 0;
}

int ccc_object_init(const struct lu_env *env, struct lu_object *obj,
			   const struct lu_object_conf *conf)
{
	struct ccc_device *dev = lu2ccc_dev(obj->lo_dev);
	struct ccc_object *vob = lu2ccc(obj);
	struct lu_object  *below;
	struct lu_device  *under;
	int result;

	under = &dev->cdv_next->cd_lu_dev;
	below = under->ld_ops->ldo_object_alloc(env, obj->lo_header, under);
	if (below != NULL) {
		const struct cl_object_conf *cconf;

		cconf = lu2cl_conf(conf);
		INIT_LIST_HEAD(&vob->cob_pending_list);
		lu_object_add(obj, below);
		result = ccc_object_init0(env, vob, cconf);
	} else
		result = -ENOMEM;
	return result;
}

void ccc_object_free(const struct lu_env *env, struct lu_object *obj)
{
	struct ccc_object *vob = lu2ccc(obj);

	lu_object_fini(obj);
	lu_object_header_fini(obj->lo_header);
	OBD_SLAB_FREE_PTR(vob, ccc_object_kmem);
}

int ccc_lock_init(const struct lu_env *env,
		  struct cl_object *obj, struct cl_lock *lock,
		  const struct cl_io *unused,
		  const struct cl_lock_operations *lkops)
{
	struct ccc_lock *clk;
	int result;

	CLOBINVRNT(env, obj, ccc_object_invariant(obj));

	OBD_SLAB_ALLOC_PTR_GFP(clk, ccc_lock_kmem, GFP_NOFS);
	if (clk != NULL) {
		cl_lock_slice_add(lock, &clk->clk_cl, obj, lkops);
		result = 0;
	} else
		result = -ENOMEM;
	return result;
}

int ccc_attr_set(const struct lu_env *env, struct cl_object *obj,
		 const struct cl_attr *attr, unsigned valid)
{
	return 0;
}

int ccc_object_glimpse(const struct lu_env *env,
		       const struct cl_object *obj, struct ost_lvb *lvb)
{
	struct inode *inode = ccc_object_inode(obj);

	lvb->lvb_mtime = cl_inode_mtime(inode);
	lvb->lvb_atime = cl_inode_atime(inode);
	lvb->lvb_ctime = cl_inode_ctime(inode);
	/*
	 * LU-417: Add dirty pages block count lest i_blocks reports 0, some
	 * "cp" or "tar" on remote node may think it's a completely sparse file
	 * and skip it.
	 */
	if (lvb->lvb_size > 0 && lvb->lvb_blocks == 0)
		lvb->lvb_blocks = dirty_cnt(inode);
	return 0;
}



int ccc_conf_set(const struct lu_env *env, struct cl_object *obj,
			const struct cl_object_conf *conf)
{
	/* TODO: destroy all pages attached to this object. */
	return 0;
}

static void ccc_object_size_lock(struct cl_object *obj)
{
	struct inode *inode = ccc_object_inode(obj);

	cl_isize_lock(inode);
	cl_object_attr_lock(obj);
}

static void ccc_object_size_unlock(struct cl_object *obj)
{
	struct inode *inode = ccc_object_inode(obj);

	cl_object_attr_unlock(obj);
	cl_isize_unlock(inode);
}

/*****************************************************************************
 *
 * Page operations.
 *
 */

struct page *ccc_page_vmpage(const struct lu_env *env,
			    const struct cl_page_slice *slice)
{
	return cl2vm_page(slice);
}

int ccc_page_is_under_lock(const struct lu_env *env,
			   const struct cl_page_slice *slice,
			   struct cl_io *io)
{
	struct ccc_io	*cio  = ccc_env_io(env);
	struct cl_lock_descr *desc = &ccc_env_info(env)->cti_descr;
	struct cl_page       *page = slice->cpl_page;

	int result;

	if (io->ci_type == CIT_READ || io->ci_type == CIT_WRITE ||
	    io->ci_type == CIT_FAULT) {
		if (cio->cui_fd->fd_flags & LL_FILE_GROUP_LOCKED)
			result = -EBUSY;
		else {
			desc->cld_start = page->cp_index;
			desc->cld_end   = page->cp_index;
			desc->cld_obj   = page->cp_obj;
			desc->cld_mode  = CLM_READ;
			result = cl_queue_match(&io->ci_lockset.cls_done,
						desc) ? -EBUSY : 0;
		}
	} else
		result = 0;
	return result;
}

int ccc_fail(const struct lu_env *env, const struct cl_page_slice *slice)
{
	/*
	 * Cached read?
	 */
	LBUG();
	return 0;
}

void ccc_transient_page_verify(const struct cl_page *page)
{
}

int ccc_transient_page_own(const struct lu_env *env,
				   const struct cl_page_slice *slice,
				   struct cl_io *unused,
				   int nonblock)
{
	ccc_transient_page_verify(slice->cpl_page);
	return 0;
}

void ccc_transient_page_assume(const struct lu_env *env,
				      const struct cl_page_slice *slice,
				      struct cl_io *unused)
{
	ccc_transient_page_verify(slice->cpl_page);
}

void ccc_transient_page_unassume(const struct lu_env *env,
					const struct cl_page_slice *slice,
					struct cl_io *unused)
{
	ccc_transient_page_verify(slice->cpl_page);
}

void ccc_transient_page_disown(const struct lu_env *env,
				      const struct cl_page_slice *slice,
				      struct cl_io *unused)
{
	ccc_transient_page_verify(slice->cpl_page);
}

void ccc_transient_page_discard(const struct lu_env *env,
				       const struct cl_page_slice *slice,
				       struct cl_io *unused)
{
	struct cl_page *page = slice->cpl_page;

	ccc_transient_page_verify(slice->cpl_page);

	/*
	 * For transient pages, remove it from the radix tree.
	 */
	cl_page_delete(env, page);
}

int ccc_transient_page_prep(const struct lu_env *env,
				   const struct cl_page_slice *slice,
				   struct cl_io *unused)
{
	/* transient page should always be sent. */
	return 0;
}

/*****************************************************************************
 *
 * Lock operations.
 *
 */

void ccc_lock_delete(const struct lu_env *env,
		     const struct cl_lock_slice *slice)
{
	CLOBINVRNT(env, slice->cls_obj, ccc_object_invariant(slice->cls_obj));
}

void ccc_lock_fini(const struct lu_env *env, struct cl_lock_slice *slice)
{
	struct ccc_lock *clk = cl2ccc_lock(slice);

	OBD_SLAB_FREE_PTR(clk, ccc_lock_kmem);
}

int ccc_lock_enqueue(const struct lu_env *env,
		     const struct cl_lock_slice *slice,
		     struct cl_io *unused, __u32 enqflags)
{
	CLOBINVRNT(env, slice->cls_obj, ccc_object_invariant(slice->cls_obj));
	return 0;
}

int ccc_lock_unuse(const struct lu_env *env, const struct cl_lock_slice *slice)
{
	CLOBINVRNT(env, slice->cls_obj, ccc_object_invariant(slice->cls_obj));
	return 0;
}

int ccc_lock_wait(const struct lu_env *env, const struct cl_lock_slice *slice)
{
	CLOBINVRNT(env, slice->cls_obj, ccc_object_invariant(slice->cls_obj));
	return 0;
}

/**
 * Implementation of cl_lock_operations::clo_fits_into() methods for ccc
 * layer. This function is executed every time io finds an existing lock in
 * the lock cache while creating new lock. This function has to decide whether
 * cached lock "fits" into io.
 *
 * \param slice lock to be checked
 * \param io    IO that wants a lock.
 *
 * \see lov_lock_fits_into().
 */
int ccc_lock_fits_into(const struct lu_env *env,
		       const struct cl_lock_slice *slice,
		       const struct cl_lock_descr *need,
		       const struct cl_io *io)
{
	const struct cl_lock       *lock  = slice->cls_lock;
	const struct cl_lock_descr *descr = &lock->cll_descr;
	const struct ccc_io	*cio   = ccc_env_io(env);
	int			 result;

	/*
	 * Work around DLM peculiarity: it assumes that glimpse
	 * (LDLM_FL_HAS_INTENT) lock is always LCK_PR, and returns reads lock
	 * when asked for LCK_PW lock with LDLM_FL_HAS_INTENT flag set. Make
	 * sure that glimpse doesn't get CLM_WRITE top-lock, so that it
	 * doesn't enqueue CLM_WRITE sub-locks.
	 */
	if (cio->cui_glimpse)
		result = descr->cld_mode != CLM_WRITE;

	/*
	 * Also, don't match incomplete write locks for read, otherwise read
	 * would enqueue missing sub-locks in the write mode.
	 */
	else if (need->cld_mode != descr->cld_mode)
		result = lock->cll_state >= CLS_ENQUEUED;
	else
		result = 1;
	return result;
}

/**
 * Implements cl_lock_operations::clo_state() method for ccc layer, invoked
 * whenever lock state changes. Transfers object attributes, that might be
 * updated as a result of lock acquiring into inode.
 */
void ccc_lock_state(const struct lu_env *env,
		    const struct cl_lock_slice *slice,
		    enum cl_lock_state state)
{
	struct cl_lock *lock = slice->cls_lock;

	/*
	 * Refresh inode attributes when the lock is moving into CLS_HELD
	 * state, and only when this is a result of real enqueue, rather than
	 * of finding lock in the cache.
	 */
	if (state == CLS_HELD && lock->cll_state < CLS_HELD) {
		struct cl_object *obj;
		struct inode     *inode;

		obj   = slice->cls_obj;
		inode = ccc_object_inode(obj);

		/* vmtruncate() sets the i_size
		 * under both a DLM lock and the
		 * ll_inode_size_lock().  If we don't get the
		 * ll_inode_size_lock() here we can match the DLM lock and
		 * reset i_size.  generic_file_write can then trust the
		 * stale i_size when doing appending writes and effectively
		 * cancel the result of the truncate.  Getting the
		 * ll_inode_size_lock() after the enqueue maintains the DLM
		 * -> ll_inode_size_lock() acquiring order. */
		if (lock->cll_descr.cld_start == 0 &&
		    lock->cll_descr.cld_end == CL_PAGE_EOF)
			cl_merge_lvb(env, inode);
	}
}

/*****************************************************************************
 *
 * io operations.
 *
 */

void ccc_io_fini(const struct lu_env *env, const struct cl_io_slice *ios)
{
	struct cl_io *io = ios->cis_io;

	CLOBINVRNT(env, io->ci_obj, ccc_object_invariant(io->ci_obj));
}

int ccc_io_one_lock_index(const struct lu_env *env, struct cl_io *io,
			  __u32 enqflags, enum cl_lock_mode mode,
			  pgoff_t start, pgoff_t end)
{
	struct ccc_io	  *cio   = ccc_env_io(env);
	struct cl_lock_descr   *descr = &cio->cui_link.cill_descr;
	struct cl_object       *obj   = io->ci_obj;

	CLOBINVRNT(env, obj, ccc_object_invariant(obj));

	CDEBUG(D_VFSTRACE, "lock: %d [%lu, %lu]\n", mode, start, end);

	memset(&cio->cui_link, 0, sizeof(cio->cui_link));

	if (cio->cui_fd && (cio->cui_fd->fd_flags & LL_FILE_GROUP_LOCKED)) {
		descr->cld_mode = CLM_GROUP;
		descr->cld_gid  = cio->cui_fd->fd_grouplock.cg_gid;
	} else {
		descr->cld_mode  = mode;
	}
	descr->cld_obj   = obj;
	descr->cld_start = start;
	descr->cld_end   = end;
	descr->cld_enq_flags = enqflags;

	cl_io_lock_add(env, io, &cio->cui_link);
	return 0;
}

void ccc_io_update_iov(const struct lu_env *env,
		       struct ccc_io *cio, struct cl_io *io)
{
	size_t size = io->u.ci_rw.crw_count;

	if (!cl_is_normalio(env, io) || cio->cui_iter == NULL)
		return;

	iov_iter_truncate(cio->cui_iter, size);
}

int ccc_io_one_lock(const struct lu_env *env, struct cl_io *io,
		    __u32 enqflags, enum cl_lock_mode mode,
		    loff_t start, loff_t end)
{
	struct cl_object *obj = io->ci_obj;

	return ccc_io_one_lock_index(env, io, enqflags, mode,
				     cl_index(obj, start), cl_index(obj, end));
}

void ccc_io_end(const struct lu_env *env, const struct cl_io_slice *ios)
{
	CLOBINVRNT(env, ios->cis_io->ci_obj,
		   ccc_object_invariant(ios->cis_io->ci_obj));
}

void ccc_io_advance(const struct lu_env *env,
		    const struct cl_io_slice *ios,
		    size_t nob)
{
	struct ccc_io    *cio = cl2ccc_io(env, ios);
	struct cl_io     *io  = ios->cis_io;
	struct cl_object *obj = ios->cis_io->ci_obj;

	CLOBINVRNT(env, obj, ccc_object_invariant(obj));

	if (!cl_is_normalio(env, io))
		return;

	iov_iter_reexpand(cio->cui_iter, cio->cui_tot_count  -= nob);
}

/**
 * Helper function that if necessary adjusts file size (inode->i_size), when
 * position at the offset \a pos is accessed. File size can be arbitrary stale
 * on a Lustre client, but client at least knows KMS. If accessed area is
 * inside [0, KMS], set file size to KMS, otherwise glimpse file size.
 *
 * Locking: cl_isize_lock is used to serialize changes to inode size and to
 * protect consistency between inode size and cl_object
 * attributes. cl_object_size_lock() protects consistency between cl_attr's of
 * top-object and sub-objects.
 */
int ccc_prep_size(const struct lu_env *env, struct cl_object *obj,
		  struct cl_io *io, loff_t start, size_t count, int *exceed)
{
	struct cl_attr *attr  = ccc_env_thread_attr(env);
	struct inode   *inode = ccc_object_inode(obj);
	loff_t	  pos   = start + count - 1;
	loff_t kms;
	int result;

	/*
	 * Consistency guarantees: following possibilities exist for the
	 * relation between region being accessed and real file size at this
	 * moment:
	 *
	 *  (A): the region is completely inside of the file;
	 *
	 *  (B-x): x bytes of region are inside of the file, the rest is
	 *  outside;
	 *
	 *  (C): the region is completely outside of the file.
	 *
	 * This classification is stable under DLM lock already acquired by
	 * the caller, because to change the class, other client has to take
	 * DLM lock conflicting with our lock. Also, any updates to ->i_size
	 * by other threads on this client are serialized by
	 * ll_inode_size_lock(). This guarantees that short reads are handled
	 * correctly in the face of concurrent writes and truncates.
	 */
	ccc_object_size_lock(obj);
	result = cl_object_attr_get(env, obj, attr);
	if (result == 0) {
		kms = attr->cat_kms;
		if (pos > kms) {
			/*
			 * A glimpse is necessary to determine whether we
			 * return a short read (B) or some zeroes at the end
			 * of the buffer (C)
			 */
			ccc_object_size_unlock(obj);
			result = cl_glimpse_lock(env, io, inode, obj, 0);
			if (result == 0 && exceed != NULL) {
				/* If objective page index exceed end-of-file
				 * page index, return directly. Do not expect
				 * kernel will check such case correctly.
				 * linux-2.6.18-128.1.1 miss to do that.
				 * --bug 17336 */
				loff_t size = cl_isize_read(inode);
				loff_t cur_index = start >> PAGE_CACHE_SHIFT;
				loff_t size_index = ((size - 1) >> PAGE_CACHE_SHIFT);

				if ((size == 0 && cur_index != 0) ||
				    size_index < cur_index)
					*exceed = 1;
			}
			return result;
		} else {
			/*
			 * region is within kms and, hence, within real file
			 * size (A). We need to increase i_size to cover the
			 * read region so that generic_file_read() will do its
			 * job, but that doesn't mean the kms size is
			 * _correct_, it is only the _minimum_ size. If
			 * someone does a stat they will get the correct size
			 * which will always be >= the kms value here.
			 * b=11081
			 */
			if (cl_isize_read(inode) < kms) {
				cl_isize_write_nolock(inode, kms);
				CDEBUG(D_VFSTRACE,
				       DFID" updating i_size "LPU64"\n",
				       PFID(lu_object_fid(&obj->co_lu)),
				       (__u64)cl_isize_read(inode));

			}
		}
	}
	ccc_object_size_unlock(obj);
	return result;
}

/*****************************************************************************
 *
 * Transfer operations.
 *
 */

void ccc_req_completion(const struct lu_env *env,
			const struct cl_req_slice *slice, int ioret)
{
	struct ccc_req *vrq;

	if (ioret > 0)
		cl_stats_tally(slice->crs_dev, slice->crs_req->crq_type, ioret);

	vrq = cl2ccc_req(slice);
	OBD_SLAB_FREE_PTR(vrq, ccc_req_kmem);
}

/**
 * Implementation of struct cl_req_operations::cro_attr_set() for ccc
 * layer. ccc is responsible for
 *
 *    - o_[mac]time
 *
 *    - o_mode
 *
 *    - o_parent_seq
 *
 *    - o_[ug]id
 *
 *    - o_parent_oid
 *
 *    - o_parent_ver
 *
 *    - o_ioepoch,
 *
 *  and capability.
 */
void ccc_req_attr_set(const struct lu_env *env,
		      const struct cl_req_slice *slice,
		      const struct cl_object *obj,
		      struct cl_req_attr *attr, obd_valid flags)
{
	struct inode *inode;
	struct obdo  *oa;
	obd_flag      valid_flags;

	oa = attr->cra_oa;
	inode = ccc_object_inode(obj);
	valid_flags = OBD_MD_FLTYPE;

	if ((flags & OBD_MD_FLOSSCAPA) != 0) {
		LASSERT(attr->cra_capa == NULL);
		attr->cra_capa = cl_capa_lookup(inode,
						slice->crs_req->crq_type);
	}

	if (slice->crs_req->crq_type == CRT_WRITE) {
		if (flags & OBD_MD_FLEPOCH) {
			oa->o_valid |= OBD_MD_FLEPOCH;
			oa->o_ioepoch = cl_i2info(inode)->lli_ioepoch;
			valid_flags |= OBD_MD_FLMTIME | OBD_MD_FLCTIME |
				       OBD_MD_FLUID | OBD_MD_FLGID;
		}
	}
	obdo_from_inode(oa, inode, valid_flags & flags);
	obdo_set_parent_fid(oa, &cl_i2info(inode)->lli_fid);
	memcpy(attr->cra_jobid, cl_i2info(inode)->lli_jobid,
	       JOBSTATS_JOBID_SIZE);
}

static const struct cl_req_operations ccc_req_ops = {
	.cro_attr_set   = ccc_req_attr_set,
	.cro_completion = ccc_req_completion
};

int cl_setattr_ost(struct inode *inode, const struct iattr *attr,
		   struct obd_capa *capa)
{
	struct lu_env *env;
	struct cl_io  *io;
	int	    result;
	int	    refcheck;

	env = cl_env_get(&refcheck);
	if (IS_ERR(env))
		return PTR_ERR(env);

	io = ccc_env_thread_io(env);
	io->ci_obj = cl_i2info(inode)->lli_clob;

	io->u.ci_setattr.sa_attr.lvb_atime = LTIME_S(attr->ia_atime);
	io->u.ci_setattr.sa_attr.lvb_mtime = LTIME_S(attr->ia_mtime);
	io->u.ci_setattr.sa_attr.lvb_ctime = LTIME_S(attr->ia_ctime);
	io->u.ci_setattr.sa_attr.lvb_size = attr->ia_size;
	io->u.ci_setattr.sa_valid = attr->ia_valid;
	io->u.ci_setattr.sa_capa = capa;

again:
	if (cl_io_init(env, io, CIT_SETATTR, io->ci_obj) == 0) {
		struct ccc_io *cio = ccc_env_io(env);

		if (attr->ia_valid & ATTR_FILE)
			/* populate the file descriptor for ftruncate to honor
			 * group lock - see LU-787 */
			cio->cui_fd = cl_iattr2fd(inode, attr);

		result = cl_io_loop(env, io);
	} else {
		result = io->ci_result;
	}
	cl_io_fini(env, io);
	if (unlikely(io->ci_need_restart))
		goto again;
	/* HSM import case: file is released, cannot be restored
	 * no need to fail except if restore registration failed
	 * with -ENODATA */
	if (result == -ENODATA && io->ci_restore_needed &&
	    io->ci_result != -ENODATA)
		result = 0;
	cl_env_put(env, &refcheck);
	return result;
}

/*****************************************************************************
 *
 * Type conversions.
 *
 */

struct lu_device *ccc2lu_dev(struct ccc_device *vdv)
{
	return &vdv->cdv_cl.cd_lu_dev;
}

struct ccc_device *lu2ccc_dev(const struct lu_device *d)
{
	return container_of0(d, struct ccc_device, cdv_cl.cd_lu_dev);
}

struct ccc_device *cl2ccc_dev(const struct cl_device *d)
{
	return container_of0(d, struct ccc_device, cdv_cl);
}

struct lu_object *ccc2lu(struct ccc_object *vob)
{
	return &vob->cob_cl.co_lu;
}

struct ccc_object *lu2ccc(const struct lu_object *obj)
{
	return container_of0(obj, struct ccc_object, cob_cl.co_lu);
}

struct ccc_object *cl2ccc(const struct cl_object *obj)
{
	return container_of0(obj, struct ccc_object, cob_cl);
}

struct ccc_lock *cl2ccc_lock(const struct cl_lock_slice *slice)
{
	return container_of(slice, struct ccc_lock, clk_cl);
}

struct ccc_io *cl2ccc_io(const struct lu_env *env,
			 const struct cl_io_slice *slice)
{
	struct ccc_io *cio;

	cio = container_of(slice, struct ccc_io, cui_cl);
	LASSERT(cio == ccc_env_io(env));
	return cio;
}

struct ccc_req *cl2ccc_req(const struct cl_req_slice *slice)
{
	return container_of0(slice, struct ccc_req, crq_cl);
}

struct page *cl2vm_page(const struct cl_page_slice *slice)
{
	return cl2ccc_page(slice)->cpg_page;
}

/*****************************************************************************
 *
 * Accessors.
 *
 */
int ccc_object_invariant(const struct cl_object *obj)
{
	struct inode	 *inode = ccc_object_inode(obj);
	struct cl_inode_info *lli   = cl_i2info(inode);

	return (S_ISREG(cl_inode_mode(inode)) ||
		/* i_mode of unlinked inode is zeroed. */
		cl_inode_mode(inode) == 0) && lli->lli_clob == obj;
}

struct inode *ccc_object_inode(const struct cl_object *obj)
{
	return cl2ccc(obj)->cob_inode;
}

/**
 * Returns a pointer to cl_page associated with \a vmpage, without acquiring
 * additional reference to the resulting page. This is an unsafe version of
 * cl_vmpage_page() that can only be used under vmpage lock.
 */
struct cl_page *ccc_vmpage_page_transient(struct page *vmpage)
{
	KLASSERT(PageLocked(vmpage));
	return (struct cl_page *)vmpage->private;
}

/**
 * Initialize or update CLIO structures for regular files when new
 * meta-data arrives from the server.
 *
 * \param inode regular file inode
 * \param md    new file metadata from MDS
 * - allocates cl_object if necessary,
 * - updated layout, if object was already here.
 */
int cl_file_inode_init(struct inode *inode, struct lustre_md *md)
{
	struct lu_env	*env;
	struct cl_inode_info *lli;
	struct cl_object     *clob;
	struct lu_site       *site;
	struct lu_fid	*fid;
	struct cl_object_conf conf = {
		.coc_inode = inode,
		.u = {
			.coc_md    = md
		}
	};
	int result = 0;
	int refcheck;

	LASSERT(md->body->valid & OBD_MD_FLID);
	LASSERT(S_ISREG(cl_inode_mode(inode)));

	env = cl_env_get(&refcheck);
	if (IS_ERR(env))
		return PTR_ERR(env);

	site = cl_i2sbi(inode)->ll_site;
	lli  = cl_i2info(inode);
	fid  = &lli->lli_fid;
	LASSERT(fid_is_sane(fid));

	if (lli->lli_clob == NULL) {
		/* clob is slave of inode, empty lli_clob means for new inode,
		 * there is no clob in cache with the given fid, so it is
		 * unnecessary to perform lookup-alloc-lookup-insert, just
		 * alloc and insert directly. */
		LASSERT(inode->i_state & I_NEW);
		conf.coc_lu.loc_flags = LOC_F_NEW;
		clob = cl_object_find(env, lu2cl_dev(site->ls_top_dev),
				      fid, &conf);
		if (!IS_ERR(clob)) {
			/*
			 * No locking is necessary, as new inode is
			 * locked by I_NEW bit.
			 */
			lli->lli_clob = clob;
			lli->lli_has_smd = lsm_has_objects(md->lsm);
			lu_object_ref_add(&clob->co_lu, "inode", inode);
		} else
			result = PTR_ERR(clob);
	} else {
		result = cl_conf_set(env, lli->lli_clob, &conf);
	}

	cl_env_put(env, &refcheck);

	if (result != 0)
		CERROR("Failure to initialize cl object "DFID": %d\n",
		       PFID(fid), result);
	return result;
}

/**
 * Wait for others drop their references of the object at first, then we drop
 * the last one, which will lead to the object be destroyed immediately.
 * Must be called after cl_object_kill() against this object.
 *
 * The reason we want to do this is: destroying top object will wait for sub
 * objects being destroyed first, so we can't let bottom layer (e.g. from ASTs)
 * to initiate top object destroying which may deadlock. See bz22520.
 */
static void cl_object_put_last(struct lu_env *env, struct cl_object *obj)
{
	struct lu_object_header *header = obj->co_lu.lo_header;
	wait_queue_t	   waiter;

	if (unlikely(atomic_read(&header->loh_ref) != 1)) {
		struct lu_site *site = obj->co_lu.lo_dev->ld_site;
		struct lu_site_bkt_data *bkt;

		bkt = lu_site_bkt_from_fid(site, &header->loh_fid);

		init_waitqueue_entry(&waiter, current);
		add_wait_queue(&bkt->lsb_marche_funebre, &waiter);

		while (1) {
			set_current_state(TASK_UNINTERRUPTIBLE);
			if (atomic_read(&header->loh_ref) == 1)
				break;
			schedule();
		}

		set_current_state(TASK_RUNNING);
		remove_wait_queue(&bkt->lsb_marche_funebre, &waiter);
	}

	cl_object_put(env, obj);
}

void cl_inode_fini(struct inode *inode)
{
	struct lu_env	   *env;
	struct cl_inode_info    *lli  = cl_i2info(inode);
	struct cl_object	*clob = lli->lli_clob;
	int refcheck;
	int emergency;

	if (clob != NULL) {
		void		    *cookie;

		cookie = cl_env_reenter();
		env = cl_env_get(&refcheck);
		emergency = IS_ERR(env);
		if (emergency) {
			mutex_lock(&ccc_inode_fini_guard);
			LASSERT(ccc_inode_fini_env != NULL);
			cl_env_implant(ccc_inode_fini_env, &refcheck);
			env = ccc_inode_fini_env;
		}
		/*
		 * cl_object cache is a slave to inode cache (which, in turn
		 * is a slave to dentry cache), don't keep cl_object in memory
		 * when its master is evicted.
		 */
		cl_object_kill(env, clob);
		lu_object_ref_del(&clob->co_lu, "inode", inode);
		cl_object_put_last(env, clob);
		lli->lli_clob = NULL;
		if (emergency) {
			cl_env_unplant(ccc_inode_fini_env, &refcheck);
			mutex_unlock(&ccc_inode_fini_guard);
		} else
			cl_env_put(env, &refcheck);
		cl_env_reexit(cookie);
	}
}

/**
 * return IF_* type for given lu_dirent entry.
 * IF_* flag shld be converted to particular OS file type in
 * platform llite module.
 */
__u16 ll_dirent_type_get(struct lu_dirent *ent)
{
	__u16 type = 0;
	struct luda_type *lt;
	int len = 0;

	if (le32_to_cpu(ent->lde_attrs) & LUDA_TYPE) {
		const unsigned align = sizeof(struct luda_type) - 1;

		len = le16_to_cpu(ent->lde_namelen);
		len = (len + align) & ~align;
		lt = (void *)ent->lde_name + len;
		type = IFTODT(le16_to_cpu(lt->lt_type));
	}
	return type;
}

/**
 * build inode number from passed @fid */
__u64 cl_fid_build_ino(const struct lu_fid *fid, int api32)
{
	if (BITS_PER_LONG == 32 || api32)
		return fid_flatten32(fid);
	else
		return fid_flatten(fid);
}

/**
 * build inode generation from passed @fid.  If our FID overflows the 32-bit
 * inode number then return a non-zero generation to distinguish them. */
__u32 cl_fid_build_gen(const struct lu_fid *fid)
{
	__u32 gen;

	if (fid_is_igif(fid)) {
		gen = lu_igif_gen(fid);
		return gen;
	}

	gen = (fid_flatten(fid) >> 32);
	return gen;
}

/* lsm is unreliable after hsm implementation as layout can be changed at
 * any time. This is only to support old, non-clio-ized interfaces. It will
 * cause deadlock if clio operations are called with this extra layout refcount
 * because in case the layout changed during the IO, ll_layout_refresh() will
 * have to wait for the refcount to become zero to destroy the older layout.
 *
 * Notice that the lsm returned by this function may not be valid unless called
 * inside layout lock - MDS_INODELOCK_LAYOUT. */
struct lov_stripe_md *ccc_inode_lsm_get(struct inode *inode)
{
	return lov_lsm_get(cl_i2info(inode)->lli_clob);
}

inline void ccc_inode_lsm_put(struct inode *inode, struct lov_stripe_md *lsm)
{
	lov_lsm_put(cl_i2info(inode)->lli_clob, lsm);
}

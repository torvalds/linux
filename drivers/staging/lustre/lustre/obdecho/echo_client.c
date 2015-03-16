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
 * Copyright (c) 2011, 2012, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 */

#define DEBUG_SUBSYSTEM S_ECHO
#include "../../include/linux/libcfs/libcfs.h"

#include "../include/obd.h"
#include "../include/obd_support.h"
#include "../include/obd_class.h"
#include "../include/lustre_debug.h"
#include "../include/lprocfs_status.h"
#include "../include/cl_object.h"
#include "../include/lustre_fid.h"
#include "../include/lustre_acl.h"
#include "../include/lustre_net.h"

#include "echo_internal.h"

/** \defgroup echo_client Echo Client
 * @{
 */

struct echo_device {
	struct cl_device	ed_cl;
	struct echo_client_obd *ed_ec;

	struct cl_site	  ed_site_myself;
	struct cl_site	 *ed_site;
	struct lu_device       *ed_next;
	int		     ed_next_islov;
};

struct echo_object {
	struct cl_object	eo_cl;
	struct cl_object_header eo_hdr;

	struct echo_device     *eo_dev;
	struct list_head	      eo_obj_chain;
	struct lov_stripe_md   *eo_lsm;
	atomic_t	    eo_npages;
	int		     eo_deleted;
};

struct echo_object_conf {
	struct cl_object_conf  eoc_cl;
	struct lov_stripe_md **eoc_md;
};

struct echo_page {
	struct cl_page_slice   ep_cl;
	struct mutex		ep_lock;
	struct page	    *ep_vmpage;
};

struct echo_lock {
	struct cl_lock_slice   el_cl;
	struct list_head	     el_chain;
	struct echo_object    *el_object;
	__u64		  el_cookie;
	atomic_t	   el_refcount;
};

static int echo_client_setup(const struct lu_env *env,
			     struct obd_device *obddev,
			     struct lustre_cfg *lcfg);
static int echo_client_cleanup(struct obd_device *obddev);


/** \defgroup echo_helpers Helper functions
 * @{
 */
static inline struct echo_device *cl2echo_dev(const struct cl_device *dev)
{
	return container_of0(dev, struct echo_device, ed_cl);
}

static inline struct cl_device *echo_dev2cl(struct echo_device *d)
{
	return &d->ed_cl;
}

static inline struct echo_device *obd2echo_dev(const struct obd_device *obd)
{
	return cl2echo_dev(lu2cl_dev(obd->obd_lu_dev));
}

static inline struct cl_object *echo_obj2cl(struct echo_object *eco)
{
	return &eco->eo_cl;
}

static inline struct echo_object *cl2echo_obj(const struct cl_object *o)
{
	return container_of(o, struct echo_object, eo_cl);
}

static inline struct echo_page *cl2echo_page(const struct cl_page_slice *s)
{
	return container_of(s, struct echo_page, ep_cl);
}

static inline struct echo_lock *cl2echo_lock(const struct cl_lock_slice *s)
{
	return container_of(s, struct echo_lock, el_cl);
}

static inline struct cl_lock *echo_lock2cl(const struct echo_lock *ecl)
{
	return ecl->el_cl.cls_lock;
}

static struct lu_context_key echo_thread_key;
static inline struct echo_thread_info *echo_env_info(const struct lu_env *env)
{
	struct echo_thread_info *info;

	info = lu_context_key_get(&env->le_ctx, &echo_thread_key);
	LASSERT(info != NULL);
	return info;
}

static inline
struct echo_object_conf *cl2echo_conf(const struct cl_object_conf *c)
{
	return container_of(c, struct echo_object_conf, eoc_cl);
}

/** @} echo_helpers */

static struct echo_object *cl_echo_object_find(struct echo_device *d,
					       struct lov_stripe_md **lsm);
static int cl_echo_object_put(struct echo_object *eco);
static int cl_echo_enqueue(struct echo_object *eco, u64 start,
			   u64 end, int mode, __u64 *cookie);
static int cl_echo_cancel(struct echo_device *d, __u64 cookie);
static int cl_echo_object_brw(struct echo_object *eco, int rw, u64 offset,
			      struct page **pages, int npages, int async);

static struct echo_thread_info *echo_env_info(const struct lu_env *env);

struct echo_thread_info {
	struct echo_object_conf eti_conf;
	struct lustre_md	eti_md;

	struct cl_2queue	eti_queue;
	struct cl_io	    eti_io;
	struct cl_lock_descr    eti_descr;
	struct lu_fid	   eti_fid;
	struct lu_fid		eti_fid2;
};

/* No session used right now */
struct echo_session_info {
	unsigned long dummy;
};

static struct kmem_cache *echo_lock_kmem;
static struct kmem_cache *echo_object_kmem;
static struct kmem_cache *echo_thread_kmem;
static struct kmem_cache *echo_session_kmem;

static struct lu_kmem_descr echo_caches[] = {
	{
		.ckd_cache = &echo_lock_kmem,
		.ckd_name  = "echo_lock_kmem",
		.ckd_size  = sizeof(struct echo_lock)
	},
	{
		.ckd_cache = &echo_object_kmem,
		.ckd_name  = "echo_object_kmem",
		.ckd_size  = sizeof(struct echo_object)
	},
	{
		.ckd_cache = &echo_thread_kmem,
		.ckd_name  = "echo_thread_kmem",
		.ckd_size  = sizeof(struct echo_thread_info)
	},
	{
		.ckd_cache = &echo_session_kmem,
		.ckd_name  = "echo_session_kmem",
		.ckd_size  = sizeof(struct echo_session_info)
	},
	{
		.ckd_cache = NULL
	}
};

/** \defgroup echo_page Page operations
 *
 * Echo page operations.
 *
 * @{
 */
static struct page *echo_page_vmpage(const struct lu_env *env,
				    const struct cl_page_slice *slice)
{
	return cl2echo_page(slice)->ep_vmpage;
}

static int echo_page_own(const struct lu_env *env,
			 const struct cl_page_slice *slice,
			 struct cl_io *io, int nonblock)
{
	struct echo_page *ep = cl2echo_page(slice);

	if (!nonblock)
		mutex_lock(&ep->ep_lock);
	else if (!mutex_trylock(&ep->ep_lock))
		return -EAGAIN;
	return 0;
}

static void echo_page_disown(const struct lu_env *env,
			     const struct cl_page_slice *slice,
			     struct cl_io *io)
{
	struct echo_page *ep = cl2echo_page(slice);

	LASSERT(mutex_is_locked(&ep->ep_lock));
	mutex_unlock(&ep->ep_lock);
}

static void echo_page_discard(const struct lu_env *env,
			      const struct cl_page_slice *slice,
			      struct cl_io *unused)
{
	cl_page_delete(env, slice->cpl_page);
}

static int echo_page_is_vmlocked(const struct lu_env *env,
				 const struct cl_page_slice *slice)
{
	if (mutex_is_locked(&cl2echo_page(slice)->ep_lock))
		return -EBUSY;
	return -ENODATA;
}

static void echo_page_completion(const struct lu_env *env,
				 const struct cl_page_slice *slice,
				 int ioret)
{
	LASSERT(slice->cpl_page->cp_sync_io != NULL);
}

static void echo_page_fini(const struct lu_env *env,
			   struct cl_page_slice *slice)
{
	struct echo_page *ep    = cl2echo_page(slice);
	struct echo_object *eco = cl2echo_obj(slice->cpl_obj);
	struct page *vmpage      = ep->ep_vmpage;

	atomic_dec(&eco->eo_npages);
	page_cache_release(vmpage);
}

static int echo_page_prep(const struct lu_env *env,
			  const struct cl_page_slice *slice,
			  struct cl_io *unused)
{
	return 0;
}

static int echo_page_print(const struct lu_env *env,
			   const struct cl_page_slice *slice,
			   void *cookie, lu_printer_t printer)
{
	struct echo_page *ep = cl2echo_page(slice);

	(*printer)(env, cookie, LUSTRE_ECHO_CLIENT_NAME"-page@%p %d vm@%p\n",
		   ep, mutex_is_locked(&ep->ep_lock), ep->ep_vmpage);
	return 0;
}

static const struct cl_page_operations echo_page_ops = {
	.cpo_own	   = echo_page_own,
	.cpo_disown	= echo_page_disown,
	.cpo_discard       = echo_page_discard,
	.cpo_vmpage	= echo_page_vmpage,
	.cpo_fini	  = echo_page_fini,
	.cpo_print	 = echo_page_print,
	.cpo_is_vmlocked   = echo_page_is_vmlocked,
	.io = {
		[CRT_READ] = {
			.cpo_prep	= echo_page_prep,
			.cpo_completion  = echo_page_completion,
		},
		[CRT_WRITE] = {
			.cpo_prep	= echo_page_prep,
			.cpo_completion  = echo_page_completion,
		}
	}
};
/** @} echo_page */

/** \defgroup echo_lock Locking
 *
 * echo lock operations
 *
 * @{
 */
static void echo_lock_fini(const struct lu_env *env,
			   struct cl_lock_slice *slice)
{
	struct echo_lock *ecl = cl2echo_lock(slice);

	LASSERT(list_empty(&ecl->el_chain));
	OBD_SLAB_FREE_PTR(ecl, echo_lock_kmem);
}

static void echo_lock_delete(const struct lu_env *env,
			     const struct cl_lock_slice *slice)
{
	struct echo_lock *ecl      = cl2echo_lock(slice);

	LASSERT(list_empty(&ecl->el_chain));
}

static int echo_lock_fits_into(const struct lu_env *env,
			       const struct cl_lock_slice *slice,
			       const struct cl_lock_descr *need,
			       const struct cl_io *unused)
{
	return 1;
}

static struct cl_lock_operations echo_lock_ops = {
	.clo_fini      = echo_lock_fini,
	.clo_delete    = echo_lock_delete,
	.clo_fits_into = echo_lock_fits_into
};

/** @} echo_lock */

/** \defgroup echo_cl_ops cl_object operations
 *
 * operations for cl_object
 *
 * @{
 */
static int echo_page_init(const struct lu_env *env, struct cl_object *obj,
			struct cl_page *page, struct page *vmpage)
{
	struct echo_page *ep = cl_object_page_slice(obj, page);
	struct echo_object *eco = cl2echo_obj(obj);

	ep->ep_vmpage = vmpage;
	page_cache_get(vmpage);
	mutex_init(&ep->ep_lock);
	cl_page_slice_add(page, &ep->ep_cl, obj, &echo_page_ops);
	atomic_inc(&eco->eo_npages);
	return 0;
}

static int echo_io_init(const struct lu_env *env, struct cl_object *obj,
			struct cl_io *io)
{
	return 0;
}

static int echo_lock_init(const struct lu_env *env,
			  struct cl_object *obj, struct cl_lock *lock,
			  const struct cl_io *unused)
{
	struct echo_lock *el;

	OBD_SLAB_ALLOC_PTR_GFP(el, echo_lock_kmem, GFP_NOFS);
	if (el != NULL) {
		cl_lock_slice_add(lock, &el->el_cl, obj, &echo_lock_ops);
		el->el_object = cl2echo_obj(obj);
		INIT_LIST_HEAD(&el->el_chain);
		atomic_set(&el->el_refcount, 0);
	}
	return el == NULL ? -ENOMEM : 0;
}

static int echo_conf_set(const struct lu_env *env, struct cl_object *obj,
			 const struct cl_object_conf *conf)
{
	return 0;
}

static const struct cl_object_operations echo_cl_obj_ops = {
	.coo_page_init = echo_page_init,
	.coo_lock_init = echo_lock_init,
	.coo_io_init   = echo_io_init,
	.coo_conf_set  = echo_conf_set
};
/** @} echo_cl_ops */

/** \defgroup echo_lu_ops lu_object operations
 *
 * operations for echo lu object.
 *
 * @{
 */
static int echo_object_init(const struct lu_env *env, struct lu_object *obj,
			    const struct lu_object_conf *conf)
{
	struct echo_device *ed	 = cl2echo_dev(lu2cl_dev(obj->lo_dev));
	struct echo_client_obd *ec     = ed->ed_ec;
	struct echo_object *eco	= cl2echo_obj(lu2cl(obj));
	const struct cl_object_conf *cconf;
	struct echo_object_conf *econf;

	if (ed->ed_next) {
		struct lu_object  *below;
		struct lu_device  *under;

		under = ed->ed_next;
		below = under->ld_ops->ldo_object_alloc(env, obj->lo_header,
							under);
		if (below == NULL)
			return -ENOMEM;
		lu_object_add(obj, below);
	}

	cconf = lu2cl_conf(conf);
	econf = cl2echo_conf(cconf);

	LASSERT(econf->eoc_md);
	eco->eo_lsm = *econf->eoc_md;
	/* clear the lsm pointer so that it won't get freed. */
	*econf->eoc_md = NULL;

	eco->eo_dev = ed;
	atomic_set(&eco->eo_npages, 0);
	cl_object_page_init(lu2cl(obj), sizeof(struct echo_page));

	spin_lock(&ec->ec_lock);
	list_add_tail(&eco->eo_obj_chain, &ec->ec_objects);
	spin_unlock(&ec->ec_lock);

	return 0;
}

/* taken from osc_unpackmd() */
static int echo_alloc_memmd(struct echo_device *ed,
			    struct lov_stripe_md **lsmp)
{
	int lsm_size;

	/* If export is lov/osc then use their obd method */
	if (ed->ed_next != NULL)
		return obd_alloc_memmd(ed->ed_ec->ec_exp, lsmp);
	/* OFD has no unpackmd method, do everything here */
	lsm_size = lov_stripe_md_size(1);

	LASSERT(*lsmp == NULL);
	OBD_ALLOC(*lsmp, lsm_size);
	if (*lsmp == NULL)
		return -ENOMEM;

	OBD_ALLOC((*lsmp)->lsm_oinfo[0], sizeof(struct lov_oinfo));
	if ((*lsmp)->lsm_oinfo[0] == NULL) {
		OBD_FREE(*lsmp, lsm_size);
		return -ENOMEM;
	}

	loi_init((*lsmp)->lsm_oinfo[0]);
	(*lsmp)->lsm_maxbytes = LUSTRE_STRIPE_MAXBYTES;
	ostid_set_seq_echo(&(*lsmp)->lsm_oi);

	return lsm_size;
}

static int echo_free_memmd(struct echo_device *ed, struct lov_stripe_md **lsmp)
{
	int lsm_size;

	/* If export is lov/osc then use their obd method */
	if (ed->ed_next != NULL)
		return obd_free_memmd(ed->ed_ec->ec_exp, lsmp);
	/* OFD has no unpackmd method, do everything here */
	lsm_size = lov_stripe_md_size(1);

	LASSERT(*lsmp != NULL);
	OBD_FREE((*lsmp)->lsm_oinfo[0], sizeof(struct lov_oinfo));
	OBD_FREE(*lsmp, lsm_size);
	*lsmp = NULL;
	return 0;
}

static void echo_object_free(const struct lu_env *env, struct lu_object *obj)
{
	struct echo_object *eco    = cl2echo_obj(lu2cl(obj));
	struct echo_client_obd *ec = eco->eo_dev->ed_ec;

	LASSERT(atomic_read(&eco->eo_npages) == 0);

	spin_lock(&ec->ec_lock);
	list_del_init(&eco->eo_obj_chain);
	spin_unlock(&ec->ec_lock);

	lu_object_fini(obj);
	lu_object_header_fini(obj->lo_header);

	if (eco->eo_lsm)
		echo_free_memmd(eco->eo_dev, &eco->eo_lsm);
	OBD_SLAB_FREE_PTR(eco, echo_object_kmem);
}

static int echo_object_print(const struct lu_env *env, void *cookie,
			    lu_printer_t p, const struct lu_object *o)
{
	struct echo_object *obj = cl2echo_obj(lu2cl(o));

	return (*p)(env, cookie, "echoclient-object@%p", obj);
}

static const struct lu_object_operations echo_lu_obj_ops = {
	.loo_object_init      = echo_object_init,
	.loo_object_delete    = NULL,
	.loo_object_release   = NULL,
	.loo_object_free      = echo_object_free,
	.loo_object_print     = echo_object_print,
	.loo_object_invariant = NULL
};
/** @} echo_lu_ops */

/** \defgroup echo_lu_dev_ops  lu_device operations
 *
 * Operations for echo lu device.
 *
 * @{
 */
static struct lu_object *echo_object_alloc(const struct lu_env *env,
					   const struct lu_object_header *hdr,
					   struct lu_device *dev)
{
	struct echo_object *eco;
	struct lu_object *obj = NULL;

	/* we're the top dev. */
	LASSERT(hdr == NULL);
	OBD_SLAB_ALLOC_PTR_GFP(eco, echo_object_kmem, GFP_NOFS);
	if (eco != NULL) {
		struct cl_object_header *hdr = &eco->eo_hdr;

		obj = &echo_obj2cl(eco)->co_lu;
		cl_object_header_init(hdr);
		lu_object_init(obj, &hdr->coh_lu, dev);
		lu_object_add_top(&hdr->coh_lu, obj);

		eco->eo_cl.co_ops = &echo_cl_obj_ops;
		obj->lo_ops       = &echo_lu_obj_ops;
	}
	return obj;
}

static struct lu_device_operations echo_device_lu_ops = {
	.ldo_object_alloc   = echo_object_alloc,
};

/** @} echo_lu_dev_ops */

static struct cl_device_operations echo_device_cl_ops = {
};

/** \defgroup echo_init Setup and teardown
 *
 * Init and fini functions for echo client.
 *
 * @{
 */
static int echo_site_init(const struct lu_env *env, struct echo_device *ed)
{
	struct cl_site *site = &ed->ed_site_myself;
	int rc;

	/* initialize site */
	rc = cl_site_init(site, &ed->ed_cl);
	if (rc) {
		CERROR("Cannot initialize site for echo client(%d)\n", rc);
		return rc;
	}

	rc = lu_site_init_finish(&site->cs_lu);
	if (rc)
		return rc;

	ed->ed_site = site;
	return 0;
}

static void echo_site_fini(const struct lu_env *env, struct echo_device *ed)
{
	if (ed->ed_site) {
		cl_site_fini(ed->ed_site);
		ed->ed_site = NULL;
	}
}

static void *echo_thread_key_init(const struct lu_context *ctx,
			  struct lu_context_key *key)
{
	struct echo_thread_info *info;

	OBD_SLAB_ALLOC_PTR_GFP(info, echo_thread_kmem, GFP_NOFS);
	if (info == NULL)
		info = ERR_PTR(-ENOMEM);
	return info;
}

static void echo_thread_key_fini(const struct lu_context *ctx,
			 struct lu_context_key *key, void *data)
{
	struct echo_thread_info *info = data;

	OBD_SLAB_FREE_PTR(info, echo_thread_kmem);
}

static void echo_thread_key_exit(const struct lu_context *ctx,
			 struct lu_context_key *key, void *data)
{
}

static struct lu_context_key echo_thread_key = {
	.lct_tags = LCT_CL_THREAD,
	.lct_init = echo_thread_key_init,
	.lct_fini = echo_thread_key_fini,
	.lct_exit = echo_thread_key_exit
};

static void *echo_session_key_init(const struct lu_context *ctx,
				  struct lu_context_key *key)
{
	struct echo_session_info *session;

	OBD_SLAB_ALLOC_PTR_GFP(session, echo_session_kmem, GFP_NOFS);
	if (session == NULL)
		session = ERR_PTR(-ENOMEM);
	return session;
}

static void echo_session_key_fini(const struct lu_context *ctx,
				 struct lu_context_key *key, void *data)
{
	struct echo_session_info *session = data;

	OBD_SLAB_FREE_PTR(session, echo_session_kmem);
}

static void echo_session_key_exit(const struct lu_context *ctx,
				 struct lu_context_key *key, void *data)
{
}

static struct lu_context_key echo_session_key = {
	.lct_tags = LCT_SESSION,
	.lct_init = echo_session_key_init,
	.lct_fini = echo_session_key_fini,
	.lct_exit = echo_session_key_exit
};

LU_TYPE_INIT_FINI(echo, &echo_thread_key, &echo_session_key);

static struct lu_device *echo_device_alloc(const struct lu_env *env,
					   struct lu_device_type *t,
					   struct lustre_cfg *cfg)
{
	struct lu_device   *next;
	struct echo_device *ed;
	struct cl_device   *cd;
	struct obd_device  *obd = NULL; /* to keep compiler happy */
	struct obd_device  *tgt;
	const char *tgt_type_name;
	int rc;
	int cleanup = 0;

	OBD_ALLOC_PTR(ed);
	if (ed == NULL) {
		rc = -ENOMEM;
		goto out;
	}

	cleanup = 1;
	cd = &ed->ed_cl;
	rc = cl_device_init(cd, t);
	if (rc)
		goto out;

	cd->cd_lu_dev.ld_ops = &echo_device_lu_ops;
	cd->cd_ops = &echo_device_cl_ops;

	cleanup = 2;
	obd = class_name2obd(lustre_cfg_string(cfg, 0));
	LASSERT(obd != NULL);
	LASSERT(env != NULL);

	tgt = class_name2obd(lustre_cfg_string(cfg, 1));
	if (tgt == NULL) {
		CERROR("Can not find tgt device %s\n",
			lustre_cfg_string(cfg, 1));
		rc = -ENODEV;
		goto out;
	}

	next = tgt->obd_lu_dev;
	if (!strcmp(tgt->obd_type->typ_name, LUSTRE_MDT_NAME)) {
		CERROR("echo MDT client must be run on server\n");
		rc = -EOPNOTSUPP;
		goto out;
	}

	rc = echo_site_init(env, ed);
	if (rc)
		goto out;

	cleanup = 3;

	rc = echo_client_setup(env, obd, cfg);
	if (rc)
		goto out;

	ed->ed_ec = &obd->u.echo_client;
	cleanup = 4;

	/* if echo client is to be stacked upon ost device, the next is
	 * NULL since ost is not a clio device so far */
	if (next != NULL && !lu_device_is_cl(next))
		next = NULL;

	tgt_type_name = tgt->obd_type->typ_name;
	if (next != NULL) {
		LASSERT(next != NULL);
		if (next->ld_site != NULL) {
			rc = -EBUSY;
			goto out;
		}

		next->ld_site = &ed->ed_site->cs_lu;
		rc = next->ld_type->ldt_ops->ldto_device_init(env, next,
						next->ld_type->ldt_name,
							      NULL);
		if (rc)
			goto out;

		/* Tricky case, I have to determine the obd type since
		 * CLIO uses the different parameters to initialize
		 * objects for lov & osc. */
		if (strcmp(tgt_type_name, LUSTRE_LOV_NAME) == 0)
			ed->ed_next_islov = 1;
		else
			LASSERT(strcmp(tgt_type_name,
				       LUSTRE_OSC_NAME) == 0);
	} else {
		LASSERT(strcmp(tgt_type_name, LUSTRE_OST_NAME) == 0);
	}

	ed->ed_next = next;
	return &cd->cd_lu_dev;
out:
	switch (cleanup) {
	case 4: {
		int rc2;

		rc2 = echo_client_cleanup(obd);
		if (rc2)
			CERROR("Cleanup obd device %s error(%d)\n",
			       obd->obd_name, rc2);
	}

	case 3:
		echo_site_fini(env, ed);
	case 2:
		cl_device_fini(&ed->ed_cl);
	case 1:
		OBD_FREE_PTR(ed);
	case 0:
	default:
		break;
	}
	return ERR_PTR(rc);
}

static int echo_device_init(const struct lu_env *env, struct lu_device *d,
			  const char *name, struct lu_device *next)
{
	LBUG();
	return 0;
}

static struct lu_device *echo_device_fini(const struct lu_env *env,
					  struct lu_device *d)
{
	struct echo_device *ed = cl2echo_dev(lu2cl_dev(d));
	struct lu_device *next = ed->ed_next;

	while (next)
		next = next->ld_type->ldt_ops->ldto_device_fini(env, next);
	return NULL;
}

static void echo_lock_release(const struct lu_env *env,
			      struct echo_lock *ecl,
			      int still_used)
{
	struct cl_lock *clk = echo_lock2cl(ecl);

	cl_lock_get(clk);
	cl_unuse(env, clk);
	cl_lock_release(env, clk, "ec enqueue", ecl->el_object);
	if (!still_used) {
		cl_lock_mutex_get(env, clk);
		cl_lock_cancel(env, clk);
		cl_lock_delete(env, clk);
		cl_lock_mutex_put(env, clk);
	}
	cl_lock_put(env, clk);
}

static struct lu_device *echo_device_free(const struct lu_env *env,
					  struct lu_device *d)
{
	struct echo_device     *ed   = cl2echo_dev(lu2cl_dev(d));
	struct echo_client_obd *ec   = ed->ed_ec;
	struct echo_object     *eco;
	struct lu_device       *next = ed->ed_next;

	CDEBUG(D_INFO, "echo device:%p is going to be freed, next = %p\n",
	       ed, next);

	lu_site_purge(env, &ed->ed_site->cs_lu, -1);

	/* check if there are objects still alive.
	 * It shouldn't have any object because lu_site_purge would cleanup
	 * all of cached objects. Anyway, probably the echo device is being
	 * parallelly accessed.
	 */
	spin_lock(&ec->ec_lock);
	list_for_each_entry(eco, &ec->ec_objects, eo_obj_chain)
		eco->eo_deleted = 1;
	spin_unlock(&ec->ec_lock);

	/* purge again */
	lu_site_purge(env, &ed->ed_site->cs_lu, -1);

	CDEBUG(D_INFO,
	       "Waiting for the reference of echo object to be dropped\n");

	/* Wait for the last reference to be dropped. */
	spin_lock(&ec->ec_lock);
	while (!list_empty(&ec->ec_objects)) {
		spin_unlock(&ec->ec_lock);
		CERROR("echo_client still has objects at cleanup time, wait for 1 second\n");
		set_current_state(TASK_UNINTERRUPTIBLE);
		schedule_timeout(cfs_time_seconds(1));
		lu_site_purge(env, &ed->ed_site->cs_lu, -1);
		spin_lock(&ec->ec_lock);
	}
	spin_unlock(&ec->ec_lock);

	LASSERT(list_empty(&ec->ec_locks));

	CDEBUG(D_INFO, "No object exists, exiting...\n");

	echo_client_cleanup(d->ld_obd);

	while (next)
		next = next->ld_type->ldt_ops->ldto_device_free(env, next);

	LASSERT(ed->ed_site == lu2cl_site(d->ld_site));
	echo_site_fini(env, ed);
	cl_device_fini(&ed->ed_cl);
	OBD_FREE_PTR(ed);

	return NULL;
}

static const struct lu_device_type_operations echo_device_type_ops = {
	.ldto_init = echo_type_init,
	.ldto_fini = echo_type_fini,

	.ldto_start = echo_type_start,
	.ldto_stop  = echo_type_stop,

	.ldto_device_alloc = echo_device_alloc,
	.ldto_device_free  = echo_device_free,
	.ldto_device_init  = echo_device_init,
	.ldto_device_fini  = echo_device_fini
};

static struct lu_device_type echo_device_type = {
	.ldt_tags     = LU_DEVICE_CL,
	.ldt_name     = LUSTRE_ECHO_CLIENT_NAME,
	.ldt_ops      = &echo_device_type_ops,
	.ldt_ctx_tags = LCT_CL_THREAD,
};
/** @} echo_init */

/** \defgroup echo_exports Exported operations
 *
 * exporting functions to echo client
 *
 * @{
 */

/* Interfaces to echo client obd device */
static struct echo_object *cl_echo_object_find(struct echo_device *d,
					       struct lov_stripe_md **lsmp)
{
	struct lu_env *env;
	struct echo_thread_info *info;
	struct echo_object_conf *conf;
	struct lov_stripe_md    *lsm;
	struct echo_object *eco;
	struct cl_object   *obj;
	struct lu_fid *fid;
	int refcheck;
	int rc;

	LASSERT(lsmp);
	lsm = *lsmp;
	LASSERT(lsm);
	LASSERTF(ostid_id(&lsm->lsm_oi) != 0, DOSTID"\n", POSTID(&lsm->lsm_oi));
	LASSERTF(ostid_seq(&lsm->lsm_oi) == FID_SEQ_ECHO, DOSTID"\n",
		 POSTID(&lsm->lsm_oi));

	/* Never return an object if the obd is to be freed. */
	if (echo_dev2cl(d)->cd_lu_dev.ld_obd->obd_stopping)
		return ERR_PTR(-ENODEV);

	env = cl_env_get(&refcheck);
	if (IS_ERR(env))
		return (void *)env;

	info = echo_env_info(env);
	conf = &info->eti_conf;
	if (d->ed_next) {
		if (!d->ed_next_islov) {
			struct lov_oinfo *oinfo = lsm->lsm_oinfo[0];

			LASSERT(oinfo != NULL);
			oinfo->loi_oi = lsm->lsm_oi;
			conf->eoc_cl.u.coc_oinfo = oinfo;
		} else {
			struct lustre_md *md;

			md = &info->eti_md;
			memset(md, 0, sizeof(*md));
			md->lsm = lsm;
			conf->eoc_cl.u.coc_md = md;
		}
	}
	conf->eoc_md = lsmp;

	fid  = &info->eti_fid;
	rc = ostid_to_fid(fid, &lsm->lsm_oi, 0);
	if (rc != 0) {
		eco = ERR_PTR(rc);
		goto out;
	}

	/* In the function below, .hs_keycmp resolves to
	 * lu_obj_hop_keycmp() */
	/* coverity[overrun-buffer-val] */
	obj = cl_object_find(env, echo_dev2cl(d), fid, &conf->eoc_cl);
	if (IS_ERR(obj)) {
		eco = (void *)obj;
		goto out;
	}

	eco = cl2echo_obj(obj);
	if (eco->eo_deleted) {
		cl_object_put(env, obj);
		eco = ERR_PTR(-EAGAIN);
	}

out:
	cl_env_put(env, &refcheck);
	return eco;
}

static int cl_echo_object_put(struct echo_object *eco)
{
	struct lu_env *env;
	struct cl_object *obj = echo_obj2cl(eco);
	int refcheck;

	env = cl_env_get(&refcheck);
	if (IS_ERR(env))
		return PTR_ERR(env);

	/* an external function to kill an object? */
	if (eco->eo_deleted) {
		struct lu_object_header *loh = obj->co_lu.lo_header;

		LASSERT(&eco->eo_hdr == luh2coh(loh));
		set_bit(LU_OBJECT_HEARD_BANSHEE, &loh->loh_flags);
	}

	cl_object_put(env, obj);
	cl_env_put(env, &refcheck);
	return 0;
}

static int cl_echo_enqueue0(struct lu_env *env, struct echo_object *eco,
			    u64 start, u64 end, int mode,
			    __u64 *cookie, __u32 enqflags)
{
	struct cl_io *io;
	struct cl_lock *lck;
	struct cl_object *obj;
	struct cl_lock_descr *descr;
	struct echo_thread_info *info;
	int rc = -ENOMEM;

	info = echo_env_info(env);
	io = &info->eti_io;
	descr = &info->eti_descr;
	obj = echo_obj2cl(eco);

	descr->cld_obj   = obj;
	descr->cld_start = cl_index(obj, start);
	descr->cld_end   = cl_index(obj, end);
	descr->cld_mode  = mode == LCK_PW ? CLM_WRITE : CLM_READ;
	descr->cld_enq_flags = enqflags;
	io->ci_obj = obj;

	lck = cl_lock_request(env, io, descr, "ec enqueue", eco);
	if (lck) {
		struct echo_client_obd *ec = eco->eo_dev->ed_ec;
		struct echo_lock *el;

		rc = cl_wait(env, lck);
		if (rc == 0) {
			el = cl2echo_lock(cl_lock_at(lck, &echo_device_type));
			spin_lock(&ec->ec_lock);
			if (list_empty(&el->el_chain)) {
				list_add(&el->el_chain, &ec->ec_locks);
				el->el_cookie = ++ec->ec_unique;
			}
			atomic_inc(&el->el_refcount);
			*cookie = el->el_cookie;
			spin_unlock(&ec->ec_lock);
		} else {
			cl_lock_release(env, lck, "ec enqueue", current);
		}
	}
	return rc;
}

static int cl_echo_enqueue(struct echo_object *eco, u64 start, u64 end,
			   int mode, __u64 *cookie)
{
	struct echo_thread_info *info;
	struct lu_env *env;
	struct cl_io *io;
	int refcheck;
	int result;

	env = cl_env_get(&refcheck);
	if (IS_ERR(env))
		return PTR_ERR(env);

	info = echo_env_info(env);
	io = &info->eti_io;

	io->ci_ignore_layout = 1;
	result = cl_io_init(env, io, CIT_MISC, echo_obj2cl(eco));
	if (result < 0)
		goto out;
	LASSERT(result == 0);

	result = cl_echo_enqueue0(env, eco, start, end, mode, cookie, 0);
	cl_io_fini(env, io);

out:
	cl_env_put(env, &refcheck);
	return result;
}

static int cl_echo_cancel0(struct lu_env *env, struct echo_device *ed,
			   __u64 cookie)
{
	struct echo_client_obd *ec = ed->ed_ec;
	struct echo_lock       *ecl = NULL;
	struct list_head	     *el;
	int found = 0, still_used = 0;

	LASSERT(ec != NULL);
	spin_lock(&ec->ec_lock);
	list_for_each(el, &ec->ec_locks) {
		ecl = list_entry(el, struct echo_lock, el_chain);
		CDEBUG(D_INFO, "ecl: %p, cookie: %#llx\n", ecl, ecl->el_cookie);
		found = (ecl->el_cookie == cookie);
		if (found) {
			if (atomic_dec_and_test(&ecl->el_refcount))
				list_del_init(&ecl->el_chain);
			else
				still_used = 1;
			break;
		}
	}
	spin_unlock(&ec->ec_lock);

	if (!found)
		return -ENOENT;

	echo_lock_release(env, ecl, still_used);
	return 0;
}

static int cl_echo_cancel(struct echo_device *ed, __u64 cookie)
{
	struct lu_env *env;
	int refcheck;
	int rc;

	env = cl_env_get(&refcheck);
	if (IS_ERR(env))
		return PTR_ERR(env);

	rc = cl_echo_cancel0(env, ed, cookie);

	cl_env_put(env, &refcheck);
	return rc;
}

static int cl_echo_async_brw(const struct lu_env *env, struct cl_io *io,
			     enum cl_req_type unused, struct cl_2queue *queue)
{
	struct cl_page *clp;
	struct cl_page *temp;
	int result = 0;

	cl_page_list_for_each_safe(clp, temp, &queue->c2_qin) {
		int rc;

		rc = cl_page_cache_add(env, io, clp, CRT_WRITE);
		if (rc == 0)
			continue;
		result = result ?: rc;
	}
	return result;
}

static int cl_echo_object_brw(struct echo_object *eco, int rw, u64 offset,
			      struct page **pages, int npages, int async)
{
	struct lu_env	   *env;
	struct echo_thread_info *info;
	struct cl_object	*obj = echo_obj2cl(eco);
	struct echo_device      *ed  = eco->eo_dev;
	struct cl_2queue	*queue;
	struct cl_io	    *io;
	struct cl_page	  *clp;
	struct lustre_handle    lh = { 0 };
	int page_size = cl_page_size(obj);
	int refcheck;
	int rc;
	int i;

	LASSERT((offset & ~CFS_PAGE_MASK) == 0);
	LASSERT(ed->ed_next != NULL);
	env = cl_env_get(&refcheck);
	if (IS_ERR(env))
		return PTR_ERR(env);

	info    = echo_env_info(env);
	io      = &info->eti_io;
	queue   = &info->eti_queue;

	cl_2queue_init(queue);

	io->ci_ignore_layout = 1;
	rc = cl_io_init(env, io, CIT_MISC, obj);
	if (rc < 0)
		goto out;
	LASSERT(rc == 0);


	rc = cl_echo_enqueue0(env, eco, offset,
			      offset + npages * PAGE_CACHE_SIZE - 1,
			      rw == READ ? LCK_PR : LCK_PW, &lh.cookie,
			      CEF_NEVER);
	if (rc < 0)
		goto error_lock;

	for (i = 0; i < npages; i++) {
		LASSERT(pages[i]);
		clp = cl_page_find(env, obj, cl_index(obj, offset),
				   pages[i], CPT_TRANSIENT);
		if (IS_ERR(clp)) {
			rc = PTR_ERR(clp);
			break;
		}
		LASSERT(clp->cp_type == CPT_TRANSIENT);

		rc = cl_page_own(env, io, clp);
		if (rc) {
			LASSERT(clp->cp_state == CPS_FREEING);
			cl_page_put(env, clp);
			break;
		}

		cl_2queue_add(queue, clp);

		/* drop the reference count for cl_page_find, so that the page
		 * will be freed in cl_2queue_fini. */
		cl_page_put(env, clp);
		cl_page_clip(env, clp, 0, page_size);

		offset += page_size;
	}

	if (rc == 0) {
		enum cl_req_type typ = rw == READ ? CRT_READ : CRT_WRITE;

		async = async && (typ == CRT_WRITE);
		if (async)
			rc = cl_echo_async_brw(env, io, typ, queue);
		else
			rc = cl_io_submit_sync(env, io, typ, queue, 0);
		CDEBUG(D_INFO, "echo_client %s write returns %d\n",
		       async ? "async" : "sync", rc);
	}

	cl_echo_cancel0(env, ed, lh.cookie);
error_lock:
	cl_2queue_discard(env, io, queue);
	cl_2queue_disown(env, io, queue);
	cl_2queue_fini(env, queue);
	cl_io_fini(env, io);
out:
	cl_env_put(env, &refcheck);
	return rc;
}
/** @} echo_exports */


static u64 last_object_id;

static int
echo_copyout_lsm(struct lov_stripe_md *lsm, void *_ulsm, int ulsm_nob)
{
	struct lov_stripe_md *ulsm = _ulsm;
	int nob, i;

	nob = offsetof(struct lov_stripe_md, lsm_oinfo[lsm->lsm_stripe_count]);
	if (nob > ulsm_nob)
		return -EINVAL;

	if (copy_to_user(ulsm, lsm, sizeof(*ulsm)))
		return -EFAULT;

	for (i = 0; i < lsm->lsm_stripe_count; i++) {
		if (copy_to_user(ulsm->lsm_oinfo[i], lsm->lsm_oinfo[i],
				      sizeof(lsm->lsm_oinfo[0])))
			return -EFAULT;
	}
	return 0;
}

static int
echo_copyin_lsm(struct echo_device *ed, struct lov_stripe_md *lsm,
		 void *ulsm, int ulsm_nob)
{
	struct echo_client_obd *ec = ed->ed_ec;
	int		     i;

	if (ulsm_nob < sizeof(*lsm))
		return -EINVAL;

	if (copy_from_user(lsm, ulsm, sizeof(*lsm)))
		return -EFAULT;

	if (lsm->lsm_stripe_count > ec->ec_nstripes ||
	    lsm->lsm_magic != LOV_MAGIC ||
	    (lsm->lsm_stripe_size & (~CFS_PAGE_MASK)) != 0 ||
	    ((__u64)lsm->lsm_stripe_size * lsm->lsm_stripe_count > ~0UL))
		return -EINVAL;


	for (i = 0; i < lsm->lsm_stripe_count; i++) {
		if (copy_from_user(lsm->lsm_oinfo[i],
				       ((struct lov_stripe_md *)ulsm)-> \
				       lsm_oinfo[i],
				       sizeof(lsm->lsm_oinfo[0])))
			return -EFAULT;
	}
	return 0;
}

static int echo_create_object(const struct lu_env *env, struct echo_device *ed,
			      int on_target, struct obdo *oa, void *ulsm,
			      int ulsm_nob, struct obd_trans_info *oti)
{
	struct echo_object     *eco;
	struct echo_client_obd *ec = ed->ed_ec;
	struct lov_stripe_md   *lsm = NULL;
	int		     rc;
	int		     created = 0;

	if ((oa->o_valid & OBD_MD_FLID) == 0 && /* no obj id */
	    (on_target ||		       /* set_stripe */
	     ec->ec_nstripes != 0)) {	   /* LOV */
		CERROR("No valid oid\n");
		return -EINVAL;
	}

	rc = echo_alloc_memmd(ed, &lsm);
	if (rc < 0) {
		CERROR("Cannot allocate md: rc = %d\n", rc);
		goto failed;
	}

	if (ulsm != NULL) {
		int i, idx;

		rc = echo_copyin_lsm(ed, lsm, ulsm, ulsm_nob);
		if (rc != 0)
			goto failed;

		if (lsm->lsm_stripe_count == 0)
			lsm->lsm_stripe_count = ec->ec_nstripes;

		if (lsm->lsm_stripe_size == 0)
			lsm->lsm_stripe_size = PAGE_CACHE_SIZE;

		idx = cfs_rand();

		/* setup stripes: indices + default ids if required */
		for (i = 0; i < lsm->lsm_stripe_count; i++) {
			if (ostid_id(&lsm->lsm_oinfo[i]->loi_oi) == 0)
				lsm->lsm_oinfo[i]->loi_oi = lsm->lsm_oi;

			lsm->lsm_oinfo[i]->loi_ost_idx =
				(idx + i) % ec->ec_nstripes;
		}
	}

	/* setup object ID here for !on_target and LOV hint */
	if (oa->o_valid & OBD_MD_FLID) {
		LASSERT(oa->o_valid & OBD_MD_FLGROUP);
		lsm->lsm_oi = oa->o_oi;
	}

	if (ostid_id(&lsm->lsm_oi) == 0)
		ostid_set_id(&lsm->lsm_oi, ++last_object_id);

	rc = 0;
	if (on_target) {
		/* Only echo objects are allowed to be created */
		LASSERT((oa->o_valid & OBD_MD_FLGROUP) &&
			(ostid_seq(&oa->o_oi) == FID_SEQ_ECHO));
		rc = obd_create(env, ec->ec_exp, oa, &lsm, oti);
		if (rc != 0) {
			CERROR("Cannot create objects: rc = %d\n", rc);
			goto failed;
		}
		created = 1;
	}

	/* See what object ID we were given */
	oa->o_oi = lsm->lsm_oi;
	oa->o_valid |= OBD_MD_FLID;

	eco = cl_echo_object_find(ed, &lsm);
	if (IS_ERR(eco)) {
		rc = PTR_ERR(eco);
		goto failed;
	}
	cl_echo_object_put(eco);

	CDEBUG(D_INFO, "oa oid "DOSTID"\n", POSTID(&oa->o_oi));

 failed:
	if (created && rc)
		obd_destroy(env, ec->ec_exp, oa, lsm, oti, NULL, NULL);
	if (lsm)
		echo_free_memmd(ed, &lsm);
	if (rc)
		CERROR("create object failed with: rc = %d\n", rc);
	return rc;
}

static int echo_get_object(struct echo_object **ecop, struct echo_device *ed,
			   struct obdo *oa)
{
	struct lov_stripe_md   *lsm = NULL;
	struct echo_object     *eco;
	int		     rc;

	if ((oa->o_valid & OBD_MD_FLID) == 0 || ostid_id(&oa->o_oi) == 0) {
		/* disallow use of object id 0 */
		CERROR("No valid oid\n");
		return -EINVAL;
	}

	rc = echo_alloc_memmd(ed, &lsm);
	if (rc < 0)
		return rc;

	lsm->lsm_oi = oa->o_oi;
	if (!(oa->o_valid & OBD_MD_FLGROUP))
		ostid_set_seq_echo(&lsm->lsm_oi);

	rc = 0;
	eco = cl_echo_object_find(ed, &lsm);
	if (!IS_ERR(eco))
		*ecop = eco;
	else
		rc = PTR_ERR(eco);
	if (lsm)
		echo_free_memmd(ed, &lsm);
	return rc;
}

static void echo_put_object(struct echo_object *eco)
{
	if (cl_echo_object_put(eco))
		CERROR("echo client: drop an object failed");
}

static void
echo_get_stripe_off_id(struct lov_stripe_md *lsm, u64 *offp, u64 *idp)
{
	unsigned long stripe_count;
	unsigned long stripe_size;
	unsigned long width;
	unsigned long woffset;
	int	   stripe_index;
	u64       offset;

	if (lsm->lsm_stripe_count <= 1)
		return;

	offset       = *offp;
	stripe_size  = lsm->lsm_stripe_size;
	stripe_count = lsm->lsm_stripe_count;

	/* width = # bytes in all stripes */
	width = stripe_size * stripe_count;

	/* woffset = offset within a width; offset = whole number of widths */
	woffset = do_div(offset, width);

	stripe_index = woffset / stripe_size;

	*idp = ostid_id(&lsm->lsm_oinfo[stripe_index]->loi_oi);
	*offp = offset * stripe_size + woffset % stripe_size;
}

static void
echo_client_page_debug_setup(struct lov_stripe_md *lsm,
			     struct page *page, int rw, u64 id,
			     u64 offset, u64 count)
{
	char    *addr;
	u64	 stripe_off;
	u64	 stripe_id;
	int      delta;

	/* no partial pages on the client */
	LASSERT(count == PAGE_CACHE_SIZE);

	addr = kmap(page);

	for (delta = 0; delta < PAGE_CACHE_SIZE; delta += OBD_ECHO_BLOCK_SIZE) {
		if (rw == OBD_BRW_WRITE) {
			stripe_off = offset + delta;
			stripe_id = id;
			echo_get_stripe_off_id(lsm, &stripe_off, &stripe_id);
		} else {
			stripe_off = 0xdeadbeef00c0ffeeULL;
			stripe_id = 0xdeadbeef00c0ffeeULL;
		}
		block_debug_setup(addr + delta, OBD_ECHO_BLOCK_SIZE,
				  stripe_off, stripe_id);
	}

	kunmap(page);
}

static int echo_client_page_debug_check(struct lov_stripe_md *lsm,
					struct page *page, u64 id,
					u64 offset, u64 count)
{
	u64	stripe_off;
	u64	stripe_id;
	char   *addr;
	int     delta;
	int     rc;
	int     rc2;

	/* no partial pages on the client */
	LASSERT(count == PAGE_CACHE_SIZE);

	addr = kmap(page);

	for (rc = delta = 0; delta < PAGE_CACHE_SIZE; delta += OBD_ECHO_BLOCK_SIZE) {
		stripe_off = offset + delta;
		stripe_id = id;
		echo_get_stripe_off_id(lsm, &stripe_off, &stripe_id);

		rc2 = block_debug_check("test_brw",
					addr + delta, OBD_ECHO_BLOCK_SIZE,
					stripe_off, stripe_id);
		if (rc2 != 0) {
			CERROR("Error in echo object %#llx\n", id);
			rc = rc2;
		}
	}

	kunmap(page);
	return rc;
}

static int echo_client_kbrw(struct echo_device *ed, int rw, struct obdo *oa,
			    struct echo_object *eco, u64 offset,
			    u64 count, int async,
			    struct obd_trans_info *oti)
{
	struct lov_stripe_md   *lsm = eco->eo_lsm;
	u32	       npages;
	struct brw_page	*pga;
	struct brw_page	*pgp;
	struct page	    **pages;
	u64		 off;
	int		     i;
	int		     rc;
	int		     verify;
	gfp_t		     gfp_mask;
	int		     brw_flags = 0;

	verify = (ostid_id(&oa->o_oi) != ECHO_PERSISTENT_OBJID &&
		  (oa->o_valid & OBD_MD_FLFLAGS) != 0 &&
		  (oa->o_flags & OBD_FL_DEBUG_CHECK) != 0);

	gfp_mask = ((ostid_id(&oa->o_oi) & 2) == 0) ? GFP_IOFS : GFP_HIGHUSER;

	LASSERT(rw == OBD_BRW_WRITE || rw == OBD_BRW_READ);
	LASSERT(lsm != NULL);
	LASSERT(ostid_id(&lsm->lsm_oi) == ostid_id(&oa->o_oi));

	if (count <= 0 ||
	    (count & (~CFS_PAGE_MASK)) != 0)
		return -EINVAL;

	/* XXX think again with misaligned I/O */
	npages = count >> PAGE_CACHE_SHIFT;

	if (rw == OBD_BRW_WRITE)
		brw_flags = OBD_BRW_ASYNC;

	OBD_ALLOC(pga, npages * sizeof(*pga));
	if (pga == NULL)
		return -ENOMEM;

	OBD_ALLOC(pages, npages * sizeof(*pages));
	if (pages == NULL) {
		OBD_FREE(pga, npages * sizeof(*pga));
		return -ENOMEM;
	}

	for (i = 0, pgp = pga, off = offset;
	     i < npages;
	     i++, pgp++, off += PAGE_CACHE_SIZE) {

		LASSERT(pgp->pg == NULL);      /* for cleanup */

		rc = -ENOMEM;
		OBD_PAGE_ALLOC(pgp->pg, gfp_mask);
		if (pgp->pg == NULL)
			goto out;

		pages[i] = pgp->pg;
		pgp->count = PAGE_CACHE_SIZE;
		pgp->off = off;
		pgp->flag = brw_flags;

		if (verify)
			echo_client_page_debug_setup(lsm, pgp->pg, rw,
						     ostid_id(&oa->o_oi), off,
						     pgp->count);
	}

	/* brw mode can only be used at client */
	LASSERT(ed->ed_next != NULL);
	rc = cl_echo_object_brw(eco, rw, offset, pages, npages, async);

 out:
	if (rc != 0 || rw != OBD_BRW_READ)
		verify = 0;

	for (i = 0, pgp = pga; i < npages; i++, pgp++) {
		if (pgp->pg == NULL)
			continue;

		if (verify) {
			int vrc;

			vrc = echo_client_page_debug_check(lsm, pgp->pg,
							   ostid_id(&oa->o_oi),
							   pgp->off, pgp->count);
			if (vrc != 0 && rc == 0)
				rc = vrc;
		}
		OBD_PAGE_FREE(pgp->pg);
	}
	OBD_FREE(pga, npages * sizeof(*pga));
	OBD_FREE(pages, npages * sizeof(*pages));
	return rc;
}

static int echo_client_prep_commit(const struct lu_env *env,
				   struct obd_export *exp, int rw,
				   struct obdo *oa, struct echo_object *eco,
				   u64 offset, u64 count,
				   u64 batch, struct obd_trans_info *oti,
				   int async)
{
	struct lov_stripe_md *lsm = eco->eo_lsm;
	struct obd_ioobj ioo;
	struct niobuf_local *lnb;
	struct niobuf_remote *rnb;
	u64 off;
	u64 npages, tot_pages;
	int i, ret = 0, brw_flags = 0;

	if (count <= 0 || (count & (~CFS_PAGE_MASK)) != 0 ||
	    (lsm != NULL && ostid_id(&lsm->lsm_oi) != ostid_id(&oa->o_oi)))
		return -EINVAL;

	npages = batch >> PAGE_CACHE_SHIFT;
	tot_pages = count >> PAGE_CACHE_SHIFT;

	OBD_ALLOC(lnb, npages * sizeof(struct niobuf_local));
	OBD_ALLOC(rnb, npages * sizeof(struct niobuf_remote));

	if (lnb == NULL || rnb == NULL) {
		ret = -ENOMEM;
		goto out;
	}

	if (rw == OBD_BRW_WRITE && async)
		brw_flags |= OBD_BRW_ASYNC;

	obdo_to_ioobj(oa, &ioo);

	off = offset;

	for (; tot_pages; tot_pages -= npages) {
		int lpages;

		if (tot_pages < npages)
			npages = tot_pages;

		for (i = 0; i < npages; i++, off += PAGE_CACHE_SIZE) {
			rnb[i].offset = off;
			rnb[i].len = PAGE_CACHE_SIZE;
			rnb[i].flags = brw_flags;
		}

		ioo.ioo_bufcnt = npages;
		oti->oti_transno = 0;

		lpages = npages;
		ret = obd_preprw(env, rw, exp, oa, 1, &ioo, rnb, &lpages,
				 lnb, oti, NULL);
		if (ret != 0)
			goto out;
		LASSERT(lpages == npages);

		for (i = 0; i < lpages; i++) {
			struct page *page = lnb[i].page;

			/* read past eof? */
			if (page == NULL && lnb[i].rc == 0)
				continue;

			if (async)
				lnb[i].flags |= OBD_BRW_ASYNC;

			if (ostid_id(&oa->o_oi) == ECHO_PERSISTENT_OBJID ||
			    (oa->o_valid & OBD_MD_FLFLAGS) == 0 ||
			    (oa->o_flags & OBD_FL_DEBUG_CHECK) == 0)
				continue;

			if (rw == OBD_BRW_WRITE)
				echo_client_page_debug_setup(lsm, page, rw,
							    ostid_id(&oa->o_oi),
							     rnb[i].offset,
							     rnb[i].len);
			else
				echo_client_page_debug_check(lsm, page,
							    ostid_id(&oa->o_oi),
							     rnb[i].offset,
							     rnb[i].len);
		}

		ret = obd_commitrw(env, rw, exp, oa, 1, &ioo,
				   rnb, npages, lnb, oti, ret);
		if (ret != 0)
			goto out;

		/* Reset oti otherwise it would confuse ldiskfs. */
		memset(oti, 0, sizeof(*oti));

		/* Reuse env context. */
		lu_context_exit((struct lu_context *)&env->le_ctx);
		lu_context_enter((struct lu_context *)&env->le_ctx);
	}

out:
	if (lnb)
		OBD_FREE(lnb, npages * sizeof(struct niobuf_local));
	if (rnb)
		OBD_FREE(rnb, npages * sizeof(struct niobuf_remote));
	return ret;
}

static int echo_client_brw_ioctl(const struct lu_env *env, int rw,
				 struct obd_export *exp,
				 struct obd_ioctl_data *data,
				 struct obd_trans_info *dummy_oti)
{
	struct obd_device *obd = class_exp2obd(exp);
	struct echo_device *ed = obd2echo_dev(obd);
	struct echo_client_obd *ec = ed->ed_ec;
	struct obdo *oa = &data->ioc_obdo1;
	struct echo_object *eco;
	int rc;
	int async = 1;
	long test_mode;

	LASSERT(oa->o_valid & OBD_MD_FLGROUP);

	rc = echo_get_object(&eco, ed, oa);
	if (rc)
		return rc;

	oa->o_valid &= ~OBD_MD_FLHANDLE;

	/* OFD/obdfilter works only via prep/commit */
	test_mode = (long)data->ioc_pbuf1;
	if (test_mode == 1)
		async = 0;

	if (ed->ed_next == NULL && test_mode != 3) {
		test_mode = 3;
		data->ioc_plen1 = data->ioc_count;
	}

	/* Truncate batch size to maximum */
	if (data->ioc_plen1 > PTLRPC_MAX_BRW_SIZE)
		data->ioc_plen1 = PTLRPC_MAX_BRW_SIZE;

	switch (test_mode) {
	case 1:
		/* fall through */
	case 2:
		rc = echo_client_kbrw(ed, rw, oa,
				      eco, data->ioc_offset,
				      data->ioc_count, async, dummy_oti);
		break;
	case 3:
		rc = echo_client_prep_commit(env, ec->ec_exp, rw, oa,
					     eco, data->ioc_offset,
					     data->ioc_count, data->ioc_plen1,
					     dummy_oti, async);
		break;
	default:
		rc = -EINVAL;
	}
	echo_put_object(eco);
	return rc;
}

static int
echo_client_enqueue(struct obd_export *exp, struct obdo *oa,
		    int mode, u64 offset, u64 nob)
{
	struct echo_device     *ed = obd2echo_dev(exp->exp_obd);
	struct lustre_handle   *ulh = &oa->o_handle;
	struct echo_object     *eco;
	u64		 end;
	int		     rc;

	if (ed->ed_next == NULL)
		return -EOPNOTSUPP;

	if (!(mode == LCK_PR || mode == LCK_PW))
		return -EINVAL;

	if ((offset & (~CFS_PAGE_MASK)) != 0 ||
	    (nob & (~CFS_PAGE_MASK)) != 0)
		return -EINVAL;

	rc = echo_get_object(&eco, ed, oa);
	if (rc != 0)
		return rc;

	end = (nob == 0) ? ((u64) -1) : (offset + nob - 1);
	rc = cl_echo_enqueue(eco, offset, end, mode, &ulh->cookie);
	if (rc == 0) {
		oa->o_valid |= OBD_MD_FLHANDLE;
		CDEBUG(D_INFO, "Cookie is %#llx\n", ulh->cookie);
	}
	echo_put_object(eco);
	return rc;
}

static int
echo_client_cancel(struct obd_export *exp, struct obdo *oa)
{
	struct echo_device *ed     = obd2echo_dev(exp->exp_obd);
	__u64	       cookie = oa->o_handle.cookie;

	if ((oa->o_valid & OBD_MD_FLHANDLE) == 0)
		return -EINVAL;

	CDEBUG(D_INFO, "Cookie is %#llx\n", cookie);
	return cl_echo_cancel(ed, cookie);
}

static int
echo_client_iocontrol(unsigned int cmd, struct obd_export *exp, int len,
		      void *karg, void *uarg)
{
	struct obd_device      *obd = exp->exp_obd;
	struct echo_device     *ed = obd2echo_dev(obd);
	struct echo_client_obd *ec = ed->ed_ec;
	struct echo_object     *eco;
	struct obd_ioctl_data  *data = karg;
	struct obd_trans_info   dummy_oti;
	struct lu_env	  *env;
	struct oti_req_ack_lock *ack_lock;
	struct obdo	    *oa;
	struct lu_fid	   fid;
	int		     rw = OBD_BRW_READ;
	int		     rc = 0;
	int		     i;

	memset(&dummy_oti, 0, sizeof(dummy_oti));

	oa = &data->ioc_obdo1;
	if (!(oa->o_valid & OBD_MD_FLGROUP)) {
		oa->o_valid |= OBD_MD_FLGROUP;
		ostid_set_seq_echo(&oa->o_oi);
	}

	/* This FID is unpacked just for validation at this point */
	rc = ostid_to_fid(&fid, &oa->o_oi, 0);
	if (rc < 0)
		return rc;

	OBD_ALLOC_PTR(env);
	if (env == NULL)
		return -ENOMEM;

	rc = lu_env_init(env, LCT_DT_THREAD);
	if (rc) {
		rc = -ENOMEM;
		goto out;
	}

	switch (cmd) {
	case OBD_IOC_CREATE:		    /* may create echo object */
		if (!capable(CFS_CAP_SYS_ADMIN)) {
			rc = -EPERM;
			goto out;
		}

		rc = echo_create_object(env, ed, 1, oa, data->ioc_pbuf1,
					data->ioc_plen1, &dummy_oti);
		goto out;

	case OBD_IOC_DESTROY:
		if (!capable(CFS_CAP_SYS_ADMIN)) {
			rc = -EPERM;
			goto out;
		}

		rc = echo_get_object(&eco, ed, oa);
		if (rc == 0) {
			rc = obd_destroy(env, ec->ec_exp, oa, eco->eo_lsm,
					 &dummy_oti, NULL, NULL);
			if (rc == 0)
				eco->eo_deleted = 1;
			echo_put_object(eco);
		}
		goto out;

	case OBD_IOC_GETATTR:
		rc = echo_get_object(&eco, ed, oa);
		if (rc == 0) {
			struct obd_info oinfo = { { { 0 } } };

			oinfo.oi_md = eco->eo_lsm;
			oinfo.oi_oa = oa;
			rc = obd_getattr(env, ec->ec_exp, &oinfo);
			echo_put_object(eco);
		}
		goto out;

	case OBD_IOC_SETATTR:
		if (!capable(CFS_CAP_SYS_ADMIN)) {
			rc = -EPERM;
			goto out;
		}

		rc = echo_get_object(&eco, ed, oa);
		if (rc == 0) {
			struct obd_info oinfo = { { { 0 } } };

			oinfo.oi_oa = oa;
			oinfo.oi_md = eco->eo_lsm;

			rc = obd_setattr(env, ec->ec_exp, &oinfo, NULL);
			echo_put_object(eco);
		}
		goto out;

	case OBD_IOC_BRW_WRITE:
		if (!capable(CFS_CAP_SYS_ADMIN)) {
			rc = -EPERM;
			goto out;
		}

		rw = OBD_BRW_WRITE;
		/* fall through */
	case OBD_IOC_BRW_READ:
		rc = echo_client_brw_ioctl(env, rw, exp, data, &dummy_oti);
		goto out;

	case ECHO_IOC_GET_STRIPE:
		rc = echo_get_object(&eco, ed, oa);
		if (rc == 0) {
			rc = echo_copyout_lsm(eco->eo_lsm, data->ioc_pbuf1,
					      data->ioc_plen1);
			echo_put_object(eco);
		}
		goto out;

	case ECHO_IOC_SET_STRIPE:
		if (!capable(CFS_CAP_SYS_ADMIN)) {
			rc = -EPERM;
			goto out;
		}

		if (data->ioc_pbuf1 == NULL) {  /* unset */
			rc = echo_get_object(&eco, ed, oa);
			if (rc == 0) {
				eco->eo_deleted = 1;
				echo_put_object(eco);
			}
		} else {
			rc = echo_create_object(env, ed, 0, oa,
						data->ioc_pbuf1,
						data->ioc_plen1, &dummy_oti);
		}
		goto out;

	case ECHO_IOC_ENQUEUE:
		if (!capable(CFS_CAP_SYS_ADMIN)) {
			rc = -EPERM;
			goto out;
		}

		rc = echo_client_enqueue(exp, oa,
					 data->ioc_conn1, /* lock mode */
					 data->ioc_offset,
					 data->ioc_count);/*extent*/
		goto out;

	case ECHO_IOC_CANCEL:
		rc = echo_client_cancel(exp, oa);
		goto out;

	default:
		CERROR("echo_ioctl(): unrecognised ioctl %#x\n", cmd);
		rc = -ENOTTY;
		goto out;
	}

out:
	lu_env_fini(env);
	OBD_FREE_PTR(env);

	/* XXX this should be in a helper also called by target_send_reply */
	for (ack_lock = dummy_oti.oti_ack_locks, i = 0; i < 4;
	     i++, ack_lock++) {
		if (!ack_lock->mode)
			break;
		ldlm_lock_decref(&ack_lock->lock, ack_lock->mode);
	}

	return rc;
}

static int echo_client_setup(const struct lu_env *env,
			     struct obd_device *obddev, struct lustre_cfg *lcfg)
{
	struct echo_client_obd *ec = &obddev->u.echo_client;
	struct obd_device *tgt;
	struct obd_uuid echo_uuid = { "ECHO_UUID" };
	struct obd_connect_data *ocd = NULL;
	int rc;

	if (lcfg->lcfg_bufcount < 2 || LUSTRE_CFG_BUFLEN(lcfg, 1) < 1) {
		CERROR("requires a TARGET OBD name\n");
		return -EINVAL;
	}

	tgt = class_name2obd(lustre_cfg_string(lcfg, 1));
	if (!tgt || !tgt->obd_attached || !tgt->obd_set_up) {
		CERROR("device not attached or not set up (%s)\n",
		       lustre_cfg_string(lcfg, 1));
		return -EINVAL;
	}

	spin_lock_init(&ec->ec_lock);
	INIT_LIST_HEAD(&ec->ec_objects);
	INIT_LIST_HEAD(&ec->ec_locks);
	ec->ec_unique = 0;
	ec->ec_nstripes = 0;

	OBD_ALLOC(ocd, sizeof(*ocd));
	if (ocd == NULL) {
		CERROR("Can't alloc ocd connecting to %s\n",
		       lustre_cfg_string(lcfg, 1));
		return -ENOMEM;
	}

	ocd->ocd_connect_flags = OBD_CONNECT_VERSION | OBD_CONNECT_REQPORTAL |
				 OBD_CONNECT_BRW_SIZE |
				 OBD_CONNECT_GRANT | OBD_CONNECT_FULL20 |
				 OBD_CONNECT_64BITHASH | OBD_CONNECT_LVB_TYPE |
				 OBD_CONNECT_FID;
	ocd->ocd_brw_size = DT_MAX_BRW_SIZE;
	ocd->ocd_version = LUSTRE_VERSION_CODE;
	ocd->ocd_group = FID_SEQ_ECHO;

	rc = obd_connect(env, &ec->ec_exp, tgt, &echo_uuid, ocd, NULL);
	if (rc == 0) {
		/* Turn off pinger because it connects to tgt obd directly. */
		spin_lock(&tgt->obd_dev_lock);
		list_del_init(&ec->ec_exp->exp_obd_chain_timed);
		spin_unlock(&tgt->obd_dev_lock);
	}

	OBD_FREE(ocd, sizeof(*ocd));

	if (rc != 0) {
		CERROR("fail to connect to device %s\n",
		       lustre_cfg_string(lcfg, 1));
		return rc;
	}

	return rc;
}

static int echo_client_cleanup(struct obd_device *obddev)
{
	struct echo_client_obd *ec = &obddev->u.echo_client;
	int rc;

	if (!list_empty(&obddev->obd_exports)) {
		CERROR("still has clients!\n");
		return -EBUSY;
	}

	LASSERT(atomic_read(&ec->ec_exp->exp_refcount) > 0);
	rc = obd_disconnect(ec->ec_exp);
	if (rc != 0)
		CERROR("fail to disconnect device: %d\n", rc);

	return rc;
}

static int echo_client_connect(const struct lu_env *env,
			       struct obd_export **exp,
			       struct obd_device *src, struct obd_uuid *cluuid,
			       struct obd_connect_data *data, void *localdata)
{
	int		rc;
	struct lustre_handle conn = { 0 };

	rc = class_connect(&conn, src, cluuid);
	if (rc == 0) {
		*exp = class_conn2export(&conn);
	}

	return rc;
}

static int echo_client_disconnect(struct obd_export *exp)
{
	int		     rc;

	if (exp == NULL) {
		rc = -EINVAL;
		goto out;
	}

	rc = class_disconnect(exp);
	goto out;
 out:
	return rc;
}

static struct obd_ops echo_client_obd_ops = {
	.o_owner       = THIS_MODULE,
	.o_iocontrol   = echo_client_iocontrol,
	.o_connect     = echo_client_connect,
	.o_disconnect  = echo_client_disconnect
};

int echo_client_init(void)
{
	struct lprocfs_static_vars lvars = { NULL };
	int rc;

	lprocfs_echo_init_vars(&lvars);

	rc = lu_kmem_init(echo_caches);
	if (rc == 0) {
		rc = class_register_type(&echo_client_obd_ops, NULL,
					 lvars.module_vars,
					 LUSTRE_ECHO_CLIENT_NAME,
					 &echo_device_type);
		if (rc)
			lu_kmem_fini(echo_caches);
	}
	return rc;
}

void echo_client_exit(void)
{
	class_unregister_type(LUSTRE_ECHO_CLIENT_NAME);
	lu_kmem_fini(echo_caches);
}

static int __init obdecho_init(void)
{
	struct lprocfs_static_vars lvars;

	LCONSOLE_INFO("Echo OBD driver; http://www.lustre.org/\n");

	LASSERT(PAGE_CACHE_SIZE % OBD_ECHO_BLOCK_SIZE == 0);

	lprocfs_echo_init_vars(&lvars);


	return echo_client_init();
}

static void /*__exit*/ obdecho_exit(void)
{
	echo_client_exit();

}

MODULE_AUTHOR("Sun Microsystems, Inc. <http://www.lustre.org/>");
MODULE_DESCRIPTION("Lustre Testing Echo OBD driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(LUSTRE_VERSION_STRING);

module_init(obdecho_init);
module_exit(obdecho_exit);

/** @} echo_client */

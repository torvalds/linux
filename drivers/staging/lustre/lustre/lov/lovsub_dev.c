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
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 *
 * Implementation of cl_device and cl_device_type for LOVSUB layer.
 *
 *   Author: Nikita Danilov <nikita.danilov@sun.com>
 */

#define DEBUG_SUBSYSTEM S_LOV

#include "lov_cl_internal.h"

/** \addtogroup lov
 *  @{
 */

/*****************************************************************************
 *
 * Lovsub transfer operations.
 *
 */

static void lovsub_req_completion(const struct lu_env *env,
				  const struct cl_req_slice *slice, int ioret)
{
	struct lovsub_req *lsr;

	lsr = cl2lovsub_req(slice);
	OBD_SLAB_FREE_PTR(lsr, lovsub_req_kmem);
}

/**
 * Implementation of struct cl_req_operations::cro_attr_set() for lovsub
 * layer. Lov and lovsub are responsible only for struct obdo::o_stripe_idx
 * field, which is filled there.
 */
static void lovsub_req_attr_set(const struct lu_env *env,
				const struct cl_req_slice *slice,
				const struct cl_object *obj,
				struct cl_req_attr *attr, u64 flags)
{
	struct lovsub_object *subobj;

	subobj = cl2lovsub(obj);
	/*
	 * There is no OBD_MD_* flag for obdo::o_stripe_idx, so set it
	 * unconditionally. It never changes anyway.
	 */
	attr->cra_oa->o_stripe_idx = subobj->lso_index;
}

static const struct cl_req_operations lovsub_req_ops = {
	.cro_attr_set   = lovsub_req_attr_set,
	.cro_completion = lovsub_req_completion
};

/*****************************************************************************
 *
 * Lov-sub device and device type functions.
 *
 */

static int lovsub_device_init(const struct lu_env *env, struct lu_device *d,
			      const char *name, struct lu_device *next)
{
	struct lovsub_device  *lsd = lu2lovsub_dev(d);
	struct lu_device_type *ldt;
	int rc;

	next->ld_site = d->ld_site;
	ldt = next->ld_type;
	LASSERT(ldt != NULL);
	rc = ldt->ldt_ops->ldto_device_init(env, next, ldt->ldt_name, NULL);
	if (rc) {
		next->ld_site = NULL;
		return rc;
	}

	lu_device_get(next);
	lu_ref_add(&next->ld_reference, "lu-stack", &lu_site_init);
	lsd->acid_next = lu2cl_dev(next);
	return rc;
}

static struct lu_device *lovsub_device_fini(const struct lu_env *env,
					    struct lu_device *d)
{
	struct lu_device *next;
	struct lovsub_device *lsd;

	lsd = lu2lovsub_dev(d);
	next = cl2lu_dev(lsd->acid_next);
	lsd->acid_super = NULL;
	lsd->acid_next = NULL;
	return next;
}

static struct lu_device *lovsub_device_free(const struct lu_env *env,
					    struct lu_device *d)
{
	struct lovsub_device *lsd  = lu2lovsub_dev(d);
	struct lu_device     *next = cl2lu_dev(lsd->acid_next);

	if (atomic_read(&d->ld_ref) && d->ld_site) {
		LIBCFS_DEBUG_MSG_DATA_DECL(msgdata, D_ERROR, NULL);
		lu_site_print(env, d->ld_site, &msgdata, lu_cdebug_printer);
	}
	cl_device_fini(lu2cl_dev(d));
	OBD_FREE_PTR(lsd);
	return next;
}

static int lovsub_req_init(const struct lu_env *env, struct cl_device *dev,
			   struct cl_req *req)
{
	struct lovsub_req *lsr;
	int result;

	OBD_SLAB_ALLOC_PTR_GFP(lsr, lovsub_req_kmem, GFP_NOFS);
	if (lsr != NULL) {
		cl_req_slice_add(req, &lsr->lsrq_cl, dev, &lovsub_req_ops);
		result = 0;
	} else
		result = -ENOMEM;
	return result;
}

static const struct lu_device_operations lovsub_lu_ops = {
	.ldo_object_alloc      = lovsub_object_alloc,
	.ldo_process_config    = NULL,
	.ldo_recovery_complete = NULL
};

static const struct cl_device_operations lovsub_cl_ops = {
	.cdo_req_init = lovsub_req_init
};

static struct lu_device *lovsub_device_alloc(const struct lu_env *env,
					     struct lu_device_type *t,
					     struct lustre_cfg *cfg)
{
	struct lu_device     *d;
	struct lovsub_device *lsd;

	OBD_ALLOC_PTR(lsd);
	if (lsd != NULL) {
		int result;

		result = cl_device_init(&lsd->acid_cl, t);
		if (result == 0) {
			d = lovsub2lu_dev(lsd);
			d->ld_ops	 = &lovsub_lu_ops;
			lsd->acid_cl.cd_ops = &lovsub_cl_ops;
		} else
			d = ERR_PTR(result);
	} else
		d = ERR_PTR(-ENOMEM);
	return d;
}

static const struct lu_device_type_operations lovsub_device_type_ops = {
	.ldto_device_alloc = lovsub_device_alloc,
	.ldto_device_free  = lovsub_device_free,

	.ldto_device_init    = lovsub_device_init,
	.ldto_device_fini    = lovsub_device_fini
};

#define LUSTRE_LOVSUB_NAME	 "lovsub"

struct lu_device_type lovsub_device_type = {
	.ldt_tags     = LU_DEVICE_CL,
	.ldt_name     = LUSTRE_LOVSUB_NAME,
	.ldt_ops      = &lovsub_device_type_ops,
	.ldt_ctx_tags = LCT_CL_THREAD
};


/** @} lov */

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
 * Copyright (c) 2012, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 *
 * Implementation of cl_object for LOVSUB layer.
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
 * Lovsub object operations.
 *
 */

int lovsub_object_init(const struct lu_env *env, struct lu_object *obj,
		       const struct lu_object_conf *conf)
{
	struct lovsub_device  *dev   = lu2lovsub_dev(obj->lo_dev);
	struct lu_object      *below;
	struct lu_device      *under;

	int result;

	under = &dev->acid_next->cd_lu_dev;
	below = under->ld_ops->ldo_object_alloc(env, obj->lo_header, under);
	if (below != NULL) {
		lu_object_add(obj, below);
		cl_object_page_init(lu2cl(obj), sizeof(struct lovsub_page));
		result = 0;
	} else
		result = -ENOMEM;
	return result;

}

static void lovsub_object_free(const struct lu_env *env, struct lu_object *obj)
{
	struct lovsub_object *los = lu2lovsub(obj);
	struct lov_object    *lov = los->lso_super;

	/* We can't assume lov was assigned here, because of the shadow
	 * object handling in lu_object_find.
	 */
	if (lov) {
		LASSERT(lov->lo_type == LLT_RAID0);
		LASSERT(lov->u.raid0.lo_sub[los->lso_index] == los);
		spin_lock(&lov->u.raid0.lo_sub_lock);
		lov->u.raid0.lo_sub[los->lso_index] = NULL;
		spin_unlock(&lov->u.raid0.lo_sub_lock);
	}

	lu_object_fini(obj);
	lu_object_header_fini(&los->lso_header.coh_lu);
	OBD_SLAB_FREE_PTR(los, lovsub_object_kmem);
}

static int lovsub_object_print(const struct lu_env *env, void *cookie,
			       lu_printer_t p, const struct lu_object *obj)
{
	struct lovsub_object *los = lu2lovsub(obj);

	return (*p)(env, cookie, "[%d]", los->lso_index);
}

static int lovsub_attr_set(const struct lu_env *env, struct cl_object *obj,
			   const struct cl_attr *attr, unsigned valid)
{
	struct lov_object *lov = cl2lovsub(obj)->lso_super;

	lov_r0(lov)->lo_attr_valid = 0;
	return 0;
}

static int lovsub_object_glimpse(const struct lu_env *env,
				 const struct cl_object *obj,
				 struct ost_lvb *lvb)
{
	struct lovsub_object *los = cl2lovsub(obj);

	return cl_object_glimpse(env, &los->lso_super->lo_cl, lvb);
}



static const struct cl_object_operations lovsub_ops = {
	.coo_page_init = lovsub_page_init,
	.coo_lock_init = lovsub_lock_init,
	.coo_attr_set  = lovsub_attr_set,
	.coo_glimpse   = lovsub_object_glimpse
};

static const struct lu_object_operations lovsub_lu_obj_ops = {
	.loo_object_init      = lovsub_object_init,
	.loo_object_delete    = NULL,
	.loo_object_release   = NULL,
	.loo_object_free      = lovsub_object_free,
	.loo_object_print     = lovsub_object_print,
	.loo_object_invariant = NULL
};

struct lu_object *lovsub_object_alloc(const struct lu_env *env,
				      const struct lu_object_header *unused,
				      struct lu_device *dev)
{
	struct lovsub_object *los;
	struct lu_object     *obj;

	OBD_SLAB_ALLOC_PTR_GFP(los, lovsub_object_kmem, __GFP_IO);
	if (los != NULL) {
		struct cl_object_header *hdr;

		obj = lovsub2lu(los);
		hdr = &los->lso_header;
		cl_object_header_init(hdr);
		lu_object_init(obj, &hdr->coh_lu, dev);
		lu_object_add_top(&hdr->coh_lu, obj);
		los->lso_cl.co_ops = &lovsub_ops;
		obj->lo_ops = &lovsub_lu_obj_ops;
	} else
		obj = NULL;
	return obj;
}

/** @} lov */

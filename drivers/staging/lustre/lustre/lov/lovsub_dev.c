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
 * Copyright (c) 2008, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright (c) 2013, 2015, Intel Corporation.
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
	kfree(lsd);
	return next;
}

static const struct lu_device_operations lovsub_lu_ops = {
	.ldo_object_alloc      = lovsub_object_alloc,
	.ldo_process_config    = NULL,
	.ldo_recovery_complete = NULL
};

static struct lu_device *lovsub_device_alloc(const struct lu_env *env,
					     struct lu_device_type *t,
					     struct lustre_cfg *cfg)
{
	struct lu_device     *d;
	struct lovsub_device *lsd;

	lsd = kzalloc(sizeof(*lsd), GFP_NOFS);
	if (lsd) {
		int result;

		result = cl_device_init(&lsd->acid_cl, t);
		if (result == 0) {
			d = lovsub2lu_dev(lsd);
			d->ld_ops	 = &lovsub_lu_ops;
		} else {
			d = ERR_PTR(result);
		}
	} else {
		d = ERR_PTR(-ENOMEM);
	}
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

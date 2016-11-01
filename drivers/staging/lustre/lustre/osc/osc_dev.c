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
 * Copyright (c) 2012, 2015, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 *
 * Implementation of cl_device, cl_req for OSC layer.
 *
 *   Author: Nikita Danilov <nikita.danilov@sun.com>
 */

#define DEBUG_SUBSYSTEM S_OSC

/* class_name2obd() */
#include "../include/obd_class.h"

#include "osc_cl_internal.h"

/** \addtogroup osc
 * @{
 */

struct kmem_cache *osc_lock_kmem;
struct kmem_cache *osc_object_kmem;
struct kmem_cache *osc_thread_kmem;
struct kmem_cache *osc_session_kmem;
struct kmem_cache *osc_req_kmem;
struct kmem_cache *osc_extent_kmem;
struct kmem_cache *osc_quota_kmem;

struct lu_kmem_descr osc_caches[] = {
	{
		.ckd_cache = &osc_lock_kmem,
		.ckd_name  = "osc_lock_kmem",
		.ckd_size  = sizeof(struct osc_lock)
	},
	{
		.ckd_cache = &osc_object_kmem,
		.ckd_name  = "osc_object_kmem",
		.ckd_size  = sizeof(struct osc_object)
	},
	{
		.ckd_cache = &osc_thread_kmem,
		.ckd_name  = "osc_thread_kmem",
		.ckd_size  = sizeof(struct osc_thread_info)
	},
	{
		.ckd_cache = &osc_session_kmem,
		.ckd_name  = "osc_session_kmem",
		.ckd_size  = sizeof(struct osc_session)
	},
	{
		.ckd_cache = &osc_req_kmem,
		.ckd_name  = "osc_req_kmem",
		.ckd_size  = sizeof(struct osc_req)
	},
	{
		.ckd_cache = &osc_extent_kmem,
		.ckd_name  = "osc_extent_kmem",
		.ckd_size  = sizeof(struct osc_extent)
	},
	{
		.ckd_cache = &osc_quota_kmem,
		.ckd_name  = "osc_quota_kmem",
		.ckd_size  = sizeof(struct osc_quota_info)
	},
	{
		.ckd_cache = NULL
	}
};

struct lock_class_key osc_ast_guard_class;

/*****************************************************************************
 *
 * Type conversions.
 *
 */

static struct lu_device *osc2lu_dev(struct osc_device *osc)
{
	return &osc->od_cl.cd_lu_dev;
}

/*****************************************************************************
 *
 * Osc device and device type functions.
 *
 */

static void *osc_key_init(const struct lu_context *ctx,
			  struct lu_context_key *key)
{
	struct osc_thread_info *info;

	info = kmem_cache_zalloc(osc_thread_kmem, GFP_NOFS);
	if (!info)
		info = ERR_PTR(-ENOMEM);
	return info;
}

static void osc_key_fini(const struct lu_context *ctx,
			 struct lu_context_key *key, void *data)
{
	struct osc_thread_info *info = data;

	kmem_cache_free(osc_thread_kmem, info);
}

struct lu_context_key osc_key = {
	.lct_tags = LCT_CL_THREAD,
	.lct_init = osc_key_init,
	.lct_fini = osc_key_fini
};

static void *osc_session_init(const struct lu_context *ctx,
			      struct lu_context_key *key)
{
	struct osc_session *info;

	info = kmem_cache_zalloc(osc_session_kmem, GFP_NOFS);
	if (!info)
		info = ERR_PTR(-ENOMEM);
	return info;
}

static void osc_session_fini(const struct lu_context *ctx,
			     struct lu_context_key *key, void *data)
{
	struct osc_session *info = data;

	kmem_cache_free(osc_session_kmem, info);
}

struct lu_context_key osc_session_key = {
	.lct_tags = LCT_SESSION,
	.lct_init = osc_session_init,
	.lct_fini = osc_session_fini
};

/* type constructor/destructor: osc_type_{init,fini,start,stop}(). */
LU_TYPE_INIT_FINI(osc, &osc_key, &osc_session_key);

static int osc_cl_process_config(const struct lu_env *env,
				 struct lu_device *d, struct lustre_cfg *cfg)
{
	return osc_process_config_base(d->ld_obd, cfg);
}

static const struct lu_device_operations osc_lu_ops = {
	.ldo_object_alloc      = osc_object_alloc,
	.ldo_process_config    = osc_cl_process_config,
	.ldo_recovery_complete = NULL
};

static const struct cl_device_operations osc_cl_ops = {
	.cdo_req_init = osc_req_init
};

static int osc_device_init(const struct lu_env *env, struct lu_device *d,
			   const char *name, struct lu_device *next)
{
	return 0;
}

static struct lu_device *osc_device_fini(const struct lu_env *env,
					 struct lu_device *d)
{
	return NULL;
}

static struct lu_device *osc_device_free(const struct lu_env *env,
					 struct lu_device *d)
{
	struct osc_device *od = lu2osc_dev(d);

	cl_device_fini(lu2cl_dev(d));
	kfree(od);
	return NULL;
}

static struct lu_device *osc_device_alloc(const struct lu_env *env,
					  struct lu_device_type *t,
					  struct lustre_cfg *cfg)
{
	struct lu_device *d;
	struct osc_device *od;
	struct obd_device *obd;
	int rc;

	od = kzalloc(sizeof(*od), GFP_NOFS);
	if (!od)
		return ERR_PTR(-ENOMEM);

	cl_device_init(&od->od_cl, t);
	d = osc2lu_dev(od);
	d->ld_ops = &osc_lu_ops;
	od->od_cl.cd_ops = &osc_cl_ops;

	/* Setup OSC OBD */
	obd = class_name2obd(lustre_cfg_string(cfg, 0));
	LASSERT(obd);
	rc = osc_setup(obd, cfg);
	if (rc) {
		osc_device_free(env, d);
		return ERR_PTR(rc);
	}
	od->od_exp = obd->obd_self_export;
	return d;
}

static const struct lu_device_type_operations osc_device_type_ops = {
	.ldto_init = osc_type_init,
	.ldto_fini = osc_type_fini,

	.ldto_start = osc_type_start,
	.ldto_stop  = osc_type_stop,

	.ldto_device_alloc = osc_device_alloc,
	.ldto_device_free  = osc_device_free,

	.ldto_device_init = osc_device_init,
	.ldto_device_fini = osc_device_fini
};

struct lu_device_type osc_device_type = {
	.ldt_tags = LU_DEVICE_CL,
	.ldt_name = LUSTRE_OSC_NAME,
	.ldt_ops = &osc_device_type_ops,
	.ldt_ctx_tags = LCT_CL_THREAD
};

/** @} osc */

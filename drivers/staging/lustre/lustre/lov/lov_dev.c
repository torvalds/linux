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
 * Implementation of cl_device and cl_device_type for LOV layer.
 *
 *   Author: Nikita Danilov <nikita.danilov@sun.com>
 */

#define DEBUG_SUBSYSTEM S_LOV

/* class_name2obd() */
#include "../include/obd_class.h"

#include "lov_cl_internal.h"
#include "lov_internal.h"

struct kmem_cache *lov_lock_kmem;
struct kmem_cache *lov_object_kmem;
struct kmem_cache *lov_thread_kmem;
struct kmem_cache *lov_session_kmem;

struct kmem_cache *lovsub_lock_kmem;
struct kmem_cache *lovsub_object_kmem;

struct lu_kmem_descr lov_caches[] = {
	{
		.ckd_cache = &lov_lock_kmem,
		.ckd_name  = "lov_lock_kmem",
		.ckd_size  = sizeof(struct lov_lock)
	},
	{
		.ckd_cache = &lov_object_kmem,
		.ckd_name  = "lov_object_kmem",
		.ckd_size  = sizeof(struct lov_object)
	},
	{
		.ckd_cache = &lov_thread_kmem,
		.ckd_name  = "lov_thread_kmem",
		.ckd_size  = sizeof(struct lov_thread_info)
	},
	{
		.ckd_cache = &lov_session_kmem,
		.ckd_name  = "lov_session_kmem",
		.ckd_size  = sizeof(struct lov_session)
	},
	{
		.ckd_cache = &lovsub_lock_kmem,
		.ckd_name  = "lovsub_lock_kmem",
		.ckd_size  = sizeof(struct lovsub_lock)
	},
	{
		.ckd_cache = &lovsub_object_kmem,
		.ckd_name  = "lovsub_object_kmem",
		.ckd_size  = sizeof(struct lovsub_object)
	},
	{
		.ckd_cache = NULL
	}
};

/*****************************************************************************
 *
 * Lov device and device type functions.
 *
 */

static void *lov_key_init(const struct lu_context *ctx,
			  struct lu_context_key *key)
{
	struct lov_thread_info *info;

	info = kmem_cache_zalloc(lov_thread_kmem, GFP_NOFS);
	if (!info)
		info = ERR_PTR(-ENOMEM);
	return info;
}

static void lov_key_fini(const struct lu_context *ctx,
			 struct lu_context_key *key, void *data)
{
	struct lov_thread_info *info = data;

	kmem_cache_free(lov_thread_kmem, info);
}

struct lu_context_key lov_key = {
	.lct_tags = LCT_CL_THREAD,
	.lct_init = lov_key_init,
	.lct_fini = lov_key_fini
};

static void *lov_session_key_init(const struct lu_context *ctx,
				  struct lu_context_key *key)
{
	struct lov_session *info;

	info = kmem_cache_zalloc(lov_session_kmem, GFP_NOFS);
	if (!info)
		info = ERR_PTR(-ENOMEM);
	return info;
}

static void lov_session_key_fini(const struct lu_context *ctx,
				 struct lu_context_key *key, void *data)
{
	struct lov_session *info = data;

	kmem_cache_free(lov_session_kmem, info);
}

struct lu_context_key lov_session_key = {
	.lct_tags = LCT_SESSION,
	.lct_init = lov_session_key_init,
	.lct_fini = lov_session_key_fini
};

/* type constructor/destructor: lov_type_{init,fini,start,stop}() */
LU_TYPE_INIT_FINI(lov, &lov_key, &lov_session_key);

static struct lu_device *lov_device_fini(const struct lu_env *env,
					 struct lu_device *d)
{
	int i;
	struct lov_device *ld = lu2lov_dev(d);

	LASSERT(ld->ld_lov);
	if (!ld->ld_target)
		return NULL;

	lov_foreach_target(ld, i) {
		struct lovsub_device *lsd;

		lsd = ld->ld_target[i];
		if (lsd) {
			cl_stack_fini(env, lovsub2cl_dev(lsd));
			ld->ld_target[i] = NULL;
		}
	}
	return NULL;
}

static int lov_device_init(const struct lu_env *env, struct lu_device *d,
			   const char *name, struct lu_device *next)
{
	struct lov_device *ld = lu2lov_dev(d);
	int i;
	int rc = 0;

	LASSERT(d->ld_site);
	if (!ld->ld_target)
		return rc;

	lov_foreach_target(ld, i) {
		struct lovsub_device *lsd;
		struct cl_device     *cl;
		struct lov_tgt_desc  *desc;

		desc = ld->ld_lov->lov_tgts[i];
		if (!desc)
			continue;

		cl = cl_type_setup(env, d->ld_site, &lovsub_device_type,
				   desc->ltd_obd->obd_lu_dev);
		if (IS_ERR(cl)) {
			rc = PTR_ERR(cl);
			break;
		}
		lsd = cl2lovsub_dev(cl);
		ld->ld_target[i] = lsd;
	}

	if (rc)
		lov_device_fini(env, d);
	else
		ld->ld_flags |= LOV_DEV_INITIALIZED;

	return rc;
}

static struct lu_device *lov_device_free(const struct lu_env *env,
					 struct lu_device *d)
{
	struct lov_device *ld = lu2lov_dev(d);

	cl_device_fini(lu2cl_dev(d));
	kfree(ld->ld_target);
	kfree(ld);
	return NULL;
}

static void lov_cl_del_target(const struct lu_env *env, struct lu_device *dev,
			      __u32 index)
{
	struct lov_device *ld = lu2lov_dev(dev);

	if (ld->ld_target[index]) {
		cl_stack_fini(env, lovsub2cl_dev(ld->ld_target[index]));
		ld->ld_target[index] = NULL;
	}
}

static int lov_expand_targets(const struct lu_env *env, struct lov_device *dev)
{
	int   result;
	__u32 tgt_size;
	__u32 sub_size;

	result = 0;
	tgt_size = dev->ld_lov->lov_tgt_size;
	sub_size = dev->ld_target_nr;
	if (sub_size < tgt_size) {
		struct lovsub_device    **newd;
		const size_t	      sz   = sizeof(newd[0]);

		newd = kcalloc(tgt_size, sz, GFP_NOFS);
		if (newd) {
			if (sub_size > 0) {
				memcpy(newd, dev->ld_target, sub_size * sz);
				kfree(dev->ld_target);
			}
			dev->ld_target    = newd;
			dev->ld_target_nr = tgt_size;
		} else {
			result = -ENOMEM;
		}
	}
	return result;
}

static int lov_cl_add_target(const struct lu_env *env, struct lu_device *dev,
			     __u32 index)
{
	struct obd_device    *obd = dev->ld_obd;
	struct lov_device    *ld  = lu2lov_dev(dev);
	struct lov_tgt_desc  *tgt;
	struct lovsub_device *lsd;
	struct cl_device     *cl;
	int rc;

	obd_getref(obd);

	tgt = obd->u.lov.lov_tgts[index];

	if (!tgt->ltd_obd->obd_set_up) {
		CERROR("Target %s not set up\n", obd_uuid2str(&tgt->ltd_uuid));
		return -EINVAL;
	}

	rc = lov_expand_targets(env, ld);
	if (rc == 0 && ld->ld_flags & LOV_DEV_INITIALIZED) {
		LASSERT(dev->ld_site);

		cl = cl_type_setup(env, dev->ld_site, &lovsub_device_type,
				   tgt->ltd_obd->obd_lu_dev);
		if (!IS_ERR(cl)) {
			lsd = cl2lovsub_dev(cl);
			ld->ld_target[index] = lsd;
		} else {
			CERROR("add failed (%d), deleting %s\n", rc,
			       obd_uuid2str(&tgt->ltd_uuid));
			lov_cl_del_target(env, dev, index);
			rc = PTR_ERR(cl);
		}
	}
	obd_putref(obd);
	return rc;
}

static int lov_process_config(const struct lu_env *env,
			      struct lu_device *d, struct lustre_cfg *cfg)
{
	struct obd_device *obd = d->ld_obd;
	int cmd;
	int rc;
	int gen;
	__u32 index;

	obd_getref(obd);

	cmd = cfg->lcfg_command;
	rc = lov_process_config_base(d->ld_obd, cfg, &index, &gen);
	if (rc == 0) {
		switch (cmd) {
		case LCFG_LOV_ADD_OBD:
		case LCFG_LOV_ADD_INA:
			rc = lov_cl_add_target(env, d, index);
			if (rc != 0)
				lov_del_target(d->ld_obd, index, NULL, 0);
			break;
		case LCFG_LOV_DEL_OBD:
			lov_cl_del_target(env, d, index);
			break;
		}
	}
	obd_putref(obd);
	return rc;
}

static const struct lu_device_operations lov_lu_ops = {
	.ldo_object_alloc      = lov_object_alloc,
	.ldo_process_config    = lov_process_config,
};

static struct lu_device *lov_device_alloc(const struct lu_env *env,
					  struct lu_device_type *t,
					  struct lustre_cfg *cfg)
{
	struct lu_device *d;
	struct lov_device *ld;
	struct obd_device *obd;
	int rc;

	ld = kzalloc(sizeof(*ld), GFP_NOFS);
	if (!ld)
		return ERR_PTR(-ENOMEM);

	cl_device_init(&ld->ld_cl, t);
	d = lov2lu_dev(ld);
	d->ld_ops	= &lov_lu_ops;

	/* setup the LOV OBD */
	obd = class_name2obd(lustre_cfg_string(cfg, 0));
	LASSERT(obd);
	rc = lov_setup(obd, cfg);
	if (rc) {
		lov_device_free(env, d);
		return ERR_PTR(rc);
	}

	ld->ld_lov = &obd->u.lov;
	return d;
}

static const struct lu_device_type_operations lov_device_type_ops = {
	.ldto_init = lov_type_init,
	.ldto_fini = lov_type_fini,

	.ldto_start = lov_type_start,
	.ldto_stop  = lov_type_stop,

	.ldto_device_alloc = lov_device_alloc,
	.ldto_device_free  = lov_device_free,

	.ldto_device_init    = lov_device_init,
	.ldto_device_fini    = lov_device_fini
};

struct lu_device_type lov_device_type = {
	.ldt_tags     = LU_DEVICE_CL,
	.ldt_name     = LUSTRE_LOV_NAME,
	.ldt_ops      = &lov_device_type_ops,
	.ldt_ctx_tags = LCT_CL_THREAD
};

/** @} lov */

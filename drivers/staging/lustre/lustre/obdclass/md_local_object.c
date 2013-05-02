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
 * lustre/obdclass/md_local_object.c
 *
 * Lustre Local Object create APIs
 * 'create on first mount' facility. Files registed under llo module will
 * be created on first mount.
 *
 * Author: Pravin Shelar  <pravin.shelar@sun.com>
 */

#define DEBUG_SUBSYSTEM S_CLASS

#include <obd_support.h>
#include <lustre_disk.h>
#include <lustre_fid.h>
#include <lu_object.h>
#include <linux/list.h>
#include <md_object.h>


/** List head to hold list of objects to be created. */
static struct list_head llo_lobj_list;

/** Lock to protect list manipulations */
static struct mutex	llo_lock;

/**
 * Structure used to maintain state of path parsing.
 * \see llo_find_entry, llo_store_resolve
 */
struct llo_find_hint {
	struct lu_fid	*lfh_cfid;
	struct md_device     *lfh_md;
	struct md_object     *lfh_pobj;
};

/**
 * Thread Local storage for this module.
 */
struct llo_thread_info {
	/** buffer to resolve path */
	char		    lti_buf[DT_MAX_PATH];
	/** used for path resolve */
	struct lu_fid	   lti_fid;
	/** used to pass child object fid */
	struct lu_fid	   lti_cfid;
	struct llo_find_hint    lti_lfh;
	struct md_op_spec       lti_spc;
	struct md_attr	  lti_ma;
	struct lu_name	  lti_lname;
};

LU_KEY_INIT(llod_global, struct llo_thread_info);
LU_KEY_FINI(llod_global, struct llo_thread_info);

static struct lu_context_key llod_key = {
	.lct_tags = LCT_MD_THREAD,
	.lct_init = llod_global_key_init,
	.lct_fini = llod_global_key_fini
};

static inline struct llo_thread_info * llo_env_info(const struct lu_env *env)
{
	return lu_context_key_get(&env->le_ctx,  &llod_key);
}

/**
 * Search md object for given fid.
 */
static struct md_object *llo_locate(const struct lu_env *env,
				    struct md_device *md,
				    const struct lu_fid *fid)
{
	struct lu_object *obj;
	struct md_object *mdo;

	obj = lu_object_find(env, &md->md_lu_dev, fid, NULL);
	if (!IS_ERR(obj)) {
		obj = lu_object_locate(obj->lo_header, md->md_lu_dev.ld_type);
		LASSERT(obj != NULL);
		mdo = (struct md_object *) obj;
	} else
		mdo = (struct md_object *)obj;
	return mdo;
}

/**
 * Lookup FID for object named \a name in directory \a pobj.
 */
static int llo_lookup(const struct lu_env  *env,
		      struct md_object *pobj,
		      const char *name,
		      struct lu_fid *fid)
{
	struct llo_thread_info *info = llo_env_info(env);
	struct lu_name	  *lname = &info->lti_lname;
	struct md_op_spec       *spec = &info->lti_spc;

	spec->sp_feat = NULL;
	spec->sp_cr_flags = 0;
	spec->sp_cr_lookup = 0;
	spec->sp_cr_mode = 0;

	lname->ln_name = name;
	lname->ln_namelen = strlen(name);

	return mdo_lookup(env, pobj, lname, fid, spec);
}

/**
 * Function to look up path component, this is passed to parsing
 * function. \see llo_store_resolve
 *
 * \retval      rc returns error code for lookup or locate operation
 *
 * pointer to object is returned in data (lfh->lfh_pobj)
 */
static int llo_find_entry(const struct lu_env  *env,
			  const char *name, void *data)
{
	struct llo_find_hint    *lfh = data;
	struct md_device	*md = lfh->lfh_md;
	struct lu_fid	   *fid = lfh->lfh_cfid;
	struct md_object	*obj = lfh->lfh_pobj;
	int		     result;

	/* lookup fid for object */
	result = llo_lookup(env, obj, name, fid);
	lu_object_put(env, &obj->mo_lu);

	if (result == 0) {
		/* get md object for fid that we got in lookup */
		obj = llo_locate(env, md, fid);
		if (IS_ERR(obj))
			result = PTR_ERR(obj);
	}

	lfh->lfh_pobj = obj;
	return result;
}

static struct md_object *llo_reg_open(const struct lu_env *env,
				      struct md_device *md,
				      struct md_object *p,
				      const char *name,
				      struct lu_fid *fid)
{
	struct md_object *o;
	int result;

	result = llo_lookup(env, p, name, fid);
	if (result == 0)
		o = llo_locate(env, md, fid);
	else
		o = ERR_PTR(result);

	return o;
}

/**
 * Resolve given \a path, on success function returns
 * md object for last directory and \a fid points to
 * its fid.
 */
struct md_object *llo_store_resolve(const struct lu_env *env,
				    struct md_device *md,
				    struct dt_device *dt,
				    const char *path,
				    struct lu_fid *fid)
{
	struct llo_thread_info *info = llo_env_info(env);
	struct llo_find_hint *lfh = &info->lti_lfh;
	char *local = info->lti_buf;
	struct md_object	*obj;
	int result;

	strncpy(local, path, DT_MAX_PATH);
	local[DT_MAX_PATH - 1] = '\0';

	lfh->lfh_md = md;
	lfh->lfh_cfid = fid;
	/* start path resolution from backend fs root. */
	result = dt->dd_ops->dt_root_get(env, dt, fid);
	if (result == 0) {
		/* get md object for root */
		obj = llo_locate(env, md, fid);
		if (!IS_ERR(obj)) {
			/* start path parser from root md */
			lfh->lfh_pobj = obj;
			result = dt_path_parser(env, local, llo_find_entry, lfh);
			if (result != 0)
				obj = ERR_PTR(result);
			else
				obj = lfh->lfh_pobj;
		}
	} else {
		obj = ERR_PTR(result);
	}
	return obj;
}
EXPORT_SYMBOL(llo_store_resolve);

/**
 * Returns md object for \a objname in given \a dirname.
 */
struct md_object *llo_store_open(const struct lu_env *env,
				 struct md_device *md,
				 struct dt_device *dt,
				 const char *dirname,
				 const char *objname,
				 struct lu_fid *fid)
{
	struct md_object *obj;
	struct md_object *dir;

	/* search md object for parent dir */
	dir = llo_store_resolve(env, md, dt, dirname, fid);
	if (!IS_ERR(dir)) {
		obj = llo_reg_open(env, md, dir, objname, fid);
		lu_object_put(env, &dir->mo_lu);
	} else
		obj = dir;

	return obj;
}
EXPORT_SYMBOL(llo_store_open);

static struct md_object *llo_create_obj(const struct lu_env *env,
					struct md_device *md,
					struct md_object *dir,
					const char *objname,
					const struct lu_fid *fid,
					const struct dt_index_features *feat)
{
	struct llo_thread_info *info = llo_env_info(env);
	struct md_object	*mdo;
	struct md_attr	  *ma = &info->lti_ma;
	struct md_op_spec       *spec = &info->lti_spc;
	struct lu_name	  *lname = &info->lti_lname;
	struct lu_attr	  *la = &ma->ma_attr;
	int rc;

	mdo = llo_locate(env, md, fid);
	if (IS_ERR(mdo))
		return mdo;

	lname->ln_name = objname;
	lname->ln_namelen = strlen(objname);

	spec->sp_feat = feat;
	spec->sp_cr_flags = 0;
	spec->sp_cr_lookup = 1;
	spec->sp_cr_mode = 0;

	if (feat == &dt_directory_features)
		la->la_mode = S_IFDIR | S_IXUGO;
	else
		la->la_mode = S_IFREG;

	la->la_mode |= S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;
	la->la_uid = la->la_gid = 0;
	la->la_valid = LA_MODE | LA_UID | LA_GID;

	ma->ma_valid = 0;
	ma->ma_need = 0;

	rc = mdo_create(env, dir, lname, mdo, spec, ma);

	if (rc) {
		lu_object_put(env, &mdo->mo_lu);
		mdo = ERR_PTR(rc);
	}

	return mdo;
}

/**
 * Create md object, object could be diretcory or
 * special index defined by \a feat in \a directory.
 *
 *       \param  md       device
 *       \param  dirname  parent directory
 *       \param  objname  file name
 *       \param  fid      object fid
 *       \param  feat     index features required for directory create
 */

struct md_object *llo_store_create_index(const struct lu_env *env,
					 struct md_device *md,
					 struct dt_device *dt,
					 const char *dirname,
					 const char *objname,
					 const struct lu_fid *fid,
					 const struct dt_index_features *feat)
{
	struct llo_thread_info *info = llo_env_info(env);
	struct md_object *obj;
	struct md_object *dir;
	struct lu_fid *ignore = &info->lti_fid;

	dir = llo_store_resolve(env, md, dt, dirname, ignore);
	if (!IS_ERR(dir)) {
		obj = llo_create_obj(env, md, dir, objname, fid, feat);
		lu_object_put(env, &dir->mo_lu);
	} else {
		obj = dir;
	}
	return obj;
}

EXPORT_SYMBOL(llo_store_create_index);

/**
 * Create md object for regular file in \a directory.
 *
 *       \param  md       device
 *       \param  dirname  parent directory
 *       \param  objname  file name
 *       \param  fid      object fid.
 */

struct md_object *llo_store_create(const struct lu_env *env,
				   struct md_device *md,
				   struct dt_device *dt,
				   const char *dirname,
				   const char *objname,
				   const struct lu_fid *fid)
{
	return llo_store_create_index(env, md, dt, dirname,
				      objname, fid, NULL);
}

EXPORT_SYMBOL(llo_store_create);

/**
 * Register object for 'create on first mount' facility.
 * objects are created in order of registration.
 */

void llo_local_obj_register(struct lu_local_obj_desc *llod)
{
	mutex_lock(&llo_lock);
	list_add_tail(&llod->llod_linkage, &llo_lobj_list);
	mutex_unlock(&llo_lock);
}

EXPORT_SYMBOL(llo_local_obj_register);

void llo_local_obj_unregister(struct lu_local_obj_desc *llod)
{
	mutex_lock(&llo_lock);
	list_del(&llod->llod_linkage);
	mutex_unlock(&llo_lock);
}

EXPORT_SYMBOL(llo_local_obj_unregister);

/**
 * Created registed objects.
 */

int llo_local_objects_setup(const struct lu_env *env,
			     struct md_device * md,
			     struct dt_device *dt)
{
	struct llo_thread_info *info = llo_env_info(env);
	struct lu_fid *fid;
	struct lu_local_obj_desc *scan;
	struct md_object *mdo;
	const char *dir;
	int rc = 0;

	fid = &info->lti_cfid;
	mutex_lock(&llo_lock);

	list_for_each_entry(scan, &llo_lobj_list, llod_linkage) {
		lu_local_obj_fid(fid, scan->llod_oid);
		dir = "";
		if (scan->llod_dir)
			dir = scan->llod_dir;

		if (scan->llod_is_index)
			mdo = llo_store_create_index(env, md, dt ,
						     dir, scan->llod_name,
						     fid,
						     scan->llod_feat);
		else
			mdo = llo_store_create(env, md, dt,
					       dir, scan->llod_name,
					       fid);
		if (IS_ERR(mdo) && PTR_ERR(mdo) != -EEXIST) {
			rc = PTR_ERR(mdo);
			CERROR("creating obj [%s] fid = "DFID" rc = %d\n",
			       scan->llod_name, PFID(fid), rc);
			goto out;
		}

		if (!IS_ERR(mdo))
			lu_object_put(env, &mdo->mo_lu);
	}

out:
	mutex_unlock(&llo_lock);
	return rc;
}

EXPORT_SYMBOL(llo_local_objects_setup);

int llo_global_init(void)
{
	int result;

	INIT_LIST_HEAD(&llo_lobj_list);
	mutex_init(&llo_lock);

	LU_CONTEXT_KEY_INIT(&llod_key);
	result = lu_context_key_register(&llod_key);
	return result;
}

void llo_global_fini(void)
{
	lu_context_key_degister(&llod_key);
	LASSERT(list_empty(&llo_lobj_list));
}

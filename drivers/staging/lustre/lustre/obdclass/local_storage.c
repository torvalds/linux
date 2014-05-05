/*
 * GPL HEADER START
 *
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 only,
 * as published by the Free Software Foundation.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License version 2 for more details.  A copy is
 * included in the COPYING file that accompanied this code.

 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 * GPL HEADER END
 */
/*
 * Copyright (c) 2012, Intel Corporation.
 */
/*
 * lustre/obdclass/local_storage.c
 *
 * Local storage for file/objects with fid generation. Works on top of OSD.
 *
 * Author: Mikhail Pershin <mike.pershin@intel.com>
 */

#define DEBUG_SUBSYSTEM S_CLASS

#include "local_storage.h"

/* all initialized local storages on this node are linked on this */
static LIST_HEAD(ls_list_head);
static DEFINE_MUTEX(ls_list_mutex);

static int ls_object_init(const struct lu_env *env, struct lu_object *o,
			  const struct lu_object_conf *unused)
{
	struct ls_device	*ls;
	struct lu_object	*below;
	struct lu_device	*under;

	ls = container_of0(o->lo_dev, struct ls_device, ls_top_dev.dd_lu_dev);
	under = &ls->ls_osd->dd_lu_dev;
	below = under->ld_ops->ldo_object_alloc(env, o->lo_header, under);
	if (below == NULL)
		return -ENOMEM;

	lu_object_add(o, below);

	return 0;
}

static void ls_object_free(const struct lu_env *env, struct lu_object *o)
{
	struct ls_object	*obj = lu2ls_obj(o);
	struct lu_object_header	*h = o->lo_header;

	dt_object_fini(&obj->ls_obj);
	lu_object_header_fini(h);
	OBD_FREE_PTR(obj);
}

struct lu_object_operations ls_lu_obj_ops = {
	.loo_object_init  = ls_object_init,
	.loo_object_free  = ls_object_free,
};

struct lu_object *ls_object_alloc(const struct lu_env *env,
				  const struct lu_object_header *_h,
				  struct lu_device *d)
{
	struct lu_object_header	*h;
	struct ls_object	*o;
	struct lu_object	*l;

	LASSERT(_h == NULL);

	OBD_ALLOC_PTR(o);
	if (o != NULL) {
		l = &o->ls_obj.do_lu;
		h = &o->ls_header;

		lu_object_header_init(h);
		dt_object_init(&o->ls_obj, h, d);
		lu_object_add_top(h, l);

		l->lo_ops = &ls_lu_obj_ops;

		return l;
	} else {
		return NULL;
	}
}

static struct lu_device_operations ls_lu_dev_ops = {
	.ldo_object_alloc =	ls_object_alloc
};

static struct ls_device *__ls_find_dev(struct dt_device *dev)
{
	struct ls_device *ls, *ret = NULL;

	list_for_each_entry(ls, &ls_list_head, ls_linkage) {
		if (ls->ls_osd == dev) {
			atomic_inc(&ls->ls_refcount);
			ret = ls;
			break;
		}
	}
	return ret;
}

struct ls_device *ls_find_dev(struct dt_device *dev)
{
	struct ls_device *ls;

	mutex_lock(&ls_list_mutex);
	ls = __ls_find_dev(dev);
	mutex_unlock(&ls_list_mutex);

	return ls;
}

static struct lu_device_type_operations ls_device_type_ops = {
	.ldto_start = NULL,
	.ldto_stop  = NULL,
};

static struct lu_device_type ls_lu_type = {
	.ldt_name = "local_storage",
	.ldt_ops  = &ls_device_type_ops,
};

struct ls_device *ls_device_get(struct dt_device *dev)
{
	struct ls_device *ls;

	mutex_lock(&ls_list_mutex);
	ls = __ls_find_dev(dev);
	if (ls)
		GOTO(out_ls, ls);

	/* not found, then create */
	OBD_ALLOC_PTR(ls);
	if (ls == NULL)
		GOTO(out_ls, ls = ERR_PTR(-ENOMEM));

	atomic_set(&ls->ls_refcount, 1);
	INIT_LIST_HEAD(&ls->ls_los_list);
	mutex_init(&ls->ls_los_mutex);

	ls->ls_osd = dev;

	LASSERT(dev->dd_lu_dev.ld_site);
	lu_device_init(&ls->ls_top_dev.dd_lu_dev, &ls_lu_type);
	ls->ls_top_dev.dd_lu_dev.ld_ops = &ls_lu_dev_ops;
	ls->ls_top_dev.dd_lu_dev.ld_site = dev->dd_lu_dev.ld_site;

	/* finally add ls to the list */
	list_add(&ls->ls_linkage, &ls_list_head);
out_ls:
	mutex_unlock(&ls_list_mutex);
	return ls;
}

void ls_device_put(const struct lu_env *env, struct ls_device *ls)
{
	LASSERT(env);
	if (!atomic_dec_and_test(&ls->ls_refcount))
		return;

	mutex_lock(&ls_list_mutex);
	if (atomic_read(&ls->ls_refcount) == 0) {
		LASSERT(list_empty(&ls->ls_los_list));
		list_del(&ls->ls_linkage);
		lu_site_purge(env, ls->ls_top_dev.dd_lu_dev.ld_site, ~0);
		lu_device_fini(&ls->ls_top_dev.dd_lu_dev);
		OBD_FREE_PTR(ls);
	}
	mutex_unlock(&ls_list_mutex);
}

/**
 * local file fid generation
 */
int local_object_fid_generate(const struct lu_env *env,
			      struct local_oid_storage *los,
			      struct lu_fid *fid)
{
	LASSERT(los->los_dev);
	LASSERT(los->los_obj);

	/* take next OID */

	/* to make it unique after reboot we store
	 * the latest generated fid atomically with
	 * object creation see local_object_create() */

	mutex_lock(&los->los_id_lock);
	fid->f_seq = los->los_seq;
	fid->f_oid = ++los->los_last_oid;
	fid->f_ver = 0;
	mutex_unlock(&los->los_id_lock);

	return 0;
}

int local_object_declare_create(const struct lu_env *env,
				struct local_oid_storage *los,
				struct dt_object *o, struct lu_attr *attr,
				struct dt_object_format *dof,
				struct thandle *th)
{
	struct dt_thread_info	*dti = dt_info(env);
	int			 rc;

	/* update fid generation file */
	if (los != NULL) {
		LASSERT(dt_object_exists(los->los_obj));
		rc = dt_declare_record_write(env, los->los_obj,
					     sizeof(struct los_ondisk), 0, th);
		if (rc)
			return rc;
	}

	rc = dt_declare_create(env, o, attr, NULL, dof, th);
	if (rc)
		return rc;

	dti->dti_lb.lb_buf = NULL;
	dti->dti_lb.lb_len = sizeof(dti->dti_lma);
	rc = dt_declare_xattr_set(env, o, &dti->dti_lb, XATTR_NAME_LMA, 0, th);

	return rc;
}

int local_object_create(const struct lu_env *env,
			struct local_oid_storage *los,
			struct dt_object *o, struct lu_attr *attr,
			struct dt_object_format *dof, struct thandle *th)
{
	struct dt_thread_info	*dti = dt_info(env);
	__le64			 lastid;
	int			 rc;

	rc = dt_create(env, o, attr, NULL, dof, th);
	if (rc)
		return rc;

	if (los == NULL)
		return rc;

	LASSERT(los->los_obj);
	LASSERT(dt_object_exists(los->los_obj));

	/* many threads can be updated this, serialize
	 * them here to avoid the race where one thread
	 * takes the value first, but writes it last */
	mutex_lock(&los->los_id_lock);

	/* update local oid number on disk so that
	 * we know the last one used after reboot */
	lastid = cpu_to_le64(los->los_last_oid);

	dti->dti_off = 0;
	dti->dti_lb.lb_buf = &lastid;
	dti->dti_lb.lb_len = sizeof(lastid);
	rc = dt_record_write(env, los->los_obj, &dti->dti_lb, &dti->dti_off,
			     th);
	mutex_unlock(&los->los_id_lock);

	return rc;
}

/*
 * Create local named object (file, directory or index) in parent directory.
 */
struct dt_object *__local_file_create(const struct lu_env *env,
				      const struct lu_fid *fid,
				      struct local_oid_storage *los,
				      struct ls_device *ls,
				      struct dt_object *parent,
				      const char *name, struct lu_attr *attr,
				      struct dt_object_format *dof)
{
	struct dt_thread_info	*dti = dt_info(env);
	struct dt_object	*dto;
	struct thandle		*th;
	int			 rc;

	dto = ls_locate(env, ls, fid);
	if (unlikely(IS_ERR(dto)))
		return dto;

	LASSERT(dto != NULL);
	if (dt_object_exists(dto))
		GOTO(out, rc = -EEXIST);

	th = dt_trans_create(env, ls->ls_osd);
	if (IS_ERR(th))
		GOTO(out, rc = PTR_ERR(th));

	rc = local_object_declare_create(env, los, dto, attr, dof, th);
	if (rc)
		GOTO(trans_stop, rc);

	if (dti->dti_dof.dof_type == DFT_DIR) {
		dt_declare_ref_add(env, dto, th);
		dt_declare_ref_add(env, parent, th);
	}

	rc = dt_declare_insert(env, parent, (void *)fid, (void *)name, th);
	if (rc)
		GOTO(trans_stop, rc);

	rc = dt_trans_start_local(env, ls->ls_osd, th);
	if (rc)
		GOTO(trans_stop, rc);

	dt_write_lock(env, dto, 0);
	if (dt_object_exists(dto))
		GOTO(unlock, rc = 0);

	CDEBUG(D_OTHER, "create new object "DFID"\n",
	       PFID(lu_object_fid(&dto->do_lu)));
	rc = local_object_create(env, los, dto, attr, dof, th);
	if (rc)
		GOTO(unlock, rc);
	LASSERT(dt_object_exists(dto));

	if (dti->dti_dof.dof_type == DFT_DIR) {
		if (!dt_try_as_dir(env, dto))
			GOTO(destroy, rc = -ENOTDIR);
		/* Add "." and ".." for newly created dir */
		rc = dt_insert(env, dto, (void *)fid, (void *)".", th,
			       BYPASS_CAPA, 1);
		if (rc)
			GOTO(destroy, rc);
		dt_ref_add(env, dto, th);
		rc = dt_insert(env, dto, (void *)lu_object_fid(&parent->do_lu),
			       (void *)"..", th, BYPASS_CAPA, 1);
		if (rc)
			GOTO(destroy, rc);
	}

	dt_write_lock(env, parent, 0);
	rc = dt_insert(env, parent, (const struct dt_rec *)fid,
		       (const struct dt_key *)name, th, BYPASS_CAPA, 1);
	if (dti->dti_dof.dof_type == DFT_DIR)
		dt_ref_add(env, parent, th);
	dt_write_unlock(env, parent);
	if (rc)
		GOTO(destroy, rc);
destroy:
	if (rc)
		dt_destroy(env, dto, th);
unlock:
	dt_write_unlock(env, dto);
trans_stop:
	dt_trans_stop(env, ls->ls_osd, th);
out:
	if (rc) {
		lu_object_put_nocache(env, &dto->do_lu);
		dto = ERR_PTR(rc);
	}
	return dto;
}

/*
 * Look up and create (if it does not exist) a local named file or directory in
 * parent directory.
 */
struct dt_object *local_file_find_or_create(const struct lu_env *env,
					    struct local_oid_storage *los,
					    struct dt_object *parent,
					    const char *name, __u32 mode)
{
	struct dt_thread_info	*dti = dt_info(env);
	struct dt_object	*dto;
	int			 rc;

	LASSERT(parent);

	rc = dt_lookup_dir(env, parent, name, &dti->dti_fid);
	if (rc == 0)
		/* name is found, get the object */
		dto = ls_locate(env, dt2ls_dev(los->los_dev), &dti->dti_fid);
	else if (rc != -ENOENT)
		dto = ERR_PTR(rc);
	else {
		rc = local_object_fid_generate(env, los, &dti->dti_fid);
		if (rc < 0) {
			dto = ERR_PTR(rc);
		} else {
			/* create the object */
			dti->dti_attr.la_valid	= LA_MODE;
			dti->dti_attr.la_mode	= mode;
			dti->dti_dof.dof_type	= dt_mode_to_dft(mode & S_IFMT);
			dto = __local_file_create(env, &dti->dti_fid, los,
						  dt2ls_dev(los->los_dev),
						  parent, name, &dti->dti_attr,
						  &dti->dti_dof);
		}
	}
	return dto;
}
EXPORT_SYMBOL(local_file_find_or_create);

struct dt_object *local_file_find_or_create_with_fid(const struct lu_env *env,
						     struct dt_device *dt,
						     const struct lu_fid *fid,
						     struct dt_object *parent,
						     const char *name,
						     __u32 mode)
{
	struct dt_thread_info	*dti = dt_info(env);
	struct dt_object	*dto;
	int			 rc;

	LASSERT(parent);

	rc = dt_lookup_dir(env, parent, name, &dti->dti_fid);
	if (rc == 0) {
		dto = dt_locate(env, dt, &dti->dti_fid);
	} else if (rc != -ENOENT) {
		dto = ERR_PTR(rc);
	} else {
		struct ls_device *ls;

		ls = ls_device_get(dt);
		if (IS_ERR(ls)) {
			dto = ERR_CAST(ls);
		} else {
			/* create the object */
			dti->dti_attr.la_valid	= LA_MODE;
			dti->dti_attr.la_mode	= mode;
			dti->dti_dof.dof_type	= dt_mode_to_dft(mode & S_IFMT);
			dto = __local_file_create(env, fid, NULL, ls, parent,
						  name, &dti->dti_attr,
						  &dti->dti_dof);
			/* ls_device_put() will finalize the ls device, we
			 * have to open the object in other device stack */
			if (!IS_ERR(dto)) {
				dti->dti_fid = dto->do_lu.lo_header->loh_fid;
				lu_object_put_nocache(env, &dto->do_lu);
				dto = dt_locate(env, dt, &dti->dti_fid);
			}
			ls_device_put(env, ls);
		}
	}
	return dto;
}
EXPORT_SYMBOL(local_file_find_or_create_with_fid);

/*
 * Look up and create (if it does not exist) a local named index file in parent
 * directory.
 */
struct dt_object *local_index_find_or_create(const struct lu_env *env,
					     struct local_oid_storage *los,
					     struct dt_object *parent,
					     const char *name, __u32 mode,
					     const struct dt_index_features *ft)
{
	struct dt_thread_info	*dti = dt_info(env);
	struct dt_object	*dto;
	int			 rc;

	LASSERT(parent);

	rc = dt_lookup_dir(env, parent, name, &dti->dti_fid);
	if (rc == 0) {
		/* name is found, get the object */
		dto = ls_locate(env, dt2ls_dev(los->los_dev), &dti->dti_fid);
	} else if (rc != -ENOENT) {
		dto = ERR_PTR(rc);
	} else {
		rc = local_object_fid_generate(env, los, &dti->dti_fid);
		if (rc < 0) {
			dto = ERR_PTR(rc);
		} else {
			/* create the object */
			dti->dti_attr.la_valid		= LA_MODE;
			dti->dti_attr.la_mode		= mode;
			dti->dti_dof.dof_type		= DFT_INDEX;
			dti->dti_dof.u.dof_idx.di_feat	= ft;
			dto = __local_file_create(env, &dti->dti_fid, los,
						  dt2ls_dev(los->los_dev),
						  parent, name, &dti->dti_attr,
						  &dti->dti_dof);
		}
	}
	return dto;

}
EXPORT_SYMBOL(local_index_find_or_create);

struct dt_object *
local_index_find_or_create_with_fid(const struct lu_env *env,
				    struct dt_device *dt,
				    const struct lu_fid *fid,
				    struct dt_object *parent,
				    const char *name, __u32 mode,
				    const struct dt_index_features *ft)
{
	struct dt_thread_info	*dti = dt_info(env);
	struct dt_object	*dto;
	int			 rc;

	LASSERT(parent);

	rc = dt_lookup_dir(env, parent, name, &dti->dti_fid);
	if (rc == 0) {
		/* name is found, get the object */
		if (!lu_fid_eq(fid, &dti->dti_fid))
			dto = ERR_PTR(-EINVAL);
		else
			dto = dt_locate(env, dt, fid);
	} else if (rc != -ENOENT) {
		dto = ERR_PTR(rc);
	} else {
		struct ls_device *ls;

		ls = ls_device_get(dt);
		if (IS_ERR(ls)) {
			dto = ERR_CAST(ls);
		} else {
			/* create the object */
			dti->dti_attr.la_valid		= LA_MODE;
			dti->dti_attr.la_mode		= mode;
			dti->dti_dof.dof_type		= DFT_INDEX;
			dti->dti_dof.u.dof_idx.di_feat  = ft;
			dto = __local_file_create(env, fid, NULL, ls, parent,
						  name, &dti->dti_attr,
						  &dti->dti_dof);
			/* ls_device_put() will finalize the ls device, we
			 * have to open the object in other device stack */
			if (!IS_ERR(dto)) {
				dti->dti_fid = dto->do_lu.lo_header->loh_fid;
				lu_object_put_nocache(env, &dto->do_lu);
				dto = dt_locate(env, dt, &dti->dti_fid);
			}
			ls_device_put(env, ls);
		}
	}
	return dto;
}
EXPORT_SYMBOL(local_index_find_or_create_with_fid);

static int local_object_declare_unlink(const struct lu_env *env,
				       struct dt_device *dt,
				       struct dt_object *p,
				       struct dt_object *c, const char *name,
				       struct thandle *th)
{
	int rc;

	rc = dt_declare_delete(env, p, (const struct dt_key *)name, th);
	if (rc < 0)
		return rc;

	rc = dt_declare_ref_del(env, c, th);
	if (rc < 0)
		return rc;

	return dt_declare_destroy(env, c, th);
}

int local_object_unlink(const struct lu_env *env, struct dt_device *dt,
			struct dt_object *parent, const char *name)
{
	struct dt_thread_info	*dti = dt_info(env);
	struct dt_object	*dto;
	struct thandle		*th;
	int			 rc;

	rc = dt_lookup_dir(env, parent, name, &dti->dti_fid);
	if (rc == -ENOENT)
		return 0;
	else if (rc < 0)
		return rc;

	dto = dt_locate(env, dt, &dti->dti_fid);
	if (unlikely(IS_ERR(dto)))
		return PTR_ERR(dto);

	th = dt_trans_create(env, dt);
	if (IS_ERR(th))
		GOTO(out, rc = PTR_ERR(th));

	rc = local_object_declare_unlink(env, dt, parent, dto, name, th);
	if (rc < 0)
		GOTO(stop, rc);

	rc = dt_trans_start_local(env, dt, th);
	if (rc < 0)
		GOTO(stop, rc);

	dt_write_lock(env, dto, 0);
	rc = dt_delete(env, parent, (struct dt_key *)name, th, BYPASS_CAPA);
	if (rc < 0)
		GOTO(unlock, rc);

	rc = dt_ref_del(env, dto, th);
	if (rc < 0) {
		rc = dt_insert(env, parent,
			       (const struct dt_rec *)&dti->dti_fid,
			       (const struct dt_key *)name, th, BYPASS_CAPA, 1);
		GOTO(unlock, rc);
	}

	rc = dt_destroy(env, dto, th);
unlock:
	dt_write_unlock(env, dto);
stop:
	dt_trans_stop(env, dt, th);
out:
	lu_object_put_nocache(env, &dto->do_lu);
	return rc;
}
EXPORT_SYMBOL(local_object_unlink);

struct local_oid_storage *dt_los_find(struct ls_device *ls, __u64 seq)
{
	struct local_oid_storage *los, *ret = NULL;

	list_for_each_entry(los, &ls->ls_los_list, los_list) {
		if (los->los_seq == seq) {
			atomic_inc(&los->los_refcount);
			ret = los;
			break;
		}
	}
	return ret;
}

void dt_los_put(struct local_oid_storage *los)
{
	if (atomic_dec_and_test(&los->los_refcount))
		/* should never happen, only local_oid_storage_fini should
		 * drop refcount to zero */
		LBUG();
	return;
}

/* after Lustre 2.3 release there may be old file to store last generated FID
 * If such file exists then we have to read its content
 */
int lastid_compat_check(const struct lu_env *env, struct dt_device *dev,
			__u64 lastid_seq, __u32 *first_oid, struct ls_device *ls)
{
	struct dt_thread_info	*dti = dt_info(env);
	struct dt_object	*root = NULL;
	struct los_ondisk	 losd;
	struct dt_object	*o = NULL;
	int			 rc = 0;

	rc = dt_root_get(env, dev, &dti->dti_fid);
	if (rc)
		return rc;

	root = ls_locate(env, ls, &dti->dti_fid);
	if (IS_ERR(root))
		return PTR_ERR(root);

	/* find old last_id file */
	snprintf(dti->dti_buf, sizeof(dti->dti_buf), "seq-"LPX64"-lastid",
		 lastid_seq);
	rc = dt_lookup_dir(env, root, dti->dti_buf, &dti->dti_fid);
	lu_object_put_nocache(env, &root->do_lu);
	if (rc == -ENOENT) {
		/* old llog lastid accessed by FID only */
		if (lastid_seq != FID_SEQ_LLOG)
			return 0;
		dti->dti_fid.f_seq = FID_SEQ_LLOG;
		dti->dti_fid.f_oid = 1;
		dti->dti_fid.f_ver = 0;
		o = ls_locate(env, ls, &dti->dti_fid);
		if (IS_ERR(o))
			return PTR_ERR(o);

		if (!dt_object_exists(o)) {
			lu_object_put_nocache(env, &o->do_lu);
			return 0;
		}
		CDEBUG(D_INFO, "Found old llog lastid file\n");
	} else if (rc < 0) {
		return rc;
	} else {
		CDEBUG(D_INFO, "Found old lastid file for sequence "LPX64"\n",
		       lastid_seq);
		o = ls_locate(env, ls, &dti->dti_fid);
		if (IS_ERR(o))
			return PTR_ERR(o);
	}
	/* let's read seq-NNNNNN-lastid file value */
	LASSERT(dt_object_exists(o));
	dti->dti_off = 0;
	dti->dti_lb.lb_buf = &losd;
	dti->dti_lb.lb_len = sizeof(losd);
	dt_read_lock(env, o, 0);
	rc = dt_record_read(env, o, &dti->dti_lb, &dti->dti_off);
	dt_read_unlock(env, o);
	lu_object_put_nocache(env, &o->do_lu);
	if (rc == 0 && le32_to_cpu(losd.lso_magic) != LOS_MAGIC) {
		CERROR("%s: wrong content of seq-"LPX64"-lastid file, magic %x\n",
		       o->do_lu.lo_dev->ld_obd->obd_name, lastid_seq,
		       le32_to_cpu(losd.lso_magic));
		return -EINVAL;
	} else if (rc < 0) {
		CERROR("%s: failed to read seq-"LPX64"-lastid: rc = %d\n",
		       o->do_lu.lo_dev->ld_obd->obd_name, lastid_seq, rc);
		return rc;
	}
	*first_oid = le32_to_cpu(losd.lso_next_oid);
	return rc;
}

/**
 * Initialize local OID storage for required sequence.
 * That may be needed for services that uses local files and requires
 * dynamic OID allocation for them.
 *
 * Per each sequence we have an object with 'first_fid' identificator
 * containing the counter for OIDs of locally created files with that
 * sequence.
 *
 * It is used now by llog subsystem and MGS for NID tables
 *
 * Function gets first_fid to create counter object.
 * All dynamic fids will be generated with the same sequence and incremented
 * OIDs
 *
 * Returned local_oid_storage is in-memory representation of OID storage
 */
int local_oid_storage_init(const struct lu_env *env, struct dt_device *dev,
			   const struct lu_fid *first_fid,
			   struct local_oid_storage **los)
{
	struct dt_thread_info	*dti = dt_info(env);
	struct ls_device	*ls;
	obd_id			 lastid;
	struct dt_object	*o = NULL;
	struct thandle		*th;
	__u32			 first_oid = fid_oid(first_fid);
	int			 rc = 0;

	ls = ls_device_get(dev);
	if (IS_ERR(ls))
		return PTR_ERR(ls);

	mutex_lock(&ls->ls_los_mutex);
	*los = dt_los_find(ls, fid_seq(first_fid));
	if (*los != NULL)
		GOTO(out, rc = 0);

	/* not found, then create */
	OBD_ALLOC_PTR(*los);
	if (*los == NULL)
		GOTO(out, rc = -ENOMEM);

	atomic_set(&(*los)->los_refcount, 1);
	mutex_init(&(*los)->los_id_lock);
	(*los)->los_dev = &ls->ls_top_dev;
	atomic_inc(&ls->ls_refcount);
	list_add(&(*los)->los_list, &ls->ls_los_list);

	/* Use {seq, 0, 0} to create the LAST_ID file for every
	 * sequence.  OIDs start at LUSTRE_FID_INIT_OID.
	 */
	dti->dti_fid.f_seq = fid_seq(first_fid);
	dti->dti_fid.f_oid = LUSTRE_FID_LASTID_OID;
	dti->dti_fid.f_ver = 0;
	o = ls_locate(env, ls, &dti->dti_fid);
	if (IS_ERR(o))
		GOTO(out_los, rc = PTR_ERR(o));

	if (!dt_object_exists(o)) {
		rc = lastid_compat_check(env, dev, fid_seq(first_fid),
					 &first_oid, ls);
		if (rc < 0)
			GOTO(out_los, rc);

		th = dt_trans_create(env, dev);
		if (IS_ERR(th))
			GOTO(out_los, rc = PTR_ERR(th));

		dti->dti_attr.la_valid = LA_MODE | LA_TYPE;
		dti->dti_attr.la_mode = S_IFREG | S_IRUGO | S_IWUSR;
		dti->dti_dof.dof_type = dt_mode_to_dft(S_IFREG);

		rc = dt_declare_create(env, o, &dti->dti_attr, NULL,
				       &dti->dti_dof, th);
		if (rc)
			GOTO(out_trans, rc);

		rc = dt_declare_record_write(env, o, sizeof(lastid), 0, th);
		if (rc)
			GOTO(out_trans, rc);

		rc = dt_trans_start_local(env, dev, th);
		if (rc)
			GOTO(out_trans, rc);

		dt_write_lock(env, o, 0);
		if (dt_object_exists(o))
			GOTO(out_lock, rc = 0);

		rc = dt_create(env, o, &dti->dti_attr, NULL, &dti->dti_dof,
			       th);
		if (rc)
			GOTO(out_lock, rc);

		lastid = cpu_to_le64(first_oid);

		dti->dti_off = 0;
		dti->dti_lb.lb_buf = &lastid;
		dti->dti_lb.lb_len = sizeof(lastid);
		rc = dt_record_write(env, o, &dti->dti_lb, &dti->dti_off, th);
		if (rc)
			GOTO(out_lock, rc);
out_lock:
		dt_write_unlock(env, o);
out_trans:
		dt_trans_stop(env, dev, th);
	} else {
		dti->dti_off = 0;
		dti->dti_lb.lb_buf = &lastid;
		dti->dti_lb.lb_len = sizeof(lastid);
		dt_read_lock(env, o, 0);
		rc = dt_record_read(env, o, &dti->dti_lb, &dti->dti_off);
		dt_read_unlock(env, o);
		if (rc == 0 && le64_to_cpu(lastid) > OBIF_MAX_OID) {
			CERROR("%s: bad oid "LPU64" is read from LAST_ID\n",
			       o->do_lu.lo_dev->ld_obd->obd_name,
			       le64_to_cpu(lastid));
			rc = -EINVAL;
		}
	}
out_los:
	if (rc != 0) {
		list_del(&(*los)->los_list);
		atomic_dec(&ls->ls_refcount);
		OBD_FREE_PTR(*los);
		*los = NULL;
		if (o != NULL && !IS_ERR(o))
			lu_object_put_nocache(env, &o->do_lu);
	} else {
		(*los)->los_seq = fid_seq(first_fid);
		(*los)->los_last_oid = le64_to_cpu(lastid);
		(*los)->los_obj = o;
		/* Read value should not be less than initial one
		 * but possible after upgrade from older fs.
		 * In this case just switch to the first_oid in memory and
		 * it will be updated on disk with first object generated */
		if ((*los)->los_last_oid < first_oid)
			(*los)->los_last_oid = first_oid;
	}
out:
	mutex_unlock(&ls->ls_los_mutex);
	ls_device_put(env, ls);
	return rc;
}
EXPORT_SYMBOL(local_oid_storage_init);

void local_oid_storage_fini(const struct lu_env *env,
			    struct local_oid_storage *los)
{
	struct ls_device *ls;

	if (!atomic_dec_and_test(&los->los_refcount))
		return;

	LASSERT(env);
	LASSERT(los->los_dev);
	ls = dt2ls_dev(los->los_dev);

	mutex_lock(&ls->ls_los_mutex);
	if (atomic_read(&los->los_refcount) == 0) {
		if (los->los_obj)
			lu_object_put_nocache(env, &los->los_obj->do_lu);
		list_del(&los->los_list);
		OBD_FREE_PTR(los);
	}
	mutex_unlock(&ls->ls_los_mutex);
	ls_device_put(env, ls);
}
EXPORT_SYMBOL(local_oid_storage_fini);

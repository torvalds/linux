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
 * Copyright (c) 2007, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright (c) 2011, 2012, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 *
 * lustre/include/md_object.h
 *
 * Extention of lu_object.h for metadata objects
 */

#ifndef _LUSTRE_MD_OBJECT_H
#define _LUSTRE_MD_OBJECT_H

/** \defgroup md md
 * Sub-class of lu_object with methods common for "meta-data" objects in MDT
 * stack.
 *
 * Meta-data objects implement namespace operations: you can link, unlink
 * them, and treat them as directories.
 *
 * Examples: mdt, cmm, and mdt are implementations of md interface.
 * @{
 */


/*
 * super-class definitions.
 */
#include <dt_object.h>

struct md_device;
struct md_device_operations;
struct md_object;
struct obd_export;

enum {
	UCRED_INVALID   = -1,
	UCRED_INIT      = 0,
	UCRED_OLD       = 1,
	UCRED_NEW       = 2
};

enum {
	MD_CAPAINFO_MAX = 5
};

/** there are at most 5 fids in one operation, see rename, NOTE the last one
 * is a temporary one used for is_subdir() */
struct md_capainfo {
	__u32		   mc_auth;
	__u32		   mc_padding;
	struct lu_fid	   mc_fid[MD_CAPAINFO_MAX];
	struct lustre_capa     *mc_capa[MD_CAPAINFO_MAX];
};

struct md_quota {
	struct obd_export       *mq_exp;
};

/**
 * Implemented in mdd/mdd_handler.c.
 *
 * XXX should be moved into separate .h/.c together with all md security
 * related definitions.
 */
struct md_capainfo *md_capainfo(const struct lu_env *env);
struct md_quota *md_quota(const struct lu_env *env);

/** metadata attributes */
enum ma_valid {
	MA_INODE     = (1 << 0),
	MA_LOV       = (1 << 1),
	MA_COOKIE    = (1 << 2),
	MA_FLAGS     = (1 << 3),
	MA_LMV       = (1 << 4),
	MA_ACL_DEF   = (1 << 5),
	MA_LOV_DEF   = (1 << 6),
	MA_LAY_GEN   = (1 << 7),
	MA_HSM       = (1 << 8),
	MA_SOM       = (1 << 9),
	MA_PFID      = (1 << 10)
};

typedef enum {
	MDL_MINMODE  = 0,
	MDL_EX       = 1,
	MDL_PW       = 2,
	MDL_PR       = 4,
	MDL_CW       = 8,
	MDL_CR       = 16,
	MDL_NL       = 32,
	MDL_GROUP    = 64,
	MDL_MAXMODE
} mdl_mode_t;

typedef enum {
	MDT_NUL_LOCK = 0,
	MDT_REG_LOCK = (1 << 0),
	MDT_PDO_LOCK = (1 << 1)
} mdl_type_t;

/* memory structure for hsm attributes
 * for fields description see the on disk structure hsm_attrs
 * which is defined in lustre_idl.h
 */
struct md_hsm {
	__u32	mh_compat;
	__u32	mh_flags;
	__u64	mh_arch_id;
	__u64	mh_arch_ver;
};

#define IOEPOCH_INVAL 0

/* memory structure for som attributes
 * for fields description see the on disk structure som_attrs
 * which is defined in lustre_idl.h
 */
struct md_som_data {
	__u32	msd_compat;
	__u32	msd_incompat;
	__u64	msd_ioepoch;
	__u64	msd_size;
	__u64	msd_blocks;
	__u64	msd_mountid;
};

struct md_attr {
	__u64		   ma_valid;
	__u64		   ma_need;
	__u64		   ma_attr_flags;
	struct lu_attr	  ma_attr;
	struct lu_fid	   ma_pfid;
	struct md_hsm	   ma_hsm;
	struct lov_mds_md      *ma_lmm;
	struct lmv_stripe_md   *ma_lmv;
	void		   *ma_acl;
	struct llog_cookie     *ma_cookie;
	struct lustre_capa     *ma_capa;
	struct md_som_data     *ma_som;
	int		     ma_lmm_size;
	int		     ma_lmv_size;
	int		     ma_acl_size;
	int		     ma_cookie_size;
	__u16		   ma_layout_gen;
};

/** Additional parameters for create */
struct md_op_spec {
	union {
		/** symlink target */
		const char	       *sp_symname;
		/** parent FID for cross-ref mkdir */
		const struct lu_fid      *sp_pfid;
		/** eadata for regular files */
		struct md_spec_reg {
			/** lov objs exist already */
			const struct lu_fid   *fid;
			const void *eadata;
			int  eadatalen;
		} sp_ea;
	} u;

	/** Create flag from client: such as MDS_OPEN_CREAT, and others. */
	__u64      sp_cr_flags;

	/** don't create lov objects or llog cookie - this replay */
	unsigned int no_create:1,
		     sp_cr_lookup:1, /* do lookup sanity check or not. */
		     sp_rm_entry:1;  /* only remove name entry */

	/** Current lock mode for parent dir where create is performing. */
	mdl_mode_t sp_cr_mode;

	/** to create directory */
	const struct dt_index_features *sp_feat;
};

/**
 * Operations implemented for each md object (both directory and leaf).
 */
struct md_object_operations {
	int (*moo_permission)(const struct lu_env *env,
			      struct md_object *pobj, struct md_object *cobj,
			      struct md_attr *attr, int mask);

	int (*moo_attr_get)(const struct lu_env *env, struct md_object *obj,
			    struct md_attr *attr);

	int (*moo_attr_set)(const struct lu_env *env, struct md_object *obj,
			    const struct md_attr *attr);

	int (*moo_xattr_get)(const struct lu_env *env, struct md_object *obj,
			     struct lu_buf *buf, const char *name);

	int (*moo_xattr_list)(const struct lu_env *env, struct md_object *obj,
			      struct lu_buf *buf);

	int (*moo_xattr_set)(const struct lu_env *env, struct md_object *obj,
			     const struct lu_buf *buf, const char *name,
			     int fl);

	int (*moo_xattr_del)(const struct lu_env *env, struct md_object *obj,
			     const char *name);

	/** This method is used to swap the layouts between 2 objects */
	int (*moo_swap_layouts)(const struct lu_env *env,
			       struct md_object *obj1, struct md_object *obj2,
			       __u64 flags);

	/** \retval number of bytes actually read upon success */
	int (*moo_readpage)(const struct lu_env *env, struct md_object *obj,
			    const struct lu_rdpg *rdpg);

	int (*moo_readlink)(const struct lu_env *env, struct md_object *obj,
			    struct lu_buf *buf);
	int (*moo_changelog)(const struct lu_env *env,
			     enum changelog_rec_type type, int flags,
			     struct md_object *obj);
	/** part of cross-ref operation */
	int (*moo_object_create)(const struct lu_env *env,
				 struct md_object *obj,
				 const struct md_op_spec *spec,
				 struct md_attr *ma);

	int (*moo_ref_add)(const struct lu_env *env,
			   struct md_object *obj,
			   const struct md_attr *ma);

	int (*moo_ref_del)(const struct lu_env *env,
			   struct md_object *obj,
			   struct md_attr *ma);

	int (*moo_open)(const struct lu_env *env,
			struct md_object *obj, int flag);

	int (*moo_close)(const struct lu_env *env, struct md_object *obj,
			 struct md_attr *ma, int mode);

	int (*moo_capa_get)(const struct lu_env *, struct md_object *,
			    struct lustre_capa *, int renewal);

	int (*moo_object_sync)(const struct lu_env *, struct md_object *);

	int (*moo_file_lock)(const struct lu_env *env, struct md_object *obj,
			     struct lov_mds_md *lmm, struct ldlm_extent *extent,
			     struct lustre_handle *lockh);
	int (*moo_file_unlock)(const struct lu_env *env, struct md_object *obj,
			       struct lov_mds_md *lmm,
			       struct lustre_handle *lockh);
	int (*moo_object_lock)(const struct lu_env *env, struct md_object *obj,
			       struct lustre_handle *lh,
			       struct ldlm_enqueue_info *einfo,
			       void *policy);
};

/**
 * Operations implemented for each directory object.
 */
struct md_dir_operations {
	int (*mdo_is_subdir) (const struct lu_env *env, struct md_object *obj,
			      const struct lu_fid *fid, struct lu_fid *sfid);

	int (*mdo_lookup)(const struct lu_env *env, struct md_object *obj,
			  const struct lu_name *lname, struct lu_fid *fid,
			  struct md_op_spec *spec);

	mdl_mode_t (*mdo_lock_mode)(const struct lu_env *env,
				    struct md_object *obj,
				    mdl_mode_t mode);

	int (*mdo_create)(const struct lu_env *env, struct md_object *pobj,
			  const struct lu_name *lname, struct md_object *child,
			  struct md_op_spec *spec,
			  struct md_attr *ma);

	/** This method is used for creating data object for this meta object*/
	int (*mdo_create_data)(const struct lu_env *env, struct md_object *p,
			       struct md_object *o,
			       const struct md_op_spec *spec,
			       struct md_attr *ma);

	int (*mdo_rename)(const struct lu_env *env, struct md_object *spobj,
			  struct md_object *tpobj, const struct lu_fid *lf,
			  const struct lu_name *lsname, struct md_object *tobj,
			  const struct lu_name *ltname, struct md_attr *ma);

	int (*mdo_link)(const struct lu_env *env, struct md_object *tgt_obj,
			struct md_object *src_obj, const struct lu_name *lname,
			struct md_attr *ma);

	int (*mdo_unlink)(const struct lu_env *env, struct md_object *pobj,
			  struct md_object *cobj, const struct lu_name *lname,
			  struct md_attr *ma, int no_name);

	/** This method is used to compare a requested layout to an existing
	 * layout (struct lov_mds_md_v1/3 vs struct lov_mds_md_v1/3) */
	int (*mdo_lum_lmm_cmp)(const struct lu_env *env,
			       struct md_object *cobj,
			       const struct md_op_spec *spec,
			       struct md_attr *ma);

	/** partial ops for cross-ref case */
	int (*mdo_name_insert)(const struct lu_env *env,
			       struct md_object *obj,
			       const struct lu_name *lname,
			       const struct lu_fid *fid,
			       const struct md_attr *ma);

	int (*mdo_name_remove)(const struct lu_env *env,
			       struct md_object *obj,
			       const struct lu_name *lname,
			       const struct md_attr *ma);

	int (*mdo_rename_tgt)(const struct lu_env *env, struct md_object *pobj,
			      struct md_object *tobj, const struct lu_fid *fid,
			      const struct lu_name *lname, struct md_attr *ma);
};

struct md_device_operations {
	/** meta-data device related handlers. */
	int (*mdo_root_get)(const struct lu_env *env, struct md_device *m,
			    struct lu_fid *f);

	int (*mdo_maxsize_get)(const struct lu_env *env, struct md_device *m,
			       int *md_size, int *cookie_size);

	int (*mdo_statfs)(const struct lu_env *env, struct md_device *m,
			  struct obd_statfs *sfs);

	int (*mdo_init_capa_ctxt)(const struct lu_env *env, struct md_device *m,
				  int mode, unsigned long timeout, __u32 alg,
				  struct lustre_capa_key *keys);

	int (*mdo_update_capa_key)(const struct lu_env *env,
				   struct md_device *m,
				   struct lustre_capa_key *key);

	int (*mdo_llog_ctxt_get)(const struct lu_env *env,
				 struct md_device *m, int idx, void **h);

	int (*mdo_iocontrol)(const struct lu_env *env, struct md_device *m,
			     unsigned int cmd, int len, void *data);
};

enum md_upcall_event {
	/** Sync the md layer*/
	MD_LOV_SYNC = (1 << 0),
	/** Just for split, no need trans, for replay */
	MD_NO_TRANS = (1 << 1),
	MD_LOV_CONFIG = (1 << 2),
	/** Trigger quota recovery */
	MD_LOV_QUOTA = (1 << 3)
};

struct md_upcall {
	/** this lock protects upcall using against its removal
	 * read lock is for usage the upcall, write - for init/fini */
	struct rw_semaphore	mu_upcall_sem;
	/** device to call, upper layer normally */
	struct md_device       *mu_upcall_dev;
	/** upcall function */
	int (*mu_upcall)(const struct lu_env *env, struct md_device *md,
			 enum md_upcall_event ev, void *data);
};

struct md_device {
	struct lu_device		   md_lu_dev;
	const struct md_device_operations *md_ops;
	struct md_upcall		   md_upcall;
};

static inline void md_upcall_init(struct md_device *m, void *upcl)
{
	init_rwsem(&m->md_upcall.mu_upcall_sem);
	m->md_upcall.mu_upcall_dev = NULL;
	m->md_upcall.mu_upcall = upcl;
}

static inline void md_upcall_dev_set(struct md_device *m, struct md_device *up)
{
	down_write(&m->md_upcall.mu_upcall_sem);
	m->md_upcall.mu_upcall_dev = up;
	up_write(&m->md_upcall.mu_upcall_sem);
}

static inline void md_upcall_fini(struct md_device *m)
{
	down_write(&m->md_upcall.mu_upcall_sem);
	m->md_upcall.mu_upcall_dev = NULL;
	m->md_upcall.mu_upcall = NULL;
	up_write(&m->md_upcall.mu_upcall_sem);
}

static inline int md_do_upcall(const struct lu_env *env, struct md_device *m,
				enum md_upcall_event ev, void *data)
{
	int rc = 0;
	down_read(&m->md_upcall.mu_upcall_sem);
	if (m->md_upcall.mu_upcall_dev != NULL &&
	    m->md_upcall.mu_upcall_dev->md_upcall.mu_upcall != NULL) {
		rc = m->md_upcall.mu_upcall_dev->md_upcall.mu_upcall(env,
					      m->md_upcall.mu_upcall_dev,
					      ev, data);
	}
	up_read(&m->md_upcall.mu_upcall_sem);
	return rc;
}

struct md_object {
	struct lu_object		   mo_lu;
	const struct md_object_operations *mo_ops;
	const struct md_dir_operations    *mo_dir_ops;
};

/**
 * seq-server site.
 */
struct seq_server_site {
	struct lu_site	     *ss_lu;
	/**
	 * mds number of this site.
	 */
	mdsno_t	       ss_node_id;
	/**
	 * Fid location database
	 */
	struct lu_server_fld *ss_server_fld;
	struct lu_client_fld *ss_client_fld;

	/**
	 * Server Seq Manager
	 */
	struct lu_server_seq *ss_server_seq;

	/**
	 * Controller Seq Manager
	 */
	struct lu_server_seq *ss_control_seq;
	struct obd_export    *ss_control_exp;

	/**
	 * Client Seq Manager
	 */
	struct lu_client_seq *ss_client_seq;
};

static inline struct md_device *lu2md_dev(const struct lu_device *d)
{
	LASSERT(IS_ERR(d) || lu_device_is_md(d));
	return container_of0(d, struct md_device, md_lu_dev);
}

static inline struct lu_device *md2lu_dev(struct md_device *d)
{
	return &d->md_lu_dev;
}

static inline struct md_object *lu2md(const struct lu_object *o)
{
	LASSERT(o == NULL || IS_ERR(o) || lu_device_is_md(o->lo_dev));
	return container_of0(o, struct md_object, mo_lu);
}

static inline struct md_object *md_object_next(const struct md_object *obj)
{
	return (obj ? lu2md(lu_object_next(&obj->mo_lu)) : NULL);
}

static inline struct md_device *md_obj2dev(const struct md_object *o)
{
	LASSERT(o == NULL || IS_ERR(o) || lu_device_is_md(o->mo_lu.lo_dev));
	return container_of0(o->mo_lu.lo_dev, struct md_device, md_lu_dev);
}

static inline int md_device_init(struct md_device *md, struct lu_device_type *t)
{
	return lu_device_init(&md->md_lu_dev, t);
}

static inline void md_device_fini(struct md_device *md)
{
	lu_device_fini(&md->md_lu_dev);
}

static inline struct md_object *md_object_find_slice(const struct lu_env *env,
						     struct md_device *md,
						     const struct lu_fid *f)
{
	return lu2md(lu_object_find_slice(env, md2lu_dev(md), f, NULL));
}


/** md operations */
static inline int mo_permission(const struct lu_env *env,
				struct md_object *p,
				struct md_object *c,
				struct md_attr *at,
				int mask)
{
	LASSERT(c->mo_ops->moo_permission);
	return c->mo_ops->moo_permission(env, p, c, at, mask);
}

static inline int mo_attr_get(const struct lu_env *env,
			      struct md_object *m,
			      struct md_attr *at)
{
	LASSERT(m->mo_ops->moo_attr_get);
	return m->mo_ops->moo_attr_get(env, m, at);
}

static inline int mo_readlink(const struct lu_env *env,
			      struct md_object *m,
			      struct lu_buf *buf)
{
	LASSERT(m->mo_ops->moo_readlink);
	return m->mo_ops->moo_readlink(env, m, buf);
}

static inline int mo_changelog(const struct lu_env *env,
			       enum changelog_rec_type type,
			       int flags, struct md_object *m)
{
	LASSERT(m->mo_ops->moo_changelog);
	return m->mo_ops->moo_changelog(env, type, flags, m);
}

static inline int mo_attr_set(const struct lu_env *env,
			      struct md_object *m,
			      const struct md_attr *at)
{
	LASSERT(m->mo_ops->moo_attr_set);
	return m->mo_ops->moo_attr_set(env, m, at);
}

static inline int mo_xattr_get(const struct lu_env *env,
			       struct md_object *m,
			       struct lu_buf *buf,
			       const char *name)
{
	LASSERT(m->mo_ops->moo_xattr_get);
	return m->mo_ops->moo_xattr_get(env, m, buf, name);
}

static inline int mo_xattr_del(const struct lu_env *env,
			       struct md_object *m,
			       const char *name)
{
	LASSERT(m->mo_ops->moo_xattr_del);
	return m->mo_ops->moo_xattr_del(env, m, name);
}

static inline int mo_xattr_set(const struct lu_env *env,
			       struct md_object *m,
			       const struct lu_buf *buf,
			       const char *name,
			       int flags)
{
	LASSERT(m->mo_ops->moo_xattr_set);
	return m->mo_ops->moo_xattr_set(env, m, buf, name, flags);
}

static inline int mo_xattr_list(const struct lu_env *env,
				struct md_object *m,
				struct lu_buf *buf)
{
	LASSERT(m->mo_ops->moo_xattr_list);
	return m->mo_ops->moo_xattr_list(env, m, buf);
}

static inline int mo_swap_layouts(const struct lu_env *env,
				  struct md_object *o1,
				  struct md_object *o2, __u64 flags)
{
	LASSERT(o1->mo_ops->moo_swap_layouts);
	LASSERT(o2->mo_ops->moo_swap_layouts);
	if (o1->mo_ops->moo_swap_layouts != o2->mo_ops->moo_swap_layouts)
		return -EPERM;
	return o1->mo_ops->moo_swap_layouts(env, o1, o2, flags);
}

static inline int mo_open(const struct lu_env *env,
			  struct md_object *m,
			  int flags)
{
	LASSERT(m->mo_ops->moo_open);
	return m->mo_ops->moo_open(env, m, flags);
}

static inline int mo_close(const struct lu_env *env,
			   struct md_object *m,
			   struct md_attr *ma,
			   int mode)
{
	LASSERT(m->mo_ops->moo_close);
	return m->mo_ops->moo_close(env, m, ma, mode);
}

static inline int mo_readpage(const struct lu_env *env,
			      struct md_object *m,
			      const struct lu_rdpg *rdpg)
{
	LASSERT(m->mo_ops->moo_readpage);
	return m->mo_ops->moo_readpage(env, m, rdpg);
}

static inline int mo_object_create(const struct lu_env *env,
				   struct md_object *m,
				   const struct md_op_spec *spc,
				   struct md_attr *at)
{
	LASSERT(m->mo_ops->moo_object_create);
	return m->mo_ops->moo_object_create(env, m, spc, at);
}

static inline int mo_ref_add(const struct lu_env *env,
			     struct md_object *m,
			     const struct md_attr *ma)
{
	LASSERT(m->mo_ops->moo_ref_add);
	return m->mo_ops->moo_ref_add(env, m, ma);
}

static inline int mo_ref_del(const struct lu_env *env,
			     struct md_object *m,
			     struct md_attr *ma)
{
	LASSERT(m->mo_ops->moo_ref_del);
	return m->mo_ops->moo_ref_del(env, m, ma);
}

static inline int mo_capa_get(const struct lu_env *env,
			      struct md_object *m,
			      struct lustre_capa *c,
			      int renewal)
{
	LASSERT(m->mo_ops->moo_capa_get);
	return m->mo_ops->moo_capa_get(env, m, c, renewal);
}

static inline int mo_object_sync(const struct lu_env *env, struct md_object *m)
{
	LASSERT(m->mo_ops->moo_object_sync);
	return m->mo_ops->moo_object_sync(env, m);
}

static inline int mo_file_lock(const struct lu_env *env, struct md_object *m,
			       struct lov_mds_md *lmm,
			       struct ldlm_extent *extent,
			       struct lustre_handle *lockh)
{
	LASSERT(m->mo_ops->moo_file_lock);
	return m->mo_ops->moo_file_lock(env, m, lmm, extent, lockh);
}

static inline int mo_file_unlock(const struct lu_env *env, struct md_object *m,
				 struct lov_mds_md *lmm,
				 struct lustre_handle *lockh)
{
	LASSERT(m->mo_ops->moo_file_unlock);
	return m->mo_ops->moo_file_unlock(env, m, lmm, lockh);
}

static inline int mo_object_lock(const struct lu_env *env,
				 struct md_object *m,
				 struct lustre_handle *lh,
				 struct ldlm_enqueue_info *einfo,
				 void *policy)
{
	LASSERT(m->mo_ops->moo_object_lock);
	return m->mo_ops->moo_object_lock(env, m, lh, einfo, policy);
}

static inline int mdo_lookup(const struct lu_env *env,
			     struct md_object *p,
			     const struct lu_name *lname,
			     struct lu_fid *f,
			     struct md_op_spec *spec)
{
	LASSERT(p->mo_dir_ops->mdo_lookup);
	return p->mo_dir_ops->mdo_lookup(env, p, lname, f, spec);
}

static inline mdl_mode_t mdo_lock_mode(const struct lu_env *env,
				       struct md_object *mo,
				       mdl_mode_t lm)
{
	if (mo->mo_dir_ops->mdo_lock_mode == NULL)
		return MDL_MINMODE;
	return mo->mo_dir_ops->mdo_lock_mode(env, mo, lm);
}

static inline int mdo_create(const struct lu_env *env,
			     struct md_object *p,
			     const struct lu_name *lchild_name,
			     struct md_object *c,
			     struct md_op_spec *spc,
			     struct md_attr *at)
{
	LASSERT(p->mo_dir_ops->mdo_create);
	return p->mo_dir_ops->mdo_create(env, p, lchild_name, c, spc, at);
}

static inline int mdo_create_data(const struct lu_env *env,
				  struct md_object *p,
				  struct md_object *c,
				  const struct md_op_spec *spec,
				  struct md_attr *ma)
{
	LASSERT(c->mo_dir_ops->mdo_create_data);
	return c->mo_dir_ops->mdo_create_data(env, p, c, spec, ma);
}

static inline int mdo_rename(const struct lu_env *env,
			     struct md_object *sp,
			     struct md_object *tp,
			     const struct lu_fid *lf,
			     const struct lu_name *lsname,
			     struct md_object *t,
			     const struct lu_name *ltname,
			     struct md_attr *ma)
{
	LASSERT(tp->mo_dir_ops->mdo_rename);
	return tp->mo_dir_ops->mdo_rename(env, sp, tp, lf, lsname, t, ltname,
					  ma);
}

static inline int mdo_is_subdir(const struct lu_env *env,
				struct md_object *mo,
				const struct lu_fid *fid,
				struct lu_fid *sfid)
{
	LASSERT(mo->mo_dir_ops->mdo_is_subdir);
	return mo->mo_dir_ops->mdo_is_subdir(env, mo, fid, sfid);
}

static inline int mdo_link(const struct lu_env *env,
			   struct md_object *p,
			   struct md_object *s,
			   const struct lu_name *lname,
			   struct md_attr *ma)
{
	LASSERT(s->mo_dir_ops->mdo_link);
	return s->mo_dir_ops->mdo_link(env, p, s, lname, ma);
}

static inline int mdo_unlink(const struct lu_env *env,
			     struct md_object *p,
			     struct md_object *c,
			     const struct lu_name *lname,
			     struct md_attr *ma, int no_name)
{
	LASSERT(p->mo_dir_ops->mdo_unlink);
	return p->mo_dir_ops->mdo_unlink(env, p, c, lname, ma, no_name);
}

static inline int mdo_lum_lmm_cmp(const struct lu_env *env,
				  struct md_object *c,
				  const struct md_op_spec *spec,
				  struct md_attr *ma)
{
	LASSERT(c->mo_dir_ops->mdo_lum_lmm_cmp);
	return c->mo_dir_ops->mdo_lum_lmm_cmp(env, c, spec, ma);
}

static inline int mdo_name_insert(const struct lu_env *env,
				  struct md_object *p,
				  const struct lu_name *lname,
				  const struct lu_fid *f,
				  const struct md_attr *ma)
{
	LASSERT(p->mo_dir_ops->mdo_name_insert);
	return p->mo_dir_ops->mdo_name_insert(env, p, lname, f, ma);
}

static inline int mdo_name_remove(const struct lu_env *env,
				  struct md_object *p,
				  const struct lu_name *lname,
				  const struct md_attr *ma)
{
	LASSERT(p->mo_dir_ops->mdo_name_remove);
	return p->mo_dir_ops->mdo_name_remove(env, p, lname, ma);
}

static inline int mdo_rename_tgt(const struct lu_env *env,
				 struct md_object *p,
				 struct md_object *t,
				 const struct lu_fid *lf,
				 const struct lu_name *lname,
				 struct md_attr *ma)
{
	if (t) {
		LASSERT(t->mo_dir_ops->mdo_rename_tgt);
		return t->mo_dir_ops->mdo_rename_tgt(env, p, t, lf, lname, ma);
	} else {
		LASSERT(p->mo_dir_ops->mdo_rename_tgt);
		return p->mo_dir_ops->mdo_rename_tgt(env, p, t, lf, lname, ma);
	}
}

/**
 * Used in MDD/OUT layer for object lock rule
 **/
enum mdd_object_role {
	MOR_SRC_PARENT,
	MOR_SRC_CHILD,
	MOR_TGT_PARENT,
	MOR_TGT_CHILD,
	MOR_TGT_ORPHAN
};

struct dt_device;
/**
 * Structure to hold object information. This is used to create object
 * \pre llod_dir exist
 */
struct lu_local_obj_desc {
	const char		      *llod_dir;
	const char		      *llod_name;
	__u32			    llod_oid;
	int			      llod_is_index;
	const struct dt_index_features  *llod_feat;
	struct list_head		       llod_linkage;
};

int lustre_buf2som(void *buf, int rc, struct md_som_data *msd);
int lustre_buf2hsm(void *buf, int rc, struct md_hsm *mh);
void lustre_hsm2buf(void *buf, struct md_hsm *mh);

struct lu_ucred {
	__u32	       uc_valid;
	__u32	       uc_o_uid;
	__u32	       uc_o_gid;
	__u32	       uc_o_fsuid;
	__u32	       uc_o_fsgid;
	__u32	       uc_uid;
	__u32	       uc_gid;
	__u32	       uc_fsuid;
	__u32	       uc_fsgid;
	__u32	       uc_suppgids[2];
	cfs_cap_t	   uc_cap;
	__u32	       uc_umask;
	struct group_info *uc_ginfo;
	struct md_identity *uc_identity;
};

struct lu_ucred *lu_ucred(const struct lu_env *env);

struct lu_ucred *lu_ucred_check(const struct lu_env *env);

struct lu_ucred *lu_ucred_assert(const struct lu_env *env);

int lu_ucred_global_init(void);

void lu_ucred_global_fini(void);

#define md_cap_t(x) (x)

#define MD_CAP_TO_MASK(x) (1 << (x))

#define md_cap_raised(c, flag) (md_cap_t(c) & MD_CAP_TO_MASK(flag))

/* capable() is copied from linux kernel! */
static inline int md_capable(struct lu_ucred *uc, cfs_cap_t cap)
{
	if (md_cap_raised(uc->uc_cap, cap))
		return 1;
	return 0;
}

/** @} md */
#endif /* _LINUX_MD_OBJECT_H */

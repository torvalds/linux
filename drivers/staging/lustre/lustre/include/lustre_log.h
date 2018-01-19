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
 * Copyright (c) 2007, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright (c) 2012, 2015, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 *
 * lustre/include/lustre_log.h
 *
 * Generic infrastructure for managing a collection of logs.
 * These logs are used for:
 *
 * - orphan recovery: OST adds record on create
 * - mtime/size consistency: the OST adds a record on first write
 * - open/unlinked objects: OST adds a record on destroy
 *
 * - mds unlink log: the MDS adds an entry upon delete
 *
 * - raid1 replication log between OST's
 * - MDS replication logs
 */

#ifndef _LUSTRE_LOG_H
#define _LUSTRE_LOG_H

/** \defgroup log log
 *
 * @{
 */

#include <obd_class.h>
#include <uapi/linux/lustre/lustre_idl.h>

#define LOG_NAME_LIMIT(logname, name)		   \
	snprintf(logname, sizeof(logname), "LOGS/%s", name)
#define LLOG_EEMPTY 4711

enum llog_open_param {
	LLOG_OPEN_EXISTS	= 0x0000,
	LLOG_OPEN_NEW		= 0x0001,
};

struct plain_handle_data {
	struct list_head	  phd_entry;
	struct llog_handle *phd_cat_handle;
	struct llog_cookie  phd_cookie; /* cookie of this log in its cat */
};

struct cat_handle_data {
	struct list_head	      chd_head;
	struct llog_handle     *chd_current_log; /* currently open log */
	struct llog_handle	*chd_next_log; /* llog to be used next */
};

struct llog_handle;

/* llog.c  -  general API */
int llog_init_handle(const struct lu_env *env, struct llog_handle *handle,
		     int flags, struct obd_uuid *uuid);
int llog_process(const struct lu_env *env, struct llog_handle *loghandle,
		 llog_cb_t cb, void *data, void *catdata);
int llog_process_or_fork(const struct lu_env *env,
			 struct llog_handle *loghandle,
			 llog_cb_t cb, void *data, void *catdata, bool fork);
int llog_open(const struct lu_env *env, struct llog_ctxt *ctxt,
	      struct llog_handle **lgh, struct llog_logid *logid,
	      char *name, enum llog_open_param open_param);
int llog_close(const struct lu_env *env, struct llog_handle *cathandle);

/* llog_process flags */
#define LLOG_FLAG_NODEAMON 0x0001

/* llog_cat.c - catalog api */
struct llog_process_data {
	/**
	 * Any useful data needed while processing catalog. This is
	 * passed later to process callback.
	 */
	void		*lpd_data;
	/**
	 * Catalog process callback function, called for each record
	 * in catalog.
	 */
	llog_cb_t	    lpd_cb;
	/**
	 * Start processing the catalog from startcat/startidx
	 */
	int		  lpd_startcat;
	int		  lpd_startidx;
};

struct llog_process_cat_data {
	/**
	 * Temporary stored first_idx while scanning log.
	 */
	int		  lpcd_first_idx;
	/**
	 * Temporary stored last_idx while scanning log.
	 */
	int		  lpcd_last_idx;
};

struct thandle;

int llog_cat_close(const struct lu_env *env, struct llog_handle *cathandle);
int llog_cat_process(const struct lu_env *env, struct llog_handle *cat_llh,
		     llog_cb_t cb, void *data, int startcat, int startidx);

/* llog_obd.c */
int llog_setup(const struct lu_env *env, struct obd_device *obd,
	       struct obd_llog_group *olg, int index,
	       struct obd_device *disk_obd, struct llog_operations *op);
int __llog_ctxt_put(const struct lu_env *env, struct llog_ctxt *ctxt);
int llog_cleanup(const struct lu_env *env, struct llog_ctxt *);

/* llog_net.c */
int llog_initiator_connect(struct llog_ctxt *ctxt);

struct llog_operations {
	int (*lop_next_block)(const struct lu_env *env, struct llog_handle *h,
			      int *curr_idx, int next_idx, __u64 *offset,
			      void *buf, int len);
	int (*lop_prev_block)(const struct lu_env *env, struct llog_handle *h,
			      int prev_idx, void *buf, int len);
	int (*lop_read_header)(const struct lu_env *env,
			       struct llog_handle *handle);
	int (*lop_setup)(const struct lu_env *env, struct obd_device *obd,
			 struct obd_llog_group *olg, int ctxt_idx,
			 struct obd_device *disk_obd);
	int (*lop_sync)(struct llog_ctxt *ctxt, struct obd_export *exp,
			int flags);
	int (*lop_cleanup)(const struct lu_env *env, struct llog_ctxt *ctxt);
	int (*lop_cancel)(const struct lu_env *env, struct llog_ctxt *ctxt,
			  struct llog_cookie *cookies, int flags);
	int (*lop_connect)(struct llog_ctxt *ctxt, struct llog_logid *logid,
			   struct llog_gen *gen, struct obd_uuid *uuid);
	/**
	 * Any llog file must be opened first using llog_open().  Llog can be
	 * opened by name, logid or without both, in last case the new logid
	 * will be generated.
	 */
	int (*lop_open)(const struct lu_env *env, struct llog_handle *lgh,
			struct llog_logid *logid, char *name,
			enum llog_open_param);
	/**
	 * Opened llog may not exist and this must be checked where needed using
	 * the llog_exist() call.
	 */
	int (*lop_exist)(struct llog_handle *lgh);
	/**
	 * Close llog file and calls llog_free_handle() implicitly.
	 * Any opened llog must be closed by llog_close() call.
	 */
	int (*lop_close)(const struct lu_env *env, struct llog_handle *handle);
	/**
	 * Create new llog file. The llog must be opened.
	 * Must be used only for local llog operations.
	 */
	int (*lop_declare_create)(const struct lu_env *env,
				  struct llog_handle *handle,
				  struct thandle *th);
	/**
	 * write new record in llog. It appends records usually but can edit
	 * existing records too.
	 */
	int (*lop_declare_write_rec)(const struct lu_env *env,
				     struct llog_handle *lgh,
				     struct llog_rec_hdr *rec,
				     int idx, struct thandle *th);
	int (*lop_write_rec)(const struct lu_env *env,
			     struct llog_handle *loghandle,
			     struct llog_rec_hdr *rec,
			     struct llog_cookie *cookie, int cookiecount,
			     void *buf, int idx, struct thandle *th);
	/**
	 * Add new record in llog catalog. Does the same as llog_write_rec()
	 * but using llog catalog.
	 */
	int (*lop_declare_add)(const struct lu_env *env,
			       struct llog_handle *lgh,
			       struct llog_rec_hdr *rec, struct thandle *th);
	int (*lop_add)(const struct lu_env *env, struct llog_handle *lgh,
		       struct llog_rec_hdr *rec, struct llog_cookie *cookie,
		       void *buf, struct thandle *th);
};

/* In-memory descriptor for a log object or log catalog */
struct llog_handle {
	struct rw_semaphore	 lgh_lock;
	spinlock_t		 lgh_hdr_lock; /* protect lgh_hdr data */
	struct llog_logid	 lgh_id; /* id of this log */
	struct llog_log_hdr	*lgh_hdr;
	size_t			 lgh_hdr_size;
	int			 lgh_last_idx;
	int			 lgh_cur_idx; /* used during llog_process */
	__u64			 lgh_cur_offset; /* used during llog_process */
	struct llog_ctxt	*lgh_ctxt;
	union {
		struct plain_handle_data	 phd;
		struct cat_handle_data		 chd;
	} u;
	char			*lgh_name;
	void			*private_data;
	struct llog_operations	*lgh_logops;
	atomic_t		 lgh_refcount;
};

#define LLOG_CTXT_FLAG_UNINITIALIZED     0x00000001
#define LLOG_CTXT_FLAG_STOP		 0x00000002

struct llog_ctxt {
	int		      loc_idx; /* my index the obd array of ctxt's */
	struct obd_device       *loc_obd; /* points back to the containing obd*/
	struct obd_llog_group   *loc_olg; /* group containing that ctxt */
	struct obd_export       *loc_exp; /* parent "disk" export (e.g. MDS) */
	struct obd_import       *loc_imp; /* to use in RPC's: can be backward
					   * pointing import
					   */
	struct llog_operations  *loc_logops;
	struct llog_handle      *loc_handle;
	struct mutex		 loc_mutex; /* protect loc_imp */
	atomic_t	     loc_refcount;
	long		     loc_flags; /* flags, see above defines */
	/*
	 * llog chunk size, and llog record size can not be bigger than
	 * loc_chunk_size
	 */
	__u32			loc_chunk_size;
};

#define LLOG_PROC_BREAK 0x0001
#define LLOG_DEL_RECORD 0x0002

static inline int llog_handle2ops(struct llog_handle *loghandle,
				  struct llog_operations **lop)
{
	if (!loghandle || !loghandle->lgh_logops)
		return -EINVAL;

	*lop = loghandle->lgh_logops;
	return 0;
}

static inline struct llog_ctxt *llog_ctxt_get(struct llog_ctxt *ctxt)
{
	atomic_inc(&ctxt->loc_refcount);
	CDEBUG(D_INFO, "GETting ctxt %p : new refcount %d\n", ctxt,
	       atomic_read(&ctxt->loc_refcount));
	return ctxt;
}

static inline void llog_ctxt_put(struct llog_ctxt *ctxt)
{
	if (!ctxt)
		return;
	LASSERT_ATOMIC_GT_LT(&ctxt->loc_refcount, 0, LI_POISON);
	CDEBUG(D_INFO, "PUTting ctxt %p : new refcount %d\n", ctxt,
	       atomic_read(&ctxt->loc_refcount) - 1);
	__llog_ctxt_put(NULL, ctxt);
}

static inline void llog_group_init(struct obd_llog_group *olg)
{
	init_waitqueue_head(&olg->olg_waitq);
	spin_lock_init(&olg->olg_lock);
	mutex_init(&olg->olg_cat_processing);
}

static inline int llog_group_set_ctxt(struct obd_llog_group *olg,
				      struct llog_ctxt *ctxt, int index)
{
	LASSERT(index >= 0 && index < LLOG_MAX_CTXTS);

	spin_lock(&olg->olg_lock);
	if (olg->olg_ctxts[index]) {
		spin_unlock(&olg->olg_lock);
		return -EEXIST;
	}
	olg->olg_ctxts[index] = ctxt;
	spin_unlock(&olg->olg_lock);
	return 0;
}

static inline struct llog_ctxt *llog_group_get_ctxt(struct obd_llog_group *olg,
						    int index)
{
	struct llog_ctxt *ctxt;

	LASSERT(index >= 0 && index < LLOG_MAX_CTXTS);

	spin_lock(&olg->olg_lock);
	if (!olg->olg_ctxts[index])
		ctxt = NULL;
	else
		ctxt = llog_ctxt_get(olg->olg_ctxts[index]);
	spin_unlock(&olg->olg_lock);
	return ctxt;
}

static inline void llog_group_clear_ctxt(struct obd_llog_group *olg, int index)
{
	LASSERT(index >= 0 && index < LLOG_MAX_CTXTS);
	spin_lock(&olg->olg_lock);
	olg->olg_ctxts[index] = NULL;
	spin_unlock(&olg->olg_lock);
}

static inline struct llog_ctxt *llog_get_context(struct obd_device *obd,
						 int index)
{
	return llog_group_get_ctxt(&obd->obd_olg, index);
}

static inline int llog_group_ctxt_null(struct obd_llog_group *olg, int index)
{
	return (!olg->olg_ctxts[index]);
}

static inline int llog_ctxt_null(struct obd_device *obd, int index)
{
	return llog_group_ctxt_null(&obd->obd_olg, index);
}

static inline int llog_next_block(const struct lu_env *env,
				  struct llog_handle *loghandle, int *cur_idx,
				  int next_idx, __u64 *cur_offset, void *buf,
				  int len)
{
	struct llog_operations *lop;
	int rc;

	rc = llog_handle2ops(loghandle, &lop);
	if (rc)
		return rc;
	if (!lop->lop_next_block)
		return -EOPNOTSUPP;

	rc = lop->lop_next_block(env, loghandle, cur_idx, next_idx,
				 cur_offset, buf, len);
	return rc;
}

/* llog.c */
int llog_declare_write_rec(const struct lu_env *env,
			   struct llog_handle *handle,
			   struct llog_rec_hdr *rec, int idx,
			   struct thandle *th);
int llog_write_rec(const struct lu_env *env, struct llog_handle *handle,
		   struct llog_rec_hdr *rec, struct llog_cookie *logcookies,
		   int numcookies, void *buf, int idx, struct thandle *th);
int lustre_process_log(struct super_block *sb, char *logname,
		       struct config_llog_instance *cfg);
int lustre_end_log(struct super_block *sb, char *logname,
		   struct config_llog_instance *cfg);
/** @} log */

#endif

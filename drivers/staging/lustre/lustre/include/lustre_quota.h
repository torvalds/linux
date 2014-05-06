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
 * version 2 along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 021110-1307, USA
 *
 * GPL HEADER END
 */
/*
 * Copyright (c) 2011, 2012, Intel Corporation.
 * Use is subject to license terms.
 */

#ifndef _LUSTRE_QUOTA_H
#define _LUSTRE_QUOTA_H

/** \defgroup quota quota
 *
 */

#include <linux/lustre_quota.h>

#include <dt_object.h>
#include <lustre_fid.h>
#include <lustre_dlm.h>

#ifndef MAX_IQ_TIME
#define MAX_IQ_TIME  604800     /* (7*24*60*60) 1 week */
#endif

#ifndef MAX_DQ_TIME
#define MAX_DQ_TIME  604800     /* (7*24*60*60) 1 week */
#endif

struct lquota_id_info;
struct lquota_trans;

/* Gather all quota record type in an union that can be used to read any records
 * from disk. All fields of these records must be 64-bit aligned, otherwise the
 * OSD layer may swab them incorrectly. */
union lquota_rec {
	struct lquota_glb_rec	lqr_glb_rec;
	struct lquota_slv_rec	lqr_slv_rec;
	struct lquota_acct_rec	lqr_acct_rec;
};

/* Index features supported by the global index objects
 * Only used for migration purpose and should be removed once on-disk migration
 * is no longer needed */
extern struct dt_index_features dt_quota_iusr_features;
extern struct dt_index_features dt_quota_busr_features;
extern struct dt_index_features dt_quota_igrp_features;
extern struct dt_index_features dt_quota_bgrp_features;

/* Name used in the configuration logs to identify the default metadata pool
 * (composed of all the MDTs, with pool ID 0) and the default data pool (all
 * the OSTs, with pool ID 0 too). */
#define QUOTA_METAPOOL_NAME   "mdt="
#define QUOTA_DATAPOOL_NAME   "ost="

/*
 * Quota Master Target support
 */

/* Request handlers for quota master operations.
 * This is used by the MDT to pass quota/lock requests to the quota master
 * target. This won't be needed any more once the QMT is a real target and
 * does not rely any more on the MDT service threads and namespace. */
struct qmt_handlers {
	/* Handle quotactl request from client. */
	int (*qmth_quotactl)(const struct lu_env *, struct lu_device *,
			     struct obd_quotactl *);

	/* Handle dqacq/dqrel request from slave. */
	int (*qmth_dqacq)(const struct lu_env *, struct lu_device *,
			  struct ptlrpc_request *);

	/* LDLM intent policy associated with quota locks */
	int (*qmth_intent_policy)(const struct lu_env *, struct lu_device *,
				  struct ptlrpc_request *, struct ldlm_lock **,
				  int);

	/* Initialize LVB of ldlm resource associated with quota objects */
	int (*qmth_lvbo_init)(struct lu_device *, struct ldlm_resource *);

	/* Update LVB of ldlm resource associated with quota objects */
	int (*qmth_lvbo_update)(struct lu_device *, struct ldlm_resource *,
				struct ptlrpc_request *, int);

	/* Return size of LVB to be packed in ldlm message */
	int (*qmth_lvbo_size)(struct lu_device *, struct ldlm_lock *);

	/* Fill request buffer with lvb */
	int (*qmth_lvbo_fill)(struct lu_device *, struct ldlm_lock *, void *,
			      int);

	/* Free lvb associated with ldlm resource */
	int (*qmth_lvbo_free)(struct lu_device *, struct ldlm_resource *);
};

/* actual handlers are defined in lustre/quota/qmt_handler.c */
extern struct qmt_handlers qmt_hdls;

/*
 * Quota enforcement support on slaves
 */

struct qsd_instance;

/* The quota slave feature is implemented under the form of a library.
 * The API is the following:
 *
 * - qsd_init(): the user (mostly the OSD layer) should first allocate a qsd
 *	       instance via qsd_init(). This creates all required structures
 *	       to manage quota enforcement for this target and performs all
 *	       low-level initialization which does not involve any lustre
 *	       object. qsd_init() should typically be called when the OSD
 *	       is being set up.
 *
 * - qsd_prepare(): This sets up on-disk objects associated with the quota slave
 *		  feature and initiates the quota reintegration procedure if
 *		  needed. qsd_prepare() should typically be called when
 *		  ->ldo_prepare is invoked.
 *
 * - qsd_start(): a qsd instance should be started once recovery is completed
 *		(i.e. when ->ldo_recovery_complete is called). This is used
 *		to notify the qsd layer that quota should now be enforced
 *		again via the qsd_op_begin/end functions. The last step of the
 *		reintegration procedure (namely usage reconciliation) will be
 *		completed during start.
 *
 * - qsd_fini(): is used to release a qsd_instance structure allocated with
 *	       qsd_init(). This releases all quota slave objects and frees the
 *	       structures associated with the qsd_instance.
 *
 * - qsd_op_begin(): is used to enforce quota, it must be called in the
 *		   declaration of each operation. qsd_op_end() should then be
 *		   invoked later once all operations have been completed in
 *		   order to release/adjust the quota space.
 *		   Running qsd_op_begin() before qsd_start() isn't fatal and
 *		   will return success.
 *		   Once qsd_start() has been run, qsd_op_begin() will block
 *		   until the reintegration procedure is completed.
 *
 * - qsd_op_end(): performs the post operation quota processing. This must be
 *		 called after the operation transaction stopped.
 *		 While qsd_op_begin() must be invoked each time a new
 *		 operation is declared, qsd_op_end() should be called only
 *		 once for the whole transaction.
 *
 * - qsd_op_adjust(): triggers pre-acquire/release if necessary.
 *
 * Below are the function prototypes to be used by OSD layer to manage quota
 * enforcement. Arguments are documented where each function is defined.  */

struct qsd_instance *qsd_init(const struct lu_env *, char *, struct dt_device *,
			      struct proc_dir_entry *);
int qsd_prepare(const struct lu_env *, struct qsd_instance *);
int qsd_start(const struct lu_env *, struct qsd_instance *);
void qsd_fini(const struct lu_env *, struct qsd_instance *);
int qsd_op_begin(const struct lu_env *, struct qsd_instance *,
		 struct lquota_trans *, struct lquota_id_info *, int *);
void qsd_op_end(const struct lu_env *, struct qsd_instance *,
		struct lquota_trans *);
void qsd_op_adjust(const struct lu_env *, struct qsd_instance *,
		   union lquota_id *, int);
/* This is exported for the ldiskfs quota migration only,
 * see convert_quota_file() */
int lquota_disk_write_glb(const struct lu_env *, struct dt_object *,
			  __u64, struct lquota_glb_rec *);

/*
 * Quota information attached to a transaction
 */

struct lquota_entry;

struct lquota_id_info {
	/* quota identifier */
	union lquota_id		 lqi_id;

	/* USRQUOTA or GRPQUOTA for now, could be expanded for
	 * directory quota or other types later.  */
	int			 lqi_type;

	/* inodes or kbytes to be consumed or released, it could
	 * be negative when releasing space.  */
	long long		 lqi_space;

	/* quota slave entry structure associated with this ID */
	struct lquota_entry	*lqi_qentry;

	/* whether we are reporting blocks or inodes */
	bool			 lqi_is_blk;
};

/* Since we enforce only inode quota in meta pool (MDTs), and block quota in
 * data pool (OSTs), there are at most 4 quota ids being enforced in a single
 * transaction, which is chown transaction:
 * original uid and gid, new uid and gid.
 *
 * This value might need to be revised when directory quota is added.  */
#define QUOTA_MAX_TRANSIDS    4

/* all qids involved in a single transaction */
struct lquota_trans {
	unsigned short		lqt_id_cnt;
	struct lquota_id_info	lqt_ids[QUOTA_MAX_TRANSIDS];
};

/* flags for quota local enforcement */
#define QUOTA_FL_OVER_USRQUOTA  0x01
#define QUOTA_FL_OVER_GRPQUOTA  0x02
#define QUOTA_FL_SYNC	   0x04

#define IS_LQUOTA_RES(res)						\
	(res->lr_name.name[LUSTRE_RES_ID_SEQ_OFF] == FID_SEQ_QUOTA ||	\
	 res->lr_name.name[LUSTRE_RES_ID_SEQ_OFF] == FID_SEQ_QUOTA_GLB)

/* helper function used by MDT & OFD to retrieve quota accounting information
 * on slave */
int lquotactl_slv(const struct lu_env *, struct dt_device *,
		  struct obd_quotactl *);
/** @} quota */
#endif /* _LUSTRE_QUOTA_H */

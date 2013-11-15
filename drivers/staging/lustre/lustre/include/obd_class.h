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
 */
#ifndef __CLASS_OBD_H
#define __CLASS_OBD_H


#include <obd_support.h>
#include <lustre_import.h>
#include <lustre_net.h>
#include <obd.h>
#include <lustre_lib.h>
#include <lustre/lustre_idl.h>
#include <lprocfs_status.h>

#include <linux/obd_class.h>

#define OBD_STATFS_NODELAY      0x0001  /* requests should be send without delay
					 * and resends for avoid deadlocks */
#define OBD_STATFS_FROM_CACHE   0x0002  /* the statfs callback should not update
					 * obd_osfs_age */
#define OBD_STATFS_PTLRPCD      0x0004  /* requests will be sent via ptlrpcd
					 * instead of a specific set. This
					 * means that we cannot rely on the set
					 * interpret routine to be called.
					 * lov_statfs_fini() must thus be called
					 * by the request interpret routine */
#define OBD_STATFS_FOR_MDT0	0x0008	/* The statfs is only for retrieving
					 * information from MDT0. */
#define OBD_FL_PUNCH    0x00000001      /* To indicate it is punch operation */

/* OBD Device Declarations */
extern struct obd_device *obd_devs[MAX_OBD_DEVICES];
extern rwlock_t obd_dev_lock;

/* OBD Operations Declarations */
extern struct obd_device *class_conn2obd(struct lustre_handle *);
extern struct obd_device *class_exp2obd(struct obd_export *);
extern int class_handle_ioctl(unsigned int cmd, unsigned long arg);
extern int lustre_get_jobid(char *jobid);

struct lu_device_type;

/* genops.c */
struct obd_export *class_conn2export(struct lustre_handle *);
int class_register_type(struct obd_ops *, struct md_ops *,
			struct lprocfs_vars *, const char *nm,
			struct lu_device_type *ldt);
int class_unregister_type(const char *nm);

struct obd_device *class_newdev(const char *type_name, const char *name);
void class_release_dev(struct obd_device *obd);

int class_name2dev(const char *name);
struct obd_device *class_name2obd(const char *name);
int class_uuid2dev(struct obd_uuid *uuid);
struct obd_device *class_uuid2obd(struct obd_uuid *uuid);
void class_obd_list(void);
struct obd_device * class_find_client_obd(struct obd_uuid *tgt_uuid,
					  const char * typ_name,
					  struct obd_uuid *grp_uuid);
struct obd_device * class_devices_in_group(struct obd_uuid *grp_uuid,
					   int *next);
struct obd_device * class_num2obd(int num);
int get_devices_count(void);

int class_notify_sptlrpc_conf(const char *fsname, int namelen);

char *obd_export_nid2str(struct obd_export *exp);

int obd_export_evict_by_nid(struct obd_device *obd, const char *nid);
int obd_export_evict_by_uuid(struct obd_device *obd, const char *uuid);
int obd_connect_flags2str(char *page, int count, __u64 flags, char *sep);

int obd_zombie_impexp_init(void);
void obd_zombie_impexp_stop(void);
void obd_zombie_impexp_cull(void);
void obd_zombie_barrier(void);
void obd_exports_barrier(struct obd_device *obd);
int kuc_len(int payload_len);
struct kuc_hdr * kuc_ptr(void *p);
int kuc_ispayload(void *p);
void *kuc_alloc(int payload_len, int transport, int type);
void kuc_free(void *p, int payload_len);

struct llog_handle;
struct llog_rec_hdr;
typedef int (*llog_cb_t)(const struct lu_env *, struct llog_handle *,
			 struct llog_rec_hdr *, void *);
/* obd_config.c */
struct lustre_cfg *lustre_cfg_rename(struct lustre_cfg *cfg,
				     const char *new_name);
int class_process_config(struct lustre_cfg *lcfg);
int class_process_proc_param(char *prefix, struct lprocfs_vars *lvars,
			     struct lustre_cfg *lcfg, void *data);
int class_attach(struct lustre_cfg *lcfg);
int class_setup(struct obd_device *obd, struct lustre_cfg *lcfg);
int class_cleanup(struct obd_device *obd, struct lustre_cfg *lcfg);
int class_detach(struct obd_device *obd, struct lustre_cfg *lcfg);
struct obd_device *class_incref(struct obd_device *obd,
				const char *scope, const void *source);
void class_decref(struct obd_device *obd,
		  const char *scope, const void *source);
void dump_exports(struct obd_device *obd, int locks);
int class_config_llog_handler(const struct lu_env *env,
			      struct llog_handle *handle,
			      struct llog_rec_hdr *rec, void *data);
int class_add_conn(struct obd_device *obd, struct lustre_cfg *lcfg);
int class_add_uuid(const char *uuid, __u64 nid);

/*obdecho*/
#ifdef LPROCFS
extern void lprocfs_echo_init_vars(struct lprocfs_static_vars *lvars);
#else
static inline void lprocfs_echo_init_vars(struct lprocfs_static_vars *lvars)
{
	memset(lvars, 0, sizeof(*lvars));
}
#endif

#define CFG_F_START     0x01   /* Set when we start updating from a log */
#define CFG_F_MARKER    0x02   /* We are within a maker */
#define CFG_F_SKIP      0x04   /* We should ignore this cfg command */
#define CFG_F_COMPAT146 0x08   /* Allow old-style logs */
#define CFG_F_EXCLUDE   0x10   /* OST exclusion list */

/* Passed as data param to class_config_parse_llog */
struct config_llog_instance {
	char	       *cfg_obdname;
	void	       *cfg_instance;
	struct super_block *cfg_sb;
	struct obd_uuid     cfg_uuid;
	llog_cb_t	    cfg_callback;
	int		 cfg_last_idx; /* for partial llog processing */
	int		 cfg_flags;
};
int class_config_parse_llog(const struct lu_env *env, struct llog_ctxt *ctxt,
			    char *name, struct config_llog_instance *cfg);
int class_config_dump_llog(const struct lu_env *env, struct llog_ctxt *ctxt,
			   char *name, struct config_llog_instance *cfg);

enum {
	CONFIG_T_CONFIG  = 0,
	CONFIG_T_SPTLRPC = 1,
	CONFIG_T_RECOVER = 2,
	CONFIG_T_MAX     = 3
};

/* list of active configuration logs  */
struct config_llog_data {
	struct ldlm_res_id	  cld_resid;
	struct config_llog_instance cld_cfg;
	struct list_head		  cld_list_chain;
	atomic_t		cld_refcount;
	struct config_llog_data    *cld_sptlrpc;/* depended sptlrpc log */
	struct config_llog_data    *cld_recover;    /* imperative recover log */
	struct obd_export	  *cld_mgcexp;
	struct mutex		    cld_lock;
	int			 cld_type;
	unsigned int		cld_stopping:1, /* we were told to stop
						     * watching */
				    cld_lostlock:1; /* lock not requeued */
	char			cld_logname[0];
};

struct lustre_profile {
	struct list_head       lp_list;
	char	    *lp_profile;
	char	    *lp_dt;
	char	    *lp_md;
};

struct lustre_profile *class_get_profile(const char * prof);
void class_del_profile(const char *prof);
void class_del_profiles(void);

#if LUSTRE_TRACKS_LOCK_EXP_REFS

void __class_export_add_lock_ref(struct obd_export *, struct ldlm_lock *);
void __class_export_del_lock_ref(struct obd_export *, struct ldlm_lock *);
extern void (*class_export_dump_hook)(struct obd_export *);

#else

#define __class_export_add_lock_ref(exp, lock)	     do {} while(0)
#define __class_export_del_lock_ref(exp, lock)	     do {} while(0)

#endif

#define class_export_rpc_inc(exp)				       \
({								      \
	atomic_inc(&(exp)->exp_rpc_count);			  \
	CDEBUG(D_INFO, "RPC GETting export %p : new rpc_count %d\n",    \
	       (exp), atomic_read(&(exp)->exp_rpc_count));	  \
})

#define class_export_rpc_dec(exp)				       \
({								      \
	LASSERT_ATOMIC_POS(&exp->exp_rpc_count);			\
	atomic_dec(&(exp)->exp_rpc_count);			  \
	CDEBUG(D_INFO, "RPC PUTting export %p : new rpc_count %d\n",    \
	       (exp), atomic_read(&(exp)->exp_rpc_count));	  \
})

#define class_export_lock_get(exp, lock)				\
({								      \
	atomic_inc(&(exp)->exp_locks_count);			\
	__class_export_add_lock_ref(exp, lock);			 \
	CDEBUG(D_INFO, "lock GETting export %p : new locks_count %d\n", \
	       (exp), atomic_read(&(exp)->exp_locks_count));	\
	class_export_get(exp);					  \
})

#define class_export_lock_put(exp, lock)				\
({								      \
	LASSERT_ATOMIC_POS(&exp->exp_locks_count);		      \
	atomic_dec(&(exp)->exp_locks_count);			\
	__class_export_del_lock_ref(exp, lock);			 \
	CDEBUG(D_INFO, "lock PUTting export %p : new locks_count %d\n", \
	       (exp), atomic_read(&(exp)->exp_locks_count));	\
	class_export_put(exp);					  \
})

#define class_export_cb_get(exp)					\
({								      \
	atomic_inc(&(exp)->exp_cb_count);			   \
	CDEBUG(D_INFO, "callback GETting export %p : new cb_count %d\n",\
	       (exp), atomic_read(&(exp)->exp_cb_count));	   \
	class_export_get(exp);					  \
})

#define class_export_cb_put(exp)					\
({								      \
	LASSERT_ATOMIC_POS(&exp->exp_cb_count);			 \
	atomic_dec(&(exp)->exp_cb_count);			   \
	CDEBUG(D_INFO, "callback PUTting export %p : new cb_count %d\n",\
	       (exp), atomic_read(&(exp)->exp_cb_count));	   \
	class_export_put(exp);					  \
})

/* genops.c */
struct obd_export *class_export_get(struct obd_export *exp);
void class_export_put(struct obd_export *exp);
struct obd_export *class_new_export(struct obd_device *obddev,
				    struct obd_uuid *cluuid);
void class_unlink_export(struct obd_export *exp);

struct obd_import *class_import_get(struct obd_import *);
void class_import_put(struct obd_import *);
struct obd_import *class_new_import(struct obd_device *obd);
void class_destroy_import(struct obd_import *exp);

struct obd_type *class_search_type(const char *name);
struct obd_type *class_get_type(const char *name);
void class_put_type(struct obd_type *type);
int class_connect(struct lustre_handle *conn, struct obd_device *obd,
		  struct obd_uuid *cluuid);
int class_disconnect(struct obd_export *exp);
void class_fail_export(struct obd_export *exp);
int class_connected_export(struct obd_export *exp);
void class_disconnect_exports(struct obd_device *obddev);
int class_manual_cleanup(struct obd_device *obd);
void class_disconnect_stale_exports(struct obd_device *,
				    int (*test_export)(struct obd_export *));
static inline enum obd_option exp_flags_from_obd(struct obd_device *obd)
{
	return ((obd->obd_fail ? OBD_OPT_FAILOVER : 0) |
		(obd->obd_force ? OBD_OPT_FORCE : 0) |
		(obd->obd_abort_recovery ? OBD_OPT_ABORT_RECOV : 0) |
		0);
}


void obdo_cpy_md(struct obdo *dst, struct obdo *src, obd_flag valid);
void obdo_to_ioobj(struct obdo *oa, struct obd_ioobj *ioobj);
void obdo_from_iattr(struct obdo *oa, struct iattr *attr,
		     unsigned int ia_valid);
void iattr_from_obdo(struct iattr *attr, struct obdo *oa, obd_flag valid);
void md_from_obdo(struct md_op_data *op_data, struct obdo *oa, obd_flag valid);
void obdo_from_md(struct obdo *oa, struct md_op_data *op_data,
		  unsigned int valid);

void obdo_cpu_to_le(struct obdo *dobdo, struct obdo *sobdo);
void obdo_le_to_cpu(struct obdo *dobdo, struct obdo *sobdo);

#define OBT(dev)	(dev)->obd_type
#define OBP(dev, op)    (dev)->obd_type->typ_dt_ops->o_ ## op
#define MDP(dev, op)    (dev)->obd_type->typ_md_ops->m_ ## op
#define CTXTP(ctxt, op) (ctxt)->loc_logops->lop_##op

/* Ensure obd_setup: used for cleanup which must be called
   while obd is stopping */
#define OBD_CHECK_DEV(obd)				      \
do {							    \
	if (!(obd)) {					   \
		CERROR("NULL device\n");			\
		return -ENODEV;				\
	}						       \
} while (0)

/* ensure obd_setup and !obd_stopping */
#define OBD_CHECK_DEV_ACTIVE(obd)			       \
do {							    \
	OBD_CHECK_DEV(obd);				     \
	if (!(obd)->obd_set_up || (obd)->obd_stopping) {	\
		CERROR("Device %d not setup\n",		 \
		       (obd)->obd_minor);		       \
		return -ENODEV;				\
	}						       \
} while (0)


#ifdef LPROCFS
#define OBD_COUNTER_OFFSET(op)				  \
	((offsetof(struct obd_ops, o_ ## op) -		  \
	  offsetof(struct obd_ops, o_iocontrol))		\
	 / sizeof(((struct obd_ops *)(0))->o_iocontrol))

#define OBD_COUNTER_INCREMENT(obdx, op)			   \
	if ((obdx)->obd_stats != NULL) {			  \
		unsigned int coffset;			     \
		coffset = (unsigned int)((obdx)->obd_cntr_base) + \
			OBD_COUNTER_OFFSET(op);		   \
		LASSERT(coffset < (obdx)->obd_stats->ls_num);     \
		lprocfs_counter_incr((obdx)->obd_stats, coffset); \
	}

#define EXP_COUNTER_INCREMENT(export, op)				    \
	if ((export)->exp_obd->obd_stats != NULL) {			  \
		unsigned int coffset;					\
		coffset = (unsigned int)((export)->exp_obd->obd_cntr_base) + \
			OBD_COUNTER_OFFSET(op);			      \
		LASSERT(coffset < (export)->exp_obd->obd_stats->ls_num);     \
		lprocfs_counter_incr((export)->exp_obd->obd_stats, coffset); \
		if ((export)->exp_nid_stats != NULL &&		       \
		    (export)->exp_nid_stats->nid_stats != NULL)	      \
			lprocfs_counter_incr(				\
				(export)->exp_nid_stats->nid_stats, coffset);\
	}

#define MD_COUNTER_OFFSET(op)				   \
	((offsetof(struct md_ops, m_ ## op) -		   \
	  offsetof(struct md_ops, m_getstatus))		 \
	 / sizeof(((struct md_ops *)(0))->m_getstatus))

#define MD_COUNTER_INCREMENT(obdx, op)			   \
	if ((obd)->md_stats != NULL) {			   \
		unsigned int coffset;			    \
		coffset = (unsigned int)((obdx)->md_cntr_base) + \
			MD_COUNTER_OFFSET(op);		   \
		LASSERT(coffset < (obdx)->md_stats->ls_num);     \
		lprocfs_counter_incr((obdx)->md_stats, coffset); \
	}

#define EXP_MD_COUNTER_INCREMENT(export, op)				 \
	if ((export)->exp_obd->obd_stats != NULL) {			  \
		unsigned int coffset;					\
		coffset = (unsigned int)((export)->exp_obd->md_cntr_base) +  \
			MD_COUNTER_OFFSET(op);			       \
		LASSERT(coffset < (export)->exp_obd->md_stats->ls_num);      \
		lprocfs_counter_incr((export)->exp_obd->md_stats, coffset);  \
		if ((export)->exp_md_stats != NULL)			  \
			lprocfs_counter_incr(				\
				(export)->exp_md_stats, coffset);	    \
	}

#else
#define OBD_COUNTER_OFFSET(op)
#define OBD_COUNTER_INCREMENT(obd, op)
#define EXP_COUNTER_INCREMENT(exp, op)
#define MD_COUNTER_INCREMENT(obd, op)
#define EXP_MD_COUNTER_INCREMENT(exp, op)
#endif

static inline int lprocfs_nid_ldlm_stats_init(struct nid_stat* tmp)
{
	/* Always add in ldlm_stats */
	tmp->nid_ldlm_stats = lprocfs_alloc_stats(LDLM_LAST_OPC - LDLM_FIRST_OPC
						  ,LPROCFS_STATS_FLAG_NOPERCPU);
	if (tmp->nid_ldlm_stats == NULL)
		return -ENOMEM;

	lprocfs_init_ldlm_stats(tmp->nid_ldlm_stats);

	return lprocfs_register_stats(tmp->nid_proc, "ldlm_stats",
				      tmp->nid_ldlm_stats);
}

#define OBD_CHECK_MD_OP(obd, op, err)			   \
do {							    \
	if (!OBT(obd) || !MDP((obd), op)) {		     \
		if (err)					\
			CERROR("md_" #op ": dev %s/%d no operation\n", \
			       obd->obd_name, obd->obd_minor);  \
		return err;				    \
	}						       \
} while (0)

#define EXP_CHECK_MD_OP(exp, op)				\
do {							    \
	if ((exp) == NULL) {				    \
		CERROR("obd_" #op ": NULL export\n");	   \
		return -ENODEV;				\
	}						       \
	if ((exp)->exp_obd == NULL || !OBT((exp)->exp_obd)) {   \
		CERROR("obd_" #op ": cleaned up obd\n");	\
		return -EOPNOTSUPP;			    \
	}						       \
	if (!OBT((exp)->exp_obd) || !MDP((exp)->exp_obd, op)) { \
		CERROR("obd_" #op ": dev %s/%d no operation\n", \
		       (exp)->exp_obd->obd_name,		\
		       (exp)->exp_obd->obd_minor);	      \
		return -EOPNOTSUPP;			    \
	}						       \
} while (0)


#define OBD_CHECK_DT_OP(obd, op, err)			   \
do {							    \
	if (!OBT(obd) || !OBP((obd), op)) {		     \
		if (err)					\
			CERROR("obd_" #op ": dev %d no operation\n",    \
			       obd->obd_minor);		 \
		return err;				    \
	}						       \
} while (0)

#define EXP_CHECK_DT_OP(exp, op)				\
do {							    \
	if ((exp) == NULL) {				    \
		CERROR("obd_" #op ": NULL export\n");	   \
		return -ENODEV;				\
	}						       \
	if ((exp)->exp_obd == NULL || !OBT((exp)->exp_obd)) {   \
		CERROR("obd_" #op ": cleaned up obd\n");	\
		return -EOPNOTSUPP;			    \
	}						       \
	if (!OBT((exp)->exp_obd) || !OBP((exp)->exp_obd, op)) { \
		CERROR("obd_" #op ": dev %d no operation\n",    \
		       (exp)->exp_obd->obd_minor);	      \
		return -EOPNOTSUPP;			    \
	}						       \
} while (0)

#define CTXT_CHECK_OP(ctxt, op, err)				 \
do {								 \
	if (!OBT(ctxt->loc_obd) || !CTXTP((ctxt), op)) {	     \
		if (err)					     \
			CERROR("lop_" #op ": dev %d no operation\n", \
			       ctxt->loc_obd->obd_minor);	    \
		return err;					 \
	}							    \
} while (0)

static inline int class_devno_max(void)
{
	return MAX_OBD_DEVICES;
}

static inline int obd_get_info(const struct lu_env *env,
			       struct obd_export *exp, __u32 keylen,
			       void *key, __u32 *vallen, void *val,
			       struct lov_stripe_md *lsm)
{
	int rc;

	EXP_CHECK_DT_OP(exp, get_info);
	EXP_COUNTER_INCREMENT(exp, get_info);

	rc = OBP(exp->exp_obd, get_info)(env, exp, keylen, key, vallen, val,
					 lsm);
	return rc;
}

static inline int obd_set_info_async(const struct lu_env *env,
				     struct obd_export *exp, obd_count keylen,
				     void *key, obd_count vallen, void *val,
				     struct ptlrpc_request_set *set)
{
	int rc;

	EXP_CHECK_DT_OP(exp, set_info_async);
	EXP_COUNTER_INCREMENT(exp, set_info_async);

	rc = OBP(exp->exp_obd, set_info_async)(env, exp, keylen, key, vallen,
					       val, set);
	return rc;
}

/*
 * obd-lu integration.
 *
 * Functionality is being moved into new lu_device-based layering, but some
 * pieces of configuration process are still based on obd devices.
 *
 * Specifically, lu_device_type_operations::ldto_device_alloc() methods fully
 * subsume ->o_setup() methods of obd devices they replace. The same for
 * lu_device_operations::ldo_process_config() and ->o_process_config(). As a
 * result, obd_setup() and obd_process_config() branch and call one XOR
 * another.
 *
 * Yet neither lu_device_type_operations::ldto_device_fini() nor
 * lu_device_type_operations::ldto_device_free() fully implement the
 * functionality of ->o_precleanup() and ->o_cleanup() they override. Hence,
 * obd_precleanup() and obd_cleanup() call both lu_device and obd operations.
 */

#define DECLARE_LU_VARS(ldt, d)		 \
	struct lu_device_type *ldt;       \
	struct lu_device *d

static inline int obd_setup(struct obd_device *obd, struct lustre_cfg *cfg)
{
	int rc;
	DECLARE_LU_VARS(ldt, d);

	ldt = obd->obd_type->typ_lu;
	if (ldt != NULL) {
		struct lu_context  session_ctx;
		struct lu_env env;
		lu_context_init(&session_ctx, LCT_SESSION);
		session_ctx.lc_thread = NULL;
		lu_context_enter(&session_ctx);

		rc = lu_env_init(&env, ldt->ldt_ctx_tags);
		if (rc == 0) {
			env.le_ses = &session_ctx;
			d = ldt->ldt_ops->ldto_device_alloc(&env, ldt, cfg);
			lu_env_fini(&env);
			if (!IS_ERR(d)) {
				obd->obd_lu_dev = d;
				d->ld_obd = obd;
				rc = 0;
			} else
				rc = PTR_ERR(d);
		}
		lu_context_exit(&session_ctx);
		lu_context_fini(&session_ctx);

	} else {
		OBD_CHECK_DT_OP(obd, setup, -EOPNOTSUPP);
		OBD_COUNTER_INCREMENT(obd, setup);
		rc = OBP(obd, setup)(obd, cfg);
	}
	return rc;
}

static inline int obd_precleanup(struct obd_device *obd,
				 enum obd_cleanup_stage cleanup_stage)
{
	int rc;
	DECLARE_LU_VARS(ldt, d);

	OBD_CHECK_DEV(obd);
	ldt = obd->obd_type->typ_lu;
	d = obd->obd_lu_dev;
	if (ldt != NULL && d != NULL) {
		if (cleanup_stage == OBD_CLEANUP_EXPORTS) {
			struct lu_env env;

			rc = lu_env_init(&env, ldt->ldt_ctx_tags);
			if (rc == 0) {
				ldt->ldt_ops->ldto_device_fini(&env, d);
				lu_env_fini(&env);
			}
		}
	}
	OBD_CHECK_DT_OP(obd, precleanup, 0);
	OBD_COUNTER_INCREMENT(obd, precleanup);

	rc = OBP(obd, precleanup)(obd, cleanup_stage);
	return rc;
}

static inline int obd_cleanup(struct obd_device *obd)
{
	int rc;
	DECLARE_LU_VARS(ldt, d);

	OBD_CHECK_DEV(obd);

	ldt = obd->obd_type->typ_lu;
	d = obd->obd_lu_dev;
	if (ldt != NULL && d != NULL) {
		struct lu_env env;

		rc = lu_env_init(&env, ldt->ldt_ctx_tags);
		if (rc == 0) {
			ldt->ldt_ops->ldto_device_free(&env, d);
			lu_env_fini(&env);
			obd->obd_lu_dev = NULL;
		}
	}
	OBD_CHECK_DT_OP(obd, cleanup, 0);
	OBD_COUNTER_INCREMENT(obd, cleanup);

	rc = OBP(obd, cleanup)(obd);
	return rc;
}

static inline void obd_cleanup_client_import(struct obd_device *obd)
{
	/* If we set up but never connected, the
	   client import will not have been cleaned. */
	down_write(&obd->u.cli.cl_sem);
	if (obd->u.cli.cl_import) {
		struct obd_import *imp;
		imp = obd->u.cli.cl_import;
		CDEBUG(D_CONFIG, "%s: client import never connected\n",
		       obd->obd_name);
		ptlrpc_invalidate_import(imp);
		if (imp->imp_rq_pool) {
			ptlrpc_free_rq_pool(imp->imp_rq_pool);
			imp->imp_rq_pool = NULL;
		}
		client_destroy_import(imp);
		obd->u.cli.cl_import = NULL;
	}
	up_write(&obd->u.cli.cl_sem);
}

static inline int
obd_process_config(struct obd_device *obd, int datalen, void *data)
{
	int rc;
	DECLARE_LU_VARS(ldt, d);

	OBD_CHECK_DEV(obd);

	obd->obd_process_conf = 1;
	ldt = obd->obd_type->typ_lu;
	d = obd->obd_lu_dev;
	if (ldt != NULL && d != NULL) {
		struct lu_env env;

		rc = lu_env_init(&env, ldt->ldt_ctx_tags);
		if (rc == 0) {
			rc = d->ld_ops->ldo_process_config(&env, d, data);
			lu_env_fini(&env);
		}
	} else {
		OBD_CHECK_DT_OP(obd, process_config, -EOPNOTSUPP);
		rc = OBP(obd, process_config)(obd, datalen, data);
	}
	OBD_COUNTER_INCREMENT(obd, process_config);
	obd->obd_process_conf = 0;

	return rc;
}

/* Pack an in-memory MD struct for storage on disk.
 * Returns +ve size of packed MD (0 for free), or -ve error.
 *
 * If @disk_tgt == NULL, MD size is returned (max size if @mem_src == NULL).
 * If @*disk_tgt != NULL and @mem_src == NULL, @*disk_tgt will be freed.
 * If @*disk_tgt == NULL, it will be allocated
 */
static inline int obd_packmd(struct obd_export *exp,
			     struct lov_mds_md **disk_tgt,
			     struct lov_stripe_md *mem_src)
{
	int rc;

	EXP_CHECK_DT_OP(exp, packmd);
	EXP_COUNTER_INCREMENT(exp, packmd);

	rc = OBP(exp->exp_obd, packmd)(exp, disk_tgt, mem_src);
	return rc;
}

static inline int obd_size_diskmd(struct obd_export *exp,
				  struct lov_stripe_md *mem_src)
{
	return obd_packmd(exp, NULL, mem_src);
}

/* helper functions */
static inline int obd_alloc_diskmd(struct obd_export *exp,
				   struct lov_mds_md **disk_tgt)
{
	LASSERT(disk_tgt);
	LASSERT(*disk_tgt == NULL);
	return obd_packmd(exp, disk_tgt, NULL);
}

static inline int obd_free_diskmd(struct obd_export *exp,
				  struct lov_mds_md **disk_tgt)
{
	LASSERT(disk_tgt);
	LASSERT(*disk_tgt);
	/*
	 * LU-2590, for caller's convenience, *disk_tgt could be host
	 * endianness, it needs swab to LE if necessary, while just
	 * lov_mds_md header needs it for figuring out how much memory
	 * needs to be freed.
	 */
	if ((cpu_to_le32(LOV_MAGIC) != LOV_MAGIC) &&
	    (((*disk_tgt)->lmm_magic == LOV_MAGIC_V1) ||
	     ((*disk_tgt)->lmm_magic == LOV_MAGIC_V3)))
		lustre_swab_lov_mds_md(*disk_tgt);
	return obd_packmd(exp, disk_tgt, NULL);
}

/* Unpack an MD struct from disk to in-memory format.
 * Returns +ve size of unpacked MD (0 for free), or -ve error.
 *
 * If @mem_tgt == NULL, MD size is returned (max size if @disk_src == NULL).
 * If @*mem_tgt != NULL and @disk_src == NULL, @*mem_tgt will be freed.
 * If @*mem_tgt == NULL, it will be allocated
 */
static inline int obd_unpackmd(struct obd_export *exp,
			       struct lov_stripe_md **mem_tgt,
			       struct lov_mds_md *disk_src,
			       int disk_len)
{
	int rc;

	EXP_CHECK_DT_OP(exp, unpackmd);
	EXP_COUNTER_INCREMENT(exp, unpackmd);

	rc = OBP(exp->exp_obd, unpackmd)(exp, mem_tgt, disk_src, disk_len);
	return rc;
}

/* helper functions */
static inline int obd_alloc_memmd(struct obd_export *exp,
				  struct lov_stripe_md **mem_tgt)
{
	LASSERT(mem_tgt);
	LASSERT(*mem_tgt == NULL);
	return obd_unpackmd(exp, mem_tgt, NULL, 0);
}

static inline int obd_free_memmd(struct obd_export *exp,
				 struct lov_stripe_md **mem_tgt)
{
	int rc;

	LASSERT(mem_tgt);
	LASSERT(*mem_tgt);
	rc = obd_unpackmd(exp, mem_tgt, NULL, 0);
	*mem_tgt = NULL;
	return rc;
}

static inline int obd_precreate(struct obd_export *exp)
{
	int rc;

	EXP_CHECK_DT_OP(exp, precreate);
	OBD_COUNTER_INCREMENT(exp->exp_obd, precreate);

	rc = OBP(exp->exp_obd, precreate)(exp);
	return rc;
}

static inline int obd_create_async(struct obd_export *exp,
				   struct obd_info *oinfo,
				   struct lov_stripe_md **ea,
				   struct obd_trans_info *oti)
{
	int rc;

	EXP_CHECK_DT_OP(exp, create_async);
	EXP_COUNTER_INCREMENT(exp, create_async);

	rc = OBP(exp->exp_obd, create_async)(exp, oinfo, ea, oti);
	return rc;
}

static inline int obd_create(const struct lu_env *env, struct obd_export *exp,
			     struct obdo *obdo, struct lov_stripe_md **ea,
			     struct obd_trans_info *oti)
{
	int rc;

	EXP_CHECK_DT_OP(exp, create);
	EXP_COUNTER_INCREMENT(exp, create);

	rc = OBP(exp->exp_obd, create)(env, exp, obdo, ea, oti);
	return rc;
}

static inline int obd_destroy(const struct lu_env *env, struct obd_export *exp,
			      struct obdo *obdo, struct lov_stripe_md *ea,
			      struct obd_trans_info *oti,
			      struct obd_export *md_exp, void *capa)
{
	int rc;

	EXP_CHECK_DT_OP(exp, destroy);
	EXP_COUNTER_INCREMENT(exp, destroy);

	rc = OBP(exp->exp_obd, destroy)(env, exp, obdo, ea, oti, md_exp, capa);
	return rc;
}

static inline int obd_getattr(const struct lu_env *env, struct obd_export *exp,
			      struct obd_info *oinfo)
{
	int rc;

	EXP_CHECK_DT_OP(exp, getattr);
	EXP_COUNTER_INCREMENT(exp, getattr);

	rc = OBP(exp->exp_obd, getattr)(env, exp, oinfo);
	return rc;
}

static inline int obd_getattr_async(struct obd_export *exp,
				    struct obd_info *oinfo,
				    struct ptlrpc_request_set *set)
{
	int rc;

	EXP_CHECK_DT_OP(exp, getattr_async);
	EXP_COUNTER_INCREMENT(exp, getattr_async);

	rc = OBP(exp->exp_obd, getattr_async)(exp, oinfo, set);
	return rc;
}

static inline int obd_setattr(const struct lu_env *env, struct obd_export *exp,
			      struct obd_info *oinfo,
			      struct obd_trans_info *oti)
{
	int rc;

	EXP_CHECK_DT_OP(exp, setattr);
	EXP_COUNTER_INCREMENT(exp, setattr);

	rc = OBP(exp->exp_obd, setattr)(env, exp, oinfo, oti);
	return rc;
}

/* This performs all the requests set init/wait/destroy actions. */
static inline int obd_setattr_rqset(struct obd_export *exp,
				    struct obd_info *oinfo,
				    struct obd_trans_info *oti)
{
	struct ptlrpc_request_set *set = NULL;
	int rc;

	EXP_CHECK_DT_OP(exp, setattr_async);
	EXP_COUNTER_INCREMENT(exp, setattr_async);

	set =  ptlrpc_prep_set();
	if (set == NULL)
		return -ENOMEM;

	rc = OBP(exp->exp_obd, setattr_async)(exp, oinfo, oti, set);
	if (rc == 0)
		rc = ptlrpc_set_wait(set);
	ptlrpc_set_destroy(set);
	return rc;
}

/* This adds all the requests into @set if @set != NULL, otherwise
   all requests are sent asynchronously without waiting for response. */
static inline int obd_setattr_async(struct obd_export *exp,
				    struct obd_info *oinfo,
				    struct obd_trans_info *oti,
				    struct ptlrpc_request_set *set)
{
	int rc;

	EXP_CHECK_DT_OP(exp, setattr_async);
	EXP_COUNTER_INCREMENT(exp, setattr_async);

	rc = OBP(exp->exp_obd, setattr_async)(exp, oinfo, oti, set);
	return rc;
}

static inline int obd_add_conn(struct obd_import *imp, struct obd_uuid *uuid,
			       int priority)
{
	struct obd_device *obd = imp->imp_obd;
	int rc;

	OBD_CHECK_DEV_ACTIVE(obd);
	OBD_CHECK_DT_OP(obd, add_conn, -EOPNOTSUPP);
	OBD_COUNTER_INCREMENT(obd, add_conn);

	rc = OBP(obd, add_conn)(imp, uuid, priority);
	return rc;
}

static inline int obd_del_conn(struct obd_import *imp, struct obd_uuid *uuid)
{
	struct obd_device *obd = imp->imp_obd;
	int rc;

	OBD_CHECK_DEV_ACTIVE(obd);
	OBD_CHECK_DT_OP(obd, del_conn, -EOPNOTSUPP);
	OBD_COUNTER_INCREMENT(obd, del_conn);

	rc = OBP(obd, del_conn)(imp, uuid);
	return rc;
}

static inline struct obd_uuid *obd_get_uuid(struct obd_export *exp)
{
	struct obd_uuid *uuid;

	OBD_CHECK_DT_OP(exp->exp_obd, get_uuid, NULL);
	EXP_COUNTER_INCREMENT(exp, get_uuid);

	uuid = OBP(exp->exp_obd, get_uuid)(exp);
	return uuid;
}

/** Create a new /a exp on device /a obd for the uuid /a cluuid
 * @param exp New export handle
 * @param d Connect data, supported flags are set, flags also understood
 *    by obd are returned.
 */
static inline int obd_connect(const struct lu_env *env,
			      struct obd_export **exp,struct obd_device *obd,
			      struct obd_uuid *cluuid,
			      struct obd_connect_data *data,
			      void *localdata)
{
	int rc;
	__u64 ocf = data ? data->ocd_connect_flags : 0; /* for post-condition
						   * check */

	OBD_CHECK_DEV_ACTIVE(obd);
	OBD_CHECK_DT_OP(obd, connect, -EOPNOTSUPP);
	OBD_COUNTER_INCREMENT(obd, connect);

	rc = OBP(obd, connect)(env, exp, obd, cluuid, data, localdata);
	/* check that only subset is granted */
	LASSERT(ergo(data != NULL, (data->ocd_connect_flags & ocf) ==
				    data->ocd_connect_flags));
	return rc;
}

static inline int obd_reconnect(const struct lu_env *env,
				struct obd_export *exp,
				struct obd_device *obd,
				struct obd_uuid *cluuid,
				struct obd_connect_data *d,
				void *localdata)
{
	int rc;
	__u64 ocf = d ? d->ocd_connect_flags : 0; /* for post-condition
						   * check */

	OBD_CHECK_DEV_ACTIVE(obd);
	OBD_CHECK_DT_OP(obd, reconnect, 0);
	OBD_COUNTER_INCREMENT(obd, reconnect);

	rc = OBP(obd, reconnect)(env, exp, obd, cluuid, d, localdata);
	/* check that only subset is granted */
	LASSERT(ergo(d != NULL,
		     (d->ocd_connect_flags & ocf) == d->ocd_connect_flags));
	return rc;
}

static inline int obd_disconnect(struct obd_export *exp)
{
	int rc;

	EXP_CHECK_DT_OP(exp, disconnect);
	EXP_COUNTER_INCREMENT(exp, disconnect);

	rc = OBP(exp->exp_obd, disconnect)(exp);
	return rc;
}

static inline int obd_fid_init(struct obd_device *obd, struct obd_export *exp,
			       enum lu_cli_type type)
{
	int rc;

	OBD_CHECK_DT_OP(obd, fid_init, 0);
	OBD_COUNTER_INCREMENT(obd, fid_init);

	rc = OBP(obd, fid_init)(obd, exp, type);
	return rc;
}

static inline int obd_fid_fini(struct obd_device *obd)
{
	int rc;

	OBD_CHECK_DT_OP(obd, fid_fini, 0);
	OBD_COUNTER_INCREMENT(obd, fid_fini);

	rc = OBP(obd, fid_fini)(obd);
	return rc;
}

static inline int obd_fid_alloc(struct obd_export *exp,
				struct lu_fid *fid,
				struct md_op_data *op_data)
{
	int rc;

	EXP_CHECK_DT_OP(exp, fid_alloc);
	EXP_COUNTER_INCREMENT(exp, fid_alloc);

	rc = OBP(exp->exp_obd, fid_alloc)(exp, fid, op_data);
	return rc;
}

static inline int obd_ping(const struct lu_env *env, struct obd_export *exp)
{
	int rc;

	OBD_CHECK_DT_OP(exp->exp_obd, ping, 0);
	EXP_COUNTER_INCREMENT(exp, ping);

	rc = OBP(exp->exp_obd, ping)(env, exp);
	return rc;
}

static inline int obd_pool_new(struct obd_device *obd, char *poolname)
{
	int rc;

	OBD_CHECK_DT_OP(obd, pool_new, -EOPNOTSUPP);
	OBD_COUNTER_INCREMENT(obd, pool_new);

	rc = OBP(obd, pool_new)(obd, poolname);
	return rc;
}

static inline int obd_pool_del(struct obd_device *obd, char *poolname)
{
	int rc;

	OBD_CHECK_DT_OP(obd, pool_del, -EOPNOTSUPP);
	OBD_COUNTER_INCREMENT(obd, pool_del);

	rc = OBP(obd, pool_del)(obd, poolname);
	return rc;
}

static inline int obd_pool_add(struct obd_device *obd, char *poolname, char *ostname)
{
	int rc;

	OBD_CHECK_DT_OP(obd, pool_add, -EOPNOTSUPP);
	OBD_COUNTER_INCREMENT(obd, pool_add);

	rc = OBP(obd, pool_add)(obd, poolname, ostname);
	return rc;
}

static inline int obd_pool_rem(struct obd_device *obd, char *poolname, char *ostname)
{
	int rc;

	OBD_CHECK_DT_OP(obd, pool_rem, -EOPNOTSUPP);
	OBD_COUNTER_INCREMENT(obd, pool_rem);

	rc = OBP(obd, pool_rem)(obd, poolname, ostname);
	return rc;
}

static inline void obd_getref(struct obd_device *obd)
{
	if (OBT(obd) && OBP(obd, getref)) {
		OBD_COUNTER_INCREMENT(obd, getref);
		OBP(obd, getref)(obd);
	}
}

static inline void obd_putref(struct obd_device *obd)
{
	if (OBT(obd) && OBP(obd, putref)) {
		OBD_COUNTER_INCREMENT(obd, putref);
		OBP(obd, putref)(obd);
	}
}

static inline int obd_init_export(struct obd_export *exp)
{
	int rc = 0;

	if ((exp)->exp_obd != NULL && OBT((exp)->exp_obd) &&
	    OBP((exp)->exp_obd, init_export))
		rc = OBP(exp->exp_obd, init_export)(exp);
	return rc;
}

static inline int obd_destroy_export(struct obd_export *exp)
{
	if ((exp)->exp_obd != NULL && OBT((exp)->exp_obd) &&
	    OBP((exp)->exp_obd, destroy_export))
		OBP(exp->exp_obd, destroy_export)(exp);
	return 0;
}

static inline int obd_extent_calc(struct obd_export *exp,
				  struct lov_stripe_md *md,
				  int cmd, obd_off *offset)
{
	int rc;

	EXP_CHECK_DT_OP(exp, extent_calc);
	rc = OBP(exp->exp_obd, extent_calc)(exp, md, cmd, offset);
	return rc;
}

static inline struct dentry *
obd_lvfs_fid2dentry(struct obd_export *exp, struct ost_id *oi, __u32 gen)
{
	struct lvfs_run_ctxt *ctxt = &exp->exp_obd->obd_lvfs_ctxt;
	LASSERT(exp->exp_obd);

	return ctxt->cb_ops.l_fid2dentry(ostid_id(oi), gen, ostid_seq(oi),
					 exp->exp_obd);
}

/* @max_age is the oldest time in jiffies that we accept using a cached data.
 * If the cache is older than @max_age we will get a new value from the
 * target.  Use a value of "cfs_time_current() + HZ" to guarantee freshness. */
static inline int obd_statfs_async(struct obd_export *exp,
				   struct obd_info *oinfo,
				   __u64 max_age,
				   struct ptlrpc_request_set *rqset)
{
	int rc = 0;
	struct obd_device *obd;

	if (exp == NULL || exp->exp_obd == NULL)
		return -EINVAL;

	obd = exp->exp_obd;
	OBD_CHECK_DT_OP(obd, statfs, -EOPNOTSUPP);
	OBD_COUNTER_INCREMENT(obd, statfs);

	CDEBUG(D_SUPER, "%s: osfs %p age "LPU64", max_age "LPU64"\n",
	       obd->obd_name, &obd->obd_osfs, obd->obd_osfs_age, max_age);
	if (cfs_time_before_64(obd->obd_osfs_age, max_age)) {
		rc = OBP(obd, statfs_async)(exp, oinfo, max_age, rqset);
	} else {
		CDEBUG(D_SUPER,"%s: use %p cache blocks "LPU64"/"LPU64
		       " objects "LPU64"/"LPU64"\n",
		       obd->obd_name, &obd->obd_osfs,
		       obd->obd_osfs.os_bavail, obd->obd_osfs.os_blocks,
		       obd->obd_osfs.os_ffree, obd->obd_osfs.os_files);
		spin_lock(&obd->obd_osfs_lock);
		memcpy(oinfo->oi_osfs, &obd->obd_osfs, sizeof(*oinfo->oi_osfs));
		spin_unlock(&obd->obd_osfs_lock);
		oinfo->oi_flags |= OBD_STATFS_FROM_CACHE;
		if (oinfo->oi_cb_up)
			oinfo->oi_cb_up(oinfo, 0);
	}
	return rc;
}

static inline int obd_statfs_rqset(struct obd_export *exp,
				   struct obd_statfs *osfs, __u64 max_age,
				   __u32 flags)
{
	struct ptlrpc_request_set *set = NULL;
	struct obd_info oinfo = { { { 0 } } };
	int rc = 0;

	set =  ptlrpc_prep_set();
	if (set == NULL)
		return -ENOMEM;

	oinfo.oi_osfs = osfs;
	oinfo.oi_flags = flags;
	rc = obd_statfs_async(exp, &oinfo, max_age, set);
	if (rc == 0)
		rc = ptlrpc_set_wait(set);
	ptlrpc_set_destroy(set);
	return rc;
}

/* @max_age is the oldest time in jiffies that we accept using a cached data.
 * If the cache is older than @max_age we will get a new value from the
 * target.  Use a value of "cfs_time_current() + HZ" to guarantee freshness. */
static inline int obd_statfs(const struct lu_env *env, struct obd_export *exp,
			     struct obd_statfs *osfs, __u64 max_age,
			     __u32 flags)
{
	int rc = 0;
	struct obd_device *obd = exp->exp_obd;

	if (obd == NULL)
		return -EINVAL;

	OBD_CHECK_DT_OP(obd, statfs, -EOPNOTSUPP);
	OBD_COUNTER_INCREMENT(obd, statfs);

	CDEBUG(D_SUPER, "osfs "LPU64", max_age "LPU64"\n",
	       obd->obd_osfs_age, max_age);
	if (cfs_time_before_64(obd->obd_osfs_age, max_age)) {
		rc = OBP(obd, statfs)(env, exp, osfs, max_age, flags);
		if (rc == 0) {
			spin_lock(&obd->obd_osfs_lock);
			memcpy(&obd->obd_osfs, osfs, sizeof(obd->obd_osfs));
			obd->obd_osfs_age = cfs_time_current_64();
			spin_unlock(&obd->obd_osfs_lock);
		}
	} else {
		CDEBUG(D_SUPER, "%s: use %p cache blocks "LPU64"/"LPU64
		       " objects "LPU64"/"LPU64"\n",
		       obd->obd_name, &obd->obd_osfs,
		       obd->obd_osfs.os_bavail, obd->obd_osfs.os_blocks,
		       obd->obd_osfs.os_ffree, obd->obd_osfs.os_files);
		spin_lock(&obd->obd_osfs_lock);
		memcpy(osfs, &obd->obd_osfs, sizeof(*osfs));
		spin_unlock(&obd->obd_osfs_lock);
	}
	return rc;
}

static inline int obd_sync_rqset(struct obd_export *exp, struct obd_info *oinfo,
				 obd_size start, obd_size end)
{
	struct ptlrpc_request_set *set = NULL;
	int rc;

	OBD_CHECK_DT_OP(exp->exp_obd, sync, -EOPNOTSUPP);
	EXP_COUNTER_INCREMENT(exp, sync);

	set =  ptlrpc_prep_set();
	if (set == NULL)
		return -ENOMEM;

	rc = OBP(exp->exp_obd, sync)(NULL, exp, oinfo, start, end, set);
	if (rc == 0)
		rc = ptlrpc_set_wait(set);
	ptlrpc_set_destroy(set);
	return rc;
}

static inline int obd_sync(const struct lu_env *env, struct obd_export *exp,
			   struct obd_info *oinfo, obd_size start, obd_size end,
			   struct ptlrpc_request_set *set)
{
	int rc;

	OBD_CHECK_DT_OP(exp->exp_obd, sync, -EOPNOTSUPP);
	EXP_COUNTER_INCREMENT(exp, sync);

	rc = OBP(exp->exp_obd, sync)(env, exp, oinfo, start, end, set);
	return rc;
}

static inline int obd_punch_rqset(struct obd_export *exp,
				  struct obd_info *oinfo,
				  struct obd_trans_info *oti)
{
	struct ptlrpc_request_set *set = NULL;
	int rc;

	EXP_CHECK_DT_OP(exp, punch);
	EXP_COUNTER_INCREMENT(exp, punch);

	set =  ptlrpc_prep_set();
	if (set == NULL)
		return -ENOMEM;

	rc = OBP(exp->exp_obd, punch)(NULL, exp, oinfo, oti, set);
	if (rc == 0)
		rc = ptlrpc_set_wait(set);
	ptlrpc_set_destroy(set);
	return rc;
}

static inline int obd_punch(const struct lu_env *env, struct obd_export *exp,
			    struct obd_info *oinfo, struct obd_trans_info *oti,
			    struct ptlrpc_request_set *rqset)
{
	int rc;

	EXP_CHECK_DT_OP(exp, punch);
	EXP_COUNTER_INCREMENT(exp, punch);

	rc = OBP(exp->exp_obd, punch)(env, exp, oinfo, oti, rqset);
	return rc;
}

static inline int obd_brw(int cmd, struct obd_export *exp,
			  struct obd_info *oinfo, obd_count oa_bufs,
			  struct brw_page *pg, struct obd_trans_info *oti)
{
	int rc;

	EXP_CHECK_DT_OP(exp, brw);
	EXP_COUNTER_INCREMENT(exp, brw);

	if (!(cmd & (OBD_BRW_RWMASK | OBD_BRW_CHECK))) {
		CERROR("obd_brw: cmd must be OBD_BRW_READ, OBD_BRW_WRITE, "
		       "or OBD_BRW_CHECK\n");
		LBUG();
	}

	rc = OBP(exp->exp_obd, brw)(cmd, exp, oinfo, oa_bufs, pg, oti);
	return rc;
}

static inline int obd_preprw(const struct lu_env *env, int cmd,
			     struct obd_export *exp, struct obdo *oa,
			     int objcount, struct obd_ioobj *obj,
			     struct niobuf_remote *remote, int *pages,
			     struct niobuf_local *local,
			     struct obd_trans_info *oti,
			     struct lustre_capa *capa)
{
	int rc;

	EXP_CHECK_DT_OP(exp, preprw);
	EXP_COUNTER_INCREMENT(exp, preprw);

	rc = OBP(exp->exp_obd, preprw)(env, cmd, exp, oa, objcount, obj, remote,
				       pages, local, oti, capa);
	return rc;
}

static inline int obd_commitrw(const struct lu_env *env, int cmd,
			       struct obd_export *exp, struct obdo *oa,
			       int objcount, struct obd_ioobj *obj,
			       struct niobuf_remote *rnb, int pages,
			       struct niobuf_local *local,
			       struct obd_trans_info *oti, int rc)
{
	EXP_CHECK_DT_OP(exp, commitrw);
	EXP_COUNTER_INCREMENT(exp, commitrw);

	rc = OBP(exp->exp_obd, commitrw)(env, cmd, exp, oa, objcount, obj,
					 rnb, pages, local, oti, rc);
	return rc;
}

static inline int obd_merge_lvb(struct obd_export *exp,
				struct lov_stripe_md *lsm,
				struct ost_lvb *lvb, int kms_only)
{
	int rc;

	EXP_CHECK_DT_OP(exp, merge_lvb);
	EXP_COUNTER_INCREMENT(exp, merge_lvb);

	rc = OBP(exp->exp_obd, merge_lvb)(exp, lsm, lvb, kms_only);
	return rc;
}

static inline int obd_adjust_kms(struct obd_export *exp,
				 struct lov_stripe_md *lsm, obd_off size,
				 int shrink)
{
	int rc;

	EXP_CHECK_DT_OP(exp, adjust_kms);
	EXP_COUNTER_INCREMENT(exp, adjust_kms);

	rc = OBP(exp->exp_obd, adjust_kms)(exp, lsm, size, shrink);
	return rc;
}

static inline int obd_iocontrol(unsigned int cmd, struct obd_export *exp,
				int len, void *karg, void *uarg)
{
	int rc;

	EXP_CHECK_DT_OP(exp, iocontrol);
	EXP_COUNTER_INCREMENT(exp, iocontrol);

	rc = OBP(exp->exp_obd, iocontrol)(cmd, exp, len, karg, uarg);
	return rc;
}

static inline int obd_enqueue_rqset(struct obd_export *exp,
				    struct obd_info *oinfo,
				    struct ldlm_enqueue_info *einfo)
{
	struct ptlrpc_request_set *set = NULL;
	int rc;

	EXP_CHECK_DT_OP(exp, enqueue);
	EXP_COUNTER_INCREMENT(exp, enqueue);

	set =  ptlrpc_prep_set();
	if (set == NULL)
		return -ENOMEM;

	rc = OBP(exp->exp_obd, enqueue)(exp, oinfo, einfo, set);
	if (rc == 0)
		rc = ptlrpc_set_wait(set);
	ptlrpc_set_destroy(set);
	return rc;
}

static inline int obd_enqueue(struct obd_export *exp,
			      struct obd_info *oinfo,
			      struct ldlm_enqueue_info *einfo,
			      struct ptlrpc_request_set *set)
{
	int rc;

	EXP_CHECK_DT_OP(exp, enqueue);
	EXP_COUNTER_INCREMENT(exp, enqueue);

	rc = OBP(exp->exp_obd, enqueue)(exp, oinfo, einfo, set);
	return rc;
}

static inline int obd_change_cbdata(struct obd_export *exp,
				    struct lov_stripe_md *lsm,
				    ldlm_iterator_t it, void *data)
{
	int rc;

	EXP_CHECK_DT_OP(exp, change_cbdata);
	EXP_COUNTER_INCREMENT(exp, change_cbdata);

	rc = OBP(exp->exp_obd, change_cbdata)(exp, lsm, it, data);
	return rc;
}

static inline int obd_find_cbdata(struct obd_export *exp,
				  struct lov_stripe_md *lsm,
				  ldlm_iterator_t it, void *data)
{
	int rc;

	EXP_CHECK_DT_OP(exp, find_cbdata);
	EXP_COUNTER_INCREMENT(exp, find_cbdata);

	rc = OBP(exp->exp_obd, find_cbdata)(exp, lsm, it, data);
	return rc;
}

static inline int obd_cancel(struct obd_export *exp,
			     struct lov_stripe_md *ea, __u32 mode,
			     struct lustre_handle *lockh)
{
	int rc;

	EXP_CHECK_DT_OP(exp, cancel);
	EXP_COUNTER_INCREMENT(exp, cancel);

	rc = OBP(exp->exp_obd, cancel)(exp, ea, mode, lockh);
	return rc;
}

static inline int obd_cancel_unused(struct obd_export *exp,
				    struct lov_stripe_md *ea,
				    ldlm_cancel_flags_t flags,
				    void *opaque)
{
	int rc;

	EXP_CHECK_DT_OP(exp, cancel_unused);
	EXP_COUNTER_INCREMENT(exp, cancel_unused);

	rc = OBP(exp->exp_obd, cancel_unused)(exp, ea, flags, opaque);
	return rc;
}

static inline int obd_pin(struct obd_export *exp, const struct lu_fid *fid,
			  struct obd_capa *oc, struct obd_client_handle *handle,
			  int flag)
{
	int rc;

	EXP_CHECK_DT_OP(exp, pin);
	EXP_COUNTER_INCREMENT(exp, pin);

	rc = OBP(exp->exp_obd, pin)(exp, fid, oc, handle, flag);
	return rc;
}

static inline int obd_unpin(struct obd_export *exp,
			    struct obd_client_handle *handle, int flag)
{
	int rc;

	EXP_CHECK_DT_OP(exp, unpin);
	EXP_COUNTER_INCREMENT(exp, unpin);

	rc = OBP(exp->exp_obd, unpin)(exp, handle, flag);
	return rc;
}


static inline void obd_import_event(struct obd_device *obd,
				    struct obd_import *imp,
				    enum obd_import_event event)
{
	if (!obd) {
		CERROR("NULL device\n");
		return;
	}
	if (obd->obd_set_up && OBP(obd, import_event)) {
		OBD_COUNTER_INCREMENT(obd, import_event);
		OBP(obd, import_event)(obd, imp, event);
	}
}

static inline int obd_llog_connect(struct obd_export *exp,
				   struct llogd_conn_body *body)
{
	int rc;

	OBD_CHECK_DT_OP(exp->exp_obd, llog_connect, 0);
	EXP_COUNTER_INCREMENT(exp, llog_connect);

	rc = OBP(exp->exp_obd, llog_connect)(exp, body);
	return rc;
}


static inline int obd_notify(struct obd_device *obd,
			     struct obd_device *watched,
			     enum obd_notify_event ev,
			     void *data)
{
	int rc;

	OBD_CHECK_DEV(obd);

	/* the check for async_recov is a complete hack - I'm hereby
	   overloading the meaning to also mean "this was called from
	   mds_postsetup".  I know that my mds is able to handle notifies
	   by this point, and it needs to get them to execute mds_postrecov. */
	if (!obd->obd_set_up && !obd->obd_async_recov) {
		CDEBUG(D_HA, "obd %s not set up\n", obd->obd_name);
		return -EINVAL;
	}

	if (!OBP(obd, notify)) {
		CDEBUG(D_HA, "obd %s has no notify handler\n", obd->obd_name);
		return -ENOSYS;
	}

	OBD_COUNTER_INCREMENT(obd, notify);
	rc = OBP(obd, notify)(obd, watched, ev, data);
	return rc;
}

static inline int obd_notify_observer(struct obd_device *observer,
				      struct obd_device *observed,
				      enum obd_notify_event ev,
				      void *data)
{
	int rc1;
	int rc2;

	struct obd_notify_upcall *onu;

	if (observer->obd_observer)
		rc1 = obd_notify(observer->obd_observer, observed, ev, data);
	else
		rc1 = 0;
	/*
	 * Also, call non-obd listener, if any
	 */
	onu = &observer->obd_upcall;
	if (onu->onu_upcall != NULL)
		rc2 = onu->onu_upcall(observer, observed, ev,
				      onu->onu_owner, NULL);
	else
		rc2 = 0;

	return rc1 ? rc1 : rc2;
}

static inline int obd_quotacheck(struct obd_export *exp,
				 struct obd_quotactl *oqctl)
{
	int rc;

	EXP_CHECK_DT_OP(exp, quotacheck);
	EXP_COUNTER_INCREMENT(exp, quotacheck);

	rc = OBP(exp->exp_obd, quotacheck)(exp->exp_obd, exp, oqctl);
	return rc;
}

static inline int obd_quotactl(struct obd_export *exp,
			       struct obd_quotactl *oqctl)
{
	int rc;

	EXP_CHECK_DT_OP(exp, quotactl);
	EXP_COUNTER_INCREMENT(exp, quotactl);

	rc = OBP(exp->exp_obd, quotactl)(exp->exp_obd, exp, oqctl);
	return rc;
}

static inline int obd_health_check(const struct lu_env *env,
				   struct obd_device *obd)
{
	/* returns: 0 on healthy
	 *	 >0 on unhealthy + reason code/flag
	 *	    however the only suppored reason == 1 right now
	 *	    We'll need to define some better reasons
	 *	    or flags in the future.
	 *	 <0 on error
	 */
	int rc;

	/* don't use EXP_CHECK_DT_OP, because NULL method is normal here */
	if (obd == NULL || !OBT(obd)) {
		CERROR("cleaned up obd\n");
		return -EOPNOTSUPP;
	}
	if (!obd->obd_set_up || obd->obd_stopping)
		return 0;
	if (!OBP(obd, health_check))
		return 0;

	rc = OBP(obd, health_check)(env, obd);
	return rc;
}

static inline int obd_register_observer(struct obd_device *obd,
					struct obd_device *observer)
{
	OBD_CHECK_DEV(obd);
	down_write(&obd->obd_observer_link_sem);
	if (obd->obd_observer && observer) {
		up_write(&obd->obd_observer_link_sem);
		return -EALREADY;
	}
	obd->obd_observer = observer;
	up_write(&obd->obd_observer_link_sem);
	return 0;
}

static inline int obd_pin_observer(struct obd_device *obd,
				   struct obd_device **observer)
{
	down_read(&obd->obd_observer_link_sem);
	if (!obd->obd_observer) {
		*observer = NULL;
		up_read(&obd->obd_observer_link_sem);
		return -ENOENT;
	}
	*observer = obd->obd_observer;
	return 0;
}

static inline int obd_unpin_observer(struct obd_device *obd)
{
	up_read(&obd->obd_observer_link_sem);
	return 0;
}

#if 0
static inline int obd_register_page_removal_cb(struct obd_export *exp,
					       obd_page_removal_cb_t cb,
					       obd_pin_extent_cb pin_cb)
{
	int rc;

	OBD_CHECK_DT_OP(exp->exp_obd, register_page_removal_cb, 0);
	OBD_COUNTER_INCREMENT(exp->exp_obd, register_page_removal_cb);

	rc = OBP(exp->exp_obd, register_page_removal_cb)(exp, cb, pin_cb);
	return rc;
}

static inline int obd_unregister_page_removal_cb(struct obd_export *exp,
						 obd_page_removal_cb_t cb)
{
	int rc;

	OBD_CHECK_DT_OP(exp->exp_obd, unregister_page_removal_cb, 0);
	OBD_COUNTER_INCREMENT(exp->exp_obd, unregister_page_removal_cb);

	rc = OBP(exp->exp_obd, unregister_page_removal_cb)(exp, cb);
	return rc;
}

static inline int obd_register_lock_cancel_cb(struct obd_export *exp,
					      obd_lock_cancel_cb cb)
{
	int rc;

	OBD_CHECK_DT_OP(exp->exp_obd, register_lock_cancel_cb, 0);
	OBD_COUNTER_INCREMENT(exp->exp_obd, register_lock_cancel_cb);

	rc = OBP(exp->exp_obd, register_lock_cancel_cb)(exp, cb);
	return rc;
}

static inline int obd_unregister_lock_cancel_cb(struct obd_export *exp,
						 obd_lock_cancel_cb cb)
{
	int rc;

	OBD_CHECK_DT_OP(exp->exp_obd, unregister_lock_cancel_cb, 0);
	OBD_COUNTER_INCREMENT(exp->exp_obd, unregister_lock_cancel_cb);

	rc = OBP(exp->exp_obd, unregister_lock_cancel_cb)(exp, cb);
	return rc;
}
#endif

/* metadata helpers */
static inline int md_getstatus(struct obd_export *exp,
			       struct lu_fid *fid, struct obd_capa **pc)
{
	int rc;

	EXP_CHECK_MD_OP(exp, getstatus);
	EXP_MD_COUNTER_INCREMENT(exp, getstatus);
	rc = MDP(exp->exp_obd, getstatus)(exp, fid, pc);
	return rc;
}

static inline int md_getattr(struct obd_export *exp, struct md_op_data *op_data,
			     struct ptlrpc_request **request)
{
	int rc;

	EXP_CHECK_MD_OP(exp, getattr);
	EXP_MD_COUNTER_INCREMENT(exp, getattr);
	rc = MDP(exp->exp_obd, getattr)(exp, op_data, request);
	return rc;
}

static inline int md_null_inode(struct obd_export *exp,
				   const struct lu_fid *fid)
{
	int rc;

	EXP_CHECK_MD_OP(exp, null_inode);
	EXP_MD_COUNTER_INCREMENT(exp, null_inode);
	rc = MDP(exp->exp_obd, null_inode)(exp, fid);
	return rc;
}

static inline int md_find_cbdata(struct obd_export *exp,
				 const struct lu_fid *fid,
				 ldlm_iterator_t it, void *data)
{
	int rc;

	EXP_CHECK_MD_OP(exp, find_cbdata);
	EXP_MD_COUNTER_INCREMENT(exp, find_cbdata);
	rc = MDP(exp->exp_obd, find_cbdata)(exp, fid, it, data);
	return rc;
}

static inline int md_close(struct obd_export *exp, struct md_op_data *op_data,
			   struct md_open_data *mod,
			   struct ptlrpc_request **request)
{
	int rc;

	EXP_CHECK_MD_OP(exp, close);
	EXP_MD_COUNTER_INCREMENT(exp, close);
	rc = MDP(exp->exp_obd, close)(exp, op_data, mod, request);
	return rc;
}

static inline int md_create(struct obd_export *exp, struct md_op_data *op_data,
			    const void *data, int datalen, int mode, __u32 uid,
			    __u32 gid, cfs_cap_t cap_effective, __u64 rdev,
			    struct ptlrpc_request **request)
{
	int rc;

	EXP_CHECK_MD_OP(exp, create);
	EXP_MD_COUNTER_INCREMENT(exp, create);
	rc = MDP(exp->exp_obd, create)(exp, op_data, data, datalen, mode,
				       uid, gid, cap_effective, rdev, request);
	return rc;
}

static inline int md_done_writing(struct obd_export *exp,
				  struct md_op_data *op_data,
				  struct md_open_data *mod)
{
	int rc;

	EXP_CHECK_MD_OP(exp, done_writing);
	EXP_MD_COUNTER_INCREMENT(exp, done_writing);
	rc = MDP(exp->exp_obd, done_writing)(exp, op_data, mod);
	return rc;
}

static inline int md_enqueue(struct obd_export *exp,
			     struct ldlm_enqueue_info *einfo,
			     struct lookup_intent *it,
			     struct md_op_data *op_data,
			     struct lustre_handle *lockh,
			     void *lmm, int lmmsize,
			     struct ptlrpc_request **req,
			     int extra_lock_flags)
{
	int rc;

	EXP_CHECK_MD_OP(exp, enqueue);
	EXP_MD_COUNTER_INCREMENT(exp, enqueue);
	rc = MDP(exp->exp_obd, enqueue)(exp, einfo, it, op_data, lockh,
					lmm, lmmsize, req, extra_lock_flags);
	return rc;
}

static inline int md_getattr_name(struct obd_export *exp,
				  struct md_op_data *op_data,
				  struct ptlrpc_request **request)
{
	int rc;

	EXP_CHECK_MD_OP(exp, getattr_name);
	EXP_MD_COUNTER_INCREMENT(exp, getattr_name);
	rc = MDP(exp->exp_obd, getattr_name)(exp, op_data, request);
	return rc;
}

static inline int md_intent_lock(struct obd_export *exp,
				 struct md_op_data *op_data, void *lmm,
				 int lmmsize, struct lookup_intent *it,
				 int lookup_flags, struct ptlrpc_request **reqp,
				 ldlm_blocking_callback cb_blocking,
				 __u64 extra_lock_flags)
{
	int rc;

	EXP_CHECK_MD_OP(exp, intent_lock);
	EXP_MD_COUNTER_INCREMENT(exp, intent_lock);
	rc = MDP(exp->exp_obd, intent_lock)(exp, op_data, lmm, lmmsize,
					    it, lookup_flags, reqp, cb_blocking,
					    extra_lock_flags);
	return rc;
}

static inline int md_link(struct obd_export *exp, struct md_op_data *op_data,
			  struct ptlrpc_request **request)
{
	int rc;

	EXP_CHECK_MD_OP(exp, link);
	EXP_MD_COUNTER_INCREMENT(exp, link);
	rc = MDP(exp->exp_obd, link)(exp, op_data, request);
	return rc;
}

static inline int md_rename(struct obd_export *exp, struct md_op_data *op_data,
			    const char *old, int oldlen, const char *new,
			    int newlen, struct ptlrpc_request **request)
{
	int rc;

	EXP_CHECK_MD_OP(exp, rename);
	EXP_MD_COUNTER_INCREMENT(exp, rename);
	rc = MDP(exp->exp_obd, rename)(exp, op_data, old, oldlen, new,
				       newlen, request);
	return rc;
}

static inline int md_is_subdir(struct obd_export *exp,
			       const struct lu_fid *pfid,
			       const struct lu_fid *cfid,
			       struct ptlrpc_request **request)
{
	int rc;

	EXP_CHECK_MD_OP(exp, is_subdir);
	EXP_MD_COUNTER_INCREMENT(exp, is_subdir);
	rc = MDP(exp->exp_obd, is_subdir)(exp, pfid, cfid, request);
	return rc;
}

static inline int md_setattr(struct obd_export *exp, struct md_op_data *op_data,
			     void *ea, int ealen, void *ea2, int ea2len,
			     struct ptlrpc_request **request,
			     struct md_open_data **mod)
{
	int rc;

	EXP_CHECK_MD_OP(exp, setattr);
	EXP_MD_COUNTER_INCREMENT(exp, setattr);
	rc = MDP(exp->exp_obd, setattr)(exp, op_data, ea, ealen,
					ea2, ea2len, request, mod);
	return rc;
}

static inline int md_sync(struct obd_export *exp, const struct lu_fid *fid,
			  struct obd_capa *oc, struct ptlrpc_request **request)
{
	int rc;

	EXP_CHECK_MD_OP(exp, sync);
	EXP_MD_COUNTER_INCREMENT(exp, sync);
	rc = MDP(exp->exp_obd, sync)(exp, fid, oc, request);
	return rc;
}

static inline int md_readpage(struct obd_export *exp, struct md_op_data *opdata,
			      struct page **pages,
			      struct ptlrpc_request **request)
{
	int rc;

	EXP_CHECK_MD_OP(exp, readpage);
	EXP_MD_COUNTER_INCREMENT(exp, readpage);
	rc = MDP(exp->exp_obd, readpage)(exp, opdata, pages, request);
	return rc;
}

static inline int md_unlink(struct obd_export *exp, struct md_op_data *op_data,
			    struct ptlrpc_request **request)
{
	int rc;

	EXP_CHECK_MD_OP(exp, unlink);
	EXP_MD_COUNTER_INCREMENT(exp, unlink);
	rc = MDP(exp->exp_obd, unlink)(exp, op_data, request);
	return rc;
}

static inline int md_get_lustre_md(struct obd_export *exp,
				   struct ptlrpc_request *req,
				   struct obd_export *dt_exp,
				   struct obd_export *md_exp,
				   struct lustre_md *md)
{
	EXP_CHECK_MD_OP(exp, get_lustre_md);
	EXP_MD_COUNTER_INCREMENT(exp, get_lustre_md);
	return MDP(exp->exp_obd, get_lustre_md)(exp, req, dt_exp, md_exp, md);
}

static inline int md_free_lustre_md(struct obd_export *exp,
				    struct lustre_md *md)
{
	EXP_CHECK_MD_OP(exp, free_lustre_md);
	EXP_MD_COUNTER_INCREMENT(exp, free_lustre_md);
	return MDP(exp->exp_obd, free_lustre_md)(exp, md);
}

static inline int md_setxattr(struct obd_export *exp,
			      const struct lu_fid *fid, struct obd_capa *oc,
			      obd_valid valid, const char *name,
			      const char *input, int input_size,
			      int output_size, int flags, __u32 suppgid,
			      struct ptlrpc_request **request)
{
	EXP_CHECK_MD_OP(exp, setxattr);
	EXP_MD_COUNTER_INCREMENT(exp, setxattr);
	return MDP(exp->exp_obd, setxattr)(exp, fid, oc, valid, name, input,
					   input_size, output_size, flags,
					   suppgid, request);
}

static inline int md_getxattr(struct obd_export *exp,
			      const struct lu_fid *fid, struct obd_capa *oc,
			      obd_valid valid, const char *name,
			      const char *input, int input_size,
			      int output_size, int flags,
			      struct ptlrpc_request **request)
{
	EXP_CHECK_MD_OP(exp, getxattr);
	EXP_MD_COUNTER_INCREMENT(exp, getxattr);
	return MDP(exp->exp_obd, getxattr)(exp, fid, oc, valid, name, input,
					   input_size, output_size, flags,
					   request);
}

static inline int md_set_open_replay_data(struct obd_export *exp,
					  struct obd_client_handle *och,
					  struct ptlrpc_request *open_req)
{
	EXP_CHECK_MD_OP(exp, set_open_replay_data);
	EXP_MD_COUNTER_INCREMENT(exp, set_open_replay_data);
	return MDP(exp->exp_obd, set_open_replay_data)(exp, och, open_req);
}

static inline int md_clear_open_replay_data(struct obd_export *exp,
					    struct obd_client_handle *och)
{
	EXP_CHECK_MD_OP(exp, clear_open_replay_data);
	EXP_MD_COUNTER_INCREMENT(exp, clear_open_replay_data);
	return MDP(exp->exp_obd, clear_open_replay_data)(exp, och);
}

static inline int md_set_lock_data(struct obd_export *exp,
				   __u64 *lockh, void *data, __u64 *bits)
{
	EXP_CHECK_MD_OP(exp, set_lock_data);
	EXP_MD_COUNTER_INCREMENT(exp, set_lock_data);
	return MDP(exp->exp_obd, set_lock_data)(exp, lockh, data, bits);
}

static inline int md_cancel_unused(struct obd_export *exp,
				   const struct lu_fid *fid,
				   ldlm_policy_data_t *policy,
				   ldlm_mode_t mode,
				   ldlm_cancel_flags_t flags,
				   void *opaque)
{
	int rc;

	EXP_CHECK_MD_OP(exp, cancel_unused);
	EXP_MD_COUNTER_INCREMENT(exp, cancel_unused);

	rc = MDP(exp->exp_obd, cancel_unused)(exp, fid, policy, mode,
					      flags, opaque);
	return rc;
}

static inline ldlm_mode_t md_lock_match(struct obd_export *exp, __u64 flags,
					const struct lu_fid *fid,
					ldlm_type_t type,
					ldlm_policy_data_t *policy,
					ldlm_mode_t mode,
					struct lustre_handle *lockh)
{
	EXP_CHECK_MD_OP(exp, lock_match);
	EXP_MD_COUNTER_INCREMENT(exp, lock_match);
	return MDP(exp->exp_obd, lock_match)(exp, flags, fid, type,
					     policy, mode, lockh);
}

static inline int md_init_ea_size(struct obd_export *exp, int easize,
				  int def_asize, int cookiesize)
{
	EXP_CHECK_MD_OP(exp, init_ea_size);
	EXP_MD_COUNTER_INCREMENT(exp, init_ea_size);
	return MDP(exp->exp_obd, init_ea_size)(exp, easize, def_asize,
					       cookiesize);
}

static inline int md_get_remote_perm(struct obd_export *exp,
				     const struct lu_fid *fid,
				     struct obd_capa *oc, __u32 suppgid,
				     struct ptlrpc_request **request)
{
	EXP_CHECK_MD_OP(exp, get_remote_perm);
	EXP_MD_COUNTER_INCREMENT(exp, get_remote_perm);
	return MDP(exp->exp_obd, get_remote_perm)(exp, fid, oc, suppgid,
						  request);
}

static inline int md_renew_capa(struct obd_export *exp, struct obd_capa *ocapa,
				renew_capa_cb_t cb)
{
	int rc;

	EXP_CHECK_MD_OP(exp, renew_capa);
	EXP_MD_COUNTER_INCREMENT(exp, renew_capa);
	rc = MDP(exp->exp_obd, renew_capa)(exp, ocapa, cb);
	return rc;
}

static inline int md_unpack_capa(struct obd_export *exp,
				 struct ptlrpc_request *req,
				 const struct req_msg_field *field,
				 struct obd_capa **oc)
{
	int rc;

	EXP_CHECK_MD_OP(exp, unpack_capa);
	EXP_MD_COUNTER_INCREMENT(exp, unpack_capa);
	rc = MDP(exp->exp_obd, unpack_capa)(exp, req, field, oc);
	return rc;
}

static inline int md_intent_getattr_async(struct obd_export *exp,
					  struct md_enqueue_info *minfo,
					  struct ldlm_enqueue_info *einfo)
{
	int rc;

	EXP_CHECK_MD_OP(exp, intent_getattr_async);
	EXP_MD_COUNTER_INCREMENT(exp, intent_getattr_async);
	rc = MDP(exp->exp_obd, intent_getattr_async)(exp, minfo, einfo);
	return rc;
}

static inline int md_revalidate_lock(struct obd_export *exp,
				     struct lookup_intent *it,
				     struct lu_fid *fid, __u64 *bits)
{
	int rc;

	EXP_CHECK_MD_OP(exp, revalidate_lock);
	EXP_MD_COUNTER_INCREMENT(exp, revalidate_lock);
	rc = MDP(exp->exp_obd, revalidate_lock)(exp, it, fid, bits);
	return rc;
}


/* OBD Metadata Support */

extern int obd_init_caches(void);
extern void obd_cleanup_caches(void);

/* support routines */
extern struct kmem_cache *obdo_cachep;

#define OBDO_ALLOC(ptr)						       \
do {									  \
	OBD_SLAB_ALLOC_PTR_GFP((ptr), obdo_cachep, __GFP_IO);	     \
} while(0)

#define OBDO_FREE(ptr)							\
do {									  \
	OBD_SLAB_FREE_PTR((ptr), obdo_cachep);				\
} while(0)


static inline void obdo2fid(struct obdo *oa, struct lu_fid *fid)
{
	/* something here */
}

static inline void fid2obdo(struct lu_fid *fid, struct obdo *oa)
{
	/* something here */
}

typedef int (*register_lwp_cb)(void *data);

struct lwp_register_item {
	struct obd_export **lri_exp;
	register_lwp_cb	    lri_cb_func;
	void		   *lri_cb_data;
	struct list_head	    lri_list;
	char		    lri_name[MTI_NAME_MAXLEN];
};

/* I'm as embarrassed about this as you are.
 *
 * <shaver> // XXX do not look into _superhack with remaining eye
 * <shaver> // XXX if this were any uglier, I'd get my own show on MTV */
extern int (*ptlrpc_put_connection_superhack)(struct ptlrpc_connection *c);

/* obd_mount.c */

/* sysctl.c */
extern void obd_sysctl_init (void);
extern void obd_sysctl_clean (void);

/* uuid.c  */
typedef __u8 class_uuid_t[16];
void class_uuid_unparse(class_uuid_t in, struct obd_uuid *out);

/* lustre_peer.c    */
int lustre_uuid_to_peer(const char *uuid, lnet_nid_t *peer_nid, int index);
int class_add_uuid(const char *uuid, __u64 nid);
int class_del_uuid (const char *uuid);
int class_check_uuid(struct obd_uuid *uuid, __u64 nid);
void class_init_uuidlist(void);
void class_exit_uuidlist(void);

/* mea.c */
int mea_name2idx(struct lmv_stripe_md *mea, const char *name, int namelen);
int raw_name2idx(int hashtype, int count, const char *name, int namelen);

/* prng.c */
#define ll_generate_random_uuid(uuid_out) cfs_get_random_bytes(uuid_out, sizeof(class_uuid_t))

#endif /* __LINUX_OBD_CLASS_H */

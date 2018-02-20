// SPDX-License-Identifier: GPL-2.0
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
 * Copyright (c) 2011, 2015, Intel Corporation.
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
#include <lprocfs_status.h>

/* requests should be send without delay and resends for avoid deadlocks */
#define OBD_STATFS_NODELAY      0x0001
/* the statfs callback should not update obd_osfs_age */
#define OBD_STATFS_FROM_CACHE   0x0002
/* the statfs is only for retrieving information from MDT0 */
#define OBD_STATFS_FOR_MDT0	0x0004

/* OBD Device Declarations */
extern struct obd_device *obd_devs[MAX_OBD_DEVICES];
extern rwlock_t obd_dev_lock;

/* OBD Operations Declarations */
struct obd_device *class_exp2obd(struct obd_export *exp);
int class_handle_ioctl(unsigned int cmd, unsigned long arg);
int lustre_get_jobid(char *jobid);

struct lu_device_type;

/* genops.c */
extern struct list_head obd_types;
struct obd_export *class_conn2export(struct lustre_handle *conn);
int class_register_type(struct obd_ops *dt_ops, struct md_ops *md_ops,
			const char *name, struct lu_device_type *ldt);
int class_unregister_type(const char *name);

struct obd_device *class_newdev(const char *type_name, const char *name);
void class_release_dev(struct obd_device *obd);

int class_name2dev(const char *name);
struct obd_device *class_name2obd(const char *name);
int class_uuid2dev(struct obd_uuid *uuid);
struct obd_device *class_find_client_obd(struct obd_uuid *tgt_uuid,
					 const char *typ_name,
					 struct obd_uuid *grp_uuid);
struct obd_device *class_devices_in_group(struct obd_uuid *grp_uuid,
					  int *next);
struct obd_device *class_num2obd(int num);

int class_notify_sptlrpc_conf(const char *fsname, int namelen);

int obd_connect_flags2str(char *page, int count, __u64 flags, char *sep);

int obd_zombie_impexp_init(void);
void obd_zombie_impexp_stop(void);
void obd_zombie_barrier(void);

int obd_get_request_slot(struct client_obd *cli);
void obd_put_request_slot(struct client_obd *cli);
__u32 obd_get_max_rpcs_in_flight(struct client_obd *cli);
int obd_set_max_rpcs_in_flight(struct client_obd *cli, __u32 max);
int obd_set_max_mod_rpcs_in_flight(struct client_obd *cli, u16 max);
int obd_mod_rpc_stats_seq_show(struct client_obd *cli, struct seq_file *seq);

u16 obd_get_mod_rpc_slot(struct client_obd *cli, u32 opc,
			 struct lookup_intent *it);
void obd_put_mod_rpc_slot(struct client_obd *cli, u32 opc,
			  struct lookup_intent *it, u16 tag);

struct llog_handle;
struct llog_rec_hdr;
typedef int (*llog_cb_t)(const struct lu_env *, struct llog_handle *,
			 struct llog_rec_hdr *, void *);

/* obd_config.c */
char *lustre_cfg_string(struct lustre_cfg *lcfg, u32 index);
int class_process_config(struct lustre_cfg *lcfg);
int class_process_proc_param(char *prefix, struct lprocfs_vars *lvars,
			     struct lustre_cfg *lcfg, void *data);

/* For interoperability */
struct cfg_interop_param {
	char *old_param;
	char *new_param;
};

int class_find_param(char *buf, char *key, char **valp);
struct cfg_interop_param *class_find_old_param(const char *param,
					       struct cfg_interop_param *ptr);
int class_get_next_param(char **params, char *copy);
int class_parse_nid(char *buf, lnet_nid_t *nid, char **endh);
int class_parse_nid_quiet(char *buf, lnet_nid_t *nid, char **endh);
int class_parse_net(char *buf, u32 *net, char **endh);
int class_match_nid(char *buf, char *key, lnet_nid_t nid);
int class_match_net(char *buf, char *key, u32 net);

struct obd_device *class_incref(struct obd_device *obd,
				const char *scope, const void *source);
void class_decref(struct obd_device *obd,
		  const char *scope, const void *source);
int class_config_llog_handler(const struct lu_env *env,
			      struct llog_handle *handle,
			      struct llog_rec_hdr *rec, void *data);
int class_add_uuid(const char *uuid, __u64 nid);

/* obdecho */
void lprocfs_echo_init_vars(struct lprocfs_static_vars *lvars);

#define CFG_F_START     0x01   /* Set when we start updating from a log */
#define CFG_F_MARKER    0x02   /* We are within a maker */
#define CFG_F_SKIP      0x04   /* We should ignore this cfg command */
#define CFG_F_COMPAT146 0x08   /* Allow old-style logs */
#define CFG_F_EXCLUDE   0x10   /* OST exclusion list */

/* Passed as data param to class_config_parse_llog */
struct config_llog_instance {
	char		   *cfg_obdname;
	void		   *cfg_instance;
	struct super_block *cfg_sb;
	struct obd_uuid     cfg_uuid;
	llog_cb_t	    cfg_callback;
	int		    cfg_last_idx; /* for partial llog processing */
	int		    cfg_flags;
};

int class_config_parse_llog(const struct lu_env *env, struct llog_ctxt *ctxt,
			    char *name, struct config_llog_instance *cfg);
enum {
	CONFIG_T_CONFIG  = 0,
	CONFIG_T_SPTLRPC = 1,
	CONFIG_T_RECOVER = 2,
	CONFIG_T_PARAMS  = 3,
	CONFIG_T_MAX     = 4
};

#define PARAMS_FILENAME	"params"
#define LCTL_UPCALL	"lctl"

/* list of active configuration logs  */
struct config_llog_data {
	struct ldlm_res_id	    cld_resid;
	struct config_llog_instance cld_cfg;
	struct list_head	    cld_list_chain;
	atomic_t		    cld_refcount;
	struct config_llog_data    *cld_sptlrpc;/* depended sptlrpc log */
	struct config_llog_data	   *cld_params;	/* common parameters log */
	struct config_llog_data    *cld_recover;/* imperative recover log */
	struct obd_export	   *cld_mgcexp;
	struct mutex		    cld_lock;
	int			    cld_type;
	unsigned int		    cld_stopping:1, /*
						     * we were told to stop
						     * watching
						     */
				    cld_lostlock:1; /* lock not requeued */
	char			    cld_logname[0];
};

struct lustre_profile {
	struct list_head lp_list;
	char		*lp_profile;
	char		*lp_dt;
	char		*lp_md;
	int		 lp_refs;
	bool		 lp_list_deleted;
};

struct lustre_profile *class_get_profile(const char *prof);
void class_del_profile(const char *prof);
void class_put_profile(struct lustre_profile *lprof);
void class_del_profiles(void);

#if LUSTRE_TRACKS_LOCK_EXP_REFS

void __class_export_add_lock_ref(struct obd_export *exp,
				 struct ldlm_lock *lock);
void __class_export_del_lock_ref(struct obd_export *exp,
				 struct ldlm_lock *lock);
extern void (*class_export_dump_hook)(struct obd_export *exp);

#else

#define __class_export_add_lock_ref(exp, lock)	     do {} while (0)
#define __class_export_del_lock_ref(exp, lock)	     do {} while (0)

#endif

/* genops.c */
struct obd_export *class_export_get(struct obd_export *exp);
void class_export_put(struct obd_export *exp);
struct obd_export *class_new_export(struct obd_device *obddev,
				    struct obd_uuid *cluuid);
void class_unlink_export(struct obd_export *exp);

struct obd_import *class_import_get(struct obd_import *imp);
void class_import_put(struct obd_import *imp);
struct obd_import *class_new_import(struct obd_device *obd);
void class_destroy_import(struct obd_import *exp);

void class_put_type(struct obd_type *type);
int class_connect(struct lustre_handle *conn, struct obd_device *obd,
		  struct obd_uuid *cluuid);
int class_disconnect(struct obd_export *exp);
void class_fail_export(struct obd_export *exp);
int class_manual_cleanup(struct obd_device *obd);

static inline void class_export_rpc_inc(struct obd_export *exp)
{
	atomic_inc(&(exp)->exp_rpc_count);
	CDEBUG(D_INFO, "RPC GETting export %p : new rpc_count %d\n",
	       (exp), atomic_read(&(exp)->exp_rpc_count));
}

static inline void class_export_rpc_dec(struct obd_export *exp)
{
	LASSERT_ATOMIC_POS(&exp->exp_rpc_count);
	atomic_dec(&(exp)->exp_rpc_count);
	CDEBUG(D_INFO, "RPC PUTting export %p : new rpc_count %d\n",
	       (exp), atomic_read(&(exp)->exp_rpc_count));
}

static inline struct obd_export *class_export_lock_get(struct obd_export *exp,
						       struct ldlm_lock *lock)
{
	atomic_inc(&(exp)->exp_locks_count);
	__class_export_add_lock_ref(exp, lock);
	CDEBUG(D_INFO, "lock GETting export %p : new locks_count %d\n",
	       (exp), atomic_read(&(exp)->exp_locks_count));
	return class_export_get(exp);
}

static inline void class_export_lock_put(struct obd_export *exp,
					 struct ldlm_lock *lock)
{
	LASSERT_ATOMIC_POS(&exp->exp_locks_count);
	atomic_dec(&(exp)->exp_locks_count);
	__class_export_del_lock_ref(exp, lock);
	CDEBUG(D_INFO, "lock PUTting export %p : new locks_count %d\n",
	       (exp), atomic_read(&(exp)->exp_locks_count));
	class_export_put(exp);
}

static inline enum obd_option exp_flags_from_obd(struct obd_device *obd)
{
	return ((obd->obd_fail ? OBD_OPT_FAILOVER : 0) |
		(obd->obd_force ? OBD_OPT_FORCE : 0) |
		0);
}

static inline int lprocfs_climp_check(struct obd_device *obd)
{
	down_read(&(obd)->u.cli.cl_sem);
	if (!(obd)->u.cli.cl_import) {
		up_read(&(obd)->u.cli.cl_sem);
		return -ENODEV;
	}
	return 0;
}

struct inode;
struct lu_attr;
struct obdo;

void obdo_to_ioobj(const struct obdo *oa, struct obd_ioobj *ioobj);

#define OBT(dev)	((dev)->obd_type)
#define OBP(dev, op)    ((dev)->obd_type->typ_dt_ops->op)
#define MDP(dev, op)    ((dev)->obd_type->typ_md_ops->op)
#define CTXTP(ctxt, op) ((ctxt)->loc_logops->lop_##op)

/*
 * Ensure obd_setup: used for cleanup which must be called
 * while obd is stopping
 */
static inline int obd_check_dev(struct obd_device *obd)
{
	if (!obd) {
		CERROR("NULL device\n");
		return -ENODEV;
	}
	return 0;
}

/* ensure obd_setup and !obd_stopping */
static inline int obd_check_dev_active(struct obd_device *obd)
{
	int rc;

	rc = obd_check_dev(obd);
	if (rc)
		return rc;
	if (!obd->obd_set_up || obd->obd_stopping) {
		CERROR("Device %d not setup\n", obd->obd_minor);
		return -ENODEV;
	}
	return rc;
}

#define OBD_COUNTER_OFFSET(op)					\
	((offsetof(struct obd_ops, op) -			\
		offsetof(struct obd_ops, iocontrol))		\
		/ sizeof(((struct obd_ops *)(0))->iocontrol))

#define OBD_COUNTER_INCREMENT(obdx, op)					\
do {									\
	if ((obdx)->obd_stats) {					\
		unsigned int coffset;					\
		coffset = (unsigned int)((obdx)->obd_cntr_base) +	\
			OBD_COUNTER_OFFSET(op);				\
		LASSERT(coffset < (obdx)->obd_stats->ls_num);		\
		lprocfs_counter_incr((obdx)->obd_stats, coffset);	\
	}								\
} while (0)

#define EXP_COUNTER_INCREMENT(export, op)				     \
do {									     \
	if ((export)->exp_obd->obd_stats) {				     \
		unsigned int coffset;					     \
		coffset = (unsigned int)((export)->exp_obd->obd_cntr_base) + \
			OBD_COUNTER_OFFSET(op);				     \
		LASSERT(coffset < (export)->exp_obd->obd_stats->ls_num);     \
		lprocfs_counter_incr((export)->exp_obd->obd_stats, coffset); \
	}								     \
} while (0)

#define MD_COUNTER_OFFSET(op)					\
	((offsetof(struct md_ops, op) -				\
		offsetof(struct md_ops, getstatus))		\
		/ sizeof(((struct md_ops *)(0))->getstatus))

#define MD_COUNTER_INCREMENT(obdx, op)				 \
do {								 \
	if ((obd)->md_stats) {					 \
		unsigned int coffset;				 \
		coffset = (unsigned int)((obdx)->md_cntr_base) + \
			MD_COUNTER_OFFSET(op);			 \
		LASSERT(coffset < (obdx)->md_stats->ls_num);	 \
		lprocfs_counter_incr((obdx)->md_stats, coffset); \
	}							 \
} while (0)

#define EXP_MD_COUNTER_INCREMENT(export, op)				    \
do {									    \
	if ((export)->exp_obd->obd_stats) {				    \
		unsigned int coffset;					    \
		coffset = (unsigned int)((export)->exp_obd->md_cntr_base) + \
			MD_COUNTER_OFFSET(op);				    \
		LASSERT(coffset < (export)->exp_obd->md_stats->ls_num);	    \
		lprocfs_counter_incr((export)->exp_obd->md_stats, coffset); \
		if ((export)->exp_md_stats)				    \
			lprocfs_counter_incr(				    \
				(export)->exp_md_stats, coffset);	    \
	}								    \
} while (0)

#define EXP_CHECK_MD_OP(exp, op)				\
do {								\
	if (!(exp)) {						\
		CERROR("obd_" #op ": NULL export\n");		\
		return -ENODEV;					\
	}							\
	if (!(exp)->exp_obd || !OBT((exp)->exp_obd)) {		\
		CERROR("obd_" #op ": cleaned up obd\n");	\
		return -EOPNOTSUPP;				\
	}							\
	if (!OBT((exp)->exp_obd) || !MDP((exp)->exp_obd, op)) {	\
		CERROR("obd_" #op ": dev %s/%d no operation\n", \
			(exp)->exp_obd->obd_name,		\
			(exp)->exp_obd->obd_minor);		\
		return -EOPNOTSUPP;				\
	}							\
} while (0)

#define OBD_CHECK_DT_OP(obd, op, err)					\
do {									\
	if (!OBT(obd) || !OBP((obd), op)) {				\
		if (err)						\
			CERROR("obd_" #op ": dev %d no operation\n",	\
				obd->obd_minor);			\
		return err;						\
	}								\
} while (0)

#define EXP_CHECK_DT_OP(exp, op)				\
do {								\
	if (!(exp)) {						\
		CERROR("obd_" #op ": NULL export\n");		\
		return -ENODEV;					\
	}							\
	if (!(exp)->exp_obd || !OBT((exp)->exp_obd)) {		\
		CERROR("obd_" #op ": cleaned up obd\n");	\
		return -EOPNOTSUPP;				\
	}							\
	if (!OBT((exp)->exp_obd) || !OBP((exp)->exp_obd, op)) {	\
		CERROR("obd_" #op ": dev %d no operation\n",	\
			(exp)->exp_obd->obd_minor);		\
		return -EOPNOTSUPP;				\
	}							\
} while (0)

#define CTXT_CHECK_OP(ctxt, op, err)					\
do {									\
	if (!OBT(ctxt->loc_obd) || !CTXTP((ctxt), op)) {		\
		if (err)						\
			CERROR("lop_" #op ": dev %d no operation\n",	\
				ctxt->loc_obd->obd_minor);		\
		return err;						\
	}								\
} while (0)

static inline int class_devno_max(void)
{
	return MAX_OBD_DEVICES;
}

static inline int obd_get_info(const struct lu_env *env,
			       struct obd_export *exp, __u32 keylen,
			       void *key, __u32 *vallen, void *val)
{
	int rc;

	EXP_CHECK_DT_OP(exp, get_info);
	EXP_COUNTER_INCREMENT(exp, get_info);

	rc = OBP(exp->exp_obd, get_info)(env, exp, keylen, key, vallen, val);
	return rc;
}

static inline int obd_set_info_async(const struct lu_env *env,
				     struct obd_export *exp, u32 keylen,
				     void *key, u32 vallen, void *val,
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

static inline int obd_setup(struct obd_device *obd, struct lustre_cfg *cfg)
{
	int rc;
	struct lu_device_type *ldt;
	struct lu_device *d;

	ldt = obd->obd_type->typ_lu;
	if (ldt) {
		struct lu_context  session_ctx;
		struct lu_env env;

		lu_context_init(&session_ctx, LCT_SESSION | LCT_SERVER_SESSION);
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
			} else {
				rc = PTR_ERR(d);
			}
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

static inline int obd_precleanup(struct obd_device *obd)
{
	int rc;
	struct lu_device_type *ldt;
	struct lu_device *d;

	rc = obd_check_dev(obd);
	if (rc)
		return rc;
	ldt = obd->obd_type->typ_lu;
	d = obd->obd_lu_dev;
	if (ldt && d) {
		struct lu_env env;

		rc = lu_env_init(&env, ldt->ldt_ctx_tags);
		if (!rc) {
			ldt->ldt_ops->ldto_device_fini(&env, d);
			lu_env_fini(&env);
		}
	}
	OBD_CHECK_DT_OP(obd, precleanup, 0);
	OBD_COUNTER_INCREMENT(obd, precleanup);

	rc = OBP(obd, precleanup)(obd);
	return rc;
}

static inline int obd_cleanup(struct obd_device *obd)
{
	int rc;
	struct lu_device_type *ldt;
	struct lu_device *d;

	rc = obd_check_dev(obd);
	if (rc)
		return rc;

	ldt = obd->obd_type->typ_lu;
	d = obd->obd_lu_dev;
	if (ldt && d) {
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
	/*
	 * If we set up but never connected, the
	 * client import will not have been cleaned.
	 */
	down_write(&obd->u.cli.cl_sem);
	if (obd->u.cli.cl_import) {
		struct obd_import *imp;

		imp = obd->u.cli.cl_import;
		CDEBUG(D_CONFIG, "%s: client import never connected\n",
		       obd->obd_name);
		ptlrpc_invalidate_import(imp);
		client_destroy_import(imp);
		obd->u.cli.cl_import = NULL;
	}
	up_write(&obd->u.cli.cl_sem);
}

static inline int
obd_process_config(struct obd_device *obd, int datalen, void *data)
{
	int rc;
	struct lu_device_type *ldt;
	struct lu_device *d;

	rc = obd_check_dev(obd);
	if (rc)
		return rc;

	obd->obd_process_conf = 1;
	ldt = obd->obd_type->typ_lu;
	d = obd->obd_lu_dev;
	if (ldt && d) {
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

static inline int obd_create(const struct lu_env *env, struct obd_export *exp,
			     struct obdo *obdo)
{
	int rc;

	EXP_CHECK_DT_OP(exp, create);
	EXP_COUNTER_INCREMENT(exp, create);

	rc = OBP(exp->exp_obd, create)(env, exp, obdo);
	return rc;
}

static inline int obd_destroy(const struct lu_env *env, struct obd_export *exp,
			      struct obdo *obdo)
{
	int rc;

	EXP_CHECK_DT_OP(exp, destroy);
	EXP_COUNTER_INCREMENT(exp, destroy);

	rc = OBP(exp->exp_obd, destroy)(env, exp, obdo);
	return rc;
}

static inline int obd_getattr(const struct lu_env *env, struct obd_export *exp,
			      struct obdo *oa)
{
	int rc;

	EXP_CHECK_DT_OP(exp, getattr);
	EXP_COUNTER_INCREMENT(exp, getattr);

	rc = OBP(exp->exp_obd, getattr)(env, exp, oa);
	return rc;
}

static inline int obd_setattr(const struct lu_env *env, struct obd_export *exp,
			      struct obdo *oa)
{
	int rc;

	EXP_CHECK_DT_OP(exp, setattr);
	EXP_COUNTER_INCREMENT(exp, setattr);

	rc = OBP(exp->exp_obd, setattr)(env, exp, oa);
	return rc;
}

static inline int obd_add_conn(struct obd_import *imp, struct obd_uuid *uuid,
			       int priority)
{
	struct obd_device *obd = imp->imp_obd;
	int rc;

	rc = obd_check_dev_active(obd);
	if (rc)
		return rc;
	OBD_CHECK_DT_OP(obd, add_conn, -EOPNOTSUPP);
	OBD_COUNTER_INCREMENT(obd, add_conn);

	rc = OBP(obd, add_conn)(imp, uuid, priority);
	return rc;
}

static inline int obd_del_conn(struct obd_import *imp, struct obd_uuid *uuid)
{
	struct obd_device *obd = imp->imp_obd;
	int rc;

	rc = obd_check_dev_active(obd);
	if (rc)
		return rc;
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

/*
 * Create a new /a exp on device /a obd for the uuid /a cluuid
 * @param exp New export handle
 * @param d Connect data, supported flags are set, flags also understood
 *    by obd are returned.
 */
static inline int obd_connect(const struct lu_env *env,
			      struct obd_export **exp, struct obd_device *obd,
			      struct obd_uuid *cluuid,
			      struct obd_connect_data *data,
			      void *localdata)
{
	int rc;
	__u64 ocf = data ? data->ocd_connect_flags : 0; /*
							 * for post-condition
							 * check
							 */

	rc = obd_check_dev_active(obd);
	if (rc)
		return rc;
	OBD_CHECK_DT_OP(obd, connect, -EOPNOTSUPP);
	OBD_COUNTER_INCREMENT(obd, connect);

	rc = OBP(obd, connect)(env, exp, obd, cluuid, data, localdata);
	/* check that only subset is granted */
	LASSERT(ergo(data, (data->ocd_connect_flags & ocf) ==
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
	__u64 ocf = d ? d->ocd_connect_flags : 0; /* for post-condition check */

	rc = obd_check_dev_active(obd);
	if (rc)
		return rc;
	OBD_CHECK_DT_OP(obd, reconnect, 0);
	OBD_COUNTER_INCREMENT(obd, reconnect);

	rc = OBP(obd, reconnect)(env, exp, obd, cluuid, d, localdata);
	/* check that only subset is granted */
	LASSERT(ergo(d, (d->ocd_connect_flags & ocf) == d->ocd_connect_flags));
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

static inline int obd_fid_alloc(const struct lu_env *env,
				struct obd_export *exp,
				struct lu_fid *fid,
				struct md_op_data *op_data)
{
	int rc;

	EXP_CHECK_DT_OP(exp, fid_alloc);
	EXP_COUNTER_INCREMENT(exp, fid_alloc);

	rc = OBP(exp->exp_obd, fid_alloc)(env, exp, fid, op_data);
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

static inline int obd_pool_add(struct obd_device *obd,
			       char *poolname,
			       char *ostname)
{
	int rc;

	OBD_CHECK_DT_OP(obd, pool_add, -EOPNOTSUPP);
	OBD_COUNTER_INCREMENT(obd, pool_add);

	rc = OBP(obd, pool_add)(obd, poolname, ostname);
	return rc;
}

static inline int obd_pool_rem(struct obd_device *obd,
			       char *poolname,
			       char *ostname)
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

	if ((exp)->exp_obd && OBT((exp)->exp_obd) &&
	    OBP((exp)->exp_obd, init_export))
		rc = OBP(exp->exp_obd, init_export)(exp);
	return rc;
}

static inline int obd_destroy_export(struct obd_export *exp)
{
	if ((exp)->exp_obd && OBT((exp)->exp_obd) &&
	    OBP((exp)->exp_obd, destroy_export))
		OBP(exp->exp_obd, destroy_export)(exp);
	return 0;
}

/*
 * @max_age is the oldest time in jiffies that we accept using a cached data.
 * If the cache is older than @max_age we will get a new value from the
 * target.  Use a value of "cfs_time_current() + HZ" to guarantee freshness.
 */
static inline int obd_statfs_async(struct obd_export *exp,
				   struct obd_info *oinfo,
				   __u64 max_age,
				   struct ptlrpc_request_set *rqset)
{
	int rc = 0;
	struct obd_device *obd;

	if (!exp || !exp->exp_obd)
		return -EINVAL;

	obd = exp->exp_obd;
	OBD_CHECK_DT_OP(obd, statfs, -EOPNOTSUPP);
	OBD_COUNTER_INCREMENT(obd, statfs);

	CDEBUG(D_SUPER, "%s: osfs %p age %llu, max_age %llu\n",
	       obd->obd_name, &obd->obd_osfs, obd->obd_osfs_age, max_age);
	if (cfs_time_before_64(obd->obd_osfs_age, max_age)) {
		rc = OBP(obd, statfs_async)(exp, oinfo, max_age, rqset);
	} else {
		CDEBUG(D_SUPER,
		       "%s: use %p cache blocks %llu/%llu objects %llu/%llu\n",
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
	struct obd_info oinfo = {
		.oi_osfs = osfs,
		.oi_flags = flags,
	};
	int rc = 0;

	set = ptlrpc_prep_set();
	if (!set)
		return -ENOMEM;

	rc = obd_statfs_async(exp, &oinfo, max_age, set);
	if (rc == 0)
		rc = ptlrpc_set_wait(set);
	ptlrpc_set_destroy(set);
	return rc;
}

/*
 * @max_age is the oldest time in jiffies that we accept using a cached data.
 * If the cache is older than @max_age we will get a new value from the
 * target.  Use a value of "cfs_time_current() + HZ" to guarantee freshness.
 */
static inline int obd_statfs(const struct lu_env *env, struct obd_export *exp,
			     struct obd_statfs *osfs, __u64 max_age,
			     __u32 flags)
{
	int rc = 0;
	struct obd_device *obd = exp->exp_obd;

	if (!obd)
		return -EINVAL;

	OBD_CHECK_DT_OP(obd, statfs, -EOPNOTSUPP);
	OBD_COUNTER_INCREMENT(obd, statfs);

	CDEBUG(D_SUPER, "osfs %llu, max_age %llu\n",
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
		CDEBUG(D_SUPER,
		       "%s: use %p cache blocks %llu/%llu objects %llu/%llu\n",
		       obd->obd_name, &obd->obd_osfs,
		       obd->obd_osfs.os_bavail, obd->obd_osfs.os_blocks,
		       obd->obd_osfs.os_ffree, obd->obd_osfs.os_files);
		spin_lock(&obd->obd_osfs_lock);
		memcpy(osfs, &obd->obd_osfs, sizeof(*osfs));
		spin_unlock(&obd->obd_osfs_lock);
	}
	return rc;
}

static inline int obd_preprw(const struct lu_env *env, int cmd,
			     struct obd_export *exp, struct obdo *oa,
			     int objcount, struct obd_ioobj *obj,
			     struct niobuf_remote *remote, int *pages,
			     struct niobuf_local *local)
{
	int rc;

	EXP_CHECK_DT_OP(exp, preprw);
	EXP_COUNTER_INCREMENT(exp, preprw);

	rc = OBP(exp->exp_obd, preprw)(env, cmd, exp, oa, objcount, obj, remote,
				       pages, local);
	return rc;
}

static inline int obd_commitrw(const struct lu_env *env, int cmd,
			       struct obd_export *exp, struct obdo *oa,
			       int objcount, struct obd_ioobj *obj,
			       struct niobuf_remote *rnb, int pages,
			       struct niobuf_local *local, int rc)
{
	EXP_CHECK_DT_OP(exp, commitrw);
	EXP_COUNTER_INCREMENT(exp, commitrw);

	rc = OBP(exp->exp_obd, commitrw)(env, cmd, exp, oa, objcount, obj,
					 rnb, pages, local, rc);
	return rc;
}

static inline int obd_iocontrol(unsigned int cmd, struct obd_export *exp,
				int len, void *karg, void __user *uarg)
{
	int rc;

	EXP_CHECK_DT_OP(exp, iocontrol);
	EXP_COUNTER_INCREMENT(exp, iocontrol);

	rc = OBP(exp->exp_obd, iocontrol)(cmd, exp, len, karg, uarg);
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

static inline int obd_notify(struct obd_device *obd,
			     struct obd_device *watched,
			     enum obd_notify_event ev,
			     void *data)
{
	int rc;

	rc = obd_check_dev(obd);
	if (rc)
		return rc;

	if (!obd->obd_set_up) {
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
	if (onu->onu_upcall)
		rc2 = onu->onu_upcall(observer, observed, ev,
				      onu->onu_owner, NULL);
	else
		rc2 = 0;

	return rc1 ? rc1 : rc2;
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
	/*
	 * returns: 0 on healthy
	 *	 >0 on unhealthy + reason code/flag
	 *	    however the only supported reason == 1 right now
	 *	    We'll need to define some better reasons
	 *	    or flags in the future.
	 *	 <0 on error
	 */
	int rc;

	/* don't use EXP_CHECK_DT_OP, because NULL method is normal here */
	if (!obd || !OBT(obd)) {
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
	int rc;

	rc = obd_check_dev(obd);
	if (rc)
		return rc;
	down_write(&obd->obd_observer_link_sem);
	if (obd->obd_observer && observer) {
		up_write(&obd->obd_observer_link_sem);
		return -EALREADY;
	}
	obd->obd_observer = observer;
	up_write(&obd->obd_observer_link_sem);
	return 0;
}

/* metadata helpers */
static inline int md_getstatus(struct obd_export *exp, struct lu_fid *fid)
{
	int rc;

	EXP_CHECK_MD_OP(exp, getstatus);
	EXP_MD_COUNTER_INCREMENT(exp, getstatus);
	rc = MDP(exp->exp_obd, getstatus)(exp, fid);
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
			    const void *data, size_t datalen, umode_t mode,
			    uid_t uid, gid_t gid, cfs_cap_t cap_effective,
			    __u64 rdev, struct ptlrpc_request **request)
{
	int rc;

	EXP_CHECK_MD_OP(exp, create);
	EXP_MD_COUNTER_INCREMENT(exp, create);
	rc = MDP(exp->exp_obd, create)(exp, op_data, data, datalen, mode,
				       uid, gid, cap_effective, rdev, request);
	return rc;
}

static inline int md_enqueue(struct obd_export *exp,
			     struct ldlm_enqueue_info *einfo,
			     const union ldlm_policy_data *policy,
			     struct lookup_intent *it,
			     struct md_op_data *op_data,
			     struct lustre_handle *lockh,
			     __u64 extra_lock_flags)
{
	int rc;

	EXP_CHECK_MD_OP(exp, enqueue);
	EXP_MD_COUNTER_INCREMENT(exp, enqueue);
	rc = MDP(exp->exp_obd, enqueue)(exp, einfo, policy, it, op_data, lockh,
					extra_lock_flags);
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
				 struct md_op_data *op_data,
				 struct lookup_intent *it,
				 struct ptlrpc_request **reqp,
				 ldlm_blocking_callback cb_blocking,
				 __u64 extra_lock_flags)
{
	int rc;

	EXP_CHECK_MD_OP(exp, intent_lock);
	EXP_MD_COUNTER_INCREMENT(exp, intent_lock);
	rc = MDP(exp->exp_obd, intent_lock)(exp, op_data, it, reqp,
					    cb_blocking, extra_lock_flags);
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
			    const char *old, size_t oldlen, const char *new,
			    size_t newlen, struct ptlrpc_request **request)
{
	int rc;

	EXP_CHECK_MD_OP(exp, rename);
	EXP_MD_COUNTER_INCREMENT(exp, rename);
	rc = MDP(exp->exp_obd, rename)(exp, op_data, old, oldlen, new,
				       newlen, request);
	return rc;
}

static inline int md_setattr(struct obd_export *exp, struct md_op_data *op_data,
			     void *ea, size_t ealen,
			     struct ptlrpc_request **request)
{
	int rc;

	EXP_CHECK_MD_OP(exp, setattr);
	EXP_MD_COUNTER_INCREMENT(exp, setattr);
	rc = MDP(exp->exp_obd, setattr)(exp, op_data, ea, ealen, request);
	return rc;
}

static inline int md_sync(struct obd_export *exp, const struct lu_fid *fid,
			  struct ptlrpc_request **request)
{
	int rc;

	EXP_CHECK_MD_OP(exp, sync);
	EXP_MD_COUNTER_INCREMENT(exp, sync);
	rc = MDP(exp->exp_obd, sync)(exp, fid, request);
	return rc;
}

static inline int md_read_page(struct obd_export *exp,
			       struct md_op_data *op_data,
			       struct md_callback *cb_op,
			       __u64  hash_offset,
			       struct page **ppage)
{
	int rc;

	EXP_CHECK_MD_OP(exp, read_page);
	EXP_MD_COUNTER_INCREMENT(exp, read_page);
	rc = MDP(exp->exp_obd, read_page)(exp, op_data, cb_op, hash_offset,
					  ppage);
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

static inline int md_merge_attr(struct obd_export *exp,
				const struct lmv_stripe_md *lsm,
				struct cl_attr *attr,
				ldlm_blocking_callback cb)
{
	EXP_CHECK_MD_OP(exp, merge_attr);
	EXP_MD_COUNTER_INCREMENT(exp, merge_attr);
	return MDP(exp->exp_obd, merge_attr)(exp, lsm, attr, cb);
}

static inline int md_setxattr(struct obd_export *exp, const struct lu_fid *fid,
			      u64 valid, const char *name,
			      const char *input, int input_size,
			      int output_size, int flags, __u32 suppgid,
			      struct ptlrpc_request **request)
{
	EXP_CHECK_MD_OP(exp, setxattr);
	EXP_MD_COUNTER_INCREMENT(exp, setxattr);
	return MDP(exp->exp_obd, setxattr)(exp, fid, valid, name, input,
					   input_size, output_size, flags,
					   suppgid, request);
}

static inline int md_getxattr(struct obd_export *exp, const struct lu_fid *fid,
			      u64 valid, const char *name,
			      const char *input, int input_size,
			      int output_size, int flags,
			      struct ptlrpc_request **request)
{
	EXP_CHECK_MD_OP(exp, getxattr);
	EXP_MD_COUNTER_INCREMENT(exp, getxattr);
	return MDP(exp->exp_obd, getxattr)(exp, fid, valid, name, input,
					   input_size, output_size, flags,
					   request);
}

static inline int md_set_open_replay_data(struct obd_export *exp,
					  struct obd_client_handle *och,
					  struct lookup_intent *it)
{
	EXP_CHECK_MD_OP(exp, set_open_replay_data);
	EXP_MD_COUNTER_INCREMENT(exp, set_open_replay_data);
	return MDP(exp->exp_obd, set_open_replay_data)(exp, och, it);
}

static inline int md_clear_open_replay_data(struct obd_export *exp,
					    struct obd_client_handle *och)
{
	EXP_CHECK_MD_OP(exp, clear_open_replay_data);
	EXP_MD_COUNTER_INCREMENT(exp, clear_open_replay_data);
	return MDP(exp->exp_obd, clear_open_replay_data)(exp, och);
}

static inline int md_set_lock_data(struct obd_export *exp,
				   const struct lustre_handle *lockh,
				   void *data, __u64 *bits)
{
	EXP_CHECK_MD_OP(exp, set_lock_data);
	EXP_MD_COUNTER_INCREMENT(exp, set_lock_data);
	return MDP(exp->exp_obd, set_lock_data)(exp, lockh, data, bits);
}

static inline int md_cancel_unused(struct obd_export *exp,
				   const struct lu_fid *fid,
				   union ldlm_policy_data *policy,
				   enum ldlm_mode mode,
				   enum ldlm_cancel_flags flags,
				   void *opaque)
{
	int rc;

	EXP_CHECK_MD_OP(exp, cancel_unused);
	EXP_MD_COUNTER_INCREMENT(exp, cancel_unused);

	rc = MDP(exp->exp_obd, cancel_unused)(exp, fid, policy, mode,
					      flags, opaque);
	return rc;
}

static inline enum ldlm_mode md_lock_match(struct obd_export *exp, __u64 flags,
					   const struct lu_fid *fid,
					   enum ldlm_type type,
					   union ldlm_policy_data *policy,
					   enum ldlm_mode mode,
					   struct lustre_handle *lockh)
{
	EXP_CHECK_MD_OP(exp, lock_match);
	EXP_MD_COUNTER_INCREMENT(exp, lock_match);
	return MDP(exp->exp_obd, lock_match)(exp, flags, fid, type,
					     policy, mode, lockh);
}

static inline int md_init_ea_size(struct obd_export *exp, u32 easize,
				  u32 def_asize)
{
	EXP_CHECK_MD_OP(exp, init_ea_size);
	EXP_MD_COUNTER_INCREMENT(exp, init_ea_size);
	return MDP(exp->exp_obd, init_ea_size)(exp, easize, def_asize);
}

static inline int md_intent_getattr_async(struct obd_export *exp,
					  struct md_enqueue_info *minfo)
{
	int rc;

	EXP_CHECK_MD_OP(exp, intent_getattr_async);
	EXP_MD_COUNTER_INCREMENT(exp, intent_getattr_async);
	rc = MDP(exp->exp_obd, intent_getattr_async)(exp, minfo);
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

static inline int md_get_fid_from_lsm(struct obd_export *exp,
				      const struct lmv_stripe_md *lsm,
				      const char *name, int namelen,
				      struct lu_fid *fid)
{
	int rc;

	EXP_CHECK_MD_OP(exp, get_fid_from_lsm);
	EXP_MD_COUNTER_INCREMENT(exp, get_fid_from_lsm);
	rc = MDP(exp->exp_obd, get_fid_from_lsm)(exp, lsm, name, namelen, fid);
	return rc;
}

/*
 * Unpack an MD struct from disk to in-memory format.
 * Returns +ve size of unpacked MD (0 for free), or -ve error.
 *
 * If *plsm != NULL and lmm == NULL then *lsm will be freed.
 * If *plsm == NULL then it will be allocated.
 */
static inline int md_unpackmd(struct obd_export *exp,
			      struct lmv_stripe_md **plsm,
			      const union lmv_mds_md *lmm, size_t lmm_size)
{
	int rc;

	EXP_CHECK_MD_OP(exp, unpackmd);
	EXP_MD_COUNTER_INCREMENT(exp, unpackmd);
	rc = MDP(exp->exp_obd, unpackmd)(exp, plsm, lmm, lmm_size);
	return rc;
}

/* OBD Metadata Support */

int obd_init_caches(void);
void obd_cleanup_caches(void);

/* support routines */
extern struct kmem_cache *obdo_cachep;

typedef int (*register_lwp_cb)(void *data);

struct lwp_register_item {
	struct obd_export **lri_exp;
	register_lwp_cb	    lri_cb_func;
	void		   *lri_cb_data;
	struct list_head    lri_list;
	char		    lri_name[MTI_NAME_MAXLEN];
};

/*
 * I'm as embarrassed about this as you are.
 *
 * <shaver> // XXX do not look into _superhack with remaining eye
 * <shaver> // XXX if this were any uglier, I'd get my own show on MTV
 */
extern int (*ptlrpc_put_connection_superhack)(struct ptlrpc_connection *c);

/* obd_mount.c */
int lustre_unregister_fs(void);
int lustre_register_fs(void);
int lustre_check_exclusion(struct super_block *sb, char *svname);

/* sysctl.c */
int obd_sysctl_init(void);

/* uuid.c  */
typedef __u8 class_uuid_t[16];
void class_uuid_unparse(class_uuid_t in, struct obd_uuid *out);

/* lustre_peer.c    */
int lustre_uuid_to_peer(const char *uuid, lnet_nid_t *peer_nid, int index);
int class_add_uuid(const char *uuid, __u64 nid);
int class_del_uuid(const char *uuid);
int class_check_uuid(struct obd_uuid *uuid, __u64 nid);
void class_init_uuidlist(void);
void class_exit_uuidlist(void);

/* class_obd.c */
extern char obd_jobid_node[];
extern struct miscdevice obd_psdev;
extern spinlock_t obd_types_lock;
int class_procfs_init(void);
int class_procfs_clean(void);

/* prng.c */
#define ll_generate_random_uuid(uuid_out) \
	get_random_bytes(uuid_out, sizeof(class_uuid_t))

/* statfs_pack.c */
struct kstatfs;
void statfs_pack(struct obd_statfs *osfs, struct kstatfs *sfs);
void statfs_unpack(struct kstatfs *sfs, struct obd_statfs *osfs);

/* root squash info */
struct rw_semaphore;
struct root_squash_info {
	uid_t			rsi_uid;
	gid_t			rsi_gid;
	struct list_head	rsi_nosquash_nids;
	struct rw_semaphore	rsi_sem;
};

/* linux-module.c */
int obd_ioctl_getdata(char **buf, int *len, void __user *arg);

#endif /* __LINUX_OBD_CLASS_H */

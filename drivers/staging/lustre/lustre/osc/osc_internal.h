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
 * Copyright (c) 2003, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright (c) 2011, 2012, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 */

#ifndef OSC_INTERNAL_H
#define OSC_INTERNAL_H

#define OAP_MAGIC 8675309

struct lu_env;

enum async_flags {
	ASYNC_READY = 0x1, /* ap_make_ready will not be called before this
			      page is added to an rpc */
	ASYNC_URGENT = 0x2, /* page must be put into an RPC before return */
	ASYNC_COUNT_STABLE = 0x4, /* ap_refresh_count will not be called
				     to give the caller a chance to update
				     or cancel the size of the io */
	ASYNC_HP = 0x10,
};

struct osc_async_page {
	int		     oap_magic;
	unsigned short	  oap_cmd;
	unsigned short	  oap_interrupted:1;

	struct list_head	      oap_pending_item;
	struct list_head	      oap_rpc_item;

	obd_off		 oap_obj_off;
	unsigned		oap_page_off;
	enum async_flags	oap_async_flags;

	struct brw_page	 oap_brw_page;

	struct ptlrpc_request   *oap_request;
	struct client_obd       *oap_cli;
	struct osc_object       *oap_obj;

	struct ldlm_lock	*oap_ldlm_lock;
	spinlock_t		 oap_lock;
};

#define oap_page	oap_brw_page.pg
#define oap_count       oap_brw_page.count
#define oap_brw_flags   oap_brw_page.flag

struct osc_cache_waiter {
	struct list_head	      ocw_entry;
	wait_queue_head_t	     ocw_waitq;
	struct osc_async_page  *ocw_oap;
	int		     ocw_grant;
	int		     ocw_rc;
};

int osc_create(const struct lu_env *env, struct obd_export *exp,
	       struct obdo *oa, struct lov_stripe_md **ea,
	       struct obd_trans_info *oti);
int osc_real_create(struct obd_export *exp, struct obdo *oa,
		    struct lov_stripe_md **ea, struct obd_trans_info *oti);
void osc_wake_cache_waiters(struct client_obd *cli);
int osc_shrink_grant_to_target(struct client_obd *cli, __u64 target_bytes);
void osc_update_next_shrink(struct client_obd *cli);

/*
 * cl integration.
 */
#include <cl_object.h>

extern struct ptlrpc_request_set *PTLRPCD_SET;

int osc_enqueue_base(struct obd_export *exp, struct ldlm_res_id *res_id,
		     __u64 *flags, ldlm_policy_data_t *policy,
		     struct ost_lvb *lvb, int kms_valid,
		     obd_enqueue_update_f upcall,
		     void *cookie, struct ldlm_enqueue_info *einfo,
		     struct lustre_handle *lockh,
		     struct ptlrpc_request_set *rqset, int async, int agl);
int osc_cancel_base(struct lustre_handle *lockh, __u32 mode);

int osc_match_base(struct obd_export *exp, struct ldlm_res_id *res_id,
		   __u32 type, ldlm_policy_data_t *policy, __u32 mode,
		   int *flags, void *data, struct lustre_handle *lockh,
		   int unref);

int osc_setattr_async_base(struct obd_export *exp, struct obd_info *oinfo,
			   struct obd_trans_info *oti,
			   obd_enqueue_update_f upcall, void *cookie,
			   struct ptlrpc_request_set *rqset);
int osc_punch_base(struct obd_export *exp, struct obd_info *oinfo,
		   obd_enqueue_update_f upcall, void *cookie,
		   struct ptlrpc_request_set *rqset);
int osc_sync_base(struct obd_export *exp, struct obd_info *oinfo,
		  obd_enqueue_update_f upcall, void *cookie,
		  struct ptlrpc_request_set *rqset);

int osc_process_config_base(struct obd_device *obd, struct lustre_cfg *cfg);
int osc_build_rpc(const struct lu_env *env, struct client_obd *cli,
		  struct list_head *ext_list, int cmd, pdl_policy_t p);
int osc_lru_shrink(struct client_obd *cli, int target);

extern spinlock_t osc_ast_guard;

int osc_cleanup(struct obd_device *obd);
int osc_setup(struct obd_device *obd, struct lustre_cfg *lcfg);

#ifdef LPROCFS
int lproc_osc_attach_seqstat(struct obd_device *dev);
void lprocfs_osc_init_vars(struct lprocfs_static_vars *lvars);
#else
static inline int lproc_osc_attach_seqstat(struct obd_device *dev) {return 0;}
static inline void lprocfs_osc_init_vars(struct lprocfs_static_vars *lvars)
{
	memset(lvars, 0, sizeof(*lvars));
}
#endif

extern struct lu_device_type osc_device_type;

static inline int osc_recoverable_error(int rc)
{
	return (rc == -EIO || rc == -EROFS || rc == -ENOMEM ||
		rc == -EAGAIN || rc == -EINPROGRESS);
}

static inline unsigned long rpcs_in_flight(struct client_obd *cli)
{
	return cli->cl_r_in_flight + cli->cl_w_in_flight;
}

#ifndef min_t
#define min_t(type,x,y) \
	({ type __x = (x); type __y = (y); __x < __y ? __x: __y; })
#endif

struct osc_device {
	struct cl_device    od_cl;
	struct obd_export  *od_exp;

	/* Write stats is actually protected by client_obd's lock. */
	struct osc_stats {
		uint64_t     os_lockless_writes;	  /* by bytes */
		uint64_t     os_lockless_reads;	   /* by bytes */
		uint64_t     os_lockless_truncates;       /* by times */
	} od_stats;

	/* configuration item(s) */
	int		 od_contention_time;
	int		 od_lockless_truncate;
};

static inline struct osc_device *obd2osc_dev(const struct obd_device *d)
{
	return container_of0(d->obd_lu_dev, struct osc_device, od_cl.cd_lu_dev);
}

int osc_dlm_lock_pageref(struct ldlm_lock *dlm);

extern struct kmem_cache *osc_quota_kmem;
struct osc_quota_info {
	/** linkage for quota hash table */
	struct hlist_node oqi_hash;
	obd_uid	  oqi_id;
};
int osc_quota_setup(struct obd_device *obd);
int osc_quota_cleanup(struct obd_device *obd);
int osc_quota_setdq(struct client_obd *cli, const unsigned int qid[],
		    obd_flag valid, obd_flag flags);
int osc_quota_chkdq(struct client_obd *cli, const unsigned int qid[]);
int osc_quotactl(struct obd_device *unused, struct obd_export *exp,
		 struct obd_quotactl *oqctl);
int osc_quotacheck(struct obd_device *unused, struct obd_export *exp,
		   struct obd_quotactl *oqctl);
int osc_quota_poll_check(struct obd_export *exp, struct if_quotacheck *qchk);

#endif /* OSC_INTERNAL_H */

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
 * Copyright (c) 2003, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright (c) 2011, 2015, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 */

#ifndef OSC_INTERNAL_H
#define OSC_INTERNAL_H

#define OAP_MAGIC 8675309

extern atomic_t osc_pool_req_count;
extern unsigned int osc_reqpool_maxreqcount;
extern struct ptlrpc_request_pool *osc_rq_pool;

struct lu_env;

enum async_flags {
	ASYNC_READY = 0x1, /* ap_make_ready will not be called before this
			    * page is added to an rpc
			    */
	ASYNC_URGENT = 0x2, /* page must be put into an RPC before return */
	ASYNC_COUNT_STABLE = 0x4, /* ap_refresh_count will not be called
				   * to give the caller a chance to update
				   * or cancel the size of the io
				   */
	ASYNC_HP = 0x10,
};

struct osc_async_page {
	int		     oap_magic;
	unsigned short	  oap_cmd;
	unsigned short	  oap_interrupted:1;

	struct list_head	      oap_pending_item;
	struct list_head	      oap_rpc_item;

	u64		 oap_obj_off;
	unsigned		oap_page_off;
	enum async_flags	oap_async_flags;

	struct brw_page	 oap_brw_page;

	struct ptlrpc_request   *oap_request;
	struct client_obd       *oap_cli;
	struct osc_object       *oap_obj;

	spinlock_t		 oap_lock;
};

#define oap_page	oap_brw_page.pg
#define oap_count       oap_brw_page.count
#define oap_brw_flags   oap_brw_page.flag

static inline struct osc_async_page *brw_page2oap(struct brw_page *pga)
{
	return (struct osc_async_page *)container_of(pga, struct osc_async_page,
						     oap_brw_page);
}

struct osc_cache_waiter {
	struct list_head	      ocw_entry;
	wait_queue_head_t	     ocw_waitq;
	struct osc_async_page  *ocw_oap;
	int		     ocw_grant;
	int		     ocw_rc;
};

void osc_wake_cache_waiters(struct client_obd *cli);
int osc_shrink_grant_to_target(struct client_obd *cli, __u64 target_bytes);
void osc_update_next_shrink(struct client_obd *cli);

/*
 * cl integration.
 */
#include "../include/cl_object.h"

extern struct ptlrpc_request_set *PTLRPCD_SET;

typedef int (*osc_enqueue_upcall_f)(void *cookie, struct lustre_handle *lockh,
				    int rc);

int osc_enqueue_base(struct obd_export *exp, struct ldlm_res_id *res_id,
		     __u64 *flags, ldlm_policy_data_t *policy,
		     struct ost_lvb *lvb, int kms_valid,
		     osc_enqueue_upcall_f upcall,
		     void *cookie, struct ldlm_enqueue_info *einfo,
		     struct ptlrpc_request_set *rqset, int async, int agl);

int osc_match_base(struct obd_export *exp, struct ldlm_res_id *res_id,
		   __u32 type, ldlm_policy_data_t *policy, __u32 mode,
		   __u64 *flags, void *data, struct lustre_handle *lockh,
		   int unref);

int osc_setattr_async(struct obd_export *exp, struct obdo *oa,
		      obd_enqueue_update_f upcall, void *cookie,
		      struct ptlrpc_request_set *rqset);
int osc_punch_base(struct obd_export *exp, struct obdo *oa,
		   obd_enqueue_update_f upcall, void *cookie,
		   struct ptlrpc_request_set *rqset);
int osc_sync_base(struct osc_object *exp, struct obdo *oa,
		  obd_enqueue_update_f upcall, void *cookie,
		  struct ptlrpc_request_set *rqset);

int osc_process_config_base(struct obd_device *obd, struct lustre_cfg *cfg);
int osc_build_rpc(const struct lu_env *env, struct client_obd *cli,
		  struct list_head *ext_list, int cmd);
long osc_lru_shrink(const struct lu_env *env, struct client_obd *cli,
		    long target, bool force);
long osc_lru_reclaim(struct client_obd *cli);

unsigned long osc_ldlm_weigh_ast(struct ldlm_lock *dlmlock);

int osc_setup(struct obd_device *obd, struct lustre_cfg *lcfg);

int lproc_osc_attach_seqstat(struct obd_device *dev);
void lprocfs_osc_init_vars(struct lprocfs_static_vars *lvars);

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

extern struct kmem_cache *osc_quota_kmem;
struct osc_quota_info {
	/** linkage for quota hash table */
	struct hlist_node oqi_hash;
	u32	  oqi_id;
};

int osc_quota_setup(struct obd_device *obd);
int osc_quota_cleanup(struct obd_device *obd);
int osc_quota_setdq(struct client_obd *cli, const unsigned int qid[],
		    u32 valid, u32 flags);
int osc_quota_chkdq(struct client_obd *cli, const unsigned int qid[]);
int osc_quotactl(struct obd_device *unused, struct obd_export *exp,
		 struct obd_quotactl *oqctl);
void osc_inc_unstable_pages(struct ptlrpc_request *req);
void osc_dec_unstable_pages(struct ptlrpc_request *req);
bool osc_over_unstable_soft_limit(struct client_obd *cli);

/**
 * Bit flags for osc_dlm_lock_at_pageoff().
 */
enum osc_dap_flags {
	/**
	 * Just check if the desired lock exists, it won't hold reference
	 * count on lock.
	 */
	OSC_DAP_FL_TEST_LOCK	= BIT(0),
	/**
	 * Return the lock even if it is being canceled.
	 */
	OSC_DAP_FL_CANCELING	= BIT(1),
};

struct ldlm_lock *osc_dlmlock_at_pgoff(const struct lu_env *env,
				       struct osc_object *obj, pgoff_t index,
				       enum osc_dap_flags flags);

#endif /* OSC_INTERNAL_H */

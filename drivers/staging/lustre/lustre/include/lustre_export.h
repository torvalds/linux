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
 * Copyright (c) 2002, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright (c) 2011, 2015, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 */
/** \defgroup obd_export PortalRPC export definitions
 *
 * @{
 */

#ifndef __EXPORT_H
#define __EXPORT_H

/** \defgroup export export
 *
 * @{
 */

#include "lprocfs_status.h"
#include "lustre/lustre_idl.h"
#include "lustre_dlm.h"

enum obd_option {
	OBD_OPT_FORCE =	 0x0001,
	OBD_OPT_FAILOVER =      0x0002,
	OBD_OPT_ABORT_RECOV =   0x0004,
};

/**
 * Export structure. Represents target-side of connection in portals.
 * Also used in Lustre to connect between layers on the same node when
 * there is no network-connection in-between.
 * For every connected client there is an export structure on the server
 * attached to the same obd device.
 */
struct obd_export {
	/**
	 * Export handle, it's id is provided to client on connect
	 * Subsequent client RPCs contain this handle id to identify
	 * what export they are talking to.
	 */
	struct portals_handle     exp_handle;
	atomic_t	      exp_refcount;
	/**
	 * Set of counters below is to track where export references are
	 * kept. The exp_rpc_count is used for reconnect handling also,
	 * the cb_count and locks_count are for debug purposes only for now.
	 * The sum of them should be less than exp_refcount by 3
	 */
	atomic_t	      exp_rpc_count; /* RPC references */
	atomic_t	      exp_cb_count; /* Commit callback references */
	/** Number of queued replay requests to be processes */
	atomic_t		  exp_replay_count;
	atomic_t	      exp_locks_count; /** Lock references */
#if LUSTRE_TRACKS_LOCK_EXP_REFS
	struct list_head		exp_locks_list;
	spinlock_t		  exp_locks_list_guard;
#endif
	/** UUID of client connected to this export */
	struct obd_uuid	   exp_client_uuid;
	/** To link all exports on an obd device */
	struct list_head		exp_obd_chain;
	struct hlist_node	  exp_uuid_hash; /** uuid-export hash*/
	/** Obd device of this export */
	struct obd_device	*exp_obd;
	/**
	 * "reverse" import to send requests (e.g. from ldlm) back to client
	 * exp_lock protect its change
	 */
	struct obd_import	*exp_imp_reverse;
	struct lprocfs_stats     *exp_md_stats;
	/** Active connection */
	struct ptlrpc_connection *exp_connection;
	/** Connection count value from last successful reconnect rpc */
	__u32		     exp_conn_cnt;
	/** Hash list of all ldlm locks granted on this export */
	struct cfs_hash	       *exp_lock_hash;
	/**
	 * Hash list for Posix lock deadlock detection, added with
	 * ldlm_lock::l_exp_flock_hash.
	 */
	struct cfs_hash	       *exp_flock_hash;
	struct list_head		exp_outstanding_replies;
	struct list_head		exp_uncommitted_replies;
	spinlock_t		  exp_uncommitted_replies_lock;
	/** Last committed transno for this export */
	__u64		     exp_last_committed;
	/** On replay all requests waiting for replay are linked here */
	struct list_head		exp_req_replay_queue;
	/**
	 * protects exp_flags, exp_outstanding_replies and the change
	 * of exp_imp_reverse
	 */
	spinlock_t		  exp_lock;
	/** Compatibility flags for this export are embedded into
	 *  exp_connect_data
	 */
	struct obd_connect_data   exp_connect_data;
	enum obd_option	   exp_flags;
	unsigned long	     exp_failed:1,
				  exp_disconnected:1,
				  exp_connecting:1,
				  exp_flvr_changed:1,
				  exp_flvr_adapt:1;
	/* also protected by exp_lock */
	enum lustre_sec_part      exp_sp_peer;
	struct sptlrpc_flavor     exp_flvr;	     /* current */
	struct sptlrpc_flavor     exp_flvr_old[2];      /* about-to-expire */
	time64_t		  exp_flvr_expire[2];   /* seconds */

	/** protects exp_hp_rpcs */
	spinlock_t		  exp_rpc_lock;
	struct list_head		  exp_hp_rpcs;	/* (potential) HP RPCs */

	/** blocking dlm lock list, protected by exp_bl_list_lock */
	struct list_head		exp_bl_list;
	spinlock_t		  exp_bl_list_lock;
};

static inline __u64 *exp_connect_flags_ptr(struct obd_export *exp)
{
	return &exp->exp_connect_data.ocd_connect_flags;
}

static inline __u64 exp_connect_flags(struct obd_export *exp)
{
	return *exp_connect_flags_ptr(exp);
}

static inline int exp_max_brw_size(struct obd_export *exp)
{
	if (exp_connect_flags(exp) & OBD_CONNECT_BRW_SIZE)
		return exp->exp_connect_data.ocd_brw_size;

	return ONE_MB_BRW_SIZE;
}

static inline int exp_connect_multibulk(struct obd_export *exp)
{
	return exp_max_brw_size(exp) > ONE_MB_BRW_SIZE;
}

static inline int exp_connect_cancelset(struct obd_export *exp)
{
	return !!(exp_connect_flags(exp) & OBD_CONNECT_CANCELSET);
}

static inline int exp_connect_lru_resize(struct obd_export *exp)
{
	return !!(exp_connect_flags(exp) & OBD_CONNECT_LRU_RESIZE);
}

static inline int exp_connect_rmtclient(struct obd_export *exp)
{
	return !!(exp_connect_flags(exp) & OBD_CONNECT_RMT_CLIENT);
}

static inline int client_is_remote(struct obd_export *exp)
{
	struct obd_import *imp = class_exp2cliimp(exp);

	return !!(imp->imp_connect_data.ocd_connect_flags &
		  OBD_CONNECT_RMT_CLIENT);
}

static inline int exp_connect_vbr(struct obd_export *exp)
{
	return !!(exp_connect_flags(exp) & OBD_CONNECT_VBR);
}

static inline int exp_connect_som(struct obd_export *exp)
{
	return !!(exp_connect_flags(exp) & OBD_CONNECT_SOM);
}

static inline int exp_connect_umask(struct obd_export *exp)
{
	return !!(exp_connect_flags(exp) & OBD_CONNECT_UMASK);
}

static inline int imp_connect_lru_resize(struct obd_import *imp)
{
	struct obd_connect_data *ocd;

	ocd = &imp->imp_connect_data;
	return !!(ocd->ocd_connect_flags & OBD_CONNECT_LRU_RESIZE);
}

static inline int exp_connect_layout(struct obd_export *exp)
{
	return !!(exp_connect_flags(exp) & OBD_CONNECT_LAYOUTLOCK);
}

static inline bool exp_connect_lvb_type(struct obd_export *exp)
{
	if (exp_connect_flags(exp) & OBD_CONNECT_LVB_TYPE)
		return true;
	else
		return false;
}

static inline bool imp_connect_lvb_type(struct obd_import *imp)
{
	struct obd_connect_data *ocd;

	ocd = &imp->imp_connect_data;
	if (ocd->ocd_connect_flags & OBD_CONNECT_LVB_TYPE)
		return true;
	else
		return false;
}

static inline __u64 exp_connect_ibits(struct obd_export *exp)
{
	struct obd_connect_data *ocd;

	ocd = &exp->exp_connect_data;
	return ocd->ocd_ibits_known;
}

static inline bool imp_connect_disp_stripe(struct obd_import *imp)
{
	struct obd_connect_data *ocd;

	ocd = &imp->imp_connect_data;
	return ocd->ocd_connect_flags & OBD_CONNECT_DISP_STRIPE;
}

struct obd_export *class_conn2export(struct lustre_handle *conn);

#define KKUC_CT_DATA_MAGIC	0x092013cea
struct kkuc_ct_data {
	__u32		kcd_magic;
	struct obd_uuid	kcd_uuid;
	__u32		kcd_archive;
};

/** @} export */

#endif /* __EXPORT_H */
/** @} obd_export */

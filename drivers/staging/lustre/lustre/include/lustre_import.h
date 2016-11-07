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
 * Copyright (c) 2002, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright (c) 2011, 2012, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 */
/** \defgroup obd_import PtlRPC import definitions
 * Imports are client-side representation of remote obd target.
 *
 * @{
 */

#ifndef __IMPORT_H
#define __IMPORT_H

/** \defgroup export export
 *
 * @{
 */

#include "lustre_handles.h"
#include "lustre/lustre_idl.h"

/**
 * Adaptive Timeout stuff
 *
 * @{
 */
#define D_ADAPTTO D_OTHER
#define AT_BINS 4		  /* "bin" means "N seconds of history" */
#define AT_FLG_NOHIST 0x1	  /* use last reported value only */

struct adaptive_timeout {
	time64_t	at_binstart;	 /* bin start time */
	unsigned int	at_hist[AT_BINS];    /* timeout history bins */
	unsigned int	at_flags;
	unsigned int	at_current;	  /* current timeout value */
	unsigned int	at_worst_ever;       /* worst-ever timeout value */
	time64_t	at_worst_time;       /* worst-ever timeout timestamp */
	spinlock_t	at_lock;
};

struct ptlrpc_at_array {
	struct list_head       *paa_reqs_array; /** array to hold requests */
	__u32	     paa_size;       /** the size of array */
	__u32	     paa_count;      /** the total count of reqs */
	time64_t     paa_deadline;   /** the earliest deadline of reqs */
	__u32	    *paa_reqs_count; /** the count of reqs in each entry */
};

#define IMP_AT_MAX_PORTALS 8
struct imp_at {
	int		     iat_portal[IMP_AT_MAX_PORTALS];
	struct adaptive_timeout iat_net_latency;
	struct adaptive_timeout iat_service_estimate[IMP_AT_MAX_PORTALS];
};

/** @} */

/** Possible import states */
enum lustre_imp_state {
	LUSTRE_IMP_CLOSED     = 1,
	LUSTRE_IMP_NEW	= 2,
	LUSTRE_IMP_DISCON     = 3,
	LUSTRE_IMP_CONNECTING = 4,
	LUSTRE_IMP_REPLAY     = 5,
	LUSTRE_IMP_REPLAY_LOCKS = 6,
	LUSTRE_IMP_REPLAY_WAIT  = 7,
	LUSTRE_IMP_RECOVER    = 8,
	LUSTRE_IMP_FULL       = 9,
	LUSTRE_IMP_EVICTED    = 10,
};

/** Returns test string representation of numeric import state \a state */
static inline char *ptlrpc_import_state_name(enum lustre_imp_state state)
{
	static char *import_state_names[] = {
		"<UNKNOWN>", "CLOSED",  "NEW", "DISCONN",
		"CONNECTING", "REPLAY", "REPLAY_LOCKS", "REPLAY_WAIT",
		"RECOVER", "FULL", "EVICTED",
	};

	LASSERT(state <= LUSTRE_IMP_EVICTED);
	return import_state_names[state];
}

/**
 * List of import event types
 */
enum obd_import_event {
	IMP_EVENT_DISCON     = 0x808001,
	IMP_EVENT_INACTIVE   = 0x808002,
	IMP_EVENT_INVALIDATE = 0x808003,
	IMP_EVENT_ACTIVE     = 0x808004,
	IMP_EVENT_OCD	= 0x808005,
	IMP_EVENT_DEACTIVATE = 0x808006,
	IMP_EVENT_ACTIVATE   = 0x808007,
};

/**
 * Definition of import connection structure
 */
struct obd_import_conn {
	/** Item for linking connections together */
	struct list_head		oic_item;
	/** Pointer to actual PortalRPC connection */
	struct ptlrpc_connection *oic_conn;
	/** uuid of remote side */
	struct obd_uuid	   oic_uuid;
	/**
	 * Time (64 bit jiffies) of last connection attempt on this connection
	 */
	__u64		     oic_last_attempt;
};

/* state history */
#define IMP_STATE_HIST_LEN 16
struct import_state_hist {
	enum lustre_imp_state ish_state;
	time64_t	ish_time;
};

/**
 * Definition of PortalRPC import structure.
 * Imports are representing client-side view to remote target.
 */
struct obd_import {
	/** Local handle (== id) for this import. */
	struct portals_handle     imp_handle;
	/** Reference counter */
	atomic_t	      imp_refcount;
	struct lustre_handle      imp_dlm_handle; /* client's ldlm export */
	/** Currently active connection */
	struct ptlrpc_connection *imp_connection;
	/** PortalRPC client structure for this import */
	struct ptlrpc_client     *imp_client;
	/** List element for linking into pinger chain */
	struct list_head		imp_pinger_chain;
	/** List element for linking into chain for destruction */
	struct list_head		imp_zombie_chain;

	/**
	 * Lists of requests that are retained for replay, waiting for a reply,
	 * or waiting for recovery to complete, respectively.
	 * @{
	 */
	struct list_head		imp_replay_list;
	struct list_head		imp_sending_list;
	struct list_head		imp_delayed_list;
	/** @} */

	/**
	 * List of requests that are retained for committed open replay. Once
	 * open is committed, open replay request will be moved from the
	 * imp_replay_list into the imp_committed_list.
	 * The imp_replay_cursor is for accelerating searching during replay.
	 * @{
	 */
	struct list_head		imp_committed_list;
	struct list_head	       *imp_replay_cursor;
	/** @} */

	/** obd device for this import */
	struct obd_device	*imp_obd;

	/**
	 * some seciruty-related fields
	 * @{
	 */
	struct ptlrpc_sec	*imp_sec;
	struct mutex		  imp_sec_mutex;
	time64_t		imp_sec_expire;
	/** @} */

	/** Wait queue for those who need to wait for recovery completion */
	wait_queue_head_t	       imp_recovery_waitq;

	/** Number of requests currently in-flight */
	atomic_t	      imp_inflight;
	/** Number of requests currently unregistering */
	atomic_t	      imp_unregistering;
	/** Number of replay requests inflight */
	atomic_t	      imp_replay_inflight;
	/** Number of currently happening import invalidations */
	atomic_t	      imp_inval_count;
	/** Numbner of request timeouts */
	atomic_t	      imp_timeouts;
	/** Current import state */
	enum lustre_imp_state     imp_state;
	/** Last replay state */
	enum lustre_imp_state	  imp_replay_state;
	/** History of import states */
	struct import_state_hist  imp_state_hist[IMP_STATE_HIST_LEN];
	int		       imp_state_hist_idx;
	/** Current import generation. Incremented on every reconnect */
	int		       imp_generation;
	/** Incremented every time we send reconnection request */
	__u32		     imp_conn_cnt;
       /**
	* \see ptlrpc_free_committed remembers imp_generation value here
	* after a check to save on unnecessary replay list iterations
	*/
	int		       imp_last_generation_checked;
	/** Last transno we replayed */
	__u64		     imp_last_replay_transno;
	/** Last transno committed on remote side */
	__u64		     imp_peer_committed_transno;
	/**
	 * \see ptlrpc_free_committed remembers last_transno since its last
	 * check here and if last_transno did not change since last run of
	 * ptlrpc_free_committed and import generation is the same, we can
	 * skip looking for requests to remove from replay list as optimisation
	 */
	__u64		     imp_last_transno_checked;
	/**
	 * Remote export handle. This is how remote side knows what export
	 * we are talking to. Filled from response to connect request
	 */
	struct lustre_handle      imp_remote_handle;
	/** When to perform next ping. time in jiffies. */
	unsigned long		imp_next_ping;
	/** When we last successfully connected. time in 64bit jiffies */
	__u64		     imp_last_success_conn;

	/** List of all possible connection for import. */
	struct list_head		imp_conn_list;
	/**
	 * Current connection. \a imp_connection is imp_conn_current->oic_conn
	 */
	struct obd_import_conn   *imp_conn_current;

	/** Protects flags, level, generation, conn_cnt, *_list */
	spinlock_t		  imp_lock;

	/* flags */
	unsigned long	     imp_no_timeout:1, /* timeouts are disabled */
				  imp_invalid:1,    /* evicted */
				  /* administratively disabled */
				  imp_deactive:1,
				  /* try to recover the import */
				  imp_replayable:1,
				  /* don't run recovery (timeout instead) */
				  imp_dlm_fake:1,
				  /* use 1/2 timeout on MDS' OSCs */
				  imp_server_timeout:1,
				  /* VBR: imp in delayed recovery */
				  imp_delayed_recovery:1,
				  /* VBR: if gap was found then no lock replays
				   */
				  imp_no_lock_replay:1,
				  /* recovery by versions was failed */
				  imp_vbr_failed:1,
				  /* force an immediate ping */
				  imp_force_verify:1,
				  /* force a scheduled ping */
				  imp_force_next_verify:1,
				  /* pingable */
				  imp_pingable:1,
				  /* resend for replay */
				  imp_resend_replay:1,
				  /* disable normal recovery, for test only. */
				  imp_no_pinger_recover:1,
#if OBD_OCD_VERSION(3, 0, 53, 0) > LUSTRE_VERSION_CODE
				  /* need IR MNE swab */
				  imp_need_mne_swab:1,
#endif
				  /* import must be reconnected instead of
				   * chosing new connection
				   */
				  imp_force_reconnect:1,
				  /* import has tried to connect with server */
				  imp_connect_tried:1;
	__u32		     imp_connect_op;
	struct obd_connect_data   imp_connect_data;
	__u64		     imp_connect_flags_orig;
	int		       imp_connect_error;

	__u32		     imp_msg_magic;
	__u32		     imp_msghdr_flags;       /* adjusted based on server capability */

	struct imp_at	     imp_at;		 /* adaptive timeout data */
	time64_t	     imp_last_reply_time;    /* for health check */
};

/* import.c */
static inline unsigned int at_est2timeout(unsigned int val)
{
	/* add an arbitrary minimum: 125% +5 sec */
	return (val + (val >> 2) + 5);
}

static inline unsigned int at_timeout2est(unsigned int val)
{
	/* restore estimate value from timeout: e=4/5(t-5) */
	LASSERT(val);
	return (max((val << 2) / 5, 5U) - 4);
}

static inline void at_reset(struct adaptive_timeout *at, int val)
{
	spin_lock(&at->at_lock);
	at->at_current = val;
	at->at_worst_ever = val;
	at->at_worst_time = ktime_get_real_seconds();
	spin_unlock(&at->at_lock);
}

static inline void at_init(struct adaptive_timeout *at, int val, int flags)
{
	memset(at, 0, sizeof(*at));
	spin_lock_init(&at->at_lock);
	at->at_flags = flags;
	at_reset(at, val);
}

extern unsigned int at_min;
static inline int at_get(struct adaptive_timeout *at)
{
	return (at->at_current > at_min) ? at->at_current : at_min;
}

int at_measured(struct adaptive_timeout *at, unsigned int val);
int import_at_get_index(struct obd_import *imp, int portal);
extern unsigned int at_max;
#define AT_OFF (at_max == 0)

/* genops.c */
struct obd_export;
struct obd_import *class_exp2cliimp(struct obd_export *);

/** @} import */

#endif /* __IMPORT_H */

/** @} obd_import */

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
 *
 * lustre/ptlrpc/import.c
 *
 * Author: Mike Shaver <shaver@clusterfs.com>
 */

#define DEBUG_SUBSYSTEM S_RPC

#include "../include/obd_support.h"
#include "../include/lustre_ha.h"
#include "../include/lustre_net.h"
#include "../include/lustre_import.h"
#include "../include/lustre_export.h"
#include "../include/obd.h"
#include "../include/obd_cksum.h"
#include "../include/obd_class.h"

#include "ptlrpc_internal.h"

struct ptlrpc_connect_async_args {
	 __u64 pcaa_peer_committed;
	int pcaa_initial_connect;
};

/**
 * Updates import \a imp current state to provided \a state value
 * Helper function. Must be called under imp_lock.
 */
static void __import_set_state(struct obd_import *imp,
			       enum lustre_imp_state state)
{
	switch (state) {
	case LUSTRE_IMP_CLOSED:
	case LUSTRE_IMP_NEW:
	case LUSTRE_IMP_DISCON:
	case LUSTRE_IMP_CONNECTING:
		break;
	case LUSTRE_IMP_REPLAY_WAIT:
		imp->imp_replay_state = LUSTRE_IMP_REPLAY_LOCKS;
		break;
	default:
		imp->imp_replay_state = LUSTRE_IMP_REPLAY;
	}

	imp->imp_state = state;
	imp->imp_state_hist[imp->imp_state_hist_idx].ish_state = state;
	imp->imp_state_hist[imp->imp_state_hist_idx].ish_time =
		get_seconds();
	imp->imp_state_hist_idx = (imp->imp_state_hist_idx + 1) %
		IMP_STATE_HIST_LEN;
}

/* A CLOSED import should remain so. */
#define IMPORT_SET_STATE_NOLOCK(imp, state)				       \
do {									       \
	if (imp->imp_state != LUSTRE_IMP_CLOSED) {			       \
		CDEBUG(D_HA, "%p %s: changing import state from %s to %s\n",   \
		       imp, obd2cli_tgt(imp->imp_obd),			       \
		       ptlrpc_import_state_name(imp->imp_state),	       \
		       ptlrpc_import_state_name(state));		       \
		__import_set_state(imp, state);				       \
	}								       \
} while (0)

#define IMPORT_SET_STATE(imp, state)					\
do {									\
	spin_lock(&imp->imp_lock);					\
	IMPORT_SET_STATE_NOLOCK(imp, state);				\
	spin_unlock(&imp->imp_lock);					\
} while (0)


static int ptlrpc_connect_interpret(const struct lu_env *env,
				    struct ptlrpc_request *request,
				    void *data, int rc);
int ptlrpc_import_recovery_state_machine(struct obd_import *imp);

/* Only this function is allowed to change the import state when it is
 * CLOSED. I would rather refcount the import and free it after
 * disconnection like we do with exports. To do that, the client_obd
 * will need to save the peer info somewhere other than in the import,
 * though. */
int ptlrpc_init_import(struct obd_import *imp)
{
	spin_lock(&imp->imp_lock);

	imp->imp_generation++;
	imp->imp_state =  LUSTRE_IMP_NEW;

	spin_unlock(&imp->imp_lock);

	return 0;
}
EXPORT_SYMBOL(ptlrpc_init_import);

#define UUID_STR "_UUID"
void deuuidify(char *uuid, const char *prefix, char **uuid_start, int *uuid_len)
{
	*uuid_start = !prefix || strncmp(uuid, prefix, strlen(prefix))
		? uuid : uuid + strlen(prefix);

	*uuid_len = strlen(*uuid_start);

	if (*uuid_len < strlen(UUID_STR))
		return;

	if (!strncmp(*uuid_start + *uuid_len - strlen(UUID_STR),
		    UUID_STR, strlen(UUID_STR)))
		*uuid_len -= strlen(UUID_STR);
}
EXPORT_SYMBOL(deuuidify);

/**
 * Returns true if import was FULL, false if import was already not
 * connected.
 * @imp - import to be disconnected
 * @conn_cnt - connection count (epoch) of the request that timed out
 *	     and caused the disconnection.  In some cases, multiple
 *	     inflight requests can fail to a single target (e.g. OST
 *	     bulk requests) and if one has already caused a reconnection
 *	     (increasing the import->conn_cnt) the older failure should
 *	     not also cause a reconnection.  If zero it forces a reconnect.
 */
int ptlrpc_set_import_discon(struct obd_import *imp, __u32 conn_cnt)
{
	int rc = 0;

	spin_lock(&imp->imp_lock);

	if (imp->imp_state == LUSTRE_IMP_FULL &&
	    (conn_cnt == 0 || conn_cnt == imp->imp_conn_cnt)) {
		char *target_start;
		int   target_len;

		deuuidify(obd2cli_tgt(imp->imp_obd), NULL,
			  &target_start, &target_len);

		if (imp->imp_replayable) {
			LCONSOLE_WARN("%s: Connection to %.*s (at %s) was lost; in progress operations using this service will wait for recovery to complete\n",
				      imp->imp_obd->obd_name, target_len, target_start,
				      libcfs_nid2str(imp->imp_connection->c_peer.nid));
		} else {
			LCONSOLE_ERROR_MSG(0x166, "%s: Connection to %.*s (at %s) was lost; in progress operations using this service will fail\n",
					   imp->imp_obd->obd_name,
					   target_len, target_start,
					   libcfs_nid2str(imp->imp_connection->c_peer.nid));
		}
		IMPORT_SET_STATE_NOLOCK(imp, LUSTRE_IMP_DISCON);
		spin_unlock(&imp->imp_lock);

		if (obd_dump_on_timeout)
			libcfs_debug_dumplog();

		obd_import_event(imp->imp_obd, imp, IMP_EVENT_DISCON);
		rc = 1;
	} else {
		spin_unlock(&imp->imp_lock);
		CDEBUG(D_HA, "%s: import %p already %s (conn %u, was %u): %s\n",
		       imp->imp_client->cli_name, imp,
		       (imp->imp_state == LUSTRE_IMP_FULL &&
			imp->imp_conn_cnt > conn_cnt) ?
		       "reconnected" : "not connected", imp->imp_conn_cnt,
		       conn_cnt, ptlrpc_import_state_name(imp->imp_state));
	}

	return rc;
}

/* Must be called with imp_lock held! */
static void ptlrpc_deactivate_and_unlock_import(struct obd_import *imp)
{
	assert_spin_locked(&imp->imp_lock);

	CDEBUG(D_HA, "setting import %s INVALID\n", obd2cli_tgt(imp->imp_obd));
	imp->imp_invalid = 1;
	imp->imp_generation++;
	spin_unlock(&imp->imp_lock);

	ptlrpc_abort_inflight(imp);
	obd_import_event(imp->imp_obd, imp, IMP_EVENT_INACTIVE);
}

/*
 * This acts as a barrier; all existing requests are rejected, and
 * no new requests will be accepted until the import is valid again.
 */
void ptlrpc_deactivate_import(struct obd_import *imp)
{
	spin_lock(&imp->imp_lock);
	ptlrpc_deactivate_and_unlock_import(imp);
}
EXPORT_SYMBOL(ptlrpc_deactivate_import);

static unsigned int
ptlrpc_inflight_deadline(struct ptlrpc_request *req, time_t now)
{
	long dl;

	if (!(((req->rq_phase == RQ_PHASE_RPC) && !req->rq_waiting) ||
	      (req->rq_phase == RQ_PHASE_BULK) ||
	      (req->rq_phase == RQ_PHASE_NEW)))
		return 0;

	if (req->rq_timedout)
		return 0;

	if (req->rq_phase == RQ_PHASE_NEW)
		dl = req->rq_sent;
	else
		dl = req->rq_deadline;

	if (dl <= now)
		return 0;

	return dl - now;
}

static unsigned int ptlrpc_inflight_timeout(struct obd_import *imp)
{
	time_t now = get_seconds();
	struct list_head *tmp, *n;
	struct ptlrpc_request *req;
	unsigned int timeout = 0;

	spin_lock(&imp->imp_lock);
	list_for_each_safe(tmp, n, &imp->imp_sending_list) {
		req = list_entry(tmp, struct ptlrpc_request, rq_list);
		timeout = max(ptlrpc_inflight_deadline(req, now), timeout);
	}
	spin_unlock(&imp->imp_lock);
	return timeout;
}

/**
 * This function will invalidate the import, if necessary, then block
 * for all the RPC completions, and finally notify the obd to
 * invalidate its state (ie cancel locks, clear pending requests,
 * etc).
 */
void ptlrpc_invalidate_import(struct obd_import *imp)
{
	struct list_head *tmp, *n;
	struct ptlrpc_request *req;
	struct l_wait_info lwi;
	unsigned int timeout;
	int rc;

	atomic_inc(&imp->imp_inval_count);

	if (!imp->imp_invalid || imp->imp_obd->obd_no_recov)
		ptlrpc_deactivate_import(imp);

	CFS_FAIL_TIMEOUT(OBD_FAIL_MGS_CONNECT_NET, 3 * cfs_fail_val / 2);
	LASSERT(imp->imp_invalid);

	/* Wait forever until inflight == 0. We really can't do it another
	 * way because in some cases we need to wait for very long reply
	 * unlink. We can't do anything before that because there is really
	 * no guarantee that some rdma transfer is not in progress right now. */
	do {
		/* Calculate max timeout for waiting on rpcs to error
		 * out. Use obd_timeout if calculated value is smaller
		 * than it. */
		if (!OBD_FAIL_CHECK(OBD_FAIL_PTLRPC_LONG_REPL_UNLINK)) {
			timeout = ptlrpc_inflight_timeout(imp);
			timeout += timeout / 3;

			if (timeout == 0)
				timeout = obd_timeout;
		} else {
			/* decrease the interval to increase race condition */
			timeout = 1;
		}

		CDEBUG(D_RPCTRACE,
		       "Sleeping %d sec for inflight to error out\n",
		       timeout);

		/* Wait for all requests to error out and call completion
		 * callbacks. Cap it at obd_timeout -- these should all
		 * have been locally cancelled by ptlrpc_abort_inflight. */
		lwi = LWI_TIMEOUT_INTERVAL(
			cfs_timeout_cap(cfs_time_seconds(timeout)),
			(timeout > 1)?cfs_time_seconds(1):cfs_time_seconds(1)/2,
			NULL, NULL);
		rc = l_wait_event(imp->imp_recovery_waitq,
				  (atomic_read(&imp->imp_inflight) == 0),
				  &lwi);
		if (rc) {
			const char *cli_tgt = obd2cli_tgt(imp->imp_obd);

			CERROR("%s: rc = %d waiting for callback (%d != 0)\n",
			       cli_tgt, rc,
			       atomic_read(&imp->imp_inflight));

			spin_lock(&imp->imp_lock);
			if (atomic_read(&imp->imp_inflight) == 0) {
				int count = atomic_read(&imp->imp_unregistering);

				/* We know that "unregistering" rpcs only can
				 * survive in sending or delaying lists (they
				 * maybe waiting for long reply unlink in
				 * sluggish nets). Let's check this. If there
				 * is no inflight and unregistering != 0, this
				 * is bug. */
				LASSERTF(count == 0, "Some RPCs are still unregistering: %d\n",
					 count);

				/* Let's save one loop as soon as inflight have
				 * dropped to zero. No new inflights possible at
				 * this point. */
				rc = 0;
			} else {
				list_for_each_safe(tmp, n,
						       &imp->imp_sending_list) {
					req = list_entry(tmp,
							     struct ptlrpc_request,
							     rq_list);
					DEBUG_REQ(D_ERROR, req,
						  "still on sending list");
				}
				list_for_each_safe(tmp, n,
						       &imp->imp_delayed_list) {
					req = list_entry(tmp,
							     struct ptlrpc_request,
							     rq_list);
					DEBUG_REQ(D_ERROR, req,
						  "still on delayed list");
				}

				CERROR("%s: RPCs in \"%s\" phase found (%d). Network is sluggish? Waiting them to error out.\n",
				       cli_tgt,
				       ptlrpc_phase2str(RQ_PHASE_UNREGISTERING),
				       atomic_read(&imp->
						   imp_unregistering));
			}
			spin_unlock(&imp->imp_lock);
		  }
	} while (rc != 0);

	/*
	 * Let's additionally check that no new rpcs added to import in
	 * "invalidate" state.
	 */
	LASSERT(atomic_read(&imp->imp_inflight) == 0);
	obd_import_event(imp->imp_obd, imp, IMP_EVENT_INVALIDATE);
	sptlrpc_import_flush_all_ctx(imp);

	atomic_dec(&imp->imp_inval_count);
	wake_up_all(&imp->imp_recovery_waitq);
}
EXPORT_SYMBOL(ptlrpc_invalidate_import);

/* unset imp_invalid */
void ptlrpc_activate_import(struct obd_import *imp)
{
	struct obd_device *obd = imp->imp_obd;

	spin_lock(&imp->imp_lock);
	if (imp->imp_deactive != 0) {
		spin_unlock(&imp->imp_lock);
		return;
	}

	imp->imp_invalid = 0;
	spin_unlock(&imp->imp_lock);
	obd_import_event(obd, imp, IMP_EVENT_ACTIVE);
}
EXPORT_SYMBOL(ptlrpc_activate_import);

static void ptlrpc_pinger_force(struct obd_import *imp)
{
	CDEBUG(D_HA, "%s: waking up pinger s:%s\n", obd2cli_tgt(imp->imp_obd),
	       ptlrpc_import_state_name(imp->imp_state));

	spin_lock(&imp->imp_lock);
	imp->imp_force_verify = 1;
	spin_unlock(&imp->imp_lock);

	if (imp->imp_state != LUSTRE_IMP_CONNECTING)
		ptlrpc_pinger_wake_up();
}

void ptlrpc_fail_import(struct obd_import *imp, __u32 conn_cnt)
{
	LASSERT(!imp->imp_dlm_fake);

	if (ptlrpc_set_import_discon(imp, conn_cnt)) {
		if (!imp->imp_replayable) {
			CDEBUG(D_HA, "import %s@%s for %s not replayable, auto-deactivating\n",
			       obd2cli_tgt(imp->imp_obd),
			       imp->imp_connection->c_remote_uuid.uuid,
			       imp->imp_obd->obd_name);
			ptlrpc_deactivate_import(imp);
		}

		ptlrpc_pinger_force(imp);
	}
}
EXPORT_SYMBOL(ptlrpc_fail_import);

int ptlrpc_reconnect_import(struct obd_import *imp)
{
#ifdef ENABLE_PINGER
	struct l_wait_info lwi;
	int secs = cfs_time_seconds(obd_timeout);
	int rc;

	ptlrpc_pinger_force(imp);

	CDEBUG(D_HA, "%s: recovery started, waiting %u seconds\n",
	       obd2cli_tgt(imp->imp_obd), secs);

	lwi = LWI_TIMEOUT(secs, NULL, NULL);
	rc = l_wait_event(imp->imp_recovery_waitq,
			  !ptlrpc_import_in_recovery(imp), &lwi);
	CDEBUG(D_HA, "%s: recovery finished s:%s\n", obd2cli_tgt(imp->imp_obd),
	       ptlrpc_import_state_name(imp->imp_state));
	return rc;
#else
	ptlrpc_set_import_discon(imp, 0);
	/* Force a new connect attempt */
	ptlrpc_invalidate_import(imp);
	/* Do a fresh connect next time by zeroing the handle */
	ptlrpc_disconnect_import(imp, 1);
	/* Wait for all invalidate calls to finish */
	if (atomic_read(&imp->imp_inval_count) > 0) {
		int rc;
		struct l_wait_info lwi = LWI_INTR(LWI_ON_SIGNAL_NOOP, NULL);
		rc = l_wait_event(imp->imp_recovery_waitq,
				  (atomic_read(&imp->imp_inval_count) == 0),
				  &lwi);
		if (rc)
			CERROR("Interrupted, inval=%d\n",
			       atomic_read(&imp->imp_inval_count));
	}

	/* Allow reconnect attempts */
	imp->imp_obd->obd_no_recov = 0;
	/* Remove 'invalid' flag */
	ptlrpc_activate_import(imp);
	/* Attempt a new connect */
	ptlrpc_recover_import(imp, NULL, 0);
	return 0;
#endif
}
EXPORT_SYMBOL(ptlrpc_reconnect_import);

/**
 * Connection on import \a imp is changed to another one (if more than one is
 * present). We typically chose connection that we have not tried to connect to
 * the longest
 */
static int import_select_connection(struct obd_import *imp)
{
	struct obd_import_conn *imp_conn = NULL, *conn;
	struct obd_export *dlmexp;
	char *target_start;
	int target_len, tried_all = 1;

	spin_lock(&imp->imp_lock);

	if (list_empty(&imp->imp_conn_list)) {
		CERROR("%s: no connections available\n",
		       imp->imp_obd->obd_name);
		spin_unlock(&imp->imp_lock);
		return -EINVAL;
	}

	list_for_each_entry(conn, &imp->imp_conn_list, oic_item) {
		CDEBUG(D_HA, "%s: connect to NID %s last attempt %llu\n",
		       imp->imp_obd->obd_name,
		       libcfs_nid2str(conn->oic_conn->c_peer.nid),
		       conn->oic_last_attempt);

		/* If we have not tried this connection since
		   the last successful attempt, go with this one */
		if ((conn->oic_last_attempt == 0) ||
		    cfs_time_beforeq_64(conn->oic_last_attempt,
				       imp->imp_last_success_conn)) {
			imp_conn = conn;
			tried_all = 0;
			break;
		}

		/* If all of the connections have already been tried
		   since the last successful connection; just choose the
		   least recently used */
		if (!imp_conn)
			imp_conn = conn;
		else if (cfs_time_before_64(conn->oic_last_attempt,
					    imp_conn->oic_last_attempt))
			imp_conn = conn;
	}

	/* if not found, simply choose the current one */
	if (!imp_conn || imp->imp_force_reconnect) {
		LASSERT(imp->imp_conn_current);
		imp_conn = imp->imp_conn_current;
		tried_all = 0;
	}
	LASSERT(imp_conn->oic_conn);

	/* If we've tried everything, and we're back to the beginning of the
	   list, increase our timeout and try again. It will be reset when
	   we do finally connect. (FIXME: really we should wait for all network
	   state associated with the last connection attempt to drain before
	   trying to reconnect on it.) */
	if (tried_all && (imp->imp_conn_list.next == &imp_conn->oic_item)) {
		struct adaptive_timeout *at = &imp->imp_at.iat_net_latency;
		if (at_get(at) < CONNECTION_SWITCH_MAX) {
			at_measured(at, at_get(at) + CONNECTION_SWITCH_INC);
			if (at_get(at) > CONNECTION_SWITCH_MAX)
				at_reset(at, CONNECTION_SWITCH_MAX);
		}
		LASSERT(imp_conn->oic_last_attempt);
		CDEBUG(D_HA, "%s: tried all connections, increasing latency to %ds\n",
		       imp->imp_obd->obd_name, at_get(at));
	}

	imp_conn->oic_last_attempt = cfs_time_current_64();

	/* switch connection, don't mind if it's same as the current one */
	if (imp->imp_connection)
		ptlrpc_connection_put(imp->imp_connection);
	imp->imp_connection = ptlrpc_connection_addref(imp_conn->oic_conn);

	dlmexp =  class_conn2export(&imp->imp_dlm_handle);
	LASSERT(dlmexp != NULL);
	if (dlmexp->exp_connection)
		ptlrpc_connection_put(dlmexp->exp_connection);
	dlmexp->exp_connection = ptlrpc_connection_addref(imp_conn->oic_conn);
	class_export_put(dlmexp);

	if (imp->imp_conn_current != imp_conn) {
		if (imp->imp_conn_current) {
			deuuidify(obd2cli_tgt(imp->imp_obd), NULL,
				  &target_start, &target_len);

			CDEBUG(D_HA, "%s: Connection changing to %.*s (at %s)\n",
			       imp->imp_obd->obd_name,
			       target_len, target_start,
			       libcfs_nid2str(imp_conn->oic_conn->c_peer.nid));
		}

		imp->imp_conn_current = imp_conn;
	}

	CDEBUG(D_HA, "%s: import %p using connection %s/%s\n",
	       imp->imp_obd->obd_name, imp, imp_conn->oic_uuid.uuid,
	       libcfs_nid2str(imp_conn->oic_conn->c_peer.nid));

	spin_unlock(&imp->imp_lock);

	return 0;
}

/*
 * must be called under imp_lock
 */
static int ptlrpc_first_transno(struct obd_import *imp, __u64 *transno)
{
	struct ptlrpc_request *req;
	struct list_head *tmp;

	/* The requests in committed_list always have smaller transnos than
	 * the requests in replay_list */
	if (!list_empty(&imp->imp_committed_list)) {
		tmp = imp->imp_committed_list.next;
		req = list_entry(tmp, struct ptlrpc_request, rq_replay_list);
		*transno = req->rq_transno;
		if (req->rq_transno == 0) {
			DEBUG_REQ(D_ERROR, req,
				  "zero transno in committed_list");
			LBUG();
		}
		return 1;
	}
	if (!list_empty(&imp->imp_replay_list)) {
		tmp = imp->imp_replay_list.next;
		req = list_entry(tmp, struct ptlrpc_request, rq_replay_list);
		*transno = req->rq_transno;
		if (req->rq_transno == 0) {
			DEBUG_REQ(D_ERROR, req, "zero transno in replay_list");
			LBUG();
		}
		return 1;
	}
	return 0;
}

/**
 * Attempt to (re)connect import \a imp. This includes all preparations,
 * initializing CONNECT RPC request and passing it to ptlrpcd for
 * actual sending.
 * Returns 0 on success or error code.
 */
int ptlrpc_connect_import(struct obd_import *imp)
{
	struct obd_device *obd = imp->imp_obd;
	int initial_connect = 0;
	int set_transno = 0;
	__u64 committed_before_reconnect = 0;
	struct ptlrpc_request *request;
	char *bufs[] = { NULL,
			 obd2cli_tgt(imp->imp_obd),
			 obd->obd_uuid.uuid,
			 (char *)&imp->imp_dlm_handle,
			 (char *)&imp->imp_connect_data };
	struct ptlrpc_connect_async_args *aa;
	int rc;

	spin_lock(&imp->imp_lock);
	if (imp->imp_state == LUSTRE_IMP_CLOSED) {
		spin_unlock(&imp->imp_lock);
		CERROR("can't connect to a closed import\n");
		return -EINVAL;
	} else if (imp->imp_state == LUSTRE_IMP_FULL) {
		spin_unlock(&imp->imp_lock);
		CERROR("already connected\n");
		return 0;
	} else if (imp->imp_state == LUSTRE_IMP_CONNECTING) {
		spin_unlock(&imp->imp_lock);
		CERROR("already connecting\n");
		return -EALREADY;
	}

	IMPORT_SET_STATE_NOLOCK(imp, LUSTRE_IMP_CONNECTING);

	imp->imp_conn_cnt++;
	imp->imp_resend_replay = 0;

	if (!lustre_handle_is_used(&imp->imp_remote_handle))
		initial_connect = 1;
	else
		committed_before_reconnect = imp->imp_peer_committed_transno;

	set_transno = ptlrpc_first_transno(imp,
					   &imp->imp_connect_data.ocd_transno);
	spin_unlock(&imp->imp_lock);

	rc = import_select_connection(imp);
	if (rc)
		goto out;

	rc = sptlrpc_import_sec_adapt(imp, NULL, NULL);
	if (rc)
		goto out;

	/* Reset connect flags to the originally requested flags, in case
	 * the server is updated on-the-fly we will get the new features. */
	imp->imp_connect_data.ocd_connect_flags = imp->imp_connect_flags_orig;
	/* Reset ocd_version each time so the server knows the exact versions */
	imp->imp_connect_data.ocd_version = LUSTRE_VERSION_CODE;
	imp->imp_msghdr_flags &= ~MSGHDR_AT_SUPPORT;
	imp->imp_msghdr_flags &= ~MSGHDR_CKSUM_INCOMPAT18;

	rc = obd_reconnect(NULL, imp->imp_obd->obd_self_export, obd,
			   &obd->obd_uuid, &imp->imp_connect_data, NULL);
	if (rc)
		goto out;

	request = ptlrpc_request_alloc(imp, &RQF_MDS_CONNECT);
	if (request == NULL) {
		rc = -ENOMEM;
		goto out;
	}

	rc = ptlrpc_request_bufs_pack(request, LUSTRE_OBD_VERSION,
				      imp->imp_connect_op, bufs, NULL);
	if (rc) {
		ptlrpc_request_free(request);
		goto out;
	}

	/* Report the rpc service time to the server so that it knows how long
	 * to wait for clients to join recovery */
	lustre_msg_set_service_time(request->rq_reqmsg,
				    at_timeout2est(request->rq_timeout));

	/* The amount of time we give the server to process the connect req.
	 * import_select_connection will increase the net latency on
	 * repeated reconnect attempts to cover slow networks.
	 * We override/ignore the server rpc completion estimate here,
	 * which may be large if this is a reconnect attempt */
	request->rq_timeout = INITIAL_CONNECT_TIMEOUT;
	lustre_msg_set_timeout(request->rq_reqmsg, request->rq_timeout);

	lustre_msg_add_op_flags(request->rq_reqmsg, MSG_CONNECT_NEXT_VER);

	request->rq_no_resend = request->rq_no_delay = 1;
	request->rq_send_state = LUSTRE_IMP_CONNECTING;
	/* Allow a slightly larger reply for future growth compatibility */
	req_capsule_set_size(&request->rq_pill, &RMF_CONNECT_DATA, RCL_SERVER,
			     sizeof(struct obd_connect_data)+16*sizeof(__u64));
	ptlrpc_request_set_replen(request);
	request->rq_interpret_reply = ptlrpc_connect_interpret;

	CLASSERT(sizeof(*aa) <= sizeof(request->rq_async_args));
	aa = ptlrpc_req_async_args(request);
	memset(aa, 0, sizeof(*aa));

	aa->pcaa_peer_committed = committed_before_reconnect;
	aa->pcaa_initial_connect = initial_connect;

	if (aa->pcaa_initial_connect) {
		spin_lock(&imp->imp_lock);
		imp->imp_replayable = 1;
		spin_unlock(&imp->imp_lock);
		lustre_msg_add_op_flags(request->rq_reqmsg,
					MSG_CONNECT_INITIAL);
	}

	if (set_transno)
		lustre_msg_add_op_flags(request->rq_reqmsg,
					MSG_CONNECT_TRANSNO);

	DEBUG_REQ(D_RPCTRACE, request, "(re)connect request (timeout %d)",
		  request->rq_timeout);
	ptlrpcd_add_req(request, PDL_POLICY_ROUND, -1);
	rc = 0;
out:
	if (rc != 0) {
		IMPORT_SET_STATE(imp, LUSTRE_IMP_DISCON);
	}

	return rc;
}
EXPORT_SYMBOL(ptlrpc_connect_import);

static void ptlrpc_maybe_ping_import_soon(struct obd_import *imp)
{
	int force_verify;

	spin_lock(&imp->imp_lock);
	force_verify = imp->imp_force_verify != 0;
	spin_unlock(&imp->imp_lock);

	if (force_verify)
		ptlrpc_pinger_wake_up();
}

static int ptlrpc_busy_reconnect(int rc)
{
	return (rc == -EBUSY) || (rc == -EAGAIN);
}

/**
 * interpret_reply callback for connect RPCs.
 * Looks into returned status of connect operation and decides
 * what to do with the import - i.e enter recovery, promote it to
 * full state for normal operations of disconnect it due to an error.
 */
static int ptlrpc_connect_interpret(const struct lu_env *env,
				    struct ptlrpc_request *request,
				    void *data, int rc)
{
	struct ptlrpc_connect_async_args *aa = data;
	struct obd_import *imp = request->rq_import;
	struct client_obd *cli = &imp->imp_obd->u.cli;
	struct lustre_handle old_hdl;
	__u64 old_connect_flags;
	int msg_flags;
	struct obd_connect_data *ocd;
	struct obd_export *exp;
	int ret;

	spin_lock(&imp->imp_lock);
	if (imp->imp_state == LUSTRE_IMP_CLOSED) {
		imp->imp_connect_tried = 1;
		spin_unlock(&imp->imp_lock);
		return 0;
	}

	if (rc) {
		/* if this reconnect to busy export - not need select new target
		 * for connecting*/
		imp->imp_force_reconnect = ptlrpc_busy_reconnect(rc);
		spin_unlock(&imp->imp_lock);
		ptlrpc_maybe_ping_import_soon(imp);
		goto out;
	}
	spin_unlock(&imp->imp_lock);

	LASSERT(imp->imp_conn_current);

	msg_flags = lustre_msg_get_op_flags(request->rq_repmsg);

	ret = req_capsule_get_size(&request->rq_pill, &RMF_CONNECT_DATA,
				   RCL_SERVER);
	/* server replied obd_connect_data is always bigger */
	ocd = req_capsule_server_sized_get(&request->rq_pill,
					   &RMF_CONNECT_DATA, ret);

	if (ocd == NULL) {
		CERROR("%s: no connect data from server\n",
		       imp->imp_obd->obd_name);
		rc = -EPROTO;
		goto out;
	}

	spin_lock(&imp->imp_lock);

	/* All imports are pingable */
	imp->imp_pingable = 1;
	imp->imp_force_reconnect = 0;
	imp->imp_force_verify = 0;

	imp->imp_connect_data = *ocd;

	CDEBUG(D_HA, "%s: connect to target with instance %u\n",
	       imp->imp_obd->obd_name, ocd->ocd_instance);
	exp = class_conn2export(&imp->imp_dlm_handle);

	spin_unlock(&imp->imp_lock);

	/* check that server granted subset of flags we asked for. */
	if ((ocd->ocd_connect_flags & imp->imp_connect_flags_orig) !=
	    ocd->ocd_connect_flags) {
		CERROR("%s: Server didn't granted asked subset of flags: asked=%#llx grranted=%#llx\n",
		       imp->imp_obd->obd_name, imp->imp_connect_flags_orig,
		       ocd->ocd_connect_flags);
		rc = -EPROTO;
		goto out;
	}

	if (!exp) {
		/* This could happen if export is cleaned during the
		   connect attempt */
		CERROR("%s: missing export after connect\n",
		       imp->imp_obd->obd_name);
		rc = -ENODEV;
		goto out;
	}
	old_connect_flags = exp_connect_flags(exp);
	exp->exp_connect_data = *ocd;
	imp->imp_obd->obd_self_export->exp_connect_data = *ocd;
	class_export_put(exp);

	obd_import_event(imp->imp_obd, imp, IMP_EVENT_OCD);

	if (aa->pcaa_initial_connect) {
		spin_lock(&imp->imp_lock);
		if (msg_flags & MSG_CONNECT_REPLAYABLE) {
			imp->imp_replayable = 1;
			spin_unlock(&imp->imp_lock);
			CDEBUG(D_HA, "connected to replayable target: %s\n",
			       obd2cli_tgt(imp->imp_obd));
		} else {
			imp->imp_replayable = 0;
			spin_unlock(&imp->imp_lock);
		}

		/* if applies, adjust the imp->imp_msg_magic here
		 * according to reply flags */

		imp->imp_remote_handle =
				*lustre_msg_get_handle(request->rq_repmsg);

		/* Initial connects are allowed for clients with non-random
		 * uuids when servers are in recovery.  Simply signal the
		 * servers replay is complete and wait in REPLAY_WAIT. */
		if (msg_flags & MSG_CONNECT_RECOVERING) {
			CDEBUG(D_HA, "connect to %s during recovery\n",
			       obd2cli_tgt(imp->imp_obd));
			IMPORT_SET_STATE(imp, LUSTRE_IMP_REPLAY_LOCKS);
		} else {
			IMPORT_SET_STATE(imp, LUSTRE_IMP_FULL);
			ptlrpc_activate_import(imp);
		}

		rc = 0;
		goto finish;
	}

	/* Determine what recovery state to move the import to. */
	if (MSG_CONNECT_RECONNECT & msg_flags) {
		memset(&old_hdl, 0, sizeof(old_hdl));
		if (!memcmp(&old_hdl, lustre_msg_get_handle(request->rq_repmsg),
			    sizeof(old_hdl))) {
			LCONSOLE_WARN("Reconnect to %s (at @%s) failed due bad handle %#llx\n",
				      obd2cli_tgt(imp->imp_obd),
				      imp->imp_connection->c_remote_uuid.uuid,
				      imp->imp_dlm_handle.cookie);
			rc = -ENOTCONN;
			goto out;
		}

		if (memcmp(&imp->imp_remote_handle,
			   lustre_msg_get_handle(request->rq_repmsg),
			   sizeof(imp->imp_remote_handle))) {
			int level = msg_flags & MSG_CONNECT_RECOVERING ?
				D_HA : D_WARNING;

			/* Bug 16611/14775: if server handle have changed,
			 * that means some sort of disconnection happened.
			 * If the server is not in recovery, that also means it
			 * already erased all of our state because of previous
			 * eviction. If it is in recovery - we are safe to
			 * participate since we can reestablish all of our state
			 * with server again */
			if ((MSG_CONNECT_RECOVERING & msg_flags)) {
				CDEBUG(level, "%s@%s changed server handle from %#llx to %#llx but is still in recovery\n",
				       obd2cli_tgt(imp->imp_obd),
				       imp->imp_connection->c_remote_uuid.uuid,
				       imp->imp_remote_handle.cookie,
				       lustre_msg_get_handle(
				       request->rq_repmsg)->cookie);
			} else {
				LCONSOLE_WARN("Evicted from %s (at %s) after server handle changed from %#llx to %#llx\n",
					      obd2cli_tgt(imp->imp_obd),
					      imp->imp_connection-> \
					      c_remote_uuid.uuid,
					      imp->imp_remote_handle.cookie,
					      lustre_msg_get_handle(
						      request->rq_repmsg)->cookie);
			}


			imp->imp_remote_handle =
				     *lustre_msg_get_handle(request->rq_repmsg);

			if (!(MSG_CONNECT_RECOVERING & msg_flags)) {
				IMPORT_SET_STATE(imp, LUSTRE_IMP_EVICTED);
				rc = 0;
				goto finish;
			}

		} else {
			CDEBUG(D_HA, "reconnected to %s@%s after partition\n",
			       obd2cli_tgt(imp->imp_obd),
			       imp->imp_connection->c_remote_uuid.uuid);
		}

		if (imp->imp_invalid) {
			CDEBUG(D_HA, "%s: reconnected but import is invalid; marking evicted\n",
			       imp->imp_obd->obd_name);
			IMPORT_SET_STATE(imp, LUSTRE_IMP_EVICTED);
		} else if (MSG_CONNECT_RECOVERING & msg_flags) {
			CDEBUG(D_HA, "%s: reconnected to %s during replay\n",
			       imp->imp_obd->obd_name,
			       obd2cli_tgt(imp->imp_obd));

			spin_lock(&imp->imp_lock);
			imp->imp_resend_replay = 1;
			spin_unlock(&imp->imp_lock);

			IMPORT_SET_STATE(imp, imp->imp_replay_state);
		} else {
			IMPORT_SET_STATE(imp, LUSTRE_IMP_RECOVER);
		}
	} else if ((MSG_CONNECT_RECOVERING & msg_flags) && !imp->imp_invalid) {
		LASSERT(imp->imp_replayable);
		imp->imp_remote_handle =
				*lustre_msg_get_handle(request->rq_repmsg);
		imp->imp_last_replay_transno = 0;
		IMPORT_SET_STATE(imp, LUSTRE_IMP_REPLAY);
	} else {
		DEBUG_REQ(D_HA, request, "%s: evicting (reconnect/recover flags not set: %x)",
			  imp->imp_obd->obd_name, msg_flags);
		imp->imp_remote_handle =
				*lustre_msg_get_handle(request->rq_repmsg);
		IMPORT_SET_STATE(imp, LUSTRE_IMP_EVICTED);
	}

	/* Sanity checks for a reconnected import. */
	if (!(imp->imp_replayable) != !(msg_flags & MSG_CONNECT_REPLAYABLE)) {
		CERROR("imp_replayable flag does not match server after reconnect. We should LBUG right here.\n");
	}

	if (lustre_msg_get_last_committed(request->rq_repmsg) > 0 &&
	    lustre_msg_get_last_committed(request->rq_repmsg) <
	    aa->pcaa_peer_committed) {
		CERROR("%s went back in time (transno %lld was previously committed, server now claims %lld)!  See https://bugzilla.lustre.org/show_bug.cgi?id=9646\n",
		       obd2cli_tgt(imp->imp_obd), aa->pcaa_peer_committed,
		       lustre_msg_get_last_committed(request->rq_repmsg));
	}

finish:
	rc = ptlrpc_import_recovery_state_machine(imp);
	if (rc != 0) {
		if (rc == -ENOTCONN) {
			CDEBUG(D_HA, "evicted/aborted by %s@%s during recovery; invalidating and reconnecting\n",
			       obd2cli_tgt(imp->imp_obd),
			       imp->imp_connection->c_remote_uuid.uuid);
			ptlrpc_connect_import(imp);
			imp->imp_connect_tried = 1;
			return 0;
		}
	} else {

		spin_lock(&imp->imp_lock);
		list_del(&imp->imp_conn_current->oic_item);
		list_add(&imp->imp_conn_current->oic_item,
			     &imp->imp_conn_list);
		imp->imp_last_success_conn =
			imp->imp_conn_current->oic_last_attempt;

		spin_unlock(&imp->imp_lock);

		if ((imp->imp_connect_flags_orig & OBD_CONNECT_IBITS) &&
		    !(ocd->ocd_connect_flags & OBD_CONNECT_IBITS)) {
			LCONSOLE_WARN("%s: MDS %s does not support ibits lock, either very old or invalid: requested %llx, replied %llx\n",
				      imp->imp_obd->obd_name,
				      imp->imp_connection->c_remote_uuid.uuid,
				      imp->imp_connect_flags_orig,
				      ocd->ocd_connect_flags);
			rc = -EPROTO;
			goto out;
		}

		if ((ocd->ocd_connect_flags & OBD_CONNECT_VERSION) &&
		    (ocd->ocd_version > LUSTRE_VERSION_CODE +
					LUSTRE_VERSION_OFFSET_WARN ||
		     ocd->ocd_version < LUSTRE_VERSION_CODE -
					LUSTRE_VERSION_OFFSET_WARN)) {
			/* Sigh, some compilers do not like #ifdef in the middle
			   of macro arguments */
			const char *older = "older. Consider upgrading server or downgrading client"
				;
			const char *newer = "newer than client version. Consider upgrading client"
					    ;

			LCONSOLE_WARN("Server %s version (%d.%d.%d.%d) is much %s (%s)\n",
				      obd2cli_tgt(imp->imp_obd),
				      OBD_OCD_VERSION_MAJOR(ocd->ocd_version),
				      OBD_OCD_VERSION_MINOR(ocd->ocd_version),
				      OBD_OCD_VERSION_PATCH(ocd->ocd_version),
				      OBD_OCD_VERSION_FIX(ocd->ocd_version),
				      ocd->ocd_version > LUSTRE_VERSION_CODE ?
				      newer : older, LUSTRE_VERSION_STRING);
		}

#if LUSTRE_VERSION_CODE < OBD_OCD_VERSION(3, 2, 50, 0)
		/* Check if server has LU-1252 fix applied to not always swab
		 * the IR MNE entries. Do this only once per connection.  This
		 * fixup is version-limited, because we don't want to carry the
		 * OBD_CONNECT_MNE_SWAB flag around forever, just so long as we
		 * need interop with unpatched 2.2 servers.  For newer servers,
		 * the client will do MNE swabbing only as needed.  LU-1644 */
		if (unlikely((ocd->ocd_connect_flags & OBD_CONNECT_VERSION) &&
			     !(ocd->ocd_connect_flags & OBD_CONNECT_MNE_SWAB) &&
			     OBD_OCD_VERSION_MAJOR(ocd->ocd_version) == 2 &&
			     OBD_OCD_VERSION_MINOR(ocd->ocd_version) == 2 &&
			     OBD_OCD_VERSION_PATCH(ocd->ocd_version) < 55 &&
			     strcmp(imp->imp_obd->obd_type->typ_name,
				    LUSTRE_MGC_NAME) == 0))
			imp->imp_need_mne_swab = 1;
		else /* clear if server was upgraded since last connect */
			imp->imp_need_mne_swab = 0;
#else
#warning "LU-1644: Remove old OBD_CONNECT_MNE_SWAB fixup and imp_need_mne_swab"
#endif

		if (ocd->ocd_connect_flags & OBD_CONNECT_CKSUM) {
			/* We sent to the server ocd_cksum_types with bits set
			 * for algorithms we understand. The server masked off
			 * the checksum types it doesn't support */
			if ((ocd->ocd_cksum_types &
			     cksum_types_supported_client()) == 0) {
				LCONSOLE_WARN("The negotiation of the checksum algorithm to use with server %s failed (%x/%x), disabling checksums\n",
					      obd2cli_tgt(imp->imp_obd),
					      ocd->ocd_cksum_types,
					      cksum_types_supported_client());
				cli->cl_checksum = 0;
				cli->cl_supp_cksum_types = OBD_CKSUM_ADLER;
			} else {
				cli->cl_supp_cksum_types = ocd->ocd_cksum_types;
			}
		} else {
			/* The server does not support OBD_CONNECT_CKSUM.
			 * Enforce ADLER for backward compatibility*/
			cli->cl_supp_cksum_types = OBD_CKSUM_ADLER;
		}
		cli->cl_cksum_type = cksum_type_select(cli->cl_supp_cksum_types);

		if (ocd->ocd_connect_flags & OBD_CONNECT_BRW_SIZE)
			cli->cl_max_pages_per_rpc =
				min(ocd->ocd_brw_size >> PAGE_CACHE_SHIFT,
				    cli->cl_max_pages_per_rpc);
		else if (imp->imp_connect_op == MDS_CONNECT ||
			 imp->imp_connect_op == MGS_CONNECT)
			cli->cl_max_pages_per_rpc = 1;

		/* Reset ns_connect_flags only for initial connect. It might be
		 * changed in while using FS and if we reset it in reconnect
		 * this leads to losing user settings done before such as
		 * disable lru_resize, etc. */
		if (old_connect_flags != exp_connect_flags(exp) ||
		    aa->pcaa_initial_connect) {
			CDEBUG(D_HA, "%s: Resetting ns_connect_flags to server flags: %#llx\n",
			       imp->imp_obd->obd_name, ocd->ocd_connect_flags);
			imp->imp_obd->obd_namespace->ns_connect_flags =
				ocd->ocd_connect_flags;
			imp->imp_obd->obd_namespace->ns_orig_connect_flags =
				ocd->ocd_connect_flags;
		}

		if ((ocd->ocd_connect_flags & OBD_CONNECT_AT) &&
		    (imp->imp_msg_magic == LUSTRE_MSG_MAGIC_V2))
			/* We need a per-message support flag, because
			   a. we don't know if the incoming connect reply
			      supports AT or not (in reply_in_callback)
			      until we unpack it.
			   b. failovered server means export and flags are gone
			      (in ptlrpc_send_reply).
			   Can only be set when we know AT is supported at
			   both ends */
			imp->imp_msghdr_flags |= MSGHDR_AT_SUPPORT;
		else
			imp->imp_msghdr_flags &= ~MSGHDR_AT_SUPPORT;

		if ((ocd->ocd_connect_flags & OBD_CONNECT_FULL20) &&
		    (imp->imp_msg_magic == LUSTRE_MSG_MAGIC_V2))
			imp->imp_msghdr_flags |= MSGHDR_CKSUM_INCOMPAT18;
		else
			imp->imp_msghdr_flags &= ~MSGHDR_CKSUM_INCOMPAT18;

		LASSERT((cli->cl_max_pages_per_rpc <= PTLRPC_MAX_BRW_PAGES) &&
			(cli->cl_max_pages_per_rpc > 0));
	}

out:
	imp->imp_connect_tried = 1;

	if (rc != 0) {
		IMPORT_SET_STATE(imp, LUSTRE_IMP_DISCON);
		if (rc == -EACCES) {
			/*
			 * Give up trying to reconnect
			 * EACCES means client has no permission for connection
			 */
			imp->imp_obd->obd_no_recov = 1;
			ptlrpc_deactivate_import(imp);
		}

		if (rc == -EPROTO) {
			struct obd_connect_data *ocd;

			/* reply message might not be ready */
			if (request->rq_repmsg == NULL)
				return -EPROTO;

			ocd = req_capsule_server_get(&request->rq_pill,
						     &RMF_CONNECT_DATA);
			if (ocd &&
			    (ocd->ocd_connect_flags & OBD_CONNECT_VERSION) &&
			    (ocd->ocd_version != LUSTRE_VERSION_CODE)) {
				/*
				 * Actually servers are only supposed to refuse
				 * connection from liblustre clients, so we
				 * should never see this from VFS context
				 */
				LCONSOLE_ERROR_MSG(0x16a, "Server %s version (%d.%d.%d.%d) refused connection from this client with an incompatible version (%s).  Client must be recompiled\n",
						   obd2cli_tgt(imp->imp_obd),
						   OBD_OCD_VERSION_MAJOR(ocd->ocd_version),
						   OBD_OCD_VERSION_MINOR(ocd->ocd_version),
						   OBD_OCD_VERSION_PATCH(ocd->ocd_version),
						   OBD_OCD_VERSION_FIX(ocd->ocd_version),
						   LUSTRE_VERSION_STRING);
				ptlrpc_deactivate_import(imp);
				IMPORT_SET_STATE(imp, LUSTRE_IMP_CLOSED);
			}
			return -EPROTO;
		}

		ptlrpc_maybe_ping_import_soon(imp);

		CDEBUG(D_HA, "recovery of %s on %s failed (%d)\n",
		       obd2cli_tgt(imp->imp_obd),
		       (char *)imp->imp_connection->c_remote_uuid.uuid, rc);
	}

	wake_up_all(&imp->imp_recovery_waitq);
	return rc;
}

/**
 * interpret callback for "completed replay" RPCs.
 * \see signal_completed_replay
 */
static int completed_replay_interpret(const struct lu_env *env,
				      struct ptlrpc_request *req,
				      void *data, int rc)
{
	atomic_dec(&req->rq_import->imp_replay_inflight);
	if (req->rq_status == 0 &&
	    !req->rq_import->imp_vbr_failed) {
		ptlrpc_import_recovery_state_machine(req->rq_import);
	} else {
		if (req->rq_import->imp_vbr_failed) {
			CDEBUG(D_WARNING,
			       "%s: version recovery fails, reconnecting\n",
			       req->rq_import->imp_obd->obd_name);
		} else {
			CDEBUG(D_HA, "%s: LAST_REPLAY message error: %d, reconnecting\n",
			       req->rq_import->imp_obd->obd_name,
			       req->rq_status);
		}
		ptlrpc_connect_import(req->rq_import);
	}

	return 0;
}

/**
 * Let server know that we have no requests to replay anymore.
 * Achieved by just sending a PING request
 */
static int signal_completed_replay(struct obd_import *imp)
{
	struct ptlrpc_request *req;

	if (unlikely(OBD_FAIL_CHECK(OBD_FAIL_PTLRPC_FINISH_REPLAY)))
		return 0;

	LASSERT(atomic_read(&imp->imp_replay_inflight) == 0);
	atomic_inc(&imp->imp_replay_inflight);

	req = ptlrpc_request_alloc_pack(imp, &RQF_OBD_PING, LUSTRE_OBD_VERSION,
					OBD_PING);
	if (req == NULL) {
		atomic_dec(&imp->imp_replay_inflight);
		return -ENOMEM;
	}

	ptlrpc_request_set_replen(req);
	req->rq_send_state = LUSTRE_IMP_REPLAY_WAIT;
	lustre_msg_add_flags(req->rq_reqmsg,
			     MSG_LOCK_REPLAY_DONE | MSG_REQ_REPLAY_DONE);
	if (AT_OFF)
		req->rq_timeout *= 3;
	req->rq_interpret_reply = completed_replay_interpret;

	ptlrpcd_add_req(req, PDL_POLICY_ROUND, -1);
	return 0;
}

/**
 * In kernel code all import invalidation happens in its own
 * separate thread, so that whatever application happened to encounter
 * a problem could still be killed or otherwise continue
 */
static int ptlrpc_invalidate_import_thread(void *data)
{
	struct obd_import *imp = data;

	unshare_fs_struct();

	CDEBUG(D_HA, "thread invalidate import %s to %s@%s\n",
	       imp->imp_obd->obd_name, obd2cli_tgt(imp->imp_obd),
	       imp->imp_connection->c_remote_uuid.uuid);

	ptlrpc_invalidate_import(imp);

	if (obd_dump_on_eviction) {
		CERROR("dump the log upon eviction\n");
		libcfs_debug_dumplog();
	}

	IMPORT_SET_STATE(imp, LUSTRE_IMP_RECOVER);
	ptlrpc_import_recovery_state_machine(imp);

	class_import_put(imp);
	return 0;
}

/**
 * This is the state machine for client-side recovery on import.
 *
 * Typically we have two possibly paths. If we came to server and it is not
 * in recovery, we just enter IMP_EVICTED state, invalidate our import
 * state and reconnect from scratch.
 * If we came to server that is in recovery, we enter IMP_REPLAY import state.
 * We go through our list of requests to replay and send them to server one by
 * one.
 * After sending all request from the list we change import state to
 * IMP_REPLAY_LOCKS and re-request all the locks we believe we have from server
 * and also all the locks we don't yet have and wait for server to grant us.
 * After that we send a special "replay completed" request and change import
 * state to IMP_REPLAY_WAIT.
 * Upon receiving reply to that "replay completed" RPC we enter IMP_RECOVER
 * state and resend all requests from sending list.
 * After that we promote import to FULL state and send all delayed requests
 * and import is fully operational after that.
 *
 */
int ptlrpc_import_recovery_state_machine(struct obd_import *imp)
{
	int rc = 0;
	int inflight;
	char *target_start;
	int target_len;

	if (imp->imp_state == LUSTRE_IMP_EVICTED) {
		deuuidify(obd2cli_tgt(imp->imp_obd), NULL,
			  &target_start, &target_len);
		/* Don't care about MGC eviction */
		if (strcmp(imp->imp_obd->obd_type->typ_name,
			   LUSTRE_MGC_NAME) != 0) {
			LCONSOLE_ERROR_MSG(0x167, "%s: This client was evicted by %.*s; in progress operations using this service will fail.\n",
					   imp->imp_obd->obd_name, target_len,
					   target_start);
		}
		CDEBUG(D_HA, "evicted from %s@%s; invalidating\n",
		       obd2cli_tgt(imp->imp_obd),
		       imp->imp_connection->c_remote_uuid.uuid);
		/* reset vbr_failed flag upon eviction */
		spin_lock(&imp->imp_lock);
		imp->imp_vbr_failed = 0;
		spin_unlock(&imp->imp_lock);

		{
		struct task_struct *task;
		/* bug 17802:  XXX client_disconnect_export vs connect request
		 * race. if client will evicted at this time, we start
		 * invalidate thread without reference to import and import can
		 * be freed at same time. */
		class_import_get(imp);
		task = kthread_run(ptlrpc_invalidate_import_thread, imp,
				     "ll_imp_inval");
		if (IS_ERR(task)) {
			class_import_put(imp);
			CERROR("error starting invalidate thread: %d\n", rc);
			rc = PTR_ERR(task);
		} else {
			rc = 0;
		}
		return rc;
		}
	}

	if (imp->imp_state == LUSTRE_IMP_REPLAY) {
		CDEBUG(D_HA, "replay requested by %s\n",
		       obd2cli_tgt(imp->imp_obd));
		rc = ptlrpc_replay_next(imp, &inflight);
		if (inflight == 0 &&
		    atomic_read(&imp->imp_replay_inflight) == 0) {
			IMPORT_SET_STATE(imp, LUSTRE_IMP_REPLAY_LOCKS);
			rc = ldlm_replay_locks(imp);
			if (rc)
				goto out;
		}
		rc = 0;
	}

	if (imp->imp_state == LUSTRE_IMP_REPLAY_LOCKS) {
		if (atomic_read(&imp->imp_replay_inflight) == 0) {
			IMPORT_SET_STATE(imp, LUSTRE_IMP_REPLAY_WAIT);
			rc = signal_completed_replay(imp);
			if (rc)
				goto out;
		}

	}

	if (imp->imp_state == LUSTRE_IMP_REPLAY_WAIT) {
		if (atomic_read(&imp->imp_replay_inflight) == 0) {
			IMPORT_SET_STATE(imp, LUSTRE_IMP_RECOVER);
		}
	}

	if (imp->imp_state == LUSTRE_IMP_RECOVER) {
		CDEBUG(D_HA, "reconnected to %s@%s\n",
		       obd2cli_tgt(imp->imp_obd),
		       imp->imp_connection->c_remote_uuid.uuid);

		rc = ptlrpc_resend(imp);
		if (rc)
			goto out;
		IMPORT_SET_STATE(imp, LUSTRE_IMP_FULL);
		ptlrpc_activate_import(imp);

		deuuidify(obd2cli_tgt(imp->imp_obd), NULL,
			  &target_start, &target_len);
		LCONSOLE_INFO("%s: Connection restored to %.*s (at %s)\n",
			      imp->imp_obd->obd_name,
			      target_len, target_start,
			      libcfs_nid2str(imp->imp_connection->c_peer.nid));
	}

	if (imp->imp_state == LUSTRE_IMP_FULL) {
		wake_up_all(&imp->imp_recovery_waitq);
		ptlrpc_wake_delayed(imp);
	}

out:
	return rc;
}

int ptlrpc_disconnect_import(struct obd_import *imp, int noclose)
{
	struct ptlrpc_request *req;
	int rq_opc, rc = 0;

	if (imp->imp_obd->obd_force)
		goto set_state;

	switch (imp->imp_connect_op) {
	case OST_CONNECT:
		rq_opc = OST_DISCONNECT;
		break;
	case MDS_CONNECT:
		rq_opc = MDS_DISCONNECT;
		break;
	case MGS_CONNECT:
		rq_opc = MGS_DISCONNECT;
		break;
	default:
		rc = -EINVAL;
		CERROR("%s: don't know how to disconnect from %s (connect_op %d): rc = %d\n",
		       imp->imp_obd->obd_name, obd2cli_tgt(imp->imp_obd),
		       imp->imp_connect_op, rc);
		return rc;
	}

	if (ptlrpc_import_in_recovery(imp)) {
		struct l_wait_info lwi;
		long timeout;

		if (AT_OFF) {
			if (imp->imp_server_timeout)
				timeout = cfs_time_seconds(obd_timeout / 2);
			else
				timeout = cfs_time_seconds(obd_timeout);
		} else {
			int idx = import_at_get_index(imp,
				imp->imp_client->cli_request_portal);
			timeout = cfs_time_seconds(
				at_get(&imp->imp_at.iat_service_estimate[idx]));
		}

		lwi = LWI_TIMEOUT_INTR(cfs_timeout_cap(timeout),
				       back_to_sleep, LWI_ON_SIGNAL_NOOP, NULL);
		rc = l_wait_event(imp->imp_recovery_waitq,
				  !ptlrpc_import_in_recovery(imp), &lwi);

	}

	spin_lock(&imp->imp_lock);
	if (imp->imp_state != LUSTRE_IMP_FULL)
		goto out;
	spin_unlock(&imp->imp_lock);

	req = ptlrpc_request_alloc_pack(imp, &RQF_MDS_DISCONNECT,
					LUSTRE_OBD_VERSION, rq_opc);
	if (req) {
		/* We are disconnecting, do not retry a failed DISCONNECT rpc if
		 * it fails.  We can get through the above with a down server
		 * if the client doesn't know the server is gone yet. */
		req->rq_no_resend = 1;

		/* We want client umounts to happen quickly, no matter the
		   server state... */
		req->rq_timeout = min_t(int, req->rq_timeout,
					INITIAL_CONNECT_TIMEOUT);

		IMPORT_SET_STATE(imp, LUSTRE_IMP_CONNECTING);
		req->rq_send_state =  LUSTRE_IMP_CONNECTING;
		ptlrpc_request_set_replen(req);
		rc = ptlrpc_queue_wait(req);
		ptlrpc_req_finished(req);
	}

set_state:
	spin_lock(&imp->imp_lock);
out:
	if (noclose)
		IMPORT_SET_STATE_NOLOCK(imp, LUSTRE_IMP_DISCON);
	else
		IMPORT_SET_STATE_NOLOCK(imp, LUSTRE_IMP_CLOSED);
	memset(&imp->imp_remote_handle, 0, sizeof(imp->imp_remote_handle));
	spin_unlock(&imp->imp_lock);

	if (rc == -ETIMEDOUT || rc == -ENOTCONN || rc == -ESHUTDOWN)
		rc = 0;

	return rc;
}
EXPORT_SYMBOL(ptlrpc_disconnect_import);

void ptlrpc_cleanup_imp(struct obd_import *imp)
{
	spin_lock(&imp->imp_lock);
	IMPORT_SET_STATE_NOLOCK(imp, LUSTRE_IMP_CLOSED);
	imp->imp_generation++;
	spin_unlock(&imp->imp_lock);
	ptlrpc_abort_inflight(imp);
}
EXPORT_SYMBOL(ptlrpc_cleanup_imp);

/* Adaptive Timeout utils */
extern unsigned int at_min, at_max, at_history;

/* Bin into timeslices using AT_BINS bins.
   This gives us a max of the last binlimit*AT_BINS secs without the storage,
   but still smoothing out a return to normalcy from a slow response.
   (E.g. remember the maximum latency in each minute of the last 4 minutes.) */
int at_measured(struct adaptive_timeout *at, unsigned int val)
{
	unsigned int old = at->at_current;
	time_t now = get_seconds();
	time_t binlimit = max_t(time_t, at_history / AT_BINS, 1);

	LASSERT(at);
	CDEBUG(D_OTHER, "add %u to %p time=%lu v=%u (%u %u %u %u)\n",
	       val, at, now - at->at_binstart, at->at_current,
	       at->at_hist[0], at->at_hist[1], at->at_hist[2], at->at_hist[3]);

	if (val == 0)
		/* 0's don't count, because we never want our timeout to
		   drop to 0, and because 0 could mean an error */
		return 0;

	spin_lock(&at->at_lock);

	if (unlikely(at->at_binstart == 0)) {
		/* Special case to remove default from history */
		at->at_current = val;
		at->at_worst_ever = val;
		at->at_worst_time = now;
		at->at_hist[0] = val;
		at->at_binstart = now;
	} else if (now - at->at_binstart < binlimit) {
		/* in bin 0 */
		at->at_hist[0] = max(val, at->at_hist[0]);
		at->at_current = max(val, at->at_current);
	} else {
		int i, shift;
		unsigned int maxv = val;
		/* move bins over */
		shift = (now - at->at_binstart) / binlimit;
		LASSERT(shift > 0);
		for (i = AT_BINS - 1; i >= 0; i--) {
			if (i >= shift) {
				at->at_hist[i] = at->at_hist[i - shift];
				maxv = max(maxv, at->at_hist[i]);
			} else {
				at->at_hist[i] = 0;
			}
		}
		at->at_hist[0] = val;
		at->at_current = maxv;
		at->at_binstart += shift * binlimit;
	}

	if (at->at_current > at->at_worst_ever) {
		at->at_worst_ever = at->at_current;
		at->at_worst_time = now;
	}

	if (at->at_flags & AT_FLG_NOHIST)
		/* Only keep last reported val; keeping the rest of the history
		   for proc only */
		at->at_current = val;

	if (at_max > 0)
		at->at_current =  min(at->at_current, at_max);
	at->at_current =  max(at->at_current, at_min);

	if (at->at_current != old)
		CDEBUG(D_OTHER, "AT %p change: old=%u new=%u delta=%d (val=%u) hist %u %u %u %u\n",
		       at,
		       old, at->at_current, at->at_current - old, val,
		       at->at_hist[0], at->at_hist[1], at->at_hist[2],
		       at->at_hist[3]);

	/* if we changed, report the old value */
	old = (at->at_current != old) ? old : 0;

	spin_unlock(&at->at_lock);
	return old;
}

/* Find the imp_at index for a given portal; assign if space available */
int import_at_get_index(struct obd_import *imp, int portal)
{
	struct imp_at *at = &imp->imp_at;
	int i;

	for (i = 0; i < IMP_AT_MAX_PORTALS; i++) {
		if (at->iat_portal[i] == portal)
			return i;
		if (at->iat_portal[i] == 0)
			/* unused */
			break;
	}

	/* Not found in list, add it under a lock */
	spin_lock(&imp->imp_lock);

	/* Check unused under lock */
	for (; i < IMP_AT_MAX_PORTALS; i++) {
		if (at->iat_portal[i] == portal)
			goto out;
		if (at->iat_portal[i] == 0)
			/* unused */
			break;
	}

	/* Not enough portals? */
	LASSERT(i < IMP_AT_MAX_PORTALS);

	at->iat_portal[i] = portal;
out:
	spin_unlock(&imp->imp_lock);
	return i;
}

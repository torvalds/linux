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
 *
 * lustre/ptlrpc/recover.c
 *
 * Author: Mike Shaver <shaver@clusterfs.com>
 */

#define DEBUG_SUBSYSTEM S_RPC
#include "../../include/linux/libcfs/libcfs.h"

#include "../include/obd_support.h"
#include "../include/lustre_ha.h"
#include "../include/lustre_net.h"
#include "../include/lustre_import.h"
#include "../include/lustre_export.h"
#include "../include/obd.h"
#include "../include/obd_class.h"
#include <linux/list.h>

#include "ptlrpc_internal.h"

/**
 * Start recovery on disconnected import.
 * This is done by just attempting a connect
 */
void ptlrpc_initiate_recovery(struct obd_import *imp)
{
	CDEBUG(D_HA, "%s: starting recovery\n", obd2cli_tgt(imp->imp_obd));
	ptlrpc_connect_import(imp);
}

/**
 * Identify what request from replay list needs to be replayed next
 * (based on what we have already replayed) and send it to server.
 */
int ptlrpc_replay_next(struct obd_import *imp, int *inflight)
{
	int rc = 0;
	struct list_head *tmp, *pos;
	struct ptlrpc_request *req = NULL;
	__u64 last_transno;

	*inflight = 0;

	/* It might have committed some after we last spoke, so make sure we
	 * get rid of them now.
	 */
	spin_lock(&imp->imp_lock);
	imp->imp_last_transno_checked = 0;
	ptlrpc_free_committed(imp);
	last_transno = imp->imp_last_replay_transno;
	spin_unlock(&imp->imp_lock);

	CDEBUG(D_HA, "import %p from %s committed %llu last %llu\n",
	       imp, obd2cli_tgt(imp->imp_obd),
	       imp->imp_peer_committed_transno, last_transno);

	/* Do I need to hold a lock across this iteration?  We shouldn't be
	 * racing with any additions to the list, because we're in recovery
	 * and are therefore not processing additional requests to add.  Calls
	 * to ptlrpc_free_committed might commit requests, but nothing "newer"
	 * than the one we're replaying (it can't be committed until it's
	 * replayed, and we're doing that here).  l_f_e_safe protects against
	 * problems with the current request being committed, in the unlikely
	 * event of that race.  So, in conclusion, I think that it's safe to
	 * perform this list-walk without the imp_lock held.
	 *
	 * But, the {mdc,osc}_replay_open callbacks both iterate
	 * request lists, and have comments saying they assume the
	 * imp_lock is being held by ptlrpc_replay, but it's not. it's
	 * just a little race...
	 */

	/* Replay all the committed open requests on committed_list first */
	if (!list_empty(&imp->imp_committed_list)) {
		tmp = imp->imp_committed_list.prev;
		req = list_entry(tmp, struct ptlrpc_request, rq_replay_list);

		/* The last request on committed_list hasn't been replayed */
		if (req->rq_transno > last_transno) {
			/* Since the imp_committed_list is immutable before
			 * all of it's requests being replayed, it's safe to
			 * use a cursor to accelerate the search
			 */
			imp->imp_replay_cursor = imp->imp_replay_cursor->next;

			while (imp->imp_replay_cursor !=
			       &imp->imp_committed_list) {
				req = list_entry(imp->imp_replay_cursor,
						 struct ptlrpc_request,
						 rq_replay_list);
				if (req->rq_transno > last_transno)
					break;

				req = NULL;
				imp->imp_replay_cursor =
					imp->imp_replay_cursor->next;
			}
		} else {
			/* All requests on committed_list have been replayed */
			imp->imp_replay_cursor = &imp->imp_committed_list;
			req = NULL;
		}
	}

	/* All the requests in committed list have been replayed, let's replay
	 * the imp_replay_list
	 */
	if (!req) {
		list_for_each_safe(tmp, pos, &imp->imp_replay_list) {
			req = list_entry(tmp, struct ptlrpc_request,
					 rq_replay_list);

			if (req->rq_transno > last_transno)
				break;
			req = NULL;
		}
	}

	/* If need to resend the last sent transno (because a reconnect
	 * has occurred), then stop on the matching req and send it again.
	 * If, however, the last sent transno has been committed then we
	 * continue replay from the next request.
	 */
	if (req && imp->imp_resend_replay)
		lustre_msg_add_flags(req->rq_reqmsg, MSG_RESENT);

	spin_lock(&imp->imp_lock);
	imp->imp_resend_replay = 0;
	spin_unlock(&imp->imp_lock);

	if (req) {
		rc = ptlrpc_replay_req(req);
		if (rc) {
			CERROR("recovery replay error %d for req %llu\n",
			       rc, req->rq_xid);
			return rc;
		}
		*inflight = 1;
	}
	return rc;
}

/**
 * Schedule resending of request on sending_list. This is done after
 * we completed replaying of requests and locks.
 */
int ptlrpc_resend(struct obd_import *imp)
{
	struct ptlrpc_request *req, *next;

	/* As long as we're in recovery, nothing should be added to the sending
	 * list, so we don't need to hold the lock during this iteration and
	 * resend process.
	 */
	/* Well... what if lctl recover is called twice at the same time?
	 */
	spin_lock(&imp->imp_lock);
	if (imp->imp_state != LUSTRE_IMP_RECOVER) {
		spin_unlock(&imp->imp_lock);
		return -1;
	}

	list_for_each_entry_safe(req, next, &imp->imp_sending_list, rq_list) {
		LASSERTF((long)req > PAGE_CACHE_SIZE && req != LP_POISON,
			 "req %p bad\n", req);
		LASSERTF(req->rq_type != LI_POISON, "req %p freed\n", req);
		if (!ptlrpc_no_resend(req))
			ptlrpc_resend_req(req);
	}
	spin_unlock(&imp->imp_lock);

	return 0;
}
EXPORT_SYMBOL(ptlrpc_resend);

/**
 * Go through all requests in delayed list and wake their threads
 * for resending
 */
void ptlrpc_wake_delayed(struct obd_import *imp)
{
	struct list_head *tmp, *pos;
	struct ptlrpc_request *req;

	spin_lock(&imp->imp_lock);
	list_for_each_safe(tmp, pos, &imp->imp_delayed_list) {
		req = list_entry(tmp, struct ptlrpc_request, rq_list);

		DEBUG_REQ(D_HA, req, "waking (set %p):", req->rq_set);
		ptlrpc_client_wake_req(req);
	}
	spin_unlock(&imp->imp_lock);
}
EXPORT_SYMBOL(ptlrpc_wake_delayed);

void ptlrpc_request_handle_notconn(struct ptlrpc_request *failed_req)
{
	struct obd_import *imp = failed_req->rq_import;

	CDEBUG(D_HA, "import %s of %s@%s abruptly disconnected: reconnecting\n",
	       imp->imp_obd->obd_name, obd2cli_tgt(imp->imp_obd),
	       imp->imp_connection->c_remote_uuid.uuid);

	if (ptlrpc_set_import_discon(imp,
			      lustre_msg_get_conn_cnt(failed_req->rq_reqmsg))) {
		if (!imp->imp_replayable) {
			CDEBUG(D_HA, "import %s@%s for %s not replayable, auto-deactivating\n",
			       obd2cli_tgt(imp->imp_obd),
			       imp->imp_connection->c_remote_uuid.uuid,
			       imp->imp_obd->obd_name);
			ptlrpc_deactivate_import(imp);
		}
		/* to control recovery via lctl {disable|enable}_recovery */
		if (imp->imp_deactive == 0)
			ptlrpc_connect_import(imp);
	}

	/* Wait for recovery to complete and resend. If evicted, then
	 * this request will be errored out later.
	 */
	spin_lock(&failed_req->rq_lock);
	if (!failed_req->rq_no_resend)
		failed_req->rq_resend = 1;
	spin_unlock(&failed_req->rq_lock);
}

/**
 * Administratively active/deactive a client.
 * This should only be called by the ioctl interface, currently
 *  - the lctl deactivate and activate commands
 *  - echo 0/1 >> /sys/fs/lustre/osc/XXX/active
 *  - client umount -f (ll_umount_begin)
 */
int ptlrpc_set_import_active(struct obd_import *imp, int active)
{
	struct obd_device *obd = imp->imp_obd;
	int rc = 0;

	LASSERT(obd);

	/* When deactivating, mark import invalid, and abort in-flight
	 * requests.
	 */
	if (!active) {
		LCONSOLE_WARN("setting import %s INACTIVE by administrator request\n",
			      obd2cli_tgt(imp->imp_obd));

		/* set before invalidate to avoid messages about imp_inval
		 * set without imp_deactive in ptlrpc_import_delay_req
		 */
		spin_lock(&imp->imp_lock);
		imp->imp_deactive = 1;
		spin_unlock(&imp->imp_lock);

		obd_import_event(imp->imp_obd, imp, IMP_EVENT_DEACTIVATE);

		ptlrpc_invalidate_import(imp);
	}

	/* When activating, mark import valid, and attempt recovery */
	if (active) {
		CDEBUG(D_HA, "setting import %s VALID\n",
		       obd2cli_tgt(imp->imp_obd));

		spin_lock(&imp->imp_lock);
		imp->imp_deactive = 0;
		spin_unlock(&imp->imp_lock);
		obd_import_event(imp->imp_obd, imp, IMP_EVENT_ACTIVATE);

		rc = ptlrpc_recover_import(imp, NULL, 0);
	}

	return rc;
}
EXPORT_SYMBOL(ptlrpc_set_import_active);

/* Attempt to reconnect an import */
int ptlrpc_recover_import(struct obd_import *imp, char *new_uuid, int async)
{
	int rc = 0;

	spin_lock(&imp->imp_lock);
	if (imp->imp_state == LUSTRE_IMP_NEW || imp->imp_deactive ||
	    atomic_read(&imp->imp_inval_count))
		rc = -EINVAL;
	spin_unlock(&imp->imp_lock);
	if (rc)
		goto out;

	/* force import to be disconnected. */
	ptlrpc_set_import_discon(imp, 0);

	if (new_uuid) {
		struct obd_uuid uuid;

		/* intruct import to use new uuid */
		obd_str2uuid(&uuid, new_uuid);
		rc = import_set_conn_priority(imp, &uuid);
		if (rc)
			goto out;
	}

	/* Check if reconnect is already in progress */
	spin_lock(&imp->imp_lock);
	if (imp->imp_state != LUSTRE_IMP_DISCON) {
		imp->imp_force_verify = 1;
		rc = -EALREADY;
	}
	spin_unlock(&imp->imp_lock);
	if (rc)
		goto out;

	rc = ptlrpc_connect_import(imp);
	if (rc)
		goto out;

	if (!async) {
		struct l_wait_info lwi;
		int secs = cfs_time_seconds(obd_timeout);

		CDEBUG(D_HA, "%s: recovery started, waiting %u seconds\n",
		       obd2cli_tgt(imp->imp_obd), secs);

		lwi = LWI_TIMEOUT(secs, NULL, NULL);
		rc = l_wait_event(imp->imp_recovery_waitq,
				  !ptlrpc_import_in_recovery(imp), &lwi);
		CDEBUG(D_HA, "%s: recovery finished\n",
		       obd2cli_tgt(imp->imp_obd));
	}

out:
	return rc;
}
EXPORT_SYMBOL(ptlrpc_recover_import);

int ptlrpc_import_in_recovery(struct obd_import *imp)
{
	int in_recovery = 1;

	spin_lock(&imp->imp_lock);
	if (imp->imp_state == LUSTRE_IMP_FULL ||
	    imp->imp_state == LUSTRE_IMP_CLOSED ||
	    imp->imp_state == LUSTRE_IMP_DISCON ||
	    imp->imp_obd->obd_no_recov)
		in_recovery = 0;
	spin_unlock(&imp->imp_lock);

	return in_recovery;
}

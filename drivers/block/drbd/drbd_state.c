/*
   drbd_state.c

   This file is part of DRBD by Philipp Reisner and Lars Ellenberg.

   Copyright (C) 2001-2008, LINBIT Information Technologies GmbH.
   Copyright (C) 1999-2008, Philipp Reisner <philipp.reisner@linbit.com>.
   Copyright (C) 2002-2008, Lars Ellenberg <lars.ellenberg@linbit.com>.

   Thanks to Carter Burden, Bart Grantham and Gennadiy Nerubayev
   from Logicworks, Inc. for making SDP replication support possible.

   drbd is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   drbd is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with drbd; see the file COPYING.  If not, write to
   the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/drbd_limits.h>
#include "drbd_int.h"
#include "drbd_protocol.h"
#include "drbd_req.h"

struct after_state_chg_work {
	struct drbd_work w;
	struct drbd_device *device;
	union drbd_state os;
	union drbd_state ns;
	enum chg_state_flags flags;
	struct completion *done;
};

enum sanitize_state_warnings {
	NO_WARNING,
	ABORTED_ONLINE_VERIFY,
	ABORTED_RESYNC,
	CONNECTION_LOST_NEGOTIATING,
	IMPLICITLY_UPGRADED_DISK,
	IMPLICITLY_UPGRADED_PDSK,
};

static int w_after_state_ch(struct drbd_work *w, int unused);
static void after_state_ch(struct drbd_device *device, union drbd_state os,
			   union drbd_state ns, enum chg_state_flags flags);
static enum drbd_state_rv is_valid_state(struct drbd_device *, union drbd_state);
static enum drbd_state_rv is_valid_soft_transition(union drbd_state, union drbd_state, struct drbd_connection *);
static enum drbd_state_rv is_valid_transition(union drbd_state os, union drbd_state ns);
static union drbd_state sanitize_state(struct drbd_device *device, union drbd_state os,
				       union drbd_state ns, enum sanitize_state_warnings *warn);

static inline bool is_susp(union drbd_state s)
{
        return s.susp || s.susp_nod || s.susp_fen;
}

bool conn_all_vols_unconf(struct drbd_connection *connection)
{
	struct drbd_peer_device *peer_device;
	bool rv = true;
	int vnr;

	rcu_read_lock();
	idr_for_each_entry(&connection->peer_devices, peer_device, vnr) {
		struct drbd_device *device = peer_device->device;
		if (device->state.disk != D_DISKLESS ||
		    device->state.conn != C_STANDALONE ||
		    device->state.role != R_SECONDARY) {
			rv = false;
			break;
		}
	}
	rcu_read_unlock();

	return rv;
}

/* Unfortunately the states where not correctly ordered, when
   they where defined. therefore can not use max_t() here. */
static enum drbd_role max_role(enum drbd_role role1, enum drbd_role role2)
{
	if (role1 == R_PRIMARY || role2 == R_PRIMARY)
		return R_PRIMARY;
	if (role1 == R_SECONDARY || role2 == R_SECONDARY)
		return R_SECONDARY;
	return R_UNKNOWN;
}
static enum drbd_role min_role(enum drbd_role role1, enum drbd_role role2)
{
	if (role1 == R_UNKNOWN || role2 == R_UNKNOWN)
		return R_UNKNOWN;
	if (role1 == R_SECONDARY || role2 == R_SECONDARY)
		return R_SECONDARY;
	return R_PRIMARY;
}

enum drbd_role conn_highest_role(struct drbd_connection *connection)
{
	enum drbd_role role = R_UNKNOWN;
	struct drbd_peer_device *peer_device;
	int vnr;

	rcu_read_lock();
	idr_for_each_entry(&connection->peer_devices, peer_device, vnr) {
		struct drbd_device *device = peer_device->device;
		role = max_role(role, device->state.role);
	}
	rcu_read_unlock();

	return role;
}

enum drbd_role conn_highest_peer(struct drbd_connection *connection)
{
	enum drbd_role peer = R_UNKNOWN;
	struct drbd_peer_device *peer_device;
	int vnr;

	rcu_read_lock();
	idr_for_each_entry(&connection->peer_devices, peer_device, vnr) {
		struct drbd_device *device = peer_device->device;
		peer = max_role(peer, device->state.peer);
	}
	rcu_read_unlock();

	return peer;
}

enum drbd_disk_state conn_highest_disk(struct drbd_connection *connection)
{
	enum drbd_disk_state disk_state = D_DISKLESS;
	struct drbd_peer_device *peer_device;
	int vnr;

	rcu_read_lock();
	idr_for_each_entry(&connection->peer_devices, peer_device, vnr) {
		struct drbd_device *device = peer_device->device;
		disk_state = max_t(enum drbd_disk_state, disk_state, device->state.disk);
	}
	rcu_read_unlock();

	return disk_state;
}

enum drbd_disk_state conn_lowest_disk(struct drbd_connection *connection)
{
	enum drbd_disk_state disk_state = D_MASK;
	struct drbd_peer_device *peer_device;
	int vnr;

	rcu_read_lock();
	idr_for_each_entry(&connection->peer_devices, peer_device, vnr) {
		struct drbd_device *device = peer_device->device;
		disk_state = min_t(enum drbd_disk_state, disk_state, device->state.disk);
	}
	rcu_read_unlock();

	return disk_state;
}

enum drbd_disk_state conn_highest_pdsk(struct drbd_connection *connection)
{
	enum drbd_disk_state disk_state = D_DISKLESS;
	struct drbd_peer_device *peer_device;
	int vnr;

	rcu_read_lock();
	idr_for_each_entry(&connection->peer_devices, peer_device, vnr) {
		struct drbd_device *device = peer_device->device;
		disk_state = max_t(enum drbd_disk_state, disk_state, device->state.pdsk);
	}
	rcu_read_unlock();

	return disk_state;
}

enum drbd_conns conn_lowest_conn(struct drbd_connection *connection)
{
	enum drbd_conns conn = C_MASK;
	struct drbd_peer_device *peer_device;
	int vnr;

	rcu_read_lock();
	idr_for_each_entry(&connection->peer_devices, peer_device, vnr) {
		struct drbd_device *device = peer_device->device;
		conn = min_t(enum drbd_conns, conn, device->state.conn);
	}
	rcu_read_unlock();

	return conn;
}

static bool no_peer_wf_report_params(struct drbd_connection *connection)
{
	struct drbd_peer_device *peer_device;
	int vnr;
	bool rv = true;

	rcu_read_lock();
	idr_for_each_entry(&connection->peer_devices, peer_device, vnr)
		if (peer_device->device->state.conn == C_WF_REPORT_PARAMS) {
			rv = false;
			break;
		}
	rcu_read_unlock();

	return rv;
}

static void wake_up_all_devices(struct drbd_connection *connection)
{
	struct drbd_peer_device *peer_device;
	int vnr;

	rcu_read_lock();
	idr_for_each_entry(&connection->peer_devices, peer_device, vnr)
		wake_up(&peer_device->device->state_wait);
	rcu_read_unlock();

}


/**
 * cl_wide_st_chg() - true if the state change is a cluster wide one
 * @device:	DRBD device.
 * @os:		old (current) state.
 * @ns:		new (wanted) state.
 */
static int cl_wide_st_chg(struct drbd_device *device,
			  union drbd_state os, union drbd_state ns)
{
	return (os.conn >= C_CONNECTED && ns.conn >= C_CONNECTED &&
		 ((os.role != R_PRIMARY && ns.role == R_PRIMARY) ||
		  (os.conn != C_STARTING_SYNC_T && ns.conn == C_STARTING_SYNC_T) ||
		  (os.conn != C_STARTING_SYNC_S && ns.conn == C_STARTING_SYNC_S) ||
		  (os.disk != D_FAILED && ns.disk == D_FAILED))) ||
		(os.conn >= C_CONNECTED && ns.conn == C_DISCONNECTING) ||
		(os.conn == C_CONNECTED && ns.conn == C_VERIFY_S) ||
		(os.conn == C_CONNECTED && ns.conn == C_WF_REPORT_PARAMS);
}

static union drbd_state
apply_mask_val(union drbd_state os, union drbd_state mask, union drbd_state val)
{
	union drbd_state ns;
	ns.i = (os.i & ~mask.i) | val.i;
	return ns;
}

enum drbd_state_rv
drbd_change_state(struct drbd_device *device, enum chg_state_flags f,
		  union drbd_state mask, union drbd_state val)
{
	unsigned long flags;
	union drbd_state ns;
	enum drbd_state_rv rv;

	spin_lock_irqsave(&device->resource->req_lock, flags);
	ns = apply_mask_val(drbd_read_state(device), mask, val);
	rv = _drbd_set_state(device, ns, f, NULL);
	spin_unlock_irqrestore(&device->resource->req_lock, flags);

	return rv;
}

/**
 * drbd_force_state() - Impose a change which happens outside our control on our state
 * @device:	DRBD device.
 * @mask:	mask of state bits to change.
 * @val:	value of new state bits.
 */
void drbd_force_state(struct drbd_device *device,
	union drbd_state mask, union drbd_state val)
{
	drbd_change_state(device, CS_HARD, mask, val);
}

static enum drbd_state_rv
_req_st_cond(struct drbd_device *device, union drbd_state mask,
	     union drbd_state val)
{
	union drbd_state os, ns;
	unsigned long flags;
	enum drbd_state_rv rv;

	if (test_and_clear_bit(CL_ST_CHG_SUCCESS, &device->flags))
		return SS_CW_SUCCESS;

	if (test_and_clear_bit(CL_ST_CHG_FAIL, &device->flags))
		return SS_CW_FAILED_BY_PEER;

	spin_lock_irqsave(&device->resource->req_lock, flags);
	os = drbd_read_state(device);
	ns = sanitize_state(device, os, apply_mask_val(os, mask, val), NULL);
	rv = is_valid_transition(os, ns);
	if (rv >= SS_SUCCESS)
		rv = SS_UNKNOWN_ERROR;  /* cont waiting, otherwise fail. */

	if (!cl_wide_st_chg(device, os, ns))
		rv = SS_CW_NO_NEED;
	if (rv == SS_UNKNOWN_ERROR) {
		rv = is_valid_state(device, ns);
		if (rv >= SS_SUCCESS) {
			rv = is_valid_soft_transition(os, ns, first_peer_device(device)->connection);
			if (rv >= SS_SUCCESS)
				rv = SS_UNKNOWN_ERROR; /* cont waiting, otherwise fail. */
		}
	}
	spin_unlock_irqrestore(&device->resource->req_lock, flags);

	return rv;
}

/**
 * drbd_req_state() - Perform an eventually cluster wide state change
 * @device:	DRBD device.
 * @mask:	mask of state bits to change.
 * @val:	value of new state bits.
 * @f:		flags
 *
 * Should not be called directly, use drbd_request_state() or
 * _drbd_request_state().
 */
static enum drbd_state_rv
drbd_req_state(struct drbd_device *device, union drbd_state mask,
	       union drbd_state val, enum chg_state_flags f)
{
	struct completion done;
	unsigned long flags;
	union drbd_state os, ns;
	enum drbd_state_rv rv;

	init_completion(&done);

	if (f & CS_SERIALIZE)
		mutex_lock(device->state_mutex);

	spin_lock_irqsave(&device->resource->req_lock, flags);
	os = drbd_read_state(device);
	ns = sanitize_state(device, os, apply_mask_val(os, mask, val), NULL);
	rv = is_valid_transition(os, ns);
	if (rv < SS_SUCCESS) {
		spin_unlock_irqrestore(&device->resource->req_lock, flags);
		goto abort;
	}

	if (cl_wide_st_chg(device, os, ns)) {
		rv = is_valid_state(device, ns);
		if (rv == SS_SUCCESS)
			rv = is_valid_soft_transition(os, ns, first_peer_device(device)->connection);
		spin_unlock_irqrestore(&device->resource->req_lock, flags);

		if (rv < SS_SUCCESS) {
			if (f & CS_VERBOSE)
				print_st_err(device, os, ns, rv);
			goto abort;
		}

		if (drbd_send_state_req(first_peer_device(device), mask, val)) {
			rv = SS_CW_FAILED_BY_PEER;
			if (f & CS_VERBOSE)
				print_st_err(device, os, ns, rv);
			goto abort;
		}

		wait_event(device->state_wait,
			(rv = _req_st_cond(device, mask, val)));

		if (rv < SS_SUCCESS) {
			if (f & CS_VERBOSE)
				print_st_err(device, os, ns, rv);
			goto abort;
		}
		spin_lock_irqsave(&device->resource->req_lock, flags);
		ns = apply_mask_val(drbd_read_state(device), mask, val);
		rv = _drbd_set_state(device, ns, f, &done);
	} else {
		rv = _drbd_set_state(device, ns, f, &done);
	}

	spin_unlock_irqrestore(&device->resource->req_lock, flags);

	if (f & CS_WAIT_COMPLETE && rv == SS_SUCCESS) {
		D_ASSERT(device, current != first_peer_device(device)->connection->worker.task);
		wait_for_completion(&done);
	}

abort:
	if (f & CS_SERIALIZE)
		mutex_unlock(device->state_mutex);

	return rv;
}

/**
 * _drbd_request_state() - Request a state change (with flags)
 * @device:	DRBD device.
 * @mask:	mask of state bits to change.
 * @val:	value of new state bits.
 * @f:		flags
 *
 * Cousin of drbd_request_state(), useful with the CS_WAIT_COMPLETE
 * flag, or when logging of failed state change requests is not desired.
 */
enum drbd_state_rv
_drbd_request_state(struct drbd_device *device, union drbd_state mask,
		    union drbd_state val, enum chg_state_flags f)
{
	enum drbd_state_rv rv;

	wait_event(device->state_wait,
		   (rv = drbd_req_state(device, mask, val, f)) != SS_IN_TRANSIENT_STATE);

	return rv;
}

enum drbd_state_rv
_drbd_request_state_holding_state_mutex(struct drbd_device *device, union drbd_state mask,
		    union drbd_state val, enum chg_state_flags f)
{
	enum drbd_state_rv rv;

	BUG_ON(f & CS_SERIALIZE);

	wait_event_cmd(device->state_wait,
		       (rv = drbd_req_state(device, mask, val, f)) != SS_IN_TRANSIENT_STATE,
		       mutex_unlock(device->state_mutex),
		       mutex_lock(device->state_mutex));

	return rv;
}

static void print_st(struct drbd_device *device, const char *name, union drbd_state ns)
{
	drbd_err(device, " %s = { cs:%s ro:%s/%s ds:%s/%s %c%c%c%c%c%c }\n",
	    name,
	    drbd_conn_str(ns.conn),
	    drbd_role_str(ns.role),
	    drbd_role_str(ns.peer),
	    drbd_disk_str(ns.disk),
	    drbd_disk_str(ns.pdsk),
	    is_susp(ns) ? 's' : 'r',
	    ns.aftr_isp ? 'a' : '-',
	    ns.peer_isp ? 'p' : '-',
	    ns.user_isp ? 'u' : '-',
	    ns.susp_fen ? 'F' : '-',
	    ns.susp_nod ? 'N' : '-'
	    );
}

void print_st_err(struct drbd_device *device, union drbd_state os,
	          union drbd_state ns, enum drbd_state_rv err)
{
	if (err == SS_IN_TRANSIENT_STATE)
		return;
	drbd_err(device, "State change failed: %s\n", drbd_set_st_err_str(err));
	print_st(device, " state", os);
	print_st(device, "wanted", ns);
}

static long print_state_change(char *pb, union drbd_state os, union drbd_state ns,
			       enum chg_state_flags flags)
{
	char *pbp;
	pbp = pb;
	*pbp = 0;

	if (ns.role != os.role && flags & CS_DC_ROLE)
		pbp += sprintf(pbp, "role( %s -> %s ) ",
			       drbd_role_str(os.role),
			       drbd_role_str(ns.role));
	if (ns.peer != os.peer && flags & CS_DC_PEER)
		pbp += sprintf(pbp, "peer( %s -> %s ) ",
			       drbd_role_str(os.peer),
			       drbd_role_str(ns.peer));
	if (ns.conn != os.conn && flags & CS_DC_CONN)
		pbp += sprintf(pbp, "conn( %s -> %s ) ",
			       drbd_conn_str(os.conn),
			       drbd_conn_str(ns.conn));
	if (ns.disk != os.disk && flags & CS_DC_DISK)
		pbp += sprintf(pbp, "disk( %s -> %s ) ",
			       drbd_disk_str(os.disk),
			       drbd_disk_str(ns.disk));
	if (ns.pdsk != os.pdsk && flags & CS_DC_PDSK)
		pbp += sprintf(pbp, "pdsk( %s -> %s ) ",
			       drbd_disk_str(os.pdsk),
			       drbd_disk_str(ns.pdsk));

	return pbp - pb;
}

static void drbd_pr_state_change(struct drbd_device *device, union drbd_state os, union drbd_state ns,
				 enum chg_state_flags flags)
{
	char pb[300];
	char *pbp = pb;

	pbp += print_state_change(pbp, os, ns, flags ^ CS_DC_MASK);

	if (ns.aftr_isp != os.aftr_isp)
		pbp += sprintf(pbp, "aftr_isp( %d -> %d ) ",
			       os.aftr_isp,
			       ns.aftr_isp);
	if (ns.peer_isp != os.peer_isp)
		pbp += sprintf(pbp, "peer_isp( %d -> %d ) ",
			       os.peer_isp,
			       ns.peer_isp);
	if (ns.user_isp != os.user_isp)
		pbp += sprintf(pbp, "user_isp( %d -> %d ) ",
			       os.user_isp,
			       ns.user_isp);

	if (pbp != pb)
		drbd_info(device, "%s\n", pb);
}

static void conn_pr_state_change(struct drbd_connection *connection, union drbd_state os, union drbd_state ns,
				 enum chg_state_flags flags)
{
	char pb[300];
	char *pbp = pb;

	pbp += print_state_change(pbp, os, ns, flags);

	if (is_susp(ns) != is_susp(os) && flags & CS_DC_SUSP)
		pbp += sprintf(pbp, "susp( %d -> %d ) ",
			       is_susp(os),
			       is_susp(ns));

	if (pbp != pb)
		drbd_info(connection, "%s\n", pb);
}


/**
 * is_valid_state() - Returns an SS_ error code if ns is not valid
 * @device:	DRBD device.
 * @ns:		State to consider.
 */
static enum drbd_state_rv
is_valid_state(struct drbd_device *device, union drbd_state ns)
{
	/* See drbd_state_sw_errors in drbd_strings.c */

	enum drbd_fencing_p fp;
	enum drbd_state_rv rv = SS_SUCCESS;
	struct net_conf *nc;

	rcu_read_lock();
	fp = FP_DONT_CARE;
	if (get_ldev(device)) {
		fp = rcu_dereference(device->ldev->disk_conf)->fencing;
		put_ldev(device);
	}

	nc = rcu_dereference(first_peer_device(device)->connection->net_conf);
	if (nc) {
		if (!nc->two_primaries && ns.role == R_PRIMARY) {
			if (ns.peer == R_PRIMARY)
				rv = SS_TWO_PRIMARIES;
			else if (conn_highest_peer(first_peer_device(device)->connection) == R_PRIMARY)
				rv = SS_O_VOL_PEER_PRI;
		}
	}

	if (rv <= 0)
		/* already found a reason to abort */;
	else if (ns.role == R_SECONDARY && device->open_cnt)
		rv = SS_DEVICE_IN_USE;

	else if (ns.role == R_PRIMARY && ns.conn < C_CONNECTED && ns.disk < D_UP_TO_DATE)
		rv = SS_NO_UP_TO_DATE_DISK;

	else if (fp >= FP_RESOURCE &&
		 ns.role == R_PRIMARY && ns.conn < C_CONNECTED && ns.pdsk >= D_UNKNOWN)
		rv = SS_PRIMARY_NOP;

	else if (ns.role == R_PRIMARY && ns.disk <= D_INCONSISTENT && ns.pdsk <= D_INCONSISTENT)
		rv = SS_NO_UP_TO_DATE_DISK;

	else if (ns.conn > C_CONNECTED && ns.disk < D_INCONSISTENT)
		rv = SS_NO_LOCAL_DISK;

	else if (ns.conn > C_CONNECTED && ns.pdsk < D_INCONSISTENT)
		rv = SS_NO_REMOTE_DISK;

	else if (ns.conn > C_CONNECTED && ns.disk < D_UP_TO_DATE && ns.pdsk < D_UP_TO_DATE)
		rv = SS_NO_UP_TO_DATE_DISK;

	else if ((ns.conn == C_CONNECTED ||
		  ns.conn == C_WF_BITMAP_S ||
		  ns.conn == C_SYNC_SOURCE ||
		  ns.conn == C_PAUSED_SYNC_S) &&
		  ns.disk == D_OUTDATED)
		rv = SS_CONNECTED_OUTDATES;

	else if ((ns.conn == C_VERIFY_S || ns.conn == C_VERIFY_T) &&
		 (nc->verify_alg[0] == 0))
		rv = SS_NO_VERIFY_ALG;

	else if ((ns.conn == C_VERIFY_S || ns.conn == C_VERIFY_T) &&
		  first_peer_device(device)->connection->agreed_pro_version < 88)
		rv = SS_NOT_SUPPORTED;

	else if (ns.role == R_PRIMARY && ns.disk < D_UP_TO_DATE && ns.pdsk < D_UP_TO_DATE)
		rv = SS_NO_UP_TO_DATE_DISK;

	else if ((ns.conn == C_STARTING_SYNC_S || ns.conn == C_STARTING_SYNC_T) &&
                 ns.pdsk == D_UNKNOWN)
		rv = SS_NEED_CONNECTION;

	else if (ns.conn >= C_CONNECTED && ns.pdsk == D_UNKNOWN)
		rv = SS_CONNECTED_OUTDATES;

	rcu_read_unlock();

	return rv;
}

/**
 * is_valid_soft_transition() - Returns an SS_ error code if the state transition is not possible
 * This function limits state transitions that may be declined by DRBD. I.e.
 * user requests (aka soft transitions).
 * @device:	DRBD device.
 * @ns:		new state.
 * @os:		old state.
 */
static enum drbd_state_rv
is_valid_soft_transition(union drbd_state os, union drbd_state ns, struct drbd_connection *connection)
{
	enum drbd_state_rv rv = SS_SUCCESS;

	if ((ns.conn == C_STARTING_SYNC_T || ns.conn == C_STARTING_SYNC_S) &&
	    os.conn > C_CONNECTED)
		rv = SS_RESYNC_RUNNING;

	if (ns.conn == C_DISCONNECTING && os.conn == C_STANDALONE)
		rv = SS_ALREADY_STANDALONE;

	if (ns.disk > D_ATTACHING && os.disk == D_DISKLESS)
		rv = SS_IS_DISKLESS;

	if (ns.conn == C_WF_CONNECTION && os.conn < C_UNCONNECTED)
		rv = SS_NO_NET_CONFIG;

	if (ns.disk == D_OUTDATED && os.disk < D_OUTDATED && os.disk != D_ATTACHING)
		rv = SS_LOWER_THAN_OUTDATED;

	if (ns.conn == C_DISCONNECTING && os.conn == C_UNCONNECTED)
		rv = SS_IN_TRANSIENT_STATE;

	/* While establishing a connection only allow cstate to change.
	   Delay/refuse role changes, detach attach etc... (they do not touch cstate) */
	if (test_bit(STATE_SENT, &connection->flags) &&
	    !((ns.conn == C_WF_REPORT_PARAMS && os.conn == C_WF_CONNECTION) ||
	      (ns.conn >= C_CONNECTED && os.conn == C_WF_REPORT_PARAMS)))
		rv = SS_IN_TRANSIENT_STATE;

	if ((ns.conn == C_VERIFY_S || ns.conn == C_VERIFY_T) && os.conn < C_CONNECTED)
		rv = SS_NEED_CONNECTION;

	if ((ns.conn == C_VERIFY_S || ns.conn == C_VERIFY_T) &&
	    ns.conn != os.conn && os.conn > C_CONNECTED)
		rv = SS_RESYNC_RUNNING;

	if ((ns.conn == C_STARTING_SYNC_S || ns.conn == C_STARTING_SYNC_T) &&
	    os.conn < C_CONNECTED)
		rv = SS_NEED_CONNECTION;

	if ((ns.conn == C_SYNC_TARGET || ns.conn == C_SYNC_SOURCE)
	    && os.conn < C_WF_REPORT_PARAMS)
		rv = SS_NEED_CONNECTION; /* No NetworkFailure -> SyncTarget etc... */

	if (ns.conn == C_DISCONNECTING && ns.pdsk == D_OUTDATED &&
	    os.conn < C_CONNECTED && os.pdsk > D_OUTDATED)
		rv = SS_OUTDATE_WO_CONN;

	return rv;
}

static enum drbd_state_rv
is_valid_conn_transition(enum drbd_conns oc, enum drbd_conns nc)
{
	/* no change -> nothing to do, at least for the connection part */
	if (oc == nc)
		return SS_NOTHING_TO_DO;

	/* disconnect of an unconfigured connection does not make sense */
	if (oc == C_STANDALONE && nc == C_DISCONNECTING)
		return SS_ALREADY_STANDALONE;

	/* from C_STANDALONE, we start with C_UNCONNECTED */
	if (oc == C_STANDALONE && nc != C_UNCONNECTED)
		return SS_NEED_CONNECTION;

	/* When establishing a connection we need to go through WF_REPORT_PARAMS!
	   Necessary to do the right thing upon invalidate-remote on a disconnected resource */
	if (oc < C_WF_REPORT_PARAMS && nc >= C_CONNECTED)
		return SS_NEED_CONNECTION;

	/* After a network error only C_UNCONNECTED or C_DISCONNECTING may follow. */
	if (oc >= C_TIMEOUT && oc <= C_TEAR_DOWN && nc != C_UNCONNECTED && nc != C_DISCONNECTING)
		return SS_IN_TRANSIENT_STATE;

	/* After C_DISCONNECTING only C_STANDALONE may follow */
	if (oc == C_DISCONNECTING && nc != C_STANDALONE)
		return SS_IN_TRANSIENT_STATE;

	return SS_SUCCESS;
}


/**
 * is_valid_transition() - Returns an SS_ error code if the state transition is not possible
 * This limits hard state transitions. Hard state transitions are facts there are
 * imposed on DRBD by the environment. E.g. disk broke or network broke down.
 * But those hard state transitions are still not allowed to do everything.
 * @ns:		new state.
 * @os:		old state.
 */
static enum drbd_state_rv
is_valid_transition(union drbd_state os, union drbd_state ns)
{
	enum drbd_state_rv rv;

	rv = is_valid_conn_transition(os.conn, ns.conn);

	/* we cannot fail (again) if we already detached */
	if (ns.disk == D_FAILED && os.disk == D_DISKLESS)
		rv = SS_IS_DISKLESS;

	return rv;
}

static void print_sanitize_warnings(struct drbd_device *device, enum sanitize_state_warnings warn)
{
	static const char *msg_table[] = {
		[NO_WARNING] = "",
		[ABORTED_ONLINE_VERIFY] = "Online-verify aborted.",
		[ABORTED_RESYNC] = "Resync aborted.",
		[CONNECTION_LOST_NEGOTIATING] = "Connection lost while negotiating, no data!",
		[IMPLICITLY_UPGRADED_DISK] = "Implicitly upgraded disk",
		[IMPLICITLY_UPGRADED_PDSK] = "Implicitly upgraded pdsk",
	};

	if (warn != NO_WARNING)
		drbd_warn(device, "%s\n", msg_table[warn]);
}

/**
 * sanitize_state() - Resolves implicitly necessary additional changes to a state transition
 * @device:	DRBD device.
 * @os:		old state.
 * @ns:		new state.
 * @warn_sync_abort:
 *
 * When we loose connection, we have to set the state of the peers disk (pdsk)
 * to D_UNKNOWN. This rule and many more along those lines are in this function.
 */
static union drbd_state sanitize_state(struct drbd_device *device, union drbd_state os,
				       union drbd_state ns, enum sanitize_state_warnings *warn)
{
	enum drbd_fencing_p fp;
	enum drbd_disk_state disk_min, disk_max, pdsk_min, pdsk_max;

	if (warn)
		*warn = NO_WARNING;

	fp = FP_DONT_CARE;
	if (get_ldev(device)) {
		rcu_read_lock();
		fp = rcu_dereference(device->ldev->disk_conf)->fencing;
		rcu_read_unlock();
		put_ldev(device);
	}

	/* Implications from connection to peer and peer_isp */
	if (ns.conn < C_CONNECTED) {
		ns.peer_isp = 0;
		ns.peer = R_UNKNOWN;
		if (ns.pdsk > D_UNKNOWN || ns.pdsk < D_INCONSISTENT)
			ns.pdsk = D_UNKNOWN;
	}

	/* Clear the aftr_isp when becoming unconfigured */
	if (ns.conn == C_STANDALONE && ns.disk == D_DISKLESS && ns.role == R_SECONDARY)
		ns.aftr_isp = 0;

	/* An implication of the disk states onto the connection state */
	/* Abort resync if a disk fails/detaches */
	if (ns.conn > C_CONNECTED && (ns.disk <= D_FAILED || ns.pdsk <= D_FAILED)) {
		if (warn)
			*warn = ns.conn == C_VERIFY_S || ns.conn == C_VERIFY_T ?
				ABORTED_ONLINE_VERIFY : ABORTED_RESYNC;
		ns.conn = C_CONNECTED;
	}

	/* Connection breaks down before we finished "Negotiating" */
	if (ns.conn < C_CONNECTED && ns.disk == D_NEGOTIATING &&
	    get_ldev_if_state(device, D_NEGOTIATING)) {
		if (device->ed_uuid == device->ldev->md.uuid[UI_CURRENT]) {
			ns.disk = device->new_state_tmp.disk;
			ns.pdsk = device->new_state_tmp.pdsk;
		} else {
			if (warn)
				*warn = CONNECTION_LOST_NEGOTIATING;
			ns.disk = D_DISKLESS;
			ns.pdsk = D_UNKNOWN;
		}
		put_ldev(device);
	}

	/* D_CONSISTENT and D_OUTDATED vanish when we get connected */
	if (ns.conn >= C_CONNECTED && ns.conn < C_AHEAD) {
		if (ns.disk == D_CONSISTENT || ns.disk == D_OUTDATED)
			ns.disk = D_UP_TO_DATE;
		if (ns.pdsk == D_CONSISTENT || ns.pdsk == D_OUTDATED)
			ns.pdsk = D_UP_TO_DATE;
	}

	/* Implications of the connection stat on the disk states */
	disk_min = D_DISKLESS;
	disk_max = D_UP_TO_DATE;
	pdsk_min = D_INCONSISTENT;
	pdsk_max = D_UNKNOWN;
	switch ((enum drbd_conns)ns.conn) {
	case C_WF_BITMAP_T:
	case C_PAUSED_SYNC_T:
	case C_STARTING_SYNC_T:
	case C_WF_SYNC_UUID:
	case C_BEHIND:
		disk_min = D_INCONSISTENT;
		disk_max = D_OUTDATED;
		pdsk_min = D_UP_TO_DATE;
		pdsk_max = D_UP_TO_DATE;
		break;
	case C_VERIFY_S:
	case C_VERIFY_T:
		disk_min = D_UP_TO_DATE;
		disk_max = D_UP_TO_DATE;
		pdsk_min = D_UP_TO_DATE;
		pdsk_max = D_UP_TO_DATE;
		break;
	case C_CONNECTED:
		disk_min = D_DISKLESS;
		disk_max = D_UP_TO_DATE;
		pdsk_min = D_DISKLESS;
		pdsk_max = D_UP_TO_DATE;
		break;
	case C_WF_BITMAP_S:
	case C_PAUSED_SYNC_S:
	case C_STARTING_SYNC_S:
	case C_AHEAD:
		disk_min = D_UP_TO_DATE;
		disk_max = D_UP_TO_DATE;
		pdsk_min = D_INCONSISTENT;
		pdsk_max = D_CONSISTENT; /* D_OUTDATED would be nice. But explicit outdate necessary*/
		break;
	case C_SYNC_TARGET:
		disk_min = D_INCONSISTENT;
		disk_max = D_INCONSISTENT;
		pdsk_min = D_UP_TO_DATE;
		pdsk_max = D_UP_TO_DATE;
		break;
	case C_SYNC_SOURCE:
		disk_min = D_UP_TO_DATE;
		disk_max = D_UP_TO_DATE;
		pdsk_min = D_INCONSISTENT;
		pdsk_max = D_INCONSISTENT;
		break;
	case C_STANDALONE:
	case C_DISCONNECTING:
	case C_UNCONNECTED:
	case C_TIMEOUT:
	case C_BROKEN_PIPE:
	case C_NETWORK_FAILURE:
	case C_PROTOCOL_ERROR:
	case C_TEAR_DOWN:
	case C_WF_CONNECTION:
	case C_WF_REPORT_PARAMS:
	case C_MASK:
		break;
	}
	if (ns.disk > disk_max)
		ns.disk = disk_max;

	if (ns.disk < disk_min) {
		if (warn)
			*warn = IMPLICITLY_UPGRADED_DISK;
		ns.disk = disk_min;
	}
	if (ns.pdsk > pdsk_max)
		ns.pdsk = pdsk_max;

	if (ns.pdsk < pdsk_min) {
		if (warn)
			*warn = IMPLICITLY_UPGRADED_PDSK;
		ns.pdsk = pdsk_min;
	}

	if (fp == FP_STONITH &&
	    (ns.role == R_PRIMARY && ns.conn < C_CONNECTED && ns.pdsk > D_OUTDATED) &&
	    !(os.role == R_PRIMARY && os.conn < C_CONNECTED && os.pdsk > D_OUTDATED))
		ns.susp_fen = 1; /* Suspend IO while fence-peer handler runs (peer lost) */

	if (device->resource->res_opts.on_no_data == OND_SUSPEND_IO &&
	    (ns.role == R_PRIMARY && ns.disk < D_UP_TO_DATE && ns.pdsk < D_UP_TO_DATE) &&
	    !(os.role == R_PRIMARY && os.disk < D_UP_TO_DATE && os.pdsk < D_UP_TO_DATE))
		ns.susp_nod = 1; /* Suspend IO while no data available (no accessible data available) */

	if (ns.aftr_isp || ns.peer_isp || ns.user_isp) {
		if (ns.conn == C_SYNC_SOURCE)
			ns.conn = C_PAUSED_SYNC_S;
		if (ns.conn == C_SYNC_TARGET)
			ns.conn = C_PAUSED_SYNC_T;
	} else {
		if (ns.conn == C_PAUSED_SYNC_S)
			ns.conn = C_SYNC_SOURCE;
		if (ns.conn == C_PAUSED_SYNC_T)
			ns.conn = C_SYNC_TARGET;
	}

	return ns;
}

void drbd_resume_al(struct drbd_device *device)
{
	if (test_and_clear_bit(AL_SUSPENDED, &device->flags))
		drbd_info(device, "Resumed AL updates\n");
}

/* helper for __drbd_set_state */
static void set_ov_position(struct drbd_device *device, enum drbd_conns cs)
{
	if (first_peer_device(device)->connection->agreed_pro_version < 90)
		device->ov_start_sector = 0;
	device->rs_total = drbd_bm_bits(device);
	device->ov_position = 0;
	if (cs == C_VERIFY_T) {
		/* starting online verify from an arbitrary position
		 * does not fit well into the existing protocol.
		 * on C_VERIFY_T, we initialize ov_left and friends
		 * implicitly in receive_DataRequest once the
		 * first P_OV_REQUEST is received */
		device->ov_start_sector = ~(sector_t)0;
	} else {
		unsigned long bit = BM_SECT_TO_BIT(device->ov_start_sector);
		if (bit >= device->rs_total) {
			device->ov_start_sector =
				BM_BIT_TO_SECT(device->rs_total - 1);
			device->rs_total = 1;
		} else
			device->rs_total -= bit;
		device->ov_position = device->ov_start_sector;
	}
	device->ov_left = device->rs_total;
}

/**
 * __drbd_set_state() - Set a new DRBD state
 * @device:	DRBD device.
 * @ns:		new state.
 * @flags:	Flags
 * @done:	Optional completion, that will get completed after the after_state_ch() finished
 *
 * Caller needs to hold req_lock, and global_state_lock. Do not call directly.
 */
enum drbd_state_rv
__drbd_set_state(struct drbd_device *device, union drbd_state ns,
	         enum chg_state_flags flags, struct completion *done)
{
	struct drbd_peer_device *peer_device = first_peer_device(device);
	struct drbd_connection *connection = peer_device ? peer_device->connection : NULL;
	union drbd_state os;
	enum drbd_state_rv rv = SS_SUCCESS;
	enum sanitize_state_warnings ssw;
	struct after_state_chg_work *ascw;

	os = drbd_read_state(device);

	ns = sanitize_state(device, os, ns, &ssw);
	if (ns.i == os.i)
		return SS_NOTHING_TO_DO;

	rv = is_valid_transition(os, ns);
	if (rv < SS_SUCCESS)
		return rv;

	if (!(flags & CS_HARD)) {
		/*  pre-state-change checks ; only look at ns  */
		/* See drbd_state_sw_errors in drbd_strings.c */

		rv = is_valid_state(device, ns);
		if (rv < SS_SUCCESS) {
			/* If the old state was illegal as well, then let
			   this happen...*/

			if (is_valid_state(device, os) == rv)
				rv = is_valid_soft_transition(os, ns, connection);
		} else
			rv = is_valid_soft_transition(os, ns, connection);
	}

	if (rv < SS_SUCCESS) {
		if (flags & CS_VERBOSE)
			print_st_err(device, os, ns, rv);
		return rv;
	}

	print_sanitize_warnings(device, ssw);

	drbd_pr_state_change(device, os, ns, flags);

	/* Display changes to the susp* flags that where caused by the call to
	   sanitize_state(). Only display it here if we where not called from
	   _conn_request_state() */
	if (!(flags & CS_DC_SUSP))
		conn_pr_state_change(connection, os, ns,
				     (flags & ~CS_DC_MASK) | CS_DC_SUSP);

	/* if we are going -> D_FAILED or D_DISKLESS, grab one extra reference
	 * on the ldev here, to be sure the transition -> D_DISKLESS resp.
	 * drbd_ldev_destroy() won't happen before our corresponding
	 * after_state_ch works run, where we put_ldev again. */
	if ((os.disk != D_FAILED && ns.disk == D_FAILED) ||
	    (os.disk != D_DISKLESS && ns.disk == D_DISKLESS))
		atomic_inc(&device->local_cnt);

	if (!is_sync_state(os.conn) && is_sync_state(ns.conn))
		clear_bit(RS_DONE, &device->flags);

	/* changes to local_cnt and device flags should be visible before
	 * changes to state, which again should be visible before anything else
	 * depending on that change happens. */
	smp_wmb();
	device->state.i = ns.i;
	device->resource->susp = ns.susp;
	device->resource->susp_nod = ns.susp_nod;
	device->resource->susp_fen = ns.susp_fen;
	smp_wmb();

	/* put replicated vs not-replicated requests in seperate epochs */
	if (drbd_should_do_remote((union drbd_dev_state)os.i) !=
	    drbd_should_do_remote((union drbd_dev_state)ns.i))
		start_new_tl_epoch(connection);

	if (os.disk == D_ATTACHING && ns.disk >= D_NEGOTIATING)
		drbd_print_uuids(device, "attached to UUIDs");

	/* Wake up role changes, that were delayed because of connection establishing */
	if (os.conn == C_WF_REPORT_PARAMS && ns.conn != C_WF_REPORT_PARAMS &&
	    no_peer_wf_report_params(connection)) {
		clear_bit(STATE_SENT, &connection->flags);
		wake_up_all_devices(connection);
	}

	wake_up(&device->misc_wait);
	wake_up(&device->state_wait);
	wake_up(&connection->ping_wait);

	/* Aborted verify run, or we reached the stop sector.
	 * Log the last position, unless end-of-device. */
	if ((os.conn == C_VERIFY_S || os.conn == C_VERIFY_T) &&
	    ns.conn <= C_CONNECTED) {
		device->ov_start_sector =
			BM_BIT_TO_SECT(drbd_bm_bits(device) - device->ov_left);
		if (device->ov_left)
			drbd_info(device, "Online Verify reached sector %llu\n",
				(unsigned long long)device->ov_start_sector);
	}

	if ((os.conn == C_PAUSED_SYNC_T || os.conn == C_PAUSED_SYNC_S) &&
	    (ns.conn == C_SYNC_TARGET  || ns.conn == C_SYNC_SOURCE)) {
		drbd_info(device, "Syncer continues.\n");
		device->rs_paused += (long)jiffies
				  -(long)device->rs_mark_time[device->rs_last_mark];
		if (ns.conn == C_SYNC_TARGET)
			mod_timer(&device->resync_timer, jiffies);
	}

	if ((os.conn == C_SYNC_TARGET  || os.conn == C_SYNC_SOURCE) &&
	    (ns.conn == C_PAUSED_SYNC_T || ns.conn == C_PAUSED_SYNC_S)) {
		drbd_info(device, "Resync suspended\n");
		device->rs_mark_time[device->rs_last_mark] = jiffies;
	}

	if (os.conn == C_CONNECTED &&
	    (ns.conn == C_VERIFY_S || ns.conn == C_VERIFY_T)) {
		unsigned long now = jiffies;
		int i;

		set_ov_position(device, ns.conn);
		device->rs_start = now;
		device->rs_last_sect_ev = 0;
		device->ov_last_oos_size = 0;
		device->ov_last_oos_start = 0;

		for (i = 0; i < DRBD_SYNC_MARKS; i++) {
			device->rs_mark_left[i] = device->ov_left;
			device->rs_mark_time[i] = now;
		}

		drbd_rs_controller_reset(device);

		if (ns.conn == C_VERIFY_S) {
			drbd_info(device, "Starting Online Verify from sector %llu\n",
					(unsigned long long)device->ov_position);
			mod_timer(&device->resync_timer, jiffies);
		}
	}

	if (get_ldev(device)) {
		u32 mdf = device->ldev->md.flags & ~(MDF_CONSISTENT|MDF_PRIMARY_IND|
						 MDF_CONNECTED_IND|MDF_WAS_UP_TO_DATE|
						 MDF_PEER_OUT_DATED|MDF_CRASHED_PRIMARY);

		mdf &= ~MDF_AL_CLEAN;
		if (test_bit(CRASHED_PRIMARY, &device->flags))
			mdf |= MDF_CRASHED_PRIMARY;
		if (device->state.role == R_PRIMARY ||
		    (device->state.pdsk < D_INCONSISTENT && device->state.peer == R_PRIMARY))
			mdf |= MDF_PRIMARY_IND;
		if (device->state.conn > C_WF_REPORT_PARAMS)
			mdf |= MDF_CONNECTED_IND;
		if (device->state.disk > D_INCONSISTENT)
			mdf |= MDF_CONSISTENT;
		if (device->state.disk > D_OUTDATED)
			mdf |= MDF_WAS_UP_TO_DATE;
		if (device->state.pdsk <= D_OUTDATED && device->state.pdsk >= D_INCONSISTENT)
			mdf |= MDF_PEER_OUT_DATED;
		if (mdf != device->ldev->md.flags) {
			device->ldev->md.flags = mdf;
			drbd_md_mark_dirty(device);
		}
		if (os.disk < D_CONSISTENT && ns.disk >= D_CONSISTENT)
			drbd_set_ed_uuid(device, device->ldev->md.uuid[UI_CURRENT]);
		put_ldev(device);
	}

	/* Peer was forced D_UP_TO_DATE & R_PRIMARY, consider to resync */
	if (os.disk == D_INCONSISTENT && os.pdsk == D_INCONSISTENT &&
	    os.peer == R_SECONDARY && ns.peer == R_PRIMARY)
		set_bit(CONSIDER_RESYNC, &device->flags);

	/* Receiver should clean up itself */
	if (os.conn != C_DISCONNECTING && ns.conn == C_DISCONNECTING)
		drbd_thread_stop_nowait(&connection->receiver);

	/* Now the receiver finished cleaning up itself, it should die */
	if (os.conn != C_STANDALONE && ns.conn == C_STANDALONE)
		drbd_thread_stop_nowait(&connection->receiver);

	/* Upon network failure, we need to restart the receiver. */
	if (os.conn > C_WF_CONNECTION &&
	    ns.conn <= C_TEAR_DOWN && ns.conn >= C_TIMEOUT)
		drbd_thread_restart_nowait(&connection->receiver);

	/* Resume AL writing if we get a connection */
	if (os.conn < C_CONNECTED && ns.conn >= C_CONNECTED) {
		drbd_resume_al(device);
		connection->connect_cnt++;
	}

	/* remember last attach time so request_timer_fn() won't
	 * kill newly established sessions while we are still trying to thaw
	 * previously frozen IO */
	if ((os.disk == D_ATTACHING || os.disk == D_NEGOTIATING) &&
	    ns.disk > D_NEGOTIATING)
		device->last_reattach_jif = jiffies;

	ascw = kmalloc(sizeof(*ascw), GFP_ATOMIC);
	if (ascw) {
		ascw->os = os;
		ascw->ns = ns;
		ascw->flags = flags;
		ascw->w.cb = w_after_state_ch;
		ascw->device = device;
		ascw->done = done;
		drbd_queue_work(&connection->sender_work,
				&ascw->w);
	} else {
		drbd_err(device, "Could not kmalloc an ascw\n");
	}

	return rv;
}

static int w_after_state_ch(struct drbd_work *w, int unused)
{
	struct after_state_chg_work *ascw =
		container_of(w, struct after_state_chg_work, w);
	struct drbd_device *device = ascw->device;

	after_state_ch(device, ascw->os, ascw->ns, ascw->flags);
	if (ascw->flags & CS_WAIT_COMPLETE)
		complete(ascw->done);
	kfree(ascw);

	return 0;
}

static void abw_start_sync(struct drbd_device *device, int rv)
{
	if (rv) {
		drbd_err(device, "Writing the bitmap failed not starting resync.\n");
		_drbd_request_state(device, NS(conn, C_CONNECTED), CS_VERBOSE);
		return;
	}

	switch (device->state.conn) {
	case C_STARTING_SYNC_T:
		_drbd_request_state(device, NS(conn, C_WF_SYNC_UUID), CS_VERBOSE);
		break;
	case C_STARTING_SYNC_S:
		drbd_start_resync(device, C_SYNC_SOURCE);
		break;
	}
}

int drbd_bitmap_io_from_worker(struct drbd_device *device,
		int (*io_fn)(struct drbd_device *),
		char *why, enum bm_flag flags)
{
	int rv;

	D_ASSERT(device, current == first_peer_device(device)->connection->worker.task);

	/* open coded non-blocking drbd_suspend_io(device); */
	set_bit(SUSPEND_IO, &device->flags);

	drbd_bm_lock(device, why, flags);
	rv = io_fn(device);
	drbd_bm_unlock(device);

	drbd_resume_io(device);

	return rv;
}

/**
 * after_state_ch() - Perform after state change actions that may sleep
 * @device:	DRBD device.
 * @os:		old state.
 * @ns:		new state.
 * @flags:	Flags
 */
static void after_state_ch(struct drbd_device *device, union drbd_state os,
			   union drbd_state ns, enum chg_state_flags flags)
{
	struct drbd_resource *resource = device->resource;
	struct drbd_peer_device *peer_device = first_peer_device(device);
	struct drbd_connection *connection = peer_device ? peer_device->connection : NULL;
	struct sib_info sib;

	sib.sib_reason = SIB_STATE_CHANGE;
	sib.os = os;
	sib.ns = ns;

	if ((os.disk != D_UP_TO_DATE || os.pdsk != D_UP_TO_DATE)
	&&  (ns.disk == D_UP_TO_DATE && ns.pdsk == D_UP_TO_DATE)) {
		clear_bit(CRASHED_PRIMARY, &device->flags);
		if (device->p_uuid)
			device->p_uuid[UI_FLAGS] &= ~((u64)2);
	}

	/* Inform userspace about the change... */
	drbd_bcast_event(device, &sib);

	if (!(os.role == R_PRIMARY && os.disk < D_UP_TO_DATE && os.pdsk < D_UP_TO_DATE) &&
	    (ns.role == R_PRIMARY && ns.disk < D_UP_TO_DATE && ns.pdsk < D_UP_TO_DATE))
		drbd_khelper(device, "pri-on-incon-degr");

	/* Here we have the actions that are performed after a
	   state change. This function might sleep */

	if (ns.susp_nod) {
		enum drbd_req_event what = NOTHING;

		spin_lock_irq(&device->resource->req_lock);
		if (os.conn < C_CONNECTED && conn_lowest_conn(connection) >= C_CONNECTED)
			what = RESEND;

		if ((os.disk == D_ATTACHING || os.disk == D_NEGOTIATING) &&
		    conn_lowest_disk(connection) > D_NEGOTIATING)
			what = RESTART_FROZEN_DISK_IO;

		if (resource->susp_nod && what != NOTHING) {
			_tl_restart(connection, what);
			_conn_request_state(connection,
					    (union drbd_state) { { .susp_nod = 1 } },
					    (union drbd_state) { { .susp_nod = 0 } },
					    CS_VERBOSE);
		}
		spin_unlock_irq(&device->resource->req_lock);
	}

	if (ns.susp_fen) {
		spin_lock_irq(&device->resource->req_lock);
		if (resource->susp_fen && conn_lowest_conn(connection) >= C_CONNECTED) {
			/* case2: The connection was established again: */
			struct drbd_peer_device *peer_device;
			int vnr;

			rcu_read_lock();
			idr_for_each_entry(&connection->peer_devices, peer_device, vnr)
				clear_bit(NEW_CUR_UUID, &peer_device->device->flags);
			rcu_read_unlock();
			_tl_restart(connection, RESEND);
			_conn_request_state(connection,
					    (union drbd_state) { { .susp_fen = 1 } },
					    (union drbd_state) { { .susp_fen = 0 } },
					    CS_VERBOSE);
		}
		spin_unlock_irq(&device->resource->req_lock);
	}

	/* Became sync source.  With protocol >= 96, we still need to send out
	 * the sync uuid now. Need to do that before any drbd_send_state, or
	 * the other side may go "paused sync" before receiving the sync uuids,
	 * which is unexpected. */
	if ((os.conn != C_SYNC_SOURCE && os.conn != C_PAUSED_SYNC_S) &&
	    (ns.conn == C_SYNC_SOURCE || ns.conn == C_PAUSED_SYNC_S) &&
	    connection->agreed_pro_version >= 96 && get_ldev(device)) {
		drbd_gen_and_send_sync_uuid(peer_device);
		put_ldev(device);
	}

	/* Do not change the order of the if above and the two below... */
	if (os.pdsk == D_DISKLESS &&
	    ns.pdsk > D_DISKLESS && ns.pdsk != D_UNKNOWN) {      /* attach on the peer */
		/* we probably will start a resync soon.
		 * make sure those things are properly reset. */
		device->rs_total = 0;
		device->rs_failed = 0;
		atomic_set(&device->rs_pending_cnt, 0);
		drbd_rs_cancel_all(device);

		drbd_send_uuids(peer_device);
		drbd_send_state(peer_device, ns);
	}
	/* No point in queuing send_bitmap if we don't have a connection
	 * anymore, so check also the _current_ state, not only the new state
	 * at the time this work was queued. */
	if (os.conn != C_WF_BITMAP_S && ns.conn == C_WF_BITMAP_S &&
	    device->state.conn == C_WF_BITMAP_S)
		drbd_queue_bitmap_io(device, &drbd_send_bitmap, NULL,
				"send_bitmap (WFBitMapS)",
				BM_LOCKED_TEST_ALLOWED);

	/* Lost contact to peer's copy of the data */
	if ((os.pdsk >= D_INCONSISTENT &&
	     os.pdsk != D_UNKNOWN &&
	     os.pdsk != D_OUTDATED)
	&&  (ns.pdsk < D_INCONSISTENT ||
	     ns.pdsk == D_UNKNOWN ||
	     ns.pdsk == D_OUTDATED)) {
		if (get_ldev(device)) {
			if ((ns.role == R_PRIMARY || ns.peer == R_PRIMARY) &&
			    device->ldev->md.uuid[UI_BITMAP] == 0 && ns.disk >= D_UP_TO_DATE) {
				if (drbd_suspended(device)) {
					set_bit(NEW_CUR_UUID, &device->flags);
				} else {
					drbd_uuid_new_current(device);
					drbd_send_uuids(peer_device);
				}
			}
			put_ldev(device);
		}
	}

	if (ns.pdsk < D_INCONSISTENT && get_ldev(device)) {
		if (os.peer == R_SECONDARY && ns.peer == R_PRIMARY &&
		    device->ldev->md.uuid[UI_BITMAP] == 0 && ns.disk >= D_UP_TO_DATE) {
			drbd_uuid_new_current(device);
			drbd_send_uuids(peer_device);
		}
		/* D_DISKLESS Peer becomes secondary */
		if (os.peer == R_PRIMARY && ns.peer == R_SECONDARY)
			/* We may still be Primary ourselves.
			 * No harm done if the bitmap still changes,
			 * redirtied pages will follow later. */
			drbd_bitmap_io_from_worker(device, &drbd_bm_write,
				"demote diskless peer", BM_LOCKED_SET_ALLOWED);
		put_ldev(device);
	}

	/* Write out all changed bits on demote.
	 * Though, no need to da that just yet
	 * if there is a resync going on still */
	if (os.role == R_PRIMARY && ns.role == R_SECONDARY &&
		device->state.conn <= C_CONNECTED && get_ldev(device)) {
		/* No changes to the bitmap expected this time, so assert that,
		 * even though no harm was done if it did change. */
		drbd_bitmap_io_from_worker(device, &drbd_bm_write,
				"demote", BM_LOCKED_TEST_ALLOWED);
		put_ldev(device);
	}

	/* Last part of the attaching process ... */
	if (ns.conn >= C_CONNECTED &&
	    os.disk == D_ATTACHING && ns.disk == D_NEGOTIATING) {
		drbd_send_sizes(peer_device, 0, 0);  /* to start sync... */
		drbd_send_uuids(peer_device);
		drbd_send_state(peer_device, ns);
	}

	/* We want to pause/continue resync, tell peer. */
	if (ns.conn >= C_CONNECTED &&
	     ((os.aftr_isp != ns.aftr_isp) ||
	      (os.user_isp != ns.user_isp)))
		drbd_send_state(peer_device, ns);

	/* In case one of the isp bits got set, suspend other devices. */
	if ((!os.aftr_isp && !os.peer_isp && !os.user_isp) &&
	    (ns.aftr_isp || ns.peer_isp || ns.user_isp))
		suspend_other_sg(device);

	/* Make sure the peer gets informed about eventual state
	   changes (ISP bits) while we were in WFReportParams. */
	if (os.conn == C_WF_REPORT_PARAMS && ns.conn >= C_CONNECTED)
		drbd_send_state(peer_device, ns);

	if (os.conn != C_AHEAD && ns.conn == C_AHEAD)
		drbd_send_state(peer_device, ns);

	/* We are in the progress to start a full sync... */
	if ((os.conn != C_STARTING_SYNC_T && ns.conn == C_STARTING_SYNC_T) ||
	    (os.conn != C_STARTING_SYNC_S && ns.conn == C_STARTING_SYNC_S))
		/* no other bitmap changes expected during this phase */
		drbd_queue_bitmap_io(device,
			&drbd_bmio_set_n_write, &abw_start_sync,
			"set_n_write from StartingSync", BM_LOCKED_TEST_ALLOWED);

	/* first half of local IO error, failure to attach,
	 * or administrative detach */
	if (os.disk != D_FAILED && ns.disk == D_FAILED) {
		enum drbd_io_error_p eh = EP_PASS_ON;
		int was_io_error = 0;
		/* corresponding get_ldev was in __drbd_set_state, to serialize
		 * our cleanup here with the transition to D_DISKLESS.
		 * But is is still not save to dreference ldev here, since
		 * we might come from an failed Attach before ldev was set. */
		if (device->ldev) {
			rcu_read_lock();
			eh = rcu_dereference(device->ldev->disk_conf)->on_io_error;
			rcu_read_unlock();

			was_io_error = test_and_clear_bit(WAS_IO_ERROR, &device->flags);

			if (was_io_error && eh == EP_CALL_HELPER)
				drbd_khelper(device, "local-io-error");

			/* Immediately allow completion of all application IO,
			 * that waits for completion from the local disk,
			 * if this was a force-detach due to disk_timeout
			 * or administrator request (drbdsetup detach --force).
			 * Do NOT abort otherwise.
			 * Aborting local requests may cause serious problems,
			 * if requests are completed to upper layers already,
			 * and then later the already submitted local bio completes.
			 * This can cause DMA into former bio pages that meanwhile
			 * have been re-used for other things.
			 * So aborting local requests may cause crashes,
			 * or even worse, silent data corruption.
			 */
			if (test_and_clear_bit(FORCE_DETACH, &device->flags))
				tl_abort_disk_io(device);

			/* current state still has to be D_FAILED,
			 * there is only one way out: to D_DISKLESS,
			 * and that may only happen after our put_ldev below. */
			if (device->state.disk != D_FAILED)
				drbd_err(device,
					"ASSERT FAILED: disk is %s during detach\n",
					drbd_disk_str(device->state.disk));

			if (ns.conn >= C_CONNECTED)
				drbd_send_state(peer_device, ns);

			drbd_rs_cancel_all(device);

			/* In case we want to get something to stable storage still,
			 * this may be the last chance.
			 * Following put_ldev may transition to D_DISKLESS. */
			drbd_md_sync(device);
		}
		put_ldev(device);
	}

	/* second half of local IO error, failure to attach,
	 * or administrative detach,
	 * after local_cnt references have reached zero again */
	if (os.disk != D_DISKLESS && ns.disk == D_DISKLESS) {
		/* We must still be diskless,
		 * re-attach has to be serialized with this! */
		if (device->state.disk != D_DISKLESS)
			drbd_err(device,
				 "ASSERT FAILED: disk is %s while going diskless\n",
				 drbd_disk_str(device->state.disk));

		if (ns.conn >= C_CONNECTED)
			drbd_send_state(peer_device, ns);
		/* corresponding get_ldev in __drbd_set_state
		 * this may finally trigger drbd_ldev_destroy. */
		put_ldev(device);
	}

	/* Notify peer that I had a local IO error, and did not detached.. */
	if (os.disk == D_UP_TO_DATE && ns.disk == D_INCONSISTENT && ns.conn >= C_CONNECTED)
		drbd_send_state(peer_device, ns);

	/* Disks got bigger while they were detached */
	if (ns.disk > D_NEGOTIATING && ns.pdsk > D_NEGOTIATING &&
	    test_and_clear_bit(RESYNC_AFTER_NEG, &device->flags)) {
		if (ns.conn == C_CONNECTED)
			resync_after_online_grow(device);
	}

	/* A resync finished or aborted, wake paused devices... */
	if ((os.conn > C_CONNECTED && ns.conn <= C_CONNECTED) ||
	    (os.peer_isp && !ns.peer_isp) ||
	    (os.user_isp && !ns.user_isp))
		resume_next_sg(device);

	/* sync target done with resync.  Explicitly notify peer, even though
	 * it should (at least for non-empty resyncs) already know itself. */
	if (os.disk < D_UP_TO_DATE && os.conn >= C_SYNC_SOURCE && ns.conn == C_CONNECTED)
		drbd_send_state(peer_device, ns);

	/* Verify finished, or reached stop sector.  Peer did not know about
	 * the stop sector, and we may even have changed the stop sector during
	 * verify to interrupt/stop early.  Send the new state. */
	if (os.conn == C_VERIFY_S && ns.conn == C_CONNECTED
	&& verify_can_do_stop_sector(device))
		drbd_send_state(peer_device, ns);

	/* This triggers bitmap writeout of potentially still unwritten pages
	 * if the resync finished cleanly, or aborted because of peer disk
	 * failure, or because of connection loss.
	 * For resync aborted because of local disk failure, we cannot do
	 * any bitmap writeout anymore.
	 * No harm done if some bits change during this phase.
	 */
	if (os.conn > C_CONNECTED && ns.conn <= C_CONNECTED && get_ldev(device)) {
		drbd_queue_bitmap_io(device, &drbd_bm_write_copy_pages, NULL,
			"write from resync_finished", BM_LOCKED_CHANGE_ALLOWED);
		put_ldev(device);
	}

	if (ns.disk == D_DISKLESS &&
	    ns.conn == C_STANDALONE &&
	    ns.role == R_SECONDARY) {
		if (os.aftr_isp != ns.aftr_isp)
			resume_next_sg(device);
	}

	drbd_md_sync(device);
}

struct after_conn_state_chg_work {
	struct drbd_work w;
	enum drbd_conns oc;
	union drbd_state ns_min;
	union drbd_state ns_max; /* new, max state, over all devices */
	enum chg_state_flags flags;
	struct drbd_connection *connection;
};

static int w_after_conn_state_ch(struct drbd_work *w, int unused)
{
	struct after_conn_state_chg_work *acscw =
		container_of(w, struct after_conn_state_chg_work, w);
	struct drbd_connection *connection = acscw->connection;
	enum drbd_conns oc = acscw->oc;
	union drbd_state ns_max = acscw->ns_max;
	struct drbd_peer_device *peer_device;
	int vnr;

	kfree(acscw);

	/* Upon network configuration, we need to start the receiver */
	if (oc == C_STANDALONE && ns_max.conn == C_UNCONNECTED)
		drbd_thread_start(&connection->receiver);

	if (oc == C_DISCONNECTING && ns_max.conn == C_STANDALONE) {
		struct net_conf *old_conf;

		mutex_lock(&connection->resource->conf_update);
		old_conf = connection->net_conf;
		connection->my_addr_len = 0;
		connection->peer_addr_len = 0;
		RCU_INIT_POINTER(connection->net_conf, NULL);
		conn_free_crypto(connection);
		mutex_unlock(&connection->resource->conf_update);

		synchronize_rcu();
		kfree(old_conf);
	}

	if (ns_max.susp_fen) {
		/* case1: The outdate peer handler is successful: */
		if (ns_max.pdsk <= D_OUTDATED) {
			rcu_read_lock();
			idr_for_each_entry(&connection->peer_devices, peer_device, vnr) {
				struct drbd_device *device = peer_device->device;
				if (test_bit(NEW_CUR_UUID, &device->flags)) {
					drbd_uuid_new_current(device);
					clear_bit(NEW_CUR_UUID, &device->flags);
				}
			}
			rcu_read_unlock();
			spin_lock_irq(&connection->resource->req_lock);
			_tl_restart(connection, CONNECTION_LOST_WHILE_PENDING);
			_conn_request_state(connection,
					    (union drbd_state) { { .susp_fen = 1 } },
					    (union drbd_state) { { .susp_fen = 0 } },
					    CS_VERBOSE);
			spin_unlock_irq(&connection->resource->req_lock);
		}
	}
	kref_put(&connection->kref, drbd_destroy_connection);

	conn_md_sync(connection);

	return 0;
}

static void conn_old_common_state(struct drbd_connection *connection, union drbd_state *pcs, enum chg_state_flags *pf)
{
	enum chg_state_flags flags = ~0;
	struct drbd_peer_device *peer_device;
	int vnr, first_vol = 1;
	union drbd_dev_state os, cs = {
		{ .role = R_SECONDARY,
		  .peer = R_UNKNOWN,
		  .conn = connection->cstate,
		  .disk = D_DISKLESS,
		  .pdsk = D_UNKNOWN,
		} };

	rcu_read_lock();
	idr_for_each_entry(&connection->peer_devices, peer_device, vnr) {
		struct drbd_device *device = peer_device->device;
		os = device->state;

		if (first_vol) {
			cs = os;
			first_vol = 0;
			continue;
		}

		if (cs.role != os.role)
			flags &= ~CS_DC_ROLE;

		if (cs.peer != os.peer)
			flags &= ~CS_DC_PEER;

		if (cs.conn != os.conn)
			flags &= ~CS_DC_CONN;

		if (cs.disk != os.disk)
			flags &= ~CS_DC_DISK;

		if (cs.pdsk != os.pdsk)
			flags &= ~CS_DC_PDSK;
	}
	rcu_read_unlock();

	*pf |= CS_DC_MASK;
	*pf &= flags;
	(*pcs).i = cs.i;
}

static enum drbd_state_rv
conn_is_valid_transition(struct drbd_connection *connection, union drbd_state mask, union drbd_state val,
			 enum chg_state_flags flags)
{
	enum drbd_state_rv rv = SS_SUCCESS;
	union drbd_state ns, os;
	struct drbd_peer_device *peer_device;
	int vnr;

	rcu_read_lock();
	idr_for_each_entry(&connection->peer_devices, peer_device, vnr) {
		struct drbd_device *device = peer_device->device;
		os = drbd_read_state(device);
		ns = sanitize_state(device, os, apply_mask_val(os, mask, val), NULL);

		if (flags & CS_IGN_OUTD_FAIL && ns.disk == D_OUTDATED && os.disk < D_OUTDATED)
			ns.disk = os.disk;

		if (ns.i == os.i)
			continue;

		rv = is_valid_transition(os, ns);

		if (rv >= SS_SUCCESS && !(flags & CS_HARD)) {
			rv = is_valid_state(device, ns);
			if (rv < SS_SUCCESS) {
				if (is_valid_state(device, os) == rv)
					rv = is_valid_soft_transition(os, ns, connection);
			} else
				rv = is_valid_soft_transition(os, ns, connection);
		}

		if (rv < SS_SUCCESS) {
			if (flags & CS_VERBOSE)
				print_st_err(device, os, ns, rv);
			break;
		}
	}
	rcu_read_unlock();

	return rv;
}

static void
conn_set_state(struct drbd_connection *connection, union drbd_state mask, union drbd_state val,
	       union drbd_state *pns_min, union drbd_state *pns_max, enum chg_state_flags flags)
{
	union drbd_state ns, os, ns_max = { };
	union drbd_state ns_min = {
		{ .role = R_MASK,
		  .peer = R_MASK,
		  .conn = val.conn,
		  .disk = D_MASK,
		  .pdsk = D_MASK
		} };
	struct drbd_peer_device *peer_device;
	enum drbd_state_rv rv;
	int vnr, number_of_volumes = 0;

	if (mask.conn == C_MASK) {
		/* remember last connect time so request_timer_fn() won't
		 * kill newly established sessions while we are still trying to thaw
		 * previously frozen IO */
		if (connection->cstate != C_WF_REPORT_PARAMS && val.conn == C_WF_REPORT_PARAMS)
			connection->last_reconnect_jif = jiffies;

		connection->cstate = val.conn;
	}

	rcu_read_lock();
	idr_for_each_entry(&connection->peer_devices, peer_device, vnr) {
		struct drbd_device *device = peer_device->device;
		number_of_volumes++;
		os = drbd_read_state(device);
		ns = apply_mask_val(os, mask, val);
		ns = sanitize_state(device, os, ns, NULL);

		if (flags & CS_IGN_OUTD_FAIL && ns.disk == D_OUTDATED && os.disk < D_OUTDATED)
			ns.disk = os.disk;

		rv = __drbd_set_state(device, ns, flags, NULL);
		if (rv < SS_SUCCESS)
			BUG();

		ns.i = device->state.i;
		ns_max.role = max_role(ns.role, ns_max.role);
		ns_max.peer = max_role(ns.peer, ns_max.peer);
		ns_max.conn = max_t(enum drbd_conns, ns.conn, ns_max.conn);
		ns_max.disk = max_t(enum drbd_disk_state, ns.disk, ns_max.disk);
		ns_max.pdsk = max_t(enum drbd_disk_state, ns.pdsk, ns_max.pdsk);

		ns_min.role = min_role(ns.role, ns_min.role);
		ns_min.peer = min_role(ns.peer, ns_min.peer);
		ns_min.conn = min_t(enum drbd_conns, ns.conn, ns_min.conn);
		ns_min.disk = min_t(enum drbd_disk_state, ns.disk, ns_min.disk);
		ns_min.pdsk = min_t(enum drbd_disk_state, ns.pdsk, ns_min.pdsk);
	}
	rcu_read_unlock();

	if (number_of_volumes == 0) {
		ns_min = ns_max = (union drbd_state) { {
				.role = R_SECONDARY,
				.peer = R_UNKNOWN,
				.conn = val.conn,
				.disk = D_DISKLESS,
				.pdsk = D_UNKNOWN
			} };
	}

	ns_min.susp = ns_max.susp = connection->resource->susp;
	ns_min.susp_nod = ns_max.susp_nod = connection->resource->susp_nod;
	ns_min.susp_fen = ns_max.susp_fen = connection->resource->susp_fen;

	*pns_min = ns_min;
	*pns_max = ns_max;
}

static enum drbd_state_rv
_conn_rq_cond(struct drbd_connection *connection, union drbd_state mask, union drbd_state val)
{
	enum drbd_state_rv err, rv = SS_UNKNOWN_ERROR; /* continue waiting */;

	if (test_and_clear_bit(CONN_WD_ST_CHG_OKAY, &connection->flags))
		rv = SS_CW_SUCCESS;

	if (test_and_clear_bit(CONN_WD_ST_CHG_FAIL, &connection->flags))
		rv = SS_CW_FAILED_BY_PEER;

	err = conn_is_valid_transition(connection, mask, val, 0);
	if (err == SS_SUCCESS && connection->cstate == C_WF_REPORT_PARAMS)
		return rv;

	return err;
}

enum drbd_state_rv
_conn_request_state(struct drbd_connection *connection, union drbd_state mask, union drbd_state val,
		    enum chg_state_flags flags)
{
	enum drbd_state_rv rv = SS_SUCCESS;
	struct after_conn_state_chg_work *acscw;
	enum drbd_conns oc = connection->cstate;
	union drbd_state ns_max, ns_min, os;
	bool have_mutex = false;

	if (mask.conn) {
		rv = is_valid_conn_transition(oc, val.conn);
		if (rv < SS_SUCCESS)
			goto abort;
	}

	rv = conn_is_valid_transition(connection, mask, val, flags);
	if (rv < SS_SUCCESS)
		goto abort;

	if (oc == C_WF_REPORT_PARAMS && val.conn == C_DISCONNECTING &&
	    !(flags & (CS_LOCAL_ONLY | CS_HARD))) {

		/* This will be a cluster-wide state change.
		 * Need to give up the spinlock, grab the mutex,
		 * then send the state change request, ... */
		spin_unlock_irq(&connection->resource->req_lock);
		mutex_lock(&connection->cstate_mutex);
		have_mutex = true;

		set_bit(CONN_WD_ST_CHG_REQ, &connection->flags);
		if (conn_send_state_req(connection, mask, val)) {
			/* sending failed. */
			clear_bit(CONN_WD_ST_CHG_REQ, &connection->flags);
			rv = SS_CW_FAILED_BY_PEER;
			/* need to re-aquire the spin lock, though */
			goto abort_unlocked;
		}

		if (val.conn == C_DISCONNECTING)
			set_bit(DISCONNECT_SENT, &connection->flags);

		/* ... and re-aquire the spinlock.
		 * If _conn_rq_cond() returned >= SS_SUCCESS, we must call
		 * conn_set_state() within the same spinlock. */
		spin_lock_irq(&connection->resource->req_lock);
		wait_event_lock_irq(connection->ping_wait,
				(rv = _conn_rq_cond(connection, mask, val)),
				connection->resource->req_lock);
		clear_bit(CONN_WD_ST_CHG_REQ, &connection->flags);
		if (rv < SS_SUCCESS)
			goto abort;
	}

	conn_old_common_state(connection, &os, &flags);
	flags |= CS_DC_SUSP;
	conn_set_state(connection, mask, val, &ns_min, &ns_max, flags);
	conn_pr_state_change(connection, os, ns_max, flags);

	acscw = kmalloc(sizeof(*acscw), GFP_ATOMIC);
	if (acscw) {
		acscw->oc = os.conn;
		acscw->ns_min = ns_min;
		acscw->ns_max = ns_max;
		acscw->flags = flags;
		acscw->w.cb = w_after_conn_state_ch;
		kref_get(&connection->kref);
		acscw->connection = connection;
		drbd_queue_work(&connection->sender_work, &acscw->w);
	} else {
		drbd_err(connection, "Could not kmalloc an acscw\n");
	}

 abort:
	if (have_mutex) {
		/* mutex_unlock() "... must not be used in interrupt context.",
		 * so give up the spinlock, then re-aquire it */
		spin_unlock_irq(&connection->resource->req_lock);
 abort_unlocked:
		mutex_unlock(&connection->cstate_mutex);
		spin_lock_irq(&connection->resource->req_lock);
	}
	if (rv < SS_SUCCESS && flags & CS_VERBOSE) {
		drbd_err(connection, "State change failed: %s\n", drbd_set_st_err_str(rv));
		drbd_err(connection, " mask = 0x%x val = 0x%x\n", mask.i, val.i);
		drbd_err(connection, " old_conn:%s wanted_conn:%s\n", drbd_conn_str(oc), drbd_conn_str(val.conn));
	}
	return rv;
}

enum drbd_state_rv
conn_request_state(struct drbd_connection *connection, union drbd_state mask, union drbd_state val,
		   enum chg_state_flags flags)
{
	enum drbd_state_rv rv;

	spin_lock_irq(&connection->resource->req_lock);
	rv = _conn_request_state(connection, mask, val, flags);
	spin_unlock_irq(&connection->resource->req_lock);

	return rv;
}

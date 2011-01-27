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
#include "drbd_req.h"

struct after_state_chg_work {
	struct drbd_work w;
	union drbd_state os;
	union drbd_state ns;
	enum chg_state_flags flags;
	struct completion *done;
};


extern void _tl_restart(struct drbd_conf *mdev, enum drbd_req_event what);
int drbd_send_state_req(struct drbd_conf *, union drbd_state, union drbd_state);
static int w_after_state_ch(struct drbd_conf *mdev, struct drbd_work *w, int unused);
static void after_state_ch(struct drbd_conf *mdev, union drbd_state os,
			   union drbd_state ns, enum chg_state_flags flags);
static void after_conn_state_ch(struct drbd_tconn *tconn, union drbd_state os,
				union drbd_state ns, enum chg_state_flags flags);
static enum drbd_state_rv is_valid_state(struct drbd_conf *, union drbd_state);
static enum drbd_state_rv is_valid_soft_transition(union drbd_state, union drbd_state);
static union drbd_state sanitize_state(struct drbd_conf *mdev, union drbd_state os,
				       union drbd_state ns, const char **warn_sync_abort);

/**
 * cl_wide_st_chg() - true if the state change is a cluster wide one
 * @mdev:	DRBD device.
 * @os:		old (current) state.
 * @ns:		new (wanted) state.
 */
static int cl_wide_st_chg(struct drbd_conf *mdev,
			  union drbd_state os, union drbd_state ns)
{
	return (os.conn >= C_CONNECTED && ns.conn >= C_CONNECTED &&
		 ((os.role != R_PRIMARY && ns.role == R_PRIMARY) ||
		  (os.conn != C_STARTING_SYNC_T && ns.conn == C_STARTING_SYNC_T) ||
		  (os.conn != C_STARTING_SYNC_S && ns.conn == C_STARTING_SYNC_S) ||
		  (os.disk != D_DISKLESS && ns.disk == D_DISKLESS))) ||
		(os.conn >= C_CONNECTED && ns.conn == C_DISCONNECTING) ||
		(os.conn == C_CONNECTED && ns.conn == C_VERIFY_S);
}

enum drbd_state_rv
drbd_change_state(struct drbd_conf *mdev, enum chg_state_flags f,
		  union drbd_state mask, union drbd_state val)
{
	unsigned long flags;
	union drbd_state os, ns;
	enum drbd_state_rv rv;

	spin_lock_irqsave(&mdev->tconn->req_lock, flags);
	os = mdev->state;
	ns.i = (os.i & ~mask.i) | val.i;
	rv = _drbd_set_state(mdev, ns, f, NULL);
	ns = mdev->state;
	spin_unlock_irqrestore(&mdev->tconn->req_lock, flags);

	return rv;
}

/**
 * drbd_force_state() - Impose a change which happens outside our control on our state
 * @mdev:	DRBD device.
 * @mask:	mask of state bits to change.
 * @val:	value of new state bits.
 */
void drbd_force_state(struct drbd_conf *mdev,
	union drbd_state mask, union drbd_state val)
{
	drbd_change_state(mdev, CS_HARD, mask, val);
}

static enum drbd_state_rv
_req_st_cond(struct drbd_conf *mdev, union drbd_state mask,
	     union drbd_state val)
{
	union drbd_state os, ns;
	unsigned long flags;
	enum drbd_state_rv rv;

	if (test_and_clear_bit(CL_ST_CHG_SUCCESS, &mdev->flags))
		return SS_CW_SUCCESS;

	if (test_and_clear_bit(CL_ST_CHG_FAIL, &mdev->flags))
		return SS_CW_FAILED_BY_PEER;

	rv = 0;
	spin_lock_irqsave(&mdev->tconn->req_lock, flags);
	os = mdev->state;
	ns.i = (os.i & ~mask.i) | val.i;
	ns = sanitize_state(mdev, os, ns, NULL);

	if (!cl_wide_st_chg(mdev, os, ns))
		rv = SS_CW_NO_NEED;
	if (!rv) {
		rv = is_valid_state(mdev, ns);
		if (rv == SS_SUCCESS) {
			rv = is_valid_soft_transition(os, ns);
			if (rv == SS_SUCCESS)
				rv = SS_UNKNOWN_ERROR; /* cont waiting, otherwise fail. */
		}
	}
	spin_unlock_irqrestore(&mdev->tconn->req_lock, flags);

	return rv;
}

/**
 * drbd_req_state() - Perform an eventually cluster wide state change
 * @mdev:	DRBD device.
 * @mask:	mask of state bits to change.
 * @val:	value of new state bits.
 * @f:		flags
 *
 * Should not be called directly, use drbd_request_state() or
 * _drbd_request_state().
 */
static enum drbd_state_rv
drbd_req_state(struct drbd_conf *mdev, union drbd_state mask,
	       union drbd_state val, enum chg_state_flags f)
{
	struct completion done;
	unsigned long flags;
	union drbd_state os, ns;
	enum drbd_state_rv rv;

	init_completion(&done);

	if (f & CS_SERIALIZE)
		mutex_lock(&mdev->state_mutex);

	spin_lock_irqsave(&mdev->tconn->req_lock, flags);
	os = mdev->state;
	ns.i = (os.i & ~mask.i) | val.i;

	ns = sanitize_state(mdev, os, ns, NULL);

	if (cl_wide_st_chg(mdev, os, ns)) {
		rv = is_valid_state(mdev, ns);
		if (rv == SS_SUCCESS)
			rv = is_valid_soft_transition(os, ns);
		spin_unlock_irqrestore(&mdev->tconn->req_lock, flags);

		if (rv < SS_SUCCESS) {
			if (f & CS_VERBOSE)
				print_st_err(mdev, os, ns, rv);
			goto abort;
		}

		drbd_state_lock(mdev);
		if (!drbd_send_state_req(mdev, mask, val)) {
			drbd_state_unlock(mdev);
			rv = SS_CW_FAILED_BY_PEER;
			if (f & CS_VERBOSE)
				print_st_err(mdev, os, ns, rv);
			goto abort;
		}

		wait_event(mdev->state_wait,
			(rv = _req_st_cond(mdev, mask, val)));

		if (rv < SS_SUCCESS) {
			drbd_state_unlock(mdev);
			if (f & CS_VERBOSE)
				print_st_err(mdev, os, ns, rv);
			goto abort;
		}
		spin_lock_irqsave(&mdev->tconn->req_lock, flags);
		os = mdev->state;
		ns.i = (os.i & ~mask.i) | val.i;
		rv = _drbd_set_state(mdev, ns, f, &done);
		drbd_state_unlock(mdev);
	} else {
		rv = _drbd_set_state(mdev, ns, f, &done);
	}

	spin_unlock_irqrestore(&mdev->tconn->req_lock, flags);

	if (f & CS_WAIT_COMPLETE && rv == SS_SUCCESS) {
		D_ASSERT(current != mdev->tconn->worker.task);
		wait_for_completion(&done);
	}

abort:
	if (f & CS_SERIALIZE)
		mutex_unlock(&mdev->state_mutex);

	return rv;
}

/**
 * _drbd_request_state() - Request a state change (with flags)
 * @mdev:	DRBD device.
 * @mask:	mask of state bits to change.
 * @val:	value of new state bits.
 * @f:		flags
 *
 * Cousin of drbd_request_state(), useful with the CS_WAIT_COMPLETE
 * flag, or when logging of failed state change requests is not desired.
 */
enum drbd_state_rv
_drbd_request_state(struct drbd_conf *mdev, union drbd_state mask,
		    union drbd_state val, enum chg_state_flags f)
{
	enum drbd_state_rv rv;

	wait_event(mdev->state_wait,
		   (rv = drbd_req_state(mdev, mask, val, f)) != SS_IN_TRANSIENT_STATE);

	return rv;
}

static void print_st(struct drbd_conf *mdev, char *name, union drbd_state ns)
{
	dev_err(DEV, " %s = { cs:%s ro:%s/%s ds:%s/%s %c%c%c%c%c%c }\n",
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

void print_st_err(struct drbd_conf *mdev, union drbd_state os,
	          union drbd_state ns, enum drbd_state_rv err)
{
	if (err == SS_IN_TRANSIENT_STATE)
		return;
	dev_err(DEV, "State change failed: %s\n", drbd_set_st_err_str(err));
	print_st(mdev, " state", os);
	print_st(mdev, "wanted", ns);
}


/**
 * is_valid_state() - Returns an SS_ error code if ns is not valid
 * @mdev:	DRBD device.
 * @ns:		State to consider.
 */
static enum drbd_state_rv
is_valid_state(struct drbd_conf *mdev, union drbd_state ns)
{
	/* See drbd_state_sw_errors in drbd_strings.c */

	enum drbd_fencing_p fp;
	enum drbd_state_rv rv = SS_SUCCESS;

	fp = FP_DONT_CARE;
	if (get_ldev(mdev)) {
		fp = mdev->ldev->dc.fencing;
		put_ldev(mdev);
	}

	if (get_net_conf(mdev->tconn)) {
		if (!mdev->tconn->net_conf->two_primaries &&
		    ns.role == R_PRIMARY && ns.peer == R_PRIMARY)
			rv = SS_TWO_PRIMARIES;
		put_net_conf(mdev->tconn);
	}

	if (rv <= 0)
		/* already found a reason to abort */;
	else if (ns.role == R_SECONDARY && mdev->open_cnt)
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
		 (mdev->sync_conf.verify_alg[0] == 0))
		rv = SS_NO_VERIFY_ALG;

	else if ((ns.conn == C_VERIFY_S || ns.conn == C_VERIFY_T) &&
		  mdev->tconn->agreed_pro_version < 88)
		rv = SS_NOT_SUPPORTED;

	else if (ns.conn >= C_CONNECTED && ns.pdsk == D_UNKNOWN)
		rv = SS_CONNECTED_OUTDATES;

	return rv;
}

/**
 * is_valid_soft_transition() - Returns an SS_ error code if the state transition is not possible
 * @mdev:	DRBD device.
 * @ns:		new state.
 * @os:		old state.
 */
static enum drbd_state_rv
is_valid_soft_transition(union drbd_state os, union drbd_state ns)
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

	if (ns.conn == os.conn && ns.conn == C_WF_REPORT_PARAMS)
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

	return rv;
}

/**
 * sanitize_state() - Resolves implicitly necessary additional changes to a state transition
 * @mdev:	DRBD device.
 * @os:		old state.
 * @ns:		new state.
 * @warn_sync_abort:
 *
 * When we loose connection, we have to set the state of the peers disk (pdsk)
 * to D_UNKNOWN. This rule and many more along those lines are in this function.
 */
static union drbd_state sanitize_state(struct drbd_conf *mdev, union drbd_state os,
				       union drbd_state ns, const char **warn_sync_abort)
{
	enum drbd_fencing_p fp;
	enum drbd_disk_state disk_min, disk_max, pdsk_min, pdsk_max;

	fp = FP_DONT_CARE;
	if (get_ldev(mdev)) {
		fp = mdev->ldev->dc.fencing;
		put_ldev(mdev);
	}

	/* Disallow Network errors to configure a device's network part */
	if ((ns.conn >= C_TIMEOUT && ns.conn <= C_TEAR_DOWN) &&
	    os.conn <= C_DISCONNECTING)
		ns.conn = os.conn;

	/* After a network error (+C_TEAR_DOWN) only C_UNCONNECTED or C_DISCONNECTING can follow.
	 * If you try to go into some Sync* state, that shall fail (elsewhere). */
	if (os.conn >= C_TIMEOUT && os.conn <= C_TEAR_DOWN &&
	    ns.conn != C_UNCONNECTED && ns.conn != C_DISCONNECTING && ns.conn <= C_TEAR_DOWN)
		ns.conn = os.conn;

	/* we cannot fail (again) if we already detached */
	if (ns.disk == D_FAILED && os.disk == D_DISKLESS)
		ns.disk = D_DISKLESS;

	/* if we are only D_ATTACHING yet,
	 * we can (and should) go directly to D_DISKLESS. */
	if (ns.disk == D_FAILED && os.disk == D_ATTACHING)
		ns.disk = D_DISKLESS;

	/* After C_DISCONNECTING only C_STANDALONE may follow */
	if (os.conn == C_DISCONNECTING && ns.conn != C_STANDALONE)
		ns.conn = os.conn;

	if (ns.conn < C_CONNECTED) {
		ns.peer_isp = 0;
		ns.peer = R_UNKNOWN;
		if (ns.pdsk > D_UNKNOWN || ns.pdsk < D_INCONSISTENT)
			ns.pdsk = D_UNKNOWN;
	}

	/* Clear the aftr_isp when becoming unconfigured */
	if (ns.conn == C_STANDALONE && ns.disk == D_DISKLESS && ns.role == R_SECONDARY)
		ns.aftr_isp = 0;

	/* Abort resync if a disk fails/detaches */
	if (os.conn > C_CONNECTED && ns.conn > C_CONNECTED &&
	    (ns.disk <= D_FAILED || ns.pdsk <= D_FAILED)) {
		if (warn_sync_abort)
			*warn_sync_abort =
				os.conn == C_VERIFY_S || os.conn == C_VERIFY_T ?
				"Online-verify" : "Resync";
		ns.conn = C_CONNECTED;
	}

	/* Connection breaks down before we finished "Negotiating" */
	if (ns.conn < C_CONNECTED && ns.disk == D_NEGOTIATING &&
	    get_ldev_if_state(mdev, D_NEGOTIATING)) {
		if (mdev->ed_uuid == mdev->ldev->md.uuid[UI_CURRENT]) {
			ns.disk = mdev->new_state_tmp.disk;
			ns.pdsk = mdev->new_state_tmp.pdsk;
		} else {
			dev_alert(DEV, "Connection lost while negotiating, no data!\n");
			ns.disk = D_DISKLESS;
			ns.pdsk = D_UNKNOWN;
		}
		put_ldev(mdev);
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
		dev_warn(DEV, "Implicitly set disk from %s to %s\n",
			 drbd_disk_str(ns.disk), drbd_disk_str(disk_min));
		ns.disk = disk_min;
	}
	if (ns.pdsk > pdsk_max)
		ns.pdsk = pdsk_max;

	if (ns.pdsk < pdsk_min) {
		dev_warn(DEV, "Implicitly set pdsk from %s to %s\n",
			 drbd_disk_str(ns.pdsk), drbd_disk_str(pdsk_min));
		ns.pdsk = pdsk_min;
	}

	if (fp == FP_STONITH &&
	    (ns.role == R_PRIMARY && ns.conn < C_CONNECTED && ns.pdsk > D_OUTDATED) &&
	    !(os.role == R_PRIMARY && os.conn < C_CONNECTED && os.pdsk > D_OUTDATED))
		ns.susp_fen = 1; /* Suspend IO while fence-peer handler runs (peer lost) */

	if (mdev->sync_conf.on_no_data == OND_SUSPEND_IO &&
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

void drbd_resume_al(struct drbd_conf *mdev)
{
	if (test_and_clear_bit(AL_SUSPENDED, &mdev->flags))
		dev_info(DEV, "Resumed AL updates\n");
}

/* helper for __drbd_set_state */
static void set_ov_position(struct drbd_conf *mdev, enum drbd_conns cs)
{
	if (mdev->tconn->agreed_pro_version < 90)
		mdev->ov_start_sector = 0;
	mdev->rs_total = drbd_bm_bits(mdev);
	mdev->ov_position = 0;
	if (cs == C_VERIFY_T) {
		/* starting online verify from an arbitrary position
		 * does not fit well into the existing protocol.
		 * on C_VERIFY_T, we initialize ov_left and friends
		 * implicitly in receive_DataRequest once the
		 * first P_OV_REQUEST is received */
		mdev->ov_start_sector = ~(sector_t)0;
	} else {
		unsigned long bit = BM_SECT_TO_BIT(mdev->ov_start_sector);
		if (bit >= mdev->rs_total) {
			mdev->ov_start_sector =
				BM_BIT_TO_SECT(mdev->rs_total - 1);
			mdev->rs_total = 1;
		} else
			mdev->rs_total -= bit;
		mdev->ov_position = mdev->ov_start_sector;
	}
	mdev->ov_left = mdev->rs_total;
}

/**
 * __drbd_set_state() - Set a new DRBD state
 * @mdev:	DRBD device.
 * @ns:		new state.
 * @flags:	Flags
 * @done:	Optional completion, that will get completed after the after_state_ch() finished
 *
 * Caller needs to hold req_lock, and global_state_lock. Do not call directly.
 */
enum drbd_state_rv
__drbd_set_state(struct drbd_conf *mdev, union drbd_state ns,
	         enum chg_state_flags flags, struct completion *done)
{
	union drbd_state os;
	enum drbd_state_rv rv = SS_SUCCESS;
	const char *warn_sync_abort = NULL;
	struct after_state_chg_work *ascw;

	os = mdev->state;

	ns = sanitize_state(mdev, os, ns, &warn_sync_abort);

	if (ns.i == os.i)
		return SS_NOTHING_TO_DO;

	if (!(flags & CS_HARD)) {
		/*  pre-state-change checks ; only look at ns  */
		/* See drbd_state_sw_errors in drbd_strings.c */

		rv = is_valid_state(mdev, ns);
		if (rv < SS_SUCCESS) {
			/* If the old state was illegal as well, then let
			   this happen...*/

			if (is_valid_state(mdev, os) == rv)
				rv = is_valid_soft_transition(os, ns);
		} else
			rv = is_valid_soft_transition(os, ns);
	}

	if (rv < SS_SUCCESS) {
		if (flags & CS_VERBOSE)
			print_st_err(mdev, os, ns, rv);
		return rv;
	}

	if (warn_sync_abort)
		dev_warn(DEV, "%s aborted.\n", warn_sync_abort);

	{
	char *pbp, pb[300];
	pbp = pb;
	*pbp = 0;
	if (ns.role != os.role)
		pbp += sprintf(pbp, "role( %s -> %s ) ",
			       drbd_role_str(os.role),
			       drbd_role_str(ns.role));
	if (ns.peer != os.peer)
		pbp += sprintf(pbp, "peer( %s -> %s ) ",
			       drbd_role_str(os.peer),
			       drbd_role_str(ns.peer));
	if (ns.conn != os.conn)
		pbp += sprintf(pbp, "conn( %s -> %s ) ",
			       drbd_conn_str(os.conn),
			       drbd_conn_str(ns.conn));
	if (ns.disk != os.disk)
		pbp += sprintf(pbp, "disk( %s -> %s ) ",
			       drbd_disk_str(os.disk),
			       drbd_disk_str(ns.disk));
	if (ns.pdsk != os.pdsk)
		pbp += sprintf(pbp, "pdsk( %s -> %s ) ",
			       drbd_disk_str(os.pdsk),
			       drbd_disk_str(ns.pdsk));
	if (is_susp(ns) != is_susp(os))
		pbp += sprintf(pbp, "susp( %d -> %d ) ",
			       is_susp(os),
			       is_susp(ns));
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
	dev_info(DEV, "%s\n", pb);
	}

	/* solve the race between becoming unconfigured,
	 * worker doing the cleanup, and
	 * admin reconfiguring us:
	 * on (re)configure, first set CONFIG_PENDING,
	 * then wait for a potentially exiting worker,
	 * start the worker, and schedule one no_op.
	 * then proceed with configuration.
	 */
	if (ns.disk == D_DISKLESS &&
	    ns.conn == C_STANDALONE &&
	    ns.role == R_SECONDARY &&
	    !test_and_set_bit(CONFIG_PENDING, &mdev->flags))
		set_bit(DEVICE_DYING, &mdev->flags);

	/* if we are going -> D_FAILED or D_DISKLESS, grab one extra reference
	 * on the ldev here, to be sure the transition -> D_DISKLESS resp.
	 * drbd_ldev_destroy() won't happen before our corresponding
	 * after_state_ch works run, where we put_ldev again. */
	if ((os.disk != D_FAILED && ns.disk == D_FAILED) ||
	    (os.disk != D_DISKLESS && ns.disk == D_DISKLESS))
		atomic_inc(&mdev->local_cnt);

	mdev->state = ns;

	if (os.disk == D_ATTACHING && ns.disk >= D_NEGOTIATING)
		drbd_print_uuids(mdev, "attached to UUIDs");

	wake_up(&mdev->misc_wait);
	wake_up(&mdev->state_wait);

	/* aborted verify run. log the last position */
	if ((os.conn == C_VERIFY_S || os.conn == C_VERIFY_T) &&
	    ns.conn < C_CONNECTED) {
		mdev->ov_start_sector =
			BM_BIT_TO_SECT(drbd_bm_bits(mdev) - mdev->ov_left);
		dev_info(DEV, "Online Verify reached sector %llu\n",
			(unsigned long long)mdev->ov_start_sector);
	}

	if ((os.conn == C_PAUSED_SYNC_T || os.conn == C_PAUSED_SYNC_S) &&
	    (ns.conn == C_SYNC_TARGET  || ns.conn == C_SYNC_SOURCE)) {
		dev_info(DEV, "Syncer continues.\n");
		mdev->rs_paused += (long)jiffies
				  -(long)mdev->rs_mark_time[mdev->rs_last_mark];
		if (ns.conn == C_SYNC_TARGET)
			mod_timer(&mdev->resync_timer, jiffies);
	}

	if ((os.conn == C_SYNC_TARGET  || os.conn == C_SYNC_SOURCE) &&
	    (ns.conn == C_PAUSED_SYNC_T || ns.conn == C_PAUSED_SYNC_S)) {
		dev_info(DEV, "Resync suspended\n");
		mdev->rs_mark_time[mdev->rs_last_mark] = jiffies;
	}

	if (os.conn == C_CONNECTED &&
	    (ns.conn == C_VERIFY_S || ns.conn == C_VERIFY_T)) {
		unsigned long now = jiffies;
		int i;

		set_ov_position(mdev, ns.conn);
		mdev->rs_start = now;
		mdev->rs_last_events = 0;
		mdev->rs_last_sect_ev = 0;
		mdev->ov_last_oos_size = 0;
		mdev->ov_last_oos_start = 0;

		for (i = 0; i < DRBD_SYNC_MARKS; i++) {
			mdev->rs_mark_left[i] = mdev->ov_left;
			mdev->rs_mark_time[i] = now;
		}

		drbd_rs_controller_reset(mdev);

		if (ns.conn == C_VERIFY_S) {
			dev_info(DEV, "Starting Online Verify from sector %llu\n",
					(unsigned long long)mdev->ov_position);
			mod_timer(&mdev->resync_timer, jiffies);
		}
	}

	if (get_ldev(mdev)) {
		u32 mdf = mdev->ldev->md.flags & ~(MDF_CONSISTENT|MDF_PRIMARY_IND|
						 MDF_CONNECTED_IND|MDF_WAS_UP_TO_DATE|
						 MDF_PEER_OUT_DATED|MDF_CRASHED_PRIMARY);

		if (test_bit(CRASHED_PRIMARY, &mdev->flags))
			mdf |= MDF_CRASHED_PRIMARY;
		if (mdev->state.role == R_PRIMARY ||
		    (mdev->state.pdsk < D_INCONSISTENT && mdev->state.peer == R_PRIMARY))
			mdf |= MDF_PRIMARY_IND;
		if (mdev->state.conn > C_WF_REPORT_PARAMS)
			mdf |= MDF_CONNECTED_IND;
		if (mdev->state.disk > D_INCONSISTENT)
			mdf |= MDF_CONSISTENT;
		if (mdev->state.disk > D_OUTDATED)
			mdf |= MDF_WAS_UP_TO_DATE;
		if (mdev->state.pdsk <= D_OUTDATED && mdev->state.pdsk >= D_INCONSISTENT)
			mdf |= MDF_PEER_OUT_DATED;
		if (mdf != mdev->ldev->md.flags) {
			mdev->ldev->md.flags = mdf;
			drbd_md_mark_dirty(mdev);
		}
		if (os.disk < D_CONSISTENT && ns.disk >= D_CONSISTENT)
			drbd_set_ed_uuid(mdev, mdev->ldev->md.uuid[UI_CURRENT]);
		put_ldev(mdev);
	}

	/* Peer was forced D_UP_TO_DATE & R_PRIMARY, consider to resync */
	if (os.disk == D_INCONSISTENT && os.pdsk == D_INCONSISTENT &&
	    os.peer == R_SECONDARY && ns.peer == R_PRIMARY)
		set_bit(CONSIDER_RESYNC, &mdev->flags);

	/* Receiver should clean up itself */
	if (os.conn != C_DISCONNECTING && ns.conn == C_DISCONNECTING)
		drbd_thread_stop_nowait(&mdev->tconn->receiver);

	/* Now the receiver finished cleaning up itself, it should die */
	if (os.conn != C_STANDALONE && ns.conn == C_STANDALONE)
		drbd_thread_stop_nowait(&mdev->tconn->receiver);

	/* Upon network failure, we need to restart the receiver. */
	if (os.conn > C_TEAR_DOWN &&
	    ns.conn <= C_TEAR_DOWN && ns.conn >= C_TIMEOUT)
		drbd_thread_restart_nowait(&mdev->tconn->receiver);

	/* Resume AL writing if we get a connection */
	if (os.conn < C_CONNECTED && ns.conn >= C_CONNECTED)
		drbd_resume_al(mdev);

	ascw = kmalloc(sizeof(*ascw), GFP_ATOMIC);
	if (ascw) {
		ascw->os = os;
		ascw->ns = ns;
		ascw->flags = flags;
		ascw->w.cb = w_after_state_ch;
		ascw->done = done;
		drbd_queue_work(&mdev->tconn->data.work, &ascw->w);
	} else {
		dev_warn(DEV, "Could not kmalloc an ascw\n");
	}

	return rv;
}

static int w_after_state_ch(struct drbd_conf *mdev, struct drbd_work *w, int unused)
{
	struct after_state_chg_work *ascw =
		container_of(w, struct after_state_chg_work, w);

	after_state_ch(mdev, ascw->os, ascw->ns, ascw->flags);
	if (ascw->flags & CS_WAIT_COMPLETE) {
		D_ASSERT(ascw->done != NULL);
		complete(ascw->done);
	}
	kfree(ascw);

	return 1;
}

static void abw_start_sync(struct drbd_conf *mdev, int rv)
{
	if (rv) {
		dev_err(DEV, "Writing the bitmap failed not starting resync.\n");
		_drbd_request_state(mdev, NS(conn, C_CONNECTED), CS_VERBOSE);
		return;
	}

	switch (mdev->state.conn) {
	case C_STARTING_SYNC_T:
		_drbd_request_state(mdev, NS(conn, C_WF_SYNC_UUID), CS_VERBOSE);
		break;
	case C_STARTING_SYNC_S:
		drbd_start_resync(mdev, C_SYNC_SOURCE);
		break;
	}
}

int drbd_bitmap_io_from_worker(struct drbd_conf *mdev,
		int (*io_fn)(struct drbd_conf *),
		char *why, enum bm_flag flags)
{
	int rv;

	D_ASSERT(current == mdev->tconn->worker.task);

	/* open coded non-blocking drbd_suspend_io(mdev); */
	set_bit(SUSPEND_IO, &mdev->flags);

	drbd_bm_lock(mdev, why, flags);
	rv = io_fn(mdev);
	drbd_bm_unlock(mdev);

	drbd_resume_io(mdev);

	return rv;
}

/**
 * after_state_ch() - Perform after state change actions that may sleep
 * @mdev:	DRBD device.
 * @os:		old state.
 * @ns:		new state.
 * @flags:	Flags
 */
static void after_state_ch(struct drbd_conf *mdev, union drbd_state os,
			   union drbd_state ns, enum chg_state_flags flags)
{
	enum drbd_fencing_p fp;
	enum drbd_req_event what = NOTHING;
	union drbd_state nsm = (union drbd_state){ .i = -1 };

	if (os.conn != C_CONNECTED && ns.conn == C_CONNECTED) {
		clear_bit(CRASHED_PRIMARY, &mdev->flags);
		if (mdev->p_uuid)
			mdev->p_uuid[UI_FLAGS] &= ~((u64)2);
	}

	fp = FP_DONT_CARE;
	if (get_ldev(mdev)) {
		fp = mdev->ldev->dc.fencing;
		put_ldev(mdev);
	}

	/* Inform userspace about the change... */
	drbd_bcast_state(mdev, ns);

	if (!(os.role == R_PRIMARY && os.disk < D_UP_TO_DATE && os.pdsk < D_UP_TO_DATE) &&
	    (ns.role == R_PRIMARY && ns.disk < D_UP_TO_DATE && ns.pdsk < D_UP_TO_DATE))
		drbd_khelper(mdev, "pri-on-incon-degr");

	/* Here we have the actions that are performed after a
	   state change. This function might sleep */

	nsm.i = -1;
	if (ns.susp_nod) {
		if (os.conn < C_CONNECTED && ns.conn >= C_CONNECTED)
			what = RESEND;

		if (os.disk == D_ATTACHING && ns.disk > D_ATTACHING)
			what = RESTART_FROZEN_DISK_IO;

		if (what != NOTHING)
			nsm.susp_nod = 0;
	}

	if (ns.susp_fen) {
		/* case1: The outdate peer handler is successful: */
		if (os.pdsk > D_OUTDATED  && ns.pdsk <= D_OUTDATED) {
			tl_clear(mdev);
			if (test_bit(NEW_CUR_UUID, &mdev->flags)) {
				drbd_uuid_new_current(mdev);
				clear_bit(NEW_CUR_UUID, &mdev->flags);
			}
			spin_lock_irq(&mdev->tconn->req_lock);
			_drbd_set_state(_NS(mdev, susp_fen, 0), CS_VERBOSE, NULL);
			spin_unlock_irq(&mdev->tconn->req_lock);
		}
		/* case2: The connection was established again: */
		if (os.conn < C_CONNECTED && ns.conn >= C_CONNECTED) {
			clear_bit(NEW_CUR_UUID, &mdev->flags);
			what = RESEND;
			nsm.susp_fen = 0;
		}
	}

	if (what != NOTHING) {
		spin_lock_irq(&mdev->tconn->req_lock);
		_tl_restart(mdev, what);
		nsm.i &= mdev->state.i;
		_drbd_set_state(mdev, nsm, CS_VERBOSE, NULL);
		spin_unlock_irq(&mdev->tconn->req_lock);
	}

	/* Became sync source.  With protocol >= 96, we still need to send out
	 * the sync uuid now. Need to do that before any drbd_send_state, or
	 * the other side may go "paused sync" before receiving the sync uuids,
	 * which is unexpected. */
	if ((os.conn != C_SYNC_SOURCE && os.conn != C_PAUSED_SYNC_S) &&
	    (ns.conn == C_SYNC_SOURCE || ns.conn == C_PAUSED_SYNC_S) &&
	    mdev->tconn->agreed_pro_version >= 96 && get_ldev(mdev)) {
		drbd_gen_and_send_sync_uuid(mdev);
		put_ldev(mdev);
	}

	/* Do not change the order of the if above and the two below... */
	if (os.pdsk == D_DISKLESS && ns.pdsk > D_DISKLESS) {      /* attach on the peer */
		drbd_send_uuids(mdev);
		drbd_send_state(mdev);
	}
	/* No point in queuing send_bitmap if we don't have a connection
	 * anymore, so check also the _current_ state, not only the new state
	 * at the time this work was queued. */
	if (os.conn != C_WF_BITMAP_S && ns.conn == C_WF_BITMAP_S &&
	    mdev->state.conn == C_WF_BITMAP_S)
		drbd_queue_bitmap_io(mdev, &drbd_send_bitmap, NULL,
				"send_bitmap (WFBitMapS)",
				BM_LOCKED_TEST_ALLOWED);

	/* Lost contact to peer's copy of the data */
	if ((os.pdsk >= D_INCONSISTENT &&
	     os.pdsk != D_UNKNOWN &&
	     os.pdsk != D_OUTDATED)
	&&  (ns.pdsk < D_INCONSISTENT ||
	     ns.pdsk == D_UNKNOWN ||
	     ns.pdsk == D_OUTDATED)) {
		if (get_ldev(mdev)) {
			if ((ns.role == R_PRIMARY || ns.peer == R_PRIMARY) &&
			    mdev->ldev->md.uuid[UI_BITMAP] == 0 && ns.disk >= D_UP_TO_DATE) {
				if (is_susp(mdev->state)) {
					set_bit(NEW_CUR_UUID, &mdev->flags);
				} else {
					drbd_uuid_new_current(mdev);
					drbd_send_uuids(mdev);
				}
			}
			put_ldev(mdev);
		}
	}

	if (ns.pdsk < D_INCONSISTENT && get_ldev(mdev)) {
		if (ns.peer == R_PRIMARY && mdev->ldev->md.uuid[UI_BITMAP] == 0) {
			drbd_uuid_new_current(mdev);
			drbd_send_uuids(mdev);
		}

		/* D_DISKLESS Peer becomes secondary */
		if (os.peer == R_PRIMARY && ns.peer == R_SECONDARY)
			/* We may still be Primary ourselves.
			 * No harm done if the bitmap still changes,
			 * redirtied pages will follow later. */
			drbd_bitmap_io_from_worker(mdev, &drbd_bm_write,
				"demote diskless peer", BM_LOCKED_SET_ALLOWED);
		put_ldev(mdev);
	}

	/* Write out all changed bits on demote.
	 * Though, no need to da that just yet
	 * if there is a resync going on still */
	if (os.role == R_PRIMARY && ns.role == R_SECONDARY &&
		mdev->state.conn <= C_CONNECTED && get_ldev(mdev)) {
		/* No changes to the bitmap expected this time, so assert that,
		 * even though no harm was done if it did change. */
		drbd_bitmap_io_from_worker(mdev, &drbd_bm_write,
				"demote", BM_LOCKED_TEST_ALLOWED);
		put_ldev(mdev);
	}

	/* Last part of the attaching process ... */
	if (ns.conn >= C_CONNECTED &&
	    os.disk == D_ATTACHING && ns.disk == D_NEGOTIATING) {
		drbd_send_sizes(mdev, 0, 0);  /* to start sync... */
		drbd_send_uuids(mdev);
		drbd_send_state(mdev);
	}

	/* We want to pause/continue resync, tell peer. */
	if (ns.conn >= C_CONNECTED &&
	     ((os.aftr_isp != ns.aftr_isp) ||
	      (os.user_isp != ns.user_isp)))
		drbd_send_state(mdev);

	/* In case one of the isp bits got set, suspend other devices. */
	if ((!os.aftr_isp && !os.peer_isp && !os.user_isp) &&
	    (ns.aftr_isp || ns.peer_isp || ns.user_isp))
		suspend_other_sg(mdev);

	/* Make sure the peer gets informed about eventual state
	   changes (ISP bits) while we were in WFReportParams. */
	if (os.conn == C_WF_REPORT_PARAMS && ns.conn >= C_CONNECTED)
		drbd_send_state(mdev);

	if (os.conn != C_AHEAD && ns.conn == C_AHEAD)
		drbd_send_state(mdev);

	/* We are in the progress to start a full sync... */
	if ((os.conn != C_STARTING_SYNC_T && ns.conn == C_STARTING_SYNC_T) ||
	    (os.conn != C_STARTING_SYNC_S && ns.conn == C_STARTING_SYNC_S))
		/* no other bitmap changes expected during this phase */
		drbd_queue_bitmap_io(mdev,
			&drbd_bmio_set_n_write, &abw_start_sync,
			"set_n_write from StartingSync", BM_LOCKED_TEST_ALLOWED);

	/* We are invalidating our self... */
	if (os.conn < C_CONNECTED && ns.conn < C_CONNECTED &&
	    os.disk > D_INCONSISTENT && ns.disk == D_INCONSISTENT)
		/* other bitmap operation expected during this phase */
		drbd_queue_bitmap_io(mdev, &drbd_bmio_set_n_write, NULL,
			"set_n_write from invalidate", BM_LOCKED_MASK);

	/* first half of local IO error, failure to attach,
	 * or administrative detach */
	if (os.disk != D_FAILED && ns.disk == D_FAILED) {
		enum drbd_io_error_p eh;
		int was_io_error;
		/* corresponding get_ldev was in __drbd_set_state, to serialize
		 * our cleanup here with the transition to D_DISKLESS,
		 * so it is safe to dreference ldev here. */
		eh = mdev->ldev->dc.on_io_error;
		was_io_error = test_and_clear_bit(WAS_IO_ERROR, &mdev->flags);

		/* current state still has to be D_FAILED,
		 * there is only one way out: to D_DISKLESS,
		 * and that may only happen after our put_ldev below. */
		if (mdev->state.disk != D_FAILED)
			dev_err(DEV,
				"ASSERT FAILED: disk is %s during detach\n",
				drbd_disk_str(mdev->state.disk));

		if (drbd_send_state(mdev))
			dev_warn(DEV, "Notified peer that I am detaching my disk\n");
		else
			dev_err(DEV, "Sending state for detaching disk failed\n");

		drbd_rs_cancel_all(mdev);

		/* In case we want to get something to stable storage still,
		 * this may be the last chance.
		 * Following put_ldev may transition to D_DISKLESS. */
		drbd_md_sync(mdev);
		put_ldev(mdev);

		if (was_io_error && eh == EP_CALL_HELPER)
			drbd_khelper(mdev, "local-io-error");
	}

        /* second half of local IO error, failure to attach,
         * or administrative detach,
         * after local_cnt references have reached zero again */
        if (os.disk != D_DISKLESS && ns.disk == D_DISKLESS) {
                /* We must still be diskless,
                 * re-attach has to be serialized with this! */
                if (mdev->state.disk != D_DISKLESS)
                        dev_err(DEV,
                                "ASSERT FAILED: disk is %s while going diskless\n",
                                drbd_disk_str(mdev->state.disk));

                mdev->rs_total = 0;
                mdev->rs_failed = 0;
                atomic_set(&mdev->rs_pending_cnt, 0);

		if (drbd_send_state(mdev))
			dev_warn(DEV, "Notified peer that I'm now diskless.\n");
		/* corresponding get_ldev in __drbd_set_state
		 * this may finally trigger drbd_ldev_destroy. */
		put_ldev(mdev);
	}

	/* Notify peer that I had a local IO error, and did not detached.. */
	if (os.disk == D_UP_TO_DATE && ns.disk == D_INCONSISTENT)
		drbd_send_state(mdev);

	/* Disks got bigger while they were detached */
	if (ns.disk > D_NEGOTIATING && ns.pdsk > D_NEGOTIATING &&
	    test_and_clear_bit(RESYNC_AFTER_NEG, &mdev->flags)) {
		if (ns.conn == C_CONNECTED)
			resync_after_online_grow(mdev);
	}

	/* A resync finished or aborted, wake paused devices... */
	if ((os.conn > C_CONNECTED && ns.conn <= C_CONNECTED) ||
	    (os.peer_isp && !ns.peer_isp) ||
	    (os.user_isp && !ns.user_isp))
		resume_next_sg(mdev);

	/* sync target done with resync.  Explicitly notify peer, even though
	 * it should (at least for non-empty resyncs) already know itself. */
	if (os.disk < D_UP_TO_DATE && os.conn >= C_SYNC_SOURCE && ns.conn == C_CONNECTED)
		drbd_send_state(mdev);

	/* This triggers bitmap writeout of potentially still unwritten pages
	 * if the resync finished cleanly, or aborted because of peer disk
	 * failure, or because of connection loss.
	 * For resync aborted because of local disk failure, we cannot do
	 * any bitmap writeout anymore.
	 * No harm done if some bits change during this phase.
	 */
	if (os.conn > C_CONNECTED && ns.conn <= C_CONNECTED && get_ldev(mdev)) {
		drbd_queue_bitmap_io(mdev, &drbd_bm_write, NULL,
			"write from resync_finished", BM_LOCKED_SET_ALLOWED);
		put_ldev(mdev);
	}

	if (ns.disk == D_DISKLESS &&
	    ns.conn == C_STANDALONE &&
	    ns.role == R_SECONDARY) {
		if (os.aftr_isp != ns.aftr_isp)
			resume_next_sg(mdev);
	}

	after_conn_state_ch(mdev->tconn, os, ns, flags);
	drbd_md_sync(mdev);
}

static void after_conn_state_ch(struct drbd_tconn *tconn, union drbd_state os,
				union drbd_state ns, enum chg_state_flags flags)
{
	/* Upon network configuration, we need to start the receiver */
	if (os.conn == C_STANDALONE && ns.conn == C_UNCONNECTED)
		drbd_thread_start(&tconn->receiver);

	if (ns.disk == D_DISKLESS &&
	    ns.conn == C_STANDALONE &&
	    ns.role == R_SECONDARY) {
		/* if (test_bit(DEVICE_DYING, &mdev->flags)) TODO: DEVICE_DYING functionality */
		drbd_thread_stop_nowait(&tconn->worker);
	}
}

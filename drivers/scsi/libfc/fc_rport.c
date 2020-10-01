/*
 * Copyright(c) 2007 - 2008 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Maintained at www.Open-FCoE.org
 */

/*
 * RPORT GENERAL INFO
 *
 * This file contains all processing regarding fc_rports. It contains the
 * rport state machine and does all rport interaction with the transport class.
 * There should be no other places in libfc that interact directly with the
 * transport class in regards to adding and deleting rports.
 *
 * fc_rport's represent N_Port's within the fabric.
 */

/*
 * RPORT LOCKING
 *
 * The rport should never hold the rport mutex and then attempt to acquire
 * either the lport or disc mutexes. The rport's mutex is considered lesser
 * than both the lport's mutex and the disc mutex. Refer to fc_lport.c for
 * more comments on the hierarchy.
 *
 * The locking strategy is similar to the lport's strategy. The lock protects
 * the rport's states and is held and released by the entry points to the rport
 * block. All _enter_* functions correspond to rport states and expect the rport
 * mutex to be locked before calling them. This means that rports only handle
 * one request or response at a time, since they're not critical for the I/O
 * path this potential over-use of the mutex is acceptable.
 */

/*
 * RPORT REFERENCE COUNTING
 *
 * A rport reference should be taken when:
 * - an rport is allocated
 * - a workqueue item is scheduled
 * - an ELS request is send
 * The reference should be dropped when:
 * - the workqueue function has finished
 * - the ELS response is handled
 * - an rport is removed
 */

#include <linux/kernel.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/rcupdate.h>
#include <linux/timer.h>
#include <linux/workqueue.h>
#include <linux/export.h>
#include <linux/rculist.h>

#include <asm/unaligned.h>

#include <scsi/libfc.h>
#include <scsi/fc_encode.h>

#include "fc_libfc.h"

static struct workqueue_struct *rport_event_queue;

static void fc_rport_enter_flogi(struct fc_rport_priv *);
static void fc_rport_enter_plogi(struct fc_rport_priv *);
static void fc_rport_enter_prli(struct fc_rport_priv *);
static void fc_rport_enter_rtv(struct fc_rport_priv *);
static void fc_rport_enter_ready(struct fc_rport_priv *);
static void fc_rport_enter_logo(struct fc_rport_priv *);
static void fc_rport_enter_adisc(struct fc_rport_priv *);

static void fc_rport_recv_plogi_req(struct fc_lport *, struct fc_frame *);
static void fc_rport_recv_prli_req(struct fc_rport_priv *, struct fc_frame *);
static void fc_rport_recv_prlo_req(struct fc_rport_priv *, struct fc_frame *);
static void fc_rport_recv_logo_req(struct fc_lport *, struct fc_frame *);
static void fc_rport_timeout(struct work_struct *);
static void fc_rport_error(struct fc_rport_priv *, int);
static void fc_rport_error_retry(struct fc_rport_priv *, int);
static void fc_rport_work(struct work_struct *);

static const char *fc_rport_state_names[] = {
	[RPORT_ST_INIT] = "Init",
	[RPORT_ST_FLOGI] = "FLOGI",
	[RPORT_ST_PLOGI_WAIT] = "PLOGI_WAIT",
	[RPORT_ST_PLOGI] = "PLOGI",
	[RPORT_ST_PRLI] = "PRLI",
	[RPORT_ST_RTV] = "RTV",
	[RPORT_ST_READY] = "Ready",
	[RPORT_ST_ADISC] = "ADISC",
	[RPORT_ST_DELETE] = "Delete",
};

/**
 * fc_rport_lookup() - Lookup a remote port by port_id
 * @lport:   The local port to lookup the remote port on
 * @port_id: The remote port ID to look up
 *
 * The reference count of the fc_rport_priv structure is
 * increased by one.
 */
struct fc_rport_priv *fc_rport_lookup(const struct fc_lport *lport,
				      u32 port_id)
{
	struct fc_rport_priv *rdata = NULL, *tmp_rdata;

	rcu_read_lock();
	list_for_each_entry_rcu(tmp_rdata, &lport->disc.rports, peers)
		if (tmp_rdata->ids.port_id == port_id &&
		    kref_get_unless_zero(&tmp_rdata->kref)) {
			rdata = tmp_rdata;
			break;
		}
	rcu_read_unlock();
	return rdata;
}
EXPORT_SYMBOL(fc_rport_lookup);

/**
 * fc_rport_create() - Create a new remote port
 * @lport: The local port this remote port will be associated with
 * @ids:   The identifiers for the new remote port
 *
 * The remote port will start in the INIT state.
 */
struct fc_rport_priv *fc_rport_create(struct fc_lport *lport, u32 port_id)
{
	struct fc_rport_priv *rdata;
	size_t rport_priv_size = sizeof(*rdata);

	lockdep_assert_held(&lport->disc.disc_mutex);

	rdata = fc_rport_lookup(lport, port_id);
	if (rdata) {
		kref_put(&rdata->kref, fc_rport_destroy);
		return rdata;
	}

	if (lport->rport_priv_size > 0)
		rport_priv_size = lport->rport_priv_size;
	rdata = kzalloc(rport_priv_size, GFP_KERNEL);
	if (!rdata)
		return NULL;

	rdata->ids.node_name = -1;
	rdata->ids.port_name = -1;
	rdata->ids.port_id = port_id;
	rdata->ids.roles = FC_RPORT_ROLE_UNKNOWN;

	kref_init(&rdata->kref);
	mutex_init(&rdata->rp_mutex);
	rdata->local_port = lport;
	rdata->rp_state = RPORT_ST_INIT;
	rdata->event = RPORT_EV_NONE;
	rdata->flags = FC_RP_FLAGS_REC_SUPPORTED;
	rdata->e_d_tov = lport->e_d_tov;
	rdata->r_a_tov = lport->r_a_tov;
	rdata->maxframe_size = FC_MIN_MAX_PAYLOAD;
	INIT_DELAYED_WORK(&rdata->retry_work, fc_rport_timeout);
	INIT_WORK(&rdata->event_work, fc_rport_work);
	if (port_id != FC_FID_DIR_SERV) {
		rdata->lld_event_callback = lport->tt.rport_event_callback;
		list_add_rcu(&rdata->peers, &lport->disc.rports);
	}
	return rdata;
}
EXPORT_SYMBOL(fc_rport_create);

/**
 * fc_rport_destroy() - Free a remote port after last reference is released
 * @kref: The remote port's kref
 */
void fc_rport_destroy(struct kref *kref)
{
	struct fc_rport_priv *rdata;

	rdata = container_of(kref, struct fc_rport_priv, kref);
	kfree_rcu(rdata, rcu);
}
EXPORT_SYMBOL(fc_rport_destroy);

/**
 * fc_rport_state() - Return a string identifying the remote port's state
 * @rdata: The remote port
 */
static const char *fc_rport_state(struct fc_rport_priv *rdata)
{
	const char *cp;

	cp = fc_rport_state_names[rdata->rp_state];
	if (!cp)
		cp = "Unknown";
	return cp;
}

/**
 * fc_set_rport_loss_tmo() - Set the remote port loss timeout
 * @rport:   The remote port that gets a new timeout value
 * @timeout: The new timeout value (in seconds)
 */
void fc_set_rport_loss_tmo(struct fc_rport *rport, u32 timeout)
{
	if (timeout)
		rport->dev_loss_tmo = timeout;
	else
		rport->dev_loss_tmo = 1;
}
EXPORT_SYMBOL(fc_set_rport_loss_tmo);

/**
 * fc_plogi_get_maxframe() - Get the maximum payload from the common service
 *			     parameters in a FLOGI frame
 * @flp:    The FLOGI or PLOGI payload
 * @maxval: The maximum frame size upper limit; this may be less than what
 *	    is in the service parameters
 */
static unsigned int fc_plogi_get_maxframe(struct fc_els_flogi *flp,
					  unsigned int maxval)
{
	unsigned int mfs;

	/*
	 * Get max payload from the common service parameters and the
	 * class 3 receive data field size.
	 */
	mfs = ntohs(flp->fl_csp.sp_bb_data) & FC_SP_BB_DATA_MASK;
	if (mfs >= FC_SP_MIN_MAX_PAYLOAD && mfs < maxval)
		maxval = mfs;
	mfs = ntohs(flp->fl_cssp[3 - 1].cp_rdfs);
	if (mfs >= FC_SP_MIN_MAX_PAYLOAD && mfs < maxval)
		maxval = mfs;
	return maxval;
}

/**
 * fc_rport_state_enter() - Change the state of a remote port
 * @rdata: The remote port whose state should change
 * @new:   The new state
 */
static void fc_rport_state_enter(struct fc_rport_priv *rdata,
				 enum fc_rport_state new)
{
	lockdep_assert_held(&rdata->rp_mutex);

	if (rdata->rp_state != new)
		rdata->retries = 0;
	rdata->rp_state = new;
}

/**
 * fc_rport_work() - Handler for remote port events in the rport_event_queue
 * @work: Handle to the remote port being dequeued
 *
 * Reference counting: drops kref on return
 */
static void fc_rport_work(struct work_struct *work)
{
	u32 port_id;
	struct fc_rport_priv *rdata =
		container_of(work, struct fc_rport_priv, event_work);
	struct fc_rport_libfc_priv *rpriv;
	enum fc_rport_event event;
	struct fc_lport *lport = rdata->local_port;
	struct fc_rport_operations *rport_ops;
	struct fc_rport_identifiers ids;
	struct fc_rport *rport;
	struct fc4_prov *prov;
	u8 type;

	mutex_lock(&rdata->rp_mutex);
	event = rdata->event;
	rport_ops = rdata->ops;
	rport = rdata->rport;

	FC_RPORT_DBG(rdata, "work event %u\n", event);

	switch (event) {
	case RPORT_EV_READY:
		ids = rdata->ids;
		rdata->event = RPORT_EV_NONE;
		rdata->major_retries = 0;
		kref_get(&rdata->kref);
		mutex_unlock(&rdata->rp_mutex);

		if (!rport) {
			FC_RPORT_DBG(rdata, "No rport!\n");
			rport = fc_remote_port_add(lport->host, 0, &ids);
		}
		if (!rport) {
			FC_RPORT_DBG(rdata, "Failed to add the rport\n");
			fc_rport_logoff(rdata);
			kref_put(&rdata->kref, fc_rport_destroy);
			return;
		}
		mutex_lock(&rdata->rp_mutex);
		if (rdata->rport)
			FC_RPORT_DBG(rdata, "rport already allocated\n");
		rdata->rport = rport;
		rport->maxframe_size = rdata->maxframe_size;
		rport->supported_classes = rdata->supported_classes;

		rpriv = rport->dd_data;
		rpriv->local_port = lport;
		rpriv->rp_state = rdata->rp_state;
		rpriv->flags = rdata->flags;
		rpriv->e_d_tov = rdata->e_d_tov;
		rpriv->r_a_tov = rdata->r_a_tov;
		mutex_unlock(&rdata->rp_mutex);

		if (rport_ops && rport_ops->event_callback) {
			FC_RPORT_DBG(rdata, "callback ev %d\n", event);
			rport_ops->event_callback(lport, rdata, event);
		}
		if (rdata->lld_event_callback) {
			FC_RPORT_DBG(rdata, "lld callback ev %d\n", event);
			rdata->lld_event_callback(lport, rdata, event);
		}
		kref_put(&rdata->kref, fc_rport_destroy);
		break;

	case RPORT_EV_FAILED:
	case RPORT_EV_LOGO:
	case RPORT_EV_STOP:
		if (rdata->prli_count) {
			mutex_lock(&fc_prov_mutex);
			for (type = 1; type < FC_FC4_PROV_SIZE; type++) {
				prov = fc_passive_prov[type];
				if (prov && prov->prlo)
					prov->prlo(rdata);
			}
			mutex_unlock(&fc_prov_mutex);
		}
		port_id = rdata->ids.port_id;
		mutex_unlock(&rdata->rp_mutex);

		if (rport_ops && rport_ops->event_callback) {
			FC_RPORT_DBG(rdata, "callback ev %d\n", event);
			rport_ops->event_callback(lport, rdata, event);
		}
		if (rdata->lld_event_callback) {
			FC_RPORT_DBG(rdata, "lld callback ev %d\n", event);
			rdata->lld_event_callback(lport, rdata, event);
		}
		if (cancel_delayed_work_sync(&rdata->retry_work))
			kref_put(&rdata->kref, fc_rport_destroy);

		/*
		 * Reset any outstanding exchanges before freeing rport.
		 */
		lport->tt.exch_mgr_reset(lport, 0, port_id);
		lport->tt.exch_mgr_reset(lport, port_id, 0);

		if (rport) {
			rpriv = rport->dd_data;
			rpriv->rp_state = RPORT_ST_DELETE;
			mutex_lock(&rdata->rp_mutex);
			rdata->rport = NULL;
			mutex_unlock(&rdata->rp_mutex);
			fc_remote_port_delete(rport);
		}

		mutex_lock(&rdata->rp_mutex);
		if (rdata->rp_state == RPORT_ST_DELETE) {
			if (port_id == FC_FID_DIR_SERV) {
				rdata->event = RPORT_EV_NONE;
				mutex_unlock(&rdata->rp_mutex);
				kref_put(&rdata->kref, fc_rport_destroy);
			} else if ((rdata->flags & FC_RP_STARTED) &&
				   rdata->major_retries <
				   lport->max_rport_retry_count) {
				rdata->major_retries++;
				rdata->event = RPORT_EV_NONE;
				FC_RPORT_DBG(rdata, "work restart\n");
				fc_rport_enter_flogi(rdata);
				mutex_unlock(&rdata->rp_mutex);
			} else {
				mutex_unlock(&rdata->rp_mutex);
				FC_RPORT_DBG(rdata, "work delete\n");
				mutex_lock(&lport->disc.disc_mutex);
				list_del_rcu(&rdata->peers);
				mutex_unlock(&lport->disc.disc_mutex);
				kref_put(&rdata->kref, fc_rport_destroy);
			}
		} else {
			/*
			 * Re-open for events.  Reissue READY event if ready.
			 */
			rdata->event = RPORT_EV_NONE;
			if (rdata->rp_state == RPORT_ST_READY) {
				FC_RPORT_DBG(rdata, "work reopen\n");
				fc_rport_enter_ready(rdata);
			}
			mutex_unlock(&rdata->rp_mutex);
		}
		break;

	default:
		mutex_unlock(&rdata->rp_mutex);
		break;
	}
	kref_put(&rdata->kref, fc_rport_destroy);
}

/**
 * fc_rport_login() - Start the remote port login state machine
 * @rdata: The remote port to be logged in to
 *
 * Initiates the RP state machine. It is called from the LP module.
 * This function will issue the following commands to the N_Port
 * identified by the FC ID provided.
 *
 * - PLOGI
 * - PRLI
 * - RTV
 *
 * Locking Note: Called without the rport lock held. This
 * function will hold the rport lock, call an _enter_*
 * function and then unlock the rport.
 *
 * This indicates the intent to be logged into the remote port.
 * If it appears we are already logged in, ADISC is used to verify
 * the setup.
 */
int fc_rport_login(struct fc_rport_priv *rdata)
{
	mutex_lock(&rdata->rp_mutex);

	if (rdata->flags & FC_RP_STARTED) {
		FC_RPORT_DBG(rdata, "port already started\n");
		mutex_unlock(&rdata->rp_mutex);
		return 0;
	}

	rdata->flags |= FC_RP_STARTED;
	switch (rdata->rp_state) {
	case RPORT_ST_READY:
		FC_RPORT_DBG(rdata, "ADISC port\n");
		fc_rport_enter_adisc(rdata);
		break;
	case RPORT_ST_DELETE:
		FC_RPORT_DBG(rdata, "Restart deleted port\n");
		break;
	case RPORT_ST_INIT:
		FC_RPORT_DBG(rdata, "Login to port\n");
		fc_rport_enter_flogi(rdata);
		break;
	default:
		FC_RPORT_DBG(rdata, "Login in progress, state %s\n",
			     fc_rport_state(rdata));
		break;
	}
	mutex_unlock(&rdata->rp_mutex);

	return 0;
}
EXPORT_SYMBOL(fc_rport_login);

/**
 * fc_rport_enter_delete() - Schedule a remote port to be deleted
 * @rdata: The remote port to be deleted
 * @event: The event to report as the reason for deletion
 *
 * Allow state change into DELETE only once.
 *
 * Call queue_work only if there's no event already pending.
 * Set the new event so that the old pending event will not occur.
 * Since we have the mutex, even if fc_rport_work() is already started,
 * it'll see the new event.
 *
 * Reference counting: does not modify kref
 */
static void fc_rport_enter_delete(struct fc_rport_priv *rdata,
				  enum fc_rport_event event)
{
	lockdep_assert_held(&rdata->rp_mutex);

	if (rdata->rp_state == RPORT_ST_DELETE)
		return;

	FC_RPORT_DBG(rdata, "Delete port\n");

	fc_rport_state_enter(rdata, RPORT_ST_DELETE);

	if (rdata->event == RPORT_EV_NONE) {
		kref_get(&rdata->kref);
		if (!queue_work(rport_event_queue, &rdata->event_work))
			kref_put(&rdata->kref, fc_rport_destroy);
	}

	rdata->event = event;
}

/**
 * fc_rport_logoff() - Logoff and remove a remote port
 * @rdata: The remote port to be logged off of
 *
 * Locking Note: Called without the rport lock held. This
 * function will hold the rport lock, call an _enter_*
 * function and then unlock the rport.
 */
int fc_rport_logoff(struct fc_rport_priv *rdata)
{
	struct fc_lport *lport = rdata->local_port;
	u32 port_id = rdata->ids.port_id;

	mutex_lock(&rdata->rp_mutex);

	FC_RPORT_DBG(rdata, "Remove port\n");

	rdata->flags &= ~FC_RP_STARTED;
	if (rdata->rp_state == RPORT_ST_DELETE) {
		FC_RPORT_DBG(rdata, "Port in Delete state, not removing\n");
		goto out;
	}
	/*
	 * FC-LS states:
	 * To explicitly Logout, the initiating Nx_Port shall terminate
	 * other open Sequences that it initiated with the destination
	 * Nx_Port prior to performing Logout.
	 */
	lport->tt.exch_mgr_reset(lport, 0, port_id);
	lport->tt.exch_mgr_reset(lport, port_id, 0);

	fc_rport_enter_logo(rdata);

	/*
	 * Change the state to Delete so that we discard
	 * the response.
	 */
	fc_rport_enter_delete(rdata, RPORT_EV_STOP);
out:
	mutex_unlock(&rdata->rp_mutex);
	return 0;
}
EXPORT_SYMBOL(fc_rport_logoff);

/**
 * fc_rport_enter_ready() - Transition to the RPORT_ST_READY state
 * @rdata: The remote port that is ready
 *
 * Reference counting: schedules workqueue, does not modify kref
 */
static void fc_rport_enter_ready(struct fc_rport_priv *rdata)
{
	lockdep_assert_held(&rdata->rp_mutex);

	fc_rport_state_enter(rdata, RPORT_ST_READY);

	FC_RPORT_DBG(rdata, "Port is Ready\n");

	kref_get(&rdata->kref);
	if (rdata->event == RPORT_EV_NONE &&
	    !queue_work(rport_event_queue, &rdata->event_work))
		kref_put(&rdata->kref, fc_rport_destroy);

	rdata->event = RPORT_EV_READY;
}

/**
 * fc_rport_timeout() - Handler for the retry_work timer
 * @work: Handle to the remote port that has timed out
 *
 * Locking Note: Called without the rport lock held. This
 * function will hold the rport lock, call an _enter_*
 * function and then unlock the rport.
 *
 * Reference counting: Drops kref on return.
 */
static void fc_rport_timeout(struct work_struct *work)
{
	struct fc_rport_priv *rdata =
		container_of(work, struct fc_rport_priv, retry_work.work);

	mutex_lock(&rdata->rp_mutex);
	FC_RPORT_DBG(rdata, "Port timeout, state %s\n", fc_rport_state(rdata));

	switch (rdata->rp_state) {
	case RPORT_ST_FLOGI:
		fc_rport_enter_flogi(rdata);
		break;
	case RPORT_ST_PLOGI:
		fc_rport_enter_plogi(rdata);
		break;
	case RPORT_ST_PRLI:
		fc_rport_enter_prli(rdata);
		break;
	case RPORT_ST_RTV:
		fc_rport_enter_rtv(rdata);
		break;
	case RPORT_ST_ADISC:
		fc_rport_enter_adisc(rdata);
		break;
	case RPORT_ST_PLOGI_WAIT:
	case RPORT_ST_READY:
	case RPORT_ST_INIT:
	case RPORT_ST_DELETE:
		break;
	}

	mutex_unlock(&rdata->rp_mutex);
	kref_put(&rdata->kref, fc_rport_destroy);
}

/**
 * fc_rport_error() - Error handler, called once retries have been exhausted
 * @rdata: The remote port the error is happened on
 * @err:   The error code
 *
 * Reference counting: does not modify kref
 */
static void fc_rport_error(struct fc_rport_priv *rdata, int err)
{
	struct fc_lport *lport = rdata->local_port;

	lockdep_assert_held(&rdata->rp_mutex);

	FC_RPORT_DBG(rdata, "Error %d in state %s, retries %d\n",
		     -err, fc_rport_state(rdata), rdata->retries);

	switch (rdata->rp_state) {
	case RPORT_ST_FLOGI:
		rdata->flags &= ~FC_RP_STARTED;
		fc_rport_enter_delete(rdata, RPORT_EV_FAILED);
		break;
	case RPORT_ST_PLOGI:
		if (lport->point_to_multipoint) {
			rdata->flags &= ~FC_RP_STARTED;
			fc_rport_enter_delete(rdata, RPORT_EV_FAILED);
		} else
			fc_rport_enter_logo(rdata);
		break;
	case RPORT_ST_RTV:
		fc_rport_enter_ready(rdata);
		break;
	case RPORT_ST_PRLI:
	case RPORT_ST_ADISC:
		fc_rport_enter_logo(rdata);
		break;
	case RPORT_ST_PLOGI_WAIT:
	case RPORT_ST_DELETE:
	case RPORT_ST_READY:
	case RPORT_ST_INIT:
		break;
	}
}

/**
 * fc_rport_error_retry() - Handler for remote port state retries
 * @rdata: The remote port whose state is to be retried
 * @err:   The error code
 *
 * If the error was an exchange timeout retry immediately,
 * otherwise wait for E_D_TOV.
 *
 * Reference counting: increments kref when scheduling retry_work
 */
static void fc_rport_error_retry(struct fc_rport_priv *rdata, int err)
{
	unsigned long delay = msecs_to_jiffies(rdata->e_d_tov);

	lockdep_assert_held(&rdata->rp_mutex);

	/* make sure this isn't an FC_EX_CLOSED error, never retry those */
	if (err == -FC_EX_CLOSED)
		goto out;

	if (rdata->retries < rdata->local_port->max_rport_retry_count) {
		FC_RPORT_DBG(rdata, "Error %d in state %s, retrying\n",
			     err, fc_rport_state(rdata));
		rdata->retries++;
		/* no additional delay on exchange timeouts */
		if (err == -FC_EX_TIMEOUT)
			delay = 0;
		kref_get(&rdata->kref);
		if (!schedule_delayed_work(&rdata->retry_work, delay))
			kref_put(&rdata->kref, fc_rport_destroy);
		return;
	}

out:
	fc_rport_error(rdata, err);
}

/**
 * fc_rport_login_complete() - Handle parameters and completion of p-mp login.
 * @rdata:  The remote port which we logged into or which logged into us.
 * @fp:     The FLOGI or PLOGI request or response frame
 *
 * Returns non-zero error if a problem is detected with the frame.
 * Does not free the frame.
 *
 * This is only used in point-to-multipoint mode for FIP currently.
 */
static int fc_rport_login_complete(struct fc_rport_priv *rdata,
				   struct fc_frame *fp)
{
	struct fc_lport *lport = rdata->local_port;
	struct fc_els_flogi *flogi;
	unsigned int e_d_tov;
	u16 csp_flags;

	flogi = fc_frame_payload_get(fp, sizeof(*flogi));
	if (!flogi)
		return -EINVAL;

	csp_flags = ntohs(flogi->fl_csp.sp_features);

	if (fc_frame_payload_op(fp) == ELS_FLOGI) {
		if (csp_flags & FC_SP_FT_FPORT) {
			FC_RPORT_DBG(rdata, "Fabric bit set in FLOGI\n");
			return -EINVAL;
		}
	} else {

		/*
		 * E_D_TOV is not valid on an incoming FLOGI request.
		 */
		e_d_tov = ntohl(flogi->fl_csp.sp_e_d_tov);
		if (csp_flags & FC_SP_FT_EDTR)
			e_d_tov /= 1000000;
		if (e_d_tov > rdata->e_d_tov)
			rdata->e_d_tov = e_d_tov;
	}
	rdata->maxframe_size = fc_plogi_get_maxframe(flogi, lport->mfs);
	return 0;
}

/**
 * fc_rport_flogi_resp() - Handle response to FLOGI request for p-mp mode
 * @sp:	    The sequence that the FLOGI was on
 * @fp:	    The FLOGI response frame
 * @rp_arg: The remote port that received the FLOGI response
 */
static void fc_rport_flogi_resp(struct fc_seq *sp, struct fc_frame *fp,
				void *rp_arg)
{
	struct fc_rport_priv *rdata = rp_arg;
	struct fc_lport *lport = rdata->local_port;
	struct fc_els_flogi *flogi;
	unsigned int r_a_tov;
	u8 opcode;
	int err = 0;

	FC_RPORT_DBG(rdata, "Received a FLOGI %s\n",
		     IS_ERR(fp) ? "error" : fc_els_resp_type(fp));

	if (fp == ERR_PTR(-FC_EX_CLOSED))
		goto put;

	mutex_lock(&rdata->rp_mutex);

	if (rdata->rp_state != RPORT_ST_FLOGI) {
		FC_RPORT_DBG(rdata, "Received a FLOGI response, but in state "
			     "%s\n", fc_rport_state(rdata));
		if (IS_ERR(fp))
			goto err;
		goto out;
	}

	if (IS_ERR(fp)) {
		fc_rport_error(rdata, PTR_ERR(fp));
		goto err;
	}
	opcode = fc_frame_payload_op(fp);
	if (opcode == ELS_LS_RJT) {
		struct fc_els_ls_rjt *rjt;

		rjt = fc_frame_payload_get(fp, sizeof(*rjt));
		FC_RPORT_DBG(rdata, "FLOGI ELS rejected, reason %x expl %x\n",
			     rjt->er_reason, rjt->er_explan);
		err = -FC_EX_ELS_RJT;
		goto bad;
	} else if (opcode != ELS_LS_ACC) {
		FC_RPORT_DBG(rdata, "FLOGI ELS invalid opcode %x\n", opcode);
		err = -FC_EX_ELS_RJT;
		goto bad;
	}
	if (fc_rport_login_complete(rdata, fp)) {
		FC_RPORT_DBG(rdata, "FLOGI failed, no login\n");
		err = -FC_EX_INV_LOGIN;
		goto bad;
	}

	flogi = fc_frame_payload_get(fp, sizeof(*flogi));
	if (!flogi) {
		err = -FC_EX_ALLOC_ERR;
		goto bad;
	}
	r_a_tov = ntohl(flogi->fl_csp.sp_r_a_tov);
	if (r_a_tov > rdata->r_a_tov)
		rdata->r_a_tov = r_a_tov;

	if (rdata->ids.port_name < lport->wwpn)
		fc_rport_enter_plogi(rdata);
	else
		fc_rport_state_enter(rdata, RPORT_ST_PLOGI_WAIT);
out:
	fc_frame_free(fp);
err:
	mutex_unlock(&rdata->rp_mutex);
put:
	kref_put(&rdata->kref, fc_rport_destroy);
	return;
bad:
	FC_RPORT_DBG(rdata, "Bad FLOGI response\n");
	fc_rport_error_retry(rdata, err);
	goto out;
}

/**
 * fc_rport_enter_flogi() - Send a FLOGI request to the remote port for p-mp
 * @rdata: The remote port to send a FLOGI to
 *
 * Reference counting: increments kref when sending ELS
 */
static void fc_rport_enter_flogi(struct fc_rport_priv *rdata)
{
	struct fc_lport *lport = rdata->local_port;
	struct fc_frame *fp;

	lockdep_assert_held(&rdata->rp_mutex);

	if (!lport->point_to_multipoint)
		return fc_rport_enter_plogi(rdata);

	FC_RPORT_DBG(rdata, "Entered FLOGI state from %s state\n",
		     fc_rport_state(rdata));

	fc_rport_state_enter(rdata, RPORT_ST_FLOGI);

	fp = fc_frame_alloc(lport, sizeof(struct fc_els_flogi));
	if (!fp)
		return fc_rport_error_retry(rdata, -FC_EX_ALLOC_ERR);

	kref_get(&rdata->kref);
	if (!lport->tt.elsct_send(lport, rdata->ids.port_id, fp, ELS_FLOGI,
				  fc_rport_flogi_resp, rdata,
				  2 * lport->r_a_tov)) {
		fc_rport_error_retry(rdata, -FC_EX_XMIT_ERR);
		kref_put(&rdata->kref, fc_rport_destroy);
	}
}

/**
 * fc_rport_recv_flogi_req() - Handle Fabric Login (FLOGI) request in p-mp mode
 * @lport: The local port that received the PLOGI request
 * @rx_fp: The PLOGI request frame
 *
 * Reference counting: drops kref on return
 */
static void fc_rport_recv_flogi_req(struct fc_lport *lport,
				    struct fc_frame *rx_fp)
{
	struct fc_disc *disc;
	struct fc_els_flogi *flp;
	struct fc_rport_priv *rdata;
	struct fc_frame *fp = rx_fp;
	struct fc_seq_els_data rjt_data;
	u32 sid;

	sid = fc_frame_sid(fp);

	FC_RPORT_ID_DBG(lport, sid, "Received FLOGI request\n");

	disc = &lport->disc;
	if (!lport->point_to_multipoint) {
		rjt_data.reason = ELS_RJT_UNSUP;
		rjt_data.explan = ELS_EXPL_NONE;
		goto reject;
	}

	flp = fc_frame_payload_get(fp, sizeof(*flp));
	if (!flp) {
		rjt_data.reason = ELS_RJT_LOGIC;
		rjt_data.explan = ELS_EXPL_INV_LEN;
		goto reject;
	}

	rdata = fc_rport_lookup(lport, sid);
	if (!rdata) {
		rjt_data.reason = ELS_RJT_FIP;
		rjt_data.explan = ELS_EXPL_NOT_NEIGHBOR;
		goto reject;
	}
	mutex_lock(&rdata->rp_mutex);

	FC_RPORT_DBG(rdata, "Received FLOGI in %s state\n",
		     fc_rport_state(rdata));

	switch (rdata->rp_state) {
	case RPORT_ST_INIT:
		/*
		 * If received the FLOGI request on RPORT which is INIT state
		 * (means not transition to FLOGI either fc_rport timeout
		 * function didn;t trigger or this end hasn;t received
		 * beacon yet from other end. In that case only, allow RPORT
		 * state machine to continue, otherwise fall through which
		 * causes the code to send reject response.
		 * NOTE; Not checking for FIP->state such as VNMP_UP or
		 * VNMP_CLAIM because if FIP state is not one of those,
		 * RPORT wouldn;t have created and 'rport_lookup' would have
		 * failed anyway in that case.
		 */
		break;
	case RPORT_ST_DELETE:
		mutex_unlock(&rdata->rp_mutex);
		rjt_data.reason = ELS_RJT_FIP;
		rjt_data.explan = ELS_EXPL_NOT_NEIGHBOR;
		goto reject_put;
	case RPORT_ST_FLOGI:
	case RPORT_ST_PLOGI_WAIT:
	case RPORT_ST_PLOGI:
		break;
	case RPORT_ST_PRLI:
	case RPORT_ST_RTV:
	case RPORT_ST_READY:
	case RPORT_ST_ADISC:
		/*
		 * Set the remote port to be deleted and to then restart.
		 * This queues work to be sure exchanges are reset.
		 */
		fc_rport_enter_delete(rdata, RPORT_EV_LOGO);
		mutex_unlock(&rdata->rp_mutex);
		rjt_data.reason = ELS_RJT_BUSY;
		rjt_data.explan = ELS_EXPL_NONE;
		goto reject_put;
	}
	if (fc_rport_login_complete(rdata, fp)) {
		mutex_unlock(&rdata->rp_mutex);
		rjt_data.reason = ELS_RJT_LOGIC;
		rjt_data.explan = ELS_EXPL_NONE;
		goto reject_put;
	}

	fp = fc_frame_alloc(lport, sizeof(*flp));
	if (!fp)
		goto out;

	fc_flogi_fill(lport, fp);
	flp = fc_frame_payload_get(fp, sizeof(*flp));
	flp->fl_cmd = ELS_LS_ACC;

	fc_fill_reply_hdr(fp, rx_fp, FC_RCTL_ELS_REP, 0);
	lport->tt.frame_send(lport, fp);

	/*
	 * Do not proceed with the state machine if our
	 * FLOGI has crossed with an FLOGI from the
	 * remote port; wait for the FLOGI response instead.
	 */
	if (rdata->rp_state != RPORT_ST_FLOGI) {
		if (rdata->ids.port_name < lport->wwpn)
			fc_rport_enter_plogi(rdata);
		else
			fc_rport_state_enter(rdata, RPORT_ST_PLOGI_WAIT);
	}
out:
	mutex_unlock(&rdata->rp_mutex);
	kref_put(&rdata->kref, fc_rport_destroy);
	fc_frame_free(rx_fp);
	return;

reject_put:
	kref_put(&rdata->kref, fc_rport_destroy);
reject:
	fc_seq_els_rsp_send(rx_fp, ELS_LS_RJT, &rjt_data);
	fc_frame_free(rx_fp);
}

/**
 * fc_rport_plogi_resp() - Handler for ELS PLOGI responses
 * @sp:	       The sequence the PLOGI is on
 * @fp:	       The PLOGI response frame
 * @rdata_arg: The remote port that sent the PLOGI response
 *
 * Locking Note: This function will be called without the rport lock
 * held, but it will lock, call an _enter_* function or fc_rport_error
 * and then unlock the rport.
 */
static void fc_rport_plogi_resp(struct fc_seq *sp, struct fc_frame *fp,
				void *rdata_arg)
{
	struct fc_rport_priv *rdata = rdata_arg;
	struct fc_lport *lport = rdata->local_port;
	struct fc_els_flogi *plp = NULL;
	u16 csp_seq;
	u16 cssp_seq;
	u8 op;

	FC_RPORT_DBG(rdata, "Received a PLOGI %s\n", fc_els_resp_type(fp));

	if (fp == ERR_PTR(-FC_EX_CLOSED))
		goto put;

	mutex_lock(&rdata->rp_mutex);

	if (rdata->rp_state != RPORT_ST_PLOGI) {
		FC_RPORT_DBG(rdata, "Received a PLOGI response, but in state "
			     "%s\n", fc_rport_state(rdata));
		if (IS_ERR(fp))
			goto err;
		goto out;
	}

	if (IS_ERR(fp)) {
		fc_rport_error_retry(rdata, PTR_ERR(fp));
		goto err;
	}

	op = fc_frame_payload_op(fp);
	if (op == ELS_LS_ACC &&
	    (plp = fc_frame_payload_get(fp, sizeof(*plp))) != NULL) {
		rdata->ids.port_name = get_unaligned_be64(&plp->fl_wwpn);
		rdata->ids.node_name = get_unaligned_be64(&plp->fl_wwnn);

		/* save plogi response sp_features for further reference */
		rdata->sp_features = ntohs(plp->fl_csp.sp_features);

		if (lport->point_to_multipoint)
			fc_rport_login_complete(rdata, fp);
		csp_seq = ntohs(plp->fl_csp.sp_tot_seq);
		cssp_seq = ntohs(plp->fl_cssp[3 - 1].cp_con_seq);
		if (cssp_seq < csp_seq)
			csp_seq = cssp_seq;
		rdata->max_seq = csp_seq;
		rdata->maxframe_size = fc_plogi_get_maxframe(plp, lport->mfs);
		fc_rport_enter_prli(rdata);
	} else {
		struct fc_els_ls_rjt *rjt;

		rjt = fc_frame_payload_get(fp, sizeof(*rjt));
		FC_RPORT_DBG(rdata, "PLOGI ELS rejected, reason %x expl %x\n",
			     rjt->er_reason, rjt->er_explan);
		fc_rport_error_retry(rdata, -FC_EX_ELS_RJT);
	}
out:
	fc_frame_free(fp);
err:
	mutex_unlock(&rdata->rp_mutex);
put:
	kref_put(&rdata->kref, fc_rport_destroy);
}

static bool
fc_rport_compatible_roles(struct fc_lport *lport, struct fc_rport_priv *rdata)
{
	if (rdata->ids.roles == FC_PORT_ROLE_UNKNOWN)
		return true;
	if ((rdata->ids.roles & FC_PORT_ROLE_FCP_TARGET) &&
	    (lport->service_params & FCP_SPPF_INIT_FCN))
		return true;
	if ((rdata->ids.roles & FC_PORT_ROLE_FCP_INITIATOR) &&
	    (lport->service_params & FCP_SPPF_TARG_FCN))
		return true;
	return false;
}

/**
 * fc_rport_enter_plogi() - Send Port Login (PLOGI) request
 * @rdata: The remote port to send a PLOGI to
 *
 * Reference counting: increments kref when sending ELS
 */
static void fc_rport_enter_plogi(struct fc_rport_priv *rdata)
{
	struct fc_lport *lport = rdata->local_port;
	struct fc_frame *fp;

	lockdep_assert_held(&rdata->rp_mutex);

	if (!fc_rport_compatible_roles(lport, rdata)) {
		FC_RPORT_DBG(rdata, "PLOGI suppressed for incompatible role\n");
		fc_rport_state_enter(rdata, RPORT_ST_PLOGI_WAIT);
		return;
	}

	FC_RPORT_DBG(rdata, "Port entered PLOGI state from %s state\n",
		     fc_rport_state(rdata));

	fc_rport_state_enter(rdata, RPORT_ST_PLOGI);

	rdata->maxframe_size = FC_MIN_MAX_PAYLOAD;
	fp = fc_frame_alloc(lport, sizeof(struct fc_els_flogi));
	if (!fp) {
		FC_RPORT_DBG(rdata, "%s frame alloc failed\n", __func__);
		fc_rport_error_retry(rdata, -FC_EX_ALLOC_ERR);
		return;
	}
	rdata->e_d_tov = lport->e_d_tov;

	kref_get(&rdata->kref);
	if (!lport->tt.elsct_send(lport, rdata->ids.port_id, fp, ELS_PLOGI,
				  fc_rport_plogi_resp, rdata,
				  2 * lport->r_a_tov)) {
		fc_rport_error_retry(rdata, -FC_EX_XMIT_ERR);
		kref_put(&rdata->kref, fc_rport_destroy);
	}
}

/**
 * fc_rport_prli_resp() - Process Login (PRLI) response handler
 * @sp:	       The sequence the PRLI response was on
 * @fp:	       The PRLI response frame
 * @rdata_arg: The remote port that sent the PRLI response
 *
 * Locking Note: This function will be called without the rport lock
 * held, but it will lock, call an _enter_* function or fc_rport_error
 * and then unlock the rport.
 */
static void fc_rport_prli_resp(struct fc_seq *sp, struct fc_frame *fp,
			       void *rdata_arg)
{
	struct fc_rport_priv *rdata = rdata_arg;
	struct {
		struct fc_els_prli prli;
		struct fc_els_spp spp;
	} *pp;
	struct fc_els_spp temp_spp;
	struct fc_els_ls_rjt *rjt;
	struct fc4_prov *prov;
	u32 roles = FC_RPORT_ROLE_UNKNOWN;
	u32 fcp_parm = 0;
	u8 op;
	enum fc_els_spp_resp resp_code;

	FC_RPORT_DBG(rdata, "Received a PRLI %s\n", fc_els_resp_type(fp));

	if (fp == ERR_PTR(-FC_EX_CLOSED))
		goto put;

	mutex_lock(&rdata->rp_mutex);

	if (rdata->rp_state != RPORT_ST_PRLI) {
		FC_RPORT_DBG(rdata, "Received a PRLI response, but in state "
			     "%s\n", fc_rport_state(rdata));
		if (IS_ERR(fp))
			goto err;
		goto out;
	}

	if (IS_ERR(fp)) {
		fc_rport_error_retry(rdata, PTR_ERR(fp));
		goto err;
	}

	/* reinitialize remote port roles */
	rdata->ids.roles = FC_RPORT_ROLE_UNKNOWN;

	op = fc_frame_payload_op(fp);
	if (op == ELS_LS_ACC) {
		pp = fc_frame_payload_get(fp, sizeof(*pp));
		if (!pp)
			goto out;

		resp_code = (pp->spp.spp_flags & FC_SPP_RESP_MASK);
		FC_RPORT_DBG(rdata, "PRLI spp_flags = 0x%x spp_type 0x%x\n",
			     pp->spp.spp_flags, pp->spp.spp_type);
		rdata->spp_type = pp->spp.spp_type;
		if (resp_code != FC_SPP_RESP_ACK) {
			if (resp_code == FC_SPP_RESP_CONF)
				fc_rport_error(rdata, -FC_EX_SEQ_ERR);
			else
				fc_rport_error_retry(rdata, -FC_EX_SEQ_ERR);
			goto out;
		}
		if (pp->prli.prli_spp_len < sizeof(pp->spp))
			goto out;

		fcp_parm = ntohl(pp->spp.spp_params);
		if (fcp_parm & FCP_SPPF_RETRY)
			rdata->flags |= FC_RP_FLAGS_RETRY;
		if (fcp_parm & FCP_SPPF_CONF_COMPL)
			rdata->flags |= FC_RP_FLAGS_CONF_REQ;

		/*
		 * Call prli provider if we should act as a target
		 */
		prov = fc_passive_prov[rdata->spp_type];
		if (prov) {
			memset(&temp_spp, 0, sizeof(temp_spp));
			prov->prli(rdata, pp->prli.prli_spp_len,
				   &pp->spp, &temp_spp);
		}
		/*
		 * Check if the image pair could be established
		 */
		if (rdata->spp_type != FC_TYPE_FCP ||
		    !(pp->spp.spp_flags & FC_SPP_EST_IMG_PAIR)) {
			/*
			 * Nope; we can't use this port as a target.
			 */
			fcp_parm &= ~FCP_SPPF_TARG_FCN;
		}
		rdata->supported_classes = FC_COS_CLASS3;
		if (fcp_parm & FCP_SPPF_INIT_FCN)
			roles |= FC_RPORT_ROLE_FCP_INITIATOR;
		if (fcp_parm & FCP_SPPF_TARG_FCN)
			roles |= FC_RPORT_ROLE_FCP_TARGET;

		rdata->ids.roles = roles;
		fc_rport_enter_rtv(rdata);

	} else {
		rjt = fc_frame_payload_get(fp, sizeof(*rjt));
		FC_RPORT_DBG(rdata, "PRLI ELS rejected, reason %x expl %x\n",
			     rjt->er_reason, rjt->er_explan);
		fc_rport_error_retry(rdata, FC_EX_ELS_RJT);
	}

out:
	fc_frame_free(fp);
err:
	mutex_unlock(&rdata->rp_mutex);
put:
	kref_put(&rdata->kref, fc_rport_destroy);
}

/**
 * fc_rport_enter_prli() - Send Process Login (PRLI) request
 * @rdata: The remote port to send the PRLI request to
 *
 * Reference counting: increments kref when sending ELS
 */
static void fc_rport_enter_prli(struct fc_rport_priv *rdata)
{
	struct fc_lport *lport = rdata->local_port;
	struct {
		struct fc_els_prli prli;
		struct fc_els_spp spp;
	} *pp;
	struct fc_frame *fp;
	struct fc4_prov *prov;

	lockdep_assert_held(&rdata->rp_mutex);

	/*
	 * If the rport is one of the well known addresses
	 * we skip PRLI and RTV and go straight to READY.
	 */
	if (rdata->ids.port_id >= FC_FID_DOM_MGR) {
		fc_rport_enter_ready(rdata);
		return;
	}

	/*
	 * And if the local port does not support the initiator function
	 * there's no need to send a PRLI, either.
	 */
	if (!(lport->service_params & FCP_SPPF_INIT_FCN)) {
		    fc_rport_enter_ready(rdata);
		    return;
	}

	FC_RPORT_DBG(rdata, "Port entered PRLI state from %s state\n",
		     fc_rport_state(rdata));

	fc_rport_state_enter(rdata, RPORT_ST_PRLI);

	fp = fc_frame_alloc(lport, sizeof(*pp));
	if (!fp) {
		fc_rport_error_retry(rdata, -FC_EX_ALLOC_ERR);
		return;
	}

	fc_prli_fill(lport, fp);

	prov = fc_passive_prov[FC_TYPE_FCP];
	if (prov) {
		pp = fc_frame_payload_get(fp, sizeof(*pp));
		prov->prli(rdata, sizeof(pp->spp), NULL, &pp->spp);
	}

	fc_fill_fc_hdr(fp, FC_RCTL_ELS_REQ, rdata->ids.port_id,
		       fc_host_port_id(lport->host), FC_TYPE_ELS,
		       FC_FC_FIRST_SEQ | FC_FC_END_SEQ | FC_FC_SEQ_INIT, 0);

	kref_get(&rdata->kref);
	if (!fc_exch_seq_send(lport, fp, fc_rport_prli_resp,
			      NULL, rdata, 2 * lport->r_a_tov)) {
		fc_rport_error_retry(rdata, -FC_EX_XMIT_ERR);
		kref_put(&rdata->kref, fc_rport_destroy);
	}
}

/**
 * fc_rport_rtv_resp() - Handler for Request Timeout Value (RTV) responses
 * @sp:	       The sequence the RTV was on
 * @fp:	       The RTV response frame
 * @rdata_arg: The remote port that sent the RTV response
 *
 * Many targets don't seem to support this.
 *
 * Locking Note: This function will be called without the rport lock
 * held, but it will lock, call an _enter_* function or fc_rport_error
 * and then unlock the rport.
 */
static void fc_rport_rtv_resp(struct fc_seq *sp, struct fc_frame *fp,
			      void *rdata_arg)
{
	struct fc_rport_priv *rdata = rdata_arg;
	u8 op;

	FC_RPORT_DBG(rdata, "Received a RTV %s\n", fc_els_resp_type(fp));

	if (fp == ERR_PTR(-FC_EX_CLOSED))
		goto put;

	mutex_lock(&rdata->rp_mutex);

	if (rdata->rp_state != RPORT_ST_RTV) {
		FC_RPORT_DBG(rdata, "Received a RTV response, but in state "
			     "%s\n", fc_rport_state(rdata));
		if (IS_ERR(fp))
			goto err;
		goto out;
	}

	if (IS_ERR(fp)) {
		fc_rport_error(rdata, PTR_ERR(fp));
		goto err;
	}

	op = fc_frame_payload_op(fp);
	if (op == ELS_LS_ACC) {
		struct fc_els_rtv_acc *rtv;
		u32 toq;
		u32 tov;

		rtv = fc_frame_payload_get(fp, sizeof(*rtv));
		if (rtv) {
			toq = ntohl(rtv->rtv_toq);
			tov = ntohl(rtv->rtv_r_a_tov);
			if (tov == 0)
				tov = 1;
			if (tov > rdata->r_a_tov)
				rdata->r_a_tov = tov;
			tov = ntohl(rtv->rtv_e_d_tov);
			if (toq & FC_ELS_RTV_EDRES)
				tov /= 1000000;
			if (tov == 0)
				tov = 1;
			if (tov > rdata->e_d_tov)
				rdata->e_d_tov = tov;
		}
	}

	fc_rport_enter_ready(rdata);

out:
	fc_frame_free(fp);
err:
	mutex_unlock(&rdata->rp_mutex);
put:
	kref_put(&rdata->kref, fc_rport_destroy);
}

/**
 * fc_rport_enter_rtv() - Send Request Timeout Value (RTV) request
 * @rdata: The remote port to send the RTV request to
 *
 * Reference counting: increments kref when sending ELS
 */
static void fc_rport_enter_rtv(struct fc_rport_priv *rdata)
{
	struct fc_frame *fp;
	struct fc_lport *lport = rdata->local_port;

	lockdep_assert_held(&rdata->rp_mutex);

	FC_RPORT_DBG(rdata, "Port entered RTV state from %s state\n",
		     fc_rport_state(rdata));

	fc_rport_state_enter(rdata, RPORT_ST_RTV);

	fp = fc_frame_alloc(lport, sizeof(struct fc_els_rtv));
	if (!fp) {
		fc_rport_error_retry(rdata, -FC_EX_ALLOC_ERR);
		return;
	}

	kref_get(&rdata->kref);
	if (!lport->tt.elsct_send(lport, rdata->ids.port_id, fp, ELS_RTV,
				  fc_rport_rtv_resp, rdata,
				  2 * lport->r_a_tov)) {
		fc_rport_error_retry(rdata, -FC_EX_XMIT_ERR);
		kref_put(&rdata->kref, fc_rport_destroy);
	}
}

/**
 * fc_rport_recv_rtv_req() - Handler for Read Timeout Value (RTV) requests
 * @rdata: The remote port that sent the RTV request
 * @in_fp: The RTV request frame
 */
static void fc_rport_recv_rtv_req(struct fc_rport_priv *rdata,
				  struct fc_frame *in_fp)
{
	struct fc_lport *lport = rdata->local_port;
	struct fc_frame *fp;
	struct fc_els_rtv_acc *rtv;
	struct fc_seq_els_data rjt_data;

	lockdep_assert_held(&rdata->rp_mutex);
	lockdep_assert_held(&lport->lp_mutex);

	FC_RPORT_DBG(rdata, "Received RTV request\n");

	fp = fc_frame_alloc(lport, sizeof(*rtv));
	if (!fp) {
		rjt_data.reason = ELS_RJT_UNAB;
		rjt_data.explan = ELS_EXPL_INSUF_RES;
		fc_seq_els_rsp_send(in_fp, ELS_LS_RJT, &rjt_data);
		goto drop;
	}
	rtv = fc_frame_payload_get(fp, sizeof(*rtv));
	rtv->rtv_cmd = ELS_LS_ACC;
	rtv->rtv_r_a_tov = htonl(lport->r_a_tov);
	rtv->rtv_e_d_tov = htonl(lport->e_d_tov);
	rtv->rtv_toq = 0;
	fc_fill_reply_hdr(fp, in_fp, FC_RCTL_ELS_REP, 0);
	lport->tt.frame_send(lport, fp);
drop:
	fc_frame_free(in_fp);
}

/**
 * fc_rport_logo_resp() - Handler for logout (LOGO) responses
 * @sp:	       The sequence the LOGO was on
 * @fp:	       The LOGO response frame
 * @lport_arg: The local port
 */
static void fc_rport_logo_resp(struct fc_seq *sp, struct fc_frame *fp,
			       void *rdata_arg)
{
	struct fc_rport_priv *rdata = rdata_arg;
	struct fc_lport *lport = rdata->local_port;

	FC_RPORT_ID_DBG(lport, fc_seq_exch(sp)->did,
			"Received a LOGO %s\n", fc_els_resp_type(fp));
	if (!IS_ERR(fp))
		fc_frame_free(fp);
	kref_put(&rdata->kref, fc_rport_destroy);
}

/**
 * fc_rport_enter_logo() - Send a logout (LOGO) request
 * @rdata: The remote port to send the LOGO request to
 *
 * Reference counting: increments kref when sending ELS
 */
static void fc_rport_enter_logo(struct fc_rport_priv *rdata)
{
	struct fc_lport *lport = rdata->local_port;
	struct fc_frame *fp;

	lockdep_assert_held(&rdata->rp_mutex);

	FC_RPORT_DBG(rdata, "Port sending LOGO from %s state\n",
		     fc_rport_state(rdata));

	fp = fc_frame_alloc(lport, sizeof(struct fc_els_logo));
	if (!fp)
		return;
	kref_get(&rdata->kref);
	if (!lport->tt.elsct_send(lport, rdata->ids.port_id, fp, ELS_LOGO,
				  fc_rport_logo_resp, rdata, 0))
		kref_put(&rdata->kref, fc_rport_destroy);
}

/**
 * fc_rport_els_adisc_resp() - Handler for Address Discovery (ADISC) responses
 * @sp:	       The sequence the ADISC response was on
 * @fp:	       The ADISC response frame
 * @rdata_arg: The remote port that sent the ADISC response
 *
 * Locking Note: This function will be called without the rport lock
 * held, but it will lock, call an _enter_* function or fc_rport_error
 * and then unlock the rport.
 */
static void fc_rport_adisc_resp(struct fc_seq *sp, struct fc_frame *fp,
				void *rdata_arg)
{
	struct fc_rport_priv *rdata = rdata_arg;
	struct fc_els_adisc *adisc;
	u8 op;

	FC_RPORT_DBG(rdata, "Received a ADISC response\n");

	if (fp == ERR_PTR(-FC_EX_CLOSED))
		goto put;

	mutex_lock(&rdata->rp_mutex);

	if (rdata->rp_state != RPORT_ST_ADISC) {
		FC_RPORT_DBG(rdata, "Received a ADISC resp but in state %s\n",
			     fc_rport_state(rdata));
		if (IS_ERR(fp))
			goto err;
		goto out;
	}

	if (IS_ERR(fp)) {
		fc_rport_error(rdata, PTR_ERR(fp));
		goto err;
	}

	/*
	 * If address verification failed.  Consider us logged out of the rport.
	 * Since the rport is still in discovery, we want to be
	 * logged in, so go to PLOGI state.  Otherwise, go back to READY.
	 */
	op = fc_frame_payload_op(fp);
	adisc = fc_frame_payload_get(fp, sizeof(*adisc));
	if (op != ELS_LS_ACC || !adisc ||
	    ntoh24(adisc->adisc_port_id) != rdata->ids.port_id ||
	    get_unaligned_be64(&adisc->adisc_wwpn) != rdata->ids.port_name ||
	    get_unaligned_be64(&adisc->adisc_wwnn) != rdata->ids.node_name) {
		FC_RPORT_DBG(rdata, "ADISC error or mismatch\n");
		fc_rport_enter_flogi(rdata);
	} else {
		FC_RPORT_DBG(rdata, "ADISC OK\n");
		fc_rport_enter_ready(rdata);
	}
out:
	fc_frame_free(fp);
err:
	mutex_unlock(&rdata->rp_mutex);
put:
	kref_put(&rdata->kref, fc_rport_destroy);
}

/**
 * fc_rport_enter_adisc() - Send Address Discover (ADISC) request
 * @rdata: The remote port to send the ADISC request to
 *
 * Reference counting: increments kref when sending ELS
 */
static void fc_rport_enter_adisc(struct fc_rport_priv *rdata)
{
	struct fc_lport *lport = rdata->local_port;
	struct fc_frame *fp;

	lockdep_assert_held(&rdata->rp_mutex);

	FC_RPORT_DBG(rdata, "sending ADISC from %s state\n",
		     fc_rport_state(rdata));

	fc_rport_state_enter(rdata, RPORT_ST_ADISC);

	fp = fc_frame_alloc(lport, sizeof(struct fc_els_adisc));
	if (!fp) {
		fc_rport_error_retry(rdata, -FC_EX_ALLOC_ERR);
		return;
	}
	kref_get(&rdata->kref);
	if (!lport->tt.elsct_send(lport, rdata->ids.port_id, fp, ELS_ADISC,
				  fc_rport_adisc_resp, rdata,
				  2 * lport->r_a_tov)) {
		fc_rport_error_retry(rdata, -FC_EX_XMIT_ERR);
		kref_put(&rdata->kref, fc_rport_destroy);
	}
}

/**
 * fc_rport_recv_adisc_req() - Handler for Address Discovery (ADISC) requests
 * @rdata: The remote port that sent the ADISC request
 * @in_fp: The ADISC request frame
 */
static void fc_rport_recv_adisc_req(struct fc_rport_priv *rdata,
				    struct fc_frame *in_fp)
{
	struct fc_lport *lport = rdata->local_port;
	struct fc_frame *fp;
	struct fc_els_adisc *adisc;
	struct fc_seq_els_data rjt_data;

	lockdep_assert_held(&rdata->rp_mutex);
	lockdep_assert_held(&lport->lp_mutex);

	FC_RPORT_DBG(rdata, "Received ADISC request\n");

	adisc = fc_frame_payload_get(in_fp, sizeof(*adisc));
	if (!adisc) {
		rjt_data.reason = ELS_RJT_PROT;
		rjt_data.explan = ELS_EXPL_INV_LEN;
		fc_seq_els_rsp_send(in_fp, ELS_LS_RJT, &rjt_data);
		goto drop;
	}

	fp = fc_frame_alloc(lport, sizeof(*adisc));
	if (!fp)
		goto drop;
	fc_adisc_fill(lport, fp);
	adisc = fc_frame_payload_get(fp, sizeof(*adisc));
	adisc->adisc_cmd = ELS_LS_ACC;
	fc_fill_reply_hdr(fp, in_fp, FC_RCTL_ELS_REP, 0);
	lport->tt.frame_send(lport, fp);
drop:
	fc_frame_free(in_fp);
}

/**
 * fc_rport_recv_rls_req() - Handle received Read Link Status request
 * @rdata: The remote port that sent the RLS request
 * @rx_fp: The PRLI request frame
 */
static void fc_rport_recv_rls_req(struct fc_rport_priv *rdata,
				  struct fc_frame *rx_fp)

{
	struct fc_lport *lport = rdata->local_port;
	struct fc_frame *fp;
	struct fc_els_rls *rls;
	struct fc_els_rls_resp *rsp;
	struct fc_els_lesb *lesb;
	struct fc_seq_els_data rjt_data;
	struct fc_host_statistics *hst;

	lockdep_assert_held(&rdata->rp_mutex);

	FC_RPORT_DBG(rdata, "Received RLS request while in state %s\n",
		     fc_rport_state(rdata));

	rls = fc_frame_payload_get(rx_fp, sizeof(*rls));
	if (!rls) {
		rjt_data.reason = ELS_RJT_PROT;
		rjt_data.explan = ELS_EXPL_INV_LEN;
		goto out_rjt;
	}

	fp = fc_frame_alloc(lport, sizeof(*rsp));
	if (!fp) {
		rjt_data.reason = ELS_RJT_UNAB;
		rjt_data.explan = ELS_EXPL_INSUF_RES;
		goto out_rjt;
	}

	rsp = fc_frame_payload_get(fp, sizeof(*rsp));
	memset(rsp, 0, sizeof(*rsp));
	rsp->rls_cmd = ELS_LS_ACC;
	lesb = &rsp->rls_lesb;
	if (lport->tt.get_lesb) {
		/* get LESB from LLD if it supports it */
		lport->tt.get_lesb(lport, lesb);
	} else {
		fc_get_host_stats(lport->host);
		hst = &lport->host_stats;
		lesb->lesb_link_fail = htonl(hst->link_failure_count);
		lesb->lesb_sync_loss = htonl(hst->loss_of_sync_count);
		lesb->lesb_sig_loss = htonl(hst->loss_of_signal_count);
		lesb->lesb_prim_err = htonl(hst->prim_seq_protocol_err_count);
		lesb->lesb_inv_word = htonl(hst->invalid_tx_word_count);
		lesb->lesb_inv_crc = htonl(hst->invalid_crc_count);
	}

	fc_fill_reply_hdr(fp, rx_fp, FC_RCTL_ELS_REP, 0);
	lport->tt.frame_send(lport, fp);
	goto out;

out_rjt:
	fc_seq_els_rsp_send(rx_fp, ELS_LS_RJT, &rjt_data);
out:
	fc_frame_free(rx_fp);
}

/**
 * fc_rport_recv_els_req() - Handler for validated ELS requests
 * @lport: The local port that received the ELS request
 * @fp:	   The ELS request frame
 *
 * Handle incoming ELS requests that require port login.
 * The ELS opcode has already been validated by the caller.
 *
 * Reference counting: does not modify kref
 */
static void fc_rport_recv_els_req(struct fc_lport *lport, struct fc_frame *fp)
{
	struct fc_rport_priv *rdata;
	struct fc_seq_els_data els_data;

	lockdep_assert_held(&lport->lp_mutex);

	rdata = fc_rport_lookup(lport, fc_frame_sid(fp));
	if (!rdata) {
		FC_RPORT_ID_DBG(lport, fc_frame_sid(fp),
				"Received ELS 0x%02x from non-logged-in port\n",
				fc_frame_payload_op(fp));
		goto reject;
	}

	mutex_lock(&rdata->rp_mutex);

	switch (rdata->rp_state) {
	case RPORT_ST_PRLI:
	case RPORT_ST_RTV:
	case RPORT_ST_READY:
	case RPORT_ST_ADISC:
		break;
	case RPORT_ST_PLOGI:
		if (fc_frame_payload_op(fp) == ELS_PRLI) {
			FC_RPORT_DBG(rdata, "Reject ELS PRLI "
				     "while in state %s\n",
				     fc_rport_state(rdata));
			mutex_unlock(&rdata->rp_mutex);
			kref_put(&rdata->kref, fc_rport_destroy);
			goto busy;
		}
	default:
		FC_RPORT_DBG(rdata,
			     "Reject ELS 0x%02x while in state %s\n",
			     fc_frame_payload_op(fp), fc_rport_state(rdata));
		mutex_unlock(&rdata->rp_mutex);
		kref_put(&rdata->kref, fc_rport_destroy);
		goto reject;
	}

	switch (fc_frame_payload_op(fp)) {
	case ELS_PRLI:
		fc_rport_recv_prli_req(rdata, fp);
		break;
	case ELS_PRLO:
		fc_rport_recv_prlo_req(rdata, fp);
		break;
	case ELS_ADISC:
		fc_rport_recv_adisc_req(rdata, fp);
		break;
	case ELS_RRQ:
		fc_seq_els_rsp_send(fp, ELS_RRQ, NULL);
		fc_frame_free(fp);
		break;
	case ELS_REC:
		fc_seq_els_rsp_send(fp, ELS_REC, NULL);
		fc_frame_free(fp);
		break;
	case ELS_RLS:
		fc_rport_recv_rls_req(rdata, fp);
		break;
	case ELS_RTV:
		fc_rport_recv_rtv_req(rdata, fp);
		break;
	default:
		fc_frame_free(fp);	/* can't happen */
		break;
	}

	mutex_unlock(&rdata->rp_mutex);
	kref_put(&rdata->kref, fc_rport_destroy);
	return;

reject:
	els_data.reason = ELS_RJT_UNAB;
	els_data.explan = ELS_EXPL_PLOGI_REQD;
	fc_seq_els_rsp_send(fp, ELS_LS_RJT, &els_data);
	fc_frame_free(fp);
	return;

busy:
	els_data.reason = ELS_RJT_BUSY;
	els_data.explan = ELS_EXPL_NONE;
	fc_seq_els_rsp_send(fp, ELS_LS_RJT, &els_data);
	fc_frame_free(fp);
	return;
}

/**
 * fc_rport_recv_req() - Handler for requests
 * @lport: The local port that received the request
 * @fp:	   The request frame
 *
 * Reference counting: does not modify kref
 */
void fc_rport_recv_req(struct fc_lport *lport, struct fc_frame *fp)
{
	struct fc_seq_els_data els_data;

	lockdep_assert_held(&lport->lp_mutex);

	/*
	 * Handle FLOGI, PLOGI and LOGO requests separately, since they
	 * don't require prior login.
	 * Check for unsupported opcodes first and reject them.
	 * For some ops, it would be incorrect to reject with "PLOGI required".
	 */
	switch (fc_frame_payload_op(fp)) {
	case ELS_FLOGI:
		fc_rport_recv_flogi_req(lport, fp);
		break;
	case ELS_PLOGI:
		fc_rport_recv_plogi_req(lport, fp);
		break;
	case ELS_LOGO:
		fc_rport_recv_logo_req(lport, fp);
		break;
	case ELS_PRLI:
	case ELS_PRLO:
	case ELS_ADISC:
	case ELS_RRQ:
	case ELS_REC:
	case ELS_RLS:
	case ELS_RTV:
		fc_rport_recv_els_req(lport, fp);
		break;
	default:
		els_data.reason = ELS_RJT_UNSUP;
		els_data.explan = ELS_EXPL_NONE;
		fc_seq_els_rsp_send(fp, ELS_LS_RJT, &els_data);
		fc_frame_free(fp);
		break;
	}
}
EXPORT_SYMBOL(fc_rport_recv_req);

/**
 * fc_rport_recv_plogi_req() - Handler for Port Login (PLOGI) requests
 * @lport: The local port that received the PLOGI request
 * @rx_fp: The PLOGI request frame
 *
 * Reference counting: increments kref on return
 */
static void fc_rport_recv_plogi_req(struct fc_lport *lport,
				    struct fc_frame *rx_fp)
{
	struct fc_disc *disc;
	struct fc_rport_priv *rdata;
	struct fc_frame *fp = rx_fp;
	struct fc_els_flogi *pl;
	struct fc_seq_els_data rjt_data;
	u32 sid;

	lockdep_assert_held(&lport->lp_mutex);

	sid = fc_frame_sid(fp);

	FC_RPORT_ID_DBG(lport, sid, "Received PLOGI request\n");

	pl = fc_frame_payload_get(fp, sizeof(*pl));
	if (!pl) {
		FC_RPORT_ID_DBG(lport, sid, "Received PLOGI too short\n");
		rjt_data.reason = ELS_RJT_PROT;
		rjt_data.explan = ELS_EXPL_INV_LEN;
		goto reject;
	}

	disc = &lport->disc;
	mutex_lock(&disc->disc_mutex);
	rdata = fc_rport_create(lport, sid);
	if (!rdata) {
		mutex_unlock(&disc->disc_mutex);
		rjt_data.reason = ELS_RJT_UNAB;
		rjt_data.explan = ELS_EXPL_INSUF_RES;
		goto reject;
	}

	mutex_lock(&rdata->rp_mutex);
	mutex_unlock(&disc->disc_mutex);

	rdata->ids.port_name = get_unaligned_be64(&pl->fl_wwpn);
	rdata->ids.node_name = get_unaligned_be64(&pl->fl_wwnn);

	/*
	 * If the rport was just created, possibly due to the incoming PLOGI,
	 * set the state appropriately and accept the PLOGI.
	 *
	 * If we had also sent a PLOGI, and if the received PLOGI is from a
	 * higher WWPN, we accept it, otherwise an LS_RJT is sent with reason
	 * "command already in progress".
	 *
	 * XXX TBD: If the session was ready before, the PLOGI should result in
	 * all outstanding exchanges being reset.
	 */
	switch (rdata->rp_state) {
	case RPORT_ST_INIT:
		FC_RPORT_DBG(rdata, "Received PLOGI in INIT state\n");
		break;
	case RPORT_ST_PLOGI_WAIT:
		FC_RPORT_DBG(rdata, "Received PLOGI in PLOGI_WAIT state\n");
		break;
	case RPORT_ST_PLOGI:
		FC_RPORT_DBG(rdata, "Received PLOGI in PLOGI state\n");
		if (rdata->ids.port_name < lport->wwpn) {
			mutex_unlock(&rdata->rp_mutex);
			rjt_data.reason = ELS_RJT_INPROG;
			rjt_data.explan = ELS_EXPL_NONE;
			goto reject;
		}
		break;
	case RPORT_ST_PRLI:
	case RPORT_ST_RTV:
	case RPORT_ST_READY:
	case RPORT_ST_ADISC:
		FC_RPORT_DBG(rdata, "Received PLOGI in logged-in state %d "
			     "- ignored for now\n", rdata->rp_state);
		/* XXX TBD - should reset */
		break;
	case RPORT_ST_FLOGI:
	case RPORT_ST_DELETE:
		FC_RPORT_DBG(rdata, "Received PLOGI in state %s - send busy\n",
			     fc_rport_state(rdata));
		mutex_unlock(&rdata->rp_mutex);
		rjt_data.reason = ELS_RJT_BUSY;
		rjt_data.explan = ELS_EXPL_NONE;
		goto reject;
	}
	if (!fc_rport_compatible_roles(lport, rdata)) {
		FC_RPORT_DBG(rdata, "Received PLOGI for incompatible role\n");
		mutex_unlock(&rdata->rp_mutex);
		rjt_data.reason = ELS_RJT_LOGIC;
		rjt_data.explan = ELS_EXPL_NONE;
		goto reject;
	}

	/*
	 * Get session payload size from incoming PLOGI.
	 */
	rdata->maxframe_size = fc_plogi_get_maxframe(pl, lport->mfs);

	/*
	 * Send LS_ACC.	 If this fails, the originator should retry.
	 */
	fp = fc_frame_alloc(lport, sizeof(*pl));
	if (!fp)
		goto out;

	fc_plogi_fill(lport, fp, ELS_LS_ACC);
	fc_fill_reply_hdr(fp, rx_fp, FC_RCTL_ELS_REP, 0);
	lport->tt.frame_send(lport, fp);
	fc_rport_enter_prli(rdata);
out:
	mutex_unlock(&rdata->rp_mutex);
	fc_frame_free(rx_fp);
	return;

reject:
	fc_seq_els_rsp_send(fp, ELS_LS_RJT, &rjt_data);
	fc_frame_free(fp);
}

/**
 * fc_rport_recv_prli_req() - Handler for process login (PRLI) requests
 * @rdata: The remote port that sent the PRLI request
 * @rx_fp: The PRLI request frame
 */
static void fc_rport_recv_prli_req(struct fc_rport_priv *rdata,
				   struct fc_frame *rx_fp)
{
	struct fc_lport *lport = rdata->local_port;
	struct fc_frame *fp;
	struct {
		struct fc_els_prli prli;
		struct fc_els_spp spp;
	} *pp;
	struct fc_els_spp *rspp;	/* request service param page */
	struct fc_els_spp *spp;	/* response spp */
	unsigned int len;
	unsigned int plen;
	enum fc_els_spp_resp resp;
	struct fc_seq_els_data rjt_data;
	struct fc4_prov *prov;

	lockdep_assert_held(&rdata->rp_mutex);

	FC_RPORT_DBG(rdata, "Received PRLI request while in state %s\n",
		     fc_rport_state(rdata));

	len = fr_len(rx_fp) - sizeof(struct fc_frame_header);
	pp = fc_frame_payload_get(rx_fp, sizeof(*pp));
	if (!pp)
		goto reject_len;
	plen = ntohs(pp->prli.prli_len);
	if ((plen % 4) != 0 || plen > len || plen < 16)
		goto reject_len;
	if (plen < len)
		len = plen;
	plen = pp->prli.prli_spp_len;
	if ((plen % 4) != 0 || plen < sizeof(*spp) ||
	    plen > len || len < sizeof(*pp) || plen < 12)
		goto reject_len;
	rspp = &pp->spp;

	fp = fc_frame_alloc(lport, len);
	if (!fp) {
		rjt_data.reason = ELS_RJT_UNAB;
		rjt_data.explan = ELS_EXPL_INSUF_RES;
		goto reject;
	}
	pp = fc_frame_payload_get(fp, len);
	WARN_ON(!pp);
	memset(pp, 0, len);
	pp->prli.prli_cmd = ELS_LS_ACC;
	pp->prli.prli_spp_len = plen;
	pp->prli.prli_len = htons(len);
	len -= sizeof(struct fc_els_prli);

	/*
	 * Go through all the service parameter pages and build
	 * response.  If plen indicates longer SPP than standard,
	 * use that.  The entire response has been pre-cleared above.
	 */
	spp = &pp->spp;
	mutex_lock(&fc_prov_mutex);
	while (len >= plen) {
		rdata->spp_type = rspp->spp_type;
		spp->spp_type = rspp->spp_type;
		spp->spp_type_ext = rspp->spp_type_ext;
		resp = 0;

		if (rspp->spp_type < FC_FC4_PROV_SIZE) {
			enum fc_els_spp_resp active = 0, passive = 0;

			prov = fc_active_prov[rspp->spp_type];
			if (prov)
				active = prov->prli(rdata, plen, rspp, spp);
			prov = fc_passive_prov[rspp->spp_type];
			if (prov)
				passive = prov->prli(rdata, plen, rspp, spp);
			if (!active || passive == FC_SPP_RESP_ACK)
				resp = passive;
			else
				resp = active;
			FC_RPORT_DBG(rdata, "PRLI rspp type %x "
				     "active %x passive %x\n",
				     rspp->spp_type, active, passive);
		}
		if (!resp) {
			if (spp->spp_flags & FC_SPP_EST_IMG_PAIR)
				resp |= FC_SPP_RESP_CONF;
			else
				resp |= FC_SPP_RESP_INVL;
		}
		spp->spp_flags |= resp;
		len -= plen;
		rspp = (struct fc_els_spp *)((char *)rspp + plen);
		spp = (struct fc_els_spp *)((char *)spp + plen);
	}
	mutex_unlock(&fc_prov_mutex);

	/*
	 * Send LS_ACC.	 If this fails, the originator should retry.
	 */
	fc_fill_reply_hdr(fp, rx_fp, FC_RCTL_ELS_REP, 0);
	lport->tt.frame_send(lport, fp);

	goto drop;

reject_len:
	rjt_data.reason = ELS_RJT_PROT;
	rjt_data.explan = ELS_EXPL_INV_LEN;
reject:
	fc_seq_els_rsp_send(rx_fp, ELS_LS_RJT, &rjt_data);
drop:
	fc_frame_free(rx_fp);
}

/**
 * fc_rport_recv_prlo_req() - Handler for process logout (PRLO) requests
 * @rdata: The remote port that sent the PRLO request
 * @rx_fp: The PRLO request frame
 */
static void fc_rport_recv_prlo_req(struct fc_rport_priv *rdata,
				   struct fc_frame *rx_fp)
{
	struct fc_lport *lport = rdata->local_port;
	struct fc_frame *fp;
	struct {
		struct fc_els_prlo prlo;
		struct fc_els_spp spp;
	} *pp;
	struct fc_els_spp *rspp;	/* request service param page */
	struct fc_els_spp *spp;		/* response spp */
	unsigned int len;
	unsigned int plen;
	struct fc_seq_els_data rjt_data;

	lockdep_assert_held(&rdata->rp_mutex);

	FC_RPORT_DBG(rdata, "Received PRLO request while in state %s\n",
		     fc_rport_state(rdata));

	len = fr_len(rx_fp) - sizeof(struct fc_frame_header);
	pp = fc_frame_payload_get(rx_fp, sizeof(*pp));
	if (!pp)
		goto reject_len;
	plen = ntohs(pp->prlo.prlo_len);
	if (plen != 20)
		goto reject_len;
	if (plen < len)
		len = plen;

	rspp = &pp->spp;

	fp = fc_frame_alloc(lport, len);
	if (!fp) {
		rjt_data.reason = ELS_RJT_UNAB;
		rjt_data.explan = ELS_EXPL_INSUF_RES;
		goto reject;
	}

	pp = fc_frame_payload_get(fp, len);
	WARN_ON(!pp);
	memset(pp, 0, len);
	pp->prlo.prlo_cmd = ELS_LS_ACC;
	pp->prlo.prlo_obs = 0x10;
	pp->prlo.prlo_len = htons(len);
	spp = &pp->spp;
	spp->spp_type = rspp->spp_type;
	spp->spp_type_ext = rspp->spp_type_ext;
	spp->spp_flags = FC_SPP_RESP_ACK;

	fc_rport_enter_prli(rdata);

	fc_fill_reply_hdr(fp, rx_fp, FC_RCTL_ELS_REP, 0);
	lport->tt.frame_send(lport, fp);
	goto drop;

reject_len:
	rjt_data.reason = ELS_RJT_PROT;
	rjt_data.explan = ELS_EXPL_INV_LEN;
reject:
	fc_seq_els_rsp_send(rx_fp, ELS_LS_RJT, &rjt_data);
drop:
	fc_frame_free(rx_fp);
}

/**
 * fc_rport_recv_logo_req() - Handler for logout (LOGO) requests
 * @lport: The local port that received the LOGO request
 * @fp:	   The LOGO request frame
 *
 * Reference counting: drops kref on return
 */
static void fc_rport_recv_logo_req(struct fc_lport *lport, struct fc_frame *fp)
{
	struct fc_rport_priv *rdata;
	u32 sid;

	lockdep_assert_held(&lport->lp_mutex);

	fc_seq_els_rsp_send(fp, ELS_LS_ACC, NULL);

	sid = fc_frame_sid(fp);

	rdata = fc_rport_lookup(lport, sid);
	if (rdata) {
		mutex_lock(&rdata->rp_mutex);
		FC_RPORT_DBG(rdata, "Received LOGO request while in state %s\n",
			     fc_rport_state(rdata));

		fc_rport_enter_delete(rdata, RPORT_EV_STOP);
		mutex_unlock(&rdata->rp_mutex);
		kref_put(&rdata->kref, fc_rport_destroy);
	} else
		FC_RPORT_ID_DBG(lport, sid,
				"Received LOGO from non-logged-in port\n");
	fc_frame_free(fp);
}

/**
 * fc_rport_flush_queue() - Flush the rport_event_queue
 */
void fc_rport_flush_queue(void)
{
	flush_workqueue(rport_event_queue);
}
EXPORT_SYMBOL(fc_rport_flush_queue);

/**
 * fc_rport_fcp_prli() - Handle incoming PRLI for the FCP initiator.
 * @rdata: remote port private
 * @spp_len: service parameter page length
 * @rspp: received service parameter page
 * @spp: response service parameter page
 *
 * Returns the value for the response code to be placed in spp_flags;
 * Returns 0 if not an initiator.
 */
static int fc_rport_fcp_prli(struct fc_rport_priv *rdata, u32 spp_len,
			     const struct fc_els_spp *rspp,
			     struct fc_els_spp *spp)
{
	struct fc_lport *lport = rdata->local_port;
	u32 fcp_parm;

	fcp_parm = ntohl(rspp->spp_params);
	rdata->ids.roles = FC_RPORT_ROLE_UNKNOWN;
	if (fcp_parm & FCP_SPPF_INIT_FCN)
		rdata->ids.roles |= FC_RPORT_ROLE_FCP_INITIATOR;
	if (fcp_parm & FCP_SPPF_TARG_FCN)
		rdata->ids.roles |= FC_RPORT_ROLE_FCP_TARGET;
	if (fcp_parm & FCP_SPPF_RETRY)
		rdata->flags |= FC_RP_FLAGS_RETRY;
	rdata->supported_classes = FC_COS_CLASS3;

	if (!(lport->service_params & FCP_SPPF_INIT_FCN))
		return 0;

	spp->spp_flags |= rspp->spp_flags & FC_SPP_EST_IMG_PAIR;

	/*
	 * OR in our service parameters with other providers (target), if any.
	 */
	fcp_parm = ntohl(spp->spp_params);
	spp->spp_params = htonl(fcp_parm | lport->service_params);
	return FC_SPP_RESP_ACK;
}

/*
 * FC-4 provider ops for FCP initiator.
 */
struct fc4_prov fc_rport_fcp_init = {
	.prli = fc_rport_fcp_prli,
};

/**
 * fc_rport_t0_prli() - Handle incoming PRLI parameters for type 0
 * @rdata: remote port private
 * @spp_len: service parameter page length
 * @rspp: received service parameter page
 * @spp: response service parameter page
 */
static int fc_rport_t0_prli(struct fc_rport_priv *rdata, u32 spp_len,
			    const struct fc_els_spp *rspp,
			    struct fc_els_spp *spp)
{
	if (rspp->spp_flags & FC_SPP_EST_IMG_PAIR)
		return FC_SPP_RESP_INVL;
	return FC_SPP_RESP_ACK;
}

/*
 * FC-4 provider ops for type 0 service parameters.
 *
 * This handles the special case of type 0 which is always successful
 * but doesn't do anything otherwise.
 */
struct fc4_prov fc_rport_t0_prov = {
	.prli = fc_rport_t0_prli,
};

/**
 * fc_setup_rport() - Initialize the rport_event_queue
 */
int fc_setup_rport(void)
{
	rport_event_queue = create_singlethread_workqueue("fc_rport_eq");
	if (!rport_event_queue)
		return -ENOMEM;
	return 0;
}

/**
 * fc_destroy_rport() - Destroy the rport_event_queue
 */
void fc_destroy_rport(void)
{
	destroy_workqueue(rport_event_queue);
}

/**
 * fc_rport_terminate_io() - Stop all outstanding I/O on a remote port
 * @rport: The remote port whose I/O should be terminated
 */
void fc_rport_terminate_io(struct fc_rport *rport)
{
	struct fc_rport_libfc_priv *rpriv = rport->dd_data;
	struct fc_lport *lport = rpriv->local_port;

	lport->tt.exch_mgr_reset(lport, 0, rport->port_id);
	lport->tt.exch_mgr_reset(lport, rport->port_id, 0);
}
EXPORT_SYMBOL(fc_rport_terminate_io);

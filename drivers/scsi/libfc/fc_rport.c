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
 * more comments on the heirarchy.
 *
 * The locking strategy is similar to the lport's strategy. The lock protects
 * the rport's states and is held and released by the entry points to the rport
 * block. All _enter_* functions correspond to rport states and expect the rport
 * mutex to be locked before calling them. This means that rports only handle
 * one request or response at a time, since they're not critical for the I/O
 * path this potential over-use of the mutex is acceptable.
 */

#include <linux/kernel.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/rcupdate.h>
#include <linux/timer.h>
#include <linux/workqueue.h>
#include <asm/unaligned.h>

#include <scsi/libfc.h>
#include <scsi/fc_encode.h>

#include "fc_libfc.h"

struct workqueue_struct *rport_event_queue;

static void fc_rport_enter_plogi(struct fc_rport_priv *);
static void fc_rport_enter_prli(struct fc_rport_priv *);
static void fc_rport_enter_rtv(struct fc_rport_priv *);
static void fc_rport_enter_ready(struct fc_rport_priv *);
static void fc_rport_enter_logo(struct fc_rport_priv *);
static void fc_rport_enter_adisc(struct fc_rport_priv *);

static void fc_rport_recv_plogi_req(struct fc_lport *,
				    struct fc_seq *, struct fc_frame *);
static void fc_rport_recv_prli_req(struct fc_rport_priv *,
				   struct fc_seq *, struct fc_frame *);
static void fc_rport_recv_prlo_req(struct fc_rport_priv *,
				   struct fc_seq *, struct fc_frame *);
static void fc_rport_recv_logo_req(struct fc_lport *,
				   struct fc_seq *, struct fc_frame *);
static void fc_rport_timeout(struct work_struct *);
static void fc_rport_error(struct fc_rport_priv *, struct fc_frame *);
static void fc_rport_error_retry(struct fc_rport_priv *, struct fc_frame *);
static void fc_rport_work(struct work_struct *);

static const char *fc_rport_state_names[] = {
	[RPORT_ST_INIT] = "Init",
	[RPORT_ST_PLOGI] = "PLOGI",
	[RPORT_ST_PRLI] = "PRLI",
	[RPORT_ST_RTV] = "RTV",
	[RPORT_ST_READY] = "Ready",
	[RPORT_ST_LOGO] = "LOGO",
	[RPORT_ST_ADISC] = "ADISC",
	[RPORT_ST_DELETE] = "Delete",
	[RPORT_ST_RESTART] = "Restart",
};

/**
 * fc_rport_lookup() - Lookup a remote port by port_id
 * @lport:   The local port to lookup the remote port on
 * @port_id: The remote port ID to look up
 */
static struct fc_rport_priv *fc_rport_lookup(const struct fc_lport *lport,
					     u32 port_id)
{
	struct fc_rport_priv *rdata;

	list_for_each_entry(rdata, &lport->disc.rports, peers)
		if (rdata->ids.port_id == port_id)
			return rdata;
	return NULL;
}

/**
 * fc_rport_create() - Create a new remote port
 * @lport: The local port this remote port will be associated with
 * @ids:   The identifiers for the new remote port
 *
 * The remote port will start in the INIT state.
 *
 * Locking note:  must be called with the disc_mutex held.
 */
static struct fc_rport_priv *fc_rport_create(struct fc_lport *lport,
					     u32 port_id)
{
	struct fc_rport_priv *rdata;

	rdata = lport->tt.rport_lookup(lport, port_id);
	if (rdata)
		return rdata;

	rdata = kzalloc(sizeof(*rdata), GFP_KERNEL);
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
	if (port_id != FC_FID_DIR_SERV)
		list_add(&rdata->peers, &lport->disc.rports);
	return rdata;
}

/**
 * fc_rport_destroy() - Free a remote port after last reference is released
 * @kref: The remote port's kref
 */
static void fc_rport_destroy(struct kref *kref)
{
	struct fc_rport_priv *rdata;

	rdata = container_of(kref, struct fc_rport_priv, kref);
	kfree(rdata);
}

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
		rport->dev_loss_tmo = timeout + 5;
	else
		rport->dev_loss_tmo = 30;
}
EXPORT_SYMBOL(fc_set_rport_loss_tmo);

/**
 * fc_plogi_get_maxframe() - Get the maximum payload from the common service
 *			     parameters in a FLOGI frame
 * @flp:    The FLOGI payload
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
 *
 * Locking Note: Called with the rport lock held
 */
static void fc_rport_state_enter(struct fc_rport_priv *rdata,
				 enum fc_rport_state new)
{
	if (rdata->rp_state != new)
		rdata->retries = 0;
	rdata->rp_state = new;
}

/**
 * fc_rport_work() - Handler for remote port events in the rport_event_queue
 * @work: Handle to the remote port being dequeued
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
	int restart = 0;

	mutex_lock(&rdata->rp_mutex);
	event = rdata->event;
	rport_ops = rdata->ops;
	rport = rdata->rport;

	FC_RPORT_DBG(rdata, "work event %u\n", event);

	switch (event) {
	case RPORT_EV_READY:
		ids = rdata->ids;
		rdata->event = RPORT_EV_NONE;
		kref_get(&rdata->kref);
		mutex_unlock(&rdata->rp_mutex);

		if (!rport)
			rport = fc_remote_port_add(lport->host, 0, &ids);
		if (!rport) {
			FC_RPORT_DBG(rdata, "Failed to add the rport\n");
			lport->tt.rport_logoff(rdata);
			kref_put(&rdata->kref, lport->tt.rport_destroy);
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
		kref_put(&rdata->kref, lport->tt.rport_destroy);
		break;

	case RPORT_EV_FAILED:
	case RPORT_EV_LOGO:
	case RPORT_EV_STOP:
		port_id = rdata->ids.port_id;
		mutex_unlock(&rdata->rp_mutex);

		if (port_id != FC_FID_DIR_SERV) {
			/*
			 * We must drop rp_mutex before taking disc_mutex.
			 * Re-evaluate state to allow for restart.
			 * A transition to RESTART state must only happen
			 * while disc_mutex is held and rdata is on the list.
			 */
			mutex_lock(&lport->disc.disc_mutex);
			mutex_lock(&rdata->rp_mutex);
			if (rdata->rp_state == RPORT_ST_RESTART)
				restart = 1;
			else
				list_del(&rdata->peers);
			rdata->event = RPORT_EV_NONE;
			mutex_unlock(&rdata->rp_mutex);
			mutex_unlock(&lport->disc.disc_mutex);
		}

		if (rport_ops && rport_ops->event_callback) {
			FC_RPORT_DBG(rdata, "callback ev %d\n", event);
			rport_ops->event_callback(lport, rdata, event);
		}
		cancel_delayed_work_sync(&rdata->retry_work);

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
		if (restart) {
			mutex_lock(&rdata->rp_mutex);
			FC_RPORT_DBG(rdata, "work restart\n");
			fc_rport_enter_plogi(rdata);
			mutex_unlock(&rdata->rp_mutex);
		} else
			kref_put(&rdata->kref, lport->tt.rport_destroy);
		break;

	default:
		mutex_unlock(&rdata->rp_mutex);
		break;
	}
}

/**
 * fc_rport_login() - Start the remote port login state machine
 * @rdata: The remote port to be logged in to
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

	switch (rdata->rp_state) {
	case RPORT_ST_READY:
		FC_RPORT_DBG(rdata, "ADISC port\n");
		fc_rport_enter_adisc(rdata);
		break;
	case RPORT_ST_RESTART:
		break;
	case RPORT_ST_DELETE:
		FC_RPORT_DBG(rdata, "Restart deleted port\n");
		fc_rport_state_enter(rdata, RPORT_ST_RESTART);
		break;
	default:
		FC_RPORT_DBG(rdata, "Login to port\n");
		fc_rport_enter_plogi(rdata);
		break;
	}
	mutex_unlock(&rdata->rp_mutex);

	return 0;
}

/**
 * fc_rport_enter_delete() - Schedule a remote port to be deleted
 * @rdata: The remote port to be deleted
 * @event: The event to report as the reason for deletion
 *
 * Locking Note: Called with the rport lock held.
 *
 * Allow state change into DELETE only once.
 *
 * Call queue_work only if there's no event already pending.
 * Set the new event so that the old pending event will not occur.
 * Since we have the mutex, even if fc_rport_work() is already started,
 * it'll see the new event.
 */
static void fc_rport_enter_delete(struct fc_rport_priv *rdata,
				  enum fc_rport_event event)
{
	if (rdata->rp_state == RPORT_ST_DELETE)
		return;

	FC_RPORT_DBG(rdata, "Delete port\n");

	fc_rport_state_enter(rdata, RPORT_ST_DELETE);

	if (rdata->event == RPORT_EV_NONE)
		queue_work(rport_event_queue, &rdata->event_work);
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
	mutex_lock(&rdata->rp_mutex);

	FC_RPORT_DBG(rdata, "Remove port\n");

	if (rdata->rp_state == RPORT_ST_DELETE) {
		FC_RPORT_DBG(rdata, "Port in Delete state, not removing\n");
		goto out;
	}

	if (rdata->rp_state == RPORT_ST_RESTART)
		FC_RPORT_DBG(rdata, "Port in Restart state, deleting\n");
	else
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

/**
 * fc_rport_enter_ready() - Transition to the RPORT_ST_READY state
 * @rdata: The remote port that is ready
 *
 * Locking Note: The rport lock is expected to be held before calling
 * this routine.
 */
static void fc_rport_enter_ready(struct fc_rport_priv *rdata)
{
	fc_rport_state_enter(rdata, RPORT_ST_READY);

	FC_RPORT_DBG(rdata, "Port is Ready\n");

	if (rdata->event == RPORT_EV_NONE)
		queue_work(rport_event_queue, &rdata->event_work);
	rdata->event = RPORT_EV_READY;
}

/**
 * fc_rport_timeout() - Handler for the retry_work timer
 * @work: Handle to the remote port that has timed out
 *
 * Locking Note: Called without the rport lock held. This
 * function will hold the rport lock, call an _enter_*
 * function and then unlock the rport.
 */
static void fc_rport_timeout(struct work_struct *work)
{
	struct fc_rport_priv *rdata =
		container_of(work, struct fc_rport_priv, retry_work.work);

	mutex_lock(&rdata->rp_mutex);

	switch (rdata->rp_state) {
	case RPORT_ST_PLOGI:
		fc_rport_enter_plogi(rdata);
		break;
	case RPORT_ST_PRLI:
		fc_rport_enter_prli(rdata);
		break;
	case RPORT_ST_RTV:
		fc_rport_enter_rtv(rdata);
		break;
	case RPORT_ST_LOGO:
		fc_rport_enter_logo(rdata);
		break;
	case RPORT_ST_ADISC:
		fc_rport_enter_adisc(rdata);
		break;
	case RPORT_ST_READY:
	case RPORT_ST_INIT:
	case RPORT_ST_DELETE:
	case RPORT_ST_RESTART:
		break;
	}

	mutex_unlock(&rdata->rp_mutex);
}

/**
 * fc_rport_error() - Error handler, called once retries have been exhausted
 * @rdata: The remote port the error is happened on
 * @fp:	   The error code encapsulated in a frame pointer
 *
 * Locking Note: The rport lock is expected to be held before
 * calling this routine
 */
static void fc_rport_error(struct fc_rport_priv *rdata, struct fc_frame *fp)
{
	FC_RPORT_DBG(rdata, "Error %ld in state %s, retries %d\n",
		     IS_ERR(fp) ? -PTR_ERR(fp) : 0,
		     fc_rport_state(rdata), rdata->retries);

	switch (rdata->rp_state) {
	case RPORT_ST_PLOGI:
	case RPORT_ST_LOGO:
		fc_rport_enter_delete(rdata, RPORT_EV_FAILED);
		break;
	case RPORT_ST_RTV:
		fc_rport_enter_ready(rdata);
		break;
	case RPORT_ST_PRLI:
	case RPORT_ST_ADISC:
		fc_rport_enter_logo(rdata);
		break;
	case RPORT_ST_DELETE:
	case RPORT_ST_RESTART:
	case RPORT_ST_READY:
	case RPORT_ST_INIT:
		break;
	}
}

/**
 * fc_rport_error_retry() - Handler for remote port state retries
 * @rdata: The remote port whose state is to be retried
 * @fp:	   The error code encapsulated in a frame pointer
 *
 * If the error was an exchange timeout retry immediately,
 * otherwise wait for E_D_TOV.
 *
 * Locking Note: The rport lock is expected to be held before
 * calling this routine
 */
static void fc_rport_error_retry(struct fc_rport_priv *rdata,
				 struct fc_frame *fp)
{
	unsigned long delay = FC_DEF_E_D_TOV;

	/* make sure this isn't an FC_EX_CLOSED error, never retry those */
	if (PTR_ERR(fp) == -FC_EX_CLOSED)
		return fc_rport_error(rdata, fp);

	if (rdata->retries < rdata->local_port->max_rport_retry_count) {
		FC_RPORT_DBG(rdata, "Error %ld in state %s, retrying\n",
			     PTR_ERR(fp), fc_rport_state(rdata));
		rdata->retries++;
		/* no additional delay on exchange timeouts */
		if (PTR_ERR(fp) == -FC_EX_TIMEOUT)
			delay = 0;
		schedule_delayed_work(&rdata->retry_work, delay);
		return;
	}

	return fc_rport_error(rdata, fp);
}

/**
 * fc_rport_plogi_recv_resp() - Handler for ELS PLOGI responses
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
	unsigned int tov;
	u16 csp_seq;
	u16 cssp_seq;
	u8 op;

	mutex_lock(&rdata->rp_mutex);

	FC_RPORT_DBG(rdata, "Received a PLOGI %s\n", fc_els_resp_type(fp));

	if (rdata->rp_state != RPORT_ST_PLOGI) {
		FC_RPORT_DBG(rdata, "Received a PLOGI response, but in state "
			     "%s\n", fc_rport_state(rdata));
		if (IS_ERR(fp))
			goto err;
		goto out;
	}

	if (IS_ERR(fp)) {
		fc_rport_error_retry(rdata, fp);
		goto err;
	}

	op = fc_frame_payload_op(fp);
	if (op == ELS_LS_ACC &&
	    (plp = fc_frame_payload_get(fp, sizeof(*plp))) != NULL) {
		rdata->ids.port_name = get_unaligned_be64(&plp->fl_wwpn);
		rdata->ids.node_name = get_unaligned_be64(&plp->fl_wwnn);

		tov = ntohl(plp->fl_csp.sp_e_d_tov);
		if (ntohs(plp->fl_csp.sp_features) & FC_SP_FT_EDTR)
			tov /= 1000000;
		if (tov > rdata->e_d_tov)
			rdata->e_d_tov = tov;
		csp_seq = ntohs(plp->fl_csp.sp_tot_seq);
		cssp_seq = ntohs(plp->fl_cssp[3 - 1].cp_con_seq);
		if (cssp_seq < csp_seq)
			csp_seq = cssp_seq;
		rdata->max_seq = csp_seq;
		rdata->maxframe_size = fc_plogi_get_maxframe(plp, lport->mfs);
		fc_rport_enter_prli(rdata);
	} else
		fc_rport_error_retry(rdata, fp);

out:
	fc_frame_free(fp);
err:
	mutex_unlock(&rdata->rp_mutex);
	kref_put(&rdata->kref, rdata->local_port->tt.rport_destroy);
}

/**
 * fc_rport_enter_plogi() - Send Port Login (PLOGI) request
 * @rdata: The remote port to send a PLOGI to
 *
 * Locking Note: The rport lock is expected to be held before calling
 * this routine.
 */
static void fc_rport_enter_plogi(struct fc_rport_priv *rdata)
{
	struct fc_lport *lport = rdata->local_port;
	struct fc_frame *fp;

	FC_RPORT_DBG(rdata, "Port entered PLOGI state from %s state\n",
		     fc_rport_state(rdata));

	fc_rport_state_enter(rdata, RPORT_ST_PLOGI);

	rdata->maxframe_size = FC_MIN_MAX_PAYLOAD;
	fp = fc_frame_alloc(lport, sizeof(struct fc_els_flogi));
	if (!fp) {
		fc_rport_error_retry(rdata, fp);
		return;
	}
	rdata->e_d_tov = lport->e_d_tov;

	if (!lport->tt.elsct_send(lport, rdata->ids.port_id, fp, ELS_PLOGI,
				  fc_rport_plogi_resp, rdata,
				  2 * lport->r_a_tov))
		fc_rport_error_retry(rdata, NULL);
	else
		kref_get(&rdata->kref);
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
	u32 roles = FC_RPORT_ROLE_UNKNOWN;
	u32 fcp_parm = 0;
	u8 op;

	mutex_lock(&rdata->rp_mutex);

	FC_RPORT_DBG(rdata, "Received a PRLI %s\n", fc_els_resp_type(fp));

	if (rdata->rp_state != RPORT_ST_PRLI) {
		FC_RPORT_DBG(rdata, "Received a PRLI response, but in state "
			     "%s\n", fc_rport_state(rdata));
		if (IS_ERR(fp))
			goto err;
		goto out;
	}

	if (IS_ERR(fp)) {
		fc_rport_error_retry(rdata, fp);
		goto err;
	}

	/* reinitialize remote port roles */
	rdata->ids.roles = FC_RPORT_ROLE_UNKNOWN;

	op = fc_frame_payload_op(fp);
	if (op == ELS_LS_ACC) {
		pp = fc_frame_payload_get(fp, sizeof(*pp));
		if (pp && pp->prli.prli_spp_len >= sizeof(pp->spp)) {
			fcp_parm = ntohl(pp->spp.spp_params);
			if (fcp_parm & FCP_SPPF_RETRY)
				rdata->flags |= FC_RP_FLAGS_RETRY;
		}

		rdata->supported_classes = FC_COS_CLASS3;
		if (fcp_parm & FCP_SPPF_INIT_FCN)
			roles |= FC_RPORT_ROLE_FCP_INITIATOR;
		if (fcp_parm & FCP_SPPF_TARG_FCN)
			roles |= FC_RPORT_ROLE_FCP_TARGET;

		rdata->ids.roles = roles;
		fc_rport_enter_rtv(rdata);

	} else {
		FC_RPORT_DBG(rdata, "Bad ELS response for PRLI command\n");
		fc_rport_enter_delete(rdata, RPORT_EV_FAILED);
	}

out:
	fc_frame_free(fp);
err:
	mutex_unlock(&rdata->rp_mutex);
	kref_put(&rdata->kref, rdata->local_port->tt.rport_destroy);
}

/**
 * fc_rport_logo_resp() - Handler for logout (LOGO) responses
 * @sp:	       The sequence the LOGO was on
 * @fp:	       The LOGO response frame
 * @rdata_arg: The remote port that sent the LOGO response
 *
 * Locking Note: This function will be called without the rport lock
 * held, but it will lock, call an _enter_* function or fc_rport_error
 * and then unlock the rport.
 */
static void fc_rport_logo_resp(struct fc_seq *sp, struct fc_frame *fp,
			       void *rdata_arg)
{
	struct fc_rport_priv *rdata = rdata_arg;
	u8 op;

	mutex_lock(&rdata->rp_mutex);

	FC_RPORT_DBG(rdata, "Received a LOGO %s\n", fc_els_resp_type(fp));

	if (rdata->rp_state != RPORT_ST_LOGO) {
		FC_RPORT_DBG(rdata, "Received a LOGO response, but in state "
			     "%s\n", fc_rport_state(rdata));
		if (IS_ERR(fp))
			goto err;
		goto out;
	}

	if (IS_ERR(fp)) {
		fc_rport_error_retry(rdata, fp);
		goto err;
	}

	op = fc_frame_payload_op(fp);
	if (op != ELS_LS_ACC)
		FC_RPORT_DBG(rdata, "Bad ELS response op %x for LOGO command\n",
			     op);
	fc_rport_enter_delete(rdata, RPORT_EV_LOGO);

out:
	fc_frame_free(fp);
err:
	mutex_unlock(&rdata->rp_mutex);
	kref_put(&rdata->kref, rdata->local_port->tt.rport_destroy);
}

/**
 * fc_rport_enter_prli() - Send Process Login (PRLI) request
 * @rdata: The remote port to send the PRLI request to
 *
 * Locking Note: The rport lock is expected to be held before calling
 * this routine.
 */
static void fc_rport_enter_prli(struct fc_rport_priv *rdata)
{
	struct fc_lport *lport = rdata->local_port;
	struct {
		struct fc_els_prli prli;
		struct fc_els_spp spp;
	} *pp;
	struct fc_frame *fp;

	/*
	 * If the rport is one of the well known addresses
	 * we skip PRLI and RTV and go straight to READY.
	 */
	if (rdata->ids.port_id >= FC_FID_DOM_MGR) {
		fc_rport_enter_ready(rdata);
		return;
	}

	FC_RPORT_DBG(rdata, "Port entered PRLI state from %s state\n",
		     fc_rport_state(rdata));

	fc_rport_state_enter(rdata, RPORT_ST_PRLI);

	fp = fc_frame_alloc(lport, sizeof(*pp));
	if (!fp) {
		fc_rport_error_retry(rdata, fp);
		return;
	}

	if (!lport->tt.elsct_send(lport, rdata->ids.port_id, fp, ELS_PRLI,
				  fc_rport_prli_resp, rdata,
				  2 * lport->r_a_tov))
		fc_rport_error_retry(rdata, NULL);
	else
		kref_get(&rdata->kref);
}

/**
 * fc_rport_els_rtv_resp() - Handler for Request Timeout Value (RTV) responses
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

	mutex_lock(&rdata->rp_mutex);

	FC_RPORT_DBG(rdata, "Received a RTV %s\n", fc_els_resp_type(fp));

	if (rdata->rp_state != RPORT_ST_RTV) {
		FC_RPORT_DBG(rdata, "Received a RTV response, but in state "
			     "%s\n", fc_rport_state(rdata));
		if (IS_ERR(fp))
			goto err;
		goto out;
	}

	if (IS_ERR(fp)) {
		fc_rport_error(rdata, fp);
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
			rdata->r_a_tov = tov;
			tov = ntohl(rtv->rtv_e_d_tov);
			if (toq & FC_ELS_RTV_EDRES)
				tov /= 1000000;
			if (tov == 0)
				tov = 1;
			rdata->e_d_tov = tov;
		}
	}

	fc_rport_enter_ready(rdata);

out:
	fc_frame_free(fp);
err:
	mutex_unlock(&rdata->rp_mutex);
	kref_put(&rdata->kref, rdata->local_port->tt.rport_destroy);
}

/**
 * fc_rport_enter_rtv() - Send Request Timeout Value (RTV) request
 * @rdata: The remote port to send the RTV request to
 *
 * Locking Note: The rport lock is expected to be held before calling
 * this routine.
 */
static void fc_rport_enter_rtv(struct fc_rport_priv *rdata)
{
	struct fc_frame *fp;
	struct fc_lport *lport = rdata->local_port;

	FC_RPORT_DBG(rdata, "Port entered RTV state from %s state\n",
		     fc_rport_state(rdata));

	fc_rport_state_enter(rdata, RPORT_ST_RTV);

	fp = fc_frame_alloc(lport, sizeof(struct fc_els_rtv));
	if (!fp) {
		fc_rport_error_retry(rdata, fp);
		return;
	}

	if (!lport->tt.elsct_send(lport, rdata->ids.port_id, fp, ELS_RTV,
				  fc_rport_rtv_resp, rdata,
				  2 * lport->r_a_tov))
		fc_rport_error_retry(rdata, NULL);
	else
		kref_get(&rdata->kref);
}

/**
 * fc_rport_enter_logo() - Send a logout (LOGO) request
 * @rdata: The remote port to send the LOGO request to
 *
 * Locking Note: The rport lock is expected to be held before calling
 * this routine.
 */
static void fc_rport_enter_logo(struct fc_rport_priv *rdata)
{
	struct fc_lport *lport = rdata->local_port;
	struct fc_frame *fp;

	FC_RPORT_DBG(rdata, "Port entered LOGO state from %s state\n",
		     fc_rport_state(rdata));

	fc_rport_state_enter(rdata, RPORT_ST_LOGO);

	fp = fc_frame_alloc(lport, sizeof(struct fc_els_logo));
	if (!fp) {
		fc_rport_error_retry(rdata, fp);
		return;
	}

	if (!lport->tt.elsct_send(lport, rdata->ids.port_id, fp, ELS_LOGO,
				  fc_rport_logo_resp, rdata,
				  2 * lport->r_a_tov))
		fc_rport_error_retry(rdata, NULL);
	else
		kref_get(&rdata->kref);
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

	mutex_lock(&rdata->rp_mutex);

	FC_RPORT_DBG(rdata, "Received a ADISC response\n");

	if (rdata->rp_state != RPORT_ST_ADISC) {
		FC_RPORT_DBG(rdata, "Received a ADISC resp but in state %s\n",
			     fc_rport_state(rdata));
		if (IS_ERR(fp))
			goto err;
		goto out;
	}

	if (IS_ERR(fp)) {
		fc_rport_error(rdata, fp);
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
		fc_rport_enter_plogi(rdata);
	} else {
		FC_RPORT_DBG(rdata, "ADISC OK\n");
		fc_rport_enter_ready(rdata);
	}
out:
	fc_frame_free(fp);
err:
	mutex_unlock(&rdata->rp_mutex);
	kref_put(&rdata->kref, rdata->local_port->tt.rport_destroy);
}

/**
 * fc_rport_enter_adisc() - Send Address Discover (ADISC) request
 * @rdata: The remote port to send the ADISC request to
 *
 * Locking Note: The rport lock is expected to be held before calling
 * this routine.
 */
static void fc_rport_enter_adisc(struct fc_rport_priv *rdata)
{
	struct fc_lport *lport = rdata->local_port;
	struct fc_frame *fp;

	FC_RPORT_DBG(rdata, "sending ADISC from %s state\n",
		     fc_rport_state(rdata));

	fc_rport_state_enter(rdata, RPORT_ST_ADISC);

	fp = fc_frame_alloc(lport, sizeof(struct fc_els_adisc));
	if (!fp) {
		fc_rport_error_retry(rdata, fp);
		return;
	}
	if (!lport->tt.elsct_send(lport, rdata->ids.port_id, fp, ELS_ADISC,
				  fc_rport_adisc_resp, rdata,
				  2 * lport->r_a_tov))
		fc_rport_error_retry(rdata, NULL);
	else
		kref_get(&rdata->kref);
}

/**
 * fc_rport_recv_adisc_req() - Handler for Address Discovery (ADISC) requests
 * @rdata: The remote port that sent the ADISC request
 * @sp:	   The sequence the ADISC request was on
 * @in_fp: The ADISC request frame
 *
 * Locking Note:  Called with the lport and rport locks held.
 */
static void fc_rport_recv_adisc_req(struct fc_rport_priv *rdata,
				    struct fc_seq *sp, struct fc_frame *in_fp)
{
	struct fc_lport *lport = rdata->local_port;
	struct fc_frame *fp;
	struct fc_exch *ep = fc_seq_exch(sp);
	struct fc_els_adisc *adisc;
	struct fc_seq_els_data rjt_data;
	u32 f_ctl;

	FC_RPORT_DBG(rdata, "Received ADISC request\n");

	adisc = fc_frame_payload_get(in_fp, sizeof(*adisc));
	if (!adisc) {
		rjt_data.fp = NULL;
		rjt_data.reason = ELS_RJT_PROT;
		rjt_data.explan = ELS_EXPL_INV_LEN;
		lport->tt.seq_els_rsp_send(sp, ELS_LS_RJT, &rjt_data);
		goto drop;
	}

	fp = fc_frame_alloc(lport, sizeof(*adisc));
	if (!fp)
		goto drop;
	fc_adisc_fill(lport, fp);
	adisc = fc_frame_payload_get(fp, sizeof(*adisc));
	adisc->adisc_cmd = ELS_LS_ACC;
	sp = lport->tt.seq_start_next(sp);
	f_ctl = FC_FC_EX_CTX | FC_FC_LAST_SEQ | FC_FC_END_SEQ | FC_FC_SEQ_INIT;
	fc_fill_fc_hdr(fp, FC_RCTL_ELS_REP, ep->did, ep->sid,
		       FC_TYPE_ELS, f_ctl, 0);
	lport->tt.seq_send(lport, sp, fp);
drop:
	fc_frame_free(in_fp);
}

/**
 * fc_rport_recv_rls_req() - Handle received Read Link Status request
 * @rdata: The remote port that sent the RLS request
 * @sp:	The sequence that the RLS was on
 * @rx_fp: The PRLI request frame
 *
 * Locking Note: The rport lock is expected to be held before calling
 * this function.
 */
static void fc_rport_recv_rls_req(struct fc_rport_priv *rdata,
				  struct fc_seq *sp, struct fc_frame *rx_fp)

{
	struct fc_lport *lport = rdata->local_port;
	struct fc_frame *fp;
	struct fc_exch *ep = fc_seq_exch(sp);
	struct fc_els_rls *rls;
	struct fc_els_rls_resp *rsp;
	struct fc_els_lesb *lesb;
	struct fc_seq_els_data rjt_data;
	struct fc_host_statistics *hst;
	u32 f_ctl;

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

	sp = lport->tt.seq_start_next(sp);
	f_ctl = FC_FC_EX_CTX | FC_FC_LAST_SEQ | FC_FC_END_SEQ;
	fc_fill_fc_hdr(fp, FC_RCTL_ELS_REP, ep->did, ep->sid,
		       FC_TYPE_ELS, f_ctl, 0);
	lport->tt.seq_send(lport, sp, fp);
	goto out;

out_rjt:
	rjt_data.fp = NULL;
	lport->tt.seq_els_rsp_send(sp, ELS_LS_RJT, &rjt_data);
out:
	fc_frame_free(rx_fp);
}

/**
 * fc_rport_recv_els_req() - Handler for validated ELS requests
 * @lport: The local port that received the ELS request
 * @sp:	   The sequence that the ELS request was on
 * @fp:	   The ELS request frame
 *
 * Handle incoming ELS requests that require port login.
 * The ELS opcode has already been validated by the caller.
 *
 * Locking Note: Called with the lport lock held.
 */
static void fc_rport_recv_els_req(struct fc_lport *lport,
				  struct fc_seq *sp, struct fc_frame *fp)
{
	struct fc_rport_priv *rdata;
	struct fc_frame_header *fh;
	struct fc_seq_els_data els_data;

	els_data.fp = NULL;
	els_data.reason = ELS_RJT_UNAB;
	els_data.explan = ELS_EXPL_PLOGI_REQD;

	fh = fc_frame_header_get(fp);

	mutex_lock(&lport->disc.disc_mutex);
	rdata = lport->tt.rport_lookup(lport, ntoh24(fh->fh_s_id));
	if (!rdata) {
		mutex_unlock(&lport->disc.disc_mutex);
		goto reject;
	}
	mutex_lock(&rdata->rp_mutex);
	mutex_unlock(&lport->disc.disc_mutex);

	switch (rdata->rp_state) {
	case RPORT_ST_PRLI:
	case RPORT_ST_RTV:
	case RPORT_ST_READY:
	case RPORT_ST_ADISC:
		break;
	default:
		mutex_unlock(&rdata->rp_mutex);
		goto reject;
	}

	switch (fc_frame_payload_op(fp)) {
	case ELS_PRLI:
		fc_rport_recv_prli_req(rdata, sp, fp);
		break;
	case ELS_PRLO:
		fc_rport_recv_prlo_req(rdata, sp, fp);
		break;
	case ELS_ADISC:
		fc_rport_recv_adisc_req(rdata, sp, fp);
		break;
	case ELS_RRQ:
		els_data.fp = fp;
		lport->tt.seq_els_rsp_send(sp, ELS_RRQ, &els_data);
		break;
	case ELS_REC:
		els_data.fp = fp;
		lport->tt.seq_els_rsp_send(sp, ELS_REC, &els_data);
		break;
	case ELS_RLS:
		fc_rport_recv_rls_req(rdata, sp, fp);
		break;
	default:
		fc_frame_free(fp);	/* can't happen */
		break;
	}

	mutex_unlock(&rdata->rp_mutex);
	return;

reject:
	lport->tt.seq_els_rsp_send(sp, ELS_LS_RJT, &els_data);
	fc_frame_free(fp);
}

/**
 * fc_rport_recv_req() - Handler for requests
 * @sp:	   The sequence the request was on
 * @fp:	   The request frame
 * @lport: The local port that received the request
 *
 * Locking Note: Called with the lport lock held.
 */
void fc_rport_recv_req(struct fc_seq *sp, struct fc_frame *fp,
		       struct fc_lport *lport)
{
	struct fc_seq_els_data els_data;

	/*
	 * Handle PLOGI and LOGO requests separately, since they
	 * don't require prior login.
	 * Check for unsupported opcodes first and reject them.
	 * For some ops, it would be incorrect to reject with "PLOGI required".
	 */
	switch (fc_frame_payload_op(fp)) {
	case ELS_PLOGI:
		fc_rport_recv_plogi_req(lport, sp, fp);
		break;
	case ELS_LOGO:
		fc_rport_recv_logo_req(lport, sp, fp);
		break;
	case ELS_PRLI:
	case ELS_PRLO:
	case ELS_ADISC:
	case ELS_RRQ:
	case ELS_REC:
	case ELS_RLS:
		fc_rport_recv_els_req(lport, sp, fp);
		break;
	default:
		fc_frame_free(fp);
		els_data.fp = NULL;
		els_data.reason = ELS_RJT_UNSUP;
		els_data.explan = ELS_EXPL_NONE;
		lport->tt.seq_els_rsp_send(sp, ELS_LS_RJT, &els_data);
		break;
	}
}

/**
 * fc_rport_recv_plogi_req() - Handler for Port Login (PLOGI) requests
 * @lport: The local port that received the PLOGI request
 * @sp:	   The sequence that the PLOGI request was on
 * @rx_fp: The PLOGI request frame
 *
 * Locking Note: The rport lock is held before calling this function.
 */
static void fc_rport_recv_plogi_req(struct fc_lport *lport,
				    struct fc_seq *sp, struct fc_frame *rx_fp)
{
	struct fc_disc *disc;
	struct fc_rport_priv *rdata;
	struct fc_frame *fp = rx_fp;
	struct fc_exch *ep;
	struct fc_frame_header *fh;
	struct fc_els_flogi *pl;
	struct fc_seq_els_data rjt_data;
	u32 sid, f_ctl;

	rjt_data.fp = NULL;
	fh = fc_frame_header_get(fp);
	sid = ntoh24(fh->fh_s_id);

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
	rdata = lport->tt.rport_create(lport, sid);
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
	case RPORT_ST_DELETE:
	case RPORT_ST_LOGO:
	case RPORT_ST_RESTART:
		FC_RPORT_DBG(rdata, "Received PLOGI in state %s - send busy\n",
			     fc_rport_state(rdata));
		mutex_unlock(&rdata->rp_mutex);
		rjt_data.reason = ELS_RJT_BUSY;
		rjt_data.explan = ELS_EXPL_NONE;
		goto reject;
	}

	/*
	 * Get session payload size from incoming PLOGI.
	 */
	rdata->maxframe_size = fc_plogi_get_maxframe(pl, lport->mfs);
	fc_frame_free(rx_fp);

	/*
	 * Send LS_ACC.	 If this fails, the originator should retry.
	 */
	sp = lport->tt.seq_start_next(sp);
	if (!sp)
		goto out;
	fp = fc_frame_alloc(lport, sizeof(*pl));
	if (!fp)
		goto out;

	fc_plogi_fill(lport, fp, ELS_LS_ACC);
	f_ctl = FC_FC_EX_CTX | FC_FC_LAST_SEQ | FC_FC_END_SEQ | FC_FC_SEQ_INIT;
	ep = fc_seq_exch(sp);
	fc_fill_fc_hdr(fp, FC_RCTL_ELS_REP, ep->did, ep->sid,
		       FC_TYPE_ELS, f_ctl, 0);
	lport->tt.seq_send(lport, sp, fp);
	fc_rport_enter_prli(rdata);
out:
	mutex_unlock(&rdata->rp_mutex);
	return;

reject:
	lport->tt.seq_els_rsp_send(sp, ELS_LS_RJT, &rjt_data);
	fc_frame_free(fp);
}

/**
 * fc_rport_recv_prli_req() - Handler for process login (PRLI) requests
 * @rdata: The remote port that sent the PRLI request
 * @sp:	   The sequence that the PRLI was on
 * @rx_fp: The PRLI request frame
 *
 * Locking Note: The rport lock is exected to be held before calling
 * this function.
 */
static void fc_rport_recv_prli_req(struct fc_rport_priv *rdata,
				   struct fc_seq *sp, struct fc_frame *rx_fp)
{
	struct fc_lport *lport = rdata->local_port;
	struct fc_exch *ep;
	struct fc_frame *fp;
	struct fc_frame_header *fh;
	struct {
		struct fc_els_prli prli;
		struct fc_els_spp spp;
	} *pp;
	struct fc_els_spp *rspp;	/* request service param page */
	struct fc_els_spp *spp;	/* response spp */
	unsigned int len;
	unsigned int plen;
	enum fc_els_rjt_reason reason = ELS_RJT_UNAB;
	enum fc_els_rjt_explan explan = ELS_EXPL_NONE;
	enum fc_els_spp_resp resp;
	struct fc_seq_els_data rjt_data;
	u32 f_ctl;
	u32 fcp_parm;
	u32 roles = FC_RPORT_ROLE_UNKNOWN;
	rjt_data.fp = NULL;

	fh = fc_frame_header_get(rx_fp);

	FC_RPORT_DBG(rdata, "Received PRLI request while in state %s\n",
		     fc_rport_state(rdata));

	switch (rdata->rp_state) {
	case RPORT_ST_PRLI:
	case RPORT_ST_RTV:
	case RPORT_ST_READY:
	case RPORT_ST_ADISC:
		reason = ELS_RJT_NONE;
		break;
	default:
		fc_frame_free(rx_fp);
		return;
		break;
	}
	len = fr_len(rx_fp) - sizeof(*fh);
	pp = fc_frame_payload_get(rx_fp, sizeof(*pp));
	if (pp == NULL) {
		reason = ELS_RJT_PROT;
		explan = ELS_EXPL_INV_LEN;
	} else {
		plen = ntohs(pp->prli.prli_len);
		if ((plen % 4) != 0 || plen > len) {
			reason = ELS_RJT_PROT;
			explan = ELS_EXPL_INV_LEN;
		} else if (plen < len) {
			len = plen;
		}
		plen = pp->prli.prli_spp_len;
		if ((plen % 4) != 0 || plen < sizeof(*spp) ||
		    plen > len || len < sizeof(*pp)) {
			reason = ELS_RJT_PROT;
			explan = ELS_EXPL_INV_LEN;
		}
		rspp = &pp->spp;
	}
	if (reason != ELS_RJT_NONE ||
	    (fp = fc_frame_alloc(lport, len)) == NULL) {
		rjt_data.reason = reason;
		rjt_data.explan = explan;
		lport->tt.seq_els_rsp_send(sp, ELS_LS_RJT, &rjt_data);
	} else {
		sp = lport->tt.seq_start_next(sp);
		WARN_ON(!sp);
		pp = fc_frame_payload_get(fp, len);
		WARN_ON(!pp);
		memset(pp, 0, len);
		pp->prli.prli_cmd = ELS_LS_ACC;
		pp->prli.prli_spp_len = plen;
		pp->prli.prli_len = htons(len);
		len -= sizeof(struct fc_els_prli);

		/* reinitialize remote port roles */
		rdata->ids.roles = FC_RPORT_ROLE_UNKNOWN;

		/*
		 * Go through all the service parameter pages and build
		 * response.  If plen indicates longer SPP than standard,
		 * use that.  The entire response has been pre-cleared above.
		 */
		spp = &pp->spp;
		while (len >= plen) {
			spp->spp_type = rspp->spp_type;
			spp->spp_type_ext = rspp->spp_type_ext;
			spp->spp_flags = rspp->spp_flags & FC_SPP_EST_IMG_PAIR;
			resp = FC_SPP_RESP_ACK;
			if (rspp->spp_flags & FC_SPP_RPA_VAL)
				resp = FC_SPP_RESP_NO_PA;
			switch (rspp->spp_type) {
			case 0:	/* common to all FC-4 types */
				break;
			case FC_TYPE_FCP:
				fcp_parm = ntohl(rspp->spp_params);
				if (fcp_parm & FCP_SPPF_RETRY)
					rdata->flags |= FC_RP_FLAGS_RETRY;
				rdata->supported_classes = FC_COS_CLASS3;
				if (fcp_parm & FCP_SPPF_INIT_FCN)
					roles |= FC_RPORT_ROLE_FCP_INITIATOR;
				if (fcp_parm & FCP_SPPF_TARG_FCN)
					roles |= FC_RPORT_ROLE_FCP_TARGET;
				rdata->ids.roles = roles;

				spp->spp_params =
					htonl(lport->service_params);
				break;
			default:
				resp = FC_SPP_RESP_INVL;
				break;
			}
			spp->spp_flags |= resp;
			len -= plen;
			rspp = (struct fc_els_spp *)((char *)rspp + plen);
			spp = (struct fc_els_spp *)((char *)spp + plen);
		}

		/*
		 * Send LS_ACC.	 If this fails, the originator should retry.
		 */
		f_ctl = FC_FC_EX_CTX | FC_FC_LAST_SEQ;
		f_ctl |= FC_FC_END_SEQ | FC_FC_SEQ_INIT;
		ep = fc_seq_exch(sp);
		fc_fill_fc_hdr(fp, FC_RCTL_ELS_REP, ep->did, ep->sid,
			       FC_TYPE_ELS, f_ctl, 0);
		lport->tt.seq_send(lport, sp, fp);

		/*
		 * Get lock and re-check state.
		 */
		switch (rdata->rp_state) {
		case RPORT_ST_PRLI:
			fc_rport_enter_ready(rdata);
			break;
		case RPORT_ST_READY:
		case RPORT_ST_ADISC:
			break;
		default:
			break;
		}
	}
	fc_frame_free(rx_fp);
}

/**
 * fc_rport_recv_prlo_req() - Handler for process logout (PRLO) requests
 * @rdata: The remote port that sent the PRLO request
 * @sp:	   The sequence that the PRLO was on
 * @fp:	   The PRLO request frame
 *
 * Locking Note: The rport lock is exected to be held before calling
 * this function.
 */
static void fc_rport_recv_prlo_req(struct fc_rport_priv *rdata,
				   struct fc_seq *sp,
				   struct fc_frame *fp)
{
	struct fc_lport *lport = rdata->local_port;

	struct fc_frame_header *fh;
	struct fc_seq_els_data rjt_data;

	fh = fc_frame_header_get(fp);

	FC_RPORT_DBG(rdata, "Received PRLO request while in state %s\n",
		     fc_rport_state(rdata));

	rjt_data.fp = NULL;
	rjt_data.reason = ELS_RJT_UNAB;
	rjt_data.explan = ELS_EXPL_NONE;
	lport->tt.seq_els_rsp_send(sp, ELS_LS_RJT, &rjt_data);
	fc_frame_free(fp);
}

/**
 * fc_rport_recv_logo_req() - Handler for logout (LOGO) requests
 * @lport: The local port that received the LOGO request
 * @sp:	   The sequence that the LOGO request was on
 * @fp:	   The LOGO request frame
 *
 * Locking Note: The rport lock is exected to be held before calling
 * this function.
 */
static void fc_rport_recv_logo_req(struct fc_lport *lport,
				   struct fc_seq *sp,
				   struct fc_frame *fp)
{
	struct fc_frame_header *fh;
	struct fc_rport_priv *rdata;
	u32 sid;

	lport->tt.seq_els_rsp_send(sp, ELS_LS_ACC, NULL);

	fh = fc_frame_header_get(fp);
	sid = ntoh24(fh->fh_s_id);

	mutex_lock(&lport->disc.disc_mutex);
	rdata = lport->tt.rport_lookup(lport, sid);
	if (rdata) {
		mutex_lock(&rdata->rp_mutex);
		FC_RPORT_DBG(rdata, "Received LOGO request while in state %s\n",
			     fc_rport_state(rdata));

		fc_rport_enter_delete(rdata, RPORT_EV_LOGO);

		/*
		 * If the remote port was created due to discovery, set state
		 * to log back in.  It may have seen a stale RSCN about us.
		 */
		if (rdata->disc_id)
			fc_rport_state_enter(rdata, RPORT_ST_RESTART);
		mutex_unlock(&rdata->rp_mutex);
	} else
		FC_RPORT_ID_DBG(lport, sid,
				"Received LOGO from non-logged-in port\n");
	mutex_unlock(&lport->disc.disc_mutex);
	fc_frame_free(fp);
}

/**
 * fc_rport_flush_queue() - Flush the rport_event_queue
 */
static void fc_rport_flush_queue(void)
{
	flush_workqueue(rport_event_queue);
}

/**
 * fc_rport_init() - Initialize the remote port layer for a local port
 * @lport: The local port to initialize the remote port layer for
 */
int fc_rport_init(struct fc_lport *lport)
{
	if (!lport->tt.rport_lookup)
		lport->tt.rport_lookup = fc_rport_lookup;

	if (!lport->tt.rport_create)
		lport->tt.rport_create = fc_rport_create;

	if (!lport->tt.rport_login)
		lport->tt.rport_login = fc_rport_login;

	if (!lport->tt.rport_logoff)
		lport->tt.rport_logoff = fc_rport_logoff;

	if (!lport->tt.rport_recv_req)
		lport->tt.rport_recv_req = fc_rport_recv_req;

	if (!lport->tt.rport_flush_queue)
		lport->tt.rport_flush_queue = fc_rport_flush_queue;

	if (!lport->tt.rport_destroy)
		lport->tt.rport_destroy = fc_rport_destroy;

	return 0;
}
EXPORT_SYMBOL(fc_rport_init);

/**
 * fc_setup_rport() - Initialize the rport_event_queue
 */
int fc_setup_rport()
{
	rport_event_queue = create_singlethread_workqueue("fc_rport_eq");
	if (!rport_event_queue)
		return -ENOMEM;
	return 0;
}

/**
 * fc_destroy_rport() - Destroy the rport_event_queue
 */
void fc_destroy_rport()
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

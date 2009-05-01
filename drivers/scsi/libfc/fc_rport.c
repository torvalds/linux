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
#include <linux/rcupdate.h>
#include <linux/timer.h>
#include <linux/workqueue.h>
#include <asm/unaligned.h>

#include <scsi/libfc.h>
#include <scsi/fc_encode.h>

static int fc_rport_debug;

#define FC_DEBUG_RPORT(fmt...)			\
	do {					\
		if (fc_rport_debug)		\
			FC_DBG(fmt);		\
	} while (0)

struct workqueue_struct *rport_event_queue;

static void fc_rport_enter_plogi(struct fc_rport *);
static void fc_rport_enter_prli(struct fc_rport *);
static void fc_rport_enter_rtv(struct fc_rport *);
static void fc_rport_enter_ready(struct fc_rport *);
static void fc_rport_enter_logo(struct fc_rport *);

static void fc_rport_recv_plogi_req(struct fc_rport *,
				    struct fc_seq *, struct fc_frame *);
static void fc_rport_recv_prli_req(struct fc_rport *,
				   struct fc_seq *, struct fc_frame *);
static void fc_rport_recv_prlo_req(struct fc_rport *,
				   struct fc_seq *, struct fc_frame *);
static void fc_rport_recv_logo_req(struct fc_rport *,
				   struct fc_seq *, struct fc_frame *);
static void fc_rport_timeout(struct work_struct *);
static void fc_rport_error(struct fc_rport *, struct fc_frame *);
static void fc_rport_error_retry(struct fc_rport *, struct fc_frame *);
static void fc_rport_work(struct work_struct *);

static const char *fc_rport_state_names[] = {
	[RPORT_ST_NONE] = "None",
	[RPORT_ST_INIT] = "Init",
	[RPORT_ST_PLOGI] = "PLOGI",
	[RPORT_ST_PRLI] = "PRLI",
	[RPORT_ST_RTV] = "RTV",
	[RPORT_ST_READY] = "Ready",
	[RPORT_ST_LOGO] = "LOGO",
};

static void fc_rport_rogue_destroy(struct device *dev)
{
	struct fc_rport *rport = dev_to_rport(dev);
	FC_DEBUG_RPORT("Destroying rogue rport (%6x)\n", rport->port_id);
	kfree(rport);
}

struct fc_rport *fc_rport_rogue_create(struct fc_disc_port *dp)
{
	struct fc_rport *rport;
	struct fc_rport_libfc_priv *rdata;
	rport = kzalloc(sizeof(*rport) + sizeof(*rdata), GFP_KERNEL);

	if (!rport)
		return NULL;

	rdata = RPORT_TO_PRIV(rport);

	rport->dd_data = rdata;
	rport->port_id = dp->ids.port_id;
	rport->port_name = dp->ids.port_name;
	rport->node_name = dp->ids.node_name;
	rport->roles = dp->ids.roles;
	rport->maxframe_size = FC_MIN_MAX_PAYLOAD;
	/*
	 * Note: all this libfc rogue rport code will be removed for
	 * upstream so it fine that this is really ugly and hacky right now.
	 */
	device_initialize(&rport->dev);
	rport->dev.release = fc_rport_rogue_destroy;

	mutex_init(&rdata->rp_mutex);
	rdata->local_port = dp->lp;
	rdata->trans_state = FC_PORTSTATE_ROGUE;
	rdata->rp_state = RPORT_ST_INIT;
	rdata->event = RPORT_EV_NONE;
	rdata->flags = FC_RP_FLAGS_REC_SUPPORTED;
	rdata->ops = NULL;
	rdata->e_d_tov = dp->lp->e_d_tov;
	rdata->r_a_tov = dp->lp->r_a_tov;
	INIT_DELAYED_WORK(&rdata->retry_work, fc_rport_timeout);
	INIT_WORK(&rdata->event_work, fc_rport_work);
	/*
	 * For good measure, but not necessary as we should only
	 * add REAL rport to the lport list.
	 */
	INIT_LIST_HEAD(&rdata->peers);

	return rport;
}

/**
 * fc_rport_state() - return a string for the state the rport is in
 * @rport: The rport whose state we want to get a string for
 */
static const char *fc_rport_state(struct fc_rport *rport)
{
	const char *cp;
	struct fc_rport_libfc_priv *rdata = rport->dd_data;

	cp = fc_rport_state_names[rdata->rp_state];
	if (!cp)
		cp = "Unknown";
	return cp;
}

/**
 * fc_set_rport_loss_tmo() - Set the remote port loss timeout in seconds.
 * @rport: Pointer to Fibre Channel remote port structure
 * @timeout: timeout in seconds
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
 * fc_plogi_get_maxframe() - Get max payload from the common service parameters
 * @flp: FLOGI payload structure
 * @maxval: upper limit, may be less than what is in the service parameters
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
 * fc_rport_state_enter() - Change the rport's state
 * @rport: The rport whose state should change
 * @new: The new state of the rport
 *
 * Locking Note: Called with the rport lock held
 */
static void fc_rport_state_enter(struct fc_rport *rport,
				 enum fc_rport_state new)
{
	struct fc_rport_libfc_priv *rdata = rport->dd_data;
	if (rdata->rp_state != new)
		rdata->retries = 0;
	rdata->rp_state = new;
}

static void fc_rport_work(struct work_struct *work)
{
	u32 port_id;
	struct fc_rport_libfc_priv *rdata =
		container_of(work, struct fc_rport_libfc_priv, event_work);
	enum fc_rport_event event;
	enum fc_rport_trans_state trans_state;
	struct fc_lport *lport = rdata->local_port;
	struct fc_rport_operations *rport_ops;
	struct fc_rport *rport = PRIV_TO_RPORT(rdata);

	mutex_lock(&rdata->rp_mutex);
	event = rdata->event;
	rport_ops = rdata->ops;

	if (event == RPORT_EV_CREATED) {
		struct fc_rport *new_rport;
		struct fc_rport_libfc_priv *new_rdata;
		struct fc_rport_identifiers ids;

		ids.port_id = rport->port_id;
		ids.roles = rport->roles;
		ids.port_name = rport->port_name;
		ids.node_name = rport->node_name;

		mutex_unlock(&rdata->rp_mutex);

		new_rport = fc_remote_port_add(lport->host, 0, &ids);
		if (new_rport) {
			/*
			 * Switch from the rogue rport to the rport
			 * returned by the FC class.
			 */
			new_rport->maxframe_size = rport->maxframe_size;

			new_rdata = new_rport->dd_data;
			new_rdata->e_d_tov = rdata->e_d_tov;
			new_rdata->r_a_tov = rdata->r_a_tov;
			new_rdata->ops = rdata->ops;
			new_rdata->local_port = rdata->local_port;
			new_rdata->flags = FC_RP_FLAGS_REC_SUPPORTED;
			new_rdata->trans_state = FC_PORTSTATE_REAL;
			mutex_init(&new_rdata->rp_mutex);
			INIT_DELAYED_WORK(&new_rdata->retry_work,
					  fc_rport_timeout);
			INIT_LIST_HEAD(&new_rdata->peers);
			INIT_WORK(&new_rdata->event_work, fc_rport_work);

			fc_rport_state_enter(new_rport, RPORT_ST_READY);
		} else {
			FC_DBG("Failed to create the rport for port "
			       "(%6x).\n", ids.port_id);
			event = RPORT_EV_FAILED;
		}
		if (rport->port_id != FC_FID_DIR_SERV)
			if (rport_ops->event_callback)
				rport_ops->event_callback(lport, rport,
							  RPORT_EV_FAILED);
		put_device(&rport->dev);
		rport = new_rport;
		rdata = new_rport->dd_data;
		if (rport_ops->event_callback)
			rport_ops->event_callback(lport, rport, event);
	} else if ((event == RPORT_EV_FAILED) ||
		   (event == RPORT_EV_LOGO) ||
		   (event == RPORT_EV_STOP)) {
		trans_state = rdata->trans_state;
		mutex_unlock(&rdata->rp_mutex);
		if (rport_ops->event_callback)
			rport_ops->event_callback(lport, rport, event);
		if (trans_state == FC_PORTSTATE_ROGUE)
			put_device(&rport->dev);
		else {
			port_id = rport->port_id;
			fc_remote_port_delete(rport);
			lport->tt.exch_mgr_reset(lport, 0, port_id);
			lport->tt.exch_mgr_reset(lport, port_id, 0);
		}
	} else
		mutex_unlock(&rdata->rp_mutex);
}

/**
 * fc_rport_login() - Start the remote port login state machine
 * @rport: Fibre Channel remote port
 *
 * Locking Note: Called without the rport lock held. This
 * function will hold the rport lock, call an _enter_*
 * function and then unlock the rport.
 */
int fc_rport_login(struct fc_rport *rport)
{
	struct fc_rport_libfc_priv *rdata = rport->dd_data;

	mutex_lock(&rdata->rp_mutex);

	FC_DEBUG_RPORT("Login to port (%6x)\n", rport->port_id);

	fc_rport_enter_plogi(rport);

	mutex_unlock(&rdata->rp_mutex);

	return 0;
}

/**
 * fc_rport_logoff() - Logoff and remove an rport
 * @rport: Fibre Channel remote port to be removed
 *
 * Locking Note: Called without the rport lock held. This
 * function will hold the rport lock, call an _enter_*
 * function and then unlock the rport.
 */
int fc_rport_logoff(struct fc_rport *rport)
{
	struct fc_rport_libfc_priv *rdata = rport->dd_data;
	struct fc_lport *lport = rdata->local_port;

	mutex_lock(&rdata->rp_mutex);

	FC_DEBUG_RPORT("Remove port (%6x)\n", rport->port_id);

	if (rdata->rp_state == RPORT_ST_NONE) {
		FC_DEBUG_RPORT("(%6x): Port (%6x) in NONE state,"
			       " not removing", fc_host_port_id(lport->host),
			       rport->port_id);
		mutex_unlock(&rdata->rp_mutex);
		goto out;
	}

	fc_rport_enter_logo(rport);

	/*
	 * Change the state to NONE so that we discard
	 * the response.
	 */
	fc_rport_state_enter(rport, RPORT_ST_NONE);

	mutex_unlock(&rdata->rp_mutex);

	cancel_delayed_work_sync(&rdata->retry_work);

	mutex_lock(&rdata->rp_mutex);

	rdata->event = RPORT_EV_STOP;
	queue_work(rport_event_queue, &rdata->event_work);

	mutex_unlock(&rdata->rp_mutex);

out:
	return 0;
}

/**
 * fc_rport_enter_ready() - The rport is ready
 * @rport: Fibre Channel remote port that is ready
 *
 * Locking Note: The rport lock is expected to be held before calling
 * this routine.
 */
static void fc_rport_enter_ready(struct fc_rport *rport)
{
	struct fc_rport_libfc_priv *rdata = rport->dd_data;

	fc_rport_state_enter(rport, RPORT_ST_READY);

	FC_DEBUG_RPORT("Port (%6x) is Ready\n", rport->port_id);

	rdata->event = RPORT_EV_CREATED;
	queue_work(rport_event_queue, &rdata->event_work);
}

/**
 * fc_rport_timeout() - Handler for the retry_work timer.
 * @work: The work struct of the fc_rport_libfc_priv
 *
 * Locking Note: Called without the rport lock held. This
 * function will hold the rport lock, call an _enter_*
 * function and then unlock the rport.
 */
static void fc_rport_timeout(struct work_struct *work)
{
	struct fc_rport_libfc_priv *rdata =
		container_of(work, struct fc_rport_libfc_priv, retry_work.work);
	struct fc_rport *rport = PRIV_TO_RPORT(rdata);

	mutex_lock(&rdata->rp_mutex);

	switch (rdata->rp_state) {
	case RPORT_ST_PLOGI:
		fc_rport_enter_plogi(rport);
		break;
	case RPORT_ST_PRLI:
		fc_rport_enter_prli(rport);
		break;
	case RPORT_ST_RTV:
		fc_rport_enter_rtv(rport);
		break;
	case RPORT_ST_LOGO:
		fc_rport_enter_logo(rport);
		break;
	case RPORT_ST_READY:
	case RPORT_ST_INIT:
	case RPORT_ST_NONE:
		break;
	}

	mutex_unlock(&rdata->rp_mutex);
	put_device(&rport->dev);
}

/**
 * fc_rport_error() - Error handler, called once retries have been exhausted
 * @rport: The fc_rport object
 * @fp: The frame pointer
 *
 * Locking Note: The rport lock is expected to be held before
 * calling this routine
 */
static void fc_rport_error(struct fc_rport *rport, struct fc_frame *fp)
{
	struct fc_rport_libfc_priv *rdata = rport->dd_data;

	FC_DEBUG_RPORT("Error %ld in state %s, retries %d\n",
		       PTR_ERR(fp), fc_rport_state(rport), rdata->retries);

	switch (rdata->rp_state) {
	case RPORT_ST_PLOGI:
	case RPORT_ST_PRLI:
	case RPORT_ST_LOGO:
		rdata->event = RPORT_EV_FAILED;
		fc_rport_state_enter(rport, RPORT_ST_NONE);
		queue_work(rport_event_queue,
			   &rdata->event_work);
		break;
	case RPORT_ST_RTV:
		fc_rport_enter_ready(rport);
		break;
	case RPORT_ST_NONE:
	case RPORT_ST_READY:
	case RPORT_ST_INIT:
		break;
	}
}

/**
 * fc_rport_error_retry() - Error handler when retries are desired
 * @rport: The fc_rport object
 * @fp: The frame pointer
 *
 * If the error was an exchange timeout retry immediately,
 * otherwise wait for E_D_TOV.
 *
 * Locking Note: The rport lock is expected to be held before
 * calling this routine
 */
static void fc_rport_error_retry(struct fc_rport *rport, struct fc_frame *fp)
{
	struct fc_rport_libfc_priv *rdata = rport->dd_data;
	unsigned long delay = FC_DEF_E_D_TOV;

	/* make sure this isn't an FC_EX_CLOSED error, never retry those */
	if (PTR_ERR(fp) == -FC_EX_CLOSED)
		return fc_rport_error(rport, fp);

	if (rdata->retries < rdata->local_port->max_rport_retry_count) {
		FC_DEBUG_RPORT("Error %ld in state %s, retrying\n",
			       PTR_ERR(fp), fc_rport_state(rport));
		rdata->retries++;
		/* no additional delay on exchange timeouts */
		if (PTR_ERR(fp) == -FC_EX_TIMEOUT)
			delay = 0;
		get_device(&rport->dev);
		schedule_delayed_work(&rdata->retry_work, delay);
		return;
	}

	return fc_rport_error(rport, fp);
}

/**
 * fc_rport_plogi_recv_resp() - Handle incoming ELS PLOGI response
 * @sp: current sequence in the PLOGI exchange
 * @fp: response frame
 * @rp_arg: Fibre Channel remote port
 *
 * Locking Note: This function will be called without the rport lock
 * held, but it will lock, call an _enter_* function or fc_rport_error
 * and then unlock the rport.
 */
static void fc_rport_plogi_resp(struct fc_seq *sp, struct fc_frame *fp,
				void *rp_arg)
{
	struct fc_rport *rport = rp_arg;
	struct fc_rport_libfc_priv *rdata = rport->dd_data;
	struct fc_lport *lport = rdata->local_port;
	struct fc_els_flogi *plp = NULL;
	unsigned int tov;
	u16 csp_seq;
	u16 cssp_seq;
	u8 op;

	mutex_lock(&rdata->rp_mutex);

	FC_DEBUG_RPORT("Received a PLOGI response from port (%6x)\n",
		       rport->port_id);

	if (rdata->rp_state != RPORT_ST_PLOGI) {
		FC_DBG("Received a PLOGI response, but in state %s\n",
		       fc_rport_state(rport));
		if (IS_ERR(fp))
			goto err;
		goto out;
	}

	if (IS_ERR(fp)) {
		fc_rport_error_retry(rport, fp);
		goto err;
	}

	op = fc_frame_payload_op(fp);
	if (op == ELS_LS_ACC &&
	    (plp = fc_frame_payload_get(fp, sizeof(*plp))) != NULL) {
		rport->port_name = get_unaligned_be64(&plp->fl_wwpn);
		rport->node_name = get_unaligned_be64(&plp->fl_wwnn);

		tov = ntohl(plp->fl_csp.sp_e_d_tov);
		if (ntohs(plp->fl_csp.sp_features) & FC_SP_FT_EDTR)
			tov /= 1000;
		if (tov > rdata->e_d_tov)
			rdata->e_d_tov = tov;
		csp_seq = ntohs(plp->fl_csp.sp_tot_seq);
		cssp_seq = ntohs(plp->fl_cssp[3 - 1].cp_con_seq);
		if (cssp_seq < csp_seq)
			csp_seq = cssp_seq;
		rdata->max_seq = csp_seq;
		rport->maxframe_size =
			fc_plogi_get_maxframe(plp, lport->mfs);

		/*
		 * If the rport is one of the well known addresses
		 * we skip PRLI and RTV and go straight to READY.
		 */
		if (rport->port_id >= FC_FID_DOM_MGR)
			fc_rport_enter_ready(rport);
		else
			fc_rport_enter_prli(rport);
	} else
		fc_rport_error_retry(rport, fp);

out:
	fc_frame_free(fp);
err:
	mutex_unlock(&rdata->rp_mutex);
	put_device(&rport->dev);
}

/**
 * fc_rport_enter_plogi() - Send Port Login (PLOGI) request to peer
 * @rport: Fibre Channel remote port to send PLOGI to
 *
 * Locking Note: The rport lock is expected to be held before calling
 * this routine.
 */
static void fc_rport_enter_plogi(struct fc_rport *rport)
{
	struct fc_rport_libfc_priv *rdata = rport->dd_data;
	struct fc_lport *lport = rdata->local_port;
	struct fc_frame *fp;

	FC_DEBUG_RPORT("Port (%6x) entered PLOGI state from %s state\n",
		       rport->port_id, fc_rport_state(rport));

	fc_rport_state_enter(rport, RPORT_ST_PLOGI);

	rport->maxframe_size = FC_MIN_MAX_PAYLOAD;
	fp = fc_frame_alloc(lport, sizeof(struct fc_els_flogi));
	if (!fp) {
		fc_rport_error_retry(rport, fp);
		return;
	}
	rdata->e_d_tov = lport->e_d_tov;

	if (!lport->tt.elsct_send(lport, rport, fp, ELS_PLOGI,
				  fc_rport_plogi_resp, rport, lport->e_d_tov))
		fc_rport_error_retry(rport, fp);
	else
		get_device(&rport->dev);
}

/**
 * fc_rport_prli_resp() - Process Login (PRLI) response handler
 * @sp: current sequence in the PRLI exchange
 * @fp: response frame
 * @rp_arg: Fibre Channel remote port
 *
 * Locking Note: This function will be called without the rport lock
 * held, but it will lock, call an _enter_* function or fc_rport_error
 * and then unlock the rport.
 */
static void fc_rport_prli_resp(struct fc_seq *sp, struct fc_frame *fp,
			       void *rp_arg)
{
	struct fc_rport *rport = rp_arg;
	struct fc_rport_libfc_priv *rdata = rport->dd_data;
	struct {
		struct fc_els_prli prli;
		struct fc_els_spp spp;
	} *pp;
	u32 roles = FC_RPORT_ROLE_UNKNOWN;
	u32 fcp_parm = 0;
	u8 op;

	mutex_lock(&rdata->rp_mutex);

	FC_DEBUG_RPORT("Received a PRLI response from port (%6x)\n",
		       rport->port_id);

	if (rdata->rp_state != RPORT_ST_PRLI) {
		FC_DBG("Received a PRLI response, but in state %s\n",
		       fc_rport_state(rport));
		if (IS_ERR(fp))
			goto err;
		goto out;
	}

	if (IS_ERR(fp)) {
		fc_rport_error_retry(rport, fp);
		goto err;
	}

	op = fc_frame_payload_op(fp);
	if (op == ELS_LS_ACC) {
		pp = fc_frame_payload_get(fp, sizeof(*pp));
		if (pp && pp->prli.prli_spp_len >= sizeof(pp->spp)) {
			fcp_parm = ntohl(pp->spp.spp_params);
			if (fcp_parm & FCP_SPPF_RETRY)
				rdata->flags |= FC_RP_FLAGS_RETRY;
		}

		rport->supported_classes = FC_COS_CLASS3;
		if (fcp_parm & FCP_SPPF_INIT_FCN)
			roles |= FC_RPORT_ROLE_FCP_INITIATOR;
		if (fcp_parm & FCP_SPPF_TARG_FCN)
			roles |= FC_RPORT_ROLE_FCP_TARGET;

		rport->roles = roles;
		fc_rport_enter_rtv(rport);

	} else {
		FC_DBG("Bad ELS response\n");
		rdata->event = RPORT_EV_FAILED;
		fc_rport_state_enter(rport, RPORT_ST_NONE);
		queue_work(rport_event_queue, &rdata->event_work);
	}

out:
	fc_frame_free(fp);
err:
	mutex_unlock(&rdata->rp_mutex);
	put_device(&rport->dev);
}

/**
 * fc_rport_logo_resp() - Logout (LOGO) response handler
 * @sp: current sequence in the LOGO exchange
 * @fp: response frame
 * @rp_arg: Fibre Channel remote port
 *
 * Locking Note: This function will be called without the rport lock
 * held, but it will lock, call an _enter_* function or fc_rport_error
 * and then unlock the rport.
 */
static void fc_rport_logo_resp(struct fc_seq *sp, struct fc_frame *fp,
			       void *rp_arg)
{
	struct fc_rport *rport = rp_arg;
	struct fc_rport_libfc_priv *rdata = rport->dd_data;
	u8 op;

	mutex_lock(&rdata->rp_mutex);

	FC_DEBUG_RPORT("Received a LOGO response from port (%6x)\n",
		       rport->port_id);

	if (rdata->rp_state != RPORT_ST_LOGO) {
		FC_DEBUG_RPORT("Received a LOGO response, but in state %s\n",
			       fc_rport_state(rport));
		if (IS_ERR(fp))
			goto err;
		goto out;
	}

	if (IS_ERR(fp)) {
		fc_rport_error_retry(rport, fp);
		goto err;
	}

	op = fc_frame_payload_op(fp);
	if (op == ELS_LS_ACC) {
		fc_rport_enter_rtv(rport);
	} else {
		FC_DBG("Bad ELS response\n");
		rdata->event = RPORT_EV_LOGO;
		fc_rport_state_enter(rport, RPORT_ST_NONE);
		queue_work(rport_event_queue, &rdata->event_work);
	}

out:
	fc_frame_free(fp);
err:
	mutex_unlock(&rdata->rp_mutex);
	put_device(&rport->dev);
}

/**
 * fc_rport_enter_prli() - Send Process Login (PRLI) request to peer
 * @rport: Fibre Channel remote port to send PRLI to
 *
 * Locking Note: The rport lock is expected to be held before calling
 * this routine.
 */
static void fc_rport_enter_prli(struct fc_rport *rport)
{
	struct fc_rport_libfc_priv *rdata = rport->dd_data;
	struct fc_lport *lport = rdata->local_port;
	struct {
		struct fc_els_prli prli;
		struct fc_els_spp spp;
	} *pp;
	struct fc_frame *fp;

	FC_DEBUG_RPORT("Port (%6x) entered PRLI state from %s state\n",
		       rport->port_id, fc_rport_state(rport));

	fc_rport_state_enter(rport, RPORT_ST_PRLI);

	fp = fc_frame_alloc(lport, sizeof(*pp));
	if (!fp) {
		fc_rport_error_retry(rport, fp);
		return;
	}

	if (!lport->tt.elsct_send(lport, rport, fp, ELS_PRLI,
				  fc_rport_prli_resp, rport, lport->e_d_tov))
		fc_rport_error_retry(rport, fp);
	else
		get_device(&rport->dev);
}

/**
 * fc_rport_els_rtv_resp() - Request Timeout Value response handler
 * @sp: current sequence in the RTV exchange
 * @fp: response frame
 * @rp_arg: Fibre Channel remote port
 *
 * Many targets don't seem to support this.
 *
 * Locking Note: This function will be called without the rport lock
 * held, but it will lock, call an _enter_* function or fc_rport_error
 * and then unlock the rport.
 */
static void fc_rport_rtv_resp(struct fc_seq *sp, struct fc_frame *fp,
			      void *rp_arg)
{
	struct fc_rport *rport = rp_arg;
	struct fc_rport_libfc_priv *rdata = rport->dd_data;
	u8 op;

	mutex_lock(&rdata->rp_mutex);

	FC_DEBUG_RPORT("Received a RTV response from port (%6x)\n",
		       rport->port_id);

	if (rdata->rp_state != RPORT_ST_RTV) {
		FC_DBG("Received a RTV response, but in state %s\n",
		       fc_rport_state(rport));
		if (IS_ERR(fp))
			goto err;
		goto out;
	}

	if (IS_ERR(fp)) {
		fc_rport_error(rport, fp);
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

	fc_rport_enter_ready(rport);

out:
	fc_frame_free(fp);
err:
	mutex_unlock(&rdata->rp_mutex);
	put_device(&rport->dev);
}

/**
 * fc_rport_enter_rtv() - Send Request Timeout Value (RTV) request to peer
 * @rport: Fibre Channel remote port to send RTV to
 *
 * Locking Note: The rport lock is expected to be held before calling
 * this routine.
 */
static void fc_rport_enter_rtv(struct fc_rport *rport)
{
	struct fc_frame *fp;
	struct fc_rport_libfc_priv *rdata = rport->dd_data;
	struct fc_lport *lport = rdata->local_port;

	FC_DEBUG_RPORT("Port (%6x) entered RTV state from %s state\n",
		       rport->port_id, fc_rport_state(rport));

	fc_rport_state_enter(rport, RPORT_ST_RTV);

	fp = fc_frame_alloc(lport, sizeof(struct fc_els_rtv));
	if (!fp) {
		fc_rport_error_retry(rport, fp);
		return;
	}

	if (!lport->tt.elsct_send(lport, rport, fp, ELS_RTV,
				     fc_rport_rtv_resp, rport, lport->e_d_tov))
		fc_rport_error_retry(rport, fp);
	else
		get_device(&rport->dev);
}

/**
 * fc_rport_enter_logo() - Send Logout (LOGO) request to peer
 * @rport: Fibre Channel remote port to send LOGO to
 *
 * Locking Note: The rport lock is expected to be held before calling
 * this routine.
 */
static void fc_rport_enter_logo(struct fc_rport *rport)
{
	struct fc_rport_libfc_priv *rdata = rport->dd_data;
	struct fc_lport *lport = rdata->local_port;
	struct fc_frame *fp;

	FC_DEBUG_RPORT("Port (%6x) entered LOGO state from %s state\n",
		       rport->port_id, fc_rport_state(rport));

	fc_rport_state_enter(rport, RPORT_ST_LOGO);

	fp = fc_frame_alloc(lport, sizeof(struct fc_els_logo));
	if (!fp) {
		fc_rport_error_retry(rport, fp);
		return;
	}

	if (!lport->tt.elsct_send(lport, rport, fp, ELS_LOGO,
				  fc_rport_logo_resp, rport, lport->e_d_tov))
		fc_rport_error_retry(rport, fp);
	else
		get_device(&rport->dev);
}


/**
 * fc_rport_recv_req() - Receive a request from a rport
 * @sp: current sequence in the PLOGI exchange
 * @fp: response frame
 * @rp_arg: Fibre Channel remote port
 *
 * Locking Note: Called without the rport lock held. This
 * function will hold the rport lock, call an _enter_*
 * function and then unlock the rport.
 */
void fc_rport_recv_req(struct fc_seq *sp, struct fc_frame *fp,
		       struct fc_rport *rport)
{
	struct fc_rport_libfc_priv *rdata = rport->dd_data;
	struct fc_lport *lport = rdata->local_port;

	struct fc_frame_header *fh;
	struct fc_seq_els_data els_data;
	u8 op;

	mutex_lock(&rdata->rp_mutex);

	els_data.fp = NULL;
	els_data.explan = ELS_EXPL_NONE;
	els_data.reason = ELS_RJT_NONE;

	fh = fc_frame_header_get(fp);

	if (fh->fh_r_ctl == FC_RCTL_ELS_REQ && fh->fh_type == FC_TYPE_ELS) {
		op = fc_frame_payload_op(fp);
		switch (op) {
		case ELS_PLOGI:
			fc_rport_recv_plogi_req(rport, sp, fp);
			break;
		case ELS_PRLI:
			fc_rport_recv_prli_req(rport, sp, fp);
			break;
		case ELS_PRLO:
			fc_rport_recv_prlo_req(rport, sp, fp);
			break;
		case ELS_LOGO:
			fc_rport_recv_logo_req(rport, sp, fp);
			break;
		case ELS_RRQ:
			els_data.fp = fp;
			lport->tt.seq_els_rsp_send(sp, ELS_RRQ, &els_data);
			break;
		case ELS_REC:
			els_data.fp = fp;
			lport->tt.seq_els_rsp_send(sp, ELS_REC, &els_data);
			break;
		default:
			els_data.reason = ELS_RJT_UNSUP;
			lport->tt.seq_els_rsp_send(sp, ELS_LS_RJT, &els_data);
			break;
		}
	}

	mutex_unlock(&rdata->rp_mutex);
}

/**
 * fc_rport_recv_plogi_req() - Handle incoming Port Login (PLOGI) request
 * @rport: Fibre Channel remote port that initiated PLOGI
 * @sp: current sequence in the PLOGI exchange
 * @fp: PLOGI request frame
 *
 * Locking Note: The rport lock is exected to be held before calling
 * this function.
 */
static void fc_rport_recv_plogi_req(struct fc_rport *rport,
				    struct fc_seq *sp, struct fc_frame *rx_fp)
{
	struct fc_rport_libfc_priv *rdata = rport->dd_data;
	struct fc_lport *lport = rdata->local_port;
	struct fc_frame *fp = rx_fp;
	struct fc_exch *ep;
	struct fc_frame_header *fh;
	struct fc_els_flogi *pl;
	struct fc_seq_els_data rjt_data;
	u32 sid;
	u64 wwpn;
	u64 wwnn;
	enum fc_els_rjt_reason reject = 0;
	u32 f_ctl;
	rjt_data.fp = NULL;

	fh = fc_frame_header_get(fp);

	FC_DEBUG_RPORT("Received PLOGI request from port (%6x) "
		       "while in state %s\n", ntoh24(fh->fh_s_id),
		       fc_rport_state(rport));

	sid = ntoh24(fh->fh_s_id);
	pl = fc_frame_payload_get(fp, sizeof(*pl));
	if (!pl) {
		FC_DBG("incoming PLOGI from %x too short\n", sid);
		WARN_ON(1);
		/* XXX TBD: send reject? */
		fc_frame_free(fp);
		return;
	}
	wwpn = get_unaligned_be64(&pl->fl_wwpn);
	wwnn = get_unaligned_be64(&pl->fl_wwnn);

	/*
	 * If the session was just created, possibly due to the incoming PLOGI,
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
		FC_DEBUG_RPORT("incoming PLOGI from %6x wwpn %llx state INIT "
			       "- reject\n", sid, (unsigned long long)wwpn);
		reject = ELS_RJT_UNSUP;
		break;
	case RPORT_ST_PLOGI:
		FC_DEBUG_RPORT("incoming PLOGI from %x in PLOGI state %d\n",
			       sid, rdata->rp_state);
		if (wwpn < lport->wwpn)
			reject = ELS_RJT_INPROG;
		break;
	case RPORT_ST_PRLI:
	case RPORT_ST_READY:
		FC_DEBUG_RPORT("incoming PLOGI from %x in logged-in state %d "
			       "- ignored for now\n", sid, rdata->rp_state);
		/* XXX TBD - should reset */
		break;
	case RPORT_ST_NONE:
	default:
		FC_DEBUG_RPORT("incoming PLOGI from %x in unexpected "
			       "state %d\n", sid, rdata->rp_state);
		fc_frame_free(fp);
		return;
		break;
	}

	if (reject) {
		rjt_data.reason = reject;
		rjt_data.explan = ELS_EXPL_NONE;
		lport->tt.seq_els_rsp_send(sp, ELS_LS_RJT, &rjt_data);
		fc_frame_free(fp);
	} else {
		fp = fc_frame_alloc(lport, sizeof(*pl));
		if (fp == NULL) {
			fp = rx_fp;
			rjt_data.reason = ELS_RJT_UNAB;
			rjt_data.explan = ELS_EXPL_NONE;
			lport->tt.seq_els_rsp_send(sp, ELS_LS_RJT, &rjt_data);
			fc_frame_free(fp);
		} else {
			sp = lport->tt.seq_start_next(sp);
			WARN_ON(!sp);
			fc_rport_set_name(rport, wwpn, wwnn);

			/*
			 * Get session payload size from incoming PLOGI.
			 */
			rport->maxframe_size =
				fc_plogi_get_maxframe(pl, lport->mfs);
			fc_frame_free(rx_fp);
			fc_plogi_fill(lport, fp, ELS_LS_ACC);

			/*
			 * Send LS_ACC.	 If this fails,
			 * the originator should retry.
			 */
			f_ctl = FC_FC_EX_CTX | FC_FC_LAST_SEQ;
			f_ctl |= FC_FC_END_SEQ | FC_FC_SEQ_INIT;
			ep = fc_seq_exch(sp);
			fc_fill_fc_hdr(fp, FC_RCTL_ELS_REP, ep->did, ep->sid,
				       FC_TYPE_ELS, f_ctl, 0);
			lport->tt.seq_send(lport, sp, fp);
			if (rdata->rp_state == RPORT_ST_PLOGI)
				fc_rport_enter_prli(rport);
		}
	}
}

/**
 * fc_rport_recv_prli_req() - Handle incoming Process Login (PRLI) request
 * @rport: Fibre Channel remote port that initiated PRLI
 * @sp: current sequence in the PRLI exchange
 * @fp: PRLI request frame
 *
 * Locking Note: The rport lock is exected to be held before calling
 * this function.
 */
static void fc_rport_recv_prli_req(struct fc_rport *rport,
				   struct fc_seq *sp, struct fc_frame *rx_fp)
{
	struct fc_rport_libfc_priv *rdata = rport->dd_data;
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

	FC_DEBUG_RPORT("Received PRLI request from port (%6x) "
		       "while in state %s\n", ntoh24(fh->fh_s_id),
		       fc_rport_state(rport));

	switch (rdata->rp_state) {
	case RPORT_ST_PRLI:
	case RPORT_ST_READY:
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
				if (fcp_parm * FCP_SPPF_RETRY)
					rdata->flags |= FC_RP_FLAGS_RETRY;
				rport->supported_classes = FC_COS_CLASS3;
				if (fcp_parm & FCP_SPPF_INIT_FCN)
					roles |= FC_RPORT_ROLE_FCP_INITIATOR;
				if (fcp_parm & FCP_SPPF_TARG_FCN)
					roles |= FC_RPORT_ROLE_FCP_TARGET;
				rport->roles = roles;

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
			fc_rport_enter_ready(rport);
			break;
		case RPORT_ST_READY:
			break;
		default:
			break;
		}
	}
	fc_frame_free(rx_fp);
}

/**
 * fc_rport_recv_prlo_req() - Handle incoming Process Logout (PRLO) request
 * @rport: Fibre Channel remote port that initiated PRLO
 * @sp: current sequence in the PRLO exchange
 * @fp: PRLO request frame
 *
 * Locking Note: The rport lock is exected to be held before calling
 * this function.
 */
static void fc_rport_recv_prlo_req(struct fc_rport *rport, struct fc_seq *sp,
				   struct fc_frame *fp)
{
	struct fc_rport_libfc_priv *rdata = rport->dd_data;
	struct fc_lport *lport = rdata->local_port;

	struct fc_frame_header *fh;
	struct fc_seq_els_data rjt_data;

	fh = fc_frame_header_get(fp);

	FC_DEBUG_RPORT("Received PRLO request from port (%6x) "
		       "while in state %s\n", ntoh24(fh->fh_s_id),
		       fc_rport_state(rport));

	if (rdata->rp_state == RPORT_ST_NONE) {
		fc_frame_free(fp);
		return;
	}

	rjt_data.fp = NULL;
	rjt_data.reason = ELS_RJT_UNAB;
	rjt_data.explan = ELS_EXPL_NONE;
	lport->tt.seq_els_rsp_send(sp, ELS_LS_RJT, &rjt_data);
	fc_frame_free(fp);
}

/**
 * fc_rport_recv_logo_req() - Handle incoming Logout (LOGO) request
 * @rport: Fibre Channel remote port that initiated LOGO
 * @sp: current sequence in the LOGO exchange
 * @fp: LOGO request frame
 *
 * Locking Note: The rport lock is exected to be held before calling
 * this function.
 */
static void fc_rport_recv_logo_req(struct fc_rport *rport, struct fc_seq *sp,
				   struct fc_frame *fp)
{
	struct fc_frame_header *fh;
	struct fc_rport_libfc_priv *rdata = rport->dd_data;
	struct fc_lport *lport = rdata->local_port;

	fh = fc_frame_header_get(fp);

	FC_DEBUG_RPORT("Received LOGO request from port (%6x) "
		       "while in state %s\n", ntoh24(fh->fh_s_id),
		       fc_rport_state(rport));

	if (rdata->rp_state == RPORT_ST_NONE) {
		fc_frame_free(fp);
		return;
	}

	rdata->event = RPORT_EV_LOGO;
	fc_rport_state_enter(rport, RPORT_ST_NONE);
	queue_work(rport_event_queue, &rdata->event_work);

	lport->tt.seq_els_rsp_send(sp, ELS_LS_ACC, NULL);
	fc_frame_free(fp);
}

static void fc_rport_flush_queue(void)
{
	flush_workqueue(rport_event_queue);
}


int fc_rport_init(struct fc_lport *lport)
{
	if (!lport->tt.rport_create)
		lport->tt.rport_create = fc_rport_rogue_create;

	if (!lport->tt.rport_login)
		lport->tt.rport_login = fc_rport_login;

	if (!lport->tt.rport_logoff)
		lport->tt.rport_logoff = fc_rport_logoff;

	if (!lport->tt.rport_recv_req)
		lport->tt.rport_recv_req = fc_rport_recv_req;

	if (!lport->tt.rport_flush_queue)
		lport->tt.rport_flush_queue = fc_rport_flush_queue;

	return 0;
}
EXPORT_SYMBOL(fc_rport_init);

int fc_setup_rport(void)
{
	rport_event_queue = create_singlethread_workqueue("fc_rport_eq");
	if (!rport_event_queue)
		return -ENOMEM;
	return 0;
}
EXPORT_SYMBOL(fc_setup_rport);

void fc_destroy_rport(void)
{
	destroy_workqueue(rport_event_queue);
}
EXPORT_SYMBOL(fc_destroy_rport);

void fc_rport_terminate_io(struct fc_rport *rport)
{
	struct fc_rport_libfc_priv *rdata = rport->dd_data;
	struct fc_lport *lport = rdata->local_port;

	lport->tt.exch_mgr_reset(lport, 0, rport->port_id);
	lport->tt.exch_mgr_reset(lport, rport->port_id, 0);
}
EXPORT_SYMBOL(fc_rport_terminate_io);

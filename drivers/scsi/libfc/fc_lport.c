/*
 * Copyright(c) 2007 Intel Corporation. All rights reserved.
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
 * PORT LOCKING NOTES
 *
 * These comments only apply to the 'port code' which consists of the lport,
 * disc and rport blocks.
 *
 * MOTIVATION
 *
 * The lport, disc and rport blocks all have mutexes that are used to protect
 * those objects. The main motivation for these locks is to prevent from
 * having an lport reset just before we send a frame. In that scenario the
 * lport's FID would get set to zero and then we'd send a frame with an
 * invalid SID. We also need to ensure that states don't change unexpectedly
 * while processing another state.
 *
 * HEIRARCHY
 *
 * The following heirarchy defines the locking rules. A greater lock
 * may be held before acquiring a lesser lock, but a lesser lock should never
 * be held while attempting to acquire a greater lock. Here is the heirarchy-
 *
 * lport > disc, lport > rport, disc > rport
 *
 * CALLBACKS
 *
 * The callbacks cause complications with this scheme. There is a callback
 * from the rport (to either lport or disc) and a callback from disc
 * (to the lport).
 *
 * As rports exit the rport state machine a callback is made to the owner of
 * the rport to notify success or failure. Since the callback is likely to
 * cause the lport or disc to grab its lock we cannot hold the rport lock
 * while making the callback. To ensure that the rport is not free'd while
 * processing the callback the rport callbacks are serialized through a
 * single-threaded workqueue. An rport would never be free'd while in a
 * callback handler becuase no other rport work in this queue can be executed
 * at the same time.
 *
 * When discovery succeeds or fails a callback is made to the lport as
 * notification. Currently, succesful discovery causes the lport to take no
 * action. A failure will cause the lport to reset. There is likely a circular
 * locking problem with this implementation.
 */

/*
 * LPORT LOCKING
 *
 * The critical sections protected by the lport's mutex are quite broad and
 * may be improved upon in the future. The lport code and its locking doesn't
 * influence the I/O path, so excessive locking doesn't penalize I/O
 * performance.
 *
 * The strategy is to lock whenever processing a request or response. Note
 * that every _enter_* function corresponds to a state change. They generally
 * change the lports state and then send a request out on the wire. We lock
 * before calling any of these functions to protect that state change. This
 * means that the entry points into the lport block manage the locks while
 * the state machine can transition between states (i.e. _enter_* functions)
 * while always staying protected.
 *
 * When handling responses we also hold the lport mutex broadly. When the
 * lport receives the response frame it locks the mutex and then calls the
 * appropriate handler for the particuar response. Generally a response will
 * trigger a state change and so the lock must already be held.
 *
 * Retries also have to consider the locking. The retries occur from a work
 * context and the work function will lock the lport and then retry the state
 * (i.e. _enter_* function).
 */

#include <linux/timer.h>
#include <asm/unaligned.h>

#include <scsi/fc/fc_gs.h>

#include <scsi/libfc.h>
#include <scsi/fc_encode.h>

/* Fabric IDs to use for point-to-point mode, chosen on whims. */
#define FC_LOCAL_PTP_FID_LO   0x010101
#define FC_LOCAL_PTP_FID_HI   0x010102

#define	DNS_DELAY	      3 /* Discovery delay after RSCN (in seconds)*/

static void fc_lport_error(struct fc_lport *, struct fc_frame *);

static void fc_lport_enter_reset(struct fc_lport *);
static void fc_lport_enter_flogi(struct fc_lport *);
static void fc_lport_enter_dns(struct fc_lport *);
static void fc_lport_enter_rpn_id(struct fc_lport *);
static void fc_lport_enter_rft_id(struct fc_lport *);
static void fc_lport_enter_scr(struct fc_lport *);
static void fc_lport_enter_ready(struct fc_lport *);
static void fc_lport_enter_logo(struct fc_lport *);

static const char *fc_lport_state_names[] = {
	[LPORT_ST_DISABLED] = "disabled",
	[LPORT_ST_FLOGI] =    "FLOGI",
	[LPORT_ST_DNS] =      "dNS",
	[LPORT_ST_RPN_ID] =   "RPN_ID",
	[LPORT_ST_RFT_ID] =   "RFT_ID",
	[LPORT_ST_SCR] =      "SCR",
	[LPORT_ST_READY] =    "Ready",
	[LPORT_ST_LOGO] =     "LOGO",
	[LPORT_ST_RESET] =    "reset",
};

static int fc_frame_drop(struct fc_lport *lport, struct fc_frame *fp)
{
	fc_frame_free(fp);
	return 0;
}

/**
 * fc_lport_rport_callback() - Event handler for rport events
 * @lport: The lport which is receiving the event
 * @rdata: private remote port data
 * @event: The event that occured
 *
 * Locking Note: The rport lock should not be held when calling
 *		 this function.
 */
static void fc_lport_rport_callback(struct fc_lport *lport,
				    struct fc_rport_priv *rdata,
				    enum fc_rport_event event)
{
	FC_LPORT_DBG(lport, "Received a %d event for port (%6x)\n", event,
		     rdata->ids.port_id);

	mutex_lock(&lport->lp_mutex);
	switch (event) {
	case RPORT_EV_READY:
		if (lport->state == LPORT_ST_DNS) {
			lport->dns_rp = rdata;
			fc_lport_enter_rpn_id(lport);
		} else {
			FC_LPORT_DBG(lport, "Received an READY event "
				     "on port (%6x) for the directory "
				     "server, but the lport is not "
				     "in the DNS state, it's in the "
				     "%d state", rdata->ids.port_id,
				     lport->state);
			lport->tt.rport_logoff(rdata);
		}
		break;
	case RPORT_EV_LOGO:
	case RPORT_EV_FAILED:
	case RPORT_EV_STOP:
		lport->dns_rp = NULL;
		break;
	case RPORT_EV_NONE:
		break;
	}
	mutex_unlock(&lport->lp_mutex);
}

/**
 * fc_lport_state() - Return a string which represents the lport's state
 * @lport: The lport whose state is to converted to a string
 */
static const char *fc_lport_state(struct fc_lport *lport)
{
	const char *cp;

	cp = fc_lport_state_names[lport->state];
	if (!cp)
		cp = "unknown";
	return cp;
}

/**
 * fc_lport_ptp_setup() - Create an rport for point-to-point mode
 * @lport: The lport to attach the ptp rport to
 * @fid: The FID of the ptp rport
 * @remote_wwpn: The WWPN of the ptp rport
 * @remote_wwnn: The WWNN of the ptp rport
 */
static void fc_lport_ptp_setup(struct fc_lport *lport,
			       u32 remote_fid, u64 remote_wwpn,
			       u64 remote_wwnn)
{
	mutex_lock(&lport->disc.disc_mutex);
	if (lport->ptp_rp)
		lport->tt.rport_logoff(lport->ptp_rp);
	lport->ptp_rp = lport->tt.rport_create(lport, remote_fid);
	lport->ptp_rp->ids.port_name = remote_wwpn;
	lport->ptp_rp->ids.node_name = remote_wwnn;
	mutex_unlock(&lport->disc.disc_mutex);

	lport->tt.rport_login(lport->ptp_rp);

	fc_lport_enter_ready(lport);
}

void fc_get_host_port_type(struct Scsi_Host *shost)
{
	/* TODO - currently just NPORT */
	fc_host_port_type(shost) = FC_PORTTYPE_NPORT;
}
EXPORT_SYMBOL(fc_get_host_port_type);

void fc_get_host_port_state(struct Scsi_Host *shost)
{
	struct fc_lport *lp = shost_priv(shost);

	if (lp->link_up)
		fc_host_port_state(shost) = FC_PORTSTATE_ONLINE;
	else
		fc_host_port_state(shost) = FC_PORTSTATE_OFFLINE;
}
EXPORT_SYMBOL(fc_get_host_port_state);

void fc_get_host_speed(struct Scsi_Host *shost)
{
	struct fc_lport *lport = shost_priv(shost);

	fc_host_speed(shost) = lport->link_speed;
}
EXPORT_SYMBOL(fc_get_host_speed);

struct fc_host_statistics *fc_get_host_stats(struct Scsi_Host *shost)
{
	struct fc_host_statistics *fcoe_stats;
	struct fc_lport *lp = shost_priv(shost);
	struct timespec v0, v1;
	unsigned int cpu;

	fcoe_stats = &lp->host_stats;
	memset(fcoe_stats, 0, sizeof(struct fc_host_statistics));

	jiffies_to_timespec(jiffies, &v0);
	jiffies_to_timespec(lp->boot_time, &v1);
	fcoe_stats->seconds_since_last_reset = (v0.tv_sec - v1.tv_sec);

	for_each_possible_cpu(cpu) {
		struct fcoe_dev_stats *stats;

		stats = per_cpu_ptr(lp->dev_stats, cpu);

		fcoe_stats->tx_frames += stats->TxFrames;
		fcoe_stats->tx_words += stats->TxWords;
		fcoe_stats->rx_frames += stats->RxFrames;
		fcoe_stats->rx_words += stats->RxWords;
		fcoe_stats->error_frames += stats->ErrorFrames;
		fcoe_stats->invalid_crc_count += stats->InvalidCRCCount;
		fcoe_stats->fcp_input_requests += stats->InputRequests;
		fcoe_stats->fcp_output_requests += stats->OutputRequests;
		fcoe_stats->fcp_control_requests += stats->ControlRequests;
		fcoe_stats->fcp_input_megabytes += stats->InputMegabytes;
		fcoe_stats->fcp_output_megabytes += stats->OutputMegabytes;
		fcoe_stats->link_failure_count += stats->LinkFailureCount;
	}
	fcoe_stats->lip_count = -1;
	fcoe_stats->nos_count = -1;
	fcoe_stats->loss_of_sync_count = -1;
	fcoe_stats->loss_of_signal_count = -1;
	fcoe_stats->prim_seq_protocol_err_count = -1;
	fcoe_stats->dumped_frames = -1;
	return fcoe_stats;
}
EXPORT_SYMBOL(fc_get_host_stats);

/*
 * Fill in FLOGI command for request.
 */
static void
fc_lport_flogi_fill(struct fc_lport *lport, struct fc_els_flogi *flogi,
		    unsigned int op)
{
	struct fc_els_csp *sp;
	struct fc_els_cssp *cp;

	memset(flogi, 0, sizeof(*flogi));
	flogi->fl_cmd = (u8) op;
	put_unaligned_be64(lport->wwpn, &flogi->fl_wwpn);
	put_unaligned_be64(lport->wwnn, &flogi->fl_wwnn);
	sp = &flogi->fl_csp;
	sp->sp_hi_ver = 0x20;
	sp->sp_lo_ver = 0x20;
	sp->sp_bb_cred = htons(10);	/* this gets set by gateway */
	sp->sp_bb_data = htons((u16) lport->mfs);
	cp = &flogi->fl_cssp[3 - 1];	/* class 3 parameters */
	cp->cp_class = htons(FC_CPC_VALID | FC_CPC_SEQ);
	if (op != ELS_FLOGI) {
		sp->sp_features = htons(FC_SP_FT_CIRO);
		sp->sp_tot_seq = htons(255);	/* seq. we accept */
		sp->sp_rel_off = htons(0x1f);
		sp->sp_e_d_tov = htonl(lport->e_d_tov);

		cp->cp_rdfs = htons((u16) lport->mfs);
		cp->cp_con_seq = htons(255);
		cp->cp_open_seq = 1;
	}
}

/*
 * Add a supported FC-4 type.
 */
static void fc_lport_add_fc4_type(struct fc_lport *lport, enum fc_fh_type type)
{
	__be32 *mp;

	mp = &lport->fcts.ff_type_map[type / FC_NS_BPW];
	*mp = htonl(ntohl(*mp) | 1UL << (type % FC_NS_BPW));
}

/**
 * fc_lport_recv_rlir_req() - Handle received Registered Link Incident Report.
 * @lport: Fibre Channel local port recieving the RLIR
 * @sp: current sequence in the RLIR exchange
 * @fp: RLIR request frame
 *
 * Locking Note: The lport lock is expected to be held before calling
 * this function.
 */
static void fc_lport_recv_rlir_req(struct fc_seq *sp, struct fc_frame *fp,
				   struct fc_lport *lport)
{
	FC_LPORT_DBG(lport, "Received RLIR request while in state %s\n",
		     fc_lport_state(lport));

	lport->tt.seq_els_rsp_send(sp, ELS_LS_ACC, NULL);
	fc_frame_free(fp);
}

/**
 * fc_lport_recv_echo_req() - Handle received ECHO request
 * @lport: Fibre Channel local port recieving the ECHO
 * @sp: current sequence in the ECHO exchange
 * @fp: ECHO request frame
 *
 * Locking Note: The lport lock is expected to be held before calling
 * this function.
 */
static void fc_lport_recv_echo_req(struct fc_seq *sp, struct fc_frame *in_fp,
				   struct fc_lport *lport)
{
	struct fc_frame *fp;
	struct fc_exch *ep = fc_seq_exch(sp);
	unsigned int len;
	void *pp;
	void *dp;
	u32 f_ctl;

	FC_LPORT_DBG(lport, "Received ECHO request while in state %s\n",
		     fc_lport_state(lport));

	len = fr_len(in_fp) - sizeof(struct fc_frame_header);
	pp = fc_frame_payload_get(in_fp, len);

	if (len < sizeof(__be32))
		len = sizeof(__be32);

	fp = fc_frame_alloc(lport, len);
	if (fp) {
		dp = fc_frame_payload_get(fp, len);
		memcpy(dp, pp, len);
		*((__be32 *)dp) = htonl(ELS_LS_ACC << 24);
		sp = lport->tt.seq_start_next(sp);
		f_ctl = FC_FC_EX_CTX | FC_FC_LAST_SEQ | FC_FC_END_SEQ;
		fc_fill_fc_hdr(fp, FC_RCTL_ELS_REP, ep->did, ep->sid,
			       FC_TYPE_ELS, f_ctl, 0);
		lport->tt.seq_send(lport, sp, fp);
	}
	fc_frame_free(in_fp);
}

/**
 * fc_lport_recv_rnid_req() - Handle received Request Node ID data request
 * @sp:	   The sequence in the RNID exchange
 * @fp:	   The RNID request frame
 * @lport: The local port recieving the RNID
 *
 * Locking Note: The lport lock is expected to be held before calling
 * this function.
 */
static void fc_lport_recv_rnid_req(struct fc_seq *sp, struct fc_frame *in_fp,
				   struct fc_lport *lport)
{
	struct fc_frame *fp;
	struct fc_exch *ep = fc_seq_exch(sp);
	struct fc_els_rnid *req;
	struct {
		struct fc_els_rnid_resp rnid;
		struct fc_els_rnid_cid cid;
		struct fc_els_rnid_gen gen;
	} *rp;
	struct fc_seq_els_data rjt_data;
	u8 fmt;
	size_t len;
	u32 f_ctl;

	FC_LPORT_DBG(lport, "Received RNID request while in state %s\n",
		     fc_lport_state(lport));

	req = fc_frame_payload_get(in_fp, sizeof(*req));
	if (!req) {
		rjt_data.fp = NULL;
		rjt_data.reason = ELS_RJT_LOGIC;
		rjt_data.explan = ELS_EXPL_NONE;
		lport->tt.seq_els_rsp_send(sp, ELS_LS_RJT, &rjt_data);
	} else {
		fmt = req->rnid_fmt;
		len = sizeof(*rp);
		if (fmt != ELS_RNIDF_GEN ||
		    ntohl(lport->rnid_gen.rnid_atype) == 0) {
			fmt = ELS_RNIDF_NONE;	/* nothing to provide */
			len -= sizeof(rp->gen);
		}
		fp = fc_frame_alloc(lport, len);
		if (fp) {
			rp = fc_frame_payload_get(fp, len);
			memset(rp, 0, len);
			rp->rnid.rnid_cmd = ELS_LS_ACC;
			rp->rnid.rnid_fmt = fmt;
			rp->rnid.rnid_cid_len = sizeof(rp->cid);
			rp->cid.rnid_wwpn = htonll(lport->wwpn);
			rp->cid.rnid_wwnn = htonll(lport->wwnn);
			if (fmt == ELS_RNIDF_GEN) {
				rp->rnid.rnid_sid_len = sizeof(rp->gen);
				memcpy(&rp->gen, &lport->rnid_gen,
				       sizeof(rp->gen));
			}
			sp = lport->tt.seq_start_next(sp);
			f_ctl = FC_FC_EX_CTX | FC_FC_LAST_SEQ;
			f_ctl |= FC_FC_END_SEQ | FC_FC_SEQ_INIT;
			fc_fill_fc_hdr(fp, FC_RCTL_ELS_REP, ep->did, ep->sid,
				       FC_TYPE_ELS, f_ctl, 0);
			lport->tt.seq_send(lport, sp, fp);
		}
	}
	fc_frame_free(in_fp);
}

/**
 * fc_lport_recv_logo_req() - Handle received fabric LOGO request
 * @lport: Fibre Channel local port recieving the LOGO
 * @sp: current sequence in the LOGO exchange
 * @fp: LOGO request frame
 *
 * Locking Note: The lport lock is exected to be held before calling
 * this function.
 */
static void fc_lport_recv_logo_req(struct fc_seq *sp, struct fc_frame *fp,
				   struct fc_lport *lport)
{
	lport->tt.seq_els_rsp_send(sp, ELS_LS_ACC, NULL);
	fc_lport_enter_reset(lport);
	fc_frame_free(fp);
}

/**
 * fc_fabric_login() - Start the lport state machine
 * @lport: The lport that should log into the fabric
 *
 * Locking Note: This function should not be called
 *		 with the lport lock held.
 */
int fc_fabric_login(struct fc_lport *lport)
{
	int rc = -1;

	mutex_lock(&lport->lp_mutex);
	if (lport->state == LPORT_ST_DISABLED) {
		fc_lport_enter_reset(lport);
		rc = 0;
	}
	mutex_unlock(&lport->lp_mutex);

	return rc;
}
EXPORT_SYMBOL(fc_fabric_login);

/**
 * fc_linkup() - Handler for transport linkup events
 * @lport: The lport whose link is up
 */
void fc_linkup(struct fc_lport *lport)
{
	printk(KERN_INFO "libfc: Link up on port (%6x)\n",
	       fc_host_port_id(lport->host));

	mutex_lock(&lport->lp_mutex);
	if (!lport->link_up) {
		lport->link_up = 1;

		if (lport->state == LPORT_ST_RESET)
			fc_lport_enter_flogi(lport);
	}
	mutex_unlock(&lport->lp_mutex);
}
EXPORT_SYMBOL(fc_linkup);

/**
 * fc_linkdown() - Handler for transport linkdown events
 * @lport: The lport whose link is down
 */
void fc_linkdown(struct fc_lport *lport)
{
	mutex_lock(&lport->lp_mutex);
	printk(KERN_INFO "libfc: Link down on port (%6x)\n",
	       fc_host_port_id(lport->host));

	if (lport->link_up) {
		lport->link_up = 0;
		fc_lport_enter_reset(lport);
		lport->tt.fcp_cleanup(lport);
	}
	mutex_unlock(&lport->lp_mutex);
}
EXPORT_SYMBOL(fc_linkdown);

/**
 * fc_fabric_logoff() - Logout of the fabric
 * @lport:	      fc_lport pointer to logoff the fabric
 *
 * Return value:
 *	0 for success, -1 for failure
 */
int fc_fabric_logoff(struct fc_lport *lport)
{
	lport->tt.disc_stop_final(lport);
	mutex_lock(&lport->lp_mutex);
	if (lport->dns_rp)
		lport->tt.rport_logoff(lport->dns_rp);
	mutex_unlock(&lport->lp_mutex);
	lport->tt.rport_flush_queue();
	mutex_lock(&lport->lp_mutex);
	fc_lport_enter_logo(lport);
	mutex_unlock(&lport->lp_mutex);
	cancel_delayed_work_sync(&lport->retry_work);
	return 0;
}
EXPORT_SYMBOL(fc_fabric_logoff);

/**
 * fc_lport_destroy() - unregister a fc_lport
 * @lport:	      fc_lport pointer to unregister
 *
 * Return value:
 *	None
 * Note:
 * exit routine for fc_lport instance
 * clean-up all the allocated memory
 * and free up other system resources.
 *
 */
int fc_lport_destroy(struct fc_lport *lport)
{
	mutex_lock(&lport->lp_mutex);
	lport->state = LPORT_ST_DISABLED;
	lport->link_up = 0;
	lport->tt.frame_send = fc_frame_drop;
	mutex_unlock(&lport->lp_mutex);

	lport->tt.fcp_abort_io(lport);
	lport->tt.disc_stop_final(lport);
	lport->tt.exch_mgr_reset(lport, 0, 0);
	return 0;
}
EXPORT_SYMBOL(fc_lport_destroy);

/**
 * fc_set_mfs() - sets up the mfs for the corresponding fc_lport
 * @lport: fc_lport pointer to unregister
 * @mfs: the new mfs for fc_lport
 *
 * Set mfs for the given fc_lport to the new mfs.
 *
 * Return: 0 for success
 */
int fc_set_mfs(struct fc_lport *lport, u32 mfs)
{
	unsigned int old_mfs;
	int rc = -EINVAL;

	mutex_lock(&lport->lp_mutex);

	old_mfs = lport->mfs;

	if (mfs >= FC_MIN_MAX_FRAME) {
		mfs &= ~3;
		if (mfs > FC_MAX_FRAME)
			mfs = FC_MAX_FRAME;
		mfs -= sizeof(struct fc_frame_header);
		lport->mfs = mfs;
		rc = 0;
	}

	if (!rc && mfs < old_mfs)
		fc_lport_enter_reset(lport);

	mutex_unlock(&lport->lp_mutex);

	return rc;
}
EXPORT_SYMBOL(fc_set_mfs);

/**
 * fc_lport_disc_callback() - Callback for discovery events
 * @lport: FC local port
 * @event: The discovery event
 */
void fc_lport_disc_callback(struct fc_lport *lport, enum fc_disc_event event)
{
	switch (event) {
	case DISC_EV_SUCCESS:
		FC_LPORT_DBG(lport, "Discovery succeeded\n");
		break;
	case DISC_EV_FAILED:
		printk(KERN_ERR "libfc: Discovery failed for port (%6x)\n",
		       fc_host_port_id(lport->host));
		mutex_lock(&lport->lp_mutex);
		fc_lport_enter_reset(lport);
		mutex_unlock(&lport->lp_mutex);
		break;
	case DISC_EV_NONE:
		WARN_ON(1);
		break;
	}
}

/**
 * fc_rport_enter_ready() - Enter the ready state and start discovery
 * @lport: Fibre Channel local port that is ready
 *
 * Locking Note: The lport lock is expected to be held before calling
 * this routine.
 */
static void fc_lport_enter_ready(struct fc_lport *lport)
{
	FC_LPORT_DBG(lport, "Entered READY from state %s\n",
		     fc_lport_state(lport));

	fc_lport_state_enter(lport, LPORT_ST_READY);

	if (!lport->ptp_rp)
		lport->tt.disc_start(fc_lport_disc_callback, lport);
}

/**
 * fc_lport_recv_flogi_req() - Receive a FLOGI request
 * @sp_in: The sequence the FLOGI is on
 * @rx_fp: The frame the FLOGI is in
 * @lport: The lport that recieved the request
 *
 * A received FLOGI request indicates a point-to-point connection.
 * Accept it with the common service parameters indicating our N port.
 * Set up to do a PLOGI if we have the higher-number WWPN.
 *
 * Locking Note: The lport lock is expected to be held before calling
 * this function.
 */
static void fc_lport_recv_flogi_req(struct fc_seq *sp_in,
				    struct fc_frame *rx_fp,
				    struct fc_lport *lport)
{
	struct fc_frame *fp;
	struct fc_frame_header *fh;
	struct fc_seq *sp;
	struct fc_exch *ep;
	struct fc_els_flogi *flp;
	struct fc_els_flogi *new_flp;
	u64 remote_wwpn;
	u32 remote_fid;
	u32 local_fid;
	u32 f_ctl;

	FC_LPORT_DBG(lport, "Received FLOGI request while in state %s\n",
		     fc_lport_state(lport));

	fh = fc_frame_header_get(rx_fp);
	remote_fid = ntoh24(fh->fh_s_id);
	flp = fc_frame_payload_get(rx_fp, sizeof(*flp));
	if (!flp)
		goto out;
	remote_wwpn = get_unaligned_be64(&flp->fl_wwpn);
	if (remote_wwpn == lport->wwpn) {
		printk(KERN_WARNING "libfc: Received FLOGI from port "
		       "with same WWPN %llx\n", remote_wwpn);
		goto out;
	}
	FC_LPORT_DBG(lport, "FLOGI from port WWPN %llx\n", remote_wwpn);

	/*
	 * XXX what is the right thing to do for FIDs?
	 * The originator might expect our S_ID to be 0xfffffe.
	 * But if so, both of us could end up with the same FID.
	 */
	local_fid = FC_LOCAL_PTP_FID_LO;
	if (remote_wwpn < lport->wwpn) {
		local_fid = FC_LOCAL_PTP_FID_HI;
		if (!remote_fid || remote_fid == local_fid)
			remote_fid = FC_LOCAL_PTP_FID_LO;
	} else if (!remote_fid) {
		remote_fid = FC_LOCAL_PTP_FID_HI;
	}

	fc_host_port_id(lport->host) = local_fid;

	fp = fc_frame_alloc(lport, sizeof(*flp));
	if (fp) {
		sp = lport->tt.seq_start_next(fr_seq(rx_fp));
		new_flp = fc_frame_payload_get(fp, sizeof(*flp));
		fc_lport_flogi_fill(lport, new_flp, ELS_FLOGI);
		new_flp->fl_cmd = (u8) ELS_LS_ACC;

		/*
		 * Send the response.  If this fails, the originator should
		 * repeat the sequence.
		 */
		f_ctl = FC_FC_EX_CTX | FC_FC_LAST_SEQ | FC_FC_END_SEQ;
		ep = fc_seq_exch(sp);
		fc_fill_fc_hdr(fp, FC_RCTL_ELS_REP, ep->did, ep->sid,
			       FC_TYPE_ELS, f_ctl, 0);
		lport->tt.seq_send(lport, sp, fp);

	} else {
		fc_lport_error(lport, fp);
	}
	fc_lport_ptp_setup(lport, remote_fid, remote_wwpn,
			   get_unaligned_be64(&flp->fl_wwnn));

out:
	sp = fr_seq(rx_fp);
	fc_frame_free(rx_fp);
}

/**
 * fc_lport_recv_req() - The generic lport request handler
 * @lport: The lport that received the request
 * @sp: The sequence the request is on
 * @fp: The frame the request is in
 *
 * This function will see if the lport handles the request or
 * if an rport should handle the request.
 *
 * Locking Note: This function should not be called with the lport
 *		 lock held becuase it will grab the lock.
 */
static void fc_lport_recv_req(struct fc_lport *lport, struct fc_seq *sp,
			      struct fc_frame *fp)
{
	struct fc_frame_header *fh = fc_frame_header_get(fp);
	void (*recv) (struct fc_seq *, struct fc_frame *, struct fc_lport *);

	mutex_lock(&lport->lp_mutex);

	/*
	 * Handle special ELS cases like FLOGI, LOGO, and
	 * RSCN here.  These don't require a session.
	 * Even if we had a session, it might not be ready.
	 */
	if (!lport->link_up)
		fc_frame_free(fp);
	else if (fh->fh_type == FC_TYPE_ELS &&
		 fh->fh_r_ctl == FC_RCTL_ELS_REQ) {
		/*
		 * Check opcode.
		 */
		recv = lport->tt.rport_recv_req;
		switch (fc_frame_payload_op(fp)) {
		case ELS_FLOGI:
			recv = fc_lport_recv_flogi_req;
			break;
		case ELS_LOGO:
			fh = fc_frame_header_get(fp);
			if (ntoh24(fh->fh_s_id) == FC_FID_FLOGI)
				recv = fc_lport_recv_logo_req;
			break;
		case ELS_RSCN:
			recv = lport->tt.disc_recv_req;
			break;
		case ELS_ECHO:
			recv = fc_lport_recv_echo_req;
			break;
		case ELS_RLIR:
			recv = fc_lport_recv_rlir_req;
			break;
		case ELS_RNID:
			recv = fc_lport_recv_rnid_req;
			break;
		}

		recv(sp, fp, lport);
	} else {
		FC_LPORT_DBG(lport, "dropping invalid frame (eof %x)\n",
			     fr_eof(fp));
		fc_frame_free(fp);
	}
	mutex_unlock(&lport->lp_mutex);

	/*
	 *  The common exch_done for all request may not be good
	 *  if any request requires longer hold on exhange. XXX
	 */
	lport->tt.exch_done(sp);
}

/**
 * fc_lport_reset() - Reset an lport
 * @lport: The lport which should be reset
 *
 * Locking Note: This functions should not be called with the
 *		 lport lock held.
 */
int fc_lport_reset(struct fc_lport *lport)
{
	cancel_delayed_work_sync(&lport->retry_work);
	mutex_lock(&lport->lp_mutex);
	fc_lport_enter_reset(lport);
	mutex_unlock(&lport->lp_mutex);
	return 0;
}
EXPORT_SYMBOL(fc_lport_reset);

/**
 * fc_lport_reset_locked() - Reset the local port
 * @lport: Fibre Channel local port to be reset
 *
 * Locking Note: The lport lock is expected to be held before calling
 * this routine.
 */
static void fc_lport_reset_locked(struct fc_lport *lport)
{
	if (lport->dns_rp)
		lport->tt.rport_logoff(lport->dns_rp);

	lport->ptp_rp = NULL;

	lport->tt.disc_stop(lport);

	lport->tt.exch_mgr_reset(lport, 0, 0);
	fc_host_fabric_name(lport->host) = 0;
	fc_host_port_id(lport->host) = 0;
}

/**
 * fc_lport_enter_reset() - Reset the local port
 * @lport: Fibre Channel local port to be reset
 *
 * Locking Note: The lport lock is expected to be held before calling
 * this routine.
 */
static void fc_lport_enter_reset(struct fc_lport *lport)
{
	FC_LPORT_DBG(lport, "Entered RESET state from %s state\n",
		     fc_lport_state(lport));

	fc_lport_state_enter(lport, LPORT_ST_RESET);
	fc_lport_reset_locked(lport);
	if (lport->link_up)
		fc_lport_enter_flogi(lport);
}

/**
 * fc_lport_enter_disabled() - disable the local port
 * @lport: Fibre Channel local port to be reset
 *
 * Locking Note: The lport lock is expected to be held before calling
 * this routine.
 */
static void fc_lport_enter_disabled(struct fc_lport *lport)
{
	FC_LPORT_DBG(lport, "Entered disabled state from %s state\n",
		     fc_lport_state(lport));

	fc_lport_state_enter(lport, LPORT_ST_DISABLED);
	fc_lport_reset_locked(lport);
}

/**
 * fc_lport_error() - Handler for any errors
 * @lport: The fc_lport object
 * @fp: The frame pointer
 *
 * If the error was caused by a resource allocation failure
 * then wait for half a second and retry, otherwise retry
 * after the e_d_tov time.
 */
static void fc_lport_error(struct fc_lport *lport, struct fc_frame *fp)
{
	unsigned long delay = 0;
	FC_LPORT_DBG(lport, "Error %ld in state %s, retries %d\n",
		     PTR_ERR(fp), fc_lport_state(lport),
		     lport->retry_count);

	if (!fp || PTR_ERR(fp) == -FC_EX_TIMEOUT) {
		/*
		 * Memory allocation failure, or the exchange timed out.
		 *  Retry after delay
		 */
		if (lport->retry_count < lport->max_retry_count) {
			lport->retry_count++;
			if (!fp)
				delay = msecs_to_jiffies(500);
			else
				delay =	msecs_to_jiffies(lport->e_d_tov);

			schedule_delayed_work(&lport->retry_work, delay);
		} else {
			switch (lport->state) {
			case LPORT_ST_DISABLED:
			case LPORT_ST_READY:
			case LPORT_ST_RESET:
			case LPORT_ST_RPN_ID:
			case LPORT_ST_RFT_ID:
			case LPORT_ST_SCR:
			case LPORT_ST_DNS:
			case LPORT_ST_FLOGI:
			case LPORT_ST_LOGO:
				fc_lport_enter_reset(lport);
				break;
			}
		}
	}
}

/**
 * fc_lport_rft_id_resp() - Handle response to Register Fibre
 *			    Channel Types by ID (RPN_ID) request
 * @sp: current sequence in RPN_ID exchange
 * @fp: response frame
 * @lp_arg: Fibre Channel host port instance
 *
 * Locking Note: This function will be called without the lport lock
 * held, but it will lock, call an _enter_* function or fc_lport_error
 * and then unlock the lport.
 */
static void fc_lport_rft_id_resp(struct fc_seq *sp, struct fc_frame *fp,
				 void *lp_arg)
{
	struct fc_lport *lport = lp_arg;
	struct fc_frame_header *fh;
	struct fc_ct_hdr *ct;

	FC_LPORT_DBG(lport, "Received a RFT_ID %s\n", fc_els_resp_type(fp));

	if (fp == ERR_PTR(-FC_EX_CLOSED))
		return;

	mutex_lock(&lport->lp_mutex);

	if (lport->state != LPORT_ST_RFT_ID) {
		FC_LPORT_DBG(lport, "Received a RFT_ID response, but in state "
			     "%s\n", fc_lport_state(lport));
		if (IS_ERR(fp))
			goto err;
		goto out;
	}

	if (IS_ERR(fp)) {
		fc_lport_error(lport, fp);
		goto err;
	}

	fh = fc_frame_header_get(fp);
	ct = fc_frame_payload_get(fp, sizeof(*ct));

	if (fh && ct && fh->fh_type == FC_TYPE_CT &&
	    ct->ct_fs_type == FC_FST_DIR &&
	    ct->ct_fs_subtype == FC_NS_SUBTYPE &&
	    ntohs(ct->ct_cmd) == FC_FS_ACC)
		fc_lport_enter_scr(lport);
	else
		fc_lport_error(lport, fp);
out:
	fc_frame_free(fp);
err:
	mutex_unlock(&lport->lp_mutex);
}

/**
 * fc_lport_rpn_id_resp() - Handle response to Register Port
 *			    Name by ID (RPN_ID) request
 * @sp: current sequence in RPN_ID exchange
 * @fp: response frame
 * @lp_arg: Fibre Channel host port instance
 *
 * Locking Note: This function will be called without the lport lock
 * held, but it will lock, call an _enter_* function or fc_lport_error
 * and then unlock the lport.
 */
static void fc_lport_rpn_id_resp(struct fc_seq *sp, struct fc_frame *fp,
				 void *lp_arg)
{
	struct fc_lport *lport = lp_arg;
	struct fc_frame_header *fh;
	struct fc_ct_hdr *ct;

	FC_LPORT_DBG(lport, "Received a RPN_ID %s\n", fc_els_resp_type(fp));

	if (fp == ERR_PTR(-FC_EX_CLOSED))
		return;

	mutex_lock(&lport->lp_mutex);

	if (lport->state != LPORT_ST_RPN_ID) {
		FC_LPORT_DBG(lport, "Received a RPN_ID response, but in state "
			     "%s\n", fc_lport_state(lport));
		if (IS_ERR(fp))
			goto err;
		goto out;
	}

	if (IS_ERR(fp)) {
		fc_lport_error(lport, fp);
		goto err;
	}

	fh = fc_frame_header_get(fp);
	ct = fc_frame_payload_get(fp, sizeof(*ct));
	if (fh && ct && fh->fh_type == FC_TYPE_CT &&
	    ct->ct_fs_type == FC_FST_DIR &&
	    ct->ct_fs_subtype == FC_NS_SUBTYPE &&
	    ntohs(ct->ct_cmd) == FC_FS_ACC)
		fc_lport_enter_rft_id(lport);
	else
		fc_lport_error(lport, fp);

out:
	fc_frame_free(fp);
err:
	mutex_unlock(&lport->lp_mutex);
}

/**
 * fc_lport_scr_resp() - Handle response to State Change Register (SCR) request
 * @sp: current sequence in SCR exchange
 * @fp: response frame
 * @lp_arg: Fibre Channel lport port instance that sent the registration request
 *
 * Locking Note: This function will be called without the lport lock
 * held, but it will lock, call an _enter_* function or fc_lport_error
 * and then unlock the lport.
 */
static void fc_lport_scr_resp(struct fc_seq *sp, struct fc_frame *fp,
			      void *lp_arg)
{
	struct fc_lport *lport = lp_arg;
	u8 op;

	FC_LPORT_DBG(lport, "Received a SCR %s\n", fc_els_resp_type(fp));

	if (fp == ERR_PTR(-FC_EX_CLOSED))
		return;

	mutex_lock(&lport->lp_mutex);

	if (lport->state != LPORT_ST_SCR) {
		FC_LPORT_DBG(lport, "Received a SCR response, but in state "
			     "%s\n", fc_lport_state(lport));
		if (IS_ERR(fp))
			goto err;
		goto out;
	}

	if (IS_ERR(fp)) {
		fc_lport_error(lport, fp);
		goto err;
	}

	op = fc_frame_payload_op(fp);
	if (op == ELS_LS_ACC)
		fc_lport_enter_ready(lport);
	else
		fc_lport_error(lport, fp);

out:
	fc_frame_free(fp);
err:
	mutex_unlock(&lport->lp_mutex);
}

/**
 * fc_lport_enter_scr() - Send a State Change Register (SCR) request
 * @lport: Fibre Channel local port to register for state changes
 *
 * Locking Note: The lport lock is expected to be held before calling
 * this routine.
 */
static void fc_lport_enter_scr(struct fc_lport *lport)
{
	struct fc_frame *fp;

	FC_LPORT_DBG(lport, "Entered SCR state from %s state\n",
		     fc_lport_state(lport));

	fc_lport_state_enter(lport, LPORT_ST_SCR);

	fp = fc_frame_alloc(lport, sizeof(struct fc_els_scr));
	if (!fp) {
		fc_lport_error(lport, fp);
		return;
	}

	if (!lport->tt.elsct_send(lport, FC_FID_FCTRL, fp, ELS_SCR,
				  fc_lport_scr_resp, lport, lport->e_d_tov))
		fc_lport_error(lport, NULL);
}

/**
 * fc_lport_enter_rft_id() - Register FC4-types with the name server
 * @lport: Fibre Channel local port to register
 *
 * Locking Note: The lport lock is expected to be held before calling
 * this routine.
 */
static void fc_lport_enter_rft_id(struct fc_lport *lport)
{
	struct fc_frame *fp;
	struct fc_ns_fts *lps;
	int i;

	FC_LPORT_DBG(lport, "Entered RFT_ID state from %s state\n",
		     fc_lport_state(lport));

	fc_lport_state_enter(lport, LPORT_ST_RFT_ID);

	lps = &lport->fcts;
	i = sizeof(lps->ff_type_map) / sizeof(lps->ff_type_map[0]);
	while (--i >= 0)
		if (ntohl(lps->ff_type_map[i]) != 0)
			break;
	if (i < 0) {
		/* nothing to register, move on to SCR */
		fc_lport_enter_scr(lport);
		return;
	}

	fp = fc_frame_alloc(lport, sizeof(struct fc_ct_hdr) +
			    sizeof(struct fc_ns_rft));
	if (!fp) {
		fc_lport_error(lport, fp);
		return;
	}

	if (!lport->tt.elsct_send(lport, FC_FID_DIR_SERV, fp, FC_NS_RFT_ID,
				  fc_lport_rft_id_resp,
				  lport, lport->e_d_tov))
		fc_lport_error(lport, fp);
}

/**
 * fc_rport_enter_rft_id() - Register port name with the name server
 * @lport: Fibre Channel local port to register
 *
 * Locking Note: The lport lock is expected to be held before calling
 * this routine.
 */
static void fc_lport_enter_rpn_id(struct fc_lport *lport)
{
	struct fc_frame *fp;

	FC_LPORT_DBG(lport, "Entered RPN_ID state from %s state\n",
		     fc_lport_state(lport));

	fc_lport_state_enter(lport, LPORT_ST_RPN_ID);

	fp = fc_frame_alloc(lport, sizeof(struct fc_ct_hdr) +
			    sizeof(struct fc_ns_rn_id));
	if (!fp) {
		fc_lport_error(lport, fp);
		return;
	}

	if (!lport->tt.elsct_send(lport, FC_FID_DIR_SERV, fp, FC_NS_RPN_ID,
				  fc_lport_rpn_id_resp,
				  lport, lport->e_d_tov))
		fc_lport_error(lport, NULL);
}

static struct fc_rport_operations fc_lport_rport_ops = {
	.event_callback = fc_lport_rport_callback,
};

/**
 * fc_rport_enter_dns() - Create a rport to the name server
 * @lport: Fibre Channel local port requesting a rport for the name server
 *
 * Locking Note: The lport lock is expected to be held before calling
 * this routine.
 */
static void fc_lport_enter_dns(struct fc_lport *lport)
{
	struct fc_rport_priv *rdata;

	FC_LPORT_DBG(lport, "Entered DNS state from %s state\n",
		     fc_lport_state(lport));

	fc_lport_state_enter(lport, LPORT_ST_DNS);

	mutex_lock(&lport->disc.disc_mutex);
	rdata = lport->tt.rport_create(lport, FC_FID_DIR_SERV);
	mutex_unlock(&lport->disc.disc_mutex);
	if (!rdata)
		goto err;

	rdata->ops = &fc_lport_rport_ops;
	lport->tt.rport_login(rdata);
	return;

err:
	fc_lport_error(lport, NULL);
}

/**
 * fc_lport_timeout() - Handler for the retry_work timer.
 * @work: The work struct of the fc_lport
 */
static void fc_lport_timeout(struct work_struct *work)
{
	struct fc_lport *lport =
		container_of(work, struct fc_lport,
			     retry_work.work);

	mutex_lock(&lport->lp_mutex);

	switch (lport->state) {
	case LPORT_ST_DISABLED:
		WARN_ON(1);
		break;
	case LPORT_ST_READY:
		WARN_ON(1);
		break;
	case LPORT_ST_RESET:
		break;
	case LPORT_ST_FLOGI:
		fc_lport_enter_flogi(lport);
		break;
	case LPORT_ST_DNS:
		fc_lport_enter_dns(lport);
		break;
	case LPORT_ST_RPN_ID:
		fc_lport_enter_rpn_id(lport);
		break;
	case LPORT_ST_RFT_ID:
		fc_lport_enter_rft_id(lport);
		break;
	case LPORT_ST_SCR:
		fc_lport_enter_scr(lport);
		break;
	case LPORT_ST_LOGO:
		fc_lport_enter_logo(lport);
		break;
	}

	mutex_unlock(&lport->lp_mutex);
}

/**
 * fc_lport_logo_resp() - Handle response to LOGO request
 * @sp: current sequence in LOGO exchange
 * @fp: response frame
 * @lp_arg: Fibre Channel lport port instance that sent the LOGO request
 *
 * Locking Note: This function will be called without the lport lock
 * held, but it will lock, call an _enter_* function or fc_lport_error
 * and then unlock the lport.
 */
static void fc_lport_logo_resp(struct fc_seq *sp, struct fc_frame *fp,
			       void *lp_arg)
{
	struct fc_lport *lport = lp_arg;
	u8 op;

	FC_LPORT_DBG(lport, "Received a LOGO %s\n", fc_els_resp_type(fp));

	if (fp == ERR_PTR(-FC_EX_CLOSED))
		return;

	mutex_lock(&lport->lp_mutex);

	if (lport->state != LPORT_ST_LOGO) {
		FC_LPORT_DBG(lport, "Received a LOGO response, but in state "
			     "%s\n", fc_lport_state(lport));
		if (IS_ERR(fp))
			goto err;
		goto out;
	}

	if (IS_ERR(fp)) {
		fc_lport_error(lport, fp);
		goto err;
	}

	op = fc_frame_payload_op(fp);
	if (op == ELS_LS_ACC)
		fc_lport_enter_disabled(lport);
	else
		fc_lport_error(lport, fp);

out:
	fc_frame_free(fp);
err:
	mutex_unlock(&lport->lp_mutex);
}

/**
 * fc_rport_enter_logo() - Logout of the fabric
 * @lport: Fibre Channel local port to be logged out
 *
 * Locking Note: The lport lock is expected to be held before calling
 * this routine.
 */
static void fc_lport_enter_logo(struct fc_lport *lport)
{
	struct fc_frame *fp;
	struct fc_els_logo *logo;

	FC_LPORT_DBG(lport, "Entered LOGO state from %s state\n",
		     fc_lport_state(lport));

	fc_lport_state_enter(lport, LPORT_ST_LOGO);

	fp = fc_frame_alloc(lport, sizeof(*logo));
	if (!fp) {
		fc_lport_error(lport, fp);
		return;
	}

	if (!lport->tt.elsct_send(lport, FC_FID_FLOGI, fp, ELS_LOGO,
				  fc_lport_logo_resp, lport, lport->e_d_tov))
		fc_lport_error(lport, NULL);
}

/**
 * fc_lport_flogi_resp() - Handle response to FLOGI request
 * @sp: current sequence in FLOGI exchange
 * @fp: response frame
 * @lp_arg: Fibre Channel lport port instance that sent the FLOGI request
 *
 * Locking Note: This function will be called without the lport lock
 * held, but it will lock, call an _enter_* function or fc_lport_error
 * and then unlock the lport.
 */
static void fc_lport_flogi_resp(struct fc_seq *sp, struct fc_frame *fp,
				void *lp_arg)
{
	struct fc_lport *lport = lp_arg;
	struct fc_frame_header *fh;
	struct fc_els_flogi *flp;
	u32 did;
	u16 csp_flags;
	unsigned int r_a_tov;
	unsigned int e_d_tov;
	u16 mfs;

	FC_LPORT_DBG(lport, "Received a FLOGI %s\n", fc_els_resp_type(fp));

	if (fp == ERR_PTR(-FC_EX_CLOSED))
		return;

	mutex_lock(&lport->lp_mutex);

	if (lport->state != LPORT_ST_FLOGI) {
		FC_LPORT_DBG(lport, "Received a FLOGI response, but in state "
			     "%s\n", fc_lport_state(lport));
		if (IS_ERR(fp))
			goto err;
		goto out;
	}

	if (IS_ERR(fp)) {
		fc_lport_error(lport, fp);
		goto err;
	}

	fh = fc_frame_header_get(fp);
	did = ntoh24(fh->fh_d_id);
	if (fc_frame_payload_op(fp) == ELS_LS_ACC && did != 0) {

		printk(KERN_INFO "libfc: Assigned FID (%6x) in FLOGI response\n",
		       did);
		fc_host_port_id(lport->host) = did;

		flp = fc_frame_payload_get(fp, sizeof(*flp));
		if (flp) {
			mfs = ntohs(flp->fl_csp.sp_bb_data) &
				FC_SP_BB_DATA_MASK;
			if (mfs >= FC_SP_MIN_MAX_PAYLOAD &&
			    mfs < lport->mfs)
				lport->mfs = mfs;
			csp_flags = ntohs(flp->fl_csp.sp_features);
			r_a_tov = ntohl(flp->fl_csp.sp_r_a_tov);
			e_d_tov = ntohl(flp->fl_csp.sp_e_d_tov);
			if (csp_flags & FC_SP_FT_EDTR)
				e_d_tov /= 1000000;
			if ((csp_flags & FC_SP_FT_FPORT) == 0) {
				if (e_d_tov > lport->e_d_tov)
					lport->e_d_tov = e_d_tov;
				lport->r_a_tov = 2 * e_d_tov;
				printk(KERN_INFO "libfc: Port (%6x) entered "
				       "point to point mode\n", did);
				fc_lport_ptp_setup(lport, ntoh24(fh->fh_s_id),
						   get_unaligned_be64(
							   &flp->fl_wwpn),
						   get_unaligned_be64(
							   &flp->fl_wwnn));
			} else {
				lport->e_d_tov = e_d_tov;
				lport->r_a_tov = r_a_tov;
				fc_host_fabric_name(lport->host) =
					get_unaligned_be64(&flp->fl_wwnn);
				fc_lport_enter_dns(lport);
			}
		}
	} else {
		FC_LPORT_DBG(lport, "Bad FLOGI response\n");
	}

out:
	fc_frame_free(fp);
err:
	mutex_unlock(&lport->lp_mutex);
}

/**
 * fc_rport_enter_flogi() - Send a FLOGI request to the fabric manager
 * @lport: Fibre Channel local port to be logged in to the fabric
 *
 * Locking Note: The lport lock is expected to be held before calling
 * this routine.
 */
void fc_lport_enter_flogi(struct fc_lport *lport)
{
	struct fc_frame *fp;

	FC_LPORT_DBG(lport, "Entered FLOGI state from %s state\n",
		     fc_lport_state(lport));

	fc_lport_state_enter(lport, LPORT_ST_FLOGI);

	fp = fc_frame_alloc(lport, sizeof(struct fc_els_flogi));
	if (!fp)
		return fc_lport_error(lport, fp);

	if (!lport->tt.elsct_send(lport, FC_FID_FLOGI, fp, ELS_FLOGI,
				  fc_lport_flogi_resp, lport, lport->e_d_tov))
		fc_lport_error(lport, NULL);
}

/* Configure a fc_lport */
int fc_lport_config(struct fc_lport *lport)
{
	INIT_DELAYED_WORK(&lport->retry_work, fc_lport_timeout);
	mutex_init(&lport->lp_mutex);

	fc_lport_state_enter(lport, LPORT_ST_DISABLED);

	fc_lport_add_fc4_type(lport, FC_TYPE_FCP);
	fc_lport_add_fc4_type(lport, FC_TYPE_CT);

	return 0;
}
EXPORT_SYMBOL(fc_lport_config);

int fc_lport_init(struct fc_lport *lport)
{
	if (!lport->tt.lport_recv)
		lport->tt.lport_recv = fc_lport_recv_req;

	if (!lport->tt.lport_reset)
		lport->tt.lport_reset = fc_lport_reset;

	fc_host_port_type(lport->host) = FC_PORTTYPE_NPORT;
	fc_host_node_name(lport->host) = lport->wwnn;
	fc_host_port_name(lport->host) = lport->wwpn;
	fc_host_supported_classes(lport->host) = FC_COS_CLASS3;
	memset(fc_host_supported_fc4s(lport->host), 0,
	       sizeof(fc_host_supported_fc4s(lport->host)));
	fc_host_supported_fc4s(lport->host)[2] = 1;
	fc_host_supported_fc4s(lport->host)[7] = 1;

	/* This value is also unchanging */
	memset(fc_host_active_fc4s(lport->host), 0,
	       sizeof(fc_host_active_fc4s(lport->host)));
	fc_host_active_fc4s(lport->host)[2] = 1;
	fc_host_active_fc4s(lport->host)[7] = 1;
	fc_host_maxframe_size(lport->host) = lport->mfs;
	fc_host_supported_speeds(lport->host) = 0;
	if (lport->link_supported_speeds & FC_PORTSPEED_1GBIT)
		fc_host_supported_speeds(lport->host) |= FC_PORTSPEED_1GBIT;
	if (lport->link_supported_speeds & FC_PORTSPEED_10GBIT)
		fc_host_supported_speeds(lport->host) |= FC_PORTSPEED_10GBIT;

	INIT_LIST_HEAD(&lport->ema_list);
	return 0;
}
EXPORT_SYMBOL(fc_lport_init);

/*
 *  QLogic FCoE Offload Driver
 *  Copyright (c) 2016-2017 Cavium Inc.
 *
 *  This software is available under the terms of the GNU General Public License
 *  (GPL) Version 2, available from the file COPYING in the main directory of
 *  this source tree.
 */
#include "qedf.h"

/* It's assumed that the lock is held when calling this function. */
static int qedf_initiate_els(struct qedf_rport *fcport, unsigned int op,
	void *data, uint32_t data_len,
	void (*cb_func)(struct qedf_els_cb_arg *cb_arg),
	struct qedf_els_cb_arg *cb_arg, uint32_t timer_msec)
{
	struct qedf_ctx *qedf;
	struct fc_lport *lport;
	struct qedf_ioreq *els_req;
	struct qedf_mp_req *mp_req;
	struct fc_frame_header *fc_hdr;
	struct e4_fcoe_task_context *task;
	int rc = 0;
	uint32_t did, sid;
	uint16_t xid;
	uint32_t start_time = jiffies / HZ;
	uint32_t current_time;
	struct fcoe_wqe *sqe;
	unsigned long flags;
	u16 sqe_idx;

	if (!fcport) {
		QEDF_ERR(NULL, "fcport is NULL");
		rc = -EINVAL;
		goto els_err;
	}

	qedf = fcport->qedf;
	lport = qedf->lport;

	QEDF_INFO(&(qedf->dbg_ctx), QEDF_LOG_ELS, "Sending ELS\n");

	rc = fc_remote_port_chkready(fcport->rport);
	if (rc) {
		QEDF_ERR(&(qedf->dbg_ctx), "els 0x%x: rport not ready\n", op);
		rc = -EAGAIN;
		goto els_err;
	}
	if (lport->state != LPORT_ST_READY || !(lport->link_up)) {
		QEDF_ERR(&(qedf->dbg_ctx), "els 0x%x: link is not ready\n",
			  op);
		rc = -EAGAIN;
		goto els_err;
	}

	if (!test_bit(QEDF_RPORT_SESSION_READY, &fcport->flags)) {
		QEDF_ERR(&(qedf->dbg_ctx), "els 0x%x: fcport not ready\n", op);
		rc = -EINVAL;
		goto els_err;
	}

retry_els:
	els_req = qedf_alloc_cmd(fcport, QEDF_ELS);
	if (!els_req) {
		current_time = jiffies / HZ;
		if ((current_time - start_time) > 10) {
			QEDF_INFO(&(qedf->dbg_ctx), QEDF_LOG_ELS,
				   "els: Failed els 0x%x\n", op);
			rc = -ENOMEM;
			goto els_err;
		}
		mdelay(20 * USEC_PER_MSEC);
		goto retry_els;
	}

	QEDF_INFO(&(qedf->dbg_ctx), QEDF_LOG_ELS, "initiate_els els_req = "
		   "0x%p cb_arg = %p xid = %x\n", els_req, cb_arg,
		   els_req->xid);
	els_req->sc_cmd = NULL;
	els_req->cmd_type = QEDF_ELS;
	els_req->fcport = fcport;
	els_req->cb_func = cb_func;
	cb_arg->io_req = els_req;
	cb_arg->op = op;
	els_req->cb_arg = cb_arg;
	els_req->data_xfer_len = data_len;

	/* Record which cpu this request is associated with */
	els_req->cpu = smp_processor_id();

	mp_req = (struct qedf_mp_req *)&(els_req->mp_req);
	rc = qedf_init_mp_req(els_req);
	if (rc) {
		QEDF_ERR(&(qedf->dbg_ctx), "ELS MP request init failed\n");
		kref_put(&els_req->refcount, qedf_release_cmd);
		goto els_err;
	} else {
		rc = 0;
	}

	/* Fill ELS Payload */
	if ((op >= ELS_LS_RJT) && (op <= ELS_AUTH_ELS)) {
		memcpy(mp_req->req_buf, data, data_len);
	} else {
		QEDF_ERR(&(qedf->dbg_ctx), "Invalid ELS op 0x%x\n", op);
		els_req->cb_func = NULL;
		els_req->cb_arg = NULL;
		kref_put(&els_req->refcount, qedf_release_cmd);
		rc = -EINVAL;
	}

	if (rc)
		goto els_err;

	/* Fill FC header */
	fc_hdr = &(mp_req->req_fc_hdr);

	did = fcport->rdata->ids.port_id;
	sid = fcport->sid;

	__fc_fill_fc_hdr(fc_hdr, FC_RCTL_ELS_REQ, did, sid,
			   FC_TYPE_ELS, FC_FC_FIRST_SEQ | FC_FC_END_SEQ |
			   FC_FC_SEQ_INIT, 0);

	/* Obtain exchange id */
	xid = els_req->xid;

	spin_lock_irqsave(&fcport->rport_lock, flags);

	sqe_idx = qedf_get_sqe_idx(fcport);
	sqe = &fcport->sq[sqe_idx];
	memset(sqe, 0, sizeof(struct fcoe_wqe));

	/* Initialize task context for this IO request */
	task = qedf_get_task_mem(&qedf->tasks, xid);
	qedf_init_mp_task(els_req, task, sqe);

	/* Put timer on original I/O request */
	if (timer_msec)
		qedf_cmd_timer_set(qedf, els_req, timer_msec);

	/* Ring doorbell */
	QEDF_INFO(&(qedf->dbg_ctx), QEDF_LOG_ELS, "Ringing doorbell for ELS "
		   "req\n");
	qedf_ring_doorbell(fcport);
	spin_unlock_irqrestore(&fcport->rport_lock, flags);
els_err:
	return rc;
}

void qedf_process_els_compl(struct qedf_ctx *qedf, struct fcoe_cqe *cqe,
	struct qedf_ioreq *els_req)
{
	struct fcoe_task_context *task_ctx;
	struct scsi_cmnd *sc_cmd;
	uint16_t xid;
	struct fcoe_cqe_midpath_info *mp_info;

	QEDF_INFO(&(qedf->dbg_ctx), QEDF_LOG_ELS, "Entered with xid = 0x%x"
		   " cmd_type = %d.\n", els_req->xid, els_req->cmd_type);

	/* Kill the ELS timer */
	cancel_delayed_work(&els_req->timeout_work);

	xid = els_req->xid;
	task_ctx = qedf_get_task_mem(&qedf->tasks, xid);
	sc_cmd = els_req->sc_cmd;

	/* Get ELS response length from CQE */
	mp_info = &cqe->cqe_info.midpath_info;
	els_req->mp_req.resp_len = mp_info->data_placement_size;

	/* Parse ELS response */
	if ((els_req->cb_func) && (els_req->cb_arg)) {
		els_req->cb_func(els_req->cb_arg);
		els_req->cb_arg = NULL;
	}

	kref_put(&els_req->refcount, qedf_release_cmd);
}

static void qedf_rrq_compl(struct qedf_els_cb_arg *cb_arg)
{
	struct qedf_ioreq *orig_io_req;
	struct qedf_ioreq *rrq_req;
	struct qedf_ctx *qedf;
	int refcount;

	rrq_req = cb_arg->io_req;
	qedf = rrq_req->fcport->qedf;

	QEDF_INFO(&(qedf->dbg_ctx), QEDF_LOG_ELS, "Entered.\n");

	orig_io_req = cb_arg->aborted_io_req;

	if (!orig_io_req)
		goto out_free;

	if (rrq_req->event != QEDF_IOREQ_EV_ELS_TMO &&
	    rrq_req->event != QEDF_IOREQ_EV_ELS_ERR_DETECT)
		cancel_delayed_work_sync(&orig_io_req->timeout_work);

	refcount = kref_read(&orig_io_req->refcount);
	QEDF_INFO(&(qedf->dbg_ctx), QEDF_LOG_ELS, "rrq_compl: orig io = %p,"
		   " orig xid = 0x%x, rrq_xid = 0x%x, refcount=%d\n",
		   orig_io_req, orig_io_req->xid, rrq_req->xid, refcount);

	/* This should return the aborted io_req to the command pool */
	if (orig_io_req)
		kref_put(&orig_io_req->refcount, qedf_release_cmd);

out_free:
	/*
	 * Release a reference to the rrq request if we timed out as the
	 * rrq completion handler is called directly from the timeout handler
	 * and not from els_compl where the reference would have normally been
	 * released.
	 */
	if (rrq_req->event == QEDF_IOREQ_EV_ELS_TMO)
		kref_put(&rrq_req->refcount, qedf_release_cmd);
	kfree(cb_arg);
}

/* Assumes kref is already held by caller */
int qedf_send_rrq(struct qedf_ioreq *aborted_io_req)
{

	struct fc_els_rrq rrq;
	struct qedf_rport *fcport;
	struct fc_lport *lport;
	struct qedf_els_cb_arg *cb_arg = NULL;
	struct qedf_ctx *qedf;
	uint32_t sid;
	uint32_t r_a_tov;
	int rc;

	if (!aborted_io_req) {
		QEDF_ERR(NULL, "abort_io_req is NULL.\n");
		return -EINVAL;
	}

	fcport = aborted_io_req->fcport;

	/* Check that fcport is still offloaded */
	if (!test_bit(QEDF_RPORT_SESSION_READY, &fcport->flags)) {
		QEDF_ERR(NULL, "fcport is no longer offloaded.\n");
		return -EINVAL;
	}

	if (!fcport->qedf) {
		QEDF_ERR(NULL, "fcport->qedf is NULL.\n");
		return -EINVAL;
	}

	qedf = fcport->qedf;
	lport = qedf->lport;
	sid = fcport->sid;
	r_a_tov = lport->r_a_tov;

	QEDF_INFO(&(qedf->dbg_ctx), QEDF_LOG_ELS, "Sending RRQ orig "
		   "io = %p, orig_xid = 0x%x\n", aborted_io_req,
		   aborted_io_req->xid);
	memset(&rrq, 0, sizeof(rrq));

	cb_arg = kzalloc(sizeof(struct qedf_els_cb_arg), GFP_NOIO);
	if (!cb_arg) {
		QEDF_ERR(&(qedf->dbg_ctx), "Unable to allocate cb_arg for "
			  "RRQ\n");
		rc = -ENOMEM;
		goto rrq_err;
	}

	cb_arg->aborted_io_req = aborted_io_req;

	rrq.rrq_cmd = ELS_RRQ;
	hton24(rrq.rrq_s_id, sid);
	rrq.rrq_ox_id = htons(aborted_io_req->xid);
	rrq.rrq_rx_id =
	    htons(aborted_io_req->task->tstorm_st_context.read_write.rx_id);

	rc = qedf_initiate_els(fcport, ELS_RRQ, &rrq, sizeof(rrq),
	    qedf_rrq_compl, cb_arg, r_a_tov);

rrq_err:
	if (rc) {
		QEDF_ERR(&(qedf->dbg_ctx), "RRQ failed - release orig io "
			  "req 0x%x\n", aborted_io_req->xid);
		kfree(cb_arg);
		kref_put(&aborted_io_req->refcount, qedf_release_cmd);
	}
	return rc;
}

static void qedf_process_l2_frame_compl(struct qedf_rport *fcport,
					struct fc_frame *fp,
					u16 l2_oxid)
{
	struct fc_lport *lport = fcport->qedf->lport;
	struct fc_frame_header *fh;
	u32 crc;

	fh = (struct fc_frame_header *)fc_frame_header_get(fp);

	/* Set the OXID we return to what libfc used */
	if (l2_oxid != FC_XID_UNKNOWN)
		fh->fh_ox_id = htons(l2_oxid);

	/* Setup header fields */
	fh->fh_r_ctl = FC_RCTL_ELS_REP;
	fh->fh_type = FC_TYPE_ELS;
	/* Last sequence, end sequence */
	fh->fh_f_ctl[0] = 0x98;
	hton24(fh->fh_d_id, lport->port_id);
	hton24(fh->fh_s_id, fcport->rdata->ids.port_id);
	fh->fh_rx_id = 0xffff;

	/* Set frame attributes */
	crc = fcoe_fc_crc(fp);
	fc_frame_init(fp);
	fr_dev(fp) = lport;
	fr_sof(fp) = FC_SOF_I3;
	fr_eof(fp) = FC_EOF_T;
	fr_crc(fp) = cpu_to_le32(~crc);

	/* Send completed request to libfc */
	fc_exch_recv(lport, fp);
}

/*
 * In instances where an ELS command times out we may need to restart the
 * rport by logging out and then logging back in.
 */
void qedf_restart_rport(struct qedf_rport *fcport)
{
	struct fc_lport *lport;
	struct fc_rport_priv *rdata;
	u32 port_id;

	if (!fcport)
		return;

	if (test_bit(QEDF_RPORT_IN_RESET, &fcport->flags) ||
	    !test_bit(QEDF_RPORT_SESSION_READY, &fcport->flags) ||
	    test_bit(QEDF_RPORT_UPLOADING_CONNECTION, &fcport->flags)) {
		QEDF_ERR(&(fcport->qedf->dbg_ctx), "fcport %p already in reset or not offloaded.\n",
		    fcport);
		return;
	}

	/* Set that we are now in reset */
	set_bit(QEDF_RPORT_IN_RESET, &fcport->flags);

	rdata = fcport->rdata;
	if (rdata) {
		lport = fcport->qedf->lport;
		port_id = rdata->ids.port_id;
		QEDF_ERR(&(fcport->qedf->dbg_ctx),
		    "LOGO port_id=%x.\n", port_id);
		fc_rport_logoff(rdata);
		/* Recreate the rport and log back in */
		rdata = fc_rport_create(lport, port_id);
		if (rdata)
			fc_rport_login(rdata);
	}
	clear_bit(QEDF_RPORT_IN_RESET, &fcport->flags);
}

static void qedf_l2_els_compl(struct qedf_els_cb_arg *cb_arg)
{
	struct qedf_ioreq *els_req;
	struct qedf_rport *fcport;
	struct qedf_mp_req *mp_req;
	struct fc_frame *fp;
	struct fc_frame_header *fh, *mp_fc_hdr;
	void *resp_buf, *fc_payload;
	u32 resp_len;
	u16 l2_oxid;

	l2_oxid = cb_arg->l2_oxid;
	els_req = cb_arg->io_req;

	if (!els_req) {
		QEDF_ERR(NULL, "els_req is NULL.\n");
		goto free_arg;
	}

	/*
	 * If we are flushing the command just free the cb_arg as none of the
	 * response data will be valid.
	 */
	if (els_req->event == QEDF_IOREQ_EV_ELS_FLUSH)
		goto free_arg;

	fcport = els_req->fcport;
	mp_req = &(els_req->mp_req);
	mp_fc_hdr = &(mp_req->resp_fc_hdr);
	resp_len = mp_req->resp_len;
	resp_buf = mp_req->resp_buf;

	/*
	 * If a middle path ELS command times out, don't try to return
	 * the command but rather do any internal cleanup and then libfc
	 * timeout the command and clean up its internal resources.
	 */
	if (els_req->event == QEDF_IOREQ_EV_ELS_TMO) {
		/*
		 * If ADISC times out, libfc will timeout the exchange and then
		 * try to send a PLOGI which will timeout since the session is
		 * still offloaded.  Force libfc to logout the session which
		 * will offload the connection and allow the PLOGI response to
		 * flow over the LL2 path.
		 */
		if (cb_arg->op == ELS_ADISC)
			qedf_restart_rport(fcport);
		return;
	}

	if (sizeof(struct fc_frame_header) + resp_len > QEDF_PAGE_SIZE) {
		QEDF_ERR(&(fcport->qedf->dbg_ctx), "resp_len is "
		   "beyond page size.\n");
		goto free_arg;
	}

	fp = fc_frame_alloc(fcport->qedf->lport, resp_len);
	if (!fp) {
		QEDF_ERR(&(fcport->qedf->dbg_ctx),
		    "fc_frame_alloc failure.\n");
		return;
	}

	/* Copy frame header from firmware into fp */
	fh = (struct fc_frame_header *)fc_frame_header_get(fp);
	memcpy(fh, mp_fc_hdr, sizeof(struct fc_frame_header));

	/* Copy payload from firmware into fp */
	fc_payload = fc_frame_payload_get(fp, resp_len);
	memcpy(fc_payload, resp_buf, resp_len);

	QEDF_INFO(&(fcport->qedf->dbg_ctx), QEDF_LOG_ELS,
	    "Completing OX_ID 0x%x back to libfc.\n", l2_oxid);
	qedf_process_l2_frame_compl(fcport, fp, l2_oxid);

free_arg:
	kfree(cb_arg);
}

int qedf_send_adisc(struct qedf_rport *fcport, struct fc_frame *fp)
{
	struct fc_els_adisc *adisc;
	struct fc_frame_header *fh;
	struct fc_lport *lport = fcport->qedf->lport;
	struct qedf_els_cb_arg *cb_arg = NULL;
	struct qedf_ctx *qedf;
	uint32_t r_a_tov = lport->r_a_tov;
	int rc;

	qedf = fcport->qedf;
	fh = fc_frame_header_get(fp);

	cb_arg = kzalloc(sizeof(struct qedf_els_cb_arg), GFP_NOIO);
	if (!cb_arg) {
		QEDF_ERR(&(qedf->dbg_ctx), "Unable to allocate cb_arg for "
			  "ADISC\n");
		rc = -ENOMEM;
		goto adisc_err;
	}
	cb_arg->l2_oxid = ntohs(fh->fh_ox_id);

	QEDF_INFO(&(qedf->dbg_ctx), QEDF_LOG_ELS,
	    "Sending ADISC ox_id=0x%x.\n", cb_arg->l2_oxid);

	adisc = fc_frame_payload_get(fp, sizeof(*adisc));

	rc = qedf_initiate_els(fcport, ELS_ADISC, adisc, sizeof(*adisc),
	    qedf_l2_els_compl, cb_arg, r_a_tov);

adisc_err:
	if (rc) {
		QEDF_ERR(&(qedf->dbg_ctx), "ADISC failed.\n");
		kfree(cb_arg);
	}
	return rc;
}

static void qedf_srr_compl(struct qedf_els_cb_arg *cb_arg)
{
	struct qedf_ioreq *orig_io_req;
	struct qedf_ioreq *srr_req;
	struct qedf_mp_req *mp_req;
	struct fc_frame_header *mp_fc_hdr, *fh;
	struct fc_frame *fp;
	void *resp_buf, *fc_payload;
	u32 resp_len;
	struct fc_lport *lport;
	struct qedf_ctx *qedf;
	int refcount;
	u8 opcode;

	srr_req = cb_arg->io_req;
	qedf = srr_req->fcport->qedf;
	lport = qedf->lport;

	orig_io_req = cb_arg->aborted_io_req;

	if (!orig_io_req)
		goto out_free;

	clear_bit(QEDF_CMD_SRR_SENT, &orig_io_req->flags);

	if (srr_req->event != QEDF_IOREQ_EV_ELS_TMO &&
	    srr_req->event != QEDF_IOREQ_EV_ELS_ERR_DETECT)
		cancel_delayed_work_sync(&orig_io_req->timeout_work);

	refcount = kref_read(&orig_io_req->refcount);
	QEDF_INFO(&(qedf->dbg_ctx), QEDF_LOG_ELS, "Entered: orig_io=%p,"
		   " orig_io_xid=0x%x, rec_xid=0x%x, refcount=%d\n",
		   orig_io_req, orig_io_req->xid, srr_req->xid, refcount);

	/* If a SRR times out, simply free resources */
	if (srr_req->event == QEDF_IOREQ_EV_ELS_TMO)
		goto out_put;

	/* Normalize response data into struct fc_frame */
	mp_req = &(srr_req->mp_req);
	mp_fc_hdr = &(mp_req->resp_fc_hdr);
	resp_len = mp_req->resp_len;
	resp_buf = mp_req->resp_buf;

	fp = fc_frame_alloc(lport, resp_len);
	if (!fp) {
		QEDF_ERR(&(qedf->dbg_ctx),
		    "fc_frame_alloc failure.\n");
		goto out_put;
	}

	/* Copy frame header from firmware into fp */
	fh = (struct fc_frame_header *)fc_frame_header_get(fp);
	memcpy(fh, mp_fc_hdr, sizeof(struct fc_frame_header));

	/* Copy payload from firmware into fp */
	fc_payload = fc_frame_payload_get(fp, resp_len);
	memcpy(fc_payload, resp_buf, resp_len);

	opcode = fc_frame_payload_op(fp);
	switch (opcode) {
	case ELS_LS_ACC:
		QEDF_INFO(&(qedf->dbg_ctx), QEDF_LOG_ELS,
		    "SRR success.\n");
		break;
	case ELS_LS_RJT:
		QEDF_INFO(&qedf->dbg_ctx, QEDF_LOG_ELS,
		    "SRR rejected.\n");
		qedf_initiate_abts(orig_io_req, true);
		break;
	}

	fc_frame_free(fp);
out_put:
	/* Put reference for original command since SRR completed */
	kref_put(&orig_io_req->refcount, qedf_release_cmd);
out_free:
	kfree(cb_arg);
}

static int qedf_send_srr(struct qedf_ioreq *orig_io_req, u32 offset, u8 r_ctl)
{
	struct fcp_srr srr;
	struct qedf_ctx *qedf;
	struct qedf_rport *fcport;
	struct fc_lport *lport;
	struct qedf_els_cb_arg *cb_arg = NULL;
	u32 sid, r_a_tov;
	int rc;

	if (!orig_io_req) {
		QEDF_ERR(NULL, "orig_io_req is NULL.\n");
		return -EINVAL;
	}

	fcport = orig_io_req->fcport;

	/* Check that fcport is still offloaded */
	if (!test_bit(QEDF_RPORT_SESSION_READY, &fcport->flags)) {
		QEDF_ERR(NULL, "fcport is no longer offloaded.\n");
		return -EINVAL;
	}

	if (!fcport->qedf) {
		QEDF_ERR(NULL, "fcport->qedf is NULL.\n");
		return -EINVAL;
	}

	/* Take reference until SRR command completion */
	kref_get(&orig_io_req->refcount);

	qedf = fcport->qedf;
	lport = qedf->lport;
	sid = fcport->sid;
	r_a_tov = lport->r_a_tov;

	QEDF_INFO(&(qedf->dbg_ctx), QEDF_LOG_ELS, "Sending SRR orig_io=%p, "
		   "orig_xid=0x%x\n", orig_io_req, orig_io_req->xid);
	memset(&srr, 0, sizeof(srr));

	cb_arg = kzalloc(sizeof(struct qedf_els_cb_arg), GFP_NOIO);
	if (!cb_arg) {
		QEDF_ERR(&(qedf->dbg_ctx), "Unable to allocate cb_arg for "
			  "SRR\n");
		rc = -ENOMEM;
		goto srr_err;
	}

	cb_arg->aborted_io_req = orig_io_req;

	srr.srr_op = ELS_SRR;
	srr.srr_ox_id = htons(orig_io_req->xid);
	srr.srr_rx_id = htons(orig_io_req->rx_id);
	srr.srr_rel_off = htonl(offset);
	srr.srr_r_ctl = r_ctl;

	rc = qedf_initiate_els(fcport, ELS_SRR, &srr, sizeof(srr),
	    qedf_srr_compl, cb_arg, r_a_tov);

srr_err:
	if (rc) {
		QEDF_ERR(&(qedf->dbg_ctx), "SRR failed - release orig_io_req"
			  "=0x%x\n", orig_io_req->xid);
		kfree(cb_arg);
		/* If we fail to queue SRR, send ABTS to orig_io */
		qedf_initiate_abts(orig_io_req, true);
		kref_put(&orig_io_req->refcount, qedf_release_cmd);
	} else
		/* Tell other threads that SRR is in progress */
		set_bit(QEDF_CMD_SRR_SENT, &orig_io_req->flags);

	return rc;
}

static void qedf_initiate_seq_cleanup(struct qedf_ioreq *orig_io_req,
	u32 offset, u8 r_ctl)
{
	struct qedf_rport *fcport;
	unsigned long flags;
	struct qedf_els_cb_arg *cb_arg;
	struct fcoe_wqe *sqe;
	u16 sqe_idx;

	fcport = orig_io_req->fcport;

	QEDF_INFO(&(fcport->qedf->dbg_ctx), QEDF_LOG_ELS,
	    "Doing sequence cleanup for xid=0x%x offset=%u.\n",
	    orig_io_req->xid, offset);

	cb_arg = kzalloc(sizeof(struct qedf_els_cb_arg), GFP_NOIO);
	if (!cb_arg) {
		QEDF_ERR(&(fcport->qedf->dbg_ctx), "Unable to allocate cb_arg "
			  "for sequence cleanup\n");
		return;
	}

	/* Get reference for cleanup request */
	kref_get(&orig_io_req->refcount);

	orig_io_req->cmd_type = QEDF_SEQ_CLEANUP;
	cb_arg->offset = offset;
	cb_arg->r_ctl = r_ctl;
	orig_io_req->cb_arg = cb_arg;

	qedf_cmd_timer_set(fcport->qedf, orig_io_req,
	    QEDF_CLEANUP_TIMEOUT * HZ);

	spin_lock_irqsave(&fcport->rport_lock, flags);

	sqe_idx = qedf_get_sqe_idx(fcport);
	sqe = &fcport->sq[sqe_idx];
	memset(sqe, 0, sizeof(struct fcoe_wqe));
	orig_io_req->task_params->sqe = sqe;

	init_initiator_sequence_recovery_fcoe_task(orig_io_req->task_params,
						   offset);
	qedf_ring_doorbell(fcport);

	spin_unlock_irqrestore(&fcport->rport_lock, flags);
}

void qedf_process_seq_cleanup_compl(struct qedf_ctx *qedf,
	struct fcoe_cqe *cqe, struct qedf_ioreq *io_req)
{
	int rc;
	struct qedf_els_cb_arg *cb_arg;

	cb_arg = io_req->cb_arg;

	/* If we timed out just free resources */
	if (io_req->event == QEDF_IOREQ_EV_ELS_TMO || !cqe)
		goto free;

	/* Kill the timer we put on the request */
	cancel_delayed_work_sync(&io_req->timeout_work);

	rc = qedf_send_srr(io_req, cb_arg->offset, cb_arg->r_ctl);
	if (rc)
		QEDF_ERR(&(qedf->dbg_ctx), "Unable to send SRR, I/O will "
		    "abort, xid=0x%x.\n", io_req->xid);
free:
	kfree(cb_arg);
	kref_put(&io_req->refcount, qedf_release_cmd);
}

static bool qedf_requeue_io_req(struct qedf_ioreq *orig_io_req)
{
	struct qedf_rport *fcport;
	struct qedf_ioreq *new_io_req;
	unsigned long flags;
	bool rc = false;

	fcport = orig_io_req->fcport;
	if (!fcport) {
		QEDF_ERR(NULL, "fcport is NULL.\n");
		goto out;
	}

	if (!orig_io_req->sc_cmd) {
		QEDF_ERR(&(fcport->qedf->dbg_ctx), "sc_cmd is NULL for "
		    "xid=0x%x.\n", orig_io_req->xid);
		goto out;
	}

	new_io_req = qedf_alloc_cmd(fcport, QEDF_SCSI_CMD);
	if (!new_io_req) {
		QEDF_ERR(&(fcport->qedf->dbg_ctx), "Could not allocate new "
		    "io_req.\n");
		goto out;
	}

	new_io_req->sc_cmd = orig_io_req->sc_cmd;

	/*
	 * This keeps the sc_cmd struct from being returned to the tape
	 * driver and being requeued twice. We do need to put a reference
	 * for the original I/O request since we will not do a SCSI completion
	 * for it.
	 */
	orig_io_req->sc_cmd = NULL;
	kref_put(&orig_io_req->refcount, qedf_release_cmd);

	spin_lock_irqsave(&fcport->rport_lock, flags);

	/* kref for new command released in qedf_post_io_req on error */
	if (qedf_post_io_req(fcport, new_io_req)) {
		QEDF_ERR(&(fcport->qedf->dbg_ctx), "Unable to post io_req\n");
		/* Return SQE to pool */
		atomic_inc(&fcport->free_sqes);
	} else {
		QEDF_INFO(&(fcport->qedf->dbg_ctx), QEDF_LOG_ELS,
		    "Reissued SCSI command from  orig_xid=0x%x on "
		    "new_xid=0x%x.\n", orig_io_req->xid, new_io_req->xid);
		/*
		 * Abort the original I/O but do not return SCSI command as
		 * it has been reissued on another OX_ID.
		 */
		spin_unlock_irqrestore(&fcport->rport_lock, flags);
		qedf_initiate_abts(orig_io_req, false);
		goto out;
	}

	spin_unlock_irqrestore(&fcport->rport_lock, flags);
out:
	return rc;
}


static void qedf_rec_compl(struct qedf_els_cb_arg *cb_arg)
{
	struct qedf_ioreq *orig_io_req;
	struct qedf_ioreq *rec_req;
	struct qedf_mp_req *mp_req;
	struct fc_frame_header *mp_fc_hdr, *fh;
	struct fc_frame *fp;
	void *resp_buf, *fc_payload;
	u32 resp_len;
	struct fc_lport *lport;
	struct qedf_ctx *qedf;
	int refcount;
	enum fc_rctl r_ctl;
	struct fc_els_ls_rjt *rjt;
	struct fc_els_rec_acc *acc;
	u8 opcode;
	u32 offset, e_stat;
	struct scsi_cmnd *sc_cmd;
	bool srr_needed = false;

	rec_req = cb_arg->io_req;
	qedf = rec_req->fcport->qedf;
	lport = qedf->lport;

	orig_io_req = cb_arg->aborted_io_req;

	if (!orig_io_req)
		goto out_free;

	if (rec_req->event != QEDF_IOREQ_EV_ELS_TMO &&
	    rec_req->event != QEDF_IOREQ_EV_ELS_ERR_DETECT)
		cancel_delayed_work_sync(&orig_io_req->timeout_work);

	refcount = kref_read(&orig_io_req->refcount);
	QEDF_INFO(&(qedf->dbg_ctx), QEDF_LOG_ELS, "Entered: orig_io=%p,"
		   " orig_io_xid=0x%x, rec_xid=0x%x, refcount=%d\n",
		   orig_io_req, orig_io_req->xid, rec_req->xid, refcount);

	/* If a REC times out, free resources */
	if (rec_req->event == QEDF_IOREQ_EV_ELS_TMO)
		goto out_put;

	/* Normalize response data into struct fc_frame */
	mp_req = &(rec_req->mp_req);
	mp_fc_hdr = &(mp_req->resp_fc_hdr);
	resp_len = mp_req->resp_len;
	acc = resp_buf = mp_req->resp_buf;

	fp = fc_frame_alloc(lport, resp_len);
	if (!fp) {
		QEDF_ERR(&(qedf->dbg_ctx),
		    "fc_frame_alloc failure.\n");
		goto out_put;
	}

	/* Copy frame header from firmware into fp */
	fh = (struct fc_frame_header *)fc_frame_header_get(fp);
	memcpy(fh, mp_fc_hdr, sizeof(struct fc_frame_header));

	/* Copy payload from firmware into fp */
	fc_payload = fc_frame_payload_get(fp, resp_len);
	memcpy(fc_payload, resp_buf, resp_len);

	opcode = fc_frame_payload_op(fp);
	if (opcode == ELS_LS_RJT) {
		rjt = fc_frame_payload_get(fp, sizeof(*rjt));
		QEDF_INFO(&(qedf->dbg_ctx), QEDF_LOG_ELS,
		    "Received LS_RJT for REC: er_reason=0x%x, "
		    "er_explan=0x%x.\n", rjt->er_reason, rjt->er_explan);
		/*
		 * The following response(s) mean that we need to reissue the
		 * request on another exchange.  We need to do this without
		 * informing the upper layers lest it cause an application
		 * error.
		 */
		if ((rjt->er_reason == ELS_RJT_LOGIC ||
		    rjt->er_reason == ELS_RJT_UNAB) &&
		    rjt->er_explan == ELS_EXPL_OXID_RXID) {
			QEDF_INFO(&(qedf->dbg_ctx), QEDF_LOG_ELS,
			    "Handle CMD LOST case.\n");
			qedf_requeue_io_req(orig_io_req);
		}
	} else if (opcode == ELS_LS_ACC) {
		offset = ntohl(acc->reca_fc4value);
		e_stat = ntohl(acc->reca_e_stat);
		QEDF_INFO(&(qedf->dbg_ctx), QEDF_LOG_ELS,
		    "Received LS_ACC for REC: offset=0x%x, e_stat=0x%x.\n",
		    offset, e_stat);
		if (e_stat & ESB_ST_SEQ_INIT)  {
			QEDF_INFO(&(qedf->dbg_ctx), QEDF_LOG_ELS,
			    "Target has the seq init\n");
			goto out_free_frame;
		}
		sc_cmd = orig_io_req->sc_cmd;
		if (!sc_cmd) {
			QEDF_INFO(&(qedf->dbg_ctx), QEDF_LOG_ELS,
			    "sc_cmd is NULL for xid=0x%x.\n",
			    orig_io_req->xid);
			goto out_free_frame;
		}
		/* SCSI write case */
		if (sc_cmd->sc_data_direction == DMA_TO_DEVICE) {
			if (offset == orig_io_req->data_xfer_len) {
				QEDF_INFO(&(qedf->dbg_ctx), QEDF_LOG_ELS,
				    "WRITE - response lost.\n");
				r_ctl = FC_RCTL_DD_CMD_STATUS;
				srr_needed = true;
				offset = 0;
			} else {
				QEDF_INFO(&(qedf->dbg_ctx), QEDF_LOG_ELS,
				    "WRITE - XFER_RDY/DATA lost.\n");
				r_ctl = FC_RCTL_DD_DATA_DESC;
				/* Use data from warning CQE instead of REC */
				offset = orig_io_req->tx_buf_off;
			}
		/* SCSI read case */
		} else {
			if (orig_io_req->rx_buf_off ==
			    orig_io_req->data_xfer_len) {
				QEDF_INFO(&(qedf->dbg_ctx), QEDF_LOG_ELS,
				    "READ - response lost.\n");
				srr_needed = true;
				r_ctl = FC_RCTL_DD_CMD_STATUS;
				offset = 0;
			} else {
				QEDF_INFO(&(qedf->dbg_ctx), QEDF_LOG_ELS,
				    "READ - DATA lost.\n");
				/*
				 * For read case we always set the offset to 0
				 * for sequence recovery task.
				 */
				offset = 0;
				r_ctl = FC_RCTL_DD_SOL_DATA;
			}
		}

		if (srr_needed)
			qedf_send_srr(orig_io_req, offset, r_ctl);
		else
			qedf_initiate_seq_cleanup(orig_io_req, offset, r_ctl);
	}

out_free_frame:
	fc_frame_free(fp);
out_put:
	/* Put reference for original command since REC completed */
	kref_put(&orig_io_req->refcount, qedf_release_cmd);
out_free:
	kfree(cb_arg);
}

/* Assumes kref is already held by caller */
int qedf_send_rec(struct qedf_ioreq *orig_io_req)
{

	struct fc_els_rec rec;
	struct qedf_rport *fcport;
	struct fc_lport *lport;
	struct qedf_els_cb_arg *cb_arg = NULL;
	struct qedf_ctx *qedf;
	uint32_t sid;
	uint32_t r_a_tov;
	int rc;

	if (!orig_io_req) {
		QEDF_ERR(NULL, "orig_io_req is NULL.\n");
		return -EINVAL;
	}

	fcport = orig_io_req->fcport;

	/* Check that fcport is still offloaded */
	if (!test_bit(QEDF_RPORT_SESSION_READY, &fcport->flags)) {
		QEDF_ERR(NULL, "fcport is no longer offloaded.\n");
		return -EINVAL;
	}

	if (!fcport->qedf) {
		QEDF_ERR(NULL, "fcport->qedf is NULL.\n");
		return -EINVAL;
	}

	/* Take reference until REC command completion */
	kref_get(&orig_io_req->refcount);

	qedf = fcport->qedf;
	lport = qedf->lport;
	sid = fcport->sid;
	r_a_tov = lport->r_a_tov;

	memset(&rec, 0, sizeof(rec));

	cb_arg = kzalloc(sizeof(struct qedf_els_cb_arg), GFP_NOIO);
	if (!cb_arg) {
		QEDF_ERR(&(qedf->dbg_ctx), "Unable to allocate cb_arg for "
			  "REC\n");
		rc = -ENOMEM;
		goto rec_err;
	}

	cb_arg->aborted_io_req = orig_io_req;

	rec.rec_cmd = ELS_REC;
	hton24(rec.rec_s_id, sid);
	rec.rec_ox_id = htons(orig_io_req->xid);
	rec.rec_rx_id =
	    htons(orig_io_req->task->tstorm_st_context.read_write.rx_id);

	QEDF_INFO(&(qedf->dbg_ctx), QEDF_LOG_ELS, "Sending REC orig_io=%p, "
	   "orig_xid=0x%x rx_id=0x%x\n", orig_io_req,
	   orig_io_req->xid, rec.rec_rx_id);
	rc = qedf_initiate_els(fcport, ELS_REC, &rec, sizeof(rec),
	    qedf_rec_compl, cb_arg, r_a_tov);

rec_err:
	if (rc) {
		QEDF_ERR(&(qedf->dbg_ctx), "REC failed - release orig_io_req"
			  "=0x%x\n", orig_io_req->xid);
		kfree(cb_arg);
		kref_put(&orig_io_req->refcount, qedf_release_cmd);
	}
	return rc;
}

/*
 * bnx2fc_els.c: QLogic Linux FCoE offload driver.
 * This file contains helper routines that handle ELS requests
 * and responses.
 *
 * Copyright (c) 2008-2013 Broadcom Corporation
 * Copyright (c) 2014-2015 QLogic Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation.
 *
 * Written by: Bhanu Prakash Gollapudi (bprakash@broadcom.com)
 */

#include "bnx2fc.h"

static void bnx2fc_logo_resp(struct fc_seq *seq, struct fc_frame *fp,
			     void *arg);
static void bnx2fc_flogi_resp(struct fc_seq *seq, struct fc_frame *fp,
			      void *arg);
static int bnx2fc_initiate_els(struct bnx2fc_rport *tgt, unsigned int op,
			void *data, u32 data_len,
			void (*cb_func)(struct bnx2fc_els_cb_arg *cb_arg),
			struct bnx2fc_els_cb_arg *cb_arg, u32 timer_msec);

static void bnx2fc_rrq_compl(struct bnx2fc_els_cb_arg *cb_arg)
{
	struct bnx2fc_cmd *orig_io_req;
	struct bnx2fc_cmd *rrq_req;
	int rc = 0;

	BUG_ON(!cb_arg);
	rrq_req = cb_arg->io_req;
	orig_io_req = cb_arg->aborted_io_req;
	BUG_ON(!orig_io_req);
	BNX2FC_ELS_DBG("rrq_compl: orig xid = 0x%x, rrq_xid = 0x%x\n",
		   orig_io_req->xid, rrq_req->xid);

	kref_put(&orig_io_req->refcount, bnx2fc_cmd_release);

	if (test_and_clear_bit(BNX2FC_FLAG_ELS_TIMEOUT, &rrq_req->req_flags)) {
		/*
		 * els req is timed out. cleanup the IO with FW and
		 * drop the completion. Remove from active_cmd_queue.
		 */
		BNX2FC_ELS_DBG("rrq xid - 0x%x timed out, clean it up\n",
			   rrq_req->xid);

		if (rrq_req->on_active_queue) {
			list_del_init(&rrq_req->link);
			rrq_req->on_active_queue = 0;
			rc = bnx2fc_initiate_cleanup(rrq_req);
			BUG_ON(rc);
		}
	}
	kfree(cb_arg);
}
int bnx2fc_send_rrq(struct bnx2fc_cmd *aborted_io_req)
{

	struct fc_els_rrq rrq;
	struct bnx2fc_rport *tgt = aborted_io_req->tgt;
	struct fc_lport *lport = tgt->rdata->local_port;
	struct bnx2fc_els_cb_arg *cb_arg = NULL;
	u32 sid = tgt->sid;
	u32 r_a_tov = lport->r_a_tov;
	unsigned long start = jiffies;
	int rc;

	BNX2FC_ELS_DBG("Sending RRQ orig_xid = 0x%x\n",
		   aborted_io_req->xid);
	memset(&rrq, 0, sizeof(rrq));

	cb_arg = kzalloc(sizeof(struct bnx2fc_els_cb_arg), GFP_NOIO);
	if (!cb_arg) {
		printk(KERN_ERR PFX "Unable to allocate cb_arg for RRQ\n");
		rc = -ENOMEM;
		goto rrq_err;
	}

	cb_arg->aborted_io_req = aborted_io_req;

	rrq.rrq_cmd = ELS_RRQ;
	hton24(rrq.rrq_s_id, sid);
	rrq.rrq_ox_id = htons(aborted_io_req->xid);
	rrq.rrq_rx_id = htons(aborted_io_req->task->rxwr_txrd.var_ctx.rx_id);

retry_rrq:
	rc = bnx2fc_initiate_els(tgt, ELS_RRQ, &rrq, sizeof(rrq),
				 bnx2fc_rrq_compl, cb_arg,
				 r_a_tov);
	if (rc == -ENOMEM) {
		if (time_after(jiffies, start + (10 * HZ))) {
			BNX2FC_ELS_DBG("rrq Failed\n");
			rc = FAILED;
			goto rrq_err;
		}
		msleep(20);
		goto retry_rrq;
	}
rrq_err:
	if (rc) {
		BNX2FC_ELS_DBG("RRQ failed - release orig io req 0x%x\n",
			aborted_io_req->xid);
		kfree(cb_arg);
		spin_lock_bh(&tgt->tgt_lock);
		kref_put(&aborted_io_req->refcount, bnx2fc_cmd_release);
		spin_unlock_bh(&tgt->tgt_lock);
	}
	return rc;
}

static void bnx2fc_l2_els_compl(struct bnx2fc_els_cb_arg *cb_arg)
{
	struct bnx2fc_cmd *els_req;
	struct bnx2fc_rport *tgt;
	struct bnx2fc_mp_req *mp_req;
	struct fc_frame_header *fc_hdr;
	unsigned char *buf;
	void *resp_buf;
	u32 resp_len, hdr_len;
	u16 l2_oxid;
	int frame_len;
	int rc = 0;

	l2_oxid = cb_arg->l2_oxid;
	BNX2FC_ELS_DBG("ELS COMPL - l2_oxid = 0x%x\n", l2_oxid);

	els_req = cb_arg->io_req;
	if (test_and_clear_bit(BNX2FC_FLAG_ELS_TIMEOUT, &els_req->req_flags)) {
		/*
		 * els req is timed out. cleanup the IO with FW and
		 * drop the completion. libfc will handle the els timeout
		 */
		if (els_req->on_active_queue) {
			list_del_init(&els_req->link);
			els_req->on_active_queue = 0;
			rc = bnx2fc_initiate_cleanup(els_req);
			BUG_ON(rc);
		}
		goto free_arg;
	}

	tgt = els_req->tgt;
	mp_req = &(els_req->mp_req);
	fc_hdr = &(mp_req->resp_fc_hdr);
	resp_len = mp_req->resp_len;
	resp_buf = mp_req->resp_buf;

	buf = kzalloc(PAGE_SIZE, GFP_ATOMIC);
	if (!buf) {
		printk(KERN_ERR PFX "Unable to alloc mp buf\n");
		goto free_arg;
	}
	hdr_len = sizeof(*fc_hdr);
	if (hdr_len + resp_len > PAGE_SIZE) {
		printk(KERN_ERR PFX "l2_els_compl: resp len is "
				    "beyond page size\n");
		goto free_buf;
	}
	memcpy(buf, fc_hdr, hdr_len);
	memcpy(buf + hdr_len, resp_buf, resp_len);
	frame_len = hdr_len + resp_len;

	bnx2fc_process_l2_frame_compl(tgt, buf, frame_len, l2_oxid);

free_buf:
	kfree(buf);
free_arg:
	kfree(cb_arg);
}

int bnx2fc_send_adisc(struct bnx2fc_rport *tgt, struct fc_frame *fp)
{
	struct fc_els_adisc *adisc;
	struct fc_frame_header *fh;
	struct bnx2fc_els_cb_arg *cb_arg;
	struct fc_lport *lport = tgt->rdata->local_port;
	u32 r_a_tov = lport->r_a_tov;
	int rc;

	fh = fc_frame_header_get(fp);
	cb_arg = kzalloc(sizeof(struct bnx2fc_els_cb_arg), GFP_ATOMIC);
	if (!cb_arg) {
		printk(KERN_ERR PFX "Unable to allocate cb_arg for ADISC\n");
		return -ENOMEM;
	}

	cb_arg->l2_oxid = ntohs(fh->fh_ox_id);

	BNX2FC_ELS_DBG("send ADISC: l2_oxid = 0x%x\n", cb_arg->l2_oxid);
	adisc = fc_frame_payload_get(fp, sizeof(*adisc));
	/* adisc is initialized by libfc */
	rc = bnx2fc_initiate_els(tgt, ELS_ADISC, adisc, sizeof(*adisc),
				 bnx2fc_l2_els_compl, cb_arg, 2 * r_a_tov);
	if (rc)
		kfree(cb_arg);
	return rc;
}

int bnx2fc_send_logo(struct bnx2fc_rport *tgt, struct fc_frame *fp)
{
	struct fc_els_logo *logo;
	struct fc_frame_header *fh;
	struct bnx2fc_els_cb_arg *cb_arg;
	struct fc_lport *lport = tgt->rdata->local_port;
	u32 r_a_tov = lport->r_a_tov;
	int rc;

	fh = fc_frame_header_get(fp);
	cb_arg = kzalloc(sizeof(struct bnx2fc_els_cb_arg), GFP_ATOMIC);
	if (!cb_arg) {
		printk(KERN_ERR PFX "Unable to allocate cb_arg for LOGO\n");
		return -ENOMEM;
	}

	cb_arg->l2_oxid = ntohs(fh->fh_ox_id);

	BNX2FC_ELS_DBG("Send LOGO: l2_oxid = 0x%x\n", cb_arg->l2_oxid);
	logo = fc_frame_payload_get(fp, sizeof(*logo));
	/* logo is initialized by libfc */
	rc = bnx2fc_initiate_els(tgt, ELS_LOGO, logo, sizeof(*logo),
				 bnx2fc_l2_els_compl, cb_arg, 2 * r_a_tov);
	if (rc)
		kfree(cb_arg);
	return rc;
}

int bnx2fc_send_rls(struct bnx2fc_rport *tgt, struct fc_frame *fp)
{
	struct fc_els_rls *rls;
	struct fc_frame_header *fh;
	struct bnx2fc_els_cb_arg *cb_arg;
	struct fc_lport *lport = tgt->rdata->local_port;
	u32 r_a_tov = lport->r_a_tov;
	int rc;

	fh = fc_frame_header_get(fp);
	cb_arg = kzalloc(sizeof(struct bnx2fc_els_cb_arg), GFP_ATOMIC);
	if (!cb_arg) {
		printk(KERN_ERR PFX "Unable to allocate cb_arg for LOGO\n");
		return -ENOMEM;
	}

	cb_arg->l2_oxid = ntohs(fh->fh_ox_id);

	rls = fc_frame_payload_get(fp, sizeof(*rls));
	/* rls is initialized by libfc */
	rc = bnx2fc_initiate_els(tgt, ELS_RLS, rls, sizeof(*rls),
				  bnx2fc_l2_els_compl, cb_arg, 2 * r_a_tov);
	if (rc)
		kfree(cb_arg);
	return rc;
}

static void bnx2fc_srr_compl(struct bnx2fc_els_cb_arg *cb_arg)
{
	struct bnx2fc_mp_req *mp_req;
	struct fc_frame_header *fc_hdr, *fh;
	struct bnx2fc_cmd *srr_req;
	struct bnx2fc_cmd *orig_io_req;
	struct fc_frame *fp;
	unsigned char *buf;
	void *resp_buf;
	u32 resp_len, hdr_len;
	u8 opcode;
	int rc = 0;

	orig_io_req = cb_arg->aborted_io_req;
	srr_req = cb_arg->io_req;
	if (test_and_clear_bit(BNX2FC_FLAG_ELS_TIMEOUT, &srr_req->req_flags)) {
		/* SRR timedout */
		BNX2FC_IO_DBG(srr_req, "srr timed out, abort "
		       "orig_io - 0x%x\n",
			orig_io_req->xid);
		rc = bnx2fc_initiate_abts(srr_req);
		if (rc != SUCCESS) {
			BNX2FC_IO_DBG(srr_req, "srr_compl: initiate_abts "
				"failed. issue cleanup\n");
			bnx2fc_initiate_cleanup(srr_req);
		}
		if (test_bit(BNX2FC_FLAG_IO_COMPL, &orig_io_req->req_flags) ||
		    test_bit(BNX2FC_FLAG_ISSUE_ABTS, &orig_io_req->req_flags)) {
			BNX2FC_IO_DBG(srr_req, "srr_compl:xid 0x%x flags = %lx",
				      orig_io_req->xid, orig_io_req->req_flags);
			goto srr_compl_done;
		}
		orig_io_req->srr_retry++;
		if (orig_io_req->srr_retry <= SRR_RETRY_COUNT) {
			struct bnx2fc_rport *tgt = orig_io_req->tgt;
			spin_unlock_bh(&tgt->tgt_lock);
			rc = bnx2fc_send_srr(orig_io_req,
					     orig_io_req->srr_offset,
					     orig_io_req->srr_rctl);
			spin_lock_bh(&tgt->tgt_lock);
			if (!rc)
				goto srr_compl_done;
		}

		rc = bnx2fc_initiate_abts(orig_io_req);
		if (rc != SUCCESS) {
			BNX2FC_IO_DBG(srr_req, "srr_compl: initiate_abts "
				"failed xid = 0x%x. issue cleanup\n",
				orig_io_req->xid);
			bnx2fc_initiate_cleanup(orig_io_req);
		}
		goto srr_compl_done;
	}
	if (test_bit(BNX2FC_FLAG_IO_COMPL, &orig_io_req->req_flags) ||
	    test_bit(BNX2FC_FLAG_ISSUE_ABTS, &orig_io_req->req_flags)) {
		BNX2FC_IO_DBG(srr_req, "srr_compl:xid - 0x%x flags = %lx",
			      orig_io_req->xid, orig_io_req->req_flags);
		goto srr_compl_done;
	}
	mp_req = &(srr_req->mp_req);
	fc_hdr = &(mp_req->resp_fc_hdr);
	resp_len = mp_req->resp_len;
	resp_buf = mp_req->resp_buf;

	hdr_len = sizeof(*fc_hdr);
	buf = kzalloc(PAGE_SIZE, GFP_ATOMIC);
	if (!buf) {
		printk(KERN_ERR PFX "srr buf: mem alloc failure\n");
		goto srr_compl_done;
	}
	memcpy(buf, fc_hdr, hdr_len);
	memcpy(buf + hdr_len, resp_buf, resp_len);

	fp = fc_frame_alloc(NULL, resp_len);
	if (!fp) {
		printk(KERN_ERR PFX "fc_frame_alloc failure\n");
		goto free_buf;
	}

	fh = (struct fc_frame_header *) fc_frame_header_get(fp);
	/* Copy FC Frame header and payload into the frame */
	memcpy(fh, buf, hdr_len + resp_len);

	opcode = fc_frame_payload_op(fp);
	switch (opcode) {
	case ELS_LS_ACC:
		BNX2FC_IO_DBG(srr_req, "SRR success\n");
		break;
	case ELS_LS_RJT:
		BNX2FC_IO_DBG(srr_req, "SRR rejected\n");
		rc = bnx2fc_initiate_abts(orig_io_req);
		if (rc != SUCCESS) {
			BNX2FC_IO_DBG(srr_req, "srr_compl: initiate_abts "
				"failed xid = 0x%x. issue cleanup\n",
				orig_io_req->xid);
			bnx2fc_initiate_cleanup(orig_io_req);
		}
		break;
	default:
		BNX2FC_IO_DBG(srr_req, "srr compl - invalid opcode = %d\n",
			opcode);
		break;
	}
	fc_frame_free(fp);
free_buf:
	kfree(buf);
srr_compl_done:
	kref_put(&orig_io_req->refcount, bnx2fc_cmd_release);
}

static void bnx2fc_rec_compl(struct bnx2fc_els_cb_arg *cb_arg)
{
	struct bnx2fc_cmd *orig_io_req, *new_io_req;
	struct bnx2fc_cmd *rec_req;
	struct bnx2fc_mp_req *mp_req;
	struct fc_frame_header *fc_hdr, *fh;
	struct fc_els_ls_rjt *rjt;
	struct fc_els_rec_acc *acc;
	struct bnx2fc_rport *tgt;
	struct fcoe_err_report_entry *err_entry;
	struct scsi_cmnd *sc_cmd;
	enum fc_rctl r_ctl;
	unsigned char *buf;
	void *resp_buf;
	struct fc_frame *fp;
	u8 opcode;
	u32 offset;
	u32 e_stat;
	u32 resp_len, hdr_len;
	int rc = 0;
	bool send_seq_clnp = false;
	bool abort_io = false;

	BNX2FC_MISC_DBG("Entered rec_compl callback\n");
	rec_req = cb_arg->io_req;
	orig_io_req = cb_arg->aborted_io_req;
	BNX2FC_IO_DBG(rec_req, "rec_compl: orig xid = 0x%x", orig_io_req->xid);
	tgt = orig_io_req->tgt;

	/* Handle REC timeout case */
	if (test_and_clear_bit(BNX2FC_FLAG_ELS_TIMEOUT, &rec_req->req_flags)) {
		BNX2FC_IO_DBG(rec_req, "timed out, abort "
		       "orig_io - 0x%x\n",
			orig_io_req->xid);
		/* els req is timed out. send abts for els */
		rc = bnx2fc_initiate_abts(rec_req);
		if (rc != SUCCESS) {
			BNX2FC_IO_DBG(rec_req, "rec_compl: initiate_abts "
				"failed. issue cleanup\n");
			bnx2fc_initiate_cleanup(rec_req);
		}
		orig_io_req->rec_retry++;
		/* REC timedout. send ABTS to the orig IO req */
		if (orig_io_req->rec_retry <= REC_RETRY_COUNT) {
			spin_unlock_bh(&tgt->tgt_lock);
			rc = bnx2fc_send_rec(orig_io_req);
			spin_lock_bh(&tgt->tgt_lock);
			if (!rc)
				goto rec_compl_done;
		}
		rc = bnx2fc_initiate_abts(orig_io_req);
		if (rc != SUCCESS) {
			BNX2FC_IO_DBG(rec_req, "rec_compl: initiate_abts "
				"failed xid = 0x%x. issue cleanup\n",
				orig_io_req->xid);
			bnx2fc_initiate_cleanup(orig_io_req);
		}
		goto rec_compl_done;
	}

	if (test_bit(BNX2FC_FLAG_IO_COMPL, &orig_io_req->req_flags)) {
		BNX2FC_IO_DBG(rec_req, "completed"
		       "orig_io - 0x%x\n",
			orig_io_req->xid);
		goto rec_compl_done;
	}
	if (test_bit(BNX2FC_FLAG_ISSUE_ABTS, &orig_io_req->req_flags)) {
		BNX2FC_IO_DBG(rec_req, "abts in prog "
		       "orig_io - 0x%x\n",
			orig_io_req->xid);
		goto rec_compl_done;
	}

	mp_req = &(rec_req->mp_req);
	fc_hdr = &(mp_req->resp_fc_hdr);
	resp_len = mp_req->resp_len;
	acc = resp_buf = mp_req->resp_buf;

	hdr_len = sizeof(*fc_hdr);

	buf = kzalloc(PAGE_SIZE, GFP_ATOMIC);
	if (!buf) {
		printk(KERN_ERR PFX "rec buf: mem alloc failure\n");
		goto rec_compl_done;
	}
	memcpy(buf, fc_hdr, hdr_len);
	memcpy(buf + hdr_len, resp_buf, resp_len);

	fp = fc_frame_alloc(NULL, resp_len);
	if (!fp) {
		printk(KERN_ERR PFX "fc_frame_alloc failure\n");
		goto free_buf;
	}

	fh = (struct fc_frame_header *) fc_frame_header_get(fp);
	/* Copy FC Frame header and payload into the frame */
	memcpy(fh, buf, hdr_len + resp_len);

	opcode = fc_frame_payload_op(fp);
	if (opcode == ELS_LS_RJT) {
		BNX2FC_IO_DBG(rec_req, "opcode is RJT\n");
		rjt = fc_frame_payload_get(fp, sizeof(*rjt));
		if ((rjt->er_reason == ELS_RJT_LOGIC ||
		    rjt->er_reason == ELS_RJT_UNAB) &&
		    rjt->er_explan == ELS_EXPL_OXID_RXID) {
			BNX2FC_IO_DBG(rec_req, "handle CMD LOST case\n");
			new_io_req = bnx2fc_cmd_alloc(tgt);
			if (!new_io_req)
				goto abort_io;
			new_io_req->sc_cmd = orig_io_req->sc_cmd;
			/* cleanup orig_io_req that is with the FW */
			set_bit(BNX2FC_FLAG_CMD_LOST,
				&orig_io_req->req_flags);
			bnx2fc_initiate_cleanup(orig_io_req);
			/* Post a new IO req with the same sc_cmd */
			BNX2FC_IO_DBG(rec_req, "Post IO request again\n");
			rc = bnx2fc_post_io_req(tgt, new_io_req);
			if (!rc)
				goto free_frame;
			BNX2FC_IO_DBG(rec_req, "REC: io post err\n");
		}
abort_io:
		rc = bnx2fc_initiate_abts(orig_io_req);
		if (rc != SUCCESS) {
			BNX2FC_IO_DBG(rec_req, "rec_compl: initiate_abts "
				"failed. issue cleanup\n");
			bnx2fc_initiate_cleanup(orig_io_req);
		}
	} else if (opcode == ELS_LS_ACC) {
		/* REVISIT: Check if the exchange is already aborted */
		offset = ntohl(acc->reca_fc4value);
		e_stat = ntohl(acc->reca_e_stat);
		if (e_stat & ESB_ST_SEQ_INIT)  {
			BNX2FC_IO_DBG(rec_req, "target has the seq init\n");
			goto free_frame;
		}
		BNX2FC_IO_DBG(rec_req, "e_stat = 0x%x, offset = 0x%x\n",
			e_stat, offset);
		/* Seq initiative is with us */
		err_entry = (struct fcoe_err_report_entry *)
			     &orig_io_req->err_entry;
		sc_cmd = orig_io_req->sc_cmd;
		if (sc_cmd->sc_data_direction == DMA_TO_DEVICE) {
			/* SCSI WRITE command */
			if (offset == orig_io_req->data_xfer_len) {
				BNX2FC_IO_DBG(rec_req, "WRITE - resp lost\n");
				/* FCP_RSP lost */
				r_ctl = FC_RCTL_DD_CMD_STATUS;
				offset = 0;
			} else  {
				/* start transmitting from offset */
				BNX2FC_IO_DBG(rec_req, "XFER_RDY/DATA lost\n");
				send_seq_clnp = true;
				r_ctl = FC_RCTL_DD_DATA_DESC;
				if (bnx2fc_initiate_seq_cleanup(orig_io_req,
								offset, r_ctl))
					abort_io = true;
				/* XFER_RDY */
			}
		} else {
			/* SCSI READ command */
			if (err_entry->data.rx_buf_off ==
					orig_io_req->data_xfer_len) {
				/* FCP_RSP lost */
				BNX2FC_IO_DBG(rec_req, "READ - resp lost\n");
				r_ctl = FC_RCTL_DD_CMD_STATUS;
				offset = 0;
			} else  {
				/* request retransmission from this offset */
				send_seq_clnp = true;
				offset = err_entry->data.rx_buf_off;
				BNX2FC_IO_DBG(rec_req, "RD DATA lost\n");
				/* FCP_DATA lost */
				r_ctl = FC_RCTL_DD_SOL_DATA;
				if (bnx2fc_initiate_seq_cleanup(orig_io_req,
								offset, r_ctl))
					abort_io = true;
			}
		}
		if (abort_io) {
			rc = bnx2fc_initiate_abts(orig_io_req);
			if (rc != SUCCESS) {
				BNX2FC_IO_DBG(rec_req, "rec_compl:initiate_abts"
					      " failed. issue cleanup\n");
				bnx2fc_initiate_cleanup(orig_io_req);
			}
		} else if (!send_seq_clnp) {
			BNX2FC_IO_DBG(rec_req, "Send SRR - FCP_RSP\n");
			spin_unlock_bh(&tgt->tgt_lock);
			rc = bnx2fc_send_srr(orig_io_req, offset, r_ctl);
			spin_lock_bh(&tgt->tgt_lock);

			if (rc) {
				BNX2FC_IO_DBG(rec_req, "Unable to send SRR"
					" IO will abort\n");
			}
		}
	}
free_frame:
	fc_frame_free(fp);
free_buf:
	kfree(buf);
rec_compl_done:
	kref_put(&orig_io_req->refcount, bnx2fc_cmd_release);
	kfree(cb_arg);
}

int bnx2fc_send_rec(struct bnx2fc_cmd *orig_io_req)
{
	struct fc_els_rec rec;
	struct bnx2fc_rport *tgt = orig_io_req->tgt;
	struct fc_lport *lport = tgt->rdata->local_port;
	struct bnx2fc_els_cb_arg *cb_arg = NULL;
	u32 sid = tgt->sid;
	u32 r_a_tov = lport->r_a_tov;
	int rc;

	BNX2FC_IO_DBG(orig_io_req, "Sending REC\n");
	memset(&rec, 0, sizeof(rec));

	cb_arg = kzalloc(sizeof(struct bnx2fc_els_cb_arg), GFP_ATOMIC);
	if (!cb_arg) {
		printk(KERN_ERR PFX "Unable to allocate cb_arg for REC\n");
		rc = -ENOMEM;
		goto rec_err;
	}
	kref_get(&orig_io_req->refcount);

	cb_arg->aborted_io_req = orig_io_req;

	rec.rec_cmd = ELS_REC;
	hton24(rec.rec_s_id, sid);
	rec.rec_ox_id = htons(orig_io_req->xid);
	rec.rec_rx_id = htons(orig_io_req->task->rxwr_txrd.var_ctx.rx_id);

	rc = bnx2fc_initiate_els(tgt, ELS_REC, &rec, sizeof(rec),
				 bnx2fc_rec_compl, cb_arg,
				 r_a_tov);
rec_err:
	if (rc) {
		BNX2FC_IO_DBG(orig_io_req, "REC failed - release\n");
		spin_lock_bh(&tgt->tgt_lock);
		kref_put(&orig_io_req->refcount, bnx2fc_cmd_release);
		spin_unlock_bh(&tgt->tgt_lock);
		kfree(cb_arg);
	}
	return rc;
}

int bnx2fc_send_srr(struct bnx2fc_cmd *orig_io_req, u32 offset, u8 r_ctl)
{
	struct fcp_srr srr;
	struct bnx2fc_rport *tgt = orig_io_req->tgt;
	struct fc_lport *lport = tgt->rdata->local_port;
	struct bnx2fc_els_cb_arg *cb_arg = NULL;
	u32 r_a_tov = lport->r_a_tov;
	int rc;

	BNX2FC_IO_DBG(orig_io_req, "Sending SRR\n");
	memset(&srr, 0, sizeof(srr));

	cb_arg = kzalloc(sizeof(struct bnx2fc_els_cb_arg), GFP_ATOMIC);
	if (!cb_arg) {
		printk(KERN_ERR PFX "Unable to allocate cb_arg for SRR\n");
		rc = -ENOMEM;
		goto srr_err;
	}
	kref_get(&orig_io_req->refcount);

	cb_arg->aborted_io_req = orig_io_req;

	srr.srr_op = ELS_SRR;
	srr.srr_ox_id = htons(orig_io_req->xid);
	srr.srr_rx_id = htons(orig_io_req->task->rxwr_txrd.var_ctx.rx_id);
	srr.srr_rel_off = htonl(offset);
	srr.srr_r_ctl = r_ctl;
	orig_io_req->srr_offset = offset;
	orig_io_req->srr_rctl = r_ctl;

	rc = bnx2fc_initiate_els(tgt, ELS_SRR, &srr, sizeof(srr),
				 bnx2fc_srr_compl, cb_arg,
				 r_a_tov);
srr_err:
	if (rc) {
		BNX2FC_IO_DBG(orig_io_req, "SRR failed - release\n");
		spin_lock_bh(&tgt->tgt_lock);
		kref_put(&orig_io_req->refcount, bnx2fc_cmd_release);
		spin_unlock_bh(&tgt->tgt_lock);
		kfree(cb_arg);
	} else
		set_bit(BNX2FC_FLAG_SRR_SENT, &orig_io_req->req_flags);

	return rc;
}

static int bnx2fc_initiate_els(struct bnx2fc_rport *tgt, unsigned int op,
			void *data, u32 data_len,
			void (*cb_func)(struct bnx2fc_els_cb_arg *cb_arg),
			struct bnx2fc_els_cb_arg *cb_arg, u32 timer_msec)
{
	struct fcoe_port *port = tgt->port;
	struct bnx2fc_interface *interface = port->priv;
	struct fc_rport *rport = tgt->rport;
	struct fc_lport *lport = port->lport;
	struct bnx2fc_cmd *els_req;
	struct bnx2fc_mp_req *mp_req;
	struct fc_frame_header *fc_hdr;
	struct fcoe_task_ctx_entry *task;
	struct fcoe_task_ctx_entry *task_page;
	int rc = 0;
	int task_idx, index;
	u32 did, sid;
	u16 xid;

	rc = fc_remote_port_chkready(rport);
	if (rc) {
		printk(KERN_ERR PFX "els 0x%x: rport not ready\n", op);
		rc = -EINVAL;
		goto els_err;
	}
	if (lport->state != LPORT_ST_READY || !(lport->link_up)) {
		printk(KERN_ERR PFX "els 0x%x: link is not ready\n", op);
		rc = -EINVAL;
		goto els_err;
	}
	if (!(test_bit(BNX2FC_FLAG_SESSION_READY, &tgt->flags))) {
		printk(KERN_ERR PFX "els 0x%x: tgt not ready\n", op);
		rc = -EINVAL;
		goto els_err;
	}
	els_req = bnx2fc_elstm_alloc(tgt, BNX2FC_ELS);
	if (!els_req) {
		rc = -ENOMEM;
		goto els_err;
	}

	els_req->sc_cmd = NULL;
	els_req->port = port;
	els_req->tgt = tgt;
	els_req->cb_func = cb_func;
	cb_arg->io_req = els_req;
	els_req->cb_arg = cb_arg;
	els_req->data_xfer_len = data_len;

	mp_req = (struct bnx2fc_mp_req *)&(els_req->mp_req);
	rc = bnx2fc_init_mp_req(els_req);
	if (rc == FAILED) {
		printk(KERN_ERR PFX "ELS MP request init failed\n");
		spin_lock_bh(&tgt->tgt_lock);
		kref_put(&els_req->refcount, bnx2fc_cmd_release);
		spin_unlock_bh(&tgt->tgt_lock);
		rc = -ENOMEM;
		goto els_err;
	} else {
		/* rc SUCCESS */
		rc = 0;
	}

	/* Set the data_xfer_len to the size of ELS payload */
	mp_req->req_len = data_len;
	els_req->data_xfer_len = mp_req->req_len;

	/* Fill ELS Payload */
	if ((op >= ELS_LS_RJT) && (op <= ELS_AUTH_ELS)) {
		memcpy(mp_req->req_buf, data, data_len);
	} else {
		printk(KERN_ERR PFX "Invalid ELS op 0x%x\n", op);
		els_req->cb_func = NULL;
		els_req->cb_arg = NULL;
		spin_lock_bh(&tgt->tgt_lock);
		kref_put(&els_req->refcount, bnx2fc_cmd_release);
		spin_unlock_bh(&tgt->tgt_lock);
		rc = -EINVAL;
	}

	if (rc)
		goto els_err;

	/* Fill FC header */
	fc_hdr = &(mp_req->req_fc_hdr);

	did = tgt->rport->port_id;
	sid = tgt->sid;

	if (op == ELS_SRR)
		__fc_fill_fc_hdr(fc_hdr, FC_RCTL_ELS4_REQ, did, sid,
				   FC_TYPE_FCP, FC_FC_FIRST_SEQ |
				   FC_FC_END_SEQ | FC_FC_SEQ_INIT, 0);
	else
		__fc_fill_fc_hdr(fc_hdr, FC_RCTL_ELS_REQ, did, sid,
				   FC_TYPE_ELS, FC_FC_FIRST_SEQ |
				   FC_FC_END_SEQ | FC_FC_SEQ_INIT, 0);

	/* Obtain exchange id */
	xid = els_req->xid;
	task_idx = xid/BNX2FC_TASKS_PER_PAGE;
	index = xid % BNX2FC_TASKS_PER_PAGE;

	/* Initialize task context for this IO request */
	task_page = (struct fcoe_task_ctx_entry *)
			interface->hba->task_ctx[task_idx];
	task = &(task_page[index]);
	bnx2fc_init_mp_task(els_req, task);

	spin_lock_bh(&tgt->tgt_lock);

	if (!test_bit(BNX2FC_FLAG_SESSION_READY, &tgt->flags)) {
		printk(KERN_ERR PFX "initiate_els.. session not ready\n");
		els_req->cb_func = NULL;
		els_req->cb_arg = NULL;
		kref_put(&els_req->refcount, bnx2fc_cmd_release);
		spin_unlock_bh(&tgt->tgt_lock);
		return -EINVAL;
	}

	if (timer_msec)
		bnx2fc_cmd_timer_set(els_req, timer_msec);
	bnx2fc_add_2_sq(tgt, xid);

	els_req->on_active_queue = 1;
	list_add_tail(&els_req->link, &tgt->els_queue);

	/* Ring doorbell */
	bnx2fc_ring_doorbell(tgt);
	spin_unlock_bh(&tgt->tgt_lock);

els_err:
	return rc;
}

void bnx2fc_process_els_compl(struct bnx2fc_cmd *els_req,
			      struct fcoe_task_ctx_entry *task, u8 num_rq)
{
	struct bnx2fc_mp_req *mp_req;
	struct fc_frame_header *fc_hdr;
	u64 *hdr;
	u64 *temp_hdr;

	BNX2FC_ELS_DBG("Entered process_els_compl xid = 0x%x"
			"cmd_type = %d\n", els_req->xid, els_req->cmd_type);

	if (test_and_set_bit(BNX2FC_FLAG_ELS_DONE,
			     &els_req->req_flags)) {
		BNX2FC_ELS_DBG("Timer context finished processing this "
			   "els - 0x%x\n", els_req->xid);
		/* This IO doesn't receive cleanup completion */
		kref_put(&els_req->refcount, bnx2fc_cmd_release);
		return;
	}

	/* Cancel the timeout_work, as we received the response */
	if (cancel_delayed_work(&els_req->timeout_work))
		kref_put(&els_req->refcount,
			 bnx2fc_cmd_release); /* drop timer hold */

	if (els_req->on_active_queue) {
		list_del_init(&els_req->link);
		els_req->on_active_queue = 0;
	}

	mp_req = &(els_req->mp_req);
	fc_hdr = &(mp_req->resp_fc_hdr);

	hdr = (u64 *)fc_hdr;
	temp_hdr = (u64 *)
		&task->rxwr_only.union_ctx.comp_info.mp_rsp.fc_hdr;
	hdr[0] = cpu_to_be64(temp_hdr[0]);
	hdr[1] = cpu_to_be64(temp_hdr[1]);
	hdr[2] = cpu_to_be64(temp_hdr[2]);

	mp_req->resp_len =
		task->rxwr_only.union_ctx.comp_info.mp_rsp.mp_payload_len;

	/* Parse ELS response */
	if ((els_req->cb_func) && (els_req->cb_arg)) {
		els_req->cb_func(els_req->cb_arg);
		els_req->cb_arg = NULL;
	}

	kref_put(&els_req->refcount, bnx2fc_cmd_release);
}

static void bnx2fc_flogi_resp(struct fc_seq *seq, struct fc_frame *fp,
			      void *arg)
{
	struct fcoe_ctlr *fip = arg;
	struct fc_exch *exch = fc_seq_exch(seq);
	struct fc_lport *lport = exch->lp;
	u8 *mac;
	u8 op;

	if (IS_ERR(fp))
		goto done;

	mac = fr_cb(fp)->granted_mac;
	if (is_zero_ether_addr(mac)) {
		op = fc_frame_payload_op(fp);
		if (lport->vport) {
			if (op == ELS_LS_RJT) {
				printk(KERN_ERR PFX "bnx2fc_flogi_resp is LS_RJT\n");
				fc_vport_terminate(lport->vport);
				fc_frame_free(fp);
				return;
			}
		}
		fcoe_ctlr_recv_flogi(fip, lport, fp);
	}
	if (!is_zero_ether_addr(mac))
		fip->update_mac(lport, mac);
done:
	fc_lport_flogi_resp(seq, fp, lport);
}

static void bnx2fc_logo_resp(struct fc_seq *seq, struct fc_frame *fp,
			     void *arg)
{
	struct fcoe_ctlr *fip = arg;
	struct fc_exch *exch = fc_seq_exch(seq);
	struct fc_lport *lport = exch->lp;
	static u8 zero_mac[ETH_ALEN] = { 0 };

	if (!IS_ERR(fp))
		fip->update_mac(lport, zero_mac);
	fc_lport_logo_resp(seq, fp, lport);
}

struct fc_seq *bnx2fc_elsct_send(struct fc_lport *lport, u32 did,
				      struct fc_frame *fp, unsigned int op,
				      void (*resp)(struct fc_seq *,
						   struct fc_frame *,
						   void *),
				      void *arg, u32 timeout)
{
	struct fcoe_port *port = lport_priv(lport);
	struct bnx2fc_interface *interface = port->priv;
	struct fcoe_ctlr *fip = bnx2fc_to_ctlr(interface);
	struct fc_frame_header *fh = fc_frame_header_get(fp);

	switch (op) {
	case ELS_FLOGI:
	case ELS_FDISC:
		return fc_elsct_send(lport, did, fp, op, bnx2fc_flogi_resp,
				     fip, timeout);
	case ELS_LOGO:
		/* only hook onto fabric logouts, not port logouts */
		if (ntoh24(fh->fh_d_id) != FC_FID_FLOGI)
			break;
		return fc_elsct_send(lport, did, fp, op, bnx2fc_logo_resp,
				     fip, timeout);
	}
	return fc_elsct_send(lport, did, fp, op, resp, arg, timeout);
}

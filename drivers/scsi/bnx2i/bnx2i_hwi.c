/* bnx2i_hwi.c: Broadcom NetXtreme II iSCSI driver.
 *
 * Copyright (c) 2006 - 2011 Broadcom Corporation
 * Copyright (c) 2007, 2008 Red Hat, Inc.  All rights reserved.
 * Copyright (c) 2007, 2008 Mike Christie
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation.
 *
 * Written by: Anil Veerabhadrappa (anilgv@broadcom.com)
 * Maintained by: Eddie Wai (eddie.wai@broadcom.com)
 */

#include <linux/gfp.h>
#include <scsi/scsi_tcq.h>
#include <scsi/libiscsi.h>
#include "bnx2i.h"

DECLARE_PER_CPU(struct bnx2i_percpu_s, bnx2i_percpu);

/**
 * bnx2i_get_cid_num - get cid from ep
 * @ep: 	endpoint pointer
 *
 * Only applicable to 57710 family of devices
 */
static u32 bnx2i_get_cid_num(struct bnx2i_endpoint *ep)
{
	u32 cid;

	if (test_bit(BNX2I_NX2_DEV_57710, &ep->hba->cnic_dev_type))
		cid = ep->ep_cid;
	else
		cid = GET_CID_NUM(ep->ep_cid);
	return cid;
}


/**
 * bnx2i_adjust_qp_size - Adjust SQ/RQ/CQ size for 57710 device type
 * @hba: 		Adapter for which adjustments is to be made
 *
 * Only applicable to 57710 family of devices
 */
static void bnx2i_adjust_qp_size(struct bnx2i_hba *hba)
{
	u32 num_elements_per_pg;

	if (test_bit(BNX2I_NX2_DEV_5706, &hba->cnic_dev_type) ||
	    test_bit(BNX2I_NX2_DEV_5708, &hba->cnic_dev_type) ||
	    test_bit(BNX2I_NX2_DEV_5709, &hba->cnic_dev_type)) {
		if (!is_power_of_2(hba->max_sqes))
			hba->max_sqes = rounddown_pow_of_two(hba->max_sqes);

		if (!is_power_of_2(hba->max_rqes))
			hba->max_rqes = rounddown_pow_of_two(hba->max_rqes);
	}

	/* Adjust each queue size if the user selection does not
	 * yield integral num of page buffers
	 */
	/* adjust SQ */
	num_elements_per_pg = PAGE_SIZE / BNX2I_SQ_WQE_SIZE;
	if (hba->max_sqes < num_elements_per_pg)
		hba->max_sqes = num_elements_per_pg;
	else if (hba->max_sqes % num_elements_per_pg)
		hba->max_sqes = (hba->max_sqes + num_elements_per_pg - 1) &
				 ~(num_elements_per_pg - 1);

	/* adjust CQ */
	num_elements_per_pg = PAGE_SIZE / BNX2I_CQE_SIZE;
	if (hba->max_cqes < num_elements_per_pg)
		hba->max_cqes = num_elements_per_pg;
	else if (hba->max_cqes % num_elements_per_pg)
		hba->max_cqes = (hba->max_cqes + num_elements_per_pg - 1) &
				 ~(num_elements_per_pg - 1);

	/* adjust RQ */
	num_elements_per_pg = PAGE_SIZE / BNX2I_RQ_WQE_SIZE;
	if (hba->max_rqes < num_elements_per_pg)
		hba->max_rqes = num_elements_per_pg;
	else if (hba->max_rqes % num_elements_per_pg)
		hba->max_rqes = (hba->max_rqes + num_elements_per_pg - 1) &
				 ~(num_elements_per_pg - 1);
}


/**
 * bnx2i_get_link_state - get network interface link state
 * @hba:	adapter instance pointer
 *
 * updates adapter structure flag based on netdev state
 */
static void bnx2i_get_link_state(struct bnx2i_hba *hba)
{
	if (test_bit(__LINK_STATE_NOCARRIER, &hba->netdev->state))
		set_bit(ADAPTER_STATE_LINK_DOWN, &hba->adapter_state);
	else
		clear_bit(ADAPTER_STATE_LINK_DOWN, &hba->adapter_state);
}


/**
 * bnx2i_iscsi_license_error - displays iscsi license related error message
 * @hba:		adapter instance pointer
 * @error_code:		error classification
 *
 * Puts out an error log when driver is unable to offload iscsi connection
 *	due to license restrictions
 */
static void bnx2i_iscsi_license_error(struct bnx2i_hba *hba, u32 error_code)
{
	if (error_code == ISCSI_KCQE_COMPLETION_STATUS_ISCSI_NOT_SUPPORTED)
		/* iSCSI offload not supported on this device */
		printk(KERN_ERR "bnx2i: iSCSI not supported, dev=%s\n",
				hba->netdev->name);
	if (error_code == ISCSI_KCQE_COMPLETION_STATUS_LOM_ISCSI_NOT_ENABLED)
		/* iSCSI offload not supported on this LOM device */
		printk(KERN_ERR "bnx2i: LOM is not enable to "
				"offload iSCSI connections, dev=%s\n",
				hba->netdev->name);
	set_bit(ADAPTER_STATE_INIT_FAILED, &hba->adapter_state);
}


/**
 * bnx2i_arm_cq_event_coalescing - arms CQ to enable EQ notification
 * @ep:		endpoint (transport indentifier) structure
 * @action:	action, ARM or DISARM. For now only ARM_CQE is used
 *
 * Arm'ing CQ will enable chip to generate global EQ events inorder to interrupt
 *	the driver. EQ event is generated CQ index is hit or at least 1 CQ is
 *	outstanding and on chip timer expires
 */
int bnx2i_arm_cq_event_coalescing(struct bnx2i_endpoint *ep, u8 action)
{
	struct bnx2i_5771x_cq_db *cq_db;
	u16 cq_index;
	u16 next_index = 0;
	u32 num_active_cmds;

	/* Coalesce CQ entries only on 10G devices */
	if (!test_bit(BNX2I_NX2_DEV_57710, &ep->hba->cnic_dev_type))
		return 0;

	/* Do not update CQ DB multiple times before firmware writes
	 * '0xFFFF' to CQDB->SQN field. Deviation may cause spurious
	 * interrupts and other unwanted results
	 */
	cq_db = (struct bnx2i_5771x_cq_db *) ep->qp.cq_pgtbl_virt;

	if (action != CNIC_ARM_CQE_FP)
		if (cq_db->sqn[0] && cq_db->sqn[0] != 0xFFFF)
			return 0;

	if (action == CNIC_ARM_CQE || action == CNIC_ARM_CQE_FP) {
		num_active_cmds = atomic_read(&ep->num_active_cmds);
		if (num_active_cmds <= event_coal_min)
			next_index = 1;
		else {
			next_index = num_active_cmds >> ep->ec_shift;
			if (next_index > num_active_cmds - event_coal_min)
				next_index = num_active_cmds - event_coal_min;
		}
		if (!next_index)
			next_index = 1;
		cq_index = ep->qp.cqe_exp_seq_sn + next_index - 1;
		if (cq_index > ep->qp.cqe_size * 2)
			cq_index -= ep->qp.cqe_size * 2;
		if (!cq_index)
			cq_index = 1;

		cq_db->sqn[0] = cq_index;
	}
	return next_index;
}


/**
 * bnx2i_get_rq_buf - copy RQ buffer contents to driver buffer
 * @conn:		iscsi connection on which RQ event occurred
 * @ptr:		driver buffer to which RQ buffer contents is to
 *			be copied
 * @len:		length of valid data inside RQ buf
 *
 * Copies RQ buffer contents from shared (DMA'able) memory region to
 *	driver buffer. RQ is used to DMA unsolicitated iscsi pdu's and
 *	scsi sense info
 */
void bnx2i_get_rq_buf(struct bnx2i_conn *bnx2i_conn, char *ptr, int len)
{
	if (!bnx2i_conn->ep->qp.rqe_left)
		return;

	bnx2i_conn->ep->qp.rqe_left--;
	memcpy(ptr, (u8 *) bnx2i_conn->ep->qp.rq_cons_qe, len);
	if (bnx2i_conn->ep->qp.rq_cons_qe == bnx2i_conn->ep->qp.rq_last_qe) {
		bnx2i_conn->ep->qp.rq_cons_qe = bnx2i_conn->ep->qp.rq_first_qe;
		bnx2i_conn->ep->qp.rq_cons_idx = 0;
	} else {
		bnx2i_conn->ep->qp.rq_cons_qe++;
		bnx2i_conn->ep->qp.rq_cons_idx++;
	}
}


static void bnx2i_ring_577xx_doorbell(struct bnx2i_conn *conn)
{
	struct bnx2i_5771x_dbell dbell;
	u32 msg;

	memset(&dbell, 0, sizeof(dbell));
	dbell.dbell.header = (B577XX_ISCSI_CONNECTION_TYPE <<
			      B577XX_DOORBELL_HDR_CONN_TYPE_SHIFT);
	msg = *((u32 *)&dbell);
	/* TODO : get doorbell register mapping */
	writel(cpu_to_le32(msg), conn->ep->qp.ctx_base);
}


/**
 * bnx2i_put_rq_buf - Replenish RQ buffer, if required ring on chip doorbell
 * @conn:	iscsi connection on which event to post
 * @count:	number of RQ buffer being posted to chip
 *
 * No need to ring hardware doorbell for 57710 family of devices
 */
void bnx2i_put_rq_buf(struct bnx2i_conn *bnx2i_conn, int count)
{
	struct bnx2i_5771x_sq_rq_db *rq_db;
	u16 hi_bit = (bnx2i_conn->ep->qp.rq_prod_idx & 0x8000);
	struct bnx2i_endpoint *ep = bnx2i_conn->ep;

	ep->qp.rqe_left += count;
	ep->qp.rq_prod_idx &= 0x7FFF;
	ep->qp.rq_prod_idx += count;

	if (ep->qp.rq_prod_idx > bnx2i_conn->hba->max_rqes) {
		ep->qp.rq_prod_idx %= bnx2i_conn->hba->max_rqes;
		if (!hi_bit)
			ep->qp.rq_prod_idx |= 0x8000;
	} else
		ep->qp.rq_prod_idx |= hi_bit;

	if (test_bit(BNX2I_NX2_DEV_57710, &ep->hba->cnic_dev_type)) {
		rq_db = (struct bnx2i_5771x_sq_rq_db *) ep->qp.rq_pgtbl_virt;
		rq_db->prod_idx = ep->qp.rq_prod_idx;
		/* no need to ring hardware doorbell for 57710 */
	} else {
		writew(ep->qp.rq_prod_idx,
		       ep->qp.ctx_base + CNIC_RECV_DOORBELL);
	}
	mmiowb();
}


/**
 * bnx2i_ring_sq_dbell - Ring SQ doorbell to wake-up the processing engine
 * @conn: 		iscsi connection to which new SQ entries belong
 * @count: 		number of SQ WQEs to post
 *
 * SQ DB is updated in host memory and TX Doorbell is rung for 57710 family
 *	of devices. For 5706/5708/5709 new SQ WQE count is written into the
 *	doorbell register
 */
static void bnx2i_ring_sq_dbell(struct bnx2i_conn *bnx2i_conn, int count)
{
	struct bnx2i_5771x_sq_rq_db *sq_db;
	struct bnx2i_endpoint *ep = bnx2i_conn->ep;

	atomic_inc(&ep->num_active_cmds);
	wmb();	/* flush SQ WQE memory before the doorbell is rung */
	if (test_bit(BNX2I_NX2_DEV_57710, &ep->hba->cnic_dev_type)) {
		sq_db = (struct bnx2i_5771x_sq_rq_db *) ep->qp.sq_pgtbl_virt;
		sq_db->prod_idx = ep->qp.sq_prod_idx;
		bnx2i_ring_577xx_doorbell(bnx2i_conn);
	} else
		writew(count, ep->qp.ctx_base + CNIC_SEND_DOORBELL);

	mmiowb(); /* flush posted PCI writes */
}


/**
 * bnx2i_ring_dbell_update_sq_params - update SQ driver parameters
 * @conn:	iscsi connection to which new SQ entries belong
 * @count:	number of SQ WQEs to post
 *
 * this routine will update SQ driver parameters and ring the doorbell
 */
static void bnx2i_ring_dbell_update_sq_params(struct bnx2i_conn *bnx2i_conn,
					      int count)
{
	int tmp_cnt;

	if (count == 1) {
		if (bnx2i_conn->ep->qp.sq_prod_qe ==
		    bnx2i_conn->ep->qp.sq_last_qe)
			bnx2i_conn->ep->qp.sq_prod_qe =
						bnx2i_conn->ep->qp.sq_first_qe;
		else
			bnx2i_conn->ep->qp.sq_prod_qe++;
	} else {
		if ((bnx2i_conn->ep->qp.sq_prod_qe + count) <=
		    bnx2i_conn->ep->qp.sq_last_qe)
			bnx2i_conn->ep->qp.sq_prod_qe += count;
		else {
			tmp_cnt = bnx2i_conn->ep->qp.sq_last_qe -
				bnx2i_conn->ep->qp.sq_prod_qe;
			bnx2i_conn->ep->qp.sq_prod_qe =
				&bnx2i_conn->ep->qp.sq_first_qe[count -
								(tmp_cnt + 1)];
		}
	}
	bnx2i_conn->ep->qp.sq_prod_idx += count;
	/* Ring the doorbell */
	bnx2i_ring_sq_dbell(bnx2i_conn, bnx2i_conn->ep->qp.sq_prod_idx);
}


/**
 * bnx2i_send_iscsi_login - post iSCSI login request MP WQE to hardware
 * @conn:	iscsi connection
 * @cmd:	driver command structure which is requesting
 *		a WQE to sent to chip for further processing
 *
 * prepare and post an iSCSI Login request WQE to CNIC firmware
 */
int bnx2i_send_iscsi_login(struct bnx2i_conn *bnx2i_conn,
			   struct iscsi_task *task)
{
	struct bnx2i_cmd *bnx2i_cmd;
	struct bnx2i_login_request *login_wqe;
	struct iscsi_login_req *login_hdr;
	u32 dword;

	bnx2i_cmd = (struct bnx2i_cmd *)task->dd_data;
	login_hdr = (struct iscsi_login_req *)task->hdr;
	login_wqe = (struct bnx2i_login_request *)
						bnx2i_conn->ep->qp.sq_prod_qe;

	login_wqe->op_code = login_hdr->opcode;
	login_wqe->op_attr = login_hdr->flags;
	login_wqe->version_max = login_hdr->max_version;
	login_wqe->version_min = login_hdr->min_version;
	login_wqe->data_length = ntoh24(login_hdr->dlength);
	login_wqe->isid_lo = *((u32 *) login_hdr->isid);
	login_wqe->isid_hi = *((u16 *) login_hdr->isid + 2);
	login_wqe->tsih = login_hdr->tsih;
	login_wqe->itt = task->itt |
		(ISCSI_TASK_TYPE_MPATH << ISCSI_LOGIN_REQUEST_TYPE_SHIFT);
	login_wqe->cid = login_hdr->cid;

	login_wqe->cmd_sn = be32_to_cpu(login_hdr->cmdsn);
	login_wqe->exp_stat_sn = be32_to_cpu(login_hdr->exp_statsn);
	login_wqe->flags = ISCSI_LOGIN_REQUEST_UPDATE_EXP_STAT_SN;

	login_wqe->resp_bd_list_addr_lo = (u32) bnx2i_conn->gen_pdu.resp_bd_dma;
	login_wqe->resp_bd_list_addr_hi =
		(u32) ((u64) bnx2i_conn->gen_pdu.resp_bd_dma >> 32);

	dword = ((1 << ISCSI_LOGIN_REQUEST_NUM_RESP_BDS_SHIFT) |
		 (bnx2i_conn->gen_pdu.resp_buf_size <<
		  ISCSI_LOGIN_REQUEST_RESP_BUFFER_LENGTH_SHIFT));
	login_wqe->resp_buffer = dword;
	login_wqe->bd_list_addr_lo = (u32) bnx2i_conn->gen_pdu.req_bd_dma;
	login_wqe->bd_list_addr_hi =
		(u32) ((u64) bnx2i_conn->gen_pdu.req_bd_dma >> 32);
	login_wqe->num_bds = 1;
	login_wqe->cq_index = 0; /* CQ# used for completion, 5771x only */

	bnx2i_ring_dbell_update_sq_params(bnx2i_conn, 1);
	return 0;
}

/**
 * bnx2i_send_iscsi_tmf - post iSCSI task management request MP WQE to hardware
 * @conn:	iscsi connection
 * @mtask:	driver command structure which is requesting
 *		a WQE to sent to chip for further processing
 *
 * prepare and post an iSCSI Login request WQE to CNIC firmware
 */
int bnx2i_send_iscsi_tmf(struct bnx2i_conn *bnx2i_conn,
			 struct iscsi_task *mtask)
{
	struct iscsi_conn *conn = bnx2i_conn->cls_conn->dd_data;
	struct iscsi_tm *tmfabort_hdr;
	struct scsi_cmnd *ref_sc;
	struct iscsi_task *ctask;
	struct bnx2i_cmd *bnx2i_cmd;
	struct bnx2i_tmf_request *tmfabort_wqe;
	u32 dword;
	u32 scsi_lun[2];

	bnx2i_cmd = (struct bnx2i_cmd *)mtask->dd_data;
	tmfabort_hdr = (struct iscsi_tm *)mtask->hdr;
	tmfabort_wqe = (struct bnx2i_tmf_request *)
						bnx2i_conn->ep->qp.sq_prod_qe;

	tmfabort_wqe->op_code = tmfabort_hdr->opcode;
	tmfabort_wqe->op_attr = tmfabort_hdr->flags;

	tmfabort_wqe->itt = (mtask->itt | (ISCSI_TASK_TYPE_MPATH << 14));
	tmfabort_wqe->reserved2 = 0;
	tmfabort_wqe->cmd_sn = be32_to_cpu(tmfabort_hdr->cmdsn);

	switch (tmfabort_hdr->flags & ISCSI_FLAG_TM_FUNC_MASK) {
	case ISCSI_TM_FUNC_ABORT_TASK:
	case ISCSI_TM_FUNC_TASK_REASSIGN:
		ctask = iscsi_itt_to_task(conn, tmfabort_hdr->rtt);
		if (!ctask || !ctask->sc)
			/*
			 * the iscsi layer must have completed the cmd while
			 * was starting up.
			 *
			 * Note: In the case of a SCSI cmd timeout, the task's
			 *       sc is still active; hence ctask->sc != 0
			 *       In this case, the task must be aborted
			 */
			return 0;

		ref_sc = ctask->sc;
		if (ref_sc->sc_data_direction == DMA_TO_DEVICE)
			dword = (ISCSI_TASK_TYPE_WRITE <<
				 ISCSI_CMD_REQUEST_TYPE_SHIFT);
		else
			dword = (ISCSI_TASK_TYPE_READ <<
				 ISCSI_CMD_REQUEST_TYPE_SHIFT);
		tmfabort_wqe->ref_itt = (dword |
					(tmfabort_hdr->rtt & ISCSI_ITT_MASK));
		break;
	default:
		tmfabort_wqe->ref_itt = RESERVED_ITT;
	}
	memcpy(scsi_lun, &tmfabort_hdr->lun, sizeof(struct scsi_lun));
	tmfabort_wqe->lun[0] = be32_to_cpu(scsi_lun[0]);
	tmfabort_wqe->lun[1] = be32_to_cpu(scsi_lun[1]);

	tmfabort_wqe->ref_cmd_sn = be32_to_cpu(tmfabort_hdr->refcmdsn);

	tmfabort_wqe->bd_list_addr_lo = (u32) bnx2i_conn->hba->mp_bd_dma;
	tmfabort_wqe->bd_list_addr_hi = (u32)
				((u64) bnx2i_conn->hba->mp_bd_dma >> 32);
	tmfabort_wqe->num_bds = 1;
	tmfabort_wqe->cq_index = 0; /* CQ# used for completion, 5771x only */

	bnx2i_ring_dbell_update_sq_params(bnx2i_conn, 1);
	return 0;
}

/**
 * bnx2i_send_iscsi_text - post iSCSI text WQE to hardware
 * @conn:	iscsi connection
 * @mtask:	driver command structure which is requesting
 *		a WQE to sent to chip for further processing
 *
 * prepare and post an iSCSI Text request WQE to CNIC firmware
 */
int bnx2i_send_iscsi_text(struct bnx2i_conn *bnx2i_conn,
			  struct iscsi_task *mtask)
{
	struct bnx2i_cmd *bnx2i_cmd;
	struct bnx2i_text_request *text_wqe;
	struct iscsi_text *text_hdr;
	u32 dword;

	bnx2i_cmd = (struct bnx2i_cmd *)mtask->dd_data;
	text_hdr = (struct iscsi_text *)mtask->hdr;
	text_wqe = (struct bnx2i_text_request *) bnx2i_conn->ep->qp.sq_prod_qe;

	memset(text_wqe, 0, sizeof(struct bnx2i_text_request));

	text_wqe->op_code = text_hdr->opcode;
	text_wqe->op_attr = text_hdr->flags;
	text_wqe->data_length = ntoh24(text_hdr->dlength);
	text_wqe->itt = mtask->itt |
		(ISCSI_TASK_TYPE_MPATH << ISCSI_TEXT_REQUEST_TYPE_SHIFT);
	text_wqe->ttt = be32_to_cpu(text_hdr->ttt);

	text_wqe->cmd_sn = be32_to_cpu(text_hdr->cmdsn);

	text_wqe->resp_bd_list_addr_lo = (u32) bnx2i_conn->gen_pdu.resp_bd_dma;
	text_wqe->resp_bd_list_addr_hi =
			(u32) ((u64) bnx2i_conn->gen_pdu.resp_bd_dma >> 32);

	dword = ((1 << ISCSI_TEXT_REQUEST_NUM_RESP_BDS_SHIFT) |
		 (bnx2i_conn->gen_pdu.resp_buf_size <<
		  ISCSI_TEXT_REQUEST_RESP_BUFFER_LENGTH_SHIFT));
	text_wqe->resp_buffer = dword;
	text_wqe->bd_list_addr_lo = (u32) bnx2i_conn->gen_pdu.req_bd_dma;
	text_wqe->bd_list_addr_hi =
			(u32) ((u64) bnx2i_conn->gen_pdu.req_bd_dma >> 32);
	text_wqe->num_bds = 1;
	text_wqe->cq_index = 0; /* CQ# used for completion, 5771x only */

	bnx2i_ring_dbell_update_sq_params(bnx2i_conn, 1);
	return 0;
}


/**
 * bnx2i_send_iscsi_scsicmd - post iSCSI scsicmd request WQE to hardware
 * @conn:	iscsi connection
 * @cmd:	driver command structure which is requesting
 *		a WQE to sent to chip for further processing
 *
 * prepare and post an iSCSI SCSI-CMD request WQE to CNIC firmware
 */
int bnx2i_send_iscsi_scsicmd(struct bnx2i_conn *bnx2i_conn,
			     struct bnx2i_cmd *cmd)
{
	struct bnx2i_cmd_request *scsi_cmd_wqe;

	scsi_cmd_wqe = (struct bnx2i_cmd_request *)
						bnx2i_conn->ep->qp.sq_prod_qe;
	memcpy(scsi_cmd_wqe, &cmd->req, sizeof(struct bnx2i_cmd_request));
	scsi_cmd_wqe->cq_index = 0; /* CQ# used for completion, 5771x only */

	bnx2i_ring_dbell_update_sq_params(bnx2i_conn, 1);
	return 0;
}

/**
 * bnx2i_send_iscsi_nopout - post iSCSI NOPOUT request WQE to hardware
 * @conn:		iscsi connection
 * @cmd:		driver command structure which is requesting
 *			a WQE to sent to chip for further processing
 * @datap:		payload buffer pointer
 * @data_len:		payload data length
 * @unsol:		indicated whether nopout pdu is unsolicited pdu or
 *			in response to target's NOPIN w/ TTT != FFFFFFFF
 *
 * prepare and post a nopout request WQE to CNIC firmware
 */
int bnx2i_send_iscsi_nopout(struct bnx2i_conn *bnx2i_conn,
			    struct iscsi_task *task,
			    char *datap, int data_len, int unsol)
{
	struct bnx2i_endpoint *ep = bnx2i_conn->ep;
	struct bnx2i_cmd *bnx2i_cmd;
	struct bnx2i_nop_out_request *nopout_wqe;
	struct iscsi_nopout *nopout_hdr;

	bnx2i_cmd = (struct bnx2i_cmd *)task->dd_data;
	nopout_hdr = (struct iscsi_nopout *)task->hdr;
	nopout_wqe = (struct bnx2i_nop_out_request *)ep->qp.sq_prod_qe;

	memset(nopout_wqe, 0x00, sizeof(struct bnx2i_nop_out_request));

	nopout_wqe->op_code = nopout_hdr->opcode;
	nopout_wqe->op_attr = ISCSI_FLAG_CMD_FINAL;
	memcpy(nopout_wqe->lun, &nopout_hdr->lun, 8);

	if (test_bit(BNX2I_NX2_DEV_57710, &ep->hba->cnic_dev_type)) {
		u32 tmp = nopout_wqe->lun[0];
		/* 57710 requires LUN field to be swapped */
		nopout_wqe->lun[0] = nopout_wqe->lun[1];
		nopout_wqe->lun[1] = tmp;
	}

	nopout_wqe->itt = ((u16)task->itt |
			   (ISCSI_TASK_TYPE_MPATH <<
			    ISCSI_TMF_REQUEST_TYPE_SHIFT));
	nopout_wqe->ttt = be32_to_cpu(nopout_hdr->ttt);
	nopout_wqe->flags = 0;
	if (!unsol)
		nopout_wqe->flags = ISCSI_NOP_OUT_REQUEST_LOCAL_COMPLETION;
	else if (nopout_hdr->itt == RESERVED_ITT)
		nopout_wqe->flags = ISCSI_NOP_OUT_REQUEST_LOCAL_COMPLETION;

	nopout_wqe->cmd_sn = be32_to_cpu(nopout_hdr->cmdsn);
	nopout_wqe->data_length = data_len;
	if (data_len) {
		/* handle payload data, not required in first release */
		printk(KERN_ALERT "NOPOUT: WARNING!! payload len != 0\n");
	} else {
		nopout_wqe->bd_list_addr_lo = (u32)
					bnx2i_conn->hba->mp_bd_dma;
		nopout_wqe->bd_list_addr_hi =
			(u32) ((u64) bnx2i_conn->hba->mp_bd_dma >> 32);
		nopout_wqe->num_bds = 1;
	}
	nopout_wqe->cq_index = 0; /* CQ# used for completion, 5771x only */

	bnx2i_ring_dbell_update_sq_params(bnx2i_conn, 1);
	return 0;
}


/**
 * bnx2i_send_iscsi_logout - post iSCSI logout request WQE to hardware
 * @conn:	iscsi connection
 * @cmd:	driver command structure which is requesting
 *		a WQE to sent to chip for further processing
 *
 * prepare and post logout request WQE to CNIC firmware
 */
int bnx2i_send_iscsi_logout(struct bnx2i_conn *bnx2i_conn,
			    struct iscsi_task *task)
{
	struct bnx2i_cmd *bnx2i_cmd;
	struct bnx2i_logout_request *logout_wqe;
	struct iscsi_logout *logout_hdr;

	bnx2i_cmd = (struct bnx2i_cmd *)task->dd_data;
	logout_hdr = (struct iscsi_logout *)task->hdr;

	logout_wqe = (struct bnx2i_logout_request *)
						bnx2i_conn->ep->qp.sq_prod_qe;
	memset(logout_wqe, 0x00, sizeof(struct bnx2i_logout_request));

	logout_wqe->op_code = logout_hdr->opcode;
	logout_wqe->cmd_sn = be32_to_cpu(logout_hdr->cmdsn);
	logout_wqe->op_attr =
			logout_hdr->flags | ISCSI_LOGOUT_REQUEST_ALWAYS_ONE;
	logout_wqe->itt = ((u16)task->itt |
			   (ISCSI_TASK_TYPE_MPATH <<
			    ISCSI_LOGOUT_REQUEST_TYPE_SHIFT));
	logout_wqe->data_length = 0;
	logout_wqe->cid = 0;

	logout_wqe->bd_list_addr_lo = (u32) bnx2i_conn->hba->mp_bd_dma;
	logout_wqe->bd_list_addr_hi = (u32)
				((u64) bnx2i_conn->hba->mp_bd_dma >> 32);
	logout_wqe->num_bds = 1;
	logout_wqe->cq_index = 0; /* CQ# used for completion, 5771x only */

	bnx2i_conn->ep->state = EP_STATE_LOGOUT_SENT;

	bnx2i_ring_dbell_update_sq_params(bnx2i_conn, 1);
	return 0;
}


/**
 * bnx2i_update_iscsi_conn - post iSCSI logout request WQE to hardware
 * @conn:	iscsi connection which requires iscsi parameter update
 *
 * sends down iSCSI Conn Update request to move iSCSI conn to FFP
 */
void bnx2i_update_iscsi_conn(struct iscsi_conn *conn)
{
	struct bnx2i_conn *bnx2i_conn = conn->dd_data;
	struct bnx2i_hba *hba = bnx2i_conn->hba;
	struct kwqe *kwqe_arr[2];
	struct iscsi_kwqe_conn_update *update_wqe;
	struct iscsi_kwqe_conn_update conn_update_kwqe;

	update_wqe = &conn_update_kwqe;

	update_wqe->hdr.op_code = ISCSI_KWQE_OPCODE_UPDATE_CONN;
	update_wqe->hdr.flags =
		(ISCSI_KWQE_LAYER_CODE << ISCSI_KWQE_HEADER_LAYER_CODE_SHIFT);

	/* 5771x requires conn context id to be passed as is */
	if (test_bit(BNX2I_NX2_DEV_57710, &bnx2i_conn->ep->hba->cnic_dev_type))
		update_wqe->context_id = bnx2i_conn->ep->ep_cid;
	else
		update_wqe->context_id = (bnx2i_conn->ep->ep_cid >> 7);
	update_wqe->conn_flags = 0;
	if (conn->hdrdgst_en)
		update_wqe->conn_flags |= ISCSI_KWQE_CONN_UPDATE_HEADER_DIGEST;
	if (conn->datadgst_en)
		update_wqe->conn_flags |= ISCSI_KWQE_CONN_UPDATE_DATA_DIGEST;
	if (conn->session->initial_r2t_en)
		update_wqe->conn_flags |= ISCSI_KWQE_CONN_UPDATE_INITIAL_R2T;
	if (conn->session->imm_data_en)
		update_wqe->conn_flags |= ISCSI_KWQE_CONN_UPDATE_IMMEDIATE_DATA;

	update_wqe->max_send_pdu_length = conn->max_xmit_dlength;
	update_wqe->max_recv_pdu_length = conn->max_recv_dlength;
	update_wqe->first_burst_length = conn->session->first_burst;
	update_wqe->max_burst_length = conn->session->max_burst;
	update_wqe->exp_stat_sn = conn->exp_statsn;
	update_wqe->max_outstanding_r2ts = conn->session->max_r2t;
	update_wqe->session_error_recovery_level = conn->session->erl;
	iscsi_conn_printk(KERN_ALERT, conn,
			  "bnx2i: conn update - MBL 0x%x FBL 0x%x"
			  "MRDSL_I 0x%x MRDSL_T 0x%x \n",
			  update_wqe->max_burst_length,
			  update_wqe->first_burst_length,
			  update_wqe->max_recv_pdu_length,
			  update_wqe->max_send_pdu_length);

	kwqe_arr[0] = (struct kwqe *) update_wqe;
	if (hba->cnic && hba->cnic->submit_kwqes)
		hba->cnic->submit_kwqes(hba->cnic, kwqe_arr, 1);
}


/**
 * bnx2i_ep_ofld_timer - post iSCSI logout request WQE to hardware
 * @data:	endpoint (transport handle) structure pointer
 *
 * routine to handle connection offload/destroy request timeout
 */
void bnx2i_ep_ofld_timer(unsigned long data)
{
	struct bnx2i_endpoint *ep = (struct bnx2i_endpoint *) data;

	if (ep->state == EP_STATE_OFLD_START) {
		printk(KERN_ALERT "ofld_timer: CONN_OFLD timeout\n");
		ep->state = EP_STATE_OFLD_FAILED;
	} else if (ep->state == EP_STATE_DISCONN_START) {
		printk(KERN_ALERT "ofld_timer: CONN_DISCON timeout\n");
		ep->state = EP_STATE_DISCONN_TIMEDOUT;
	} else if (ep->state == EP_STATE_CLEANUP_START) {
		printk(KERN_ALERT "ofld_timer: CONN_CLEANUP timeout\n");
		ep->state = EP_STATE_CLEANUP_FAILED;
	}

	wake_up_interruptible(&ep->ofld_wait);
}


static int bnx2i_power_of2(u32 val)
{
	u32 power = 0;
	if (val & (val - 1))
		return power;
	val--;
	while (val) {
		val = val >> 1;
		power++;
	}
	return power;
}


/**
 * bnx2i_send_cmd_cleanup_req - send iscsi cmd context clean-up request
 * @hba:	adapter structure pointer
 * @cmd:	driver command structure which is requesting
 *		a WQE to sent to chip for further processing
 *
 * prepares and posts CONN_OFLD_REQ1/2 KWQE
 */
void bnx2i_send_cmd_cleanup_req(struct bnx2i_hba *hba, struct bnx2i_cmd *cmd)
{
	struct bnx2i_cleanup_request *cmd_cleanup;

	cmd_cleanup =
		(struct bnx2i_cleanup_request *)cmd->conn->ep->qp.sq_prod_qe;
	memset(cmd_cleanup, 0x00, sizeof(struct bnx2i_cleanup_request));

	cmd_cleanup->op_code = ISCSI_OPCODE_CLEANUP_REQUEST;
	cmd_cleanup->itt = cmd->req.itt;
	cmd_cleanup->cq_index = 0; /* CQ# used for completion, 5771x only */

	bnx2i_ring_dbell_update_sq_params(cmd->conn, 1);
}


/**
 * bnx2i_send_conn_destroy - initiates iscsi connection teardown process
 * @hba:	adapter structure pointer
 * @ep:		endpoint (transport indentifier) structure
 *
 * this routine prepares and posts CONN_OFLD_REQ1/2 KWQE to initiate
 * 	iscsi connection context clean-up process
 */
int bnx2i_send_conn_destroy(struct bnx2i_hba *hba, struct bnx2i_endpoint *ep)
{
	struct kwqe *kwqe_arr[2];
	struct iscsi_kwqe_conn_destroy conn_cleanup;
	int rc = -EINVAL;

	memset(&conn_cleanup, 0x00, sizeof(struct iscsi_kwqe_conn_destroy));

	conn_cleanup.hdr.op_code = ISCSI_KWQE_OPCODE_DESTROY_CONN;
	conn_cleanup.hdr.flags =
		(ISCSI_KWQE_LAYER_CODE << ISCSI_KWQE_HEADER_LAYER_CODE_SHIFT);
	/* 5771x requires conn context id to be passed as is */
	if (test_bit(BNX2I_NX2_DEV_57710, &ep->hba->cnic_dev_type))
		conn_cleanup.context_id = ep->ep_cid;
	else
		conn_cleanup.context_id = (ep->ep_cid >> 7);

	conn_cleanup.reserved0 = (u16)ep->ep_iscsi_cid;

	kwqe_arr[0] = (struct kwqe *) &conn_cleanup;
	if (hba->cnic && hba->cnic->submit_kwqes)
		rc = hba->cnic->submit_kwqes(hba->cnic, kwqe_arr, 1);

	return rc;
}


/**
 * bnx2i_570x_send_conn_ofld_req - initiates iscsi conn context setup process
 * @hba: 		adapter structure pointer
 * @ep: 		endpoint (transport indentifier) structure
 *
 * 5706/5708/5709 specific - prepares and posts CONN_OFLD_REQ1/2 KWQE
 */
static int bnx2i_570x_send_conn_ofld_req(struct bnx2i_hba *hba,
					 struct bnx2i_endpoint *ep)
{
	struct kwqe *kwqe_arr[2];
	struct iscsi_kwqe_conn_offload1 ofld_req1;
	struct iscsi_kwqe_conn_offload2 ofld_req2;
	dma_addr_t dma_addr;
	int num_kwqes = 2;
	u32 *ptbl;
	int rc = -EINVAL;

	ofld_req1.hdr.op_code = ISCSI_KWQE_OPCODE_OFFLOAD_CONN1;
	ofld_req1.hdr.flags =
		(ISCSI_KWQE_LAYER_CODE << ISCSI_KWQE_HEADER_LAYER_CODE_SHIFT);

	ofld_req1.iscsi_conn_id = (u16) ep->ep_iscsi_cid;

	dma_addr = ep->qp.sq_pgtbl_phys;
	ofld_req1.sq_page_table_addr_lo = (u32) dma_addr;
	ofld_req1.sq_page_table_addr_hi = (u32) ((u64) dma_addr >> 32);

	dma_addr = ep->qp.cq_pgtbl_phys;
	ofld_req1.cq_page_table_addr_lo = (u32) dma_addr;
	ofld_req1.cq_page_table_addr_hi = (u32) ((u64) dma_addr >> 32);

	ofld_req2.hdr.op_code = ISCSI_KWQE_OPCODE_OFFLOAD_CONN2;
	ofld_req2.hdr.flags =
		(ISCSI_KWQE_LAYER_CODE << ISCSI_KWQE_HEADER_LAYER_CODE_SHIFT);

	dma_addr = ep->qp.rq_pgtbl_phys;
	ofld_req2.rq_page_table_addr_lo = (u32) dma_addr;
	ofld_req2.rq_page_table_addr_hi = (u32) ((u64) dma_addr >> 32);

	ptbl = (u32 *) ep->qp.sq_pgtbl_virt;

	ofld_req2.sq_first_pte.hi = *ptbl++;
	ofld_req2.sq_first_pte.lo = *ptbl;

	ptbl = (u32 *) ep->qp.cq_pgtbl_virt;
	ofld_req2.cq_first_pte.hi = *ptbl++;
	ofld_req2.cq_first_pte.lo = *ptbl;

	kwqe_arr[0] = (struct kwqe *) &ofld_req1;
	kwqe_arr[1] = (struct kwqe *) &ofld_req2;
	ofld_req2.num_additional_wqes = 0;

	if (hba->cnic && hba->cnic->submit_kwqes)
		rc = hba->cnic->submit_kwqes(hba->cnic, kwqe_arr, num_kwqes);

	return rc;
}


/**
 * bnx2i_5771x_send_conn_ofld_req - initiates iscsi connection context creation
 * @hba: 		adapter structure pointer
 * @ep: 		endpoint (transport indentifier) structure
 *
 * 57710 specific - prepares and posts CONN_OFLD_REQ1/2 KWQE
 */
static int bnx2i_5771x_send_conn_ofld_req(struct bnx2i_hba *hba,
					  struct bnx2i_endpoint *ep)
{
	struct kwqe *kwqe_arr[5];
	struct iscsi_kwqe_conn_offload1 ofld_req1;
	struct iscsi_kwqe_conn_offload2 ofld_req2;
	struct iscsi_kwqe_conn_offload3 ofld_req3[1];
	dma_addr_t dma_addr;
	int num_kwqes = 2;
	u32 *ptbl;
	int rc = -EINVAL;

	ofld_req1.hdr.op_code = ISCSI_KWQE_OPCODE_OFFLOAD_CONN1;
	ofld_req1.hdr.flags =
		(ISCSI_KWQE_LAYER_CODE << ISCSI_KWQE_HEADER_LAYER_CODE_SHIFT);

	ofld_req1.iscsi_conn_id = (u16) ep->ep_iscsi_cid;

	dma_addr = ep->qp.sq_pgtbl_phys + ISCSI_SQ_DB_SIZE;
	ofld_req1.sq_page_table_addr_lo = (u32) dma_addr;
	ofld_req1.sq_page_table_addr_hi = (u32) ((u64) dma_addr >> 32);

	dma_addr = ep->qp.cq_pgtbl_phys + ISCSI_CQ_DB_SIZE;
	ofld_req1.cq_page_table_addr_lo = (u32) dma_addr;
	ofld_req1.cq_page_table_addr_hi = (u32) ((u64) dma_addr >> 32);

	ofld_req2.hdr.op_code = ISCSI_KWQE_OPCODE_OFFLOAD_CONN2;
	ofld_req2.hdr.flags =
		(ISCSI_KWQE_LAYER_CODE << ISCSI_KWQE_HEADER_LAYER_CODE_SHIFT);

	dma_addr = ep->qp.rq_pgtbl_phys + ISCSI_RQ_DB_SIZE;
	ofld_req2.rq_page_table_addr_lo = (u32) dma_addr;
	ofld_req2.rq_page_table_addr_hi = (u32) ((u64) dma_addr >> 32);

	ptbl = (u32 *)((u8 *)ep->qp.sq_pgtbl_virt + ISCSI_SQ_DB_SIZE);
	ofld_req2.sq_first_pte.hi = *ptbl++;
	ofld_req2.sq_first_pte.lo = *ptbl;

	ptbl = (u32 *)((u8 *)ep->qp.cq_pgtbl_virt + ISCSI_CQ_DB_SIZE);
	ofld_req2.cq_first_pte.hi = *ptbl++;
	ofld_req2.cq_first_pte.lo = *ptbl;

	kwqe_arr[0] = (struct kwqe *) &ofld_req1;
	kwqe_arr[1] = (struct kwqe *) &ofld_req2;

	ofld_req2.num_additional_wqes = 1;
	memset(ofld_req3, 0x00, sizeof(ofld_req3[0]));
	ptbl = (u32 *)((u8 *)ep->qp.rq_pgtbl_virt + ISCSI_RQ_DB_SIZE);
	ofld_req3[0].qp_first_pte[0].hi = *ptbl++;
	ofld_req3[0].qp_first_pte[0].lo = *ptbl;

	kwqe_arr[2] = (struct kwqe *) ofld_req3;
	/* need if we decide to go with multiple KCQE's per conn */
	num_kwqes += 1;

	if (hba->cnic && hba->cnic->submit_kwqes)
		rc = hba->cnic->submit_kwqes(hba->cnic, kwqe_arr, num_kwqes);

	return rc;
}

/**
 * bnx2i_send_conn_ofld_req - initiates iscsi connection context setup process
 *
 * @hba: 		adapter structure pointer
 * @ep: 		endpoint (transport indentifier) structure
 *
 * this routine prepares and posts CONN_OFLD_REQ1/2 KWQE
 */
int bnx2i_send_conn_ofld_req(struct bnx2i_hba *hba, struct bnx2i_endpoint *ep)
{
	int rc;

	if (test_bit(BNX2I_NX2_DEV_57710, &hba->cnic_dev_type))
		rc = bnx2i_5771x_send_conn_ofld_req(hba, ep);
	else
		rc = bnx2i_570x_send_conn_ofld_req(hba, ep);

	return rc;
}


/**
 * setup_qp_page_tables - iscsi QP page table setup function
 * @ep:		endpoint (transport indentifier) structure
 *
 * Sets up page tables for SQ/RQ/CQ, 1G/sec (5706/5708/5709) devices requires
 * 	64-bit address in big endian format. Whereas 10G/sec (57710) requires
 * 	PT in little endian format
 */
static void setup_qp_page_tables(struct bnx2i_endpoint *ep)
{
	int num_pages;
	u32 *ptbl;
	dma_addr_t page;
	int cnic_dev_10g;

	if (test_bit(BNX2I_NX2_DEV_57710, &ep->hba->cnic_dev_type))
		cnic_dev_10g = 1;
	else
		cnic_dev_10g = 0;

	/* SQ page table */
	memset(ep->qp.sq_pgtbl_virt, 0, ep->qp.sq_pgtbl_size);
	num_pages = ep->qp.sq_mem_size / PAGE_SIZE;
	page = ep->qp.sq_phys;

	if (cnic_dev_10g)
		ptbl = (u32 *)((u8 *)ep->qp.sq_pgtbl_virt + ISCSI_SQ_DB_SIZE);
	else
		ptbl = (u32 *) ep->qp.sq_pgtbl_virt;
	while (num_pages--) {
		if (cnic_dev_10g) {
			/* PTE is written in little endian format for 57710 */
			*ptbl = (u32) page;
			ptbl++;
			*ptbl = (u32) ((u64) page >> 32);
			ptbl++;
			page += PAGE_SIZE;
		} else {
			/* PTE is written in big endian format for
			 * 5706/5708/5709 devices */
			*ptbl = (u32) ((u64) page >> 32);
			ptbl++;
			*ptbl = (u32) page;
			ptbl++;
			page += PAGE_SIZE;
		}
	}

	/* RQ page table */
	memset(ep->qp.rq_pgtbl_virt, 0, ep->qp.rq_pgtbl_size);
	num_pages = ep->qp.rq_mem_size / PAGE_SIZE;
	page = ep->qp.rq_phys;

	if (cnic_dev_10g)
		ptbl = (u32 *)((u8 *)ep->qp.rq_pgtbl_virt + ISCSI_RQ_DB_SIZE);
	else
		ptbl = (u32 *) ep->qp.rq_pgtbl_virt;
	while (num_pages--) {
		if (cnic_dev_10g) {
			/* PTE is written in little endian format for 57710 */
			*ptbl = (u32) page;
			ptbl++;
			*ptbl = (u32) ((u64) page >> 32);
			ptbl++;
			page += PAGE_SIZE;
		} else {
			/* PTE is written in big endian format for
			 * 5706/5708/5709 devices */
			*ptbl = (u32) ((u64) page >> 32);
			ptbl++;
			*ptbl = (u32) page;
			ptbl++;
			page += PAGE_SIZE;
		}
	}

	/* CQ page table */
	memset(ep->qp.cq_pgtbl_virt, 0, ep->qp.cq_pgtbl_size);
	num_pages = ep->qp.cq_mem_size / PAGE_SIZE;
	page = ep->qp.cq_phys;

	if (cnic_dev_10g)
		ptbl = (u32 *)((u8 *)ep->qp.cq_pgtbl_virt + ISCSI_CQ_DB_SIZE);
	else
		ptbl = (u32 *) ep->qp.cq_pgtbl_virt;
	while (num_pages--) {
		if (cnic_dev_10g) {
			/* PTE is written in little endian format for 57710 */
			*ptbl = (u32) page;
			ptbl++;
			*ptbl = (u32) ((u64) page >> 32);
			ptbl++;
			page += PAGE_SIZE;
		} else {
			/* PTE is written in big endian format for
			 * 5706/5708/5709 devices */
			*ptbl = (u32) ((u64) page >> 32);
			ptbl++;
			*ptbl = (u32) page;
			ptbl++;
			page += PAGE_SIZE;
		}
	}
}


/**
 * bnx2i_alloc_qp_resc - allocates required resources for QP.
 * @hba:	adapter structure pointer
 * @ep:		endpoint (transport indentifier) structure
 *
 * Allocate QP (transport layer for iSCSI connection) resources, DMA'able
 *	memory for SQ/RQ/CQ and page tables. EP structure elements such
 *	as producer/consumer indexes/pointers, queue sizes and page table
 *	contents are setup
 */
int bnx2i_alloc_qp_resc(struct bnx2i_hba *hba, struct bnx2i_endpoint *ep)
{
	struct bnx2i_5771x_cq_db *cq_db;

	ep->hba = hba;
	ep->conn = NULL;
	ep->ep_cid = ep->ep_iscsi_cid = ep->ep_pg_cid = 0;

	/* Allocate page table memory for SQ which is page aligned */
	ep->qp.sq_mem_size = hba->max_sqes * BNX2I_SQ_WQE_SIZE;
	ep->qp.sq_mem_size =
		(ep->qp.sq_mem_size + (PAGE_SIZE - 1)) & PAGE_MASK;
	ep->qp.sq_pgtbl_size =
		(ep->qp.sq_mem_size / PAGE_SIZE) * sizeof(void *);
	ep->qp.sq_pgtbl_size =
		(ep->qp.sq_pgtbl_size + (PAGE_SIZE - 1)) & PAGE_MASK;

	ep->qp.sq_pgtbl_virt =
		dma_alloc_coherent(&hba->pcidev->dev, ep->qp.sq_pgtbl_size,
				   &ep->qp.sq_pgtbl_phys, GFP_KERNEL);
	if (!ep->qp.sq_pgtbl_virt) {
		printk(KERN_ALERT "bnx2i: unable to alloc SQ PT mem (%d)\n",
				  ep->qp.sq_pgtbl_size);
		goto mem_alloc_err;
	}

	/* Allocate memory area for actual SQ element */
	ep->qp.sq_virt =
		dma_alloc_coherent(&hba->pcidev->dev, ep->qp.sq_mem_size,
				   &ep->qp.sq_phys, GFP_KERNEL);
	if (!ep->qp.sq_virt) {
		printk(KERN_ALERT "bnx2i: unable to alloc SQ BD memory %d\n",
				  ep->qp.sq_mem_size);
		goto mem_alloc_err;
	}

	memset(ep->qp.sq_virt, 0x00, ep->qp.sq_mem_size);
	ep->qp.sq_first_qe = ep->qp.sq_virt;
	ep->qp.sq_prod_qe = ep->qp.sq_first_qe;
	ep->qp.sq_cons_qe = ep->qp.sq_first_qe;
	ep->qp.sq_last_qe = &ep->qp.sq_first_qe[hba->max_sqes - 1];
	ep->qp.sq_prod_idx = 0;
	ep->qp.sq_cons_idx = 0;
	ep->qp.sqe_left = hba->max_sqes;

	/* Allocate page table memory for CQ which is page aligned */
	ep->qp.cq_mem_size = hba->max_cqes * BNX2I_CQE_SIZE;
	ep->qp.cq_mem_size =
		(ep->qp.cq_mem_size + (PAGE_SIZE - 1)) & PAGE_MASK;
	ep->qp.cq_pgtbl_size =
		(ep->qp.cq_mem_size / PAGE_SIZE) * sizeof(void *);
	ep->qp.cq_pgtbl_size =
		(ep->qp.cq_pgtbl_size + (PAGE_SIZE - 1)) & PAGE_MASK;

	ep->qp.cq_pgtbl_virt =
		dma_alloc_coherent(&hba->pcidev->dev, ep->qp.cq_pgtbl_size,
				   &ep->qp.cq_pgtbl_phys, GFP_KERNEL);
	if (!ep->qp.cq_pgtbl_virt) {
		printk(KERN_ALERT "bnx2i: unable to alloc CQ PT memory %d\n",
				  ep->qp.cq_pgtbl_size);
		goto mem_alloc_err;
	}

	/* Allocate memory area for actual CQ element */
	ep->qp.cq_virt =
		dma_alloc_coherent(&hba->pcidev->dev, ep->qp.cq_mem_size,
				   &ep->qp.cq_phys, GFP_KERNEL);
	if (!ep->qp.cq_virt) {
		printk(KERN_ALERT "bnx2i: unable to alloc CQ BD memory %d\n",
				  ep->qp.cq_mem_size);
		goto mem_alloc_err;
	}
	memset(ep->qp.cq_virt, 0x00, ep->qp.cq_mem_size);

	ep->qp.cq_first_qe = ep->qp.cq_virt;
	ep->qp.cq_prod_qe = ep->qp.cq_first_qe;
	ep->qp.cq_cons_qe = ep->qp.cq_first_qe;
	ep->qp.cq_last_qe = &ep->qp.cq_first_qe[hba->max_cqes - 1];
	ep->qp.cq_prod_idx = 0;
	ep->qp.cq_cons_idx = 0;
	ep->qp.cqe_left = hba->max_cqes;
	ep->qp.cqe_exp_seq_sn = ISCSI_INITIAL_SN;
	ep->qp.cqe_size = hba->max_cqes;

	/* Invalidate all EQ CQE index, req only for 57710 */
	cq_db = (struct bnx2i_5771x_cq_db *) ep->qp.cq_pgtbl_virt;
	memset(cq_db->sqn, 0xFF, sizeof(cq_db->sqn[0]) * BNX2X_MAX_CQS);

	/* Allocate page table memory for RQ which is page aligned */
	ep->qp.rq_mem_size = hba->max_rqes * BNX2I_RQ_WQE_SIZE;
	ep->qp.rq_mem_size =
		(ep->qp.rq_mem_size + (PAGE_SIZE - 1)) & PAGE_MASK;
	ep->qp.rq_pgtbl_size =
		(ep->qp.rq_mem_size / PAGE_SIZE) * sizeof(void *);
	ep->qp.rq_pgtbl_size =
		(ep->qp.rq_pgtbl_size + (PAGE_SIZE - 1)) & PAGE_MASK;

	ep->qp.rq_pgtbl_virt =
		dma_alloc_coherent(&hba->pcidev->dev, ep->qp.rq_pgtbl_size,
				   &ep->qp.rq_pgtbl_phys, GFP_KERNEL);
	if (!ep->qp.rq_pgtbl_virt) {
		printk(KERN_ALERT "bnx2i: unable to alloc RQ PT mem %d\n",
				  ep->qp.rq_pgtbl_size);
		goto mem_alloc_err;
	}

	/* Allocate memory area for actual RQ element */
	ep->qp.rq_virt =
		dma_alloc_coherent(&hba->pcidev->dev, ep->qp.rq_mem_size,
				   &ep->qp.rq_phys, GFP_KERNEL);
	if (!ep->qp.rq_virt) {
		printk(KERN_ALERT "bnx2i: unable to alloc RQ BD memory %d\n",
				  ep->qp.rq_mem_size);
		goto mem_alloc_err;
	}

	ep->qp.rq_first_qe = ep->qp.rq_virt;
	ep->qp.rq_prod_qe = ep->qp.rq_first_qe;
	ep->qp.rq_cons_qe = ep->qp.rq_first_qe;
	ep->qp.rq_last_qe = &ep->qp.rq_first_qe[hba->max_rqes - 1];
	ep->qp.rq_prod_idx = 0x8000;
	ep->qp.rq_cons_idx = 0;
	ep->qp.rqe_left = hba->max_rqes;

	setup_qp_page_tables(ep);

	return 0;

mem_alloc_err:
	bnx2i_free_qp_resc(hba, ep);
	return -ENOMEM;
}



/**
 * bnx2i_free_qp_resc - free memory resources held by QP
 * @hba:	adapter structure pointer
 * @ep:	endpoint (transport indentifier) structure
 *
 * Free QP resources - SQ/RQ/CQ memory and page tables.
 */
void bnx2i_free_qp_resc(struct bnx2i_hba *hba, struct bnx2i_endpoint *ep)
{
	if (ep->qp.ctx_base) {
		iounmap(ep->qp.ctx_base);
		ep->qp.ctx_base = NULL;
	}
	/* Free SQ mem */
	if (ep->qp.sq_pgtbl_virt) {
		dma_free_coherent(&hba->pcidev->dev, ep->qp.sq_pgtbl_size,
				  ep->qp.sq_pgtbl_virt, ep->qp.sq_pgtbl_phys);
		ep->qp.sq_pgtbl_virt = NULL;
		ep->qp.sq_pgtbl_phys = 0;
	}
	if (ep->qp.sq_virt) {
		dma_free_coherent(&hba->pcidev->dev, ep->qp.sq_mem_size,
				  ep->qp.sq_virt, ep->qp.sq_phys);
		ep->qp.sq_virt = NULL;
		ep->qp.sq_phys = 0;
	}

	/* Free RQ mem */
	if (ep->qp.rq_pgtbl_virt) {
		dma_free_coherent(&hba->pcidev->dev, ep->qp.rq_pgtbl_size,
				  ep->qp.rq_pgtbl_virt, ep->qp.rq_pgtbl_phys);
		ep->qp.rq_pgtbl_virt = NULL;
		ep->qp.rq_pgtbl_phys = 0;
	}
	if (ep->qp.rq_virt) {
		dma_free_coherent(&hba->pcidev->dev, ep->qp.rq_mem_size,
				  ep->qp.rq_virt, ep->qp.rq_phys);
		ep->qp.rq_virt = NULL;
		ep->qp.rq_phys = 0;
	}

	/* Free CQ mem */
	if (ep->qp.cq_pgtbl_virt) {
		dma_free_coherent(&hba->pcidev->dev, ep->qp.cq_pgtbl_size,
				  ep->qp.cq_pgtbl_virt, ep->qp.cq_pgtbl_phys);
		ep->qp.cq_pgtbl_virt = NULL;
		ep->qp.cq_pgtbl_phys = 0;
	}
	if (ep->qp.cq_virt) {
		dma_free_coherent(&hba->pcidev->dev, ep->qp.cq_mem_size,
				  ep->qp.cq_virt, ep->qp.cq_phys);
		ep->qp.cq_virt = NULL;
		ep->qp.cq_phys = 0;
	}
}


/**
 * bnx2i_send_fw_iscsi_init_msg - initiates initial handshake with iscsi f/w
 * @hba:	adapter structure pointer
 *
 * Send down iscsi_init KWQEs which initiates the initial handshake with the f/w
 * 	This results in iSCSi support validation and on-chip context manager
 * 	initialization.  Firmware completes this handshake with a CQE carrying
 * 	the result of iscsi support validation. Parameter carried by
 * 	iscsi init request determines the number of offloaded connection and
 * 	tolerance level for iscsi protocol violation this hba/chip can support
 */
int bnx2i_send_fw_iscsi_init_msg(struct bnx2i_hba *hba)
{
	struct kwqe *kwqe_arr[3];
	struct iscsi_kwqe_init1 iscsi_init;
	struct iscsi_kwqe_init2 iscsi_init2;
	int rc = 0;
	u64 mask64;

	bnx2i_adjust_qp_size(hba);

	iscsi_init.flags =
		ISCSI_PAGE_SIZE_4K << ISCSI_KWQE_INIT1_PAGE_SIZE_SHIFT;
	if (en_tcp_dack)
		iscsi_init.flags |= ISCSI_KWQE_INIT1_DELAYED_ACK_ENABLE;
	iscsi_init.reserved0 = 0;
	iscsi_init.num_cqs = 1;
	iscsi_init.hdr.op_code = ISCSI_KWQE_OPCODE_INIT1;
	iscsi_init.hdr.flags =
		(ISCSI_KWQE_LAYER_CODE << ISCSI_KWQE_HEADER_LAYER_CODE_SHIFT);

	iscsi_init.dummy_buffer_addr_lo = (u32) hba->dummy_buf_dma;
	iscsi_init.dummy_buffer_addr_hi =
		(u32) ((u64) hba->dummy_buf_dma >> 32);

	hba->num_ccell = hba->max_sqes >> 1;
	hba->ctx_ccell_tasks =
			((hba->num_ccell & 0xFFFF) | (hba->max_sqes << 16));
	iscsi_init.num_ccells_per_conn = hba->num_ccell;
	iscsi_init.num_tasks_per_conn = hba->max_sqes;
	iscsi_init.sq_wqes_per_page = PAGE_SIZE / BNX2I_SQ_WQE_SIZE;
	iscsi_init.sq_num_wqes = hba->max_sqes;
	iscsi_init.cq_log_wqes_per_page =
		(u8) bnx2i_power_of2(PAGE_SIZE / BNX2I_CQE_SIZE);
	iscsi_init.cq_num_wqes = hba->max_cqes;
	iscsi_init.cq_num_pages = (hba->max_cqes * BNX2I_CQE_SIZE +
				   (PAGE_SIZE - 1)) / PAGE_SIZE;
	iscsi_init.sq_num_pages = (hba->max_sqes * BNX2I_SQ_WQE_SIZE +
				   (PAGE_SIZE - 1)) / PAGE_SIZE;
	iscsi_init.rq_buffer_size = BNX2I_RQ_WQE_SIZE;
	iscsi_init.rq_num_wqes = hba->max_rqes;


	iscsi_init2.hdr.op_code = ISCSI_KWQE_OPCODE_INIT2;
	iscsi_init2.hdr.flags =
		(ISCSI_KWQE_LAYER_CODE << ISCSI_KWQE_HEADER_LAYER_CODE_SHIFT);
	iscsi_init2.max_cq_sqn = hba->max_cqes * 2 + 1;
	mask64 = 0x0ULL;
	mask64 |= (
		/* CISCO MDS */
		(1UL <<
		  ISCSI_KCQE_COMPLETION_STATUS_PROTOCOL_ERR_TTT_NOT_RSRV) |
		/* HP MSA1510i */
		(1UL <<
		  ISCSI_KCQE_COMPLETION_STATUS_PROTOCOL_ERR_EXP_DATASN) |
		/* EMC */
		(1ULL << ISCSI_KCQE_COMPLETION_STATUS_PROTOCOL_ERR_LUN));
	if (error_mask1) {
		iscsi_init2.error_bit_map[0] = error_mask1;
		mask64 &= (u32)(~mask64);
		mask64 |= error_mask1;
	} else
		iscsi_init2.error_bit_map[0] = (u32) mask64;

	if (error_mask2) {
		iscsi_init2.error_bit_map[1] = error_mask2;
		mask64 &= 0xffffffff;
		mask64 |= ((u64)error_mask2 << 32);
	} else
		iscsi_init2.error_bit_map[1] = (u32) (mask64 >> 32);

	iscsi_error_mask = mask64;

	kwqe_arr[0] = (struct kwqe *) &iscsi_init;
	kwqe_arr[1] = (struct kwqe *) &iscsi_init2;

	if (hba->cnic && hba->cnic->submit_kwqes)
		rc = hba->cnic->submit_kwqes(hba->cnic, kwqe_arr, 2);
	return rc;
}


/**
 * bnx2i_process_scsi_cmd_resp - this function handles scsi cmd completion.
 * @session:	iscsi session
 * @bnx2i_conn:	bnx2i connection
 * @cqe:	pointer to newly DMA'ed CQE entry for processing
 *
 * process SCSI CMD Response CQE & complete the request to SCSI-ML
 */
int bnx2i_process_scsi_cmd_resp(struct iscsi_session *session,
				struct bnx2i_conn *bnx2i_conn,
				struct cqe *cqe)
{
	struct iscsi_conn *conn = bnx2i_conn->cls_conn->dd_data;
	struct bnx2i_cmd_response *resp_cqe;
	struct bnx2i_cmd *bnx2i_cmd;
	struct iscsi_task *task;
	struct iscsi_scsi_rsp *hdr;
	u32 datalen = 0;

	resp_cqe = (struct bnx2i_cmd_response *)cqe;
	spin_lock_bh(&session->lock);
	task = iscsi_itt_to_task(conn,
				 resp_cqe->itt & ISCSI_CMD_RESPONSE_INDEX);
	if (!task)
		goto fail;

	bnx2i_cmd = task->dd_data;

	if (bnx2i_cmd->req.op_attr & ISCSI_CMD_REQUEST_READ) {
		conn->datain_pdus_cnt +=
			resp_cqe->task_stat.read_stat.num_data_outs;
		conn->rxdata_octets +=
			bnx2i_cmd->req.total_data_transfer_length;
	} else {
		conn->dataout_pdus_cnt +=
			resp_cqe->task_stat.read_stat.num_data_outs;
		conn->r2t_pdus_cnt +=
			resp_cqe->task_stat.read_stat.num_r2ts;
		conn->txdata_octets +=
			bnx2i_cmd->req.total_data_transfer_length;
	}
	bnx2i_iscsi_unmap_sg_list(bnx2i_cmd);

	hdr = (struct iscsi_scsi_rsp *)task->hdr;
	resp_cqe = (struct bnx2i_cmd_response *)cqe;
	hdr->opcode = resp_cqe->op_code;
	hdr->max_cmdsn = cpu_to_be32(resp_cqe->max_cmd_sn);
	hdr->exp_cmdsn = cpu_to_be32(resp_cqe->exp_cmd_sn);
	hdr->response = resp_cqe->response;
	hdr->cmd_status = resp_cqe->status;
	hdr->flags = resp_cqe->response_flags;
	hdr->residual_count = cpu_to_be32(resp_cqe->residual_count);

	if (resp_cqe->op_code == ISCSI_OP_SCSI_DATA_IN)
		goto done;

	if (resp_cqe->status == SAM_STAT_CHECK_CONDITION) {
		datalen = resp_cqe->data_length;
		if (datalen < 2)
			goto done;

		if (datalen > BNX2I_RQ_WQE_SIZE) {
			iscsi_conn_printk(KERN_ERR, conn,
					  "sense data len %d > RQ sz\n",
					  datalen);
			datalen = BNX2I_RQ_WQE_SIZE;
		} else if (datalen > ISCSI_DEF_MAX_RECV_SEG_LEN) {
			iscsi_conn_printk(KERN_ERR, conn,
					  "sense data len %d > conn data\n",
					  datalen);
			datalen = ISCSI_DEF_MAX_RECV_SEG_LEN;
		}

		bnx2i_get_rq_buf(bnx2i_cmd->conn, conn->data, datalen);
		bnx2i_put_rq_buf(bnx2i_cmd->conn, 1);
	}

done:
	__iscsi_complete_pdu(conn, (struct iscsi_hdr *)hdr,
			     conn->data, datalen);
fail:
	spin_unlock_bh(&session->lock);
	return 0;
}


/**
 * bnx2i_process_login_resp - this function handles iscsi login response
 * @session:		iscsi session pointer
 * @bnx2i_conn:		iscsi connection pointer
 * @cqe:		pointer to newly DMA'ed CQE entry for processing
 *
 * process Login Response CQE & complete it to open-iscsi user daemon
 */
static int bnx2i_process_login_resp(struct iscsi_session *session,
				    struct bnx2i_conn *bnx2i_conn,
				    struct cqe *cqe)
{
	struct iscsi_conn *conn = bnx2i_conn->cls_conn->dd_data;
	struct iscsi_task *task;
	struct bnx2i_login_response *login;
	struct iscsi_login_rsp *resp_hdr;
	int pld_len;
	int pad_len;

	login = (struct bnx2i_login_response *) cqe;
	spin_lock(&session->lock);
	task = iscsi_itt_to_task(conn,
				 login->itt & ISCSI_LOGIN_RESPONSE_INDEX);
	if (!task)
		goto done;

	resp_hdr = (struct iscsi_login_rsp *) &bnx2i_conn->gen_pdu.resp_hdr;
	memset(resp_hdr, 0, sizeof(struct iscsi_hdr));
	resp_hdr->opcode = login->op_code;
	resp_hdr->flags = login->response_flags;
	resp_hdr->max_version = login->version_max;
	resp_hdr->active_version = login->version_active;
	resp_hdr->hlength = 0;

	hton24(resp_hdr->dlength, login->data_length);
	memcpy(resp_hdr->isid, &login->isid_lo, 6);
	resp_hdr->tsih = cpu_to_be16(login->tsih);
	resp_hdr->itt = task->hdr->itt;
	resp_hdr->statsn = cpu_to_be32(login->stat_sn);
	resp_hdr->exp_cmdsn = cpu_to_be32(login->exp_cmd_sn);
	resp_hdr->max_cmdsn = cpu_to_be32(login->max_cmd_sn);
	resp_hdr->status_class = login->status_class;
	resp_hdr->status_detail = login->status_detail;
	pld_len = login->data_length;
	bnx2i_conn->gen_pdu.resp_wr_ptr =
					bnx2i_conn->gen_pdu.resp_buf + pld_len;

	pad_len = 0;
	if (pld_len & 0x3)
		pad_len = 4 - (pld_len % 4);

	if (pad_len) {
		int i = 0;
		for (i = 0; i < pad_len; i++) {
			bnx2i_conn->gen_pdu.resp_wr_ptr[0] = 0;
			bnx2i_conn->gen_pdu.resp_wr_ptr++;
		}
	}

	__iscsi_complete_pdu(conn, (struct iscsi_hdr *)resp_hdr,
		bnx2i_conn->gen_pdu.resp_buf,
		bnx2i_conn->gen_pdu.resp_wr_ptr - bnx2i_conn->gen_pdu.resp_buf);
done:
	spin_unlock(&session->lock);
	return 0;
}


/**
 * bnx2i_process_text_resp - this function handles iscsi text response
 * @session:	iscsi session pointer
 * @bnx2i_conn:	iscsi connection pointer
 * @cqe:	pointer to newly DMA'ed CQE entry for processing
 *
 * process iSCSI Text Response CQE&  complete it to open-iscsi user daemon
 */
static int bnx2i_process_text_resp(struct iscsi_session *session,
				   struct bnx2i_conn *bnx2i_conn,
				   struct cqe *cqe)
{
	struct iscsi_conn *conn = bnx2i_conn->cls_conn->dd_data;
	struct iscsi_task *task;
	struct bnx2i_text_response *text;
	struct iscsi_text_rsp *resp_hdr;
	int pld_len;
	int pad_len;

	text = (struct bnx2i_text_response *) cqe;
	spin_lock(&session->lock);
	task = iscsi_itt_to_task(conn, text->itt & ISCSI_LOGIN_RESPONSE_INDEX);
	if (!task)
		goto done;

	resp_hdr = (struct iscsi_text_rsp *)&bnx2i_conn->gen_pdu.resp_hdr;
	memset(resp_hdr, 0, sizeof(struct iscsi_hdr));
	resp_hdr->opcode = text->op_code;
	resp_hdr->flags = text->response_flags;
	resp_hdr->hlength = 0;

	hton24(resp_hdr->dlength, text->data_length);
	resp_hdr->itt = task->hdr->itt;
	resp_hdr->ttt = cpu_to_be32(text->ttt);
	resp_hdr->statsn = task->hdr->exp_statsn;
	resp_hdr->exp_cmdsn = cpu_to_be32(text->exp_cmd_sn);
	resp_hdr->max_cmdsn = cpu_to_be32(text->max_cmd_sn);
	pld_len = text->data_length;
	bnx2i_conn->gen_pdu.resp_wr_ptr = bnx2i_conn->gen_pdu.resp_buf +
					  pld_len;
	pad_len = 0;
	if (pld_len & 0x3)
		pad_len = 4 - (pld_len % 4);

	if (pad_len) {
		int i = 0;
		for (i = 0; i < pad_len; i++) {
			bnx2i_conn->gen_pdu.resp_wr_ptr[0] = 0;
			bnx2i_conn->gen_pdu.resp_wr_ptr++;
		}
	}
	__iscsi_complete_pdu(conn, (struct iscsi_hdr *)resp_hdr,
			     bnx2i_conn->gen_pdu.resp_buf,
			     bnx2i_conn->gen_pdu.resp_wr_ptr -
			     bnx2i_conn->gen_pdu.resp_buf);
done:
	spin_unlock(&session->lock);
	return 0;
}


/**
 * bnx2i_process_tmf_resp - this function handles iscsi TMF response
 * @session:		iscsi session pointer
 * @bnx2i_conn:		iscsi connection pointer
 * @cqe:		pointer to newly DMA'ed CQE entry for processing
 *
 * process iSCSI TMF Response CQE and wake up the driver eh thread.
 */
static int bnx2i_process_tmf_resp(struct iscsi_session *session,
				  struct bnx2i_conn *bnx2i_conn,
				  struct cqe *cqe)
{
	struct iscsi_conn *conn = bnx2i_conn->cls_conn->dd_data;
	struct iscsi_task *task;
	struct bnx2i_tmf_response *tmf_cqe;
	struct iscsi_tm_rsp *resp_hdr;

	tmf_cqe = (struct bnx2i_tmf_response *)cqe;
	spin_lock(&session->lock);
	task = iscsi_itt_to_task(conn,
				 tmf_cqe->itt & ISCSI_TMF_RESPONSE_INDEX);
	if (!task)
		goto done;

	resp_hdr = (struct iscsi_tm_rsp *) &bnx2i_conn->gen_pdu.resp_hdr;
	memset(resp_hdr, 0, sizeof(struct iscsi_hdr));
	resp_hdr->opcode = tmf_cqe->op_code;
	resp_hdr->max_cmdsn = cpu_to_be32(tmf_cqe->max_cmd_sn);
	resp_hdr->exp_cmdsn = cpu_to_be32(tmf_cqe->exp_cmd_sn);
	resp_hdr->itt = task->hdr->itt;
	resp_hdr->response = tmf_cqe->response;

	__iscsi_complete_pdu(conn, (struct iscsi_hdr *)resp_hdr, NULL, 0);
done:
	spin_unlock(&session->lock);
	return 0;
}

/**
 * bnx2i_process_logout_resp - this function handles iscsi logout response
 * @session:		iscsi session pointer
 * @bnx2i_conn:		iscsi connection pointer
 * @cqe:		pointer to newly DMA'ed CQE entry for processing
 *
 * process iSCSI Logout Response CQE & make function call to
 * notify the user daemon.
 */
static int bnx2i_process_logout_resp(struct iscsi_session *session,
				     struct bnx2i_conn *bnx2i_conn,
				     struct cqe *cqe)
{
	struct iscsi_conn *conn = bnx2i_conn->cls_conn->dd_data;
	struct iscsi_task *task;
	struct bnx2i_logout_response *logout;
	struct iscsi_logout_rsp *resp_hdr;

	logout = (struct bnx2i_logout_response *) cqe;
	spin_lock(&session->lock);
	task = iscsi_itt_to_task(conn,
				 logout->itt & ISCSI_LOGOUT_RESPONSE_INDEX);
	if (!task)
		goto done;

	resp_hdr = (struct iscsi_logout_rsp *) &bnx2i_conn->gen_pdu.resp_hdr;
	memset(resp_hdr, 0, sizeof(struct iscsi_hdr));
	resp_hdr->opcode = logout->op_code;
	resp_hdr->flags = logout->response;
	resp_hdr->hlength = 0;

	resp_hdr->itt = task->hdr->itt;
	resp_hdr->statsn = task->hdr->exp_statsn;
	resp_hdr->exp_cmdsn = cpu_to_be32(logout->exp_cmd_sn);
	resp_hdr->max_cmdsn = cpu_to_be32(logout->max_cmd_sn);

	resp_hdr->t2wait = cpu_to_be32(logout->time_to_wait);
	resp_hdr->t2retain = cpu_to_be32(logout->time_to_retain);

	__iscsi_complete_pdu(conn, (struct iscsi_hdr *)resp_hdr, NULL, 0);

	bnx2i_conn->ep->state = EP_STATE_LOGOUT_RESP_RCVD;
done:
	spin_unlock(&session->lock);
	return 0;
}

/**
 * bnx2i_process_nopin_local_cmpl - this function handles iscsi nopin CQE
 * @session:		iscsi session pointer
 * @bnx2i_conn:		iscsi connection pointer
 * @cqe:		pointer to newly DMA'ed CQE entry for processing
 *
 * process iSCSI NOPIN local completion CQE, frees IIT and command structures
 */
static void bnx2i_process_nopin_local_cmpl(struct iscsi_session *session,
					   struct bnx2i_conn *bnx2i_conn,
					   struct cqe *cqe)
{
	struct iscsi_conn *conn = bnx2i_conn->cls_conn->dd_data;
	struct bnx2i_nop_in_msg *nop_in;
	struct iscsi_task *task;

	nop_in = (struct bnx2i_nop_in_msg *)cqe;
	spin_lock(&session->lock);
	task = iscsi_itt_to_task(conn,
				 nop_in->itt & ISCSI_NOP_IN_MSG_INDEX);
	if (task)
		__iscsi_put_task(task);
	spin_unlock(&session->lock);
}

/**
 * bnx2i_unsol_pdu_adjust_rq - makes adjustments to RQ after unsol pdu is recvd
 * @conn:	iscsi connection
 *
 * Firmware advances RQ producer index for every unsolicited PDU even if
 *	payload data length is '0'. This function makes corresponding
 *	adjustments on the driver side to match this f/w behavior
 */
static void bnx2i_unsol_pdu_adjust_rq(struct bnx2i_conn *bnx2i_conn)
{
	char dummy_rq_data[2];
	bnx2i_get_rq_buf(bnx2i_conn, dummy_rq_data, 1);
	bnx2i_put_rq_buf(bnx2i_conn, 1);
}


/**
 * bnx2i_process_nopin_mesg - this function handles iscsi nopin CQE
 * @session:		iscsi session pointer
 * @bnx2i_conn:		iscsi connection pointer
 * @cqe:		pointer to newly DMA'ed CQE entry for processing
 *
 * process iSCSI target's proactive iSCSI NOPIN request
 */
static int bnx2i_process_nopin_mesg(struct iscsi_session *session,
				     struct bnx2i_conn *bnx2i_conn,
				     struct cqe *cqe)
{
	struct iscsi_conn *conn = bnx2i_conn->cls_conn->dd_data;
	struct iscsi_task *task;
	struct bnx2i_nop_in_msg *nop_in;
	struct iscsi_nopin *hdr;
	int tgt_async_nop = 0;

	nop_in = (struct bnx2i_nop_in_msg *)cqe;

	spin_lock(&session->lock);
	hdr = (struct iscsi_nopin *)&bnx2i_conn->gen_pdu.resp_hdr;
	memset(hdr, 0, sizeof(struct iscsi_hdr));
	hdr->opcode = nop_in->op_code;
	hdr->max_cmdsn = cpu_to_be32(nop_in->max_cmd_sn);
	hdr->exp_cmdsn = cpu_to_be32(nop_in->exp_cmd_sn);
	hdr->ttt = cpu_to_be32(nop_in->ttt);

	if (nop_in->itt == (u16) RESERVED_ITT) {
		bnx2i_unsol_pdu_adjust_rq(bnx2i_conn);
		hdr->itt = RESERVED_ITT;
		tgt_async_nop = 1;
		goto done;
	}

	/* this is a response to one of our nop-outs */
	task = iscsi_itt_to_task(conn,
			 (itt_t) (nop_in->itt & ISCSI_NOP_IN_MSG_INDEX));
	if (task) {
		hdr->flags = ISCSI_FLAG_CMD_FINAL;
		hdr->itt = task->hdr->itt;
		hdr->ttt = cpu_to_be32(nop_in->ttt);
		memcpy(&hdr->lun, nop_in->lun, 8);
	}
done:
	__iscsi_complete_pdu(conn, (struct iscsi_hdr *)hdr, NULL, 0);
	spin_unlock(&session->lock);

	return tgt_async_nop;
}


/**
 * bnx2i_process_async_mesg - this function handles iscsi async message
 * @session:		iscsi session pointer
 * @bnx2i_conn:		iscsi connection pointer
 * @cqe:		pointer to newly DMA'ed CQE entry for processing
 *
 * process iSCSI ASYNC Message
 */
static void bnx2i_process_async_mesg(struct iscsi_session *session,
				     struct bnx2i_conn *bnx2i_conn,
				     struct cqe *cqe)
{
	struct bnx2i_async_msg *async_cqe;
	struct iscsi_async *resp_hdr;
	u8 async_event;

	bnx2i_unsol_pdu_adjust_rq(bnx2i_conn);

	async_cqe = (struct bnx2i_async_msg *)cqe;
	async_event = async_cqe->async_event;

	if (async_event == ISCSI_ASYNC_MSG_SCSI_EVENT) {
		iscsi_conn_printk(KERN_ALERT, bnx2i_conn->cls_conn->dd_data,
				  "async: scsi events not supported\n");
		return;
	}

	spin_lock(&session->lock);
	resp_hdr = (struct iscsi_async *) &bnx2i_conn->gen_pdu.resp_hdr;
	memset(resp_hdr, 0, sizeof(struct iscsi_hdr));
	resp_hdr->opcode = async_cqe->op_code;
	resp_hdr->flags = 0x80;

	memcpy(&resp_hdr->lun, async_cqe->lun, 8);
	resp_hdr->exp_cmdsn = cpu_to_be32(async_cqe->exp_cmd_sn);
	resp_hdr->max_cmdsn = cpu_to_be32(async_cqe->max_cmd_sn);

	resp_hdr->async_event = async_cqe->async_event;
	resp_hdr->async_vcode = async_cqe->async_vcode;

	resp_hdr->param1 = cpu_to_be16(async_cqe->param1);
	resp_hdr->param2 = cpu_to_be16(async_cqe->param2);
	resp_hdr->param3 = cpu_to_be16(async_cqe->param3);

	__iscsi_complete_pdu(bnx2i_conn->cls_conn->dd_data,
			     (struct iscsi_hdr *)resp_hdr, NULL, 0);
	spin_unlock(&session->lock);
}


/**
 * bnx2i_process_reject_mesg - process iscsi reject pdu
 * @session:		iscsi session pointer
 * @bnx2i_conn:		iscsi connection pointer
 * @cqe:		pointer to newly DMA'ed CQE entry for processing
 *
 * process iSCSI REJECT message
 */
static void bnx2i_process_reject_mesg(struct iscsi_session *session,
				      struct bnx2i_conn *bnx2i_conn,
				      struct cqe *cqe)
{
	struct iscsi_conn *conn = bnx2i_conn->cls_conn->dd_data;
	struct bnx2i_reject_msg *reject;
	struct iscsi_reject *hdr;

	reject = (struct bnx2i_reject_msg *) cqe;
	if (reject->data_length) {
		bnx2i_get_rq_buf(bnx2i_conn, conn->data, reject->data_length);
		bnx2i_put_rq_buf(bnx2i_conn, 1);
	} else
		bnx2i_unsol_pdu_adjust_rq(bnx2i_conn);

	spin_lock(&session->lock);
	hdr = (struct iscsi_reject *) &bnx2i_conn->gen_pdu.resp_hdr;
	memset(hdr, 0, sizeof(struct iscsi_hdr));
	hdr->opcode = reject->op_code;
	hdr->reason = reject->reason;
	hton24(hdr->dlength, reject->data_length);
	hdr->max_cmdsn = cpu_to_be32(reject->max_cmd_sn);
	hdr->exp_cmdsn = cpu_to_be32(reject->exp_cmd_sn);
	hdr->ffffffff = cpu_to_be32(RESERVED_ITT);
	__iscsi_complete_pdu(conn, (struct iscsi_hdr *)hdr, conn->data,
			     reject->data_length);
	spin_unlock(&session->lock);
}

/**
 * bnx2i_process_cmd_cleanup_resp - process scsi command clean-up completion
 * @session:		iscsi session pointer
 * @bnx2i_conn:		iscsi connection pointer
 * @cqe:		pointer to newly DMA'ed CQE entry for processing
 *
 * process command cleanup response CQE during conn shutdown or error recovery
 */
static void bnx2i_process_cmd_cleanup_resp(struct iscsi_session *session,
					   struct bnx2i_conn *bnx2i_conn,
					   struct cqe *cqe)
{
	struct bnx2i_cleanup_response *cmd_clean_rsp;
	struct iscsi_conn *conn = bnx2i_conn->cls_conn->dd_data;
	struct iscsi_task *task;

	cmd_clean_rsp = (struct bnx2i_cleanup_response *)cqe;
	spin_lock(&session->lock);
	task = iscsi_itt_to_task(conn,
			cmd_clean_rsp->itt & ISCSI_CLEANUP_RESPONSE_INDEX);
	if (!task)
		printk(KERN_ALERT "bnx2i: cmd clean ITT %x not active\n",
			cmd_clean_rsp->itt & ISCSI_CLEANUP_RESPONSE_INDEX);
	spin_unlock(&session->lock);
	complete(&bnx2i_conn->cmd_cleanup_cmpl);
}


/**
 * bnx2i_percpu_io_thread - thread per cpu for ios
 *
 * @arg:	ptr to bnx2i_percpu_info structure
 */
int bnx2i_percpu_io_thread(void *arg)
{
	struct bnx2i_percpu_s *p = arg;
	struct bnx2i_work *work, *tmp;
	LIST_HEAD(work_list);

	set_user_nice(current, -20);

	while (!kthread_should_stop()) {
		spin_lock_bh(&p->p_work_lock);
		while (!list_empty(&p->work_list)) {
			list_splice_init(&p->work_list, &work_list);
			spin_unlock_bh(&p->p_work_lock);

			list_for_each_entry_safe(work, tmp, &work_list, list) {
				list_del_init(&work->list);
				/* work allocated in the bh, freed here */
				bnx2i_process_scsi_cmd_resp(work->session,
							    work->bnx2i_conn,
							    &work->cqe);
				atomic_dec(&work->bnx2i_conn->work_cnt);
				kfree(work);
			}
			spin_lock_bh(&p->p_work_lock);
		}
		set_current_state(TASK_INTERRUPTIBLE);
		spin_unlock_bh(&p->p_work_lock);
		schedule();
	}
	__set_current_state(TASK_RUNNING);

	return 0;
}


/**
 * bnx2i_queue_scsi_cmd_resp - queue cmd completion to the percpu thread
 * @bnx2i_conn:		bnx2i connection
 *
 * this function is called by generic KCQ handler to queue all pending cmd
 * completion CQEs
 *
 * The implementation is to queue the cmd response based on the
 * last recorded command for the given connection.  The
 * cpu_id gets recorded upon task_xmit.  No out-of-order completion!
 */
static int bnx2i_queue_scsi_cmd_resp(struct iscsi_session *session,
				     struct bnx2i_conn *bnx2i_conn,
				     struct bnx2i_nop_in_msg *cqe)
{
	struct bnx2i_work *bnx2i_work = NULL;
	struct bnx2i_percpu_s *p = NULL;
	struct iscsi_task *task;
	struct scsi_cmnd *sc;
	int rc = 0;
	int cpu;

	spin_lock(&session->lock);
	task = iscsi_itt_to_task(bnx2i_conn->cls_conn->dd_data,
				 cqe->itt & ISCSI_CMD_RESPONSE_INDEX);
	if (!task || !task->sc) {
		spin_unlock(&session->lock);
		return -EINVAL;
	}
	sc = task->sc;

	if (!blk_rq_cpu_valid(sc->request))
		cpu = smp_processor_id();
	else
		cpu = sc->request->cpu;

	spin_unlock(&session->lock);

	p = &per_cpu(bnx2i_percpu, cpu);
	spin_lock(&p->p_work_lock);
	if (unlikely(!p->iothread)) {
		rc = -EINVAL;
		goto err;
	}
	/* Alloc and copy to the cqe */
	bnx2i_work = kzalloc(sizeof(struct bnx2i_work), GFP_ATOMIC);
	if (bnx2i_work) {
		INIT_LIST_HEAD(&bnx2i_work->list);
		bnx2i_work->session = session;
		bnx2i_work->bnx2i_conn = bnx2i_conn;
		memcpy(&bnx2i_work->cqe, cqe, sizeof(struct cqe));
		list_add_tail(&bnx2i_work->list, &p->work_list);
		atomic_inc(&bnx2i_conn->work_cnt);
		wake_up_process(p->iothread);
		spin_unlock(&p->p_work_lock);
		goto done;
	} else
		rc = -ENOMEM;
err:
	spin_unlock(&p->p_work_lock);
	bnx2i_process_scsi_cmd_resp(session, bnx2i_conn, (struct cqe *)cqe);
done:
	return rc;
}


/**
 * bnx2i_process_new_cqes - process newly DMA'ed CQE's
 * @bnx2i_conn:		bnx2i connection
 *
 * this function is called by generic KCQ handler to process all pending CQE's
 */
static int bnx2i_process_new_cqes(struct bnx2i_conn *bnx2i_conn)
{
	struct iscsi_conn *conn = bnx2i_conn->cls_conn->dd_data;
	struct iscsi_session *session = conn->session;
	struct qp_info *qp;
	struct bnx2i_nop_in_msg *nopin;
	int tgt_async_msg;
	int cqe_cnt = 0;

	if (bnx2i_conn->ep == NULL)
		return 0;

	qp = &bnx2i_conn->ep->qp;

	if (!qp->cq_virt) {
		printk(KERN_ALERT "bnx2i (%s): cq resr freed in bh execution!",
			bnx2i_conn->hba->netdev->name);
		goto out;
	}
	while (1) {
		nopin = (struct bnx2i_nop_in_msg *) qp->cq_cons_qe;
		if (nopin->cq_req_sn != qp->cqe_exp_seq_sn)
			break;

		if (unlikely(test_bit(ISCSI_SUSPEND_BIT, &conn->suspend_rx))) {
			if (nopin->op_code == ISCSI_OP_NOOP_IN &&
			    nopin->itt == (u16) RESERVED_ITT) {
				printk(KERN_ALERT "bnx2i: Unsolicited "
					"NOP-In detected for suspended "
					"connection dev=%s!\n",
					bnx2i_conn->hba->netdev->name);
				bnx2i_unsol_pdu_adjust_rq(bnx2i_conn);
				goto cqe_out;
			}
			break;
		}
		tgt_async_msg = 0;

		switch (nopin->op_code) {
		case ISCSI_OP_SCSI_CMD_RSP:
		case ISCSI_OP_SCSI_DATA_IN:
			/* Run the kthread engine only for data cmds
			   All other cmds will be completed in this bh! */
			bnx2i_queue_scsi_cmd_resp(session, bnx2i_conn, nopin);
			break;
		case ISCSI_OP_LOGIN_RSP:
			bnx2i_process_login_resp(session, bnx2i_conn,
						 qp->cq_cons_qe);
			break;
		case ISCSI_OP_SCSI_TMFUNC_RSP:
			bnx2i_process_tmf_resp(session, bnx2i_conn,
					       qp->cq_cons_qe);
			break;
		case ISCSI_OP_TEXT_RSP:
			bnx2i_process_text_resp(session, bnx2i_conn,
						qp->cq_cons_qe);
			break;
		case ISCSI_OP_LOGOUT_RSP:
			bnx2i_process_logout_resp(session, bnx2i_conn,
						  qp->cq_cons_qe);
			break;
		case ISCSI_OP_NOOP_IN:
			if (bnx2i_process_nopin_mesg(session, bnx2i_conn,
						     qp->cq_cons_qe))
				tgt_async_msg = 1;
			break;
		case ISCSI_OPCODE_NOPOUT_LOCAL_COMPLETION:
			bnx2i_process_nopin_local_cmpl(session, bnx2i_conn,
						       qp->cq_cons_qe);
			break;
		case ISCSI_OP_ASYNC_EVENT:
			bnx2i_process_async_mesg(session, bnx2i_conn,
						 qp->cq_cons_qe);
			tgt_async_msg = 1;
			break;
		case ISCSI_OP_REJECT:
			bnx2i_process_reject_mesg(session, bnx2i_conn,
						  qp->cq_cons_qe);
			break;
		case ISCSI_OPCODE_CLEANUP_RESPONSE:
			bnx2i_process_cmd_cleanup_resp(session, bnx2i_conn,
						       qp->cq_cons_qe);
			break;
		default:
			printk(KERN_ALERT "bnx2i: unknown opcode 0x%x\n",
					  nopin->op_code);
		}
		if (!tgt_async_msg) {
			if (!atomic_read(&bnx2i_conn->ep->num_active_cmds))
				printk(KERN_ALERT "bnx2i (%s): no active cmd! "
				       "op 0x%x\n",
				       bnx2i_conn->hba->netdev->name,
				       nopin->op_code);
			else
				atomic_dec(&bnx2i_conn->ep->num_active_cmds);
		}
cqe_out:
		/* clear out in production version only, till beta keep opcode
		 * field intact, will be helpful in debugging (context dump)
		 * nopin->op_code = 0;
		 */
		cqe_cnt++;
		qp->cqe_exp_seq_sn++;
		if (qp->cqe_exp_seq_sn == (qp->cqe_size * 2 + 1))
			qp->cqe_exp_seq_sn = ISCSI_INITIAL_SN;

		if (qp->cq_cons_qe == qp->cq_last_qe) {
			qp->cq_cons_qe = qp->cq_first_qe;
			qp->cq_cons_idx = 0;
		} else {
			qp->cq_cons_qe++;
			qp->cq_cons_idx++;
		}
	}
out:
	return cqe_cnt;
}

/**
 * bnx2i_fastpath_notification - process global event queue (KCQ)
 * @hba:		adapter structure pointer
 * @new_cqe_kcqe:	pointer to newly DMA'ed KCQE entry
 *
 * Fast path event notification handler, KCQ entry carries context id
 *	of the connection that has 1 or more pending CQ entries
 */
static void bnx2i_fastpath_notification(struct bnx2i_hba *hba,
					struct iscsi_kcqe *new_cqe_kcqe)
{
	struct bnx2i_conn *bnx2i_conn;
	u32 iscsi_cid;
	int nxt_idx;

	iscsi_cid = new_cqe_kcqe->iscsi_conn_id;
	bnx2i_conn = bnx2i_get_conn_from_id(hba, iscsi_cid);

	if (!bnx2i_conn) {
		printk(KERN_ALERT "cid #%x not valid\n", iscsi_cid);
		return;
	}
	if (!bnx2i_conn->ep) {
		printk(KERN_ALERT "cid #%x - ep not bound\n", iscsi_cid);
		return;
	}

	bnx2i_process_new_cqes(bnx2i_conn);
	nxt_idx = bnx2i_arm_cq_event_coalescing(bnx2i_conn->ep,
						CNIC_ARM_CQE_FP);
	if (nxt_idx && nxt_idx == bnx2i_process_new_cqes(bnx2i_conn))
		bnx2i_arm_cq_event_coalescing(bnx2i_conn->ep, CNIC_ARM_CQE_FP);
}


/**
 * bnx2i_process_update_conn_cmpl - process iscsi conn update completion KCQE
 * @hba:		adapter structure pointer
 * @update_kcqe:	kcqe pointer
 *
 * CONN_UPDATE completion handler, this completes iSCSI connection FFP migration
 */
static void bnx2i_process_update_conn_cmpl(struct bnx2i_hba *hba,
					   struct iscsi_kcqe *update_kcqe)
{
	struct bnx2i_conn *conn;
	u32 iscsi_cid;

	iscsi_cid = update_kcqe->iscsi_conn_id;
	conn = bnx2i_get_conn_from_id(hba, iscsi_cid);

	if (!conn) {
		printk(KERN_ALERT "conn_update: cid %x not valid\n", iscsi_cid);
		return;
	}
	if (!conn->ep) {
		printk(KERN_ALERT "cid %x does not have ep bound\n", iscsi_cid);
		return;
	}

	if (update_kcqe->completion_status) {
		printk(KERN_ALERT "request failed cid %x\n", iscsi_cid);
		conn->ep->state = EP_STATE_ULP_UPDATE_FAILED;
	} else
		conn->ep->state = EP_STATE_ULP_UPDATE_COMPL;

	wake_up_interruptible(&conn->ep->ofld_wait);
}


/**
 * bnx2i_recovery_que_add_conn - add connection to recovery queue
 * @hba:		adapter structure pointer
 * @bnx2i_conn:		iscsi connection
 *
 * Add connection to recovery queue and schedule adapter eh worker
 */
static void bnx2i_recovery_que_add_conn(struct bnx2i_hba *hba,
					struct bnx2i_conn *bnx2i_conn)
{
	iscsi_conn_failure(bnx2i_conn->cls_conn->dd_data,
			   ISCSI_ERR_CONN_FAILED);
}


/**
 * bnx2i_process_tcp_error - process error notification on a given connection
 *
 * @hba: 		adapter structure pointer
 * @tcp_err: 		tcp error kcqe pointer
 *
 * handles tcp level error notifications from FW.
 */
static void bnx2i_process_tcp_error(struct bnx2i_hba *hba,
				    struct iscsi_kcqe *tcp_err)
{
	struct bnx2i_conn *bnx2i_conn;
	u32 iscsi_cid;

	iscsi_cid = tcp_err->iscsi_conn_id;
	bnx2i_conn = bnx2i_get_conn_from_id(hba, iscsi_cid);

	if (!bnx2i_conn) {
		printk(KERN_ALERT "bnx2i - cid 0x%x not valid\n", iscsi_cid);
		return;
	}

	printk(KERN_ALERT "bnx2i - cid 0x%x had TCP errors, error code 0x%x\n",
			  iscsi_cid, tcp_err->completion_status);
	bnx2i_recovery_que_add_conn(bnx2i_conn->hba, bnx2i_conn);
}


/**
 * bnx2i_process_iscsi_error - process error notification on a given connection
 * @hba:		adapter structure pointer
 * @iscsi_err:		iscsi error kcqe pointer
 *
 * handles iscsi error notifications from the FW. Firmware based in initial
 *	handshake classifies iscsi protocol / TCP rfc violation into either
 *	warning or error indications. If indication is of "Error" type, driver
 *	will initiate session recovery for that connection/session. For
 *	"Warning" type indication, driver will put out a system log message
 *	(there will be only one message for each type for the life of the
 *	session, this is to avoid un-necessarily overloading the system)
 */
static void bnx2i_process_iscsi_error(struct bnx2i_hba *hba,
				      struct iscsi_kcqe *iscsi_err)
{
	struct bnx2i_conn *bnx2i_conn;
	u32 iscsi_cid;
	char warn_notice[] = "iscsi_warning";
	char error_notice[] = "iscsi_error";
	char additional_notice[64];
	char *message;
	int need_recovery;
	u64 err_mask64;

	iscsi_cid = iscsi_err->iscsi_conn_id;
	bnx2i_conn = bnx2i_get_conn_from_id(hba, iscsi_cid);
	if (!bnx2i_conn) {
		printk(KERN_ALERT "bnx2i - cid 0x%x not valid\n", iscsi_cid);
		return;
	}

	err_mask64 = (0x1ULL << iscsi_err->completion_status);

	if (err_mask64 & iscsi_error_mask) {
		need_recovery = 0;
		message = warn_notice;
	} else {
		need_recovery = 1;
		message = error_notice;
	}

	switch (iscsi_err->completion_status) {
	case ISCSI_KCQE_COMPLETION_STATUS_HDR_DIG_ERR:
		strcpy(additional_notice, "hdr digest err");
		break;
	case ISCSI_KCQE_COMPLETION_STATUS_DATA_DIG_ERR:
		strcpy(additional_notice, "data digest err");
		break;
	case ISCSI_KCQE_COMPLETION_STATUS_PROTOCOL_ERR_OPCODE:
		strcpy(additional_notice, "wrong opcode rcvd");
		break;
	case ISCSI_KCQE_COMPLETION_STATUS_PROTOCOL_ERR_AHS_LEN:
		strcpy(additional_notice, "AHS len > 0 rcvd");
		break;
	case ISCSI_KCQE_COMPLETION_STATUS_PROTOCOL_ERR_ITT:
		strcpy(additional_notice, "invalid ITT rcvd");
		break;
	case ISCSI_KCQE_COMPLETION_STATUS_PROTOCOL_ERR_STATSN:
		strcpy(additional_notice, "wrong StatSN rcvd");
		break;
	case ISCSI_KCQE_COMPLETION_STATUS_PROTOCOL_ERR_EXP_DATASN:
		strcpy(additional_notice, "wrong DataSN rcvd");
		break;
	case ISCSI_KCQE_COMPLETION_STATUS_PROTOCOL_ERR_PEND_R2T:
		strcpy(additional_notice, "pend R2T violation");
		break;
	case ISCSI_KCQE_COMPLETION_STATUS_PROTOCOL_ERR_O_U_0:
		strcpy(additional_notice, "ERL0, UO");
		break;
	case ISCSI_KCQE_COMPLETION_STATUS_PROTOCOL_ERR_O_U_1:
		strcpy(additional_notice, "ERL0, U1");
		break;
	case ISCSI_KCQE_COMPLETION_STATUS_PROTOCOL_ERR_O_U_2:
		strcpy(additional_notice, "ERL0, U2");
		break;
	case ISCSI_KCQE_COMPLETION_STATUS_PROTOCOL_ERR_O_U_3:
		strcpy(additional_notice, "ERL0, U3");
		break;
	case ISCSI_KCQE_COMPLETION_STATUS_PROTOCOL_ERR_O_U_4:
		strcpy(additional_notice, "ERL0, U4");
		break;
	case ISCSI_KCQE_COMPLETION_STATUS_PROTOCOL_ERR_O_U_5:
		strcpy(additional_notice, "ERL0, U5");
		break;
	case ISCSI_KCQE_COMPLETION_STATUS_PROTOCOL_ERR_O_U_6:
		strcpy(additional_notice, "ERL0, U6");
		break;
	case ISCSI_KCQE_COMPLETION_STATUS_PROTOCOL_ERR_REMAIN_RCV_LEN:
		strcpy(additional_notice, "invalid resi len");
		break;
	case ISCSI_KCQE_COMPLETION_STATUS_PROTOCOL_ERR_MAX_RCV_PDU_LEN:
		strcpy(additional_notice, "MRDSL violation");
		break;
	case ISCSI_KCQE_COMPLETION_STATUS_PROTOCOL_ERR_F_BIT_ZERO:
		strcpy(additional_notice, "F-bit not set");
		break;
	case ISCSI_KCQE_COMPLETION_STATUS_PROTOCOL_ERR_TTT_NOT_RSRV:
		strcpy(additional_notice, "invalid TTT");
		break;
	case ISCSI_KCQE_COMPLETION_STATUS_PROTOCOL_ERR_DATASN:
		strcpy(additional_notice, "invalid DataSN");
		break;
	case ISCSI_KCQE_COMPLETION_STATUS_PROTOCOL_ERR_REMAIN_BURST_LEN:
		strcpy(additional_notice, "burst len violation");
		break;
	case ISCSI_KCQE_COMPLETION_STATUS_PROTOCOL_ERR_BUFFER_OFF:
		strcpy(additional_notice, "buf offset violation");
		break;
	case ISCSI_KCQE_COMPLETION_STATUS_PROTOCOL_ERR_LUN:
		strcpy(additional_notice, "invalid LUN field");
		break;
	case ISCSI_KCQE_COMPLETION_STATUS_PROTOCOL_ERR_R2TSN:
		strcpy(additional_notice, "invalid R2TSN field");
		break;
#define BNX2I_ERR_DESIRED_DATA_TRNS_LEN_0 	\
	ISCSI_KCQE_COMPLETION_STATUS_PROTOCOL_ERR_DESIRED_DATA_TRNS_LEN_0
	case BNX2I_ERR_DESIRED_DATA_TRNS_LEN_0:
		strcpy(additional_notice, "invalid cmd len1");
		break;
#define BNX2I_ERR_DESIRED_DATA_TRNS_LEN_1 	\
	ISCSI_KCQE_COMPLETION_STATUS_PROTOCOL_ERR_DESIRED_DATA_TRNS_LEN_1
	case BNX2I_ERR_DESIRED_DATA_TRNS_LEN_1:
		strcpy(additional_notice, "invalid cmd len2");
		break;
	case ISCSI_KCQE_COMPLETION_STATUS_PROTOCOL_ERR_PEND_R2T_EXCEED:
		strcpy(additional_notice,
		       "pend r2t exceeds MaxOutstandingR2T value");
		break;
	case ISCSI_KCQE_COMPLETION_STATUS_PROTOCOL_ERR_TTT_IS_RSRV:
		strcpy(additional_notice, "TTT is rsvd");
		break;
	case ISCSI_KCQE_COMPLETION_STATUS_PROTOCOL_ERR_MAX_BURST_LEN:
		strcpy(additional_notice, "MBL violation");
		break;
#define BNX2I_ERR_DATA_SEG_LEN_NOT_ZERO 	\
	ISCSI_KCQE_COMPLETION_STATUS_PROTOCOL_ERR_DATA_SEG_LEN_NOT_ZERO
	case BNX2I_ERR_DATA_SEG_LEN_NOT_ZERO:
		strcpy(additional_notice, "data seg len != 0");
		break;
	case ISCSI_KCQE_COMPLETION_STATUS_PROTOCOL_ERR_REJECT_PDU_LEN:
		strcpy(additional_notice, "reject pdu len error");
		break;
	case ISCSI_KCQE_COMPLETION_STATUS_PROTOCOL_ERR_ASYNC_PDU_LEN:
		strcpy(additional_notice, "async pdu len error");
		break;
	case ISCSI_KCQE_COMPLETION_STATUS_PROTOCOL_ERR_NOPIN_PDU_LEN:
		strcpy(additional_notice, "nopin pdu len error");
		break;
#define BNX2_ERR_PEND_R2T_IN_CLEANUP			\
	ISCSI_KCQE_COMPLETION_STATUS_PROTOCOL_ERR_PEND_R2T_IN_CLEANUP
	case BNX2_ERR_PEND_R2T_IN_CLEANUP:
		strcpy(additional_notice, "pend r2t in cleanup");
		break;

	case ISCI_KCQE_COMPLETION_STATUS_TCP_ERROR_IP_FRAGMENT:
		strcpy(additional_notice, "IP fragments rcvd");
		break;
	case ISCI_KCQE_COMPLETION_STATUS_TCP_ERROR_IP_OPTIONS:
		strcpy(additional_notice, "IP options error");
		break;
	case ISCI_KCQE_COMPLETION_STATUS_TCP_ERROR_URGENT_FLAG:
		strcpy(additional_notice, "urgent flag error");
		break;
	default:
		printk(KERN_ALERT "iscsi_err - unknown err %x\n",
				  iscsi_err->completion_status);
	}

	if (need_recovery) {
		iscsi_conn_printk(KERN_ALERT,
				  bnx2i_conn->cls_conn->dd_data,
				  "bnx2i: %s - %s\n",
				  message, additional_notice);

		iscsi_conn_printk(KERN_ALERT,
				  bnx2i_conn->cls_conn->dd_data,
				  "conn_err - hostno %d conn %p, "
				  "iscsi_cid %x cid %x\n",
				  bnx2i_conn->hba->shost->host_no,
				  bnx2i_conn, bnx2i_conn->ep->ep_iscsi_cid,
				  bnx2i_conn->ep->ep_cid);
		bnx2i_recovery_que_add_conn(bnx2i_conn->hba, bnx2i_conn);
	} else
		if (!test_and_set_bit(iscsi_err->completion_status,
				      (void *) &bnx2i_conn->violation_notified))
			iscsi_conn_printk(KERN_ALERT,
					  bnx2i_conn->cls_conn->dd_data,
					  "bnx2i: %s - %s\n",
					  message, additional_notice);
}


/**
 * bnx2i_process_conn_destroy_cmpl - process iscsi conn destroy completion
 * @hba:		adapter structure pointer
 * @conn_destroy:	conn destroy kcqe pointer
 *
 * handles connection destroy completion request.
 */
static void bnx2i_process_conn_destroy_cmpl(struct bnx2i_hba *hba,
					    struct iscsi_kcqe *conn_destroy)
{
	struct bnx2i_endpoint *ep;

	ep = bnx2i_find_ep_in_destroy_list(hba, conn_destroy->iscsi_conn_id);
	if (!ep) {
		printk(KERN_ALERT "bnx2i_conn_destroy_cmpl: no pending "
				  "offload request, unexpected complection\n");
		return;
	}

	if (hba != ep->hba) {
		printk(KERN_ALERT "conn destroy- error hba mis-match\n");
		return;
	}

	if (conn_destroy->completion_status) {
		printk(KERN_ALERT "conn_destroy_cmpl: op failed\n");
		ep->state = EP_STATE_CLEANUP_FAILED;
	} else
		ep->state = EP_STATE_CLEANUP_CMPL;
	wake_up_interruptible(&ep->ofld_wait);
}


/**
 * bnx2i_process_ofld_cmpl - process initial iscsi conn offload completion
 * @hba:		adapter structure pointer
 * @ofld_kcqe:		conn offload kcqe pointer
 *
 * handles initial connection offload completion, ep_connect() thread is
 *	woken-up to continue with LLP connect process
 */
static void bnx2i_process_ofld_cmpl(struct bnx2i_hba *hba,
				    struct iscsi_kcqe *ofld_kcqe)
{
	u32 cid_addr;
	struct bnx2i_endpoint *ep;
	u32 cid_num;

	ep = bnx2i_find_ep_in_ofld_list(hba, ofld_kcqe->iscsi_conn_id);
	if (!ep) {
		printk(KERN_ALERT "ofld_cmpl: no pend offload request\n");
		return;
	}

	if (hba != ep->hba) {
		printk(KERN_ALERT "ofld_cmpl: error hba mis-match\n");
		return;
	}

	if (ofld_kcqe->completion_status) {
		ep->state = EP_STATE_OFLD_FAILED;
		if (ofld_kcqe->completion_status ==
		    ISCSI_KCQE_COMPLETION_STATUS_CTX_ALLOC_FAILURE)
			printk(KERN_ALERT "bnx2i (%s): ofld1 cmpl - unable "
				"to allocate iSCSI context resources\n",
				hba->netdev->name);
		else if (ofld_kcqe->completion_status ==
			 ISCSI_KCQE_COMPLETION_STATUS_INVALID_OPCODE)
			printk(KERN_ALERT "bnx2i (%s): ofld1 cmpl - invalid "
				"opcode\n", hba->netdev->name);
		else if (ofld_kcqe->completion_status ==
			 ISCSI_KCQE_COMPLETION_STATUS_CID_BUSY)
			/* error status code valid only for 5771x chipset */
			ep->state = EP_STATE_OFLD_FAILED_CID_BUSY;
		else
			printk(KERN_ALERT "bnx2i (%s): ofld1 cmpl - invalid "
				"error code %d\n", hba->netdev->name,
				ofld_kcqe->completion_status);
	} else {
		ep->state = EP_STATE_OFLD_COMPL;
		cid_addr = ofld_kcqe->iscsi_conn_context_id;
		cid_num = bnx2i_get_cid_num(ep);
		ep->ep_cid = cid_addr;
		ep->qp.ctx_base = NULL;
	}
	wake_up_interruptible(&ep->ofld_wait);
}

/**
 * bnx2i_indicate_kcqe - process iscsi conn update completion KCQE
 * @hba:		adapter structure pointer
 * @update_kcqe:	kcqe pointer
 *
 * Generic KCQ event handler/dispatcher
 */
static void bnx2i_indicate_kcqe(void *context, struct kcqe *kcqe[],
				u32 num_cqe)
{
	struct bnx2i_hba *hba = context;
	int i = 0;
	struct iscsi_kcqe *ikcqe = NULL;

	while (i < num_cqe) {
		ikcqe = (struct iscsi_kcqe *) kcqe[i++];

		if (ikcqe->op_code ==
		    ISCSI_KCQE_OPCODE_CQ_EVENT_NOTIFICATION)
			bnx2i_fastpath_notification(hba, ikcqe);
		else if (ikcqe->op_code == ISCSI_KCQE_OPCODE_OFFLOAD_CONN)
			bnx2i_process_ofld_cmpl(hba, ikcqe);
		else if (ikcqe->op_code == ISCSI_KCQE_OPCODE_UPDATE_CONN)
			bnx2i_process_update_conn_cmpl(hba, ikcqe);
		else if (ikcqe->op_code == ISCSI_KCQE_OPCODE_INIT) {
			if (ikcqe->completion_status !=
			    ISCSI_KCQE_COMPLETION_STATUS_SUCCESS)
				bnx2i_iscsi_license_error(hba, ikcqe->\
							  completion_status);
			else {
				set_bit(ADAPTER_STATE_UP, &hba->adapter_state);
				bnx2i_get_link_state(hba);
				printk(KERN_INFO "bnx2i [%.2x:%.2x.%.2x]: "
						 "ISCSI_INIT passed\n",
						 (u8)hba->pcidev->bus->number,
						 hba->pci_devno,
						 (u8)hba->pci_func);


			}
		} else if (ikcqe->op_code == ISCSI_KCQE_OPCODE_DESTROY_CONN)
			bnx2i_process_conn_destroy_cmpl(hba, ikcqe);
		else if (ikcqe->op_code == ISCSI_KCQE_OPCODE_ISCSI_ERROR)
			bnx2i_process_iscsi_error(hba, ikcqe);
		else if (ikcqe->op_code == ISCSI_KCQE_OPCODE_TCP_ERROR)
			bnx2i_process_tcp_error(hba, ikcqe);
		else
			printk(KERN_ALERT "bnx2i: unknown opcode 0x%x\n",
					  ikcqe->op_code);
	}
}


/**
 * bnx2i_indicate_netevent - Generic netdev event handler
 * @context:	adapter structure pointer
 * @event:	event type
 * @vlan_id:	vlans id - associated vlan id with this event
 *
 * Handles four netdev events, NETDEV_UP, NETDEV_DOWN,
 *	NETDEV_GOING_DOWN and NETDEV_CHANGE
 */
static void bnx2i_indicate_netevent(void *context, unsigned long event,
				    u16 vlan_id)
{
	struct bnx2i_hba *hba = context;

	/* Ignore all netevent coming from vlans */
	if (vlan_id != 0)
		return;

	switch (event) {
	case NETDEV_UP:
		if (!test_bit(ADAPTER_STATE_UP, &hba->adapter_state))
			bnx2i_send_fw_iscsi_init_msg(hba);
		break;
	case NETDEV_DOWN:
		clear_bit(ADAPTER_STATE_GOING_DOWN, &hba->adapter_state);
		clear_bit(ADAPTER_STATE_UP, &hba->adapter_state);
		break;
	case NETDEV_GOING_DOWN:
		set_bit(ADAPTER_STATE_GOING_DOWN, &hba->adapter_state);
		iscsi_host_for_each_session(hba->shost,
					    bnx2i_drop_session);
		break;
	case NETDEV_CHANGE:
		bnx2i_get_link_state(hba);
		break;
	default:
		;
	}
}


/**
 * bnx2i_cm_connect_cmpl - process iscsi conn establishment completion
 * @cm_sk: 		cnic sock structure pointer
 *
 * function callback exported via bnx2i - cnic driver interface to
 *	indicate completion of option-2 TCP connect request.
 */
static void bnx2i_cm_connect_cmpl(struct cnic_sock *cm_sk)
{
	struct bnx2i_endpoint *ep = (struct bnx2i_endpoint *) cm_sk->context;

	if (test_bit(ADAPTER_STATE_GOING_DOWN, &ep->hba->adapter_state))
		ep->state = EP_STATE_CONNECT_FAILED;
	else if (test_bit(SK_F_OFFLD_COMPLETE, &cm_sk->flags))
		ep->state = EP_STATE_CONNECT_COMPL;
	else
		ep->state = EP_STATE_CONNECT_FAILED;

	wake_up_interruptible(&ep->ofld_wait);
}


/**
 * bnx2i_cm_close_cmpl - process tcp conn close completion
 * @cm_sk:	cnic sock structure pointer
 *
 * function callback exported via bnx2i - cnic driver interface to
 *	indicate completion of option-2 graceful TCP connect shutdown
 */
static void bnx2i_cm_close_cmpl(struct cnic_sock *cm_sk)
{
	struct bnx2i_endpoint *ep = (struct bnx2i_endpoint *) cm_sk->context;

	ep->state = EP_STATE_DISCONN_COMPL;
	wake_up_interruptible(&ep->ofld_wait);
}


/**
 * bnx2i_cm_abort_cmpl - process abortive tcp conn teardown completion
 * @cm_sk:	cnic sock structure pointer
 *
 * function callback exported via bnx2i - cnic driver interface to
 *	indicate completion of option-2 abortive TCP connect termination
 */
static void bnx2i_cm_abort_cmpl(struct cnic_sock *cm_sk)
{
	struct bnx2i_endpoint *ep = (struct bnx2i_endpoint *) cm_sk->context;

	ep->state = EP_STATE_DISCONN_COMPL;
	wake_up_interruptible(&ep->ofld_wait);
}


/**
 * bnx2i_cm_remote_close - process received TCP FIN
 * @hba:		adapter structure pointer
 * @update_kcqe:	kcqe pointer
 *
 * function callback exported via bnx2i - cnic driver interface to indicate
 *	async TCP events such as FIN
 */
static void bnx2i_cm_remote_close(struct cnic_sock *cm_sk)
{
	struct bnx2i_endpoint *ep = (struct bnx2i_endpoint *) cm_sk->context;

	ep->state = EP_STATE_TCP_FIN_RCVD;
	if (ep->conn)
		bnx2i_recovery_que_add_conn(ep->hba, ep->conn);
}

/**
 * bnx2i_cm_remote_abort - process TCP RST and start conn cleanup
 * @hba:		adapter structure pointer
 * @update_kcqe:	kcqe pointer
 *
 * function callback exported via bnx2i - cnic driver interface to
 *	indicate async TCP events (RST) sent by the peer.
 */
static void bnx2i_cm_remote_abort(struct cnic_sock *cm_sk)
{
	struct bnx2i_endpoint *ep = (struct bnx2i_endpoint *) cm_sk->context;
	u32 old_state = ep->state;

	ep->state = EP_STATE_TCP_RST_RCVD;
	if (old_state == EP_STATE_DISCONN_START)
		wake_up_interruptible(&ep->ofld_wait);
	else
		if (ep->conn)
			bnx2i_recovery_que_add_conn(ep->hba, ep->conn);
}


static int bnx2i_send_nl_mesg(void *context, u32 msg_type,
			      char *buf, u16 buflen)
{
	struct bnx2i_hba *hba = context;
	int rc;

	if (!hba)
		return -ENODEV;

	rc = iscsi_offload_mesg(hba->shost, &bnx2i_iscsi_transport,
				msg_type, buf, buflen);
	if (rc)
		printk(KERN_ALERT "bnx2i: private nl message send error\n");

	return rc;
}


/**
 * bnx2i_cnic_cb - global template of bnx2i - cnic driver interface structure
 *			carrying callback function pointers
 *
 */
struct cnic_ulp_ops bnx2i_cnic_cb = {
	.cnic_init = bnx2i_ulp_init,
	.cnic_exit = bnx2i_ulp_exit,
	.cnic_start = bnx2i_start,
	.cnic_stop = bnx2i_stop,
	.indicate_kcqes = bnx2i_indicate_kcqe,
	.indicate_netevent = bnx2i_indicate_netevent,
	.cm_connect_complete = bnx2i_cm_connect_cmpl,
	.cm_close_complete = bnx2i_cm_close_cmpl,
	.cm_abort_complete = bnx2i_cm_abort_cmpl,
	.cm_remote_close = bnx2i_cm_remote_close,
	.cm_remote_abort = bnx2i_cm_remote_abort,
	.iscsi_nl_send_msg = bnx2i_send_nl_mesg,
	.owner = THIS_MODULE
};


/**
 * bnx2i_map_ep_dbell_regs - map connection doorbell registers
 * @ep: bnx2i endpoint
 *
 * maps connection's SQ and RQ doorbell registers, 5706/5708/5709 hosts these
 *	register in BAR #0. Whereas in 57710 these register are accessed by
 *	mapping BAR #1
 */
int bnx2i_map_ep_dbell_regs(struct bnx2i_endpoint *ep)
{
	u32 cid_num;
	u32 reg_off;
	u32 first_l4l5;
	u32 ctx_sz;
	u32 config2;
	resource_size_t reg_base;

	cid_num = bnx2i_get_cid_num(ep);

	if (test_bit(BNX2I_NX2_DEV_57710, &ep->hba->cnic_dev_type)) {
		reg_base = pci_resource_start(ep->hba->pcidev,
					      BNX2X_DOORBELL_PCI_BAR);
		reg_off = BNX2I_5771X_DBELL_PAGE_SIZE * (cid_num & 0x1FFFF) +
			  DPM_TRIGER_TYPE;
		ep->qp.ctx_base = ioremap_nocache(reg_base + reg_off, 4);
		goto arm_cq;
	}

	reg_base = ep->hba->netdev->base_addr;
	if ((test_bit(BNX2I_NX2_DEV_5709, &ep->hba->cnic_dev_type)) &&
	    (ep->hba->mail_queue_access == BNX2I_MQ_BIN_MODE)) {
		config2 = REG_RD(ep->hba, BNX2_MQ_CONFIG2);
		first_l4l5 = config2 & BNX2_MQ_CONFIG2_FIRST_L4L5;
		ctx_sz = (config2 & BNX2_MQ_CONFIG2_CONT_SZ) >> 3;
		if (ctx_sz)
			reg_off = CTX_OFFSET + MAX_CID_CNT * MB_KERNEL_CTX_SIZE
				  + BNX2I_570X_PAGE_SIZE_DEFAULT *
				  (((cid_num - first_l4l5) / ctx_sz) + 256);
		else
			reg_off = CTX_OFFSET + (MB_KERNEL_CTX_SIZE * cid_num);
	} else
		/* 5709 device in normal node and 5706/5708 devices */
		reg_off = CTX_OFFSET + (MB_KERNEL_CTX_SIZE * cid_num);

	ep->qp.ctx_base = ioremap_nocache(reg_base + reg_off,
					  MB_KERNEL_CTX_SIZE);
	if (!ep->qp.ctx_base)
		return -ENOMEM;

arm_cq:
	bnx2i_arm_cq_event_coalescing(ep, CNIC_ARM_CQE);
	return 0;
}

/*
 * QLogic iSCSI Offload Driver
 * Copyright (c) 2016 Cavium Inc.
 *
 * This software is available under the terms of the GNU General Public License
 * (GPL) Version 2, available from the file COPYING in the main directory of
 * this source tree.
 */

#include <linux/blkdev.h>
#include <scsi/scsi_tcq.h>
#include <linux/delay.h>

#include "qedi.h"
#include "qedi_iscsi.h"
#include "qedi_gbl.h"

static int qedi_send_iscsi_tmf(struct qedi_conn *qedi_conn,
			       struct iscsi_task *mtask);

void qedi_iscsi_unmap_sg_list(struct qedi_cmd *cmd)
{
	struct scsi_cmnd *sc = cmd->scsi_cmd;

	if (cmd->io_tbl.sge_valid && sc) {
		cmd->io_tbl.sge_valid = 0;
		scsi_dma_unmap(sc);
	}
}

static void qedi_process_logout_resp(struct qedi_ctx *qedi,
				     union iscsi_cqe *cqe,
				     struct iscsi_task *task,
				     struct qedi_conn *qedi_conn)
{
	struct iscsi_conn *conn = qedi_conn->cls_conn->dd_data;
	struct iscsi_logout_rsp *resp_hdr;
	struct iscsi_session *session = conn->session;
	struct iscsi_logout_response_hdr *cqe_logout_response;
	struct qedi_cmd *cmd;

	cmd = (struct qedi_cmd *)task->dd_data;
	cqe_logout_response = &cqe->cqe_common.iscsi_hdr.logout_response;
	spin_lock(&session->back_lock);
	resp_hdr = (struct iscsi_logout_rsp *)&qedi_conn->gen_pdu.resp_hdr;
	memset(resp_hdr, 0, sizeof(struct iscsi_hdr));
	resp_hdr->opcode = cqe_logout_response->opcode;
	resp_hdr->flags = cqe_logout_response->flags;
	resp_hdr->hlength = 0;

	resp_hdr->itt = build_itt(cqe->cqe_solicited.itid, conn->session->age);
	resp_hdr->statsn = cpu_to_be32(cqe_logout_response->stat_sn);
	resp_hdr->exp_cmdsn = cpu_to_be32(cqe_logout_response->exp_cmd_sn);
	resp_hdr->max_cmdsn = cpu_to_be32(cqe_logout_response->max_cmd_sn);

	resp_hdr->t2wait = cpu_to_be32(cqe_logout_response->time2wait);
	resp_hdr->t2retain = cpu_to_be32(cqe_logout_response->time2retain);

	QEDI_INFO(&qedi->dbg_ctx, QEDI_LOG_TID,
		  "Freeing tid=0x%x for cid=0x%x\n",
		  cmd->task_id, qedi_conn->iscsi_conn_id);

	if (likely(cmd->io_cmd_in_list)) {
		cmd->io_cmd_in_list = false;
		list_del_init(&cmd->io_cmd);
		qedi_conn->active_cmd_count--;
	} else {
		QEDI_INFO(&qedi->dbg_ctx, QEDI_LOG_INFO,
			  "Active cmd list node already deleted, tid=0x%x, cid=0x%x, io_cmd_node=%p\n",
			  cmd->task_id, qedi_conn->iscsi_conn_id,
			  &cmd->io_cmd);
	}

	cmd->state = RESPONSE_RECEIVED;
	qedi_clear_task_idx(qedi, cmd->task_id);
	__iscsi_complete_pdu(conn, (struct iscsi_hdr *)resp_hdr, NULL, 0);

	spin_unlock(&session->back_lock);
}

static void qedi_process_text_resp(struct qedi_ctx *qedi,
				   union iscsi_cqe *cqe,
				   struct iscsi_task *task,
				   struct qedi_conn *qedi_conn)
{
	struct iscsi_conn *conn = qedi_conn->cls_conn->dd_data;
	struct iscsi_session *session = conn->session;
	struct iscsi_task_context *task_ctx;
	struct iscsi_text_rsp *resp_hdr_ptr;
	struct iscsi_text_response_hdr *cqe_text_response;
	struct qedi_cmd *cmd;
	int pld_len;
	u32 *tmp;

	cmd = (struct qedi_cmd *)task->dd_data;
	task_ctx = qedi_get_task_mem(&qedi->tasks, cmd->task_id);

	cqe_text_response = &cqe->cqe_common.iscsi_hdr.text_response;
	spin_lock(&session->back_lock);
	resp_hdr_ptr =  (struct iscsi_text_rsp *)&qedi_conn->gen_pdu.resp_hdr;
	memset(resp_hdr_ptr, 0, sizeof(struct iscsi_hdr));
	resp_hdr_ptr->opcode = cqe_text_response->opcode;
	resp_hdr_ptr->flags = cqe_text_response->flags;
	resp_hdr_ptr->hlength = 0;

	hton24(resp_hdr_ptr->dlength,
	       (cqe_text_response->hdr_second_dword &
		ISCSI_TEXT_RESPONSE_HDR_DATA_SEG_LEN_MASK));
	tmp = (u32 *)resp_hdr_ptr->dlength;

	resp_hdr_ptr->itt = build_itt(cqe->cqe_solicited.itid,
				      conn->session->age);
	resp_hdr_ptr->ttt = cqe_text_response->ttt;
	resp_hdr_ptr->statsn = cpu_to_be32(cqe_text_response->stat_sn);
	resp_hdr_ptr->exp_cmdsn = cpu_to_be32(cqe_text_response->exp_cmd_sn);
	resp_hdr_ptr->max_cmdsn = cpu_to_be32(cqe_text_response->max_cmd_sn);

	pld_len = cqe_text_response->hdr_second_dword &
		  ISCSI_TEXT_RESPONSE_HDR_DATA_SEG_LEN_MASK;
	qedi_conn->gen_pdu.resp_wr_ptr = qedi_conn->gen_pdu.resp_buf + pld_len;

	memset(task_ctx, '\0', sizeof(*task_ctx));

	QEDI_INFO(&qedi->dbg_ctx, QEDI_LOG_TID,
		  "Freeing tid=0x%x for cid=0x%x\n",
		  cmd->task_id, qedi_conn->iscsi_conn_id);

	if (likely(cmd->io_cmd_in_list)) {
		cmd->io_cmd_in_list = false;
		list_del_init(&cmd->io_cmd);
		qedi_conn->active_cmd_count--;
	} else {
		QEDI_INFO(&qedi->dbg_ctx, QEDI_LOG_INFO,
			  "Active cmd list node already deleted, tid=0x%x, cid=0x%x, io_cmd_node=%p\n",
			  cmd->task_id, qedi_conn->iscsi_conn_id,
			  &cmd->io_cmd);
	}

	cmd->state = RESPONSE_RECEIVED;
	qedi_clear_task_idx(qedi, cmd->task_id);

	__iscsi_complete_pdu(conn, (struct iscsi_hdr *)resp_hdr_ptr,
			     qedi_conn->gen_pdu.resp_buf,
			     (qedi_conn->gen_pdu.resp_wr_ptr -
			      qedi_conn->gen_pdu.resp_buf));
	spin_unlock(&session->back_lock);
}

static void qedi_tmf_resp_work(struct work_struct *work)
{
	struct qedi_cmd *qedi_cmd =
				container_of(work, struct qedi_cmd, tmf_work);
	struct qedi_conn *qedi_conn = qedi_cmd->conn;
	struct qedi_ctx *qedi = qedi_conn->qedi;
	struct iscsi_conn *conn = qedi_conn->cls_conn->dd_data;
	struct iscsi_session *session = conn->session;
	struct iscsi_tm_rsp *resp_hdr_ptr;
	struct iscsi_cls_session *cls_sess;
	int rval = 0;

	set_bit(QEDI_CONN_FW_CLEANUP, &qedi_conn->flags);
	resp_hdr_ptr =  (struct iscsi_tm_rsp *)qedi_cmd->tmf_resp_buf;
	cls_sess = iscsi_conn_to_session(qedi_conn->cls_conn);

	iscsi_block_session(session->cls_session);
	rval = qedi_cleanup_all_io(qedi, qedi_conn, qedi_cmd->task, true);
	if (rval) {
		qedi_clear_task_idx(qedi, qedi_cmd->task_id);
		iscsi_unblock_session(session->cls_session);
		goto exit_tmf_resp;
	}

	iscsi_unblock_session(session->cls_session);
	qedi_clear_task_idx(qedi, qedi_cmd->task_id);

	spin_lock(&session->back_lock);
	__iscsi_complete_pdu(conn, (struct iscsi_hdr *)resp_hdr_ptr, NULL, 0);
	spin_unlock(&session->back_lock);

exit_tmf_resp:
	kfree(resp_hdr_ptr);
	clear_bit(QEDI_CONN_FW_CLEANUP, &qedi_conn->flags);
}

static void qedi_process_tmf_resp(struct qedi_ctx *qedi,
				  union iscsi_cqe *cqe,
				  struct iscsi_task *task,
				  struct qedi_conn *qedi_conn)

{
	struct iscsi_conn *conn = qedi_conn->cls_conn->dd_data;
	struct iscsi_session *session = conn->session;
	struct iscsi_tmf_response_hdr *cqe_tmp_response;
	struct iscsi_tm_rsp *resp_hdr_ptr;
	struct iscsi_tm *tmf_hdr;
	struct qedi_cmd *qedi_cmd = NULL;
	u32 *tmp;

	cqe_tmp_response = &cqe->cqe_common.iscsi_hdr.tmf_response;

	qedi_cmd = task->dd_data;
	qedi_cmd->tmf_resp_buf = kzalloc(sizeof(*resp_hdr_ptr), GFP_KERNEL);
	if (!qedi_cmd->tmf_resp_buf) {
		QEDI_ERR(&qedi->dbg_ctx,
			 "Failed to allocate resp buf, cid=0x%x\n",
			  qedi_conn->iscsi_conn_id);
		return;
	}

	spin_lock(&session->back_lock);
	resp_hdr_ptr =  (struct iscsi_tm_rsp *)qedi_cmd->tmf_resp_buf;
	memset(resp_hdr_ptr, 0, sizeof(struct iscsi_tm_rsp));

	/* Fill up the header */
	resp_hdr_ptr->opcode = cqe_tmp_response->opcode;
	resp_hdr_ptr->flags = cqe_tmp_response->hdr_flags;
	resp_hdr_ptr->response = cqe_tmp_response->hdr_response;
	resp_hdr_ptr->hlength = 0;

	hton24(resp_hdr_ptr->dlength,
	       (cqe_tmp_response->hdr_second_dword &
		ISCSI_TMF_RESPONSE_HDR_DATA_SEG_LEN_MASK));
	tmp = (u32 *)resp_hdr_ptr->dlength;
	resp_hdr_ptr->itt = build_itt(cqe->cqe_solicited.itid,
				      conn->session->age);
	resp_hdr_ptr->statsn = cpu_to_be32(cqe_tmp_response->stat_sn);
	resp_hdr_ptr->exp_cmdsn  = cpu_to_be32(cqe_tmp_response->exp_cmd_sn);
	resp_hdr_ptr->max_cmdsn = cpu_to_be32(cqe_tmp_response->max_cmd_sn);

	tmf_hdr = (struct iscsi_tm *)qedi_cmd->task->hdr;

	if (likely(qedi_cmd->io_cmd_in_list)) {
		qedi_cmd->io_cmd_in_list = false;
		list_del_init(&qedi_cmd->io_cmd);
		qedi_conn->active_cmd_count--;
	}

	if (((tmf_hdr->flags & ISCSI_FLAG_TM_FUNC_MASK) ==
	      ISCSI_TM_FUNC_LOGICAL_UNIT_RESET) ||
	    ((tmf_hdr->flags & ISCSI_FLAG_TM_FUNC_MASK) ==
	      ISCSI_TM_FUNC_TARGET_WARM_RESET) ||
	    ((tmf_hdr->flags & ISCSI_FLAG_TM_FUNC_MASK) ==
	      ISCSI_TM_FUNC_TARGET_COLD_RESET)) {
		INIT_WORK(&qedi_cmd->tmf_work, qedi_tmf_resp_work);
		queue_work(qedi->tmf_thread, &qedi_cmd->tmf_work);
		goto unblock_sess;
	}

	qedi_clear_task_idx(qedi, qedi_cmd->task_id);

	__iscsi_complete_pdu(conn, (struct iscsi_hdr *)resp_hdr_ptr, NULL, 0);
	kfree(resp_hdr_ptr);

unblock_sess:
	spin_unlock(&session->back_lock);
}

static void qedi_process_login_resp(struct qedi_ctx *qedi,
				    union iscsi_cqe *cqe,
				    struct iscsi_task *task,
				    struct qedi_conn *qedi_conn)
{
	struct iscsi_conn *conn = qedi_conn->cls_conn->dd_data;
	struct iscsi_session *session = conn->session;
	struct iscsi_task_context *task_ctx;
	struct iscsi_login_rsp *resp_hdr_ptr;
	struct iscsi_login_response_hdr *cqe_login_response;
	struct qedi_cmd *cmd;
	int pld_len;
	u32 *tmp;

	cmd = (struct qedi_cmd *)task->dd_data;

	cqe_login_response = &cqe->cqe_common.iscsi_hdr.login_response;
	task_ctx = qedi_get_task_mem(&qedi->tasks, cmd->task_id);

	spin_lock(&session->back_lock);
	resp_hdr_ptr =  (struct iscsi_login_rsp *)&qedi_conn->gen_pdu.resp_hdr;
	memset(resp_hdr_ptr, 0, sizeof(struct iscsi_login_rsp));
	resp_hdr_ptr->opcode = cqe_login_response->opcode;
	resp_hdr_ptr->flags = cqe_login_response->flags_attr;
	resp_hdr_ptr->hlength = 0;

	hton24(resp_hdr_ptr->dlength,
	       (cqe_login_response->hdr_second_dword &
		ISCSI_LOGIN_RESPONSE_HDR_DATA_SEG_LEN_MASK));
	tmp = (u32 *)resp_hdr_ptr->dlength;
	resp_hdr_ptr->itt = build_itt(cqe->cqe_solicited.itid,
				      conn->session->age);
	resp_hdr_ptr->tsih = cqe_login_response->tsih;
	resp_hdr_ptr->statsn = cpu_to_be32(cqe_login_response->stat_sn);
	resp_hdr_ptr->exp_cmdsn = cpu_to_be32(cqe_login_response->exp_cmd_sn);
	resp_hdr_ptr->max_cmdsn = cpu_to_be32(cqe_login_response->max_cmd_sn);
	resp_hdr_ptr->status_class = cqe_login_response->status_class;
	resp_hdr_ptr->status_detail = cqe_login_response->status_detail;
	pld_len = cqe_login_response->hdr_second_dword &
		  ISCSI_LOGIN_RESPONSE_HDR_DATA_SEG_LEN_MASK;
	qedi_conn->gen_pdu.resp_wr_ptr = qedi_conn->gen_pdu.resp_buf + pld_len;

	if (likely(cmd->io_cmd_in_list)) {
		cmd->io_cmd_in_list = false;
		list_del_init(&cmd->io_cmd);
		qedi_conn->active_cmd_count--;
	}

	memset(task_ctx, '\0', sizeof(*task_ctx));

	__iscsi_complete_pdu(conn, (struct iscsi_hdr *)resp_hdr_ptr,
			     qedi_conn->gen_pdu.resp_buf,
			     (qedi_conn->gen_pdu.resp_wr_ptr -
			     qedi_conn->gen_pdu.resp_buf));

	spin_unlock(&session->back_lock);
	QEDI_INFO(&qedi->dbg_ctx, QEDI_LOG_TID,
		  "Freeing tid=0x%x for cid=0x%x\n",
		  cmd->task_id, qedi_conn->iscsi_conn_id);
	cmd->state = RESPONSE_RECEIVED;
	qedi_clear_task_idx(qedi, cmd->task_id);
}

static void qedi_get_rq_bdq_buf(struct qedi_ctx *qedi,
				struct iscsi_cqe_unsolicited *cqe,
				char *ptr, int len)
{
	u16 idx = 0;

	QEDI_INFO(&qedi->dbg_ctx, QEDI_LOG_CONN,
		  "pld_len [%d], bdq_prod_idx [%d], idx [%d]\n",
		  len, qedi->bdq_prod_idx,
		  (qedi->bdq_prod_idx % qedi->rq_num_entries));

	/* Obtain buffer address from rqe_opaque */
	idx = cqe->rqe_opaque.lo;
	if ((idx < 0) || (idx > (QEDI_BDQ_NUM - 1))) {
		QEDI_INFO(&qedi->dbg_ctx, QEDI_LOG_CONN,
			  "wrong idx %d returned by FW, dropping the unsolicited pkt\n",
			  idx);
		return;
	}

	QEDI_INFO(&qedi->dbg_ctx, QEDI_LOG_CONN,
		  "rqe_opaque.lo [0x%p], rqe_opaque.hi [0x%p], idx [%d]\n",
		  cqe->rqe_opaque.lo, cqe->rqe_opaque.hi, idx);

	QEDI_INFO(&qedi->dbg_ctx, QEDI_LOG_CONN,
		  "unsol_cqe_type = %d\n", cqe->unsol_cqe_type);
	switch (cqe->unsol_cqe_type) {
	case ISCSI_CQE_UNSOLICITED_SINGLE:
	case ISCSI_CQE_UNSOLICITED_FIRST:
		if (len)
			memcpy(ptr, (void *)qedi->bdq[idx].buf_addr, len);
		break;
	case ISCSI_CQE_UNSOLICITED_MIDDLE:
	case ISCSI_CQE_UNSOLICITED_LAST:
		break;
	default:
		break;
	}
}

static void qedi_put_rq_bdq_buf(struct qedi_ctx *qedi,
				struct iscsi_cqe_unsolicited *cqe,
				int count)
{
	u16 tmp;
	u16 idx = 0;
	struct scsi_bd *pbl;

	/* Obtain buffer address from rqe_opaque */
	idx = cqe->rqe_opaque.lo;
	if ((idx < 0) || (idx > (QEDI_BDQ_NUM - 1))) {
		QEDI_INFO(&qedi->dbg_ctx, QEDI_LOG_CONN,
			  "wrong idx %d returned by FW, dropping the unsolicited pkt\n",
			  idx);
		return;
	}

	pbl = (struct scsi_bd *)qedi->bdq_pbl;
	pbl += (qedi->bdq_prod_idx % qedi->rq_num_entries);
	pbl->address.hi = cpu_to_le32(QEDI_U64_HI(qedi->bdq[idx].buf_dma));
	pbl->address.lo = cpu_to_le32(QEDI_U64_LO(qedi->bdq[idx].buf_dma));
	QEDI_INFO(&qedi->dbg_ctx, QEDI_LOG_CONN,
		  "pbl [0x%p] pbl->address hi [0x%llx] lo [0x%llx] idx [%d]\n",
		  pbl, pbl->address.hi, pbl->address.lo, idx);
	pbl->opaque.hi = 0;
	pbl->opaque.lo = cpu_to_le32(QEDI_U64_LO(idx));

	/* Increment producer to let f/w know we've handled the frame */
	qedi->bdq_prod_idx += count;

	writew(qedi->bdq_prod_idx, qedi->bdq_primary_prod);
	tmp = readw(qedi->bdq_primary_prod);

	writew(qedi->bdq_prod_idx, qedi->bdq_secondary_prod);
	tmp = readw(qedi->bdq_secondary_prod);
}

static void qedi_unsol_pdu_adjust_bdq(struct qedi_ctx *qedi,
				      struct iscsi_cqe_unsolicited *cqe,
				      u32 pdu_len, u32 num_bdqs,
				      char *bdq_data)
{
	QEDI_INFO(&qedi->dbg_ctx, QEDI_LOG_CONN,
		  "num_bdqs [%d]\n", num_bdqs);

	qedi_get_rq_bdq_buf(qedi, cqe, bdq_data, pdu_len);
	qedi_put_rq_bdq_buf(qedi, cqe, (num_bdqs + 1));
}

static int qedi_process_nopin_mesg(struct qedi_ctx *qedi,
				   union iscsi_cqe *cqe,
				   struct iscsi_task *task,
				   struct qedi_conn *qedi_conn, u16 que_idx)
{
	struct iscsi_conn *conn = qedi_conn->cls_conn->dd_data;
	struct iscsi_session *session = conn->session;
	struct iscsi_nop_in_hdr *cqe_nop_in;
	struct iscsi_nopin *hdr;
	struct qedi_cmd *cmd;
	int tgt_async_nop = 0;
	u32 lun[2];
	u32 pdu_len, num_bdqs;
	char bdq_data[QEDI_BDQ_BUF_SIZE];
	unsigned long flags;

	spin_lock_bh(&session->back_lock);
	cqe_nop_in = &cqe->cqe_common.iscsi_hdr.nop_in;

	pdu_len = cqe_nop_in->hdr_second_dword &
		  ISCSI_NOP_IN_HDR_DATA_SEG_LEN_MASK;
	num_bdqs = pdu_len / QEDI_BDQ_BUF_SIZE;

	hdr = (struct iscsi_nopin *)&qedi_conn->gen_pdu.resp_hdr;
	memset(hdr, 0, sizeof(struct iscsi_hdr));
	hdr->opcode = cqe_nop_in->opcode;
	hdr->max_cmdsn = cpu_to_be32(cqe_nop_in->max_cmd_sn);
	hdr->exp_cmdsn = cpu_to_be32(cqe_nop_in->exp_cmd_sn);
	hdr->statsn = cpu_to_be32(cqe_nop_in->stat_sn);
	hdr->ttt = cpu_to_be32(cqe_nop_in->ttt);

	if (cqe->cqe_common.cqe_type == ISCSI_CQE_TYPE_UNSOLICITED) {
		spin_lock_irqsave(&qedi->hba_lock, flags);
		qedi_unsol_pdu_adjust_bdq(qedi, &cqe->cqe_unsolicited,
					  pdu_len, num_bdqs, bdq_data);
		hdr->itt = RESERVED_ITT;
		tgt_async_nop = 1;
		spin_unlock_irqrestore(&qedi->hba_lock, flags);
		goto done;
	}

	/* Response to one of our nop-outs */
	if (task) {
		cmd = task->dd_data;
		hdr->flags = ISCSI_FLAG_CMD_FINAL;
		hdr->itt = build_itt(cqe->cqe_solicited.itid,
				     conn->session->age);
		lun[0] = 0xffffffff;
		lun[1] = 0xffffffff;
		memcpy(&hdr->lun, lun, sizeof(struct scsi_lun));
		QEDI_INFO(&qedi->dbg_ctx, QEDI_LOG_TID,
			  "Freeing tid=0x%x for cid=0x%x\n",
			  cmd->task_id, qedi_conn->iscsi_conn_id);
		cmd->state = RESPONSE_RECEIVED;
		spin_lock(&qedi_conn->list_lock);
		if (likely(cmd->io_cmd_in_list)) {
			cmd->io_cmd_in_list = false;
			list_del_init(&cmd->io_cmd);
			qedi_conn->active_cmd_count--;
		}

		spin_unlock(&qedi_conn->list_lock);
		qedi_clear_task_idx(qedi, cmd->task_id);
	}

done:
	__iscsi_complete_pdu(conn, (struct iscsi_hdr *)hdr, bdq_data, pdu_len);

	spin_unlock_bh(&session->back_lock);
	return tgt_async_nop;
}

static void qedi_process_async_mesg(struct qedi_ctx *qedi,
				    union iscsi_cqe *cqe,
				    struct iscsi_task *task,
				    struct qedi_conn *qedi_conn,
				    u16 que_idx)
{
	struct iscsi_conn *conn = qedi_conn->cls_conn->dd_data;
	struct iscsi_session *session = conn->session;
	struct iscsi_async_msg_hdr *cqe_async_msg;
	struct iscsi_async *resp_hdr;
	u32 lun[2];
	u32 pdu_len, num_bdqs;
	char bdq_data[QEDI_BDQ_BUF_SIZE];
	unsigned long flags;

	spin_lock_bh(&session->back_lock);

	cqe_async_msg = &cqe->cqe_common.iscsi_hdr.async_msg;
	pdu_len = cqe_async_msg->hdr_second_dword &
		ISCSI_ASYNC_MSG_HDR_DATA_SEG_LEN_MASK;
	num_bdqs = pdu_len / QEDI_BDQ_BUF_SIZE;

	if (cqe->cqe_common.cqe_type == ISCSI_CQE_TYPE_UNSOLICITED) {
		spin_lock_irqsave(&qedi->hba_lock, flags);
		qedi_unsol_pdu_adjust_bdq(qedi, &cqe->cqe_unsolicited,
					  pdu_len, num_bdqs, bdq_data);
		spin_unlock_irqrestore(&qedi->hba_lock, flags);
	}

	resp_hdr = (struct iscsi_async *)&qedi_conn->gen_pdu.resp_hdr;
	memset(resp_hdr, 0, sizeof(struct iscsi_hdr));
	resp_hdr->opcode = cqe_async_msg->opcode;
	resp_hdr->flags = 0x80;

	lun[0] = cpu_to_be32(cqe_async_msg->lun.lo);
	lun[1] = cpu_to_be32(cqe_async_msg->lun.hi);
	memcpy(&resp_hdr->lun, lun, sizeof(struct scsi_lun));
	resp_hdr->exp_cmdsn = cpu_to_be32(cqe_async_msg->exp_cmd_sn);
	resp_hdr->max_cmdsn = cpu_to_be32(cqe_async_msg->max_cmd_sn);
	resp_hdr->statsn = cpu_to_be32(cqe_async_msg->stat_sn);

	resp_hdr->async_event = cqe_async_msg->async_event;
	resp_hdr->async_vcode = cqe_async_msg->async_vcode;

	resp_hdr->param1 = cpu_to_be16(cqe_async_msg->param1_rsrv);
	resp_hdr->param2 = cpu_to_be16(cqe_async_msg->param2_rsrv);
	resp_hdr->param3 = cpu_to_be16(cqe_async_msg->param3_rsrv);

	__iscsi_complete_pdu(conn, (struct iscsi_hdr *)resp_hdr, bdq_data,
			     pdu_len);

	spin_unlock_bh(&session->back_lock);
}

static void qedi_process_reject_mesg(struct qedi_ctx *qedi,
				     union iscsi_cqe *cqe,
				     struct iscsi_task *task,
				     struct qedi_conn *qedi_conn,
				     uint16_t que_idx)
{
	struct iscsi_conn *conn = qedi_conn->cls_conn->dd_data;
	struct iscsi_session *session = conn->session;
	struct iscsi_reject_hdr *cqe_reject;
	struct iscsi_reject *hdr;
	u32 pld_len, num_bdqs;
	unsigned long flags;

	spin_lock_bh(&session->back_lock);
	cqe_reject = &cqe->cqe_common.iscsi_hdr.reject;
	pld_len = cqe_reject->hdr_second_dword &
		  ISCSI_REJECT_HDR_DATA_SEG_LEN_MASK;
	num_bdqs = pld_len / QEDI_BDQ_BUF_SIZE;

	if (cqe->cqe_common.cqe_type == ISCSI_CQE_TYPE_UNSOLICITED) {
		spin_lock_irqsave(&qedi->hba_lock, flags);
		qedi_unsol_pdu_adjust_bdq(qedi, &cqe->cqe_unsolicited,
					  pld_len, num_bdqs, conn->data);
		spin_unlock_irqrestore(&qedi->hba_lock, flags);
	}
	hdr = (struct iscsi_reject *)&qedi_conn->gen_pdu.resp_hdr;
	memset(hdr, 0, sizeof(struct iscsi_hdr));
	hdr->opcode = cqe_reject->opcode;
	hdr->reason = cqe_reject->hdr_reason;
	hdr->flags = cqe_reject->hdr_flags;
	hton24(hdr->dlength, (cqe_reject->hdr_second_dword &
			      ISCSI_REJECT_HDR_DATA_SEG_LEN_MASK));
	hdr->max_cmdsn = cpu_to_be32(cqe_reject->max_cmd_sn);
	hdr->exp_cmdsn = cpu_to_be32(cqe_reject->exp_cmd_sn);
	hdr->statsn = cpu_to_be32(cqe_reject->stat_sn);
	hdr->ffffffff = cpu_to_be32(0xffffffff);

	__iscsi_complete_pdu(conn, (struct iscsi_hdr *)hdr,
			     conn->data, pld_len);
	spin_unlock_bh(&session->back_lock);
}

static void qedi_scsi_completion(struct qedi_ctx *qedi,
				 union iscsi_cqe *cqe,
				 struct iscsi_task *task,
				 struct iscsi_conn *conn)
{
	struct scsi_cmnd *sc_cmd;
	struct qedi_cmd *cmd = task->dd_data;
	struct iscsi_session *session = conn->session;
	struct iscsi_scsi_rsp *hdr;
	struct iscsi_data_in_hdr *cqe_data_in;
	int datalen = 0;
	struct qedi_conn *qedi_conn;
	u32 iscsi_cid;
	bool mark_cmd_node_deleted = false;
	u8 cqe_err_bits = 0;

	iscsi_cid  = cqe->cqe_common.conn_id;
	qedi_conn = qedi->cid_que.conn_cid_tbl[iscsi_cid];

	cqe_data_in = &cqe->cqe_common.iscsi_hdr.data_in;
	cqe_err_bits =
		cqe->cqe_common.error_bitmap.error_bits.cqe_error_status_bits;

	spin_lock_bh(&session->back_lock);
	/* get the scsi command */
	sc_cmd = cmd->scsi_cmd;

	if (!sc_cmd) {
		QEDI_WARN(&qedi->dbg_ctx, "sc_cmd is NULL!\n");
		goto error;
	}

	if (!sc_cmd->SCp.ptr) {
		QEDI_WARN(&qedi->dbg_ctx,
			  "SCp.ptr is NULL, returned in another context.\n");
		goto error;
	}

	if (!sc_cmd->request) {
		QEDI_WARN(&qedi->dbg_ctx,
			  "sc_cmd->request is NULL, sc_cmd=%p.\n",
			  sc_cmd);
		goto error;
	}

	if (!sc_cmd->request->special) {
		QEDI_WARN(&qedi->dbg_ctx,
			  "request->special is NULL so request not valid, sc_cmd=%p.\n",
			  sc_cmd);
		goto error;
	}

	if (!sc_cmd->request->q) {
		QEDI_WARN(&qedi->dbg_ctx,
			  "request->q is NULL so request is not valid, sc_cmd=%p.\n",
			  sc_cmd);
		goto error;
	}

	qedi_iscsi_unmap_sg_list(cmd);

	hdr = (struct iscsi_scsi_rsp *)task->hdr;
	hdr->opcode = cqe_data_in->opcode;
	hdr->max_cmdsn = cpu_to_be32(cqe_data_in->max_cmd_sn);
	hdr->exp_cmdsn = cpu_to_be32(cqe_data_in->exp_cmd_sn);
	hdr->itt = build_itt(cqe->cqe_solicited.itid, conn->session->age);
	hdr->response = cqe_data_in->reserved1;
	hdr->cmd_status = cqe_data_in->status_rsvd;
	hdr->flags = cqe_data_in->flags;
	hdr->residual_count = cpu_to_be32(cqe_data_in->residual_count);

	if (hdr->cmd_status == SAM_STAT_CHECK_CONDITION) {
		datalen = cqe_data_in->reserved2 &
			  ISCSI_COMMON_HDR_DATA_SEG_LEN_MASK;
		memcpy((char *)conn->data, (char *)cmd->sense_buffer, datalen);
	}

	/* If f/w reports data underrun err then set residual to IO transfer
	 * length, set Underrun flag and clear Overrun flag explicitly
	 */
	if (unlikely(cqe_err_bits &&
		     GET_FIELD(cqe_err_bits, CQE_ERROR_BITMAP_UNDER_RUN_ERR))) {
		QEDI_INFO(&qedi->dbg_ctx, QEDI_LOG_INFO,
			  "Under flow itt=0x%x proto flags=0x%x tid=0x%x cid 0x%x fw resid 0x%x sc dlen 0x%x\n",
			  hdr->itt, cqe_data_in->flags, cmd->task_id,
			  qedi_conn->iscsi_conn_id, hdr->residual_count,
			  scsi_bufflen(sc_cmd));
		hdr->residual_count = cpu_to_be32(scsi_bufflen(sc_cmd));
		hdr->flags |= ISCSI_FLAG_CMD_UNDERFLOW;
		hdr->flags &= (~ISCSI_FLAG_CMD_OVERFLOW);
	}

	spin_lock(&qedi_conn->list_lock);
	if (likely(cmd->io_cmd_in_list)) {
		cmd->io_cmd_in_list = false;
		list_del_init(&cmd->io_cmd);
		qedi_conn->active_cmd_count--;
		mark_cmd_node_deleted = true;
	}
	spin_unlock(&qedi_conn->list_lock);

	QEDI_INFO(&qedi->dbg_ctx, QEDI_LOG_TID,
		  "Freeing tid=0x%x for cid=0x%x\n",
		  cmd->task_id, qedi_conn->iscsi_conn_id);
	cmd->state = RESPONSE_RECEIVED;
	if (qedi_io_tracing)
		qedi_trace_io(qedi, task, cmd->task_id, QEDI_IO_TRACE_RSP);

	qedi_clear_task_idx(qedi, cmd->task_id);
	__iscsi_complete_pdu(conn, (struct iscsi_hdr *)hdr,
			     conn->data, datalen);
error:
	spin_unlock_bh(&session->back_lock);
}

static void qedi_mtask_completion(struct qedi_ctx *qedi,
				  union iscsi_cqe *cqe,
				  struct iscsi_task *task,
				  struct qedi_conn *conn, uint16_t que_idx)
{
	struct iscsi_conn *iscsi_conn;
	u32 hdr_opcode;

	hdr_opcode = cqe->cqe_common.iscsi_hdr.common.hdr_first_byte;
	iscsi_conn = conn->cls_conn->dd_data;

	switch (hdr_opcode) {
	case ISCSI_OPCODE_SCSI_RESPONSE:
	case ISCSI_OPCODE_DATA_IN:
		qedi_scsi_completion(qedi, cqe, task, iscsi_conn);
		break;
	case ISCSI_OPCODE_LOGIN_RESPONSE:
		qedi_process_login_resp(qedi, cqe, task, conn);
		break;
	case ISCSI_OPCODE_TMF_RESPONSE:
		qedi_process_tmf_resp(qedi, cqe, task, conn);
		break;
	case ISCSI_OPCODE_TEXT_RESPONSE:
		qedi_process_text_resp(qedi, cqe, task, conn);
		break;
	case ISCSI_OPCODE_LOGOUT_RESPONSE:
		qedi_process_logout_resp(qedi, cqe, task, conn);
		break;
	case ISCSI_OPCODE_NOP_IN:
		qedi_process_nopin_mesg(qedi, cqe, task, conn, que_idx);
		break;
	default:
		QEDI_ERR(&qedi->dbg_ctx, "unknown opcode\n");
	}
}

static void qedi_process_nopin_local_cmpl(struct qedi_ctx *qedi,
					  struct iscsi_cqe_solicited *cqe,
					  struct iscsi_task *task,
					  struct qedi_conn *qedi_conn)
{
	struct iscsi_conn *conn = qedi_conn->cls_conn->dd_data;
	struct iscsi_session *session = conn->session;
	struct qedi_cmd *cmd = task->dd_data;

	QEDI_INFO(&qedi->dbg_ctx, QEDI_LOG_UNSOL,
		  "itid=0x%x, cmd task id=0x%x\n",
		  cqe->itid, cmd->task_id);

	cmd->state = RESPONSE_RECEIVED;
	qedi_clear_task_idx(qedi, cmd->task_id);

	spin_lock_bh(&session->back_lock);
	__iscsi_put_task(task);
	spin_unlock_bh(&session->back_lock);
}

static void qedi_process_cmd_cleanup_resp(struct qedi_ctx *qedi,
					  struct iscsi_cqe_solicited *cqe,
					  struct iscsi_task *task,
					  struct iscsi_conn *conn)
{
	struct qedi_work_map *work, *work_tmp;
	u32 proto_itt = cqe->itid;
	u32 ptmp_itt = 0;
	itt_t protoitt = 0;
	int found = 0;
	struct qedi_cmd *qedi_cmd = NULL;
	u32 rtid = 0;
	u32 iscsi_cid;
	struct qedi_conn *qedi_conn;
	struct qedi_cmd *cmd_new, *dbg_cmd;
	struct iscsi_task *mtask;
	struct iscsi_tm *tmf_hdr = NULL;

	iscsi_cid = cqe->conn_id;
	qedi_conn = qedi->cid_que.conn_cid_tbl[iscsi_cid];

	/* Based on this itt get the corresponding qedi_cmd */
	spin_lock_bh(&qedi_conn->tmf_work_lock);
	list_for_each_entry_safe(work, work_tmp, &qedi_conn->tmf_work_list,
				 list) {
		if (work->rtid == proto_itt) {
			/* We found the command */
			qedi_cmd = work->qedi_cmd;
			if (!qedi_cmd->list_tmf_work) {
				QEDI_INFO(&qedi->dbg_ctx, QEDI_LOG_SCSI_TM,
					  "TMF work not found, cqe->tid=0x%x, cid=0x%x\n",
					  proto_itt, qedi_conn->iscsi_conn_id);
				WARN_ON(1);
			}
			found = 1;
			mtask = qedi_cmd->task;
			tmf_hdr = (struct iscsi_tm *)mtask->hdr;
			rtid = work->rtid;

			list_del_init(&work->list);
			kfree(work);
			qedi_cmd->list_tmf_work = NULL;
		}
	}
	spin_unlock_bh(&qedi_conn->tmf_work_lock);

	if (found) {
		QEDI_INFO(&qedi->dbg_ctx, QEDI_LOG_SCSI_TM,
			  "TMF work, cqe->tid=0x%x, tmf flags=0x%x, cid=0x%x\n",
			  proto_itt, tmf_hdr->flags, qedi_conn->iscsi_conn_id);

		if ((tmf_hdr->flags & ISCSI_FLAG_TM_FUNC_MASK) ==
		    ISCSI_TM_FUNC_ABORT_TASK) {
			spin_lock_bh(&conn->session->back_lock);

			protoitt = build_itt(get_itt(tmf_hdr->rtt),
					     conn->session->age);
			task = iscsi_itt_to_task(conn, protoitt);

			spin_unlock_bh(&conn->session->back_lock);

			if (!task) {
				QEDI_NOTICE(&qedi->dbg_ctx,
					    "IO task completed, tmf rtt=0x%x, cid=0x%x\n",
					    get_itt(tmf_hdr->rtt),
					    qedi_conn->iscsi_conn_id);
				return;
			}

			dbg_cmd = task->dd_data;

			QEDI_INFO(&qedi->dbg_ctx, QEDI_LOG_SCSI_TM,
				  "Abort tmf rtt=0x%x, i/o itt=0x%x, i/o tid=0x%x, cid=0x%x\n",
				  get_itt(tmf_hdr->rtt), get_itt(task->itt),
				  dbg_cmd->task_id, qedi_conn->iscsi_conn_id);

			if (qedi_cmd->state == CLEANUP_WAIT_FAILED)
				qedi_cmd->state = CLEANUP_RECV;

			qedi_clear_task_idx(qedi_conn->qedi, rtid);

			spin_lock(&qedi_conn->list_lock);
			list_del_init(&dbg_cmd->io_cmd);
			qedi_conn->active_cmd_count--;
			spin_unlock(&qedi_conn->list_lock);
			qedi_cmd->state = CLEANUP_RECV;
			wake_up_interruptible(&qedi_conn->wait_queue);
		}
	} else if (qedi_conn->cmd_cleanup_req > 0) {
		spin_lock_bh(&conn->session->back_lock);
		qedi_get_proto_itt(qedi, cqe->itid, &ptmp_itt);
		protoitt = build_itt(ptmp_itt, conn->session->age);
		task = iscsi_itt_to_task(conn, protoitt);
		QEDI_INFO(&qedi->dbg_ctx, QEDI_LOG_SCSI_TM,
			  "cleanup io itid=0x%x, protoitt=0x%x, cmd_cleanup_cmpl=%d, cid=0x%x\n",
			  cqe->itid, protoitt, qedi_conn->cmd_cleanup_cmpl,
			  qedi_conn->iscsi_conn_id);

		spin_unlock_bh(&conn->session->back_lock);
		if (!task) {
			QEDI_NOTICE(&qedi->dbg_ctx,
				    "task is null, itid=0x%x, cid=0x%x\n",
				    cqe->itid, qedi_conn->iscsi_conn_id);
			return;
		}
		qedi_conn->cmd_cleanup_cmpl++;
		wake_up(&qedi_conn->wait_queue);
		cmd_new = task->dd_data;

		QEDI_INFO(&qedi->dbg_ctx, QEDI_LOG_TID,
			  "Freeing tid=0x%x for cid=0x%x\n",
			  cqe->itid, qedi_conn->iscsi_conn_id);
		qedi_clear_task_idx(qedi_conn->qedi, cqe->itid);

	} else {
		qedi_get_proto_itt(qedi, cqe->itid, &ptmp_itt);
		protoitt = build_itt(ptmp_itt, conn->session->age);
		task = iscsi_itt_to_task(conn, protoitt);
		QEDI_ERR(&qedi->dbg_ctx,
			 "Delayed or untracked cleanup response, itt=0x%x, tid=0x%x, cid=0x%x, task=%p\n",
			 protoitt, cqe->itid, qedi_conn->iscsi_conn_id, task);
		WARN_ON(1);
	}
}

void qedi_fp_process_cqes(struct qedi_work *work)
{
	struct qedi_ctx *qedi = work->qedi;
	union iscsi_cqe *cqe = &work->cqe;
	struct iscsi_task *task = NULL;
	struct iscsi_nopout *nopout_hdr;
	struct qedi_conn *q_conn;
	struct iscsi_conn *conn;
	struct qedi_cmd *qedi_cmd;
	u32 comp_type;
	u32 iscsi_cid;
	u32 hdr_opcode;
	u16 que_idx = work->que_idx;
	u8 cqe_err_bits = 0;

	comp_type = cqe->cqe_common.cqe_type;
	hdr_opcode = cqe->cqe_common.iscsi_hdr.common.hdr_first_byte;
	cqe_err_bits =
		cqe->cqe_common.error_bitmap.error_bits.cqe_error_status_bits;

	QEDI_INFO(&qedi->dbg_ctx, QEDI_LOG_CONN,
		  "fw_cid=0x%x, cqe type=0x%x, opcode=0x%x\n",
		  cqe->cqe_common.conn_id, comp_type, hdr_opcode);

	if (comp_type >= MAX_ISCSI_CQES_TYPE) {
		QEDI_WARN(&qedi->dbg_ctx, "Invalid CqE type\n");
		return;
	}

	iscsi_cid  = cqe->cqe_common.conn_id;
	q_conn = qedi->cid_que.conn_cid_tbl[iscsi_cid];
	if (!q_conn) {
		QEDI_WARN(&qedi->dbg_ctx,
			  "Session no longer exists for cid=0x%x!!\n",
			  iscsi_cid);
		return;
	}

	conn = q_conn->cls_conn->dd_data;

	if (unlikely(cqe_err_bits &&
		     GET_FIELD(cqe_err_bits,
			       CQE_ERROR_BITMAP_DATA_DIGEST_ERR))) {
		iscsi_conn_failure(conn, ISCSI_ERR_DATA_DGST);
		return;
	}

	switch (comp_type) {
	case ISCSI_CQE_TYPE_SOLICITED:
	case ISCSI_CQE_TYPE_SOLICITED_WITH_SENSE:
		qedi_cmd = container_of(work, struct qedi_cmd, cqe_work);
		task = qedi_cmd->task;
		if (!task) {
			QEDI_WARN(&qedi->dbg_ctx, "task is NULL\n");
			return;
		}

		/* Process NOPIN local completion */
		nopout_hdr = (struct iscsi_nopout *)task->hdr;
		if ((nopout_hdr->itt == RESERVED_ITT) &&
		    (cqe->cqe_solicited.itid != (u16)RESERVED_ITT)) {
			qedi_process_nopin_local_cmpl(qedi, &cqe->cqe_solicited,
						      task, q_conn);
		} else {
			cqe->cqe_solicited.itid =
					       qedi_get_itt(cqe->cqe_solicited);
			/* Process other solicited responses */
			qedi_mtask_completion(qedi, cqe, task, q_conn, que_idx);
		}
		break;
	case ISCSI_CQE_TYPE_UNSOLICITED:
		switch (hdr_opcode) {
		case ISCSI_OPCODE_NOP_IN:
			qedi_process_nopin_mesg(qedi, cqe, task, q_conn,
						que_idx);
			break;
		case ISCSI_OPCODE_ASYNC_MSG:
			qedi_process_async_mesg(qedi, cqe, task, q_conn,
						que_idx);
			break;
		case ISCSI_OPCODE_REJECT:
			qedi_process_reject_mesg(qedi, cqe, task, q_conn,
						 que_idx);
			break;
		}
		goto exit_fp_process;
	case ISCSI_CQE_TYPE_DUMMY:
		QEDI_INFO(&qedi->dbg_ctx, QEDI_LOG_SCSI_TM, "Dummy CqE\n");
		goto exit_fp_process;
	case ISCSI_CQE_TYPE_TASK_CLEANUP:
		QEDI_INFO(&qedi->dbg_ctx, QEDI_LOG_SCSI_TM, "CleanUp CqE\n");
		qedi_process_cmd_cleanup_resp(qedi, &cqe->cqe_solicited, task,
					      conn);
		goto exit_fp_process;
	default:
		QEDI_ERR(&qedi->dbg_ctx, "Error cqe.\n");
		break;
	}

exit_fp_process:
	return;
}

static void qedi_add_to_sq(struct qedi_conn *qedi_conn, struct iscsi_task *task,
			   u16 tid, uint16_t ptu_invalidate, int is_cleanup)
{
	struct iscsi_wqe *wqe;
	struct iscsi_wqe_field *cont_field;
	struct qedi_endpoint *ep;
	struct scsi_cmnd *sc = task->sc;
	struct iscsi_login_req *login_hdr;
	struct qedi_cmd *cmd = task->dd_data;

	login_hdr = (struct iscsi_login_req *)task->hdr;
	ep = qedi_conn->ep;
	wqe = &ep->sq[ep->sq_prod_idx];

	memset(wqe, 0, sizeof(*wqe));

	ep->sq_prod_idx++;
	ep->fw_sq_prod_idx++;
	if (ep->sq_prod_idx == QEDI_SQ_SIZE)
		ep->sq_prod_idx = 0;

	if (is_cleanup) {
		SET_FIELD(wqe->flags, ISCSI_WQE_WQE_TYPE,
			  ISCSI_WQE_TYPE_TASK_CLEANUP);
		wqe->task_id = tid;
		return;
	}

	if (ptu_invalidate) {
		SET_FIELD(wqe->flags, ISCSI_WQE_PTU_INVALIDATE,
			  ISCSI_WQE_SET_PTU_INVALIDATE);
	}

	cont_field = &wqe->cont_prevtid_union.cont_field;

	switch (task->hdr->opcode & ISCSI_OPCODE_MASK) {
	case ISCSI_OP_LOGIN:
	case ISCSI_OP_TEXT:
		SET_FIELD(wqe->flags, ISCSI_WQE_WQE_TYPE,
			  ISCSI_WQE_TYPE_MIDDLE_PATH);
		SET_FIELD(wqe->flags, ISCSI_WQE_NUM_FAST_SGES,
			  1);
		cont_field->contlen_cdbsize_field = ntoh24(login_hdr->dlength);
		break;
	case ISCSI_OP_LOGOUT:
	case ISCSI_OP_NOOP_OUT:
	case ISCSI_OP_SCSI_TMFUNC:
		 SET_FIELD(wqe->flags, ISCSI_WQE_WQE_TYPE,
			   ISCSI_WQE_TYPE_NORMAL);
		break;
	default:
		if (!sc)
			break;

		SET_FIELD(wqe->flags, ISCSI_WQE_WQE_TYPE,
			  ISCSI_WQE_TYPE_NORMAL);
		cont_field->contlen_cdbsize_field =
				(sc->sc_data_direction == DMA_TO_DEVICE) ?
				scsi_bufflen(sc) : 0;
		if (cmd->use_slowpath)
			SET_FIELD(wqe->flags, ISCSI_WQE_NUM_FAST_SGES, 0);
		else
			SET_FIELD(wqe->flags, ISCSI_WQE_NUM_FAST_SGES,
				  (sc->sc_data_direction ==
				   DMA_TO_DEVICE) ?
				  min((u16)QEDI_FAST_SGE_COUNT,
				      (u16)cmd->io_tbl.sge_valid) : 0);
		break;
	}

	wqe->task_id = tid;
	/* Make sure SQ data is coherent */
	wmb();
}

static void qedi_ring_doorbell(struct qedi_conn *qedi_conn)
{
	struct iscsi_db_data dbell = { 0 };

	dbell.agg_flags = 0;

	dbell.params |= DB_DEST_XCM << ISCSI_DB_DATA_DEST_SHIFT;
	dbell.params |= DB_AGG_CMD_SET << ISCSI_DB_DATA_AGG_CMD_SHIFT;
	dbell.params |=
		   DQ_XCM_ISCSI_SQ_PROD_CMD << ISCSI_DB_DATA_AGG_VAL_SEL_SHIFT;

	dbell.sq_prod = qedi_conn->ep->fw_sq_prod_idx;
	writel(*(u32 *)&dbell, qedi_conn->ep->p_doorbell);

	/* Make sure fw write idx is coherent, and include both memory barriers
	 * as a failsafe as for some architectures the call is the same but on
	 * others they are two different assembly operations.
	 */
	wmb();
	mmiowb();
	QEDI_INFO(&qedi_conn->qedi->dbg_ctx, QEDI_LOG_MP_REQ,
		  "prod_idx=0x%x, fw_prod_idx=0x%x, cid=0x%x\n",
		  qedi_conn->ep->sq_prod_idx, qedi_conn->ep->fw_sq_prod_idx,
		  qedi_conn->iscsi_conn_id);
}

int qedi_send_iscsi_login(struct qedi_conn *qedi_conn,
			  struct iscsi_task *task)
{
	struct qedi_ctx *qedi = qedi_conn->qedi;
	struct iscsi_task_context *fw_task_ctx;
	struct iscsi_login_req *login_hdr;
	struct iscsi_login_req_hdr *fw_login_req = NULL;
	struct iscsi_cached_sge_ctx *cached_sge = NULL;
	struct iscsi_sge *single_sge = NULL;
	struct iscsi_sge *req_sge = NULL;
	struct iscsi_sge *resp_sge = NULL;
	struct qedi_cmd *qedi_cmd;
	s16 ptu_invalidate = 0;
	s16 tid = 0;

	req_sge = (struct iscsi_sge *)qedi_conn->gen_pdu.req_bd_tbl;
	resp_sge = (struct iscsi_sge *)qedi_conn->gen_pdu.resp_bd_tbl;
	qedi_cmd = (struct qedi_cmd *)task->dd_data;
	login_hdr = (struct iscsi_login_req *)task->hdr;

	tid = qedi_get_task_idx(qedi);
	if (tid == -1)
		return -ENOMEM;

	fw_task_ctx = qedi_get_task_mem(&qedi->tasks, tid);
	memset(fw_task_ctx, 0, sizeof(struct iscsi_task_context));

	qedi_cmd->task_id = tid;

	/* Ystorm context */
	fw_login_req = &fw_task_ctx->ystorm_st_context.pdu_hdr.login_req;
	fw_login_req->opcode = login_hdr->opcode;
	fw_login_req->version_min = login_hdr->min_version;
	fw_login_req->version_max = login_hdr->max_version;
	fw_login_req->flags_attr = login_hdr->flags;
	fw_login_req->isid_tabc = *((u16 *)login_hdr->isid + 2);
	fw_login_req->isid_d = *((u32 *)login_hdr->isid);
	fw_login_req->tsih = login_hdr->tsih;
	qedi_update_itt_map(qedi, tid, task->itt, qedi_cmd);
	fw_login_req->itt = qedi_set_itt(tid, get_itt(task->itt));
	fw_login_req->cid = qedi_conn->iscsi_conn_id;
	fw_login_req->cmd_sn = be32_to_cpu(login_hdr->cmdsn);
	fw_login_req->exp_stat_sn = be32_to_cpu(login_hdr->exp_statsn);
	fw_login_req->exp_stat_sn = 0;

	if (qedi->tid_reuse_count[tid] == QEDI_MAX_TASK_NUM) {
		ptu_invalidate = 1;
		qedi->tid_reuse_count[tid] = 0;
	}

	fw_task_ctx->ystorm_st_context.state.reuse_count =
						qedi->tid_reuse_count[tid];
	fw_task_ctx->mstorm_st_context.reuse_count =
						qedi->tid_reuse_count[tid]++;
	cached_sge =
	       &fw_task_ctx->ystorm_st_context.state.sgl_ctx_union.cached_sge;
	cached_sge->sge.sge_len = req_sge->sge_len;
	cached_sge->sge.sge_addr.lo = (u32)(qedi_conn->gen_pdu.req_dma_addr);
	cached_sge->sge.sge_addr.hi =
			     (u32)((u64)qedi_conn->gen_pdu.req_dma_addr >> 32);

	/* Mstorm context */
	single_sge = &fw_task_ctx->mstorm_st_context.sgl_union.single_sge;
	fw_task_ctx->mstorm_st_context.task_type = 0x2;
	fw_task_ctx->mstorm_ag_context.task_cid = (u16)qedi_conn->iscsi_conn_id;
	single_sge->sge_addr.lo = resp_sge->sge_addr.lo;
	single_sge->sge_addr.hi = resp_sge->sge_addr.hi;
	single_sge->sge_len = resp_sge->sge_len;

	SET_FIELD(fw_task_ctx->mstorm_st_context.flags.mflags,
		  ISCSI_MFLAGS_SINGLE_SGE, 1);
	SET_FIELD(fw_task_ctx->mstorm_st_context.flags.mflags,
		  ISCSI_MFLAGS_SLOW_IO, 0);
	fw_task_ctx->mstorm_st_context.sgl_size = 1;
	fw_task_ctx->mstorm_st_context.rem_task_size = resp_sge->sge_len;

	/* Ustorm context */
	fw_task_ctx->ustorm_st_context.rem_rcv_len = resp_sge->sge_len;
	fw_task_ctx->ustorm_st_context.exp_data_transfer_len =
						ntoh24(login_hdr->dlength);
	fw_task_ctx->ustorm_st_context.exp_data_sn = 0;
	fw_task_ctx->ustorm_st_context.cq_rss_number = 0;
	fw_task_ctx->ustorm_st_context.task_type = 0x2;
	fw_task_ctx->ustorm_ag_context.icid = (u16)qedi_conn->iscsi_conn_id;
	fw_task_ctx->ustorm_ag_context.exp_data_acked =
						 ntoh24(login_hdr->dlength);
	SET_FIELD(fw_task_ctx->ustorm_ag_context.flags1,
		  USTORM_ISCSI_TASK_AG_CTX_R2T2RECV, 1);
	SET_FIELD(fw_task_ctx->ustorm_st_context.flags,
		  USTORM_ISCSI_TASK_ST_CTX_LOCAL_COMP, 0);

	spin_lock(&qedi_conn->list_lock);
	list_add_tail(&qedi_cmd->io_cmd, &qedi_conn->active_cmd_list);
	qedi_cmd->io_cmd_in_list = true;
	qedi_conn->active_cmd_count++;
	spin_unlock(&qedi_conn->list_lock);

	qedi_add_to_sq(qedi_conn, task, tid, ptu_invalidate, false);
	qedi_ring_doorbell(qedi_conn);
	return 0;
}

int qedi_send_iscsi_logout(struct qedi_conn *qedi_conn,
			   struct iscsi_task *task)
{
	struct qedi_ctx *qedi = qedi_conn->qedi;
	struct iscsi_logout_req_hdr *fw_logout_req = NULL;
	struct iscsi_task_context *fw_task_ctx = NULL;
	struct iscsi_logout *logout_hdr = NULL;
	struct qedi_cmd *qedi_cmd = NULL;
	s16  tid = 0;
	s16 ptu_invalidate = 0;

	qedi_cmd = (struct qedi_cmd *)task->dd_data;
	logout_hdr = (struct iscsi_logout *)task->hdr;

	tid = qedi_get_task_idx(qedi);
	if (tid == -1)
		return -ENOMEM;

	fw_task_ctx = qedi_get_task_mem(&qedi->tasks, tid);

	memset(fw_task_ctx, 0, sizeof(struct iscsi_task_context));
	qedi_cmd->task_id = tid;

	/* Ystorm context */
	fw_logout_req = &fw_task_ctx->ystorm_st_context.pdu_hdr.logout_req;
	fw_logout_req->opcode = ISCSI_OPCODE_LOGOUT_REQUEST;
	fw_logout_req->reason_code = 0x80 | logout_hdr->flags;
	qedi_update_itt_map(qedi, tid, task->itt, qedi_cmd);
	fw_logout_req->itt = qedi_set_itt(tid, get_itt(task->itt));
	fw_logout_req->exp_stat_sn = be32_to_cpu(logout_hdr->exp_statsn);
	fw_logout_req->cmd_sn = be32_to_cpu(logout_hdr->cmdsn);

	if (qedi->tid_reuse_count[tid] == QEDI_MAX_TASK_NUM) {
		ptu_invalidate = 1;
		qedi->tid_reuse_count[tid] = 0;
	}
	fw_task_ctx->ystorm_st_context.state.reuse_count =
						  qedi->tid_reuse_count[tid];
	fw_task_ctx->mstorm_st_context.reuse_count =
						qedi->tid_reuse_count[tid]++;
	fw_logout_req->cid = qedi_conn->iscsi_conn_id;
	fw_task_ctx->ystorm_st_context.state.buffer_offset[0] = 0;

	/* Mstorm context */
	fw_task_ctx->mstorm_st_context.task_type = ISCSI_TASK_TYPE_MIDPATH;
	fw_task_ctx->mstorm_ag_context.task_cid = (u16)qedi_conn->iscsi_conn_id;

	/* Ustorm context */
	fw_task_ctx->ustorm_st_context.rem_rcv_len = 0;
	fw_task_ctx->ustorm_st_context.exp_data_transfer_len = 0;
	fw_task_ctx->ustorm_st_context.exp_data_sn = 0;
	fw_task_ctx->ustorm_st_context.task_type =  ISCSI_TASK_TYPE_MIDPATH;
	fw_task_ctx->ustorm_st_context.cq_rss_number = 0;

	SET_FIELD(fw_task_ctx->ustorm_st_context.flags,
		  USTORM_ISCSI_TASK_ST_CTX_LOCAL_COMP, 0);
	SET_FIELD(fw_task_ctx->ustorm_st_context.reg1.reg1_map,
		  ISCSI_REG1_NUM_FAST_SGES, 0);

	fw_task_ctx->ustorm_ag_context.icid = (u16)qedi_conn->iscsi_conn_id;
	SET_FIELD(fw_task_ctx->ustorm_ag_context.flags1,
		  USTORM_ISCSI_TASK_AG_CTX_R2T2RECV, 1);

	spin_lock(&qedi_conn->list_lock);
	list_add_tail(&qedi_cmd->io_cmd, &qedi_conn->active_cmd_list);
	qedi_cmd->io_cmd_in_list = true;
	qedi_conn->active_cmd_count++;
	spin_unlock(&qedi_conn->list_lock);

	qedi_add_to_sq(qedi_conn, task, tid, ptu_invalidate, false);
	qedi_ring_doorbell(qedi_conn);

	return 0;
}

int qedi_cleanup_all_io(struct qedi_ctx *qedi, struct qedi_conn *qedi_conn,
			struct iscsi_task *task, bool in_recovery)
{
	int rval;
	struct iscsi_task *ctask;
	struct qedi_cmd *cmd, *cmd_tmp;
	struct iscsi_tm *tmf_hdr;
	unsigned int lun = 0;
	bool lun_reset = false;
	struct iscsi_conn *conn = qedi_conn->cls_conn->dd_data;
	struct iscsi_session *session = conn->session;

	/* From recovery, task is NULL or from tmf resp valid task */
	if (task) {
		tmf_hdr = (struct iscsi_tm *)task->hdr;

		if ((tmf_hdr->flags & ISCSI_FLAG_TM_FUNC_MASK) ==
			ISCSI_TM_FUNC_LOGICAL_UNIT_RESET) {
			lun_reset = true;
			lun = scsilun_to_int(&tmf_hdr->lun);
		}
	}

	qedi_conn->cmd_cleanup_req = 0;
	qedi_conn->cmd_cleanup_cmpl = 0;

	QEDI_INFO(&qedi->dbg_ctx, QEDI_LOG_SCSI_TM,
		  "active_cmd_count=%d, cid=0x%x, in_recovery=%d, lun_reset=%d\n",
		  qedi_conn->active_cmd_count, qedi_conn->iscsi_conn_id,
		  in_recovery, lun_reset);

	if (lun_reset)
		spin_lock_bh(&session->back_lock);

	spin_lock(&qedi_conn->list_lock);

	list_for_each_entry_safe(cmd, cmd_tmp, &qedi_conn->active_cmd_list,
				 io_cmd) {
		ctask = cmd->task;
		if (ctask == task)
			continue;

		if (lun_reset) {
			if (cmd->scsi_cmd && cmd->scsi_cmd->device) {
				QEDI_INFO(&qedi->dbg_ctx, QEDI_LOG_SCSI_TM,
					  "tid=0x%x itt=0x%x scsi_cmd_ptr=%p device=%p task_state=%d cmd_state=0%x cid=0x%x\n",
					  cmd->task_id, get_itt(ctask->itt),
					  cmd->scsi_cmd, cmd->scsi_cmd->device,
					  ctask->state, cmd->state,
					  qedi_conn->iscsi_conn_id);
				if (cmd->scsi_cmd->device->lun != lun)
					continue;
			}
		}
		qedi_conn->cmd_cleanup_req++;
		qedi_iscsi_cleanup_task(ctask, true);

		list_del_init(&cmd->io_cmd);
		qedi_conn->active_cmd_count--;
		QEDI_WARN(&qedi->dbg_ctx,
			  "Deleted active cmd list node io_cmd=%p, cid=0x%x\n",
			  &cmd->io_cmd, qedi_conn->iscsi_conn_id);
	}

	spin_unlock(&qedi_conn->list_lock);

	if (lun_reset)
		spin_unlock_bh(&session->back_lock);

	QEDI_INFO(&qedi->dbg_ctx, QEDI_LOG_SCSI_TM,
		  "cmd_cleanup_req=%d, cid=0x%x\n",
		  qedi_conn->cmd_cleanup_req,
		  qedi_conn->iscsi_conn_id);

	rval  = wait_event_interruptible_timeout(qedi_conn->wait_queue,
						 ((qedi_conn->cmd_cleanup_req ==
						 qedi_conn->cmd_cleanup_cmpl) ||
						 qedi_conn->ep),
						 5 * HZ);
	if (rval) {
		QEDI_INFO(&qedi->dbg_ctx, QEDI_LOG_SCSI_TM,
			  "i/o cmd_cleanup_req=%d, equal to cmd_cleanup_cmpl=%d, cid=0x%x\n",
			  qedi_conn->cmd_cleanup_req,
			  qedi_conn->cmd_cleanup_cmpl,
			  qedi_conn->iscsi_conn_id);

		return 0;
	}

	QEDI_INFO(&qedi->dbg_ctx, QEDI_LOG_SCSI_TM,
		  "i/o cmd_cleanup_req=%d, not equal to cmd_cleanup_cmpl=%d, cid=0x%x\n",
		  qedi_conn->cmd_cleanup_req,
		  qedi_conn->cmd_cleanup_cmpl,
		  qedi_conn->iscsi_conn_id);

	iscsi_host_for_each_session(qedi->shost,
				    qedi_mark_device_missing);
	qedi_ops->common->drain(qedi->cdev);

	/* Enable IOs for all other sessions except current.*/
	if (!wait_event_interruptible_timeout(qedi_conn->wait_queue,
					      (qedi_conn->cmd_cleanup_req ==
					       qedi_conn->cmd_cleanup_cmpl),
					      5 * HZ)) {
		iscsi_host_for_each_session(qedi->shost,
					    qedi_mark_device_available);
		return -1;
	}

	iscsi_host_for_each_session(qedi->shost,
				    qedi_mark_device_available);

	return 0;
}

void qedi_clearsq(struct qedi_ctx *qedi, struct qedi_conn *qedi_conn,
		  struct iscsi_task *task)
{
	struct qedi_endpoint *qedi_ep;
	int rval;

	qedi_ep = qedi_conn->ep;
	qedi_conn->cmd_cleanup_req = 0;
	qedi_conn->cmd_cleanup_cmpl = 0;

	if (!qedi_ep) {
		QEDI_WARN(&qedi->dbg_ctx,
			  "Cannot proceed, ep already disconnected, cid=0x%x\n",
			  qedi_conn->iscsi_conn_id);
		return;
	}

	QEDI_INFO(&qedi->dbg_ctx, QEDI_LOG_INFO,
		  "Clearing SQ for cid=0x%x, conn=%p, ep=%p\n",
		  qedi_conn->iscsi_conn_id, qedi_conn, qedi_ep);

	qedi_ops->clear_sq(qedi->cdev, qedi_ep->handle);

	rval = qedi_cleanup_all_io(qedi, qedi_conn, task, true);
	if (rval) {
		QEDI_ERR(&qedi->dbg_ctx,
			 "fatal error, need hard reset, cid=0x%x\n",
			 qedi_conn->iscsi_conn_id);
		WARN_ON(1);
	}
}

static int qedi_wait_for_cleanup_request(struct qedi_ctx *qedi,
					 struct qedi_conn *qedi_conn,
					 struct iscsi_task *task,
					 struct qedi_cmd *qedi_cmd,
					 struct qedi_work_map *list_work)
{
	struct qedi_cmd *cmd = (struct qedi_cmd *)task->dd_data;
	int wait;

	wait  = wait_event_interruptible_timeout(qedi_conn->wait_queue,
						 ((qedi_cmd->state ==
						   CLEANUP_RECV) ||
						 ((qedi_cmd->type == TYPEIO) &&
						  (cmd->state ==
						   RESPONSE_RECEIVED))),
						 5 * HZ);
	if (!wait) {
		qedi_cmd->state = CLEANUP_WAIT_FAILED;

		QEDI_INFO(&qedi->dbg_ctx, QEDI_LOG_SCSI_TM,
			  "Cleanup timedout tid=0x%x, issue connection recovery, cid=0x%x\n",
			  cmd->task_id, qedi_conn->iscsi_conn_id);

		return -1;
	}
	return 0;
}

static void qedi_tmf_work(struct work_struct *work)
{
	struct qedi_cmd *qedi_cmd =
		container_of(work, struct qedi_cmd, tmf_work);
	struct qedi_conn *qedi_conn = qedi_cmd->conn;
	struct qedi_ctx *qedi = qedi_conn->qedi;
	struct iscsi_conn *conn = qedi_conn->cls_conn->dd_data;
	struct iscsi_cls_session *cls_sess;
	struct qedi_work_map *list_work = NULL;
	struct iscsi_task *mtask;
	struct qedi_cmd *cmd;
	struct iscsi_task *ctask;
	struct iscsi_tm *tmf_hdr;
	s16 rval = 0;
	s16 tid = 0;

	mtask = qedi_cmd->task;
	tmf_hdr = (struct iscsi_tm *)mtask->hdr;
	cls_sess = iscsi_conn_to_session(qedi_conn->cls_conn);
	set_bit(QEDI_CONN_FW_CLEANUP, &qedi_conn->flags);

	ctask = iscsi_itt_to_task(conn, tmf_hdr->rtt);
	if (!ctask || !ctask->sc) {
		QEDI_ERR(&qedi->dbg_ctx, "Task already completed\n");
		goto abort_ret;
	}

	cmd = (struct qedi_cmd *)ctask->dd_data;
	QEDI_INFO(&qedi->dbg_ctx, QEDI_LOG_INFO,
		  "Abort tmf rtt=0x%x, cmd itt=0x%x, cmd tid=0x%x, cid=0x%x\n",
		  get_itt(tmf_hdr->rtt), get_itt(ctask->itt), cmd->task_id,
		  qedi_conn->iscsi_conn_id);

	if (do_not_recover) {
		QEDI_ERR(&qedi->dbg_ctx, "DONT SEND CLEANUP/ABORT %d\n",
			 do_not_recover);
		goto abort_ret;
	}

	list_work = kzalloc(sizeof(*list_work), GFP_ATOMIC);
	if (!list_work) {
		QEDI_ERR(&qedi->dbg_ctx, "Memory alloction failed\n");
		goto abort_ret;
	}

	qedi_cmd->type = TYPEIO;
	list_work->qedi_cmd = qedi_cmd;
	list_work->rtid = cmd->task_id;
	list_work->state = QEDI_WORK_SCHEDULED;
	qedi_cmd->list_tmf_work = list_work;

	QEDI_INFO(&qedi->dbg_ctx, QEDI_LOG_SCSI_TM,
		  "Queue tmf work=%p, list node=%p, cid=0x%x, tmf flags=0x%x\n",
		  list_work->ptr_tmf_work, list_work, qedi_conn->iscsi_conn_id,
		  tmf_hdr->flags);

	spin_lock_bh(&qedi_conn->tmf_work_lock);
	list_add_tail(&list_work->list, &qedi_conn->tmf_work_list);
	spin_unlock_bh(&qedi_conn->tmf_work_lock);

	qedi_iscsi_cleanup_task(ctask, false);

	rval = qedi_wait_for_cleanup_request(qedi, qedi_conn, ctask, qedi_cmd,
					     list_work);
	if (rval == -1) {
		QEDI_INFO(&qedi->dbg_ctx, QEDI_LOG_INFO,
			  "FW cleanup got escalated, cid=0x%x\n",
			  qedi_conn->iscsi_conn_id);
		goto ldel_exit;
	}

	tid = qedi_get_task_idx(qedi);
	if (tid == -1) {
		QEDI_ERR(&qedi->dbg_ctx, "Invalid tid, cid=0x%x\n",
			 qedi_conn->iscsi_conn_id);
		goto ldel_exit;
	}

	qedi_cmd->task_id = tid;
	qedi_send_iscsi_tmf(qedi_conn, qedi_cmd->task);

abort_ret:
	clear_bit(QEDI_CONN_FW_CLEANUP, &qedi_conn->flags);
	return;

ldel_exit:
	spin_lock_bh(&qedi_conn->tmf_work_lock);
	if (!qedi_cmd->list_tmf_work) {
		list_del_init(&list_work->list);
		qedi_cmd->list_tmf_work = NULL;
		kfree(list_work);
	}
	spin_unlock_bh(&qedi_conn->tmf_work_lock);

	spin_lock(&qedi_conn->list_lock);
	list_del_init(&cmd->io_cmd);
	qedi_conn->active_cmd_count--;
	spin_unlock(&qedi_conn->list_lock);

	clear_bit(QEDI_CONN_FW_CLEANUP, &qedi_conn->flags);
}

static int qedi_send_iscsi_tmf(struct qedi_conn *qedi_conn,
			       struct iscsi_task *mtask)
{
	struct iscsi_conn *conn = qedi_conn->cls_conn->dd_data;
	struct qedi_ctx *qedi = qedi_conn->qedi;
	struct iscsi_task_context *fw_task_ctx;
	struct iscsi_tmf_request_hdr *fw_tmf_request;
	struct iscsi_sge *single_sge;
	struct qedi_cmd *qedi_cmd;
	struct qedi_cmd *cmd;
	struct iscsi_task *ctask;
	struct iscsi_tm *tmf_hdr;
	struct iscsi_sge *req_sge;
	struct iscsi_sge *resp_sge;
	u32 lun[2];
	s16 tid = 0, ptu_invalidate = 0;

	req_sge = (struct iscsi_sge *)qedi_conn->gen_pdu.req_bd_tbl;
	resp_sge = (struct iscsi_sge *)qedi_conn->gen_pdu.resp_bd_tbl;
	qedi_cmd = (struct qedi_cmd *)mtask->dd_data;
	tmf_hdr = (struct iscsi_tm *)mtask->hdr;

	tid = qedi_cmd->task_id;
	qedi_update_itt_map(qedi, tid, mtask->itt, qedi_cmd);

	fw_task_ctx = qedi_get_task_mem(&qedi->tasks, tid);
	memset(fw_task_ctx, 0, sizeof(struct iscsi_task_context));

	fw_tmf_request = &fw_task_ctx->ystorm_st_context.pdu_hdr.tmf_request;
	fw_tmf_request->itt = qedi_set_itt(tid, get_itt(mtask->itt));
	fw_tmf_request->cmd_sn = be32_to_cpu(tmf_hdr->cmdsn);

	memcpy(lun, &tmf_hdr->lun, sizeof(struct scsi_lun));
	fw_tmf_request->lun.lo = be32_to_cpu(lun[0]);
	fw_tmf_request->lun.hi = be32_to_cpu(lun[1]);

	if (qedi->tid_reuse_count[tid] == QEDI_MAX_TASK_NUM) {
		ptu_invalidate = 1;
		qedi->tid_reuse_count[tid] = 0;
	}
	fw_task_ctx->ystorm_st_context.state.reuse_count =
						qedi->tid_reuse_count[tid];
	fw_task_ctx->mstorm_st_context.reuse_count =
						qedi->tid_reuse_count[tid]++;

	if ((tmf_hdr->flags & ISCSI_FLAG_TM_FUNC_MASK) ==
	     ISCSI_TM_FUNC_ABORT_TASK) {
		ctask = iscsi_itt_to_task(conn, tmf_hdr->rtt);
		if (!ctask || !ctask->sc) {
			QEDI_ERR(&qedi->dbg_ctx,
				 "Could not get reference task\n");
			return 0;
		}
		cmd = (struct qedi_cmd *)ctask->dd_data;
		fw_tmf_request->rtt =
				qedi_set_itt(cmd->task_id,
					     get_itt(tmf_hdr->rtt));
	} else {
		fw_tmf_request->rtt = ISCSI_RESERVED_TAG;
	}

	fw_tmf_request->opcode = tmf_hdr->opcode;
	fw_tmf_request->function = tmf_hdr->flags;
	fw_tmf_request->hdr_second_dword = ntoh24(tmf_hdr->dlength);
	fw_tmf_request->ref_cmd_sn = be32_to_cpu(tmf_hdr->refcmdsn);

	single_sge = &fw_task_ctx->mstorm_st_context.sgl_union.single_sge;
	fw_task_ctx->mstorm_st_context.task_type = ISCSI_TASK_TYPE_MIDPATH;
	fw_task_ctx->mstorm_ag_context.task_cid = (u16)qedi_conn->iscsi_conn_id;
	single_sge->sge_addr.lo = resp_sge->sge_addr.lo;
	single_sge->sge_addr.hi = resp_sge->sge_addr.hi;
	single_sge->sge_len = resp_sge->sge_len;

	SET_FIELD(fw_task_ctx->mstorm_st_context.flags.mflags,
		  ISCSI_MFLAGS_SINGLE_SGE, 1);
	SET_FIELD(fw_task_ctx->mstorm_st_context.flags.mflags,
		  ISCSI_MFLAGS_SLOW_IO, 0);
	fw_task_ctx->mstorm_st_context.sgl_size = 1;
	fw_task_ctx->mstorm_st_context.rem_task_size = resp_sge->sge_len;

	/* Ustorm context */
	fw_task_ctx->ustorm_st_context.rem_rcv_len = 0;
	fw_task_ctx->ustorm_st_context.exp_data_transfer_len = 0;
	fw_task_ctx->ustorm_st_context.exp_data_sn = 0;
	fw_task_ctx->ustorm_st_context.task_type =  ISCSI_TASK_TYPE_MIDPATH;
	fw_task_ctx->ustorm_st_context.cq_rss_number = 0;

	SET_FIELD(fw_task_ctx->ustorm_st_context.flags,
		  USTORM_ISCSI_TASK_ST_CTX_LOCAL_COMP, 0);
	SET_FIELD(fw_task_ctx->ustorm_st_context.reg1.reg1_map,
		  ISCSI_REG1_NUM_FAST_SGES, 0);

	fw_task_ctx->ustorm_ag_context.icid = (u16)qedi_conn->iscsi_conn_id;
	SET_FIELD(fw_task_ctx->ustorm_ag_context.flags1,
		  USTORM_ISCSI_TASK_AG_CTX_R2T2RECV, 1);
	fw_task_ctx->ustorm_st_context.lun.lo = be32_to_cpu(lun[0]);
	fw_task_ctx->ustorm_st_context.lun.hi = be32_to_cpu(lun[1]);

	QEDI_INFO(&qedi->dbg_ctx, QEDI_LOG_SCSI_TM,
		  "Add TMF to SQ, tmf tid=0x%x, itt=0x%x, cid=0x%x\n",
		  tid,  mtask->itt, qedi_conn->iscsi_conn_id);

	spin_lock(&qedi_conn->list_lock);
	list_add_tail(&qedi_cmd->io_cmd, &qedi_conn->active_cmd_list);
	qedi_cmd->io_cmd_in_list = true;
	qedi_conn->active_cmd_count++;
	spin_unlock(&qedi_conn->list_lock);

	qedi_add_to_sq(qedi_conn, mtask, tid, ptu_invalidate, false);
	qedi_ring_doorbell(qedi_conn);
	return 0;
}

int qedi_iscsi_abort_work(struct qedi_conn *qedi_conn,
			  struct iscsi_task *mtask)
{
	struct qedi_ctx *qedi = qedi_conn->qedi;
	struct iscsi_tm *tmf_hdr;
	struct qedi_cmd *qedi_cmd = (struct qedi_cmd *)mtask->dd_data;
	s16 tid = 0;

	tmf_hdr = (struct iscsi_tm *)mtask->hdr;
	qedi_cmd->task = mtask;

	/* If abort task then schedule the work and return */
	if ((tmf_hdr->flags & ISCSI_FLAG_TM_FUNC_MASK) ==
	    ISCSI_TM_FUNC_ABORT_TASK) {
		qedi_cmd->state = CLEANUP_WAIT;
		INIT_WORK(&qedi_cmd->tmf_work, qedi_tmf_work);
		queue_work(qedi->tmf_thread, &qedi_cmd->tmf_work);

	} else if (((tmf_hdr->flags & ISCSI_FLAG_TM_FUNC_MASK) ==
		    ISCSI_TM_FUNC_LOGICAL_UNIT_RESET) ||
		   ((tmf_hdr->flags & ISCSI_FLAG_TM_FUNC_MASK) ==
		    ISCSI_TM_FUNC_TARGET_WARM_RESET) ||
		   ((tmf_hdr->flags & ISCSI_FLAG_TM_FUNC_MASK) ==
		    ISCSI_TM_FUNC_TARGET_COLD_RESET)) {
		tid = qedi_get_task_idx(qedi);
		if (tid == -1) {
			QEDI_ERR(&qedi->dbg_ctx, "Invalid tid, cid=0x%x\n",
				 qedi_conn->iscsi_conn_id);
			return -1;
		}
		qedi_cmd->task_id = tid;

		qedi_send_iscsi_tmf(qedi_conn, qedi_cmd->task);

	} else {
		QEDI_ERR(&qedi->dbg_ctx, "Invalid tmf, cid=0x%x\n",
			 qedi_conn->iscsi_conn_id);
		return -1;
	}

	return 0;
}

int qedi_send_iscsi_text(struct qedi_conn *qedi_conn,
			 struct iscsi_task *task)
{
	struct qedi_ctx *qedi = qedi_conn->qedi;
	struct iscsi_task_context *fw_task_ctx;
	struct iscsi_text_request_hdr *fw_text_request;
	struct iscsi_cached_sge_ctx *cached_sge;
	struct iscsi_sge *single_sge;
	struct qedi_cmd *qedi_cmd;
	/* For 6.5 hdr iscsi_hdr */
	struct iscsi_text *text_hdr;
	struct iscsi_sge *req_sge;
	struct iscsi_sge *resp_sge;
	s16 ptu_invalidate = 0;
	s16 tid = 0;

	req_sge = (struct iscsi_sge *)qedi_conn->gen_pdu.req_bd_tbl;
	resp_sge = (struct iscsi_sge *)qedi_conn->gen_pdu.resp_bd_tbl;
	qedi_cmd = (struct qedi_cmd *)task->dd_data;
	text_hdr = (struct iscsi_text *)task->hdr;

	tid = qedi_get_task_idx(qedi);
	if (tid == -1)
		return -ENOMEM;

	fw_task_ctx = qedi_get_task_mem(&qedi->tasks, tid);
	memset(fw_task_ctx, 0, sizeof(struct iscsi_task_context));

	qedi_cmd->task_id = tid;

	/* Ystorm context */
	fw_text_request =
			&fw_task_ctx->ystorm_st_context.pdu_hdr.text_request;
	fw_text_request->opcode = text_hdr->opcode;
	fw_text_request->flags_attr = text_hdr->flags;

	qedi_update_itt_map(qedi, tid, task->itt, qedi_cmd);
	fw_text_request->itt = qedi_set_itt(tid, get_itt(task->itt));
	fw_text_request->ttt = text_hdr->ttt;
	fw_text_request->cmd_sn = be32_to_cpu(text_hdr->cmdsn);
	fw_text_request->exp_stat_sn = be32_to_cpu(text_hdr->exp_statsn);
	fw_text_request->hdr_second_dword = ntoh24(text_hdr->dlength);

	if (qedi->tid_reuse_count[tid] == QEDI_MAX_TASK_NUM) {
		ptu_invalidate = 1;
		qedi->tid_reuse_count[tid] = 0;
	}
	fw_task_ctx->ystorm_st_context.state.reuse_count =
						     qedi->tid_reuse_count[tid];
	fw_task_ctx->mstorm_st_context.reuse_count =
						   qedi->tid_reuse_count[tid]++;

	cached_sge =
	       &fw_task_ctx->ystorm_st_context.state.sgl_ctx_union.cached_sge;
	cached_sge->sge.sge_len = req_sge->sge_len;
	cached_sge->sge.sge_addr.lo = (u32)(qedi_conn->gen_pdu.req_dma_addr);
	cached_sge->sge.sge_addr.hi =
			      (u32)((u64)qedi_conn->gen_pdu.req_dma_addr >> 32);

	/* Mstorm context */
	single_sge = &fw_task_ctx->mstorm_st_context.sgl_union.single_sge;
	fw_task_ctx->mstorm_st_context.task_type = 0x2;
	fw_task_ctx->mstorm_ag_context.task_cid = (u16)qedi_conn->iscsi_conn_id;
	single_sge->sge_addr.lo = resp_sge->sge_addr.lo;
	single_sge->sge_addr.hi = resp_sge->sge_addr.hi;
	single_sge->sge_len = resp_sge->sge_len;

	SET_FIELD(fw_task_ctx->mstorm_st_context.flags.mflags,
		  ISCSI_MFLAGS_SINGLE_SGE, 1);
	SET_FIELD(fw_task_ctx->mstorm_st_context.flags.mflags,
		  ISCSI_MFLAGS_SLOW_IO, 0);
	fw_task_ctx->mstorm_st_context.sgl_size = 1;
	fw_task_ctx->mstorm_st_context.rem_task_size = resp_sge->sge_len;

	/* Ustorm context */
	fw_task_ctx->ustorm_ag_context.exp_data_acked =
						      ntoh24(text_hdr->dlength);
	fw_task_ctx->ustorm_st_context.rem_rcv_len = resp_sge->sge_len;
	fw_task_ctx->ustorm_st_context.exp_data_transfer_len =
						      ntoh24(text_hdr->dlength);
	fw_task_ctx->ustorm_st_context.exp_data_sn =
					      be32_to_cpu(text_hdr->exp_statsn);
	fw_task_ctx->ustorm_st_context.cq_rss_number = 0;
	fw_task_ctx->ustorm_st_context.task_type = 0x2;
	fw_task_ctx->ustorm_ag_context.icid = (u16)qedi_conn->iscsi_conn_id;
	SET_FIELD(fw_task_ctx->ustorm_ag_context.flags1,
		  USTORM_ISCSI_TASK_AG_CTX_R2T2RECV, 1);

	/*  Add command in active command list */
	spin_lock(&qedi_conn->list_lock);
	list_add_tail(&qedi_cmd->io_cmd, &qedi_conn->active_cmd_list);
	qedi_cmd->io_cmd_in_list = true;
	qedi_conn->active_cmd_count++;
	spin_unlock(&qedi_conn->list_lock);

	qedi_add_to_sq(qedi_conn, task, tid, ptu_invalidate, false);
	qedi_ring_doorbell(qedi_conn);

	return 0;
}

int qedi_send_iscsi_nopout(struct qedi_conn *qedi_conn,
			   struct iscsi_task *task,
			   char *datap, int data_len, int unsol)
{
	struct qedi_ctx *qedi = qedi_conn->qedi;
	struct iscsi_task_context *fw_task_ctx;
	struct iscsi_nop_out_hdr *fw_nop_out;
	struct qedi_cmd *qedi_cmd;
	/* For 6.5 hdr iscsi_hdr */
	struct iscsi_nopout *nopout_hdr;
	struct iscsi_cached_sge_ctx *cached_sge;
	struct iscsi_sge *single_sge;
	struct iscsi_sge *req_sge;
	struct iscsi_sge *resp_sge;
	u32 lun[2];
	s16 ptu_invalidate = 0;
	s16 tid = 0;

	req_sge = (struct iscsi_sge *)qedi_conn->gen_pdu.req_bd_tbl;
	resp_sge = (struct iscsi_sge *)qedi_conn->gen_pdu.resp_bd_tbl;
	qedi_cmd = (struct qedi_cmd *)task->dd_data;
	nopout_hdr = (struct iscsi_nopout *)task->hdr;

	tid = qedi_get_task_idx(qedi);
	if (tid == -1) {
		QEDI_WARN(&qedi->dbg_ctx, "Invalid tid\n");
		return -ENOMEM;
	}

	fw_task_ctx = qedi_get_task_mem(&qedi->tasks, tid);

	memset(fw_task_ctx, 0, sizeof(struct iscsi_task_context));
	qedi_cmd->task_id = tid;

	/* Ystorm context */
	fw_nop_out = &fw_task_ctx->ystorm_st_context.pdu_hdr.nop_out;
	SET_FIELD(fw_nop_out->flags_attr, ISCSI_NOP_OUT_HDR_CONST1, 1);
	SET_FIELD(fw_nop_out->flags_attr, ISCSI_NOP_OUT_HDR_RSRV, 0);

	memcpy(lun, &nopout_hdr->lun, sizeof(struct scsi_lun));
	fw_nop_out->lun.lo = be32_to_cpu(lun[0]);
	fw_nop_out->lun.hi = be32_to_cpu(lun[1]);

	qedi_update_itt_map(qedi, tid, task->itt, qedi_cmd);

	if (nopout_hdr->ttt != ISCSI_TTT_ALL_ONES) {
		fw_nop_out->itt = be32_to_cpu(nopout_hdr->itt);
		fw_nop_out->ttt = be32_to_cpu(nopout_hdr->ttt);
		fw_task_ctx->ystorm_st_context.state.buffer_offset[0] = 0;
		fw_task_ctx->ystorm_st_context.state.local_comp = 1;
		SET_FIELD(fw_task_ctx->ustorm_st_context.flags,
			  USTORM_ISCSI_TASK_ST_CTX_LOCAL_COMP, 1);
	} else {
		fw_nop_out->itt = qedi_set_itt(tid, get_itt(task->itt));
		fw_nop_out->ttt = ISCSI_TTT_ALL_ONES;
		fw_task_ctx->ystorm_st_context.state.buffer_offset[0] = 0;

		spin_lock(&qedi_conn->list_lock);
		list_add_tail(&qedi_cmd->io_cmd, &qedi_conn->active_cmd_list);
		qedi_cmd->io_cmd_in_list = true;
		qedi_conn->active_cmd_count++;
		spin_unlock(&qedi_conn->list_lock);
	}

	fw_nop_out->opcode = ISCSI_OPCODE_NOP_OUT;
	fw_nop_out->cmd_sn = be32_to_cpu(nopout_hdr->cmdsn);
	fw_nop_out->exp_stat_sn = be32_to_cpu(nopout_hdr->exp_statsn);

	cached_sge =
	       &fw_task_ctx->ystorm_st_context.state.sgl_ctx_union.cached_sge;
	cached_sge->sge.sge_len = req_sge->sge_len;
	cached_sge->sge.sge_addr.lo = (u32)(qedi_conn->gen_pdu.req_dma_addr);
	cached_sge->sge.sge_addr.hi =
			(u32)((u64)qedi_conn->gen_pdu.req_dma_addr >> 32);

	/* Mstorm context */
	fw_task_ctx->mstorm_st_context.task_type = ISCSI_TASK_TYPE_MIDPATH;
	fw_task_ctx->mstorm_ag_context.task_cid = (u16)qedi_conn->iscsi_conn_id;

	single_sge = &fw_task_ctx->mstorm_st_context.sgl_union.single_sge;
	single_sge->sge_addr.lo = resp_sge->sge_addr.lo;
	single_sge->sge_addr.hi = resp_sge->sge_addr.hi;
	single_sge->sge_len = resp_sge->sge_len;
	fw_task_ctx->mstorm_st_context.rem_task_size = resp_sge->sge_len;

	if (qedi->tid_reuse_count[tid] == QEDI_MAX_TASK_NUM) {
		ptu_invalidate = 1;
		qedi->tid_reuse_count[tid] = 0;
	}
	fw_task_ctx->ystorm_st_context.state.reuse_count =
						qedi->tid_reuse_count[tid];
	fw_task_ctx->mstorm_st_context.reuse_count =
						qedi->tid_reuse_count[tid]++;
	/* Ustorm context */
	fw_task_ctx->ustorm_st_context.rem_rcv_len = resp_sge->sge_len;
	fw_task_ctx->ustorm_st_context.exp_data_transfer_len = data_len;
	fw_task_ctx->ustorm_st_context.exp_data_sn = 0;
	fw_task_ctx->ustorm_st_context.task_type =  ISCSI_TASK_TYPE_MIDPATH;
	fw_task_ctx->ustorm_st_context.cq_rss_number = 0;

	SET_FIELD(fw_task_ctx->ustorm_st_context.reg1.reg1_map,
		  ISCSI_REG1_NUM_FAST_SGES, 0);

	fw_task_ctx->ustorm_ag_context.icid = (u16)qedi_conn->iscsi_conn_id;
	SET_FIELD(fw_task_ctx->ustorm_ag_context.flags1,
		  USTORM_ISCSI_TASK_AG_CTX_R2T2RECV, 1);

	fw_task_ctx->ustorm_st_context.lun.lo = be32_to_cpu(lun[0]);
	fw_task_ctx->ustorm_st_context.lun.hi = be32_to_cpu(lun[1]);

	qedi_add_to_sq(qedi_conn, task, tid, ptu_invalidate, false);
	qedi_ring_doorbell(qedi_conn);
	return 0;
}

static int qedi_split_bd(struct qedi_cmd *cmd, u64 addr, int sg_len,
			 int bd_index)
{
	struct iscsi_sge *bd = cmd->io_tbl.sge_tbl;
	int frag_size, sg_frags;

	sg_frags = 0;

	while (sg_len) {
		if (addr % QEDI_PAGE_SIZE)
			frag_size =
				   (QEDI_PAGE_SIZE - (addr % QEDI_PAGE_SIZE));
		else
			frag_size = (sg_len > QEDI_BD_SPLIT_SZ) ? 0 :
				    (sg_len % QEDI_BD_SPLIT_SZ);

		if (frag_size == 0)
			frag_size = QEDI_BD_SPLIT_SZ;

		bd[bd_index + sg_frags].sge_addr.lo = (addr & 0xffffffff);
		bd[bd_index + sg_frags].sge_addr.hi = (addr >> 32);
		bd[bd_index + sg_frags].sge_len = (u16)frag_size;
		QEDI_INFO(&cmd->conn->qedi->dbg_ctx, QEDI_LOG_IO,
			  "split sge %d: addr=%llx, len=%x",
			  (bd_index + sg_frags), addr, frag_size);

		addr += (u64)frag_size;
		sg_frags++;
		sg_len -= frag_size;
	}
	return sg_frags;
}

static int qedi_map_scsi_sg(struct qedi_ctx *qedi, struct qedi_cmd *cmd)
{
	struct scsi_cmnd *sc = cmd->scsi_cmd;
	struct iscsi_sge *bd = cmd->io_tbl.sge_tbl;
	struct scatterlist *sg;
	int byte_count = 0;
	int bd_count = 0;
	int sg_count;
	int sg_len;
	int sg_frags;
	u64 addr, end_addr;
	int i;

	WARN_ON(scsi_sg_count(sc) > QEDI_ISCSI_MAX_BDS_PER_CMD);

	sg_count = dma_map_sg(&qedi->pdev->dev, scsi_sglist(sc),
			      scsi_sg_count(sc), sc->sc_data_direction);

	/*
	 * New condition to send single SGE as cached-SGL.
	 * Single SGE with length less than 64K.
	 */
	sg = scsi_sglist(sc);
	if ((sg_count == 1) && (sg_dma_len(sg) <= MAX_SGLEN_FOR_CACHESGL)) {
		sg_len = sg_dma_len(sg);
		addr = (u64)sg_dma_address(sg);

		bd[bd_count].sge_addr.lo = (addr & 0xffffffff);
		bd[bd_count].sge_addr.hi = (addr >> 32);
		bd[bd_count].sge_len = (u16)sg_len;

		QEDI_INFO(&qedi->dbg_ctx, QEDI_LOG_IO,
			  "single-cashed-sgl: bd_count:%d addr=%llx, len=%x",
			  sg_count, addr, sg_len);

		return ++bd_count;
	}

	scsi_for_each_sg(sc, sg, sg_count, i) {
		sg_len = sg_dma_len(sg);
		addr = (u64)sg_dma_address(sg);
		end_addr = (addr + sg_len);

		/*
		 * first sg elem in the 'list',
		 * check if end addr is page-aligned.
		 */
		if ((i == 0) && (sg_count > 1) && (end_addr % QEDI_PAGE_SIZE))
			cmd->use_slowpath = true;

		/*
		 * last sg elem in the 'list',
		 * check if start addr is page-aligned.
		 */
		else if ((i == (sg_count - 1)) &&
			 (sg_count > 1) && (addr % QEDI_PAGE_SIZE))
			cmd->use_slowpath = true;

		/*
		 * middle sg elements in list,
		 * check if start and end addr is page-aligned
		 */
		else if ((i != 0) && (i != (sg_count - 1)) &&
			 ((addr % QEDI_PAGE_SIZE) ||
			 (end_addr % QEDI_PAGE_SIZE)))
			cmd->use_slowpath = true;

		QEDI_INFO(&qedi->dbg_ctx, QEDI_LOG_IO, "sg[%d] size=0x%x",
			  i, sg_len);

		if (sg_len > QEDI_BD_SPLIT_SZ) {
			sg_frags = qedi_split_bd(cmd, addr, sg_len, bd_count);
		} else {
			sg_frags = 1;
			bd[bd_count].sge_addr.lo = addr & 0xffffffff;
			bd[bd_count].sge_addr.hi = addr >> 32;
			bd[bd_count].sge_len = sg_len;
		}
		byte_count += sg_len;
		bd_count += sg_frags;
	}

	if (byte_count != scsi_bufflen(sc))
		QEDI_ERR(&qedi->dbg_ctx,
			 "byte_count = %d != scsi_bufflen = %d\n", byte_count,
			 scsi_bufflen(sc));
	else
		QEDI_INFO(&qedi->dbg_ctx, QEDI_LOG_IO, "byte_count = %d\n",
			  byte_count);

	WARN_ON(byte_count != scsi_bufflen(sc));

	return bd_count;
}

static void qedi_iscsi_map_sg_list(struct qedi_cmd *cmd)
{
	int bd_count;
	struct scsi_cmnd *sc = cmd->scsi_cmd;

	if (scsi_sg_count(sc)) {
		bd_count  = qedi_map_scsi_sg(cmd->conn->qedi, cmd);
		if (bd_count == 0)
			return;
	} else {
		struct iscsi_sge *bd = cmd->io_tbl.sge_tbl;

		bd[0].sge_addr.lo = 0;
		bd[0].sge_addr.hi = 0;
		bd[0].sge_len = 0;
		bd_count = 0;
	}
	cmd->io_tbl.sge_valid = bd_count;
}

static void qedi_cpy_scsi_cdb(struct scsi_cmnd *sc, u32 *dstp)
{
	u32 dword;
	int lpcnt;
	u8 *srcp;

	lpcnt = sc->cmd_len / sizeof(dword);
	srcp = (u8 *)sc->cmnd;
	while (lpcnt--) {
		memcpy(&dword, (const void *)srcp, 4);
		*dstp = cpu_to_be32(dword);
		srcp += 4;
		dstp++;
	}
	if (sc->cmd_len & 0x3) {
		dword = (u32)srcp[0] | ((u32)srcp[1] << 8);
		*dstp = cpu_to_be32(dword);
	}
}

void qedi_trace_io(struct qedi_ctx *qedi, struct iscsi_task *task,
		   u16 tid, int8_t direction)
{
	struct qedi_io_log *io_log;
	struct iscsi_conn *conn = task->conn;
	struct qedi_conn *qedi_conn = conn->dd_data;
	struct scsi_cmnd *sc_cmd = task->sc;
	unsigned long flags;
	u8 op;

	spin_lock_irqsave(&qedi->io_trace_lock, flags);

	io_log = &qedi->io_trace_buf[qedi->io_trace_idx];
	io_log->direction = direction;
	io_log->task_id = tid;
	io_log->cid = qedi_conn->iscsi_conn_id;
	io_log->lun = sc_cmd->device->lun;
	io_log->op = sc_cmd->cmnd[0];
	op = sc_cmd->cmnd[0];
	io_log->lba[0] = sc_cmd->cmnd[2];
	io_log->lba[1] = sc_cmd->cmnd[3];
	io_log->lba[2] = sc_cmd->cmnd[4];
	io_log->lba[3] = sc_cmd->cmnd[5];
	io_log->bufflen = scsi_bufflen(sc_cmd);
	io_log->sg_count = scsi_sg_count(sc_cmd);
	io_log->fast_sgs = qedi->fast_sgls;
	io_log->cached_sgs = qedi->cached_sgls;
	io_log->slow_sgs = qedi->slow_sgls;
	io_log->cached_sge = qedi->use_cached_sge;
	io_log->slow_sge = qedi->use_slow_sge;
	io_log->fast_sge = qedi->use_fast_sge;
	io_log->result = sc_cmd->result;
	io_log->jiffies = jiffies;
	io_log->blk_req_cpu = smp_processor_id();

	if (direction == QEDI_IO_TRACE_REQ) {
		/* For requests we only care about the submission CPU */
		io_log->req_cpu = smp_processor_id() % qedi->num_queues;
		io_log->intr_cpu = 0;
		io_log->blk_rsp_cpu = 0;
	} else if (direction == QEDI_IO_TRACE_RSP) {
		io_log->req_cpu = smp_processor_id() % qedi->num_queues;
		io_log->intr_cpu = qedi->intr_cpu;
		io_log->blk_rsp_cpu = smp_processor_id();
	}

	qedi->io_trace_idx++;
	if (qedi->io_trace_idx == QEDI_IO_TRACE_SIZE)
		qedi->io_trace_idx = 0;

	qedi->use_cached_sge = false;
	qedi->use_slow_sge = false;
	qedi->use_fast_sge = false;

	spin_unlock_irqrestore(&qedi->io_trace_lock, flags);
}

int qedi_iscsi_send_ioreq(struct iscsi_task *task)
{
	struct iscsi_conn *conn = task->conn;
	struct iscsi_session *session = conn->session;
	struct Scsi_Host *shost = iscsi_session_to_shost(session->cls_session);
	struct qedi_ctx *qedi = iscsi_host_priv(shost);
	struct qedi_conn *qedi_conn = conn->dd_data;
	struct qedi_cmd *cmd = task->dd_data;
	struct scsi_cmnd *sc = task->sc;
	struct iscsi_task_context *fw_task_ctx;
	struct iscsi_cached_sge_ctx *cached_sge;
	struct iscsi_phys_sgl_ctx *phys_sgl;
	struct iscsi_virt_sgl_ctx *virt_sgl;
	struct ystorm_iscsi_task_st_ctx *yst_cxt;
	struct mstorm_iscsi_task_st_ctx *mst_cxt;
	struct iscsi_sgl *sgl_struct;
	struct iscsi_sge *single_sge;
	struct iscsi_scsi_req *hdr = (struct iscsi_scsi_req *)task->hdr;
	struct iscsi_sge *bd = cmd->io_tbl.sge_tbl;
	enum iscsi_task_type task_type;
	struct iscsi_cmd_hdr *fw_cmd;
	u32 lun[2];
	u32 exp_data;
	u16 cq_idx = smp_processor_id() % qedi->num_queues;
	s16 ptu_invalidate = 0;
	s16 tid = 0;
	u8 num_fast_sgs;

	tid = qedi_get_task_idx(qedi);
	if (tid == -1)
		return -ENOMEM;

	qedi_iscsi_map_sg_list(cmd);

	int_to_scsilun(sc->device->lun, (struct scsi_lun *)lun);
	fw_task_ctx = qedi_get_task_mem(&qedi->tasks, tid);

	memset(fw_task_ctx, 0, sizeof(struct iscsi_task_context));
	cmd->task_id = tid;

	/* Ystorm context */
	fw_cmd = &fw_task_ctx->ystorm_st_context.pdu_hdr.cmd;
	SET_FIELD(fw_cmd->flags_attr, ISCSI_CMD_HDR_ATTR, ISCSI_ATTR_SIMPLE);

	if (sc->sc_data_direction == DMA_TO_DEVICE) {
		if (conn->session->initial_r2t_en) {
			exp_data = min((conn->session->imm_data_en *
					conn->max_xmit_dlength),
				       conn->session->first_burst);
			exp_data = min(exp_data, scsi_bufflen(sc));
			fw_task_ctx->ustorm_ag_context.exp_data_acked =
							  cpu_to_le32(exp_data);
		} else {
			fw_task_ctx->ustorm_ag_context.exp_data_acked =
			      min(conn->session->first_burst, scsi_bufflen(sc));
		}

		SET_FIELD(fw_cmd->flags_attr, ISCSI_CMD_HDR_WRITE, 1);
		task_type = ISCSI_TASK_TYPE_INITIATOR_WRITE;
	} else {
		if (scsi_bufflen(sc))
			SET_FIELD(fw_cmd->flags_attr, ISCSI_CMD_HDR_READ, 1);
		task_type = ISCSI_TASK_TYPE_INITIATOR_READ;
	}

	fw_cmd->lun.lo = be32_to_cpu(lun[0]);
	fw_cmd->lun.hi = be32_to_cpu(lun[1]);

	qedi_update_itt_map(qedi, tid, task->itt, cmd);
	fw_cmd->itt = qedi_set_itt(tid, get_itt(task->itt));
	fw_cmd->expected_transfer_length = scsi_bufflen(sc);
	fw_cmd->cmd_sn = be32_to_cpu(hdr->cmdsn);
	fw_cmd->opcode = hdr->opcode;
	qedi_cpy_scsi_cdb(sc, (u32 *)fw_cmd->cdb);

	/* Mstorm context */
	fw_task_ctx->mstorm_st_context.sense_db.lo = (u32)cmd->sense_buffer_dma;
	fw_task_ctx->mstorm_st_context.sense_db.hi =
					(u32)((u64)cmd->sense_buffer_dma >> 32);
	fw_task_ctx->mstorm_ag_context.task_cid = qedi_conn->iscsi_conn_id;
	fw_task_ctx->mstorm_st_context.task_type = task_type;

	if (qedi->tid_reuse_count[tid] == QEDI_MAX_TASK_NUM) {
		ptu_invalidate = 1;
		qedi->tid_reuse_count[tid] = 0;
	}
	fw_task_ctx->ystorm_st_context.state.reuse_count =
						     qedi->tid_reuse_count[tid];
	fw_task_ctx->mstorm_st_context.reuse_count =
						   qedi->tid_reuse_count[tid]++;

	/* Ustorm context */
	fw_task_ctx->ustorm_st_context.rem_rcv_len = scsi_bufflen(sc);
	fw_task_ctx->ustorm_st_context.exp_data_transfer_len = scsi_bufflen(sc);
	fw_task_ctx->ustorm_st_context.exp_data_sn =
						   be32_to_cpu(hdr->exp_statsn);
	fw_task_ctx->ustorm_st_context.task_type = task_type;
	fw_task_ctx->ustorm_st_context.cq_rss_number = cq_idx;
	fw_task_ctx->ustorm_ag_context.icid = (u16)qedi_conn->iscsi_conn_id;

	SET_FIELD(fw_task_ctx->ustorm_ag_context.flags1,
		  USTORM_ISCSI_TASK_AG_CTX_R2T2RECV, 1);
	SET_FIELD(fw_task_ctx->ustorm_st_context.flags,
		  USTORM_ISCSI_TASK_ST_CTX_LOCAL_COMP, 0);

	num_fast_sgs = (cmd->io_tbl.sge_valid ?
			min((u16)QEDI_FAST_SGE_COUNT,
			    (u16)cmd->io_tbl.sge_valid) : 0);
	SET_FIELD(fw_task_ctx->ustorm_st_context.reg1.reg1_map,
		  ISCSI_REG1_NUM_FAST_SGES, num_fast_sgs);

	fw_task_ctx->ustorm_st_context.lun.lo = be32_to_cpu(lun[0]);
	fw_task_ctx->ustorm_st_context.lun.hi = be32_to_cpu(lun[1]);

	QEDI_INFO(&qedi->dbg_ctx, QEDI_LOG_IO, "Total sge count [%d]\n",
		  cmd->io_tbl.sge_valid);

	yst_cxt = &fw_task_ctx->ystorm_st_context;
	mst_cxt = &fw_task_ctx->mstorm_st_context;
	/* Tx path */
	if (task_type == ISCSI_TASK_TYPE_INITIATOR_WRITE) {
		/* not considering  superIO or FastIO */
		if (cmd->io_tbl.sge_valid == 1) {
			cached_sge = &yst_cxt->state.sgl_ctx_union.cached_sge;
			cached_sge->sge.sge_addr.lo = bd[0].sge_addr.lo;
			cached_sge->sge.sge_addr.hi = bd[0].sge_addr.hi;
			cached_sge->sge.sge_len = bd[0].sge_len;
			qedi->cached_sgls++;
		} else if ((cmd->io_tbl.sge_valid != 1) && cmd->use_slowpath) {
			SET_FIELD(fw_task_ctx->mstorm_st_context.flags.mflags,
				  ISCSI_MFLAGS_SLOW_IO, 1);
			SET_FIELD(fw_task_ctx->ustorm_st_context.reg1.reg1_map,
				  ISCSI_REG1_NUM_FAST_SGES, 0);
			phys_sgl = &yst_cxt->state.sgl_ctx_union.phys_sgl;
			phys_sgl->sgl_base.lo = (u32)(cmd->io_tbl.sge_tbl_dma);
			phys_sgl->sgl_base.hi =
				     (u32)((u64)cmd->io_tbl.sge_tbl_dma >> 32);
			phys_sgl->sgl_size = cmd->io_tbl.sge_valid;
			qedi->slow_sgls++;
		} else if ((cmd->io_tbl.sge_valid != 1) && !cmd->use_slowpath) {
			SET_FIELD(fw_task_ctx->mstorm_st_context.flags.mflags,
				  ISCSI_MFLAGS_SLOW_IO, 0);
			SET_FIELD(fw_task_ctx->ustorm_st_context.reg1.reg1_map,
				  ISCSI_REG1_NUM_FAST_SGES,
				  min((u16)QEDI_FAST_SGE_COUNT,
				      (u16)cmd->io_tbl.sge_valid));
			virt_sgl = &yst_cxt->state.sgl_ctx_union.virt_sgl;
			virt_sgl->sgl_base.lo = (u32)(cmd->io_tbl.sge_tbl_dma);
			virt_sgl->sgl_base.hi =
				      (u32)((u64)cmd->io_tbl.sge_tbl_dma >> 32);
			virt_sgl->sgl_initial_offset =
				 (u32)bd[0].sge_addr.lo & (QEDI_PAGE_SIZE - 1);
			qedi->fast_sgls++;
		}
		fw_task_ctx->mstorm_st_context.sgl_size = cmd->io_tbl.sge_valid;
		fw_task_ctx->mstorm_st_context.rem_task_size = scsi_bufflen(sc);
	} else {
	/* Rx path */
		if (cmd->io_tbl.sge_valid == 1) {
			SET_FIELD(fw_task_ctx->mstorm_st_context.flags.mflags,
				  ISCSI_MFLAGS_SLOW_IO, 0);
			SET_FIELD(fw_task_ctx->mstorm_st_context.flags.mflags,
				  ISCSI_MFLAGS_SINGLE_SGE, 1);
			single_sge = &mst_cxt->sgl_union.single_sge;
			single_sge->sge_addr.lo = bd[0].sge_addr.lo;
			single_sge->sge_addr.hi = bd[0].sge_addr.hi;
			single_sge->sge_len = bd[0].sge_len;
			qedi->cached_sgls++;
		} else if ((cmd->io_tbl.sge_valid != 1) && cmd->use_slowpath) {
			sgl_struct = &mst_cxt->sgl_union.sgl_struct;
			sgl_struct->sgl_addr.lo =
						(u32)(cmd->io_tbl.sge_tbl_dma);
			sgl_struct->sgl_addr.hi =
				     (u32)((u64)cmd->io_tbl.sge_tbl_dma >> 32);
			SET_FIELD(fw_task_ctx->mstorm_st_context.flags.mflags,
				  ISCSI_MFLAGS_SLOW_IO, 1);
			SET_FIELD(fw_task_ctx->ustorm_st_context.reg1.reg1_map,
				  ISCSI_REG1_NUM_FAST_SGES, 0);
			sgl_struct->updated_sge_size = 0;
			sgl_struct->updated_sge_offset = 0;
			qedi->slow_sgls++;
		} else if ((cmd->io_tbl.sge_valid != 1) && !cmd->use_slowpath) {
			sgl_struct = &mst_cxt->sgl_union.sgl_struct;
			sgl_struct->sgl_addr.lo =
						(u32)(cmd->io_tbl.sge_tbl_dma);
			sgl_struct->sgl_addr.hi =
				     (u32)((u64)cmd->io_tbl.sge_tbl_dma >> 32);
			sgl_struct->byte_offset =
				(u32)bd[0].sge_addr.lo & (QEDI_PAGE_SIZE - 1);
			SET_FIELD(fw_task_ctx->mstorm_st_context.flags.mflags,
				  ISCSI_MFLAGS_SLOW_IO, 0);
			SET_FIELD(fw_task_ctx->ustorm_st_context.reg1.reg1_map,
				  ISCSI_REG1_NUM_FAST_SGES, 0);
			sgl_struct->updated_sge_size = 0;
			sgl_struct->updated_sge_offset = 0;
			qedi->fast_sgls++;
		}
		fw_task_ctx->mstorm_st_context.sgl_size = cmd->io_tbl.sge_valid;
		fw_task_ctx->mstorm_st_context.rem_task_size = scsi_bufflen(sc);
	}

	if (cmd->io_tbl.sge_valid == 1)
		/* Singel-SGL */
		qedi->use_cached_sge = true;
	else {
		if (cmd->use_slowpath)
			qedi->use_slow_sge = true;
		else
			qedi->use_fast_sge = true;
	}
	QEDI_INFO(&qedi->dbg_ctx, QEDI_LOG_IO,
		  "%s: %s-SGL: num_sges=0x%x first-sge-lo=0x%x first-sge-hi=0x%x",
		  (task_type == ISCSI_TASK_TYPE_INITIATOR_WRITE) ?
		  "Write " : "Read ", (cmd->io_tbl.sge_valid == 1) ?
		  "Single" : (cmd->use_slowpath ? "SLOW" : "FAST"),
		  (u16)cmd->io_tbl.sge_valid, (u32)(cmd->io_tbl.sge_tbl_dma),
		  (u32)((u64)cmd->io_tbl.sge_tbl_dma >> 32));

	/*  Add command in active command list */
	spin_lock(&qedi_conn->list_lock);
	list_add_tail(&cmd->io_cmd, &qedi_conn->active_cmd_list);
	cmd->io_cmd_in_list = true;
	qedi_conn->active_cmd_count++;
	spin_unlock(&qedi_conn->list_lock);

	qedi_add_to_sq(qedi_conn, task, tid, ptu_invalidate, false);
	qedi_ring_doorbell(qedi_conn);
	if (qedi_io_tracing)
		qedi_trace_io(qedi, task, tid, QEDI_IO_TRACE_REQ);

	return 0;
}

int qedi_iscsi_cleanup_task(struct iscsi_task *task, bool mark_cmd_node_deleted)
{
	struct iscsi_conn *conn = task->conn;
	struct qedi_conn *qedi_conn = conn->dd_data;
	struct qedi_cmd *cmd = task->dd_data;
	s16 ptu_invalidate = 0;

	QEDI_INFO(&qedi_conn->qedi->dbg_ctx, QEDI_LOG_SCSI_TM,
		  "issue cleanup tid=0x%x itt=0x%x task_state=%d cmd_state=0%x cid=0x%x\n",
		  cmd->task_id, get_itt(task->itt), task->state,
		  cmd->state, qedi_conn->iscsi_conn_id);

	qedi_add_to_sq(qedi_conn, task, cmd->task_id, ptu_invalidate, true);
	qedi_ring_doorbell(qedi_conn);

	return 0;
}

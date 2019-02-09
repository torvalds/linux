/*******************************************************************************
 * This file contains the iSCSI Target specific Task Management functions.
 *
 * (c) Copyright 2007-2013 Datera, Inc.
 *
 * Author: Nicholas A. Bellinger <nab@linux-iscsi.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 ******************************************************************************/

#include <asm/unaligned.h>
#include <scsi/scsi_proto.h>
#include <scsi/iscsi_proto.h>
#include <target/target_core_base.h>
#include <target/target_core_fabric.h>
#include <target/iscsi/iscsi_transport.h>

#include <target/iscsi/iscsi_target_core.h>
#include "iscsi_target_seq_pdu_list.h"
#include "iscsi_target_datain_values.h"
#include "iscsi_target_device.h"
#include "iscsi_target_erl0.h"
#include "iscsi_target_erl1.h"
#include "iscsi_target_erl2.h"
#include "iscsi_target_tmr.h"
#include "iscsi_target_tpg.h"
#include "iscsi_target_util.h"
#include "iscsi_target.h"

u8 iscsit_tmr_abort_task(
	struct iscsi_cmd *cmd,
	unsigned char *buf)
{
	struct iscsi_cmd *ref_cmd;
	struct iscsi_conn *conn = cmd->conn;
	struct iscsi_tmr_req *tmr_req = cmd->tmr_req;
	struct se_tmr_req *se_tmr = cmd->se_cmd.se_tmr_req;
	struct iscsi_tm *hdr = (struct iscsi_tm *) buf;

	ref_cmd = iscsit_find_cmd_from_itt(conn, hdr->rtt);
	if (!ref_cmd) {
		pr_err("Unable to locate RefTaskTag: 0x%08x on CID:"
			" %hu.\n", hdr->rtt, conn->cid);
		return (iscsi_sna_gte(be32_to_cpu(hdr->refcmdsn), conn->sess->exp_cmd_sn) &&
			iscsi_sna_lte(be32_to_cpu(hdr->refcmdsn), (u32) atomic_read(&conn->sess->max_cmd_sn))) ?
			ISCSI_TMF_RSP_COMPLETE : ISCSI_TMF_RSP_NO_TASK;
	}
	if (ref_cmd->cmd_sn != be32_to_cpu(hdr->refcmdsn)) {
		pr_err("RefCmdSN 0x%08x does not equal"
			" task's CmdSN 0x%08x. Rejecting ABORT_TASK.\n",
			hdr->refcmdsn, ref_cmd->cmd_sn);
		return ISCSI_TMF_RSP_REJECTED;
	}

	se_tmr->ref_task_tag		= (__force u32)hdr->rtt;
	tmr_req->ref_cmd		= ref_cmd;
	tmr_req->exp_data_sn		= be32_to_cpu(hdr->exp_datasn);

	return ISCSI_TMF_RSP_COMPLETE;
}

/*
 *	Called from iscsit_handle_task_mgt_cmd().
 */
int iscsit_tmr_task_warm_reset(
	struct iscsi_conn *conn,
	struct iscsi_tmr_req *tmr_req,
	unsigned char *buf)
{
	struct iscsi_session *sess = conn->sess;
	struct iscsi_node_attrib *na = iscsit_tpg_get_node_attrib(sess);

	if (!na->tmr_warm_reset) {
		pr_err("TMR Opcode TARGET_WARM_RESET authorization"
			" failed for Initiator Node: %s\n",
			sess->se_sess->se_node_acl->initiatorname);
		return -1;
	}
	/*
	 * Do the real work in transport_generic_do_tmr().
	 */
	return 0;
}

int iscsit_tmr_task_cold_reset(
	struct iscsi_conn *conn,
	struct iscsi_tmr_req *tmr_req,
	unsigned char *buf)
{
	struct iscsi_session *sess = conn->sess;
	struct iscsi_node_attrib *na = iscsit_tpg_get_node_attrib(sess);

	if (!na->tmr_cold_reset) {
		pr_err("TMR Opcode TARGET_COLD_RESET authorization"
			" failed for Initiator Node: %s\n",
			sess->se_sess->se_node_acl->initiatorname);
		return -1;
	}
	/*
	 * Do the real work in transport_generic_do_tmr().
	 */
	return 0;
}

u8 iscsit_tmr_task_reassign(
	struct iscsi_cmd *cmd,
	unsigned char *buf)
{
	struct iscsi_cmd *ref_cmd = NULL;
	struct iscsi_conn *conn = cmd->conn;
	struct iscsi_conn_recovery *cr = NULL;
	struct iscsi_tmr_req *tmr_req = cmd->tmr_req;
	struct se_tmr_req *se_tmr = cmd->se_cmd.se_tmr_req;
	struct iscsi_tm *hdr = (struct iscsi_tm *) buf;
	u64 ret, ref_lun;

	pr_debug("Got TASK_REASSIGN TMR ITT: 0x%08x,"
		" RefTaskTag: 0x%08x, ExpDataSN: 0x%08x, CID: %hu\n",
		hdr->itt, hdr->rtt, hdr->exp_datasn, conn->cid);

	if (conn->sess->sess_ops->ErrorRecoveryLevel != 2) {
		pr_err("TMR TASK_REASSIGN not supported in ERL<2,"
				" ignoring request.\n");
		return ISCSI_TMF_RSP_NOT_SUPPORTED;
	}

	ret = iscsit_find_cmd_for_recovery(conn->sess, &ref_cmd, &cr, hdr->rtt);
	if (ret == -2) {
		pr_err("Command ITT: 0x%08x is still alligent to CID:"
			" %hu\n", ref_cmd->init_task_tag, cr->cid);
		return ISCSI_TMF_RSP_TASK_ALLEGIANT;
	} else if (ret == -1) {
		pr_err("Unable to locate RefTaskTag: 0x%08x in"
			" connection recovery command list.\n", hdr->rtt);
		return ISCSI_TMF_RSP_NO_TASK;
	}
	/*
	 * Temporary check to prevent connection recovery for
	 * connections with a differing Max*DataSegmentLength.
	 */
	if (cr->maxrecvdatasegmentlength !=
	    conn->conn_ops->MaxRecvDataSegmentLength) {
		pr_err("Unable to perform connection recovery for"
			" differing MaxRecvDataSegmentLength, rejecting"
			" TMR TASK_REASSIGN.\n");
		return ISCSI_TMF_RSP_REJECTED;
	}
	if (cr->maxxmitdatasegmentlength !=
	    conn->conn_ops->MaxXmitDataSegmentLength) {
		pr_err("Unable to perform connection recovery for"
			" differing MaxXmitDataSegmentLength, rejecting"
			" TMR TASK_REASSIGN.\n");
		return ISCSI_TMF_RSP_REJECTED;
	}

	ref_lun = scsilun_to_int(&hdr->lun);
	if (ref_lun != ref_cmd->se_cmd.orig_fe_lun) {
		pr_err("Unable to perform connection recovery for"
			" differing ref_lun: %llu ref_cmd orig_fe_lun: %llu\n",
			ref_lun, ref_cmd->se_cmd.orig_fe_lun);
		return ISCSI_TMF_RSP_REJECTED;
	}

	se_tmr->ref_task_tag		= (__force u32)hdr->rtt;
	tmr_req->ref_cmd		= ref_cmd;
	tmr_req->exp_data_sn		= be32_to_cpu(hdr->exp_datasn);
	tmr_req->conn_recovery		= cr;
	tmr_req->task_reassign		= 1;
	/*
	 * Command can now be reassigned to a new connection.
	 * The task management response must be sent before the
	 * reassignment actually happens.  See iscsi_tmr_post_handler().
	 */
	return ISCSI_TMF_RSP_COMPLETE;
}

static void iscsit_task_reassign_remove_cmd(
	struct iscsi_cmd *cmd,
	struct iscsi_conn_recovery *cr,
	struct iscsi_session *sess)
{
	int ret;

	spin_lock(&cr->conn_recovery_cmd_lock);
	ret = iscsit_remove_cmd_from_connection_recovery(cmd, sess);
	spin_unlock(&cr->conn_recovery_cmd_lock);
	if (!ret) {
		pr_debug("iSCSI connection recovery successful for CID:"
			" %hu on SID: %u\n", cr->cid, sess->sid);
		iscsit_remove_active_connection_recovery_entry(cr, sess);
	}
}

static int iscsit_task_reassign_complete_nop_out(
	struct iscsi_tmr_req *tmr_req,
	struct iscsi_conn *conn)
{
	struct iscsi_cmd *cmd = tmr_req->ref_cmd;
	struct iscsi_conn_recovery *cr;

	if (!cmd->cr) {
		pr_err("struct iscsi_conn_recovery pointer for ITT: 0x%08x"
			" is NULL!\n", cmd->init_task_tag);
		return -1;
	}
	cr = cmd->cr;

	/*
	 * Reset the StatSN so a new one for this commands new connection
	 * will be assigned.
	 * Reset the ExpStatSN as well so we may receive Status SNACKs.
	 */
	cmd->stat_sn = cmd->exp_stat_sn = 0;

	iscsit_task_reassign_remove_cmd(cmd, cr, conn->sess);

	spin_lock_bh(&conn->cmd_lock);
	list_add_tail(&cmd->i_conn_node, &conn->conn_cmd_list);
	spin_unlock_bh(&conn->cmd_lock);

	cmd->i_state = ISTATE_SEND_NOPIN;
	iscsit_add_cmd_to_response_queue(cmd, conn, cmd->i_state);
	return 0;
}

static int iscsit_task_reassign_complete_write(
	struct iscsi_cmd *cmd,
	struct iscsi_tmr_req *tmr_req)
{
	int no_build_r2ts = 0;
	u32 length = 0, offset = 0;
	struct iscsi_conn *conn = cmd->conn;
	struct se_cmd *se_cmd = &cmd->se_cmd;
	/*
	 * The Initiator must not send a R2T SNACK with a Begrun less than
	 * the TMR TASK_REASSIGN's ExpDataSN.
	 */
	if (!tmr_req->exp_data_sn) {
		cmd->cmd_flags &= ~ICF_GOT_DATACK_SNACK;
		cmd->acked_data_sn = 0;
	} else {
		cmd->cmd_flags |= ICF_GOT_DATACK_SNACK;
		cmd->acked_data_sn = (tmr_req->exp_data_sn - 1);
	}

	/*
	 * The TMR TASK_REASSIGN's ExpDataSN contains the next R2TSN the
	 * Initiator is expecting.  The Target controls all WRITE operations
	 * so if we have received all DataOUT we can safety ignore Initiator.
	 */
	if (cmd->cmd_flags & ICF_GOT_LAST_DATAOUT) {
		if (!(cmd->se_cmd.transport_state & CMD_T_SENT)) {
			pr_debug("WRITE ITT: 0x%08x: t_state: %d"
				" never sent to transport\n",
				cmd->init_task_tag, cmd->se_cmd.t_state);
			target_execute_cmd(se_cmd);
			return 0;
		}

		cmd->i_state = ISTATE_SEND_STATUS;
		iscsit_add_cmd_to_response_queue(cmd, conn, cmd->i_state);
		return 0;
	}

	/*
	 * Special case to deal with DataSequenceInOrder=No and Non-Immeidate
	 * Unsolicited DataOut.
	 */
	if (cmd->unsolicited_data) {
		cmd->unsolicited_data = 0;

		offset = cmd->next_burst_len = cmd->write_data_done;

		if ((conn->sess->sess_ops->FirstBurstLength - offset) >=
		     cmd->se_cmd.data_length) {
			no_build_r2ts = 1;
			length = (cmd->se_cmd.data_length - offset);
		} else
			length = (conn->sess->sess_ops->FirstBurstLength - offset);

		spin_lock_bh(&cmd->r2t_lock);
		if (iscsit_add_r2t_to_list(cmd, offset, length, 0, 0) < 0) {
			spin_unlock_bh(&cmd->r2t_lock);
			return -1;
		}
		cmd->outstanding_r2ts++;
		spin_unlock_bh(&cmd->r2t_lock);

		if (no_build_r2ts)
			return 0;
	}
	/*
	 * iscsit_build_r2ts_for_cmd() can handle the rest from here.
	 */
	return conn->conn_transport->iscsit_get_dataout(conn, cmd, true);
}

static int iscsit_task_reassign_complete_read(
	struct iscsi_cmd *cmd,
	struct iscsi_tmr_req *tmr_req)
{
	struct iscsi_conn *conn = cmd->conn;
	struct iscsi_datain_req *dr;
	struct se_cmd *se_cmd = &cmd->se_cmd;
	/*
	 * The Initiator must not send a Data SNACK with a BegRun less than
	 * the TMR TASK_REASSIGN's ExpDataSN.
	 */
	if (!tmr_req->exp_data_sn) {
		cmd->cmd_flags &= ~ICF_GOT_DATACK_SNACK;
		cmd->acked_data_sn = 0;
	} else {
		cmd->cmd_flags |= ICF_GOT_DATACK_SNACK;
		cmd->acked_data_sn = (tmr_req->exp_data_sn - 1);
	}

	if (!(cmd->se_cmd.transport_state & CMD_T_SENT)) {
		pr_debug("READ ITT: 0x%08x: t_state: %d never sent to"
			" transport\n", cmd->init_task_tag,
			cmd->se_cmd.t_state);
		transport_handle_cdb_direct(se_cmd);
		return 0;
	}

	if (!(se_cmd->transport_state & CMD_T_COMPLETE)) {
		pr_err("READ ITT: 0x%08x: t_state: %d, never returned"
			" from transport\n", cmd->init_task_tag,
			cmd->se_cmd.t_state);
		return -1;
	}

	dr = iscsit_allocate_datain_req();
	if (!dr)
		return -1;
	/*
	 * The TMR TASK_REASSIGN's ExpDataSN contains the next DataSN the
	 * Initiator is expecting.
	 */
	dr->data_sn = dr->begrun = tmr_req->exp_data_sn;
	dr->runlength = 0;
	dr->generate_recovery_values = 1;
	dr->recovery = DATAIN_CONNECTION_RECOVERY;

	iscsit_attach_datain_req(cmd, dr);

	cmd->i_state = ISTATE_SEND_DATAIN;
	iscsit_add_cmd_to_response_queue(cmd, conn, cmd->i_state);
	return 0;
}

static int iscsit_task_reassign_complete_none(
	struct iscsi_cmd *cmd,
	struct iscsi_tmr_req *tmr_req)
{
	struct iscsi_conn *conn = cmd->conn;

	cmd->i_state = ISTATE_SEND_STATUS;
	iscsit_add_cmd_to_response_queue(cmd, conn, cmd->i_state);
	return 0;
}

static int iscsit_task_reassign_complete_scsi_cmnd(
	struct iscsi_tmr_req *tmr_req,
	struct iscsi_conn *conn)
{
	struct iscsi_cmd *cmd = tmr_req->ref_cmd;
	struct iscsi_conn_recovery *cr;

	if (!cmd->cr) {
		pr_err("struct iscsi_conn_recovery pointer for ITT: 0x%08x"
			" is NULL!\n", cmd->init_task_tag);
		return -1;
	}
	cr = cmd->cr;

	/*
	 * Reset the StatSN so a new one for this commands new connection
	 * will be assigned.
	 * Reset the ExpStatSN as well so we may receive Status SNACKs.
	 */
	cmd->stat_sn = cmd->exp_stat_sn = 0;

	iscsit_task_reassign_remove_cmd(cmd, cr, conn->sess);

	spin_lock_bh(&conn->cmd_lock);
	list_add_tail(&cmd->i_conn_node, &conn->conn_cmd_list);
	spin_unlock_bh(&conn->cmd_lock);

	if (cmd->se_cmd.se_cmd_flags & SCF_SENT_CHECK_CONDITION) {
		cmd->i_state = ISTATE_SEND_STATUS;
		iscsit_add_cmd_to_response_queue(cmd, conn, cmd->i_state);
		return 0;
	}

	switch (cmd->data_direction) {
	case DMA_TO_DEVICE:
		return iscsit_task_reassign_complete_write(cmd, tmr_req);
	case DMA_FROM_DEVICE:
		return iscsit_task_reassign_complete_read(cmd, tmr_req);
	case DMA_NONE:
		return iscsit_task_reassign_complete_none(cmd, tmr_req);
	default:
		pr_err("Unknown cmd->data_direction: 0x%02x\n",
				cmd->data_direction);
		return -1;
	}

	return 0;
}

static int iscsit_task_reassign_complete(
	struct iscsi_tmr_req *tmr_req,
	struct iscsi_conn *conn)
{
	struct iscsi_cmd *cmd;
	int ret = 0;

	if (!tmr_req->ref_cmd) {
		pr_err("TMR Request is missing a RefCmd struct iscsi_cmd.\n");
		return -1;
	}
	cmd = tmr_req->ref_cmd;

	cmd->conn = conn;

	switch (cmd->iscsi_opcode) {
	case ISCSI_OP_NOOP_OUT:
		ret = iscsit_task_reassign_complete_nop_out(tmr_req, conn);
		break;
	case ISCSI_OP_SCSI_CMD:
		ret = iscsit_task_reassign_complete_scsi_cmnd(tmr_req, conn);
		break;
	default:
		 pr_err("Illegal iSCSI Opcode 0x%02x during"
			" command reallegiance\n", cmd->iscsi_opcode);
		return -1;
	}

	if (ret != 0)
		return ret;

	pr_debug("Completed connection reallegiance for Opcode: 0x%02x,"
		" ITT: 0x%08x to CID: %hu.\n", cmd->iscsi_opcode,
			cmd->init_task_tag, conn->cid);

	return 0;
}

/*
 *	Handles special after-the-fact actions related to TMRs.
 *	Right now the only one that its really needed for is
 *	connection recovery releated TASK_REASSIGN.
 */
int iscsit_tmr_post_handler(struct iscsi_cmd *cmd, struct iscsi_conn *conn)
{
	struct iscsi_tmr_req *tmr_req = cmd->tmr_req;
	struct se_tmr_req *se_tmr = cmd->se_cmd.se_tmr_req;

	if (tmr_req->task_reassign &&
	   (se_tmr->response == ISCSI_TMF_RSP_COMPLETE))
		return iscsit_task_reassign_complete(tmr_req, conn);

	return 0;
}
EXPORT_SYMBOL(iscsit_tmr_post_handler);

/*
 *	Nothing to do here, but leave it for good measure. :-)
 */
static int iscsit_task_reassign_prepare_read(
	struct iscsi_tmr_req *tmr_req,
	struct iscsi_conn *conn)
{
	return 0;
}

static void iscsit_task_reassign_prepare_unsolicited_dataout(
	struct iscsi_cmd *cmd,
	struct iscsi_conn *conn)
{
	int i, j;
	struct iscsi_pdu *pdu = NULL;
	struct iscsi_seq *seq = NULL;

	if (conn->sess->sess_ops->DataSequenceInOrder) {
		cmd->data_sn = 0;

		if (cmd->immediate_data)
			cmd->r2t_offset += (cmd->first_burst_len -
				cmd->seq_start_offset);

		if (conn->sess->sess_ops->DataPDUInOrder) {
			cmd->write_data_done -= (cmd->immediate_data) ?
						(cmd->first_burst_len -
						 cmd->seq_start_offset) :
						 cmd->first_burst_len;
			cmd->first_burst_len = 0;
			return;
		}

		for (i = 0; i < cmd->pdu_count; i++) {
			pdu = &cmd->pdu_list[i];

			if (pdu->status != ISCSI_PDU_RECEIVED_OK)
				continue;

			if ((pdu->offset >= cmd->seq_start_offset) &&
			   ((pdu->offset + pdu->length) <=
			     cmd->seq_end_offset)) {
				cmd->first_burst_len -= pdu->length;
				cmd->write_data_done -= pdu->length;
				pdu->status = ISCSI_PDU_NOT_RECEIVED;
			}
		}
	} else {
		for (i = 0; i < cmd->seq_count; i++) {
			seq = &cmd->seq_list[i];

			if (seq->type != SEQTYPE_UNSOLICITED)
				continue;

			cmd->write_data_done -=
					(seq->offset - seq->orig_offset);
			cmd->first_burst_len = 0;
			seq->data_sn = 0;
			seq->offset = seq->orig_offset;
			seq->next_burst_len = 0;
			seq->status = DATAOUT_SEQUENCE_WITHIN_COMMAND_RECOVERY;

			if (conn->sess->sess_ops->DataPDUInOrder)
				continue;

			for (j = 0; j < seq->pdu_count; j++) {
				pdu = &cmd->pdu_list[j+seq->pdu_start];

				if (pdu->status != ISCSI_PDU_RECEIVED_OK)
					continue;

				pdu->status = ISCSI_PDU_NOT_RECEIVED;
			}
		}
	}
}

static int iscsit_task_reassign_prepare_write(
	struct iscsi_tmr_req *tmr_req,
	struct iscsi_conn *conn)
{
	struct iscsi_cmd *cmd = tmr_req->ref_cmd;
	struct iscsi_pdu *pdu = NULL;
	struct iscsi_r2t *r2t = NULL, *r2t_tmp;
	int first_incomplete_r2t = 1, i = 0;

	/*
	 * The command was in the process of receiving Unsolicited DataOUT when
	 * the connection failed.
	 */
	if (cmd->unsolicited_data)
		iscsit_task_reassign_prepare_unsolicited_dataout(cmd, conn);

	/*
	 * The Initiator is requesting R2Ts starting from zero,  skip
	 * checking acknowledged R2Ts and start checking struct iscsi_r2ts
	 * greater than zero.
	 */
	if (!tmr_req->exp_data_sn)
		goto drop_unacknowledged_r2ts;

	/*
	 * We now check that the PDUs in DataOUT sequences below
	 * the TMR TASK_REASSIGN ExpDataSN (R2TSN the Initiator is
	 * expecting next) have all the DataOUT they require to complete
	 * the DataOUT sequence.  First scan from R2TSN 0 to TMR
	 * TASK_REASSIGN ExpDataSN-1.
	 *
	 * If we have not received all DataOUT in question,  we must
	 * make sure to make the appropriate changes to values in
	 * struct iscsi_cmd (and elsewhere depending on session parameters)
	 * so iscsit_build_r2ts_for_cmd() in iscsit_task_reassign_complete_write()
	 * will resend a new R2T for the DataOUT sequences in question.
	 */
	spin_lock_bh(&cmd->r2t_lock);
	if (list_empty(&cmd->cmd_r2t_list)) {
		spin_unlock_bh(&cmd->r2t_lock);
		return -1;
	}

	list_for_each_entry(r2t, &cmd->cmd_r2t_list, r2t_list) {

		if (r2t->r2t_sn >= tmr_req->exp_data_sn)
			continue;
		/*
		 * Safely ignore Recovery R2Ts and R2Ts that have completed
		 * DataOUT sequences.
		 */
		if (r2t->seq_complete)
			continue;

		if (r2t->recovery_r2t)
			continue;

		/*
		 *                 DataSequenceInOrder=Yes:
		 *
		 * Taking into account the iSCSI implementation requirement of
		 * MaxOutstandingR2T=1 while ErrorRecoveryLevel>0 and
		 * DataSequenceInOrder=Yes, we must take into consideration
		 * the following:
		 *
		 *                  DataSequenceInOrder=No:
		 *
		 * Taking into account that the Initiator controls the (possibly
		 * random) PDU Order in (possibly random) Sequence Order of
		 * DataOUT the target requests with R2Ts,  we must take into
		 * consideration the following:
		 *
		 *      DataPDUInOrder=Yes for DataSequenceInOrder=[Yes,No]:
		 *
		 * While processing non-complete R2T DataOUT sequence requests
		 * the Target will re-request only the total sequence length
		 * minus current received offset.  This is because we must
		 * assume the initiator will continue sending DataOUT from the
		 * last PDU before the connection failed.
		 *
		 *      DataPDUInOrder=No for DataSequenceInOrder=[Yes,No]:
		 *
		 * While processing non-complete R2T DataOUT sequence requests
		 * the Target will re-request the entire DataOUT sequence if
		 * any single PDU is missing from the sequence.  This is because
		 * we have no logical method to determine the next PDU offset,
		 * and we must assume the Initiator will be sending any random
		 * PDU offset in the current sequence after TASK_REASSIGN
		 * has completed.
		 */
		if (conn->sess->sess_ops->DataSequenceInOrder) {
			if (!first_incomplete_r2t) {
				cmd->r2t_offset -= r2t->xfer_len;
				goto next;
			}

			if (conn->sess->sess_ops->DataPDUInOrder) {
				cmd->data_sn = 0;
				cmd->r2t_offset -= (r2t->xfer_len -
					cmd->next_burst_len);
				first_incomplete_r2t = 0;
				goto next;
			}

			cmd->data_sn = 0;
			cmd->r2t_offset -= r2t->xfer_len;

			for (i = 0; i < cmd->pdu_count; i++) {
				pdu = &cmd->pdu_list[i];

				if (pdu->status != ISCSI_PDU_RECEIVED_OK)
					continue;

				if ((pdu->offset >= r2t->offset) &&
				    (pdu->offset < (r2t->offset +
						r2t->xfer_len))) {
					cmd->next_burst_len -= pdu->length;
					cmd->write_data_done -= pdu->length;
					pdu->status = ISCSI_PDU_NOT_RECEIVED;
				}
			}

			first_incomplete_r2t = 0;
		} else {
			struct iscsi_seq *seq;

			seq = iscsit_get_seq_holder(cmd, r2t->offset,
					r2t->xfer_len);
			if (!seq) {
				spin_unlock_bh(&cmd->r2t_lock);
				return -1;
			}

			cmd->write_data_done -=
					(seq->offset - seq->orig_offset);
			seq->data_sn = 0;
			seq->offset = seq->orig_offset;
			seq->next_burst_len = 0;
			seq->status = DATAOUT_SEQUENCE_WITHIN_COMMAND_RECOVERY;

			cmd->seq_send_order--;

			if (conn->sess->sess_ops->DataPDUInOrder)
				goto next;

			for (i = 0; i < seq->pdu_count; i++) {
				pdu = &cmd->pdu_list[i+seq->pdu_start];

				if (pdu->status != ISCSI_PDU_RECEIVED_OK)
					continue;

				pdu->status = ISCSI_PDU_NOT_RECEIVED;
			}
		}

next:
		cmd->outstanding_r2ts--;
	}
	spin_unlock_bh(&cmd->r2t_lock);

	/*
	 * We now drop all unacknowledged R2Ts, ie: ExpDataSN from TMR
	 * TASK_REASSIGN to the last R2T in the list..  We are also careful
	 * to check that the Initiator is not requesting R2Ts for DataOUT
	 * sequences it has already completed.
	 *
	 * Free each R2T in question and adjust values in struct iscsi_cmd
	 * accordingly so iscsit_build_r2ts_for_cmd() do the rest of
	 * the work after the TMR TASK_REASSIGN Response is sent.
	 */
drop_unacknowledged_r2ts:

	cmd->cmd_flags &= ~ICF_SENT_LAST_R2T;
	cmd->r2t_sn = tmr_req->exp_data_sn;

	spin_lock_bh(&cmd->r2t_lock);
	list_for_each_entry_safe(r2t, r2t_tmp, &cmd->cmd_r2t_list, r2t_list) {
		/*
		 * Skip up to the R2T Sequence number provided by the
		 * iSCSI TASK_REASSIGN TMR
		 */
		if (r2t->r2t_sn < tmr_req->exp_data_sn)
			continue;

		if (r2t->seq_complete) {
			pr_err("Initiator is requesting R2Ts from"
				" R2TSN: 0x%08x, but R2TSN: 0x%08x, Offset: %u,"
				" Length: %u is already complete."
				"   BAD INITIATOR ERL=2 IMPLEMENTATION!\n",
				tmr_req->exp_data_sn, r2t->r2t_sn,
				r2t->offset, r2t->xfer_len);
			spin_unlock_bh(&cmd->r2t_lock);
			return -1;
		}

		if (r2t->recovery_r2t) {
			iscsit_free_r2t(r2t, cmd);
			continue;
		}

		/*		   DataSequenceInOrder=Yes:
		 *
		 * Taking into account the iSCSI implementation requirement of
		 * MaxOutstandingR2T=1 while ErrorRecoveryLevel>0 and
		 * DataSequenceInOrder=Yes, it's safe to subtract the R2Ts
		 * entire transfer length from the commands R2T offset marker.
		 *
		 *		   DataSequenceInOrder=No:
		 *
		 * We subtract the difference from struct iscsi_seq between the
		 * current offset and original offset from cmd->write_data_done
		 * for account for DataOUT PDUs already received.  Then reset
		 * the current offset to the original and zero out the current
		 * burst length,  to make sure we re-request the entire DataOUT
		 * sequence.
		 */
		if (conn->sess->sess_ops->DataSequenceInOrder)
			cmd->r2t_offset -= r2t->xfer_len;
		else
			cmd->seq_send_order--;

		cmd->outstanding_r2ts--;
		iscsit_free_r2t(r2t, cmd);
	}
	spin_unlock_bh(&cmd->r2t_lock);

	return 0;
}

/*
 *	Performs sanity checks TMR TASK_REASSIGN's ExpDataSN for
 *	a given struct iscsi_cmd.
 */
int iscsit_check_task_reassign_expdatasn(
	struct iscsi_tmr_req *tmr_req,
	struct iscsi_conn *conn)
{
	struct iscsi_cmd *ref_cmd = tmr_req->ref_cmd;

	if (ref_cmd->iscsi_opcode != ISCSI_OP_SCSI_CMD)
		return 0;

	if (ref_cmd->se_cmd.se_cmd_flags & SCF_SENT_CHECK_CONDITION)
		return 0;

	if (ref_cmd->data_direction == DMA_NONE)
		return 0;

	/*
	 * For READs the TMR TASK_REASSIGNs ExpDataSN contains the next DataSN
	 * of DataIN the Initiator is expecting.
	 *
	 * Also check that the Initiator is not re-requesting DataIN that has
	 * already been acknowledged with a DataAck SNACK.
	 */
	if (ref_cmd->data_direction == DMA_FROM_DEVICE) {
		if (tmr_req->exp_data_sn > ref_cmd->data_sn) {
			pr_err("Received ExpDataSN: 0x%08x for READ"
				" in TMR TASK_REASSIGN greater than command's"
				" DataSN: 0x%08x.\n", tmr_req->exp_data_sn,
				ref_cmd->data_sn);
			return -1;
		}
		if ((ref_cmd->cmd_flags & ICF_GOT_DATACK_SNACK) &&
		    (tmr_req->exp_data_sn <= ref_cmd->acked_data_sn)) {
			pr_err("Received ExpDataSN: 0x%08x for READ"
				" in TMR TASK_REASSIGN for previously"
				" acknowledged DataIN: 0x%08x,"
				" protocol error\n", tmr_req->exp_data_sn,
				ref_cmd->acked_data_sn);
			return -1;
		}
		return iscsit_task_reassign_prepare_read(tmr_req, conn);
	}

	/*
	 * For WRITEs the TMR TASK_REASSIGNs ExpDataSN contains the next R2TSN
	 * for R2Ts the Initiator is expecting.
	 *
	 * Do the magic in iscsit_task_reassign_prepare_write().
	 */
	if (ref_cmd->data_direction == DMA_TO_DEVICE) {
		if (tmr_req->exp_data_sn > ref_cmd->r2t_sn) {
			pr_err("Received ExpDataSN: 0x%08x for WRITE"
				" in TMR TASK_REASSIGN greater than command's"
				" R2TSN: 0x%08x.\n", tmr_req->exp_data_sn,
					ref_cmd->r2t_sn);
			return -1;
		}
		return iscsit_task_reassign_prepare_write(tmr_req, conn);
	}

	pr_err("Unknown iSCSI data_direction: 0x%02x\n",
			ref_cmd->data_direction);

	return -1;
}

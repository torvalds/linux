/*******************************************************************************
 * This file contains error recovery level one used by the iSCSI Target driver.
 *
 * \u00a9 Copyright 2007-2011 RisingTide Systems LLC.
 *
 * Licensed to the Linux Foundation under the General Public License (GPL) version 2.
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

#include <linux/list.h>
#include <scsi/iscsi_proto.h>
#include <target/target_core_base.h>
#include <target/target_core_fabric.h>

#include "iscsi_target_core.h"
#include "iscsi_target_seq_pdu_list.h"
#include "iscsi_target_datain_values.h"
#include "iscsi_target_device.h"
#include "iscsi_target_tpg.h"
#include "iscsi_target_util.h"
#include "iscsi_target_erl0.h"
#include "iscsi_target_erl1.h"
#include "iscsi_target_erl2.h"
#include "iscsi_target.h"

#define OFFLOAD_BUF_SIZE	32768

/*
 *	Used to dump excess datain payload for certain error recovery
 *	situations.  Receive in OFFLOAD_BUF_SIZE max of datain per rx_data().
 *
 *	dump_padding_digest denotes if padding and data digests need
 *	to be dumped.
 */
int iscsit_dump_data_payload(
	struct iscsi_conn *conn,
	u32 buf_len,
	int dump_padding_digest)
{
	char *buf, pad_bytes[4];
	int ret = DATAOUT_WITHIN_COMMAND_RECOVERY, rx_got;
	u32 length, padding, offset = 0, size;
	struct kvec iov;

	length = (buf_len > OFFLOAD_BUF_SIZE) ? OFFLOAD_BUF_SIZE : buf_len;

	buf = kzalloc(length, GFP_ATOMIC);
	if (!buf) {
		pr_err("Unable to allocate %u bytes for offload"
				" buffer.\n", length);
		return -1;
	}
	memset(&iov, 0, sizeof(struct kvec));

	while (offset < buf_len) {
		size = ((offset + length) > buf_len) ?
			(buf_len - offset) : length;

		iov.iov_len = size;
		iov.iov_base = buf;

		rx_got = rx_data(conn, &iov, 1, size);
		if (rx_got != size) {
			ret = DATAOUT_CANNOT_RECOVER;
			goto out;
		}

		offset += size;
	}

	if (!dump_padding_digest)
		goto out;

	padding = ((-buf_len) & 3);
	if (padding != 0) {
		iov.iov_len = padding;
		iov.iov_base = pad_bytes;

		rx_got = rx_data(conn, &iov, 1, padding);
		if (rx_got != padding) {
			ret = DATAOUT_CANNOT_RECOVER;
			goto out;
		}
	}

	if (conn->conn_ops->DataDigest) {
		u32 data_crc;

		iov.iov_len = ISCSI_CRC_LEN;
		iov.iov_base = &data_crc;

		rx_got = rx_data(conn, &iov, 1, ISCSI_CRC_LEN);
		if (rx_got != ISCSI_CRC_LEN) {
			ret = DATAOUT_CANNOT_RECOVER;
			goto out;
		}
	}

out:
	kfree(buf);
	return ret;
}

/*
 *	Used for retransmitting R2Ts from a R2T SNACK request.
 */
static int iscsit_send_recovery_r2t_for_snack(
	struct iscsi_cmd *cmd,
	struct iscsi_r2t *r2t)
{
	/*
	 * If the struct iscsi_r2t has not been sent yet, we can safely
	 * ignore retransmission
	 * of the R2TSN in question.
	 */
	spin_lock_bh(&cmd->r2t_lock);
	if (!r2t->sent_r2t) {
		spin_unlock_bh(&cmd->r2t_lock);
		return 0;
	}
	r2t->sent_r2t = 0;
	spin_unlock_bh(&cmd->r2t_lock);

	iscsit_add_cmd_to_immediate_queue(cmd, cmd->conn, ISTATE_SEND_R2T);

	return 0;
}

static int iscsit_handle_r2t_snack(
	struct iscsi_cmd *cmd,
	unsigned char *buf,
	u32 begrun,
	u32 runlength)
{
	u32 last_r2tsn;
	struct iscsi_r2t *r2t;

	/*
	 * Make sure the initiator is not requesting retransmission
	 * of R2TSNs already acknowledged by a TMR TASK_REASSIGN.
	 */
	if ((cmd->cmd_flags & ICF_GOT_DATACK_SNACK) &&
	    (begrun <= cmd->acked_data_sn)) {
		pr_err("ITT: 0x%08x, R2T SNACK requesting"
			" retransmission of R2TSN: 0x%08x to 0x%08x but already"
			" acked to  R2TSN: 0x%08x by TMR TASK_REASSIGN,"
			" protocol error.\n", cmd->init_task_tag, begrun,
			(begrun + runlength), cmd->acked_data_sn);

			return iscsit_add_reject_from_cmd(
					ISCSI_REASON_PROTOCOL_ERROR,
					1, 0, buf, cmd);
	}

	if (runlength) {
		if ((begrun + runlength) > cmd->r2t_sn) {
			pr_err("Command ITT: 0x%08x received R2T SNACK"
			" with BegRun: 0x%08x, RunLength: 0x%08x, exceeds"
			" current R2TSN: 0x%08x, protocol error.\n",
			cmd->init_task_tag, begrun, runlength, cmd->r2t_sn);
			return iscsit_add_reject_from_cmd(
				ISCSI_REASON_BOOKMARK_INVALID, 1, 0, buf, cmd);
		}
		last_r2tsn = (begrun + runlength);
	} else
		last_r2tsn = cmd->r2t_sn;

	while (begrun < last_r2tsn) {
		r2t = iscsit_get_holder_for_r2tsn(cmd, begrun);
		if (!r2t)
			return -1;
		if (iscsit_send_recovery_r2t_for_snack(cmd, r2t) < 0)
			return -1;

		begrun++;
	}

	return 0;
}

/*
 *	Generates Offsets and NextBurstLength based on Begrun and Runlength
 *	carried in a Data SNACK or ExpDataSN in TMR TASK_REASSIGN.
 *
 *	For DataSequenceInOrder=Yes and DataPDUInOrder=[Yes,No] only.
 *
 *	FIXME: How is this handled for a RData SNACK?
 */
int iscsit_create_recovery_datain_values_datasequenceinorder_yes(
	struct iscsi_cmd *cmd,
	struct iscsi_datain_req *dr)
{
	u32 data_sn = 0, data_sn_count = 0;
	u32 pdu_start = 0, seq_no = 0;
	u32 begrun = dr->begrun;
	struct iscsi_conn *conn = cmd->conn;

	while (begrun > data_sn++) {
		data_sn_count++;
		if ((dr->next_burst_len +
		     conn->conn_ops->MaxRecvDataSegmentLength) <
		     conn->sess->sess_ops->MaxBurstLength) {
			dr->read_data_done +=
				conn->conn_ops->MaxRecvDataSegmentLength;
			dr->next_burst_len +=
				conn->conn_ops->MaxRecvDataSegmentLength;
		} else {
			dr->read_data_done +=
				(conn->sess->sess_ops->MaxBurstLength -
				 dr->next_burst_len);
			dr->next_burst_len = 0;
			pdu_start += data_sn_count;
			data_sn_count = 0;
			seq_no++;
		}
	}

	if (!conn->sess->sess_ops->DataPDUInOrder) {
		cmd->seq_no = seq_no;
		cmd->pdu_start = pdu_start;
		cmd->pdu_send_order = data_sn_count;
	}

	return 0;
}

/*
 *	Generates Offsets and NextBurstLength based on Begrun and Runlength
 *	carried in a Data SNACK or ExpDataSN in TMR TASK_REASSIGN.
 *
 *	For DataSequenceInOrder=No and DataPDUInOrder=[Yes,No] only.
 *
 *	FIXME: How is this handled for a RData SNACK?
 */
int iscsit_create_recovery_datain_values_datasequenceinorder_no(
	struct iscsi_cmd *cmd,
	struct iscsi_datain_req *dr)
{
	int found_seq = 0, i;
	u32 data_sn, read_data_done = 0, seq_send_order = 0;
	u32 begrun = dr->begrun;
	u32 runlength = dr->runlength;
	struct iscsi_conn *conn = cmd->conn;
	struct iscsi_seq *first_seq = NULL, *seq = NULL;

	if (!cmd->seq_list) {
		pr_err("struct iscsi_cmd->seq_list is NULL!\n");
		return -1;
	}

	/*
	 * Calculate read_data_done for all sequences containing a
	 * first_datasn and last_datasn less than the BegRun.
	 *
	 * Locate the struct iscsi_seq the BegRun lies within and calculate
	 * NextBurstLenghth up to the DataSN based on MaxRecvDataSegmentLength.
	 *
	 * Also use struct iscsi_seq->seq_send_order to determine where to start.
	 */
	for (i = 0; i < cmd->seq_count; i++) {
		seq = &cmd->seq_list[i];

		if (!seq->seq_send_order)
			first_seq = seq;

		/*
		 * No data has been transferred for this DataIN sequence, so the
		 * seq->first_datasn and seq->last_datasn have not been set.
		 */
		if (!seq->sent) {
			pr_err("Ignoring non-sent sequence 0x%08x ->"
				" 0x%08x\n\n", seq->first_datasn,
				seq->last_datasn);
			continue;
		}

		/*
		 * This DataIN sequence is precedes the received BegRun, add the
		 * total xfer_len of the sequence to read_data_done and reset
		 * seq->pdu_send_order.
		 */
		if ((seq->first_datasn < begrun) &&
				(seq->last_datasn < begrun)) {
			pr_err("Pre BegRun sequence 0x%08x ->"
				" 0x%08x\n", seq->first_datasn,
				seq->last_datasn);

			read_data_done += cmd->seq_list[i].xfer_len;
			seq->next_burst_len = seq->pdu_send_order = 0;
			continue;
		}

		/*
		 * The BegRun lies within this DataIN sequence.
		 */
		if ((seq->first_datasn <= begrun) &&
				(seq->last_datasn >= begrun)) {
			pr_err("Found sequence begrun: 0x%08x in"
				" 0x%08x -> 0x%08x\n", begrun,
				seq->first_datasn, seq->last_datasn);

			seq_send_order = seq->seq_send_order;
			data_sn = seq->first_datasn;
			seq->next_burst_len = seq->pdu_send_order = 0;
			found_seq = 1;

			/*
			 * For DataPDUInOrder=Yes, while the first DataSN of
			 * the sequence is less than the received BegRun, add
			 * the MaxRecvDataSegmentLength to read_data_done and
			 * to the sequence's next_burst_len;
			 *
			 * For DataPDUInOrder=No, while the first DataSN of the
			 * sequence is less than the received BegRun, find the
			 * struct iscsi_pdu of the DataSN in question and add the
			 * MaxRecvDataSegmentLength to read_data_done and to the
			 * sequence's next_burst_len;
			 */
			if (conn->sess->sess_ops->DataPDUInOrder) {
				while (data_sn < begrun) {
					seq->pdu_send_order++;
					read_data_done +=
						conn->conn_ops->MaxRecvDataSegmentLength;
					seq->next_burst_len +=
						conn->conn_ops->MaxRecvDataSegmentLength;
					data_sn++;
				}
			} else {
				int j;
				struct iscsi_pdu *pdu;

				while (data_sn < begrun) {
					seq->pdu_send_order++;

					for (j = 0; j < seq->pdu_count; j++) {
						pdu = &cmd->pdu_list[
							seq->pdu_start + j];
						if (pdu->data_sn == data_sn) {
							read_data_done +=
								pdu->length;
							seq->next_burst_len +=
								pdu->length;
						}
					}
					data_sn++;
				}
			}
			continue;
		}

		/*
		 * This DataIN sequence is larger than the received BegRun,
		 * reset seq->pdu_send_order and continue.
		 */
		if ((seq->first_datasn > begrun) ||
				(seq->last_datasn > begrun)) {
			pr_err("Post BegRun sequence 0x%08x -> 0x%08x\n",
					seq->first_datasn, seq->last_datasn);

			seq->next_burst_len = seq->pdu_send_order = 0;
			continue;
		}
	}

	if (!found_seq) {
		if (!begrun) {
			if (!first_seq) {
				pr_err("ITT: 0x%08x, Begrun: 0x%08x"
					" but first_seq is NULL\n",
					cmd->init_task_tag, begrun);
				return -1;
			}
			seq_send_order = first_seq->seq_send_order;
			seq->next_burst_len = seq->pdu_send_order = 0;
			goto done;
		}

		pr_err("Unable to locate struct iscsi_seq for ITT: 0x%08x,"
			" BegRun: 0x%08x, RunLength: 0x%08x while"
			" DataSequenceInOrder=No and DataPDUInOrder=%s.\n",
				cmd->init_task_tag, begrun, runlength,
			(conn->sess->sess_ops->DataPDUInOrder) ? "Yes" : "No");
		return -1;
	}

done:
	dr->read_data_done = read_data_done;
	dr->seq_send_order = seq_send_order;

	return 0;
}

static int iscsit_handle_recovery_datain(
	struct iscsi_cmd *cmd,
	unsigned char *buf,
	u32 begrun,
	u32 runlength)
{
	struct iscsi_conn *conn = cmd->conn;
	struct iscsi_datain_req *dr;
	struct se_cmd *se_cmd = &cmd->se_cmd;

	if (!(se_cmd->transport_state & CMD_T_COMPLETE)) {
		pr_err("Ignoring ITT: 0x%08x Data SNACK\n",
				cmd->init_task_tag);
		return 0;
	}

	/*
	 * Make sure the initiator is not requesting retransmission
	 * of DataSNs already acknowledged by a Data ACK SNACK.
	 */
	if ((cmd->cmd_flags & ICF_GOT_DATACK_SNACK) &&
	    (begrun <= cmd->acked_data_sn)) {
		pr_err("ITT: 0x%08x, Data SNACK requesting"
			" retransmission of DataSN: 0x%08x to 0x%08x but"
			" already acked to DataSN: 0x%08x by Data ACK SNACK,"
			" protocol error.\n", cmd->init_task_tag, begrun,
			(begrun + runlength), cmd->acked_data_sn);

		return iscsit_add_reject_from_cmd(ISCSI_REASON_PROTOCOL_ERROR,
				1, 0, buf, cmd);
	}

	/*
	 * Make sure BegRun and RunLength in the Data SNACK are sane.
	 * Note: (cmd->data_sn - 1) will carry the maximum DataSN sent.
	 */
	if ((begrun + runlength) > (cmd->data_sn - 1)) {
		pr_err("Initiator requesting BegRun: 0x%08x, RunLength"
			": 0x%08x greater than maximum DataSN: 0x%08x.\n",
				begrun, runlength, (cmd->data_sn - 1));
		return iscsit_add_reject_from_cmd(ISCSI_REASON_BOOKMARK_INVALID,
				1, 0, buf, cmd);
	}

	dr = iscsit_allocate_datain_req();
	if (!dr)
		return iscsit_add_reject_from_cmd(ISCSI_REASON_BOOKMARK_NO_RESOURCES,
				1, 0, buf, cmd);

	dr->data_sn = dr->begrun = begrun;
	dr->runlength = runlength;
	dr->generate_recovery_values = 1;
	dr->recovery = DATAIN_WITHIN_COMMAND_RECOVERY;

	iscsit_attach_datain_req(cmd, dr);

	cmd->i_state = ISTATE_SEND_DATAIN;
	iscsit_add_cmd_to_response_queue(cmd, conn, cmd->i_state);

	return 0;
}

int iscsit_handle_recovery_datain_or_r2t(
	struct iscsi_conn *conn,
	unsigned char *buf,
	u32 init_task_tag,
	u32 targ_xfer_tag,
	u32 begrun,
	u32 runlength)
{
	struct iscsi_cmd *cmd;

	cmd = iscsit_find_cmd_from_itt(conn, init_task_tag);
	if (!cmd)
		return 0;

	/*
	 * FIXME: This will not work for bidi commands.
	 */
	switch (cmd->data_direction) {
	case DMA_TO_DEVICE:
		return iscsit_handle_r2t_snack(cmd, buf, begrun, runlength);
	case DMA_FROM_DEVICE:
		return iscsit_handle_recovery_datain(cmd, buf, begrun,
				runlength);
	default:
		pr_err("Unknown cmd->data_direction: 0x%02x\n",
				cmd->data_direction);
		return -1;
	}

	return 0;
}

/* #warning FIXME: Status SNACK needs to be dependent on OPCODE!!! */
int iscsit_handle_status_snack(
	struct iscsi_conn *conn,
	u32 init_task_tag,
	u32 targ_xfer_tag,
	u32 begrun,
	u32 runlength)
{
	struct iscsi_cmd *cmd = NULL;
	u32 last_statsn;
	int found_cmd;

	if (conn->exp_statsn > begrun) {
		pr_err("Got Status SNACK Begrun: 0x%08x, RunLength:"
			" 0x%08x but already got ExpStatSN: 0x%08x on CID:"
			" %hu.\n", begrun, runlength, conn->exp_statsn,
			conn->cid);
		return 0;
	}

	last_statsn = (!runlength) ? conn->stat_sn : (begrun + runlength);

	while (begrun < last_statsn) {
		found_cmd = 0;

		spin_lock_bh(&conn->cmd_lock);
		list_for_each_entry(cmd, &conn->conn_cmd_list, i_conn_node) {
			if (cmd->stat_sn == begrun) {
				found_cmd = 1;
				break;
			}
		}
		spin_unlock_bh(&conn->cmd_lock);

		if (!found_cmd) {
			pr_err("Unable to find StatSN: 0x%08x for"
				" a Status SNACK, assuming this was a"
				" protactic SNACK for an untransmitted"
				" StatSN, ignoring.\n", begrun);
			begrun++;
			continue;
		}

		spin_lock_bh(&cmd->istate_lock);
		if (cmd->i_state == ISTATE_SEND_DATAIN) {
			spin_unlock_bh(&cmd->istate_lock);
			pr_err("Ignoring Status SNACK for BegRun:"
				" 0x%08x, RunLength: 0x%08x, assuming this was"
				" a protactic SNACK for an untransmitted"
				" StatSN\n", begrun, runlength);
			begrun++;
			continue;
		}
		spin_unlock_bh(&cmd->istate_lock);

		cmd->i_state = ISTATE_SEND_STATUS_RECOVERY;
		iscsit_add_cmd_to_response_queue(cmd, conn, cmd->i_state);
		begrun++;
	}

	return 0;
}

int iscsit_handle_data_ack(
	struct iscsi_conn *conn,
	u32 targ_xfer_tag,
	u32 begrun,
	u32 runlength)
{
	struct iscsi_cmd *cmd = NULL;

	cmd = iscsit_find_cmd_from_ttt(conn, targ_xfer_tag);
	if (!cmd) {
		pr_err("Data ACK SNACK for TTT: 0x%08x is"
			" invalid.\n", targ_xfer_tag);
		return -1;
	}

	if (begrun <= cmd->acked_data_sn) {
		pr_err("ITT: 0x%08x Data ACK SNACK BegRUN: 0x%08x is"
			" less than the already acked DataSN: 0x%08x.\n",
			cmd->init_task_tag, begrun, cmd->acked_data_sn);
		return -1;
	}

	/*
	 * For Data ACK SNACK, BegRun is the next expected DataSN.
	 * (see iSCSI v19: 10.16.6)
	 */
	cmd->cmd_flags |= ICF_GOT_DATACK_SNACK;
	cmd->acked_data_sn = (begrun - 1);

	pr_debug("Received Data ACK SNACK for ITT: 0x%08x,"
		" updated acked DataSN to 0x%08x.\n",
			cmd->init_task_tag, cmd->acked_data_sn);

	return 0;
}

static int iscsit_send_recovery_r2t(
	struct iscsi_cmd *cmd,
	u32 offset,
	u32 xfer_len)
{
	int ret;

	spin_lock_bh(&cmd->r2t_lock);
	ret = iscsit_add_r2t_to_list(cmd, offset, xfer_len, 1, 0);
	spin_unlock_bh(&cmd->r2t_lock);

	return ret;
}

int iscsit_dataout_datapduinorder_no_fbit(
	struct iscsi_cmd *cmd,
	struct iscsi_pdu *pdu)
{
	int i, send_recovery_r2t = 0, recovery = 0;
	u32 length = 0, offset = 0, pdu_count = 0, xfer_len = 0;
	struct iscsi_conn *conn = cmd->conn;
	struct iscsi_pdu *first_pdu = NULL;

	/*
	 * Get an struct iscsi_pdu pointer to the first PDU, and total PDU count
	 * of the DataOUT sequence.
	 */
	if (conn->sess->sess_ops->DataSequenceInOrder) {
		for (i = 0; i < cmd->pdu_count; i++) {
			if (cmd->pdu_list[i].seq_no == pdu->seq_no) {
				if (!first_pdu)
					first_pdu = &cmd->pdu_list[i];
				 xfer_len += cmd->pdu_list[i].length;
				 pdu_count++;
			} else if (pdu_count)
				break;
		}
	} else {
		struct iscsi_seq *seq = cmd->seq_ptr;

		first_pdu = &cmd->pdu_list[seq->pdu_start];
		pdu_count = seq->pdu_count;
	}

	if (!first_pdu || !pdu_count)
		return DATAOUT_CANNOT_RECOVER;

	/*
	 * Loop through the ending DataOUT Sequence checking each struct iscsi_pdu.
	 * The following ugly logic does batching of not received PDUs.
	 */
	for (i = 0; i < pdu_count; i++) {
		if (first_pdu[i].status == ISCSI_PDU_RECEIVED_OK) {
			if (!send_recovery_r2t)
				continue;

			if (iscsit_send_recovery_r2t(cmd, offset, length) < 0)
				return DATAOUT_CANNOT_RECOVER;

			send_recovery_r2t = length = offset = 0;
			continue;
		}
		/*
		 * Set recovery = 1 for any missing, CRC failed, or timed
		 * out PDUs to let the DataOUT logic know that this sequence
		 * has not been completed yet.
		 *
		 * Also, only send a Recovery R2T for ISCSI_PDU_NOT_RECEIVED.
		 * We assume if the PDU either failed CRC or timed out
		 * that a Recovery R2T has already been sent.
		 */
		recovery = 1;

		if (first_pdu[i].status != ISCSI_PDU_NOT_RECEIVED)
			continue;

		if (!offset)
			offset = first_pdu[i].offset;
		length += first_pdu[i].length;

		send_recovery_r2t = 1;
	}

	if (send_recovery_r2t)
		if (iscsit_send_recovery_r2t(cmd, offset, length) < 0)
			return DATAOUT_CANNOT_RECOVER;

	return (!recovery) ? DATAOUT_NORMAL : DATAOUT_WITHIN_COMMAND_RECOVERY;
}

static int iscsit_recalculate_dataout_values(
	struct iscsi_cmd *cmd,
	u32 pdu_offset,
	u32 pdu_length,
	u32 *r2t_offset,
	u32 *r2t_length)
{
	int i;
	struct iscsi_conn *conn = cmd->conn;
	struct iscsi_pdu *pdu = NULL;

	if (conn->sess->sess_ops->DataSequenceInOrder) {
		cmd->data_sn = 0;

		if (conn->sess->sess_ops->DataPDUInOrder) {
			*r2t_offset = cmd->write_data_done;
			*r2t_length = (cmd->seq_end_offset -
					cmd->write_data_done);
			return 0;
		}

		*r2t_offset = cmd->seq_start_offset;
		*r2t_length = (cmd->seq_end_offset - cmd->seq_start_offset);

		for (i = 0; i < cmd->pdu_count; i++) {
			pdu = &cmd->pdu_list[i];

			if (pdu->status != ISCSI_PDU_RECEIVED_OK)
				continue;

			if ((pdu->offset >= cmd->seq_start_offset) &&
			   ((pdu->offset + pdu->length) <=
			     cmd->seq_end_offset)) {
				if (!cmd->unsolicited_data)
					cmd->next_burst_len -= pdu->length;
				else
					cmd->first_burst_len -= pdu->length;

				cmd->write_data_done -= pdu->length;
				pdu->status = ISCSI_PDU_NOT_RECEIVED;
			}
		}
	} else {
		struct iscsi_seq *seq = NULL;

		seq = iscsit_get_seq_holder(cmd, pdu_offset, pdu_length);
		if (!seq)
			return -1;

		*r2t_offset = seq->orig_offset;
		*r2t_length = seq->xfer_len;

		cmd->write_data_done -= (seq->offset - seq->orig_offset);
		if (cmd->immediate_data)
			cmd->first_burst_len = cmd->write_data_done;

		seq->data_sn = 0;
		seq->offset = seq->orig_offset;
		seq->next_burst_len = 0;
		seq->status = DATAOUT_SEQUENCE_WITHIN_COMMAND_RECOVERY;

		if (conn->sess->sess_ops->DataPDUInOrder)
			return 0;

		for (i = 0; i < seq->pdu_count; i++) {
			pdu = &cmd->pdu_list[i+seq->pdu_start];

			if (pdu->status != ISCSI_PDU_RECEIVED_OK)
				continue;

			pdu->status = ISCSI_PDU_NOT_RECEIVED;
		}
	}

	return 0;
}

int iscsit_recover_dataout_sequence(
	struct iscsi_cmd *cmd,
	u32 pdu_offset,
	u32 pdu_length)
{
	u32 r2t_length = 0, r2t_offset = 0;

	spin_lock_bh(&cmd->istate_lock);
	cmd->cmd_flags |= ICF_WITHIN_COMMAND_RECOVERY;
	spin_unlock_bh(&cmd->istate_lock);

	if (iscsit_recalculate_dataout_values(cmd, pdu_offset, pdu_length,
			&r2t_offset, &r2t_length) < 0)
		return DATAOUT_CANNOT_RECOVER;

	iscsit_send_recovery_r2t(cmd, r2t_offset, r2t_length);

	return DATAOUT_WITHIN_COMMAND_RECOVERY;
}

static struct iscsi_ooo_cmdsn *iscsit_allocate_ooo_cmdsn(void)
{
	struct iscsi_ooo_cmdsn *ooo_cmdsn = NULL;

	ooo_cmdsn = kmem_cache_zalloc(lio_ooo_cache, GFP_ATOMIC);
	if (!ooo_cmdsn) {
		pr_err("Unable to allocate memory for"
			" struct iscsi_ooo_cmdsn.\n");
		return NULL;
	}
	INIT_LIST_HEAD(&ooo_cmdsn->ooo_list);

	return ooo_cmdsn;
}

/*
 *	Called with sess->cmdsn_mutex held.
 */
static int iscsit_attach_ooo_cmdsn(
	struct iscsi_session *sess,
	struct iscsi_ooo_cmdsn *ooo_cmdsn)
{
	struct iscsi_ooo_cmdsn *ooo_tail, *ooo_tmp;
	/*
	 * We attach the struct iscsi_ooo_cmdsn entry to the out of order
	 * list in increasing CmdSN order.
	 * This allows iscsi_execute_ooo_cmdsns() to detect any
	 * additional CmdSN holes while performing delayed execution.
	 */
	if (list_empty(&sess->sess_ooo_cmdsn_list))
		list_add_tail(&ooo_cmdsn->ooo_list,
				&sess->sess_ooo_cmdsn_list);
	else {
		ooo_tail = list_entry(sess->sess_ooo_cmdsn_list.prev,
				typeof(*ooo_tail), ooo_list);
		/*
		 * CmdSN is greater than the tail of the list.
		 */
		if (ooo_tail->cmdsn < ooo_cmdsn->cmdsn)
			list_add_tail(&ooo_cmdsn->ooo_list,
					&sess->sess_ooo_cmdsn_list);
		else {
			/*
			 * CmdSN is either lower than the head,  or somewhere
			 * in the middle.
			 */
			list_for_each_entry(ooo_tmp, &sess->sess_ooo_cmdsn_list,
						ooo_list) {
				if (ooo_tmp->cmdsn < ooo_cmdsn->cmdsn)
					continue;

				list_add(&ooo_cmdsn->ooo_list,
					&ooo_tmp->ooo_list);
				break;
			}
		}
	}

	return 0;
}

/*
 *	Removes an struct iscsi_ooo_cmdsn from a session's list,
 *	called with struct iscsi_session->cmdsn_mutex held.
 */
void iscsit_remove_ooo_cmdsn(
	struct iscsi_session *sess,
	struct iscsi_ooo_cmdsn *ooo_cmdsn)
{
	list_del(&ooo_cmdsn->ooo_list);
	kmem_cache_free(lio_ooo_cache, ooo_cmdsn);
}

void iscsit_clear_ooo_cmdsns_for_conn(struct iscsi_conn *conn)
{
	struct iscsi_ooo_cmdsn *ooo_cmdsn;
	struct iscsi_session *sess = conn->sess;

	mutex_lock(&sess->cmdsn_mutex);
	list_for_each_entry(ooo_cmdsn, &sess->sess_ooo_cmdsn_list, ooo_list) {
		if (ooo_cmdsn->cid != conn->cid)
			continue;

		ooo_cmdsn->cmd = NULL;
	}
	mutex_unlock(&sess->cmdsn_mutex);
}

/*
 *	Called with sess->cmdsn_mutex held.
 */
int iscsit_execute_ooo_cmdsns(struct iscsi_session *sess)
{
	int ooo_count = 0;
	struct iscsi_cmd *cmd = NULL;
	struct iscsi_ooo_cmdsn *ooo_cmdsn, *ooo_cmdsn_tmp;

	list_for_each_entry_safe(ooo_cmdsn, ooo_cmdsn_tmp,
				&sess->sess_ooo_cmdsn_list, ooo_list) {
		if (ooo_cmdsn->cmdsn != sess->exp_cmd_sn)
			continue;

		if (!ooo_cmdsn->cmd) {
			sess->exp_cmd_sn++;
			iscsit_remove_ooo_cmdsn(sess, ooo_cmdsn);
			continue;
		}

		cmd = ooo_cmdsn->cmd;
		cmd->i_state = cmd->deferred_i_state;
		ooo_count++;
		sess->exp_cmd_sn++;
		pr_debug("Executing out of order CmdSN: 0x%08x,"
			" incremented ExpCmdSN to 0x%08x.\n",
			cmd->cmd_sn, sess->exp_cmd_sn);

		iscsit_remove_ooo_cmdsn(sess, ooo_cmdsn);

		if (iscsit_execute_cmd(cmd, 1) < 0)
			return -1;

		continue;
	}

	return ooo_count;
}

/*
 *	Called either:
 *
 *	1. With sess->cmdsn_mutex held from iscsi_execute_ooo_cmdsns()
 *	or iscsi_check_received_cmdsn().
 *	2. With no locks held directly from iscsi_handle_XXX_pdu() functions
 *	for immediate commands.
 */
int iscsit_execute_cmd(struct iscsi_cmd *cmd, int ooo)
{
	struct se_cmd *se_cmd = &cmd->se_cmd;
	int lr = 0;

	spin_lock_bh(&cmd->istate_lock);
	if (ooo)
		cmd->cmd_flags &= ~ICF_OOO_CMDSN;

	switch (cmd->iscsi_opcode) {
	case ISCSI_OP_SCSI_CMD:
		/*
		 * Go ahead and send the CHECK_CONDITION status for
		 * any SCSI CDB exceptions that may have occurred, also
		 * handle the SCF_SCSI_RESERVATION_CONFLICT case here as well.
		 */
		if (se_cmd->se_cmd_flags & SCF_SCSI_CDB_EXCEPTION) {
			if (se_cmd->scsi_sense_reason == TCM_RESERVATION_CONFLICT) {
				cmd->i_state = ISTATE_SEND_STATUS;
				spin_unlock_bh(&cmd->istate_lock);
				iscsit_add_cmd_to_response_queue(cmd, cmd->conn,
						cmd->i_state);
				return 0;
			}
			spin_unlock_bh(&cmd->istate_lock);
			/*
			 * Determine if delayed TASK_ABORTED status for WRITEs
			 * should be sent now if no unsolicited data out
			 * payloads are expected, or if the delayed status
			 * should be sent after unsolicited data out with
			 * ISCSI_FLAG_CMD_FINAL set in iscsi_handle_data_out()
			 */
			if (transport_check_aborted_status(se_cmd,
					(cmd->unsolicited_data == 0)) != 0)
				return 0;
			/*
			 * Otherwise send CHECK_CONDITION and sense for
			 * exception
			 */
			return transport_send_check_condition_and_sense(se_cmd,
					se_cmd->scsi_sense_reason, 0);
		}
		/*
		 * Special case for delayed CmdSN with Immediate
		 * Data and/or Unsolicited Data Out attached.
		 */
		if (cmd->immediate_data) {
			if (cmd->cmd_flags & ICF_GOT_LAST_DATAOUT) {
				spin_unlock_bh(&cmd->istate_lock);
				target_execute_cmd(&cmd->se_cmd);
				return 0;
			}
			spin_unlock_bh(&cmd->istate_lock);

			if (!(cmd->cmd_flags &
					ICF_NON_IMMEDIATE_UNSOLICITED_DATA)) {
				/*
				 * Send the delayed TASK_ABORTED status for
				 * WRITEs if no more unsolicitied data is
				 * expected.
				 */
				if (transport_check_aborted_status(se_cmd, 1)
						!= 0)
					return 0;

				iscsit_set_dataout_sequence_values(cmd);
				iscsit_build_r2ts_for_cmd(cmd, cmd->conn, false);
			}
			return 0;
		}
		/*
		 * The default handler.
		 */
		spin_unlock_bh(&cmd->istate_lock);

		if ((cmd->data_direction == DMA_TO_DEVICE) &&
		    !(cmd->cmd_flags & ICF_NON_IMMEDIATE_UNSOLICITED_DATA)) {
			/*
			 * Send the delayed TASK_ABORTED status for WRITEs if
			 * no more nsolicitied data is expected.
			 */
			if (transport_check_aborted_status(se_cmd, 1) != 0)
				return 0;

			iscsit_set_dataout_sequence_values(cmd);
			spin_lock_bh(&cmd->dataout_timeout_lock);
			iscsit_start_dataout_timer(cmd, cmd->conn);
			spin_unlock_bh(&cmd->dataout_timeout_lock);
		}
		return transport_handle_cdb_direct(&cmd->se_cmd);

	case ISCSI_OP_NOOP_OUT:
	case ISCSI_OP_TEXT:
		spin_unlock_bh(&cmd->istate_lock);
		iscsit_add_cmd_to_response_queue(cmd, cmd->conn, cmd->i_state);
		break;
	case ISCSI_OP_SCSI_TMFUNC:
		if (se_cmd->se_cmd_flags & SCF_SCSI_CDB_EXCEPTION) {
			spin_unlock_bh(&cmd->istate_lock);
			iscsit_add_cmd_to_response_queue(cmd, cmd->conn,
					cmd->i_state);
			return 0;
		}
		spin_unlock_bh(&cmd->istate_lock);

		return transport_generic_handle_tmr(&cmd->se_cmd);
	case ISCSI_OP_LOGOUT:
		spin_unlock_bh(&cmd->istate_lock);
		switch (cmd->logout_reason) {
		case ISCSI_LOGOUT_REASON_CLOSE_SESSION:
			lr = iscsit_logout_closesession(cmd, cmd->conn);
			break;
		case ISCSI_LOGOUT_REASON_CLOSE_CONNECTION:
			lr = iscsit_logout_closeconnection(cmd, cmd->conn);
			break;
		case ISCSI_LOGOUT_REASON_RECOVERY:
			lr = iscsit_logout_removeconnforrecovery(cmd, cmd->conn);
			break;
		default:
			pr_err("Unknown iSCSI Logout Request Code:"
				" 0x%02x\n", cmd->logout_reason);
			return -1;
		}

		return lr;
	default:
		spin_unlock_bh(&cmd->istate_lock);
		pr_err("Cannot perform out of order execution for"
		" unknown iSCSI Opcode: 0x%02x\n", cmd->iscsi_opcode);
		return -1;
	}

	return 0;
}

void iscsit_free_all_ooo_cmdsns(struct iscsi_session *sess)
{
	struct iscsi_ooo_cmdsn *ooo_cmdsn, *ooo_cmdsn_tmp;

	mutex_lock(&sess->cmdsn_mutex);
	list_for_each_entry_safe(ooo_cmdsn, ooo_cmdsn_tmp,
			&sess->sess_ooo_cmdsn_list, ooo_list) {

		list_del(&ooo_cmdsn->ooo_list);
		kmem_cache_free(lio_ooo_cache, ooo_cmdsn);
	}
	mutex_unlock(&sess->cmdsn_mutex);
}

int iscsit_handle_ooo_cmdsn(
	struct iscsi_session *sess,
	struct iscsi_cmd *cmd,
	u32 cmdsn)
{
	int batch = 0;
	struct iscsi_ooo_cmdsn *ooo_cmdsn = NULL, *ooo_tail = NULL;

	cmd->deferred_i_state		= cmd->i_state;
	cmd->i_state			= ISTATE_DEFERRED_CMD;
	cmd->cmd_flags			|= ICF_OOO_CMDSN;

	if (list_empty(&sess->sess_ooo_cmdsn_list))
		batch = 1;
	else {
		ooo_tail = list_entry(sess->sess_ooo_cmdsn_list.prev,
				typeof(*ooo_tail), ooo_list);
		if (ooo_tail->cmdsn != (cmdsn - 1))
			batch = 1;
	}

	ooo_cmdsn = iscsit_allocate_ooo_cmdsn();
	if (!ooo_cmdsn)
		return CMDSN_ERROR_CANNOT_RECOVER;

	ooo_cmdsn->cmd			= cmd;
	ooo_cmdsn->batch_count		= (batch) ?
					  (cmdsn - sess->exp_cmd_sn) : 1;
	ooo_cmdsn->cid			= cmd->conn->cid;
	ooo_cmdsn->exp_cmdsn		= sess->exp_cmd_sn;
	ooo_cmdsn->cmdsn		= cmdsn;

	if (iscsit_attach_ooo_cmdsn(sess, ooo_cmdsn) < 0) {
		kmem_cache_free(lio_ooo_cache, ooo_cmdsn);
		return CMDSN_ERROR_CANNOT_RECOVER;
	}

	return CMDSN_HIGHER_THAN_EXP;
}

static int iscsit_set_dataout_timeout_values(
	struct iscsi_cmd *cmd,
	u32 *offset,
	u32 *length)
{
	struct iscsi_conn *conn = cmd->conn;
	struct iscsi_r2t *r2t;

	if (cmd->unsolicited_data) {
		*offset = 0;
		*length = (conn->sess->sess_ops->FirstBurstLength >
			   cmd->se_cmd.data_length) ?
			   cmd->se_cmd.data_length :
			   conn->sess->sess_ops->FirstBurstLength;
		return 0;
	}

	spin_lock_bh(&cmd->r2t_lock);
	if (list_empty(&cmd->cmd_r2t_list)) {
		pr_err("cmd->cmd_r2t_list is empty!\n");
		spin_unlock_bh(&cmd->r2t_lock);
		return -1;
	}

	list_for_each_entry(r2t, &cmd->cmd_r2t_list, r2t_list) {
		if (r2t->sent_r2t && !r2t->recovery_r2t && !r2t->seq_complete) {
			*offset = r2t->offset;
			*length = r2t->xfer_len;
			spin_unlock_bh(&cmd->r2t_lock);
			return 0;
		}
	}
	spin_unlock_bh(&cmd->r2t_lock);

	pr_err("Unable to locate any incomplete DataOUT"
		" sequences for ITT: 0x%08x.\n", cmd->init_task_tag);

	return -1;
}

/*
 *	NOTE: Called from interrupt (timer) context.
 */
static void iscsit_handle_dataout_timeout(unsigned long data)
{
	u32 pdu_length = 0, pdu_offset = 0;
	u32 r2t_length = 0, r2t_offset = 0;
	struct iscsi_cmd *cmd = (struct iscsi_cmd *) data;
	struct iscsi_conn *conn = cmd->conn;
	struct iscsi_session *sess = NULL;
	struct iscsi_node_attrib *na;

	iscsit_inc_conn_usage_count(conn);

	spin_lock_bh(&cmd->dataout_timeout_lock);
	if (cmd->dataout_timer_flags & ISCSI_TF_STOP) {
		spin_unlock_bh(&cmd->dataout_timeout_lock);
		iscsit_dec_conn_usage_count(conn);
		return;
	}
	cmd->dataout_timer_flags &= ~ISCSI_TF_RUNNING;
	sess = conn->sess;
	na = iscsit_tpg_get_node_attrib(sess);

	if (!sess->sess_ops->ErrorRecoveryLevel) {
		pr_debug("Unable to recover from DataOut timeout while"
			" in ERL=0.\n");
		goto failure;
	}

	if (++cmd->dataout_timeout_retries == na->dataout_timeout_retries) {
		pr_debug("Command ITT: 0x%08x exceeded max retries"
			" for DataOUT timeout %u, closing iSCSI connection.\n",
			cmd->init_task_tag, na->dataout_timeout_retries);
		goto failure;
	}

	cmd->cmd_flags |= ICF_WITHIN_COMMAND_RECOVERY;

	if (conn->sess->sess_ops->DataSequenceInOrder) {
		if (conn->sess->sess_ops->DataPDUInOrder) {
			pdu_offset = cmd->write_data_done;
			if ((pdu_offset + (conn->sess->sess_ops->MaxBurstLength -
			     cmd->next_burst_len)) > cmd->se_cmd.data_length)
				pdu_length = (cmd->se_cmd.data_length -
					cmd->write_data_done);
			else
				pdu_length = (conn->sess->sess_ops->MaxBurstLength -
						cmd->next_burst_len);
		} else {
			pdu_offset = cmd->seq_start_offset;
			pdu_length = (cmd->seq_end_offset -
				cmd->seq_start_offset);
		}
	} else {
		if (iscsit_set_dataout_timeout_values(cmd, &pdu_offset,
				&pdu_length) < 0)
			goto failure;
	}

	if (iscsit_recalculate_dataout_values(cmd, pdu_offset, pdu_length,
			&r2t_offset, &r2t_length) < 0)
		goto failure;

	pr_debug("Command ITT: 0x%08x timed out waiting for"
		" completion of %sDataOUT Sequence Offset: %u, Length: %u\n",
		cmd->init_task_tag, (cmd->unsolicited_data) ? "Unsolicited " :
		"", r2t_offset, r2t_length);

	if (iscsit_send_recovery_r2t(cmd, r2t_offset, r2t_length) < 0)
		goto failure;

	iscsit_start_dataout_timer(cmd, conn);
	spin_unlock_bh(&cmd->dataout_timeout_lock);
	iscsit_dec_conn_usage_count(conn);

	return;

failure:
	spin_unlock_bh(&cmd->dataout_timeout_lock);
	iscsit_cause_connection_reinstatement(conn, 0);
	iscsit_dec_conn_usage_count(conn);
}

void iscsit_mod_dataout_timer(struct iscsi_cmd *cmd)
{
	struct iscsi_conn *conn = cmd->conn;
	struct iscsi_session *sess = conn->sess;
	struct iscsi_node_attrib *na = iscsit_tpg_get_node_attrib(sess);

	spin_lock_bh(&cmd->dataout_timeout_lock);
	if (!(cmd->dataout_timer_flags & ISCSI_TF_RUNNING)) {
		spin_unlock_bh(&cmd->dataout_timeout_lock);
		return;
	}

	mod_timer(&cmd->dataout_timer,
		(get_jiffies_64() + na->dataout_timeout * HZ));
	pr_debug("Updated DataOUT timer for ITT: 0x%08x",
			cmd->init_task_tag);
	spin_unlock_bh(&cmd->dataout_timeout_lock);
}

/*
 *	Called with cmd->dataout_timeout_lock held.
 */
void iscsit_start_dataout_timer(
	struct iscsi_cmd *cmd,
	struct iscsi_conn *conn)
{
	struct iscsi_session *sess = conn->sess;
	struct iscsi_node_attrib *na = iscsit_tpg_get_node_attrib(sess);

	if (cmd->dataout_timer_flags & ISCSI_TF_RUNNING)
		return;

	pr_debug("Starting DataOUT timer for ITT: 0x%08x on"
		" CID: %hu.\n", cmd->init_task_tag, conn->cid);

	init_timer(&cmd->dataout_timer);
	cmd->dataout_timer.expires = (get_jiffies_64() + na->dataout_timeout * HZ);
	cmd->dataout_timer.data = (unsigned long)cmd;
	cmd->dataout_timer.function = iscsit_handle_dataout_timeout;
	cmd->dataout_timer_flags &= ~ISCSI_TF_STOP;
	cmd->dataout_timer_flags |= ISCSI_TF_RUNNING;
	add_timer(&cmd->dataout_timer);
}

void iscsit_stop_dataout_timer(struct iscsi_cmd *cmd)
{
	spin_lock_bh(&cmd->dataout_timeout_lock);
	if (!(cmd->dataout_timer_flags & ISCSI_TF_RUNNING)) {
		spin_unlock_bh(&cmd->dataout_timeout_lock);
		return;
	}
	cmd->dataout_timer_flags |= ISCSI_TF_STOP;
	spin_unlock_bh(&cmd->dataout_timeout_lock);

	del_timer_sync(&cmd->dataout_timer);

	spin_lock_bh(&cmd->dataout_timeout_lock);
	cmd->dataout_timer_flags &= ~ISCSI_TF_RUNNING;
	pr_debug("Stopped DataOUT Timer for ITT: 0x%08x\n",
			cmd->init_task_tag);
	spin_unlock_bh(&cmd->dataout_timeout_lock);
}

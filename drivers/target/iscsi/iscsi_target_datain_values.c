/*******************************************************************************
 * This file contains the iSCSI Target DataIN value generation functions.
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

#include <scsi/iscsi_proto.h>

#include "iscsi_target_core.h"
#include "iscsi_target_seq_pdu_list.h"
#include "iscsi_target_erl1.h"
#include "iscsi_target_util.h"
#include "iscsi_target.h"
#include "iscsi_target_datain_values.h"

struct iscsi_datain_req *iscsit_allocate_datain_req(void)
{
	struct iscsi_datain_req *dr;

	dr = kmem_cache_zalloc(lio_dr_cache, GFP_ATOMIC);
	if (!dr) {
		pr_err("Unable to allocate memory for"
				" struct iscsi_datain_req\n");
		return NULL;
	}
	INIT_LIST_HEAD(&dr->cmd_datain_node);

	return dr;
}

void iscsit_attach_datain_req(struct iscsi_cmd *cmd, struct iscsi_datain_req *dr)
{
	spin_lock(&cmd->datain_lock);
	list_add_tail(&dr->cmd_datain_node, &cmd->datain_list);
	spin_unlock(&cmd->datain_lock);
}

void iscsit_free_datain_req(struct iscsi_cmd *cmd, struct iscsi_datain_req *dr)
{
	spin_lock(&cmd->datain_lock);
	list_del(&dr->cmd_datain_node);
	spin_unlock(&cmd->datain_lock);

	kmem_cache_free(lio_dr_cache, dr);
}

void iscsit_free_all_datain_reqs(struct iscsi_cmd *cmd)
{
	struct iscsi_datain_req *dr, *dr_tmp;

	spin_lock(&cmd->datain_lock);
	list_for_each_entry_safe(dr, dr_tmp, &cmd->datain_list, cmd_datain_node) {
		list_del(&dr->cmd_datain_node);
		kmem_cache_free(lio_dr_cache, dr);
	}
	spin_unlock(&cmd->datain_lock);
}

struct iscsi_datain_req *iscsit_get_datain_req(struct iscsi_cmd *cmd)
{
	if (list_empty(&cmd->datain_list)) {
		pr_err("cmd->datain_list is empty for ITT:"
			" 0x%08x\n", cmd->init_task_tag);
		return NULL;
	}

	return list_first_entry(&cmd->datain_list, struct iscsi_datain_req,
				cmd_datain_node);
}

/*
 *	For Normal and Recovery DataSequenceInOrder=Yes and DataPDUInOrder=Yes.
 */
static struct iscsi_datain_req *iscsit_set_datain_values_yes_and_yes(
	struct iscsi_cmd *cmd,
	struct iscsi_datain *datain)
{
	u32 next_burst_len, read_data_done, read_data_left;
	struct iscsi_conn *conn = cmd->conn;
	struct iscsi_datain_req *dr;

	dr = iscsit_get_datain_req(cmd);
	if (!dr)
		return NULL;

	if (dr->recovery && dr->generate_recovery_values) {
		if (iscsit_create_recovery_datain_values_datasequenceinorder_yes(
					cmd, dr) < 0)
			return NULL;

		dr->generate_recovery_values = 0;
	}

	next_burst_len = (!dr->recovery) ?
			cmd->next_burst_len : dr->next_burst_len;
	read_data_done = (!dr->recovery) ?
			cmd->read_data_done : dr->read_data_done;

	read_data_left = (cmd->se_cmd.data_length - read_data_done);
	if (!read_data_left) {
		pr_err("ITT: 0x%08x read_data_left is zero!\n",
				cmd->init_task_tag);
		return NULL;
	}

	if ((read_data_left <= conn->conn_ops->MaxRecvDataSegmentLength) &&
	    (read_data_left <= (conn->sess->sess_ops->MaxBurstLength -
	     next_burst_len))) {
		datain->length = read_data_left;

		datain->flags |= (ISCSI_FLAG_CMD_FINAL | ISCSI_FLAG_DATA_STATUS);
		if (conn->sess->sess_ops->ErrorRecoveryLevel > 0)
			datain->flags |= ISCSI_FLAG_DATA_ACK;
	} else {
		if ((next_burst_len +
		     conn->conn_ops->MaxRecvDataSegmentLength) <
		     conn->sess->sess_ops->MaxBurstLength) {
			datain->length =
				conn->conn_ops->MaxRecvDataSegmentLength;
			next_burst_len += datain->length;
		} else {
			datain->length = (conn->sess->sess_ops->MaxBurstLength -
					  next_burst_len);
			next_burst_len = 0;

			datain->flags |= ISCSI_FLAG_CMD_FINAL;
			if (conn->sess->sess_ops->ErrorRecoveryLevel > 0)
				datain->flags |= ISCSI_FLAG_DATA_ACK;
		}
	}

	datain->data_sn = (!dr->recovery) ? cmd->data_sn++ : dr->data_sn++;
	datain->offset = read_data_done;

	if (!dr->recovery) {
		cmd->next_burst_len = next_burst_len;
		cmd->read_data_done += datain->length;
	} else {
		dr->next_burst_len = next_burst_len;
		dr->read_data_done += datain->length;
	}

	if (!dr->recovery) {
		if (datain->flags & ISCSI_FLAG_DATA_STATUS)
			dr->dr_complete = DATAIN_COMPLETE_NORMAL;

		return dr;
	}

	if (!dr->runlength) {
		if (datain->flags & ISCSI_FLAG_DATA_STATUS) {
			dr->dr_complete =
			    (dr->recovery == DATAIN_WITHIN_COMMAND_RECOVERY) ?
				DATAIN_COMPLETE_WITHIN_COMMAND_RECOVERY :
				DATAIN_COMPLETE_CONNECTION_RECOVERY;
		}
	} else {
		if ((dr->begrun + dr->runlength) == dr->data_sn) {
			dr->dr_complete =
			    (dr->recovery == DATAIN_WITHIN_COMMAND_RECOVERY) ?
				DATAIN_COMPLETE_WITHIN_COMMAND_RECOVERY :
				DATAIN_COMPLETE_CONNECTION_RECOVERY;
		}
	}

	return dr;
}

/*
 *	For Normal and Recovery DataSequenceInOrder=No and DataPDUInOrder=Yes.
 */
static struct iscsi_datain_req *iscsit_set_datain_values_no_and_yes(
	struct iscsi_cmd *cmd,
	struct iscsi_datain *datain)
{
	u32 offset, read_data_done, read_data_left, seq_send_order;
	struct iscsi_conn *conn = cmd->conn;
	struct iscsi_datain_req *dr;
	struct iscsi_seq *seq;

	dr = iscsit_get_datain_req(cmd);
	if (!dr)
		return NULL;

	if (dr->recovery && dr->generate_recovery_values) {
		if (iscsit_create_recovery_datain_values_datasequenceinorder_no(
					cmd, dr) < 0)
			return NULL;

		dr->generate_recovery_values = 0;
	}

	read_data_done = (!dr->recovery) ?
			cmd->read_data_done : dr->read_data_done;
	seq_send_order = (!dr->recovery) ?
			cmd->seq_send_order : dr->seq_send_order;

	read_data_left = (cmd->se_cmd.data_length - read_data_done);
	if (!read_data_left) {
		pr_err("ITT: 0x%08x read_data_left is zero!\n",
				cmd->init_task_tag);
		return NULL;
	}

	seq = iscsit_get_seq_holder_for_datain(cmd, seq_send_order);
	if (!seq)
		return NULL;

	seq->sent = 1;

	if (!dr->recovery && !seq->next_burst_len)
		seq->first_datasn = cmd->data_sn;

	offset = (seq->offset + seq->next_burst_len);

	if ((offset + conn->conn_ops->MaxRecvDataSegmentLength) >=
	     cmd->se_cmd.data_length) {
		datain->length = (cmd->se_cmd.data_length - offset);
		datain->offset = offset;

		datain->flags |= ISCSI_FLAG_CMD_FINAL;
		if (conn->sess->sess_ops->ErrorRecoveryLevel > 0)
			datain->flags |= ISCSI_FLAG_DATA_ACK;

		seq->next_burst_len = 0;
		seq_send_order++;
	} else {
		if ((seq->next_burst_len +
		     conn->conn_ops->MaxRecvDataSegmentLength) <
		     conn->sess->sess_ops->MaxBurstLength) {
			datain->length =
				conn->conn_ops->MaxRecvDataSegmentLength;
			datain->offset = (seq->offset + seq->next_burst_len);

			seq->next_burst_len += datain->length;
		} else {
			datain->length = (conn->sess->sess_ops->MaxBurstLength -
					  seq->next_burst_len);
			datain->offset = (seq->offset + seq->next_burst_len);

			datain->flags |= ISCSI_FLAG_CMD_FINAL;
			if (conn->sess->sess_ops->ErrorRecoveryLevel > 0)
				datain->flags |= ISCSI_FLAG_DATA_ACK;

			seq->next_burst_len = 0;
			seq_send_order++;
		}
	}

	if ((read_data_done + datain->length) == cmd->se_cmd.data_length)
		datain->flags |= ISCSI_FLAG_DATA_STATUS;

	datain->data_sn = (!dr->recovery) ? cmd->data_sn++ : dr->data_sn++;
	if (!dr->recovery) {
		cmd->seq_send_order = seq_send_order;
		cmd->read_data_done += datain->length;
	} else {
		dr->seq_send_order = seq_send_order;
		dr->read_data_done += datain->length;
	}

	if (!dr->recovery) {
		if (datain->flags & ISCSI_FLAG_CMD_FINAL)
			seq->last_datasn = datain->data_sn;
		if (datain->flags & ISCSI_FLAG_DATA_STATUS)
			dr->dr_complete = DATAIN_COMPLETE_NORMAL;

		return dr;
	}

	if (!dr->runlength) {
		if (datain->flags & ISCSI_FLAG_DATA_STATUS) {
			dr->dr_complete =
			    (dr->recovery == DATAIN_WITHIN_COMMAND_RECOVERY) ?
				DATAIN_COMPLETE_WITHIN_COMMAND_RECOVERY :
				DATAIN_COMPLETE_CONNECTION_RECOVERY;
		}
	} else {
		if ((dr->begrun + dr->runlength) == dr->data_sn) {
			dr->dr_complete =
			    (dr->recovery == DATAIN_WITHIN_COMMAND_RECOVERY) ?
				DATAIN_COMPLETE_WITHIN_COMMAND_RECOVERY :
				DATAIN_COMPLETE_CONNECTION_RECOVERY;
		}
	}

	return dr;
}

/*
 *	For Normal and Recovery DataSequenceInOrder=Yes and DataPDUInOrder=No.
 */
static struct iscsi_datain_req *iscsit_set_datain_values_yes_and_no(
	struct iscsi_cmd *cmd,
	struct iscsi_datain *datain)
{
	u32 next_burst_len, read_data_done, read_data_left;
	struct iscsi_conn *conn = cmd->conn;
	struct iscsi_datain_req *dr;
	struct iscsi_pdu *pdu;

	dr = iscsit_get_datain_req(cmd);
	if (!dr)
		return NULL;

	if (dr->recovery && dr->generate_recovery_values) {
		if (iscsit_create_recovery_datain_values_datasequenceinorder_yes(
					cmd, dr) < 0)
			return NULL;

		dr->generate_recovery_values = 0;
	}

	next_burst_len = (!dr->recovery) ?
			cmd->next_burst_len : dr->next_burst_len;
	read_data_done = (!dr->recovery) ?
			cmd->read_data_done : dr->read_data_done;

	read_data_left = (cmd->se_cmd.data_length - read_data_done);
	if (!read_data_left) {
		pr_err("ITT: 0x%08x read_data_left is zero!\n",
				cmd->init_task_tag);
		return dr;
	}

	pdu = iscsit_get_pdu_holder_for_seq(cmd, NULL);
	if (!pdu)
		return dr;

	if ((read_data_done + pdu->length) == cmd->se_cmd.data_length) {
		pdu->flags |= (ISCSI_FLAG_CMD_FINAL | ISCSI_FLAG_DATA_STATUS);
		if (conn->sess->sess_ops->ErrorRecoveryLevel > 0)
			pdu->flags |= ISCSI_FLAG_DATA_ACK;

		next_burst_len = 0;
	} else {
		if ((next_burst_len + conn->conn_ops->MaxRecvDataSegmentLength) <
		     conn->sess->sess_ops->MaxBurstLength)
			next_burst_len += pdu->length;
		else {
			pdu->flags |= ISCSI_FLAG_CMD_FINAL;
			if (conn->sess->sess_ops->ErrorRecoveryLevel > 0)
				pdu->flags |= ISCSI_FLAG_DATA_ACK;

			next_burst_len = 0;
		}
	}

	pdu->data_sn = (!dr->recovery) ? cmd->data_sn++ : dr->data_sn++;
	if (!dr->recovery) {
		cmd->next_burst_len = next_burst_len;
		cmd->read_data_done += pdu->length;
	} else {
		dr->next_burst_len = next_burst_len;
		dr->read_data_done += pdu->length;
	}

	datain->flags = pdu->flags;
	datain->length = pdu->length;
	datain->offset = pdu->offset;
	datain->data_sn = pdu->data_sn;

	if (!dr->recovery) {
		if (datain->flags & ISCSI_FLAG_DATA_STATUS)
			dr->dr_complete = DATAIN_COMPLETE_NORMAL;

		return dr;
	}

	if (!dr->runlength) {
		if (datain->flags & ISCSI_FLAG_DATA_STATUS) {
			dr->dr_complete =
			    (dr->recovery == DATAIN_WITHIN_COMMAND_RECOVERY) ?
				DATAIN_COMPLETE_WITHIN_COMMAND_RECOVERY :
				DATAIN_COMPLETE_CONNECTION_RECOVERY;
		}
	} else {
		if ((dr->begrun + dr->runlength) == dr->data_sn) {
			dr->dr_complete =
			    (dr->recovery == DATAIN_WITHIN_COMMAND_RECOVERY) ?
				DATAIN_COMPLETE_WITHIN_COMMAND_RECOVERY :
				DATAIN_COMPLETE_CONNECTION_RECOVERY;
		}
	}

	return dr;
}

/*
 *	For Normal and Recovery DataSequenceInOrder=No and DataPDUInOrder=No.
 */
static struct iscsi_datain_req *iscsit_set_datain_values_no_and_no(
	struct iscsi_cmd *cmd,
	struct iscsi_datain *datain)
{
	u32 read_data_done, read_data_left, seq_send_order;
	struct iscsi_conn *conn = cmd->conn;
	struct iscsi_datain_req *dr;
	struct iscsi_pdu *pdu;
	struct iscsi_seq *seq = NULL;

	dr = iscsit_get_datain_req(cmd);
	if (!dr)
		return NULL;

	if (dr->recovery && dr->generate_recovery_values) {
		if (iscsit_create_recovery_datain_values_datasequenceinorder_no(
					cmd, dr) < 0)
			return NULL;

		dr->generate_recovery_values = 0;
	}

	read_data_done = (!dr->recovery) ?
			cmd->read_data_done : dr->read_data_done;
	seq_send_order = (!dr->recovery) ?
			cmd->seq_send_order : dr->seq_send_order;

	read_data_left = (cmd->se_cmd.data_length - read_data_done);
	if (!read_data_left) {
		pr_err("ITT: 0x%08x read_data_left is zero!\n",
				cmd->init_task_tag);
		return NULL;
	}

	seq = iscsit_get_seq_holder_for_datain(cmd, seq_send_order);
	if (!seq)
		return NULL;

	seq->sent = 1;

	if (!dr->recovery && !seq->next_burst_len)
		seq->first_datasn = cmd->data_sn;

	pdu = iscsit_get_pdu_holder_for_seq(cmd, seq);
	if (!pdu)
		return NULL;

	if (seq->pdu_send_order == seq->pdu_count) {
		pdu->flags |= ISCSI_FLAG_CMD_FINAL;
		if (conn->sess->sess_ops->ErrorRecoveryLevel > 0)
			pdu->flags |= ISCSI_FLAG_DATA_ACK;

		seq->next_burst_len = 0;
		seq_send_order++;
	} else
		seq->next_burst_len += pdu->length;

	if ((read_data_done + pdu->length) == cmd->se_cmd.data_length)
		pdu->flags |= ISCSI_FLAG_DATA_STATUS;

	pdu->data_sn = (!dr->recovery) ? cmd->data_sn++ : dr->data_sn++;
	if (!dr->recovery) {
		cmd->seq_send_order = seq_send_order;
		cmd->read_data_done += pdu->length;
	} else {
		dr->seq_send_order = seq_send_order;
		dr->read_data_done += pdu->length;
	}

	datain->flags = pdu->flags;
	datain->length = pdu->length;
	datain->offset = pdu->offset;
	datain->data_sn = pdu->data_sn;

	if (!dr->recovery) {
		if (datain->flags & ISCSI_FLAG_CMD_FINAL)
			seq->last_datasn = datain->data_sn;
		if (datain->flags & ISCSI_FLAG_DATA_STATUS)
			dr->dr_complete = DATAIN_COMPLETE_NORMAL;

		return dr;
	}

	if (!dr->runlength) {
		if (datain->flags & ISCSI_FLAG_DATA_STATUS) {
			dr->dr_complete =
			    (dr->recovery == DATAIN_WITHIN_COMMAND_RECOVERY) ?
				DATAIN_COMPLETE_WITHIN_COMMAND_RECOVERY :
				DATAIN_COMPLETE_CONNECTION_RECOVERY;
		}
	} else {
		if ((dr->begrun + dr->runlength) == dr->data_sn) {
			dr->dr_complete =
			    (dr->recovery == DATAIN_WITHIN_COMMAND_RECOVERY) ?
				DATAIN_COMPLETE_WITHIN_COMMAND_RECOVERY :
				DATAIN_COMPLETE_CONNECTION_RECOVERY;
		}
	}

	return dr;
}

struct iscsi_datain_req *iscsit_get_datain_values(
	struct iscsi_cmd *cmd,
	struct iscsi_datain *datain)
{
	struct iscsi_conn *conn = cmd->conn;

	if (conn->sess->sess_ops->DataSequenceInOrder &&
	    conn->sess->sess_ops->DataPDUInOrder)
		return iscsit_set_datain_values_yes_and_yes(cmd, datain);
	else if (!conn->sess->sess_ops->DataSequenceInOrder &&
		  conn->sess->sess_ops->DataPDUInOrder)
		return iscsit_set_datain_values_no_and_yes(cmd, datain);
	else if (conn->sess->sess_ops->DataSequenceInOrder &&
		 !conn->sess->sess_ops->DataPDUInOrder)
		return iscsit_set_datain_values_yes_and_no(cmd, datain);
	else if (!conn->sess->sess_ops->DataSequenceInOrder &&
		   !conn->sess->sess_ops->DataPDUInOrder)
		return iscsit_set_datain_values_no_and_no(cmd, datain);

	return NULL;
}

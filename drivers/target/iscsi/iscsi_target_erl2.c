/*******************************************************************************
 * This file contains error recovery level two functions used by
 * the iSCSI Target driver.
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

#include <scsi/iscsi_proto.h>
#include <target/target_core_base.h>
#include <target/target_core_fabric.h>

#include "iscsi_target_core.h"
#include "iscsi_target_datain_values.h"
#include "iscsi_target_util.h"
#include "iscsi_target_erl0.h"
#include "iscsi_target_erl1.h"
#include "iscsi_target_erl2.h"
#include "iscsi_target.h"

/*
 *	FIXME: Does RData SNACK apply here as well?
 */
void iscsit_create_conn_recovery_datain_values(
	struct iscsi_cmd *cmd,
	__be32 exp_data_sn)
{
	u32 data_sn = 0;
	struct iscsi_conn *conn = cmd->conn;

	cmd->next_burst_len = 0;
	cmd->read_data_done = 0;

	while (be32_to_cpu(exp_data_sn) > data_sn) {
		if ((cmd->next_burst_len +
		     conn->conn_ops->MaxRecvDataSegmentLength) <
		     conn->sess->sess_ops->MaxBurstLength) {
			cmd->read_data_done +=
			       conn->conn_ops->MaxRecvDataSegmentLength;
			cmd->next_burst_len +=
			       conn->conn_ops->MaxRecvDataSegmentLength;
		} else {
			cmd->read_data_done +=
				(conn->sess->sess_ops->MaxBurstLength -
				cmd->next_burst_len);
			cmd->next_burst_len = 0;
		}
		data_sn++;
	}
}

void iscsit_create_conn_recovery_dataout_values(
	struct iscsi_cmd *cmd)
{
	u32 write_data_done = 0;
	struct iscsi_conn *conn = cmd->conn;

	cmd->data_sn = 0;
	cmd->next_burst_len = 0;

	while (cmd->write_data_done > write_data_done) {
		if ((write_data_done + conn->sess->sess_ops->MaxBurstLength) <=
		     cmd->write_data_done)
			write_data_done += conn->sess->sess_ops->MaxBurstLength;
		else
			break;
	}

	cmd->write_data_done = write_data_done;
}

static int iscsit_attach_active_connection_recovery_entry(
	struct iscsi_session *sess,
	struct iscsi_conn_recovery *cr)
{
	spin_lock(&sess->cr_a_lock);
	list_add_tail(&cr->cr_list, &sess->cr_active_list);
	spin_unlock(&sess->cr_a_lock);

	return 0;
}

static int iscsit_attach_inactive_connection_recovery_entry(
	struct iscsi_session *sess,
	struct iscsi_conn_recovery *cr)
{
	spin_lock(&sess->cr_i_lock);
	list_add_tail(&cr->cr_list, &sess->cr_inactive_list);

	sess->conn_recovery_count++;
	pr_debug("Incremented connection recovery count to %u for"
		" SID: %u\n", sess->conn_recovery_count, sess->sid);
	spin_unlock(&sess->cr_i_lock);

	return 0;
}

struct iscsi_conn_recovery *iscsit_get_inactive_connection_recovery_entry(
	struct iscsi_session *sess,
	u16 cid)
{
	struct iscsi_conn_recovery *cr;

	spin_lock(&sess->cr_i_lock);
	list_for_each_entry(cr, &sess->cr_inactive_list, cr_list) {
		if (cr->cid == cid) {
			spin_unlock(&sess->cr_i_lock);
			return cr;
		}
	}
	spin_unlock(&sess->cr_i_lock);

	return NULL;
}

void iscsit_free_connection_recovery_entires(struct iscsi_session *sess)
{
	struct iscsi_cmd *cmd, *cmd_tmp;
	struct iscsi_conn_recovery *cr, *cr_tmp;

	spin_lock(&sess->cr_a_lock);
	list_for_each_entry_safe(cr, cr_tmp, &sess->cr_active_list, cr_list) {
		list_del(&cr->cr_list);
		spin_unlock(&sess->cr_a_lock);

		spin_lock(&cr->conn_recovery_cmd_lock);
		list_for_each_entry_safe(cmd, cmd_tmp,
				&cr->conn_recovery_cmd_list, i_conn_node) {

			list_del(&cmd->i_conn_node);
			cmd->conn = NULL;
			spin_unlock(&cr->conn_recovery_cmd_lock);
			iscsit_free_cmd(cmd);
			spin_lock(&cr->conn_recovery_cmd_lock);
		}
		spin_unlock(&cr->conn_recovery_cmd_lock);
		spin_lock(&sess->cr_a_lock);

		kfree(cr);
	}
	spin_unlock(&sess->cr_a_lock);

	spin_lock(&sess->cr_i_lock);
	list_for_each_entry_safe(cr, cr_tmp, &sess->cr_inactive_list, cr_list) {
		list_del(&cr->cr_list);
		spin_unlock(&sess->cr_i_lock);

		spin_lock(&cr->conn_recovery_cmd_lock);
		list_for_each_entry_safe(cmd, cmd_tmp,
				&cr->conn_recovery_cmd_list, i_conn_node) {

			list_del(&cmd->i_conn_node);
			cmd->conn = NULL;
			spin_unlock(&cr->conn_recovery_cmd_lock);
			iscsit_free_cmd(cmd);
			spin_lock(&cr->conn_recovery_cmd_lock);
		}
		spin_unlock(&cr->conn_recovery_cmd_lock);
		spin_lock(&sess->cr_i_lock);

		kfree(cr);
	}
	spin_unlock(&sess->cr_i_lock);
}

int iscsit_remove_active_connection_recovery_entry(
	struct iscsi_conn_recovery *cr,
	struct iscsi_session *sess)
{
	spin_lock(&sess->cr_a_lock);
	list_del(&cr->cr_list);

	sess->conn_recovery_count--;
	pr_debug("Decremented connection recovery count to %u for"
		" SID: %u\n", sess->conn_recovery_count, sess->sid);
	spin_unlock(&sess->cr_a_lock);

	kfree(cr);

	return 0;
}

static void iscsit_remove_inactive_connection_recovery_entry(
	struct iscsi_conn_recovery *cr,
	struct iscsi_session *sess)
{
	spin_lock(&sess->cr_i_lock);
	list_del(&cr->cr_list);
	spin_unlock(&sess->cr_i_lock);
}

/*
 *	Called with cr->conn_recovery_cmd_lock help.
 */
int iscsit_remove_cmd_from_connection_recovery(
	struct iscsi_cmd *cmd,
	struct iscsi_session *sess)
{
	struct iscsi_conn_recovery *cr;

	if (!cmd->cr) {
		pr_err("struct iscsi_conn_recovery pointer for ITT: 0x%08x"
			" is NULL!\n", cmd->init_task_tag);
		BUG();
	}
	cr = cmd->cr;

	list_del(&cmd->i_conn_node);
	return --cr->cmd_count;
}

void iscsit_discard_cr_cmds_by_expstatsn(
	struct iscsi_conn_recovery *cr,
	u32 exp_statsn)
{
	u32 dropped_count = 0;
	struct iscsi_cmd *cmd, *cmd_tmp;
	struct iscsi_session *sess = cr->sess;

	spin_lock(&cr->conn_recovery_cmd_lock);
	list_for_each_entry_safe(cmd, cmd_tmp,
			&cr->conn_recovery_cmd_list, i_conn_node) {

		if (((cmd->deferred_i_state != ISTATE_SENT_STATUS) &&
		     (cmd->deferred_i_state != ISTATE_REMOVE)) ||
		     (cmd->stat_sn >= exp_statsn)) {
			continue;
		}

		dropped_count++;
		pr_debug("Dropping Acknowledged ITT: 0x%08x, StatSN:"
			" 0x%08x, CID: %hu.\n", cmd->init_task_tag,
				cmd->stat_sn, cr->cid);

		iscsit_remove_cmd_from_connection_recovery(cmd, sess);

		spin_unlock(&cr->conn_recovery_cmd_lock);
		iscsit_free_cmd(cmd);
		spin_lock(&cr->conn_recovery_cmd_lock);
	}
	spin_unlock(&cr->conn_recovery_cmd_lock);

	pr_debug("Dropped %u total acknowledged commands on"
		" CID: %hu less than old ExpStatSN: 0x%08x\n",
			dropped_count, cr->cid, exp_statsn);

	if (!cr->cmd_count) {
		pr_debug("No commands to be reassigned for failed"
			" connection CID: %hu on SID: %u\n",
			cr->cid, sess->sid);
		iscsit_remove_inactive_connection_recovery_entry(cr, sess);
		iscsit_attach_active_connection_recovery_entry(sess, cr);
		pr_debug("iSCSI connection recovery successful for CID:"
			" %hu on SID: %u\n", cr->cid, sess->sid);
		iscsit_remove_active_connection_recovery_entry(cr, sess);
	} else {
		iscsit_remove_inactive_connection_recovery_entry(cr, sess);
		iscsit_attach_active_connection_recovery_entry(sess, cr);
	}
}

int iscsit_discard_unacknowledged_ooo_cmdsns_for_conn(struct iscsi_conn *conn)
{
	u32 dropped_count = 0;
	struct iscsi_cmd *cmd, *cmd_tmp;
	struct iscsi_ooo_cmdsn *ooo_cmdsn, *ooo_cmdsn_tmp;
	struct iscsi_session *sess = conn->sess;

	mutex_lock(&sess->cmdsn_mutex);
	list_for_each_entry_safe(ooo_cmdsn, ooo_cmdsn_tmp,
			&sess->sess_ooo_cmdsn_list, ooo_list) {

		if (ooo_cmdsn->cid != conn->cid)
			continue;

		dropped_count++;
		pr_debug("Dropping unacknowledged CmdSN:"
		" 0x%08x during connection recovery on CID: %hu\n",
			ooo_cmdsn->cmdsn, conn->cid);
		iscsit_remove_ooo_cmdsn(sess, ooo_cmdsn);
	}
	mutex_unlock(&sess->cmdsn_mutex);

	spin_lock_bh(&conn->cmd_lock);
	list_for_each_entry_safe(cmd, cmd_tmp, &conn->conn_cmd_list, i_conn_node) {
		if (!(cmd->cmd_flags & ICF_OOO_CMDSN))
			continue;

		list_del(&cmd->i_conn_node);

		spin_unlock_bh(&conn->cmd_lock);
		iscsit_free_cmd(cmd);
		spin_lock_bh(&conn->cmd_lock);
	}
	spin_unlock_bh(&conn->cmd_lock);

	pr_debug("Dropped %u total unacknowledged commands on CID:"
		" %hu for ExpCmdSN: 0x%08x.\n", dropped_count, conn->cid,
				sess->exp_cmd_sn);
	return 0;
}

int iscsit_prepare_cmds_for_realligance(struct iscsi_conn *conn)
{
	u32 cmd_count = 0;
	struct iscsi_cmd *cmd, *cmd_tmp;
	struct iscsi_conn_recovery *cr;

	/*
	 * Allocate an struct iscsi_conn_recovery for this connection.
	 * Each struct iscsi_cmd contains an struct iscsi_conn_recovery pointer
	 * (struct iscsi_cmd->cr) so we need to allocate this before preparing the
	 * connection's command list for connection recovery.
	 */
	cr = kzalloc(sizeof(struct iscsi_conn_recovery), GFP_KERNEL);
	if (!cr) {
		pr_err("Unable to allocate memory for"
			" struct iscsi_conn_recovery.\n");
		return -1;
	}
	INIT_LIST_HEAD(&cr->cr_list);
	INIT_LIST_HEAD(&cr->conn_recovery_cmd_list);
	spin_lock_init(&cr->conn_recovery_cmd_lock);
	/*
	 * Only perform connection recovery on ISCSI_OP_SCSI_CMD or
	 * ISCSI_OP_NOOP_OUT opcodes.  For all other opcodes call
	 * list_del(&cmd->i_conn_node); to release the command to the
	 * session pool and remove it from the connection's list.
	 *
	 * Also stop the DataOUT timer, which will be restarted after
	 * sending the TMR response.
	 */
	spin_lock_bh(&conn->cmd_lock);
	list_for_each_entry_safe(cmd, cmd_tmp, &conn->conn_cmd_list, i_conn_node) {

		if ((cmd->iscsi_opcode != ISCSI_OP_SCSI_CMD) &&
		    (cmd->iscsi_opcode != ISCSI_OP_NOOP_OUT)) {
			pr_debug("Not performing realligence on"
				" Opcode: 0x%02x, ITT: 0x%08x, CmdSN: 0x%08x,"
				" CID: %hu\n", cmd->iscsi_opcode,
				cmd->init_task_tag, cmd->cmd_sn, conn->cid);

			list_del(&cmd->i_conn_node);
			spin_unlock_bh(&conn->cmd_lock);
			iscsit_free_cmd(cmd);
			spin_lock_bh(&conn->cmd_lock);
			continue;
		}

		/*
		 * Special case where commands greater than or equal to
		 * the session's ExpCmdSN are attached to the connection
		 * list but not to the out of order CmdSN list.  The one
		 * obvious case is when a command with immediate data
		 * attached must only check the CmdSN against ExpCmdSN
		 * after the data is received.  The special case below
		 * is when the connection fails before data is received,
		 * but also may apply to other PDUs, so it has been
		 * made generic here.
		 */
		if (!(cmd->cmd_flags & ICF_OOO_CMDSN) && !cmd->immediate_cmd &&
		     iscsi_sna_gte(cmd->cmd_sn, conn->sess->exp_cmd_sn)) {
			list_del(&cmd->i_conn_node);
			spin_unlock_bh(&conn->cmd_lock);
			iscsit_free_cmd(cmd);
			spin_lock_bh(&conn->cmd_lock);
			continue;
		}

		cmd_count++;
		pr_debug("Preparing Opcode: 0x%02x, ITT: 0x%08x,"
			" CmdSN: 0x%08x, StatSN: 0x%08x, CID: %hu for"
			" realligence.\n", cmd->iscsi_opcode,
			cmd->init_task_tag, cmd->cmd_sn, cmd->stat_sn,
			conn->cid);

		cmd->deferred_i_state = cmd->i_state;
		cmd->i_state = ISTATE_IN_CONNECTION_RECOVERY;

		if (cmd->data_direction == DMA_TO_DEVICE)
			iscsit_stop_dataout_timer(cmd);

		cmd->sess = conn->sess;

		list_del(&cmd->i_conn_node);
		spin_unlock_bh(&conn->cmd_lock);

		iscsit_free_all_datain_reqs(cmd);

		transport_wait_for_tasks(&cmd->se_cmd);
		/*
		 * Add the struct iscsi_cmd to the connection recovery cmd list
		 */
		spin_lock(&cr->conn_recovery_cmd_lock);
		list_add_tail(&cmd->i_conn_node, &cr->conn_recovery_cmd_list);
		spin_unlock(&cr->conn_recovery_cmd_lock);

		spin_lock_bh(&conn->cmd_lock);
		cmd->cr = cr;
		cmd->conn = NULL;
	}
	spin_unlock_bh(&conn->cmd_lock);
	/*
	 * Fill in the various values in the preallocated struct iscsi_conn_recovery.
	 */
	cr->cid = conn->cid;
	cr->cmd_count = cmd_count;
	cr->maxrecvdatasegmentlength = conn->conn_ops->MaxRecvDataSegmentLength;
	cr->maxxmitdatasegmentlength = conn->conn_ops->MaxXmitDataSegmentLength;
	cr->sess = conn->sess;

	iscsit_attach_inactive_connection_recovery_entry(conn->sess, cr);

	return 0;
}

int iscsit_connection_recovery_transport_reset(struct iscsi_conn *conn)
{
	atomic_set(&conn->connection_recovery, 1);

	if (iscsit_close_connection(conn) < 0)
		return -1;

	return 0;
}

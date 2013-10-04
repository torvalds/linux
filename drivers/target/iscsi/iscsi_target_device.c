/*******************************************************************************
 * This file contains the iSCSI Virtual Device and Disk Transport
 * agnostic related functions.
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

#include <scsi/scsi_device.h>
#include <target/target_core_base.h>
#include <target/target_core_fabric.h>

#include "iscsi_target_core.h"
#include "iscsi_target_device.h"
#include "iscsi_target_tpg.h"
#include "iscsi_target_util.h"

void iscsit_determine_maxcmdsn(struct iscsi_session *sess)
{
	struct se_node_acl *se_nacl;

	/*
	 * This is a discovery session, the single queue slot was already
	 * assigned in iscsi_login_zero_tsih().  Since only Logout and
	 * Text Opcodes are allowed during discovery we do not have to worry
	 * about the HBA's queue depth here.
	 */
	if (sess->sess_ops->SessionType)
		return;

	se_nacl = sess->se_sess->se_node_acl;

	/*
	 * This is a normal session, set the Session's CmdSN window to the
	 * struct se_node_acl->queue_depth.  The value in struct se_node_acl->queue_depth
	 * has already been validated as a legal value in
	 * core_set_queue_depth_for_node().
	 */
	sess->cmdsn_window = se_nacl->queue_depth;
	sess->max_cmd_sn = (sess->max_cmd_sn + se_nacl->queue_depth) - 1;
}

void iscsit_increment_maxcmdsn(struct iscsi_cmd *cmd, struct iscsi_session *sess)
{
	if (cmd->immediate_cmd || cmd->maxcmdsn_inc)
		return;

	cmd->maxcmdsn_inc = 1;

	if (!mutex_trylock(&sess->cmdsn_mutex)) {
		sess->max_cmd_sn += 1;
		pr_debug("Updated MaxCmdSN to 0x%08x\n", sess->max_cmd_sn);
		return;
	}
	sess->max_cmd_sn += 1;
	pr_debug("Updated MaxCmdSN to 0x%08x\n", sess->max_cmd_sn);
	mutex_unlock(&sess->cmdsn_mutex);
}
EXPORT_SYMBOL(iscsit_increment_maxcmdsn);

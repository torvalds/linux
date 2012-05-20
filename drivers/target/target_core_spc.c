/*
 * SCSI Primary Commands (SPC) parsing and emulation.
 *
 * Copyright (c) 2002, 2003, 2004, 2005 PyX Technologies, Inc.
 * Copyright (c) 2005, 2006, 2007 SBE, Inc.
 * Copyright (c) 2007-2010 Rising Tide Systems
 * Copyright (c) 2008-2010 Linux-iSCSI.org
 *
 * Nicholas A. Bellinger <nab@kernel.org>
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
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <asm/unaligned.h>

#include <scsi/scsi.h>
#include <scsi/scsi_tcq.h>

#include <target/target_core_base.h>
#include <target/target_core_backend.h>
#include <target/target_core_fabric.h>

#include "target_core_internal.h"
#include "target_core_pr.h"
#include "target_core_ua.h"


int spc_parse_cdb(struct se_cmd *cmd, unsigned int *size, bool passthrough)
{
	struct se_subsystem_dev *su_dev = cmd->se_dev->se_sub_dev;
	unsigned char *cdb = cmd->t_task_cdb;

	switch (cdb[0]) {
	case MODE_SELECT:
		*size = cdb[4];
		break;
	case MODE_SELECT_10:
		*size = (cdb[7] << 8) + cdb[8];
		break;
	case MODE_SENSE:
		*size = cdb[4];
		if (!passthrough)
			cmd->execute_cmd = target_emulate_modesense;
		break;
	case MODE_SENSE_10:
		*size = (cdb[7] << 8) + cdb[8];
		if (!passthrough)
			cmd->execute_cmd = target_emulate_modesense;
		break;
	case LOG_SELECT:
	case LOG_SENSE:
		*size = (cdb[7] << 8) + cdb[8];
		break;
	case PERSISTENT_RESERVE_IN:
		if (su_dev->t10_pr.res_type == SPC3_PERSISTENT_RESERVATIONS)
			cmd->execute_cmd = target_scsi3_emulate_pr_in;
		*size = (cdb[7] << 8) + cdb[8];
		break;
	case PERSISTENT_RESERVE_OUT:
		if (su_dev->t10_pr.res_type == SPC3_PERSISTENT_RESERVATIONS)
			cmd->execute_cmd = target_scsi3_emulate_pr_out;
		*size = (cdb[7] << 8) + cdb[8];
		break;
	case RELEASE:
	case RELEASE_10:
		if (cdb[0] == RELEASE_10)
			*size = (cdb[7] << 8) | cdb[8];
		else
			*size = cmd->data_length;

		if (su_dev->t10_pr.res_type != SPC_PASSTHROUGH)
			cmd->execute_cmd = target_scsi2_reservation_release;
		break;
	case RESERVE:
	case RESERVE_10:
		/*
		 * The SPC-2 RESERVE does not contain a size in the SCSI CDB.
		 * Assume the passthrough or $FABRIC_MOD will tell us about it.
		 */
		if (cdb[0] == RESERVE_10)
			*size = (cdb[7] << 8) | cdb[8];
		else
			*size = cmd->data_length;

		/*
		 * Setup the legacy emulated handler for SPC-2 and
		 * >= SPC-3 compatible reservation handling (CRH=1)
		 * Otherwise, we assume the underlying SCSI logic is
		 * is running in SPC_PASSTHROUGH, and wants reservations
		 * emulation disabled.
		 */
		if (su_dev->t10_pr.res_type != SPC_PASSTHROUGH)
			cmd->execute_cmd = target_scsi2_reservation_reserve;
		break;
	case REQUEST_SENSE:
		*size = cdb[4];
		if (!passthrough)
			cmd->execute_cmd = target_emulate_request_sense;
		break;
	case INQUIRY:
		*size = (cdb[3] << 8) + cdb[4];

		/*
		 * Do implict HEAD_OF_QUEUE processing for INQUIRY.
		 * See spc4r17 section 5.3
		 */
		if (cmd->se_dev->dev_task_attr_type == SAM_TASK_ATTR_EMULATED)
			cmd->sam_task_attr = MSG_HEAD_TAG;
		if (!passthrough)
			cmd->execute_cmd = target_emulate_inquiry;
		break;
	case SECURITY_PROTOCOL_IN:
	case SECURITY_PROTOCOL_OUT:
		*size = (cdb[6] << 24) | (cdb[7] << 16) | (cdb[8] << 8) | cdb[9];
		break;
	case EXTENDED_COPY:
	case READ_ATTRIBUTE:
	case RECEIVE_COPY_RESULTS:
	case WRITE_ATTRIBUTE:
		*size = (cdb[10] << 24) | (cdb[11] << 16) |
		       (cdb[12] << 8) | cdb[13];
		break;
	case RECEIVE_DIAGNOSTIC:
	case SEND_DIAGNOSTIC:
		*size = (cdb[3] << 8) | cdb[4];
		break;
	case WRITE_BUFFER:
		*size = (cdb[6] << 16) + (cdb[7] << 8) + cdb[8];
		break;
	case REPORT_LUNS:
		cmd->execute_cmd = target_report_luns;
		*size = (cdb[6] << 24) | (cdb[7] << 16) | (cdb[8] << 8) | cdb[9];
		/*
		 * Do implict HEAD_OF_QUEUE processing for REPORT_LUNS
		 * See spc4r17 section 5.3
		 */
		if (cmd->se_dev->dev_task_attr_type == SAM_TASK_ATTR_EMULATED)
			cmd->sam_task_attr = MSG_HEAD_TAG;
		break;
	case TEST_UNIT_READY:
		*size = 0;
		if (!passthrough)
			cmd->execute_cmd = target_emulate_noop;
		break;
	default:
		pr_warn("TARGET_CORE[%s]: Unsupported SCSI Opcode"
			" 0x%02x, sending CHECK_CONDITION.\n",
			cmd->se_tfo->get_fabric_name(), cdb[0]);
		cmd->se_cmd_flags |= SCF_SCSI_CDB_EXCEPTION;
		cmd->scsi_sense_reason = TCM_UNSUPPORTED_SCSI_OPCODE;
		return -EINVAL;
	}

	return 0;
}
EXPORT_SYMBOL(spc_parse_cdb);

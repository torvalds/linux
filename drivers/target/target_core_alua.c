/*******************************************************************************
 * Filename:  target_core_alua.c
 *
 * This file contains SPC-3 compliant asymmetric logical unit assigntment (ALUA)
 *
 * (c) Copyright 2009-2013 Datera, Inc.
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
 *
 ******************************************************************************/

#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/configfs.h>
#include <linux/delay.h>
#include <linux/export.h>
#include <linux/fcntl.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <scsi/scsi_proto.h>
#include <asm/unaligned.h>

#include <target/target_core_base.h>
#include <target/target_core_backend.h>
#include <target/target_core_fabric.h>

#include "target_core_internal.h"
#include "target_core_alua.h"
#include "target_core_ua.h"

static sense_reason_t core_alua_check_transition(int state, int valid,
						 int *primary, int explicit);
static int core_alua_set_tg_pt_secondary_state(
		struct se_lun *lun, int explicit, int offline);

static char *core_alua_dump_state(int state);

static void __target_attach_tg_pt_gp(struct se_lun *lun,
		struct t10_alua_tg_pt_gp *tg_pt_gp);

static u16 alua_lu_gps_counter;
static u32 alua_lu_gps_count;

static DEFINE_SPINLOCK(lu_gps_lock);
static LIST_HEAD(lu_gps_list);

struct t10_alua_lu_gp *default_lu_gp;

/*
 * REPORT REFERRALS
 *
 * See sbc3r35 section 5.23
 */
sense_reason_t
target_emulate_report_referrals(struct se_cmd *cmd)
{
	struct se_device *dev = cmd->se_dev;
	struct t10_alua_lba_map *map;
	struct t10_alua_lba_map_member *map_mem;
	unsigned char *buf;
	u32 rd_len = 0, off;

	if (cmd->data_length < 4) {
		pr_warn("REPORT REFERRALS allocation length %u too"
			" small\n", cmd->data_length);
		return TCM_INVALID_CDB_FIELD;
	}

	buf = transport_kmap_data_sg(cmd);
	if (!buf)
		return TCM_LOGICAL_UNIT_COMMUNICATION_FAILURE;

	off = 4;
	spin_lock(&dev->t10_alua.lba_map_lock);
	if (list_empty(&dev->t10_alua.lba_map_list)) {
		spin_unlock(&dev->t10_alua.lba_map_lock);
		transport_kunmap_data_sg(cmd);

		return TCM_UNSUPPORTED_SCSI_OPCODE;
	}

	list_for_each_entry(map, &dev->t10_alua.lba_map_list,
			    lba_map_list) {
		int desc_num = off + 3;
		int pg_num;

		off += 4;
		if (cmd->data_length > off)
			put_unaligned_be64(map->lba_map_first_lba, &buf[off]);
		off += 8;
		if (cmd->data_length > off)
			put_unaligned_be64(map->lba_map_last_lba, &buf[off]);
		off += 8;
		rd_len += 20;
		pg_num = 0;
		list_for_each_entry(map_mem, &map->lba_map_mem_list,
				    lba_map_mem_list) {
			int alua_state = map_mem->lba_map_mem_alua_state;
			int alua_pg_id = map_mem->lba_map_mem_alua_pg_id;

			if (cmd->data_length > off)
				buf[off] = alua_state & 0x0f;
			off += 2;
			if (cmd->data_length > off)
				buf[off] = (alua_pg_id >> 8) & 0xff;
			off++;
			if (cmd->data_length > off)
				buf[off] = (alua_pg_id & 0xff);
			off++;
			rd_len += 4;
			pg_num++;
		}
		if (cmd->data_length > desc_num)
			buf[desc_num] = pg_num;
	}
	spin_unlock(&dev->t10_alua.lba_map_lock);

	/*
	 * Set the RETURN DATA LENGTH set in the header of the DataIN Payload
	 */
	put_unaligned_be16(rd_len, &buf[2]);

	transport_kunmap_data_sg(cmd);

	target_complete_cmd(cmd, GOOD);
	return 0;
}

/*
 * REPORT_TARGET_PORT_GROUPS
 *
 * See spc4r17 section 6.27
 */
sense_reason_t
target_emulate_report_target_port_groups(struct se_cmd *cmd)
{
	struct se_device *dev = cmd->se_dev;
	struct t10_alua_tg_pt_gp *tg_pt_gp;
	struct se_lun *lun;
	unsigned char *buf;
	u32 rd_len = 0, off;
	int ext_hdr = (cmd->t_task_cdb[1] & 0x20);

	/*
	 * Skip over RESERVED area to first Target port group descriptor
	 * depending on the PARAMETER DATA FORMAT type..
	 */
	if (ext_hdr != 0)
		off = 8;
	else
		off = 4;

	if (cmd->data_length < off) {
		pr_warn("REPORT TARGET PORT GROUPS allocation length %u too"
			" small for %s header\n", cmd->data_length,
			(ext_hdr) ? "extended" : "normal");
		return TCM_INVALID_CDB_FIELD;
	}
	buf = transport_kmap_data_sg(cmd);
	if (!buf)
		return TCM_LOGICAL_UNIT_COMMUNICATION_FAILURE;

	spin_lock(&dev->t10_alua.tg_pt_gps_lock);
	list_for_each_entry(tg_pt_gp, &dev->t10_alua.tg_pt_gps_list,
			tg_pt_gp_list) {
		/*
		 * Check if the Target port group and Target port descriptor list
		 * based on tg_pt_gp_members count will fit into the response payload.
		 * Otherwise, bump rd_len to let the initiator know we have exceeded
		 * the allocation length and the response is truncated.
		 */
		if ((off + 8 + (tg_pt_gp->tg_pt_gp_members * 4)) >
		     cmd->data_length) {
			rd_len += 8 + (tg_pt_gp->tg_pt_gp_members * 4);
			continue;
		}
		/*
		 * PREF: Preferred target port bit, determine if this
		 * bit should be set for port group.
		 */
		if (tg_pt_gp->tg_pt_gp_pref)
			buf[off] = 0x80;
		/*
		 * Set the ASYMMETRIC ACCESS State
		 */
		buf[off++] |= tg_pt_gp->tg_pt_gp_alua_access_state & 0xff;
		/*
		 * Set supported ASYMMETRIC ACCESS State bits
		 */
		buf[off++] |= tg_pt_gp->tg_pt_gp_alua_supported_states;
		/*
		 * TARGET PORT GROUP
		 */
		put_unaligned_be16(tg_pt_gp->tg_pt_gp_id, &buf[off]);
		off += 2;

		off++; /* Skip over Reserved */
		/*
		 * STATUS CODE
		 */
		buf[off++] = (tg_pt_gp->tg_pt_gp_alua_access_status & 0xff);
		/*
		 * Vendor Specific field
		 */
		buf[off++] = 0x00;
		/*
		 * TARGET PORT COUNT
		 */
		buf[off++] = (tg_pt_gp->tg_pt_gp_members & 0xff);
		rd_len += 8;

		spin_lock(&tg_pt_gp->tg_pt_gp_lock);
		list_for_each_entry(lun, &tg_pt_gp->tg_pt_gp_lun_list,
				lun_tg_pt_gp_link) {
			/*
			 * Start Target Port descriptor format
			 *
			 * See spc4r17 section 6.2.7 Table 247
			 */
			off += 2; /* Skip over Obsolete */
			/*
			 * Set RELATIVE TARGET PORT IDENTIFIER
			 */
			put_unaligned_be16(lun->lun_rtpi, &buf[off]);
			off += 2;
			rd_len += 4;
		}
		spin_unlock(&tg_pt_gp->tg_pt_gp_lock);
	}
	spin_unlock(&dev->t10_alua.tg_pt_gps_lock);
	/*
	 * Set the RETURN DATA LENGTH set in the header of the DataIN Payload
	 */
	put_unaligned_be32(rd_len, &buf[0]);

	/*
	 * Fill in the Extended header parameter data format if requested
	 */
	if (ext_hdr != 0) {
		buf[4] = 0x10;
		/*
		 * Set the implicit transition time (in seconds) for the application
		 * client to use as a base for it's transition timeout value.
		 *
		 * Use the current tg_pt_gp_mem -> tg_pt_gp membership from the LUN
		 * this CDB was received upon to determine this value individually
		 * for ALUA target port group.
		 */
		spin_lock(&cmd->se_lun->lun_tg_pt_gp_lock);
		tg_pt_gp = cmd->se_lun->lun_tg_pt_gp;
		if (tg_pt_gp)
			buf[5] = tg_pt_gp->tg_pt_gp_implicit_trans_secs;
		spin_unlock(&cmd->se_lun->lun_tg_pt_gp_lock);
	}
	transport_kunmap_data_sg(cmd);

	target_complete_cmd_with_length(cmd, GOOD, rd_len + 4);
	return 0;
}

/*
 * SET_TARGET_PORT_GROUPS for explicit ALUA operation.
 *
 * See spc4r17 section 6.35
 */
sense_reason_t
target_emulate_set_target_port_groups(struct se_cmd *cmd)
{
	struct se_device *dev = cmd->se_dev;
	struct se_lun *l_lun = cmd->se_lun;
	struct se_node_acl *nacl = cmd->se_sess->se_node_acl;
	struct t10_alua_tg_pt_gp *tg_pt_gp = NULL, *l_tg_pt_gp;
	unsigned char *buf;
	unsigned char *ptr;
	sense_reason_t rc = TCM_NO_SENSE;
	u32 len = 4; /* Skip over RESERVED area in header */
	int alua_access_state, primary = 0, valid_states;
	u16 tg_pt_id, rtpi;

	if (cmd->data_length < 4) {
		pr_warn("SET TARGET PORT GROUPS parameter list length %u too"
			" small\n", cmd->data_length);
		return TCM_INVALID_PARAMETER_LIST;
	}

	buf = transport_kmap_data_sg(cmd);
	if (!buf)
		return TCM_LOGICAL_UNIT_COMMUNICATION_FAILURE;

	/*
	 * Determine if explicit ALUA via SET_TARGET_PORT_GROUPS is allowed
	 * for the local tg_pt_gp.
	 */
	spin_lock(&l_lun->lun_tg_pt_gp_lock);
	l_tg_pt_gp = l_lun->lun_tg_pt_gp;
	if (!l_tg_pt_gp) {
		spin_unlock(&l_lun->lun_tg_pt_gp_lock);
		pr_err("Unable to access l_lun->tg_pt_gp\n");
		rc = TCM_UNSUPPORTED_SCSI_OPCODE;
		goto out;
	}

	if (!(l_tg_pt_gp->tg_pt_gp_alua_access_type & TPGS_EXPLICIT_ALUA)) {
		spin_unlock(&l_lun->lun_tg_pt_gp_lock);
		pr_debug("Unable to process SET_TARGET_PORT_GROUPS"
				" while TPGS_EXPLICIT_ALUA is disabled\n");
		rc = TCM_UNSUPPORTED_SCSI_OPCODE;
		goto out;
	}
	valid_states = l_tg_pt_gp->tg_pt_gp_alua_supported_states;
	spin_unlock(&l_lun->lun_tg_pt_gp_lock);

	ptr = &buf[4]; /* Skip over RESERVED area in header */

	while (len < cmd->data_length) {
		bool found = false;
		alua_access_state = (ptr[0] & 0x0f);
		/*
		 * Check the received ALUA access state, and determine if
		 * the state is a primary or secondary target port asymmetric
		 * access state.
		 */
		rc = core_alua_check_transition(alua_access_state, valid_states,
						&primary, 1);
		if (rc) {
			/*
			 * If the SET TARGET PORT GROUPS attempts to establish
			 * an invalid combination of target port asymmetric
			 * access states or attempts to establish an
			 * unsupported target port asymmetric access state,
			 * then the command shall be terminated with CHECK
			 * CONDITION status, with the sense key set to ILLEGAL
			 * REQUEST, and the additional sense code set to INVALID
			 * FIELD IN PARAMETER LIST.
			 */
			goto out;
		}

		/*
		 * If the ASYMMETRIC ACCESS STATE field (see table 267)
		 * specifies a primary target port asymmetric access state,
		 * then the TARGET PORT GROUP OR TARGET PORT field specifies
		 * a primary target port group for which the primary target
		 * port asymmetric access state shall be changed. If the
		 * ASYMMETRIC ACCESS STATE field specifies a secondary target
		 * port asymmetric access state, then the TARGET PORT GROUP OR
		 * TARGET PORT field specifies the relative target port
		 * identifier (see 3.1.120) of the target port for which the
		 * secondary target port asymmetric access state shall be
		 * changed.
		 */
		if (primary) {
			tg_pt_id = get_unaligned_be16(ptr + 2);
			/*
			 * Locate the matching target port group ID from
			 * the global tg_pt_gp list
			 */
			spin_lock(&dev->t10_alua.tg_pt_gps_lock);
			list_for_each_entry(tg_pt_gp,
					&dev->t10_alua.tg_pt_gps_list,
					tg_pt_gp_list) {
				if (!tg_pt_gp->tg_pt_gp_valid_id)
					continue;

				if (tg_pt_id != tg_pt_gp->tg_pt_gp_id)
					continue;

				atomic_inc_mb(&tg_pt_gp->tg_pt_gp_ref_cnt);

				spin_unlock(&dev->t10_alua.tg_pt_gps_lock);

				if (!core_alua_do_port_transition(tg_pt_gp,
						dev, l_lun, nacl,
						alua_access_state, 1))
					found = true;

				spin_lock(&dev->t10_alua.tg_pt_gps_lock);
				atomic_dec_mb(&tg_pt_gp->tg_pt_gp_ref_cnt);
				break;
			}
			spin_unlock(&dev->t10_alua.tg_pt_gps_lock);
		} else {
			struct se_lun *lun;

			/*
			 * Extract the RELATIVE TARGET PORT IDENTIFIER to identify
			 * the Target Port in question for the the incoming
			 * SET_TARGET_PORT_GROUPS op.
			 */
			rtpi = get_unaligned_be16(ptr + 2);
			/*
			 * Locate the matching relative target port identifier
			 * for the struct se_device storage object.
			 */
			spin_lock(&dev->se_port_lock);
			list_for_each_entry(lun, &dev->dev_sep_list,
							lun_dev_link) {
				if (lun->lun_rtpi != rtpi)
					continue;

				// XXX: racy unlock
				spin_unlock(&dev->se_port_lock);

				if (!core_alua_set_tg_pt_secondary_state(
						lun, 1, 1))
					found = true;

				spin_lock(&dev->se_port_lock);
				break;
			}
			spin_unlock(&dev->se_port_lock);
		}

		if (!found) {
			rc = TCM_INVALID_PARAMETER_LIST;
			goto out;
		}

		ptr += 4;
		len += 4;
	}

out:
	transport_kunmap_data_sg(cmd);
	if (!rc)
		target_complete_cmd(cmd, GOOD);
	return rc;
}

static inline void set_ascq(struct se_cmd *cmd, u8 alua_ascq)
{
	/*
	 * Set SCSI additional sense code (ASC) to 'LUN Not Accessible';
	 * The ALUA additional sense code qualifier (ASCQ) is determined
	 * by the ALUA primary or secondary access state..
	 */
	pr_debug("[%s]: ALUA TG Port not available, "
		"SenseKey: NOT_READY, ASC/ASCQ: "
		"0x04/0x%02x\n",
		cmd->se_tfo->fabric_name, alua_ascq);

	cmd->scsi_asc = 0x04;
	cmd->scsi_ascq = alua_ascq;
}

static inline void core_alua_state_nonoptimized(
	struct se_cmd *cmd,
	unsigned char *cdb,
	int nonop_delay_msecs)
{
	/*
	 * Set SCF_ALUA_NON_OPTIMIZED here, this value will be checked
	 * later to determine if processing of this cmd needs to be
	 * temporarily delayed for the Active/NonOptimized primary access state.
	 */
	cmd->se_cmd_flags |= SCF_ALUA_NON_OPTIMIZED;
	cmd->alua_nonop_delay = nonop_delay_msecs;
}

static inline int core_alua_state_lba_dependent(
	struct se_cmd *cmd,
	struct t10_alua_tg_pt_gp *tg_pt_gp)
{
	struct se_device *dev = cmd->se_dev;
	u64 segment_size, segment_mult, sectors, lba;

	/* Only need to check for cdb actually containing LBAs */
	if (!(cmd->se_cmd_flags & SCF_SCSI_DATA_CDB))
		return 0;

	spin_lock(&dev->t10_alua.lba_map_lock);
	segment_size = dev->t10_alua.lba_map_segment_size;
	segment_mult = dev->t10_alua.lba_map_segment_multiplier;
	sectors = cmd->data_length / dev->dev_attrib.block_size;

	lba = cmd->t_task_lba;
	while (lba < cmd->t_task_lba + sectors) {
		struct t10_alua_lba_map *cur_map = NULL, *map;
		struct t10_alua_lba_map_member *map_mem;

		list_for_each_entry(map, &dev->t10_alua.lba_map_list,
				    lba_map_list) {
			u64 start_lba, last_lba;
			u64 first_lba = map->lba_map_first_lba;

			if (segment_mult) {
				u64 tmp = lba;
				start_lba = do_div(tmp, segment_size * segment_mult);

				last_lba = first_lba + segment_size - 1;
				if (start_lba >= first_lba &&
				    start_lba <= last_lba) {
					lba += segment_size;
					cur_map = map;
					break;
				}
			} else {
				last_lba = map->lba_map_last_lba;
				if (lba >= first_lba && lba <= last_lba) {
					lba = last_lba + 1;
					cur_map = map;
					break;
				}
			}
		}
		if (!cur_map) {
			spin_unlock(&dev->t10_alua.lba_map_lock);
			set_ascq(cmd, ASCQ_04H_ALUA_TG_PT_UNAVAILABLE);
			return 1;
		}
		list_for_each_entry(map_mem, &cur_map->lba_map_mem_list,
				    lba_map_mem_list) {
			if (map_mem->lba_map_mem_alua_pg_id !=
			    tg_pt_gp->tg_pt_gp_id)
				continue;
			switch(map_mem->lba_map_mem_alua_state) {
			case ALUA_ACCESS_STATE_STANDBY:
				spin_unlock(&dev->t10_alua.lba_map_lock);
				set_ascq(cmd, ASCQ_04H_ALUA_TG_PT_STANDBY);
				return 1;
			case ALUA_ACCESS_STATE_UNAVAILABLE:
				spin_unlock(&dev->t10_alua.lba_map_lock);
				set_ascq(cmd, ASCQ_04H_ALUA_TG_PT_UNAVAILABLE);
				return 1;
			default:
				break;
			}
		}
	}
	spin_unlock(&dev->t10_alua.lba_map_lock);
	return 0;
}

static inline int core_alua_state_standby(
	struct se_cmd *cmd,
	unsigned char *cdb)
{
	/*
	 * Allowed CDBs for ALUA_ACCESS_STATE_STANDBY as defined by
	 * spc4r17 section 5.9.2.4.4
	 */
	switch (cdb[0]) {
	case INQUIRY:
	case LOG_SELECT:
	case LOG_SENSE:
	case MODE_SELECT:
	case MODE_SENSE:
	case REPORT_LUNS:
	case RECEIVE_DIAGNOSTIC:
	case SEND_DIAGNOSTIC:
	case READ_CAPACITY:
		return 0;
	case SERVICE_ACTION_IN_16:
		switch (cdb[1] & 0x1f) {
		case SAI_READ_CAPACITY_16:
			return 0;
		default:
			set_ascq(cmd, ASCQ_04H_ALUA_TG_PT_STANDBY);
			return 1;
		}
	case MAINTENANCE_IN:
		switch (cdb[1] & 0x1f) {
		case MI_REPORT_TARGET_PGS:
			return 0;
		default:
			set_ascq(cmd, ASCQ_04H_ALUA_TG_PT_STANDBY);
			return 1;
		}
	case MAINTENANCE_OUT:
		switch (cdb[1]) {
		case MO_SET_TARGET_PGS:
			return 0;
		default:
			set_ascq(cmd, ASCQ_04H_ALUA_TG_PT_STANDBY);
			return 1;
		}
	case REQUEST_SENSE:
	case PERSISTENT_RESERVE_IN:
	case PERSISTENT_RESERVE_OUT:
	case READ_BUFFER:
	case WRITE_BUFFER:
		return 0;
	default:
		set_ascq(cmd, ASCQ_04H_ALUA_TG_PT_STANDBY);
		return 1;
	}

	return 0;
}

static inline int core_alua_state_unavailable(
	struct se_cmd *cmd,
	unsigned char *cdb)
{
	/*
	 * Allowed CDBs for ALUA_ACCESS_STATE_UNAVAILABLE as defined by
	 * spc4r17 section 5.9.2.4.5
	 */
	switch (cdb[0]) {
	case INQUIRY:
	case REPORT_LUNS:
		return 0;
	case MAINTENANCE_IN:
		switch (cdb[1] & 0x1f) {
		case MI_REPORT_TARGET_PGS:
			return 0;
		default:
			set_ascq(cmd, ASCQ_04H_ALUA_TG_PT_UNAVAILABLE);
			return 1;
		}
	case MAINTENANCE_OUT:
		switch (cdb[1]) {
		case MO_SET_TARGET_PGS:
			return 0;
		default:
			set_ascq(cmd, ASCQ_04H_ALUA_TG_PT_UNAVAILABLE);
			return 1;
		}
	case REQUEST_SENSE:
	case READ_BUFFER:
	case WRITE_BUFFER:
		return 0;
	default:
		set_ascq(cmd, ASCQ_04H_ALUA_TG_PT_UNAVAILABLE);
		return 1;
	}

	return 0;
}

static inline int core_alua_state_transition(
	struct se_cmd *cmd,
	unsigned char *cdb)
{
	/*
	 * Allowed CDBs for ALUA_ACCESS_STATE_TRANSITION as defined by
	 * spc4r17 section 5.9.2.5
	 */
	switch (cdb[0]) {
	case INQUIRY:
	case REPORT_LUNS:
		return 0;
	case MAINTENANCE_IN:
		switch (cdb[1] & 0x1f) {
		case MI_REPORT_TARGET_PGS:
			return 0;
		default:
			set_ascq(cmd, ASCQ_04H_ALUA_STATE_TRANSITION);
			return 1;
		}
	case REQUEST_SENSE:
	case READ_BUFFER:
	case WRITE_BUFFER:
		return 0;
	default:
		set_ascq(cmd, ASCQ_04H_ALUA_STATE_TRANSITION);
		return 1;
	}

	return 0;
}

/*
 * return 1: Is used to signal LUN not accessible, and check condition/not ready
 * return 0: Used to signal success
 * return -1: Used to signal failure, and invalid cdb field
 */
sense_reason_t
target_alua_state_check(struct se_cmd *cmd)
{
	struct se_device *dev = cmd->se_dev;
	unsigned char *cdb = cmd->t_task_cdb;
	struct se_lun *lun = cmd->se_lun;
	struct t10_alua_tg_pt_gp *tg_pt_gp;
	int out_alua_state, nonop_delay_msecs;

	if (dev->se_hba->hba_flags & HBA_FLAGS_INTERNAL_USE)
		return 0;
	if (dev->transport->transport_flags & TRANSPORT_FLAG_PASSTHROUGH_ALUA)
		return 0;

	/*
	 * First, check for a struct se_port specific secondary ALUA target port
	 * access state: OFFLINE
	 */
	if (atomic_read(&lun->lun_tg_pt_secondary_offline)) {
		pr_debug("ALUA: Got secondary offline status for local"
				" target port\n");
		set_ascq(cmd, ASCQ_04H_ALUA_OFFLINE);
		return TCM_CHECK_CONDITION_NOT_READY;
	}

	if (!lun->lun_tg_pt_gp)
		return 0;

	spin_lock(&lun->lun_tg_pt_gp_lock);
	tg_pt_gp = lun->lun_tg_pt_gp;
	out_alua_state = tg_pt_gp->tg_pt_gp_alua_access_state;
	nonop_delay_msecs = tg_pt_gp->tg_pt_gp_nonop_delay_msecs;

	// XXX: keeps using tg_pt_gp witout reference after unlock
	spin_unlock(&lun->lun_tg_pt_gp_lock);
	/*
	 * Process ALUA_ACCESS_STATE_ACTIVE_OPTIMIZED in a separate conditional
	 * statement so the compiler knows explicitly to check this case first.
	 * For the Optimized ALUA access state case, we want to process the
	 * incoming fabric cmd ASAP..
	 */
	if (out_alua_state == ALUA_ACCESS_STATE_ACTIVE_OPTIMIZED)
		return 0;

	switch (out_alua_state) {
	case ALUA_ACCESS_STATE_ACTIVE_NON_OPTIMIZED:
		core_alua_state_nonoptimized(cmd, cdb, nonop_delay_msecs);
		break;
	case ALUA_ACCESS_STATE_STANDBY:
		if (core_alua_state_standby(cmd, cdb))
			return TCM_CHECK_CONDITION_NOT_READY;
		break;
	case ALUA_ACCESS_STATE_UNAVAILABLE:
		if (core_alua_state_unavailable(cmd, cdb))
			return TCM_CHECK_CONDITION_NOT_READY;
		break;
	case ALUA_ACCESS_STATE_TRANSITION:
		if (core_alua_state_transition(cmd, cdb))
			return TCM_CHECK_CONDITION_NOT_READY;
		break;
	case ALUA_ACCESS_STATE_LBA_DEPENDENT:
		if (core_alua_state_lba_dependent(cmd, tg_pt_gp))
			return TCM_CHECK_CONDITION_NOT_READY;
		break;
	/*
	 * OFFLINE is a secondary ALUA target port group access state, that is
	 * handled above with struct se_lun->lun_tg_pt_secondary_offline=1
	 */
	case ALUA_ACCESS_STATE_OFFLINE:
	default:
		pr_err("Unknown ALUA access state: 0x%02x\n",
				out_alua_state);
		return TCM_INVALID_CDB_FIELD;
	}

	return 0;
}

/*
 * Check implicit and explicit ALUA state change request.
 */
static sense_reason_t
core_alua_check_transition(int state, int valid, int *primary, int explicit)
{
	/*
	 * OPTIMIZED, NON-OPTIMIZED, STANDBY and UNAVAILABLE are
	 * defined as primary target port asymmetric access states.
	 */
	switch (state) {
	case ALUA_ACCESS_STATE_ACTIVE_OPTIMIZED:
		if (!(valid & ALUA_AO_SUP))
			goto not_supported;
		*primary = 1;
		break;
	case ALUA_ACCESS_STATE_ACTIVE_NON_OPTIMIZED:
		if (!(valid & ALUA_AN_SUP))
			goto not_supported;
		*primary = 1;
		break;
	case ALUA_ACCESS_STATE_STANDBY:
		if (!(valid & ALUA_S_SUP))
			goto not_supported;
		*primary = 1;
		break;
	case ALUA_ACCESS_STATE_UNAVAILABLE:
		if (!(valid & ALUA_U_SUP))
			goto not_supported;
		*primary = 1;
		break;
	case ALUA_ACCESS_STATE_LBA_DEPENDENT:
		if (!(valid & ALUA_LBD_SUP))
			goto not_supported;
		*primary = 1;
		break;
	case ALUA_ACCESS_STATE_OFFLINE:
		/*
		 * OFFLINE state is defined as a secondary target port
		 * asymmetric access state.
		 */
		if (!(valid & ALUA_O_SUP))
			goto not_supported;
		*primary = 0;
		break;
	case ALUA_ACCESS_STATE_TRANSITION:
		if (!(valid & ALUA_T_SUP) || explicit)
			/*
			 * Transitioning is set internally and by tcmu daemon,
			 * and cannot be selected through a STPG.
			 */
			goto not_supported;
		*primary = 0;
		break;
	default:
		pr_err("Unknown ALUA access state: 0x%02x\n", state);
		return TCM_INVALID_PARAMETER_LIST;
	}

	return 0;

not_supported:
	pr_err("ALUA access state %s not supported",
	       core_alua_dump_state(state));
	return TCM_INVALID_PARAMETER_LIST;
}

static char *core_alua_dump_state(int state)
{
	switch (state) {
	case ALUA_ACCESS_STATE_ACTIVE_OPTIMIZED:
		return "Active/Optimized";
	case ALUA_ACCESS_STATE_ACTIVE_NON_OPTIMIZED:
		return "Active/NonOptimized";
	case ALUA_ACCESS_STATE_LBA_DEPENDENT:
		return "LBA Dependent";
	case ALUA_ACCESS_STATE_STANDBY:
		return "Standby";
	case ALUA_ACCESS_STATE_UNAVAILABLE:
		return "Unavailable";
	case ALUA_ACCESS_STATE_OFFLINE:
		return "Offline";
	case ALUA_ACCESS_STATE_TRANSITION:
		return "Transitioning";
	default:
		return "Unknown";
	}

	return NULL;
}

char *core_alua_dump_status(int status)
{
	switch (status) {
	case ALUA_STATUS_NONE:
		return "None";
	case ALUA_STATUS_ALTERED_BY_EXPLICIT_STPG:
		return "Altered by Explicit STPG";
	case ALUA_STATUS_ALTERED_BY_IMPLICIT_ALUA:
		return "Altered by Implicit ALUA";
	default:
		return "Unknown";
	}

	return NULL;
}

/*
 * Used by fabric modules to determine when we need to delay processing
 * for the Active/NonOptimized paths..
 */
int core_alua_check_nonop_delay(
	struct se_cmd *cmd)
{
	if (!(cmd->se_cmd_flags & SCF_ALUA_NON_OPTIMIZED))
		return 0;
	if (in_interrupt())
		return 0;
	/*
	 * The ALUA Active/NonOptimized access state delay can be disabled
	 * in via configfs with a value of zero
	 */
	if (!cmd->alua_nonop_delay)
		return 0;
	/*
	 * struct se_cmd->alua_nonop_delay gets set by a target port group
	 * defined interval in core_alua_state_nonoptimized()
	 */
	msleep_interruptible(cmd->alua_nonop_delay);
	return 0;
}
EXPORT_SYMBOL(core_alua_check_nonop_delay);

static int core_alua_write_tpg_metadata(
	const char *path,
	unsigned char *md_buf,
	u32 md_buf_len)
{
	struct file *file = filp_open(path, O_RDWR | O_CREAT | O_TRUNC, 0600);
	loff_t pos = 0;
	int ret;

	if (IS_ERR(file)) {
		pr_err("filp_open(%s) for ALUA metadata failed\n", path);
		return -ENODEV;
	}
	ret = kernel_write(file, md_buf, md_buf_len, &pos);
	if (ret < 0)
		pr_err("Error writing ALUA metadata file: %s\n", path);
	fput(file);
	return (ret < 0) ? -EIO : 0;
}

static int core_alua_update_tpg_primary_metadata(
	struct t10_alua_tg_pt_gp *tg_pt_gp)
{
	unsigned char *md_buf;
	struct t10_wwn *wwn = &tg_pt_gp->tg_pt_gp_dev->t10_wwn;
	char *path;
	int len, rc;

	lockdep_assert_held(&tg_pt_gp->tg_pt_gp_transition_mutex);

	md_buf = kzalloc(ALUA_MD_BUF_LEN, GFP_KERNEL);
	if (!md_buf) {
		pr_err("Unable to allocate buf for ALUA metadata\n");
		return -ENOMEM;
	}

	len = snprintf(md_buf, ALUA_MD_BUF_LEN,
			"tg_pt_gp_id=%hu\n"
			"alua_access_state=0x%02x\n"
			"alua_access_status=0x%02x\n",
			tg_pt_gp->tg_pt_gp_id,
			tg_pt_gp->tg_pt_gp_alua_access_state,
			tg_pt_gp->tg_pt_gp_alua_access_status);

	rc = -ENOMEM;
	path = kasprintf(GFP_KERNEL, "%s/alua/tpgs_%s/%s", db_root,
			&wwn->unit_serial[0],
			config_item_name(&tg_pt_gp->tg_pt_gp_group.cg_item));
	if (path) {
		rc = core_alua_write_tpg_metadata(path, md_buf, len);
		kfree(path);
	}
	kfree(md_buf);
	return rc;
}

static void core_alua_queue_state_change_ua(struct t10_alua_tg_pt_gp *tg_pt_gp)
{
	struct se_dev_entry *se_deve;
	struct se_lun *lun;
	struct se_lun_acl *lacl;

	spin_lock(&tg_pt_gp->tg_pt_gp_lock);
	list_for_each_entry(lun, &tg_pt_gp->tg_pt_gp_lun_list,
				lun_tg_pt_gp_link) {
		/*
		 * After an implicit target port asymmetric access state
		 * change, a device server shall establish a unit attention
		 * condition for the initiator port associated with every I_T
		 * nexus with the additional sense code set to ASYMMETRIC
		 * ACCESS STATE CHANGED.
		 *
		 * After an explicit target port asymmetric access state
		 * change, a device server shall establish a unit attention
		 * condition with the additional sense code set to ASYMMETRIC
		 * ACCESS STATE CHANGED for the initiator port associated with
		 * every I_T nexus other than the I_T nexus on which the SET
		 * TARGET PORT GROUPS command
		 */
		if (!percpu_ref_tryget_live(&lun->lun_ref))
			continue;
		spin_unlock(&tg_pt_gp->tg_pt_gp_lock);

		spin_lock(&lun->lun_deve_lock);
		list_for_each_entry(se_deve, &lun->lun_deve_list, lun_link) {
			lacl = rcu_dereference_check(se_deve->se_lun_acl,
					lockdep_is_held(&lun->lun_deve_lock));

			/*
			 * spc4r37 p.242:
			 * After an explicit target port asymmetric access
			 * state change, a device server shall establish a
			 * unit attention condition with the additional sense
			 * code set to ASYMMETRIC ACCESS STATE CHANGED for
			 * the initiator port associated with every I_T nexus
			 * other than the I_T nexus on which the SET TARGET
			 * PORT GROUPS command was received.
			 */
			if ((tg_pt_gp->tg_pt_gp_alua_access_status ==
			     ALUA_STATUS_ALTERED_BY_EXPLICIT_STPG) &&
			   (tg_pt_gp->tg_pt_gp_alua_lun != NULL) &&
			    (tg_pt_gp->tg_pt_gp_alua_lun == lun))
				continue;

			/*
			 * se_deve->se_lun_acl pointer may be NULL for a
			 * entry created without explicit Node+MappedLUN ACLs
			 */
			if (lacl && (tg_pt_gp->tg_pt_gp_alua_nacl != NULL) &&
			    (tg_pt_gp->tg_pt_gp_alua_nacl == lacl->se_lun_nacl))
				continue;

			core_scsi3_ua_allocate(se_deve, 0x2A,
				ASCQ_2AH_ASYMMETRIC_ACCESS_STATE_CHANGED);
		}
		spin_unlock(&lun->lun_deve_lock);

		spin_lock(&tg_pt_gp->tg_pt_gp_lock);
		percpu_ref_put(&lun->lun_ref);
	}
	spin_unlock(&tg_pt_gp->tg_pt_gp_lock);
}

static int core_alua_do_transition_tg_pt(
	struct t10_alua_tg_pt_gp *tg_pt_gp,
	int new_state,
	int explicit)
{
	int prev_state;

	mutex_lock(&tg_pt_gp->tg_pt_gp_transition_mutex);
	/* Nothing to be done here */
	if (tg_pt_gp->tg_pt_gp_alua_access_state == new_state) {
		mutex_unlock(&tg_pt_gp->tg_pt_gp_transition_mutex);
		return 0;
	}

	if (explicit && new_state == ALUA_ACCESS_STATE_TRANSITION) {
		mutex_unlock(&tg_pt_gp->tg_pt_gp_transition_mutex);
		return -EAGAIN;
	}

	/*
	 * Save the old primary ALUA access state, and set the current state
	 * to ALUA_ACCESS_STATE_TRANSITION.
	 */
	prev_state = tg_pt_gp->tg_pt_gp_alua_access_state;
	tg_pt_gp->tg_pt_gp_alua_access_state = ALUA_ACCESS_STATE_TRANSITION;
	tg_pt_gp->tg_pt_gp_alua_access_status = (explicit) ?
				ALUA_STATUS_ALTERED_BY_EXPLICIT_STPG :
				ALUA_STATUS_ALTERED_BY_IMPLICIT_ALUA;

	core_alua_queue_state_change_ua(tg_pt_gp);

	if (new_state == ALUA_ACCESS_STATE_TRANSITION) {
		mutex_unlock(&tg_pt_gp->tg_pt_gp_transition_mutex);
		return 0;
	}

	/*
	 * Check for the optional ALUA primary state transition delay
	 */
	if (tg_pt_gp->tg_pt_gp_trans_delay_msecs != 0)
		msleep_interruptible(tg_pt_gp->tg_pt_gp_trans_delay_msecs);

	/*
	 * Set the current primary ALUA access state to the requested new state
	 */
	tg_pt_gp->tg_pt_gp_alua_access_state = new_state;

	/*
	 * Update the ALUA metadata buf that has been allocated in
	 * core_alua_do_port_transition(), this metadata will be written
	 * to struct file.
	 *
	 * Note that there is the case where we do not want to update the
	 * metadata when the saved metadata is being parsed in userspace
	 * when setting the existing port access state and access status.
	 *
	 * Also note that the failure to write out the ALUA metadata to
	 * struct file does NOT affect the actual ALUA transition.
	 */
	if (tg_pt_gp->tg_pt_gp_write_metadata) {
		core_alua_update_tpg_primary_metadata(tg_pt_gp);
	}

	pr_debug("Successful %s ALUA transition TG PT Group: %s ID: %hu"
		" from primary access state %s to %s\n", (explicit) ? "explicit" :
		"implicit", config_item_name(&tg_pt_gp->tg_pt_gp_group.cg_item),
		tg_pt_gp->tg_pt_gp_id,
		core_alua_dump_state(prev_state),
		core_alua_dump_state(new_state));

	core_alua_queue_state_change_ua(tg_pt_gp);

	mutex_unlock(&tg_pt_gp->tg_pt_gp_transition_mutex);
	return 0;
}

int core_alua_do_port_transition(
	struct t10_alua_tg_pt_gp *l_tg_pt_gp,
	struct se_device *l_dev,
	struct se_lun *l_lun,
	struct se_node_acl *l_nacl,
	int new_state,
	int explicit)
{
	struct se_device *dev;
	struct t10_alua_lu_gp *lu_gp;
	struct t10_alua_lu_gp_member *lu_gp_mem, *local_lu_gp_mem;
	struct t10_alua_tg_pt_gp *tg_pt_gp;
	int primary, valid_states, rc = 0;

	if (l_dev->transport->transport_flags & TRANSPORT_FLAG_PASSTHROUGH_ALUA)
		return -ENODEV;

	valid_states = l_tg_pt_gp->tg_pt_gp_alua_supported_states;
	if (core_alua_check_transition(new_state, valid_states, &primary,
				       explicit) != 0)
		return -EINVAL;

	local_lu_gp_mem = l_dev->dev_alua_lu_gp_mem;
	spin_lock(&local_lu_gp_mem->lu_gp_mem_lock);
	lu_gp = local_lu_gp_mem->lu_gp;
	atomic_inc(&lu_gp->lu_gp_ref_cnt);
	spin_unlock(&local_lu_gp_mem->lu_gp_mem_lock);
	/*
	 * For storage objects that are members of the 'default_lu_gp',
	 * we only do transition on the passed *l_tp_pt_gp, and not
	 * on all of the matching target port groups IDs in default_lu_gp.
	 */
	if (!lu_gp->lu_gp_id) {
		/*
		 * core_alua_do_transition_tg_pt() will always return
		 * success.
		 */
		l_tg_pt_gp->tg_pt_gp_alua_lun = l_lun;
		l_tg_pt_gp->tg_pt_gp_alua_nacl = l_nacl;
		rc = core_alua_do_transition_tg_pt(l_tg_pt_gp,
						   new_state, explicit);
		atomic_dec_mb(&lu_gp->lu_gp_ref_cnt);
		return rc;
	}
	/*
	 * For all other LU groups aside from 'default_lu_gp', walk all of
	 * the associated storage objects looking for a matching target port
	 * group ID from the local target port group.
	 */
	spin_lock(&lu_gp->lu_gp_lock);
	list_for_each_entry(lu_gp_mem, &lu_gp->lu_gp_mem_list,
				lu_gp_mem_list) {

		dev = lu_gp_mem->lu_gp_mem_dev;
		atomic_inc_mb(&lu_gp_mem->lu_gp_mem_ref_cnt);
		spin_unlock(&lu_gp->lu_gp_lock);

		spin_lock(&dev->t10_alua.tg_pt_gps_lock);
		list_for_each_entry(tg_pt_gp,
				&dev->t10_alua.tg_pt_gps_list,
				tg_pt_gp_list) {

			if (!tg_pt_gp->tg_pt_gp_valid_id)
				continue;
			/*
			 * If the target behavior port asymmetric access state
			 * is changed for any target port group accessible via
			 * a logical unit within a LU group, the target port
			 * behavior group asymmetric access states for the same
			 * target port group accessible via other logical units
			 * in that LU group will also change.
			 */
			if (l_tg_pt_gp->tg_pt_gp_id != tg_pt_gp->tg_pt_gp_id)
				continue;

			if (l_tg_pt_gp == tg_pt_gp) {
				tg_pt_gp->tg_pt_gp_alua_lun = l_lun;
				tg_pt_gp->tg_pt_gp_alua_nacl = l_nacl;
			} else {
				tg_pt_gp->tg_pt_gp_alua_lun = NULL;
				tg_pt_gp->tg_pt_gp_alua_nacl = NULL;
			}
			atomic_inc_mb(&tg_pt_gp->tg_pt_gp_ref_cnt);
			spin_unlock(&dev->t10_alua.tg_pt_gps_lock);
			/*
			 * core_alua_do_transition_tg_pt() will always return
			 * success.
			 */
			rc = core_alua_do_transition_tg_pt(tg_pt_gp,
					new_state, explicit);

			spin_lock(&dev->t10_alua.tg_pt_gps_lock);
			atomic_dec_mb(&tg_pt_gp->tg_pt_gp_ref_cnt);
			if (rc)
				break;
		}
		spin_unlock(&dev->t10_alua.tg_pt_gps_lock);

		spin_lock(&lu_gp->lu_gp_lock);
		atomic_dec_mb(&lu_gp_mem->lu_gp_mem_ref_cnt);
	}
	spin_unlock(&lu_gp->lu_gp_lock);

	if (!rc) {
		pr_debug("Successfully processed LU Group: %s all ALUA TG PT"
			 " Group IDs: %hu %s transition to primary state: %s\n",
			 config_item_name(&lu_gp->lu_gp_group.cg_item),
			 l_tg_pt_gp->tg_pt_gp_id,
			 (explicit) ? "explicit" : "implicit",
			 core_alua_dump_state(new_state));
	}

	atomic_dec_mb(&lu_gp->lu_gp_ref_cnt);
	return rc;
}

static int core_alua_update_tpg_secondary_metadata(struct se_lun *lun)
{
	struct se_portal_group *se_tpg = lun->lun_tpg;
	unsigned char *md_buf;
	char *path;
	int len, rc;

	mutex_lock(&lun->lun_tg_pt_md_mutex);

	md_buf = kzalloc(ALUA_MD_BUF_LEN, GFP_KERNEL);
	if (!md_buf) {
		pr_err("Unable to allocate buf for ALUA metadata\n");
		rc = -ENOMEM;
		goto out_unlock;
	}

	len = snprintf(md_buf, ALUA_MD_BUF_LEN, "alua_tg_pt_offline=%d\n"
			"alua_tg_pt_status=0x%02x\n",
			atomic_read(&lun->lun_tg_pt_secondary_offline),
			lun->lun_tg_pt_secondary_stat);

	if (se_tpg->se_tpg_tfo->tpg_get_tag != NULL) {
		path = kasprintf(GFP_KERNEL, "%s/alua/%s/%s+%hu/lun_%llu",
				db_root, se_tpg->se_tpg_tfo->fabric_name,
				se_tpg->se_tpg_tfo->tpg_get_wwn(se_tpg),
				se_tpg->se_tpg_tfo->tpg_get_tag(se_tpg),
				lun->unpacked_lun);
	} else {
		path = kasprintf(GFP_KERNEL, "%s/alua/%s/%s/lun_%llu",
				db_root, se_tpg->se_tpg_tfo->fabric_name,
				se_tpg->se_tpg_tfo->tpg_get_wwn(se_tpg),
				lun->unpacked_lun);
	}
	if (!path) {
		rc = -ENOMEM;
		goto out_free;
	}

	rc = core_alua_write_tpg_metadata(path, md_buf, len);
	kfree(path);
out_free:
	kfree(md_buf);
out_unlock:
	mutex_unlock(&lun->lun_tg_pt_md_mutex);
	return rc;
}

static int core_alua_set_tg_pt_secondary_state(
	struct se_lun *lun,
	int explicit,
	int offline)
{
	struct t10_alua_tg_pt_gp *tg_pt_gp;
	int trans_delay_msecs;

	spin_lock(&lun->lun_tg_pt_gp_lock);
	tg_pt_gp = lun->lun_tg_pt_gp;
	if (!tg_pt_gp) {
		spin_unlock(&lun->lun_tg_pt_gp_lock);
		pr_err("Unable to complete secondary state"
				" transition\n");
		return -EINVAL;
	}
	trans_delay_msecs = tg_pt_gp->tg_pt_gp_trans_delay_msecs;
	/*
	 * Set the secondary ALUA target port access state to OFFLINE
	 * or release the previously secondary state for struct se_lun
	 */
	if (offline)
		atomic_set(&lun->lun_tg_pt_secondary_offline, 1);
	else
		atomic_set(&lun->lun_tg_pt_secondary_offline, 0);

	lun->lun_tg_pt_secondary_stat = (explicit) ?
			ALUA_STATUS_ALTERED_BY_EXPLICIT_STPG :
			ALUA_STATUS_ALTERED_BY_IMPLICIT_ALUA;

	pr_debug("Successful %s ALUA transition TG PT Group: %s ID: %hu"
		" to secondary access state: %s\n", (explicit) ? "explicit" :
		"implicit", config_item_name(&tg_pt_gp->tg_pt_gp_group.cg_item),
		tg_pt_gp->tg_pt_gp_id, (offline) ? "OFFLINE" : "ONLINE");

	spin_unlock(&lun->lun_tg_pt_gp_lock);
	/*
	 * Do the optional transition delay after we set the secondary
	 * ALUA access state.
	 */
	if (trans_delay_msecs != 0)
		msleep_interruptible(trans_delay_msecs);
	/*
	 * See if we need to update the ALUA fabric port metadata for
	 * secondary state and status
	 */
	if (lun->lun_tg_pt_secondary_write_md)
		core_alua_update_tpg_secondary_metadata(lun);

	return 0;
}

struct t10_alua_lba_map *
core_alua_allocate_lba_map(struct list_head *list,
			   u64 first_lba, u64 last_lba)
{
	struct t10_alua_lba_map *lba_map;

	lba_map = kmem_cache_zalloc(t10_alua_lba_map_cache, GFP_KERNEL);
	if (!lba_map) {
		pr_err("Unable to allocate struct t10_alua_lba_map\n");
		return ERR_PTR(-ENOMEM);
	}
	INIT_LIST_HEAD(&lba_map->lba_map_mem_list);
	lba_map->lba_map_first_lba = first_lba;
	lba_map->lba_map_last_lba = last_lba;

	list_add_tail(&lba_map->lba_map_list, list);
	return lba_map;
}

int
core_alua_allocate_lba_map_mem(struct t10_alua_lba_map *lba_map,
			       int pg_id, int state)
{
	struct t10_alua_lba_map_member *lba_map_mem;

	list_for_each_entry(lba_map_mem, &lba_map->lba_map_mem_list,
			    lba_map_mem_list) {
		if (lba_map_mem->lba_map_mem_alua_pg_id == pg_id) {
			pr_err("Duplicate pg_id %d in lba_map\n", pg_id);
			return -EINVAL;
		}
	}

	lba_map_mem = kmem_cache_zalloc(t10_alua_lba_map_mem_cache, GFP_KERNEL);
	if (!lba_map_mem) {
		pr_err("Unable to allocate struct t10_alua_lba_map_mem\n");
		return -ENOMEM;
	}
	lba_map_mem->lba_map_mem_alua_state = state;
	lba_map_mem->lba_map_mem_alua_pg_id = pg_id;

	list_add_tail(&lba_map_mem->lba_map_mem_list,
		      &lba_map->lba_map_mem_list);
	return 0;
}

void
core_alua_free_lba_map(struct list_head *lba_list)
{
	struct t10_alua_lba_map *lba_map, *lba_map_tmp;
	struct t10_alua_lba_map_member *lba_map_mem, *lba_map_mem_tmp;

	list_for_each_entry_safe(lba_map, lba_map_tmp, lba_list,
				 lba_map_list) {
		list_for_each_entry_safe(lba_map_mem, lba_map_mem_tmp,
					 &lba_map->lba_map_mem_list,
					 lba_map_mem_list) {
			list_del(&lba_map_mem->lba_map_mem_list);
			kmem_cache_free(t10_alua_lba_map_mem_cache,
					lba_map_mem);
		}
		list_del(&lba_map->lba_map_list);
		kmem_cache_free(t10_alua_lba_map_cache, lba_map);
	}
}

void
core_alua_set_lba_map(struct se_device *dev, struct list_head *lba_map_list,
		      int segment_size, int segment_mult)
{
	struct list_head old_lba_map_list;
	struct t10_alua_tg_pt_gp *tg_pt_gp;
	int activate = 0, supported;

	INIT_LIST_HEAD(&old_lba_map_list);
	spin_lock(&dev->t10_alua.lba_map_lock);
	dev->t10_alua.lba_map_segment_size = segment_size;
	dev->t10_alua.lba_map_segment_multiplier = segment_mult;
	list_splice_init(&dev->t10_alua.lba_map_list, &old_lba_map_list);
	if (lba_map_list) {
		list_splice_init(lba_map_list, &dev->t10_alua.lba_map_list);
		activate = 1;
	}
	spin_unlock(&dev->t10_alua.lba_map_lock);
	spin_lock(&dev->t10_alua.tg_pt_gps_lock);
	list_for_each_entry(tg_pt_gp, &dev->t10_alua.tg_pt_gps_list,
			    tg_pt_gp_list) {

		if (!tg_pt_gp->tg_pt_gp_valid_id)
			continue;
		supported = tg_pt_gp->tg_pt_gp_alua_supported_states;
		if (activate)
			supported |= ALUA_LBD_SUP;
		else
			supported &= ~ALUA_LBD_SUP;
		tg_pt_gp->tg_pt_gp_alua_supported_states = supported;
	}
	spin_unlock(&dev->t10_alua.tg_pt_gps_lock);
	core_alua_free_lba_map(&old_lba_map_list);
}

struct t10_alua_lu_gp *
core_alua_allocate_lu_gp(const char *name, int def_group)
{
	struct t10_alua_lu_gp *lu_gp;

	lu_gp = kmem_cache_zalloc(t10_alua_lu_gp_cache, GFP_KERNEL);
	if (!lu_gp) {
		pr_err("Unable to allocate struct t10_alua_lu_gp\n");
		return ERR_PTR(-ENOMEM);
	}
	INIT_LIST_HEAD(&lu_gp->lu_gp_node);
	INIT_LIST_HEAD(&lu_gp->lu_gp_mem_list);
	spin_lock_init(&lu_gp->lu_gp_lock);
	atomic_set(&lu_gp->lu_gp_ref_cnt, 0);

	if (def_group) {
		lu_gp->lu_gp_id = alua_lu_gps_counter++;
		lu_gp->lu_gp_valid_id = 1;
		alua_lu_gps_count++;
	}

	return lu_gp;
}

int core_alua_set_lu_gp_id(struct t10_alua_lu_gp *lu_gp, u16 lu_gp_id)
{
	struct t10_alua_lu_gp *lu_gp_tmp;
	u16 lu_gp_id_tmp;
	/*
	 * The lu_gp->lu_gp_id may only be set once..
	 */
	if (lu_gp->lu_gp_valid_id) {
		pr_warn("ALUA LU Group already has a valid ID,"
			" ignoring request\n");
		return -EINVAL;
	}

	spin_lock(&lu_gps_lock);
	if (alua_lu_gps_count == 0x0000ffff) {
		pr_err("Maximum ALUA alua_lu_gps_count:"
				" 0x0000ffff reached\n");
		spin_unlock(&lu_gps_lock);
		kmem_cache_free(t10_alua_lu_gp_cache, lu_gp);
		return -ENOSPC;
	}
again:
	lu_gp_id_tmp = (lu_gp_id != 0) ? lu_gp_id :
				alua_lu_gps_counter++;

	list_for_each_entry(lu_gp_tmp, &lu_gps_list, lu_gp_node) {
		if (lu_gp_tmp->lu_gp_id == lu_gp_id_tmp) {
			if (!lu_gp_id)
				goto again;

			pr_warn("ALUA Logical Unit Group ID: %hu"
				" already exists, ignoring request\n",
				lu_gp_id);
			spin_unlock(&lu_gps_lock);
			return -EINVAL;
		}
	}

	lu_gp->lu_gp_id = lu_gp_id_tmp;
	lu_gp->lu_gp_valid_id = 1;
	list_add_tail(&lu_gp->lu_gp_node, &lu_gps_list);
	alua_lu_gps_count++;
	spin_unlock(&lu_gps_lock);

	return 0;
}

static struct t10_alua_lu_gp_member *
core_alua_allocate_lu_gp_mem(struct se_device *dev)
{
	struct t10_alua_lu_gp_member *lu_gp_mem;

	lu_gp_mem = kmem_cache_zalloc(t10_alua_lu_gp_mem_cache, GFP_KERNEL);
	if (!lu_gp_mem) {
		pr_err("Unable to allocate struct t10_alua_lu_gp_member\n");
		return ERR_PTR(-ENOMEM);
	}
	INIT_LIST_HEAD(&lu_gp_mem->lu_gp_mem_list);
	spin_lock_init(&lu_gp_mem->lu_gp_mem_lock);
	atomic_set(&lu_gp_mem->lu_gp_mem_ref_cnt, 0);

	lu_gp_mem->lu_gp_mem_dev = dev;
	dev->dev_alua_lu_gp_mem = lu_gp_mem;

	return lu_gp_mem;
}

void core_alua_free_lu_gp(struct t10_alua_lu_gp *lu_gp)
{
	struct t10_alua_lu_gp_member *lu_gp_mem, *lu_gp_mem_tmp;
	/*
	 * Once we have reached this point, config_item_put() has
	 * already been called from target_core_alua_drop_lu_gp().
	 *
	 * Here, we remove the *lu_gp from the global list so that
	 * no associations can be made while we are releasing
	 * struct t10_alua_lu_gp.
	 */
	spin_lock(&lu_gps_lock);
	list_del(&lu_gp->lu_gp_node);
	alua_lu_gps_count--;
	spin_unlock(&lu_gps_lock);
	/*
	 * Allow struct t10_alua_lu_gp * referenced by core_alua_get_lu_gp_by_name()
	 * in target_core_configfs.c:target_core_store_alua_lu_gp() to be
	 * released with core_alua_put_lu_gp_from_name()
	 */
	while (atomic_read(&lu_gp->lu_gp_ref_cnt))
		cpu_relax();
	/*
	 * Release reference to struct t10_alua_lu_gp * from all associated
	 * struct se_device.
	 */
	spin_lock(&lu_gp->lu_gp_lock);
	list_for_each_entry_safe(lu_gp_mem, lu_gp_mem_tmp,
				&lu_gp->lu_gp_mem_list, lu_gp_mem_list) {
		if (lu_gp_mem->lu_gp_assoc) {
			list_del(&lu_gp_mem->lu_gp_mem_list);
			lu_gp->lu_gp_members--;
			lu_gp_mem->lu_gp_assoc = 0;
		}
		spin_unlock(&lu_gp->lu_gp_lock);
		/*
		 *
		 * lu_gp_mem is associated with a single
		 * struct se_device->dev_alua_lu_gp_mem, and is released when
		 * struct se_device is released via core_alua_free_lu_gp_mem().
		 *
		 * If the passed lu_gp does NOT match the default_lu_gp, assume
		 * we want to re-associate a given lu_gp_mem with default_lu_gp.
		 */
		spin_lock(&lu_gp_mem->lu_gp_mem_lock);
		if (lu_gp != default_lu_gp)
			__core_alua_attach_lu_gp_mem(lu_gp_mem,
					default_lu_gp);
		else
			lu_gp_mem->lu_gp = NULL;
		spin_unlock(&lu_gp_mem->lu_gp_mem_lock);

		spin_lock(&lu_gp->lu_gp_lock);
	}
	spin_unlock(&lu_gp->lu_gp_lock);

	kmem_cache_free(t10_alua_lu_gp_cache, lu_gp);
}

void core_alua_free_lu_gp_mem(struct se_device *dev)
{
	struct t10_alua_lu_gp *lu_gp;
	struct t10_alua_lu_gp_member *lu_gp_mem;

	lu_gp_mem = dev->dev_alua_lu_gp_mem;
	if (!lu_gp_mem)
		return;

	while (atomic_read(&lu_gp_mem->lu_gp_mem_ref_cnt))
		cpu_relax();

	spin_lock(&lu_gp_mem->lu_gp_mem_lock);
	lu_gp = lu_gp_mem->lu_gp;
	if (lu_gp) {
		spin_lock(&lu_gp->lu_gp_lock);
		if (lu_gp_mem->lu_gp_assoc) {
			list_del(&lu_gp_mem->lu_gp_mem_list);
			lu_gp->lu_gp_members--;
			lu_gp_mem->lu_gp_assoc = 0;
		}
		spin_unlock(&lu_gp->lu_gp_lock);
		lu_gp_mem->lu_gp = NULL;
	}
	spin_unlock(&lu_gp_mem->lu_gp_mem_lock);

	kmem_cache_free(t10_alua_lu_gp_mem_cache, lu_gp_mem);
}

struct t10_alua_lu_gp *core_alua_get_lu_gp_by_name(const char *name)
{
	struct t10_alua_lu_gp *lu_gp;
	struct config_item *ci;

	spin_lock(&lu_gps_lock);
	list_for_each_entry(lu_gp, &lu_gps_list, lu_gp_node) {
		if (!lu_gp->lu_gp_valid_id)
			continue;
		ci = &lu_gp->lu_gp_group.cg_item;
		if (!strcmp(config_item_name(ci), name)) {
			atomic_inc(&lu_gp->lu_gp_ref_cnt);
			spin_unlock(&lu_gps_lock);
			return lu_gp;
		}
	}
	spin_unlock(&lu_gps_lock);

	return NULL;
}

void core_alua_put_lu_gp_from_name(struct t10_alua_lu_gp *lu_gp)
{
	spin_lock(&lu_gps_lock);
	atomic_dec(&lu_gp->lu_gp_ref_cnt);
	spin_unlock(&lu_gps_lock);
}

/*
 * Called with struct t10_alua_lu_gp_member->lu_gp_mem_lock
 */
void __core_alua_attach_lu_gp_mem(
	struct t10_alua_lu_gp_member *lu_gp_mem,
	struct t10_alua_lu_gp *lu_gp)
{
	spin_lock(&lu_gp->lu_gp_lock);
	lu_gp_mem->lu_gp = lu_gp;
	lu_gp_mem->lu_gp_assoc = 1;
	list_add_tail(&lu_gp_mem->lu_gp_mem_list, &lu_gp->lu_gp_mem_list);
	lu_gp->lu_gp_members++;
	spin_unlock(&lu_gp->lu_gp_lock);
}

/*
 * Called with struct t10_alua_lu_gp_member->lu_gp_mem_lock
 */
void __core_alua_drop_lu_gp_mem(
	struct t10_alua_lu_gp_member *lu_gp_mem,
	struct t10_alua_lu_gp *lu_gp)
{
	spin_lock(&lu_gp->lu_gp_lock);
	list_del(&lu_gp_mem->lu_gp_mem_list);
	lu_gp_mem->lu_gp = NULL;
	lu_gp_mem->lu_gp_assoc = 0;
	lu_gp->lu_gp_members--;
	spin_unlock(&lu_gp->lu_gp_lock);
}

struct t10_alua_tg_pt_gp *core_alua_allocate_tg_pt_gp(struct se_device *dev,
		const char *name, int def_group)
{
	struct t10_alua_tg_pt_gp *tg_pt_gp;

	tg_pt_gp = kmem_cache_zalloc(t10_alua_tg_pt_gp_cache, GFP_KERNEL);
	if (!tg_pt_gp) {
		pr_err("Unable to allocate struct t10_alua_tg_pt_gp\n");
		return NULL;
	}
	INIT_LIST_HEAD(&tg_pt_gp->tg_pt_gp_list);
	INIT_LIST_HEAD(&tg_pt_gp->tg_pt_gp_lun_list);
	mutex_init(&tg_pt_gp->tg_pt_gp_transition_mutex);
	spin_lock_init(&tg_pt_gp->tg_pt_gp_lock);
	atomic_set(&tg_pt_gp->tg_pt_gp_ref_cnt, 0);
	tg_pt_gp->tg_pt_gp_dev = dev;
	tg_pt_gp->tg_pt_gp_alua_access_state =
			ALUA_ACCESS_STATE_ACTIVE_OPTIMIZED;
	/*
	 * Enable both explicit and implicit ALUA support by default
	 */
	tg_pt_gp->tg_pt_gp_alua_access_type =
			TPGS_EXPLICIT_ALUA | TPGS_IMPLICIT_ALUA;
	/*
	 * Set the default Active/NonOptimized Delay in milliseconds
	 */
	tg_pt_gp->tg_pt_gp_nonop_delay_msecs = ALUA_DEFAULT_NONOP_DELAY_MSECS;
	tg_pt_gp->tg_pt_gp_trans_delay_msecs = ALUA_DEFAULT_TRANS_DELAY_MSECS;
	tg_pt_gp->tg_pt_gp_implicit_trans_secs = ALUA_DEFAULT_IMPLICIT_TRANS_SECS;

	/*
	 * Enable all supported states
	 */
	tg_pt_gp->tg_pt_gp_alua_supported_states =
	    ALUA_T_SUP | ALUA_O_SUP |
	    ALUA_U_SUP | ALUA_S_SUP | ALUA_AN_SUP | ALUA_AO_SUP;

	if (def_group) {
		spin_lock(&dev->t10_alua.tg_pt_gps_lock);
		tg_pt_gp->tg_pt_gp_id =
				dev->t10_alua.alua_tg_pt_gps_counter++;
		tg_pt_gp->tg_pt_gp_valid_id = 1;
		dev->t10_alua.alua_tg_pt_gps_count++;
		list_add_tail(&tg_pt_gp->tg_pt_gp_list,
			      &dev->t10_alua.tg_pt_gps_list);
		spin_unlock(&dev->t10_alua.tg_pt_gps_lock);
	}

	return tg_pt_gp;
}

int core_alua_set_tg_pt_gp_id(
	struct t10_alua_tg_pt_gp *tg_pt_gp,
	u16 tg_pt_gp_id)
{
	struct se_device *dev = tg_pt_gp->tg_pt_gp_dev;
	struct t10_alua_tg_pt_gp *tg_pt_gp_tmp;
	u16 tg_pt_gp_id_tmp;

	/*
	 * The tg_pt_gp->tg_pt_gp_id may only be set once..
	 */
	if (tg_pt_gp->tg_pt_gp_valid_id) {
		pr_warn("ALUA TG PT Group already has a valid ID,"
			" ignoring request\n");
		return -EINVAL;
	}

	spin_lock(&dev->t10_alua.tg_pt_gps_lock);
	if (dev->t10_alua.alua_tg_pt_gps_count == 0x0000ffff) {
		pr_err("Maximum ALUA alua_tg_pt_gps_count:"
			" 0x0000ffff reached\n");
		spin_unlock(&dev->t10_alua.tg_pt_gps_lock);
		kmem_cache_free(t10_alua_tg_pt_gp_cache, tg_pt_gp);
		return -ENOSPC;
	}
again:
	tg_pt_gp_id_tmp = (tg_pt_gp_id != 0) ? tg_pt_gp_id :
			dev->t10_alua.alua_tg_pt_gps_counter++;

	list_for_each_entry(tg_pt_gp_tmp, &dev->t10_alua.tg_pt_gps_list,
			tg_pt_gp_list) {
		if (tg_pt_gp_tmp->tg_pt_gp_id == tg_pt_gp_id_tmp) {
			if (!tg_pt_gp_id)
				goto again;

			pr_err("ALUA Target Port Group ID: %hu already"
				" exists, ignoring request\n", tg_pt_gp_id);
			spin_unlock(&dev->t10_alua.tg_pt_gps_lock);
			return -EINVAL;
		}
	}

	tg_pt_gp->tg_pt_gp_id = tg_pt_gp_id_tmp;
	tg_pt_gp->tg_pt_gp_valid_id = 1;
	list_add_tail(&tg_pt_gp->tg_pt_gp_list,
			&dev->t10_alua.tg_pt_gps_list);
	dev->t10_alua.alua_tg_pt_gps_count++;
	spin_unlock(&dev->t10_alua.tg_pt_gps_lock);

	return 0;
}

void core_alua_free_tg_pt_gp(
	struct t10_alua_tg_pt_gp *tg_pt_gp)
{
	struct se_device *dev = tg_pt_gp->tg_pt_gp_dev;
	struct se_lun *lun, *next;

	/*
	 * Once we have reached this point, config_item_put() has already
	 * been called from target_core_alua_drop_tg_pt_gp().
	 *
	 * Here we remove *tg_pt_gp from the global list so that
	 * no associations *OR* explicit ALUA via SET_TARGET_PORT_GROUPS
	 * can be made while we are releasing struct t10_alua_tg_pt_gp.
	 */
	spin_lock(&dev->t10_alua.tg_pt_gps_lock);
	list_del(&tg_pt_gp->tg_pt_gp_list);
	dev->t10_alua.alua_tg_pt_gps_counter--;
	spin_unlock(&dev->t10_alua.tg_pt_gps_lock);

	/*
	 * Allow a struct t10_alua_tg_pt_gp_member * referenced by
	 * core_alua_get_tg_pt_gp_by_name() in
	 * target_core_configfs.c:target_core_store_alua_tg_pt_gp()
	 * to be released with core_alua_put_tg_pt_gp_from_name().
	 */
	while (atomic_read(&tg_pt_gp->tg_pt_gp_ref_cnt))
		cpu_relax();

	/*
	 * Release reference to struct t10_alua_tg_pt_gp from all associated
	 * struct se_port.
	 */
	spin_lock(&tg_pt_gp->tg_pt_gp_lock);
	list_for_each_entry_safe(lun, next,
			&tg_pt_gp->tg_pt_gp_lun_list, lun_tg_pt_gp_link) {
		list_del_init(&lun->lun_tg_pt_gp_link);
		tg_pt_gp->tg_pt_gp_members--;

		spin_unlock(&tg_pt_gp->tg_pt_gp_lock);
		/*
		 * If the passed tg_pt_gp does NOT match the default_tg_pt_gp,
		 * assume we want to re-associate a given tg_pt_gp_mem with
		 * default_tg_pt_gp.
		 */
		spin_lock(&lun->lun_tg_pt_gp_lock);
		if (tg_pt_gp != dev->t10_alua.default_tg_pt_gp) {
			__target_attach_tg_pt_gp(lun,
					dev->t10_alua.default_tg_pt_gp);
		} else
			lun->lun_tg_pt_gp = NULL;
		spin_unlock(&lun->lun_tg_pt_gp_lock);

		spin_lock(&tg_pt_gp->tg_pt_gp_lock);
	}
	spin_unlock(&tg_pt_gp->tg_pt_gp_lock);

	kmem_cache_free(t10_alua_tg_pt_gp_cache, tg_pt_gp);
}

static struct t10_alua_tg_pt_gp *core_alua_get_tg_pt_gp_by_name(
		struct se_device *dev, const char *name)
{
	struct t10_alua_tg_pt_gp *tg_pt_gp;
	struct config_item *ci;

	spin_lock(&dev->t10_alua.tg_pt_gps_lock);
	list_for_each_entry(tg_pt_gp, &dev->t10_alua.tg_pt_gps_list,
			tg_pt_gp_list) {
		if (!tg_pt_gp->tg_pt_gp_valid_id)
			continue;
		ci = &tg_pt_gp->tg_pt_gp_group.cg_item;
		if (!strcmp(config_item_name(ci), name)) {
			atomic_inc(&tg_pt_gp->tg_pt_gp_ref_cnt);
			spin_unlock(&dev->t10_alua.tg_pt_gps_lock);
			return tg_pt_gp;
		}
	}
	spin_unlock(&dev->t10_alua.tg_pt_gps_lock);

	return NULL;
}

static void core_alua_put_tg_pt_gp_from_name(
	struct t10_alua_tg_pt_gp *tg_pt_gp)
{
	struct se_device *dev = tg_pt_gp->tg_pt_gp_dev;

	spin_lock(&dev->t10_alua.tg_pt_gps_lock);
	atomic_dec(&tg_pt_gp->tg_pt_gp_ref_cnt);
	spin_unlock(&dev->t10_alua.tg_pt_gps_lock);
}

static void __target_attach_tg_pt_gp(struct se_lun *lun,
		struct t10_alua_tg_pt_gp *tg_pt_gp)
{
	struct se_dev_entry *se_deve;

	assert_spin_locked(&lun->lun_tg_pt_gp_lock);

	spin_lock(&tg_pt_gp->tg_pt_gp_lock);
	lun->lun_tg_pt_gp = tg_pt_gp;
	list_add_tail(&lun->lun_tg_pt_gp_link, &tg_pt_gp->tg_pt_gp_lun_list);
	tg_pt_gp->tg_pt_gp_members++;
	spin_lock(&lun->lun_deve_lock);
	list_for_each_entry(se_deve, &lun->lun_deve_list, lun_link)
		core_scsi3_ua_allocate(se_deve, 0x3f,
				       ASCQ_3FH_INQUIRY_DATA_HAS_CHANGED);
	spin_unlock(&lun->lun_deve_lock);
	spin_unlock(&tg_pt_gp->tg_pt_gp_lock);
}

void target_attach_tg_pt_gp(struct se_lun *lun,
		struct t10_alua_tg_pt_gp *tg_pt_gp)
{
	spin_lock(&lun->lun_tg_pt_gp_lock);
	__target_attach_tg_pt_gp(lun, tg_pt_gp);
	spin_unlock(&lun->lun_tg_pt_gp_lock);
}

static void __target_detach_tg_pt_gp(struct se_lun *lun,
		struct t10_alua_tg_pt_gp *tg_pt_gp)
{
	assert_spin_locked(&lun->lun_tg_pt_gp_lock);

	spin_lock(&tg_pt_gp->tg_pt_gp_lock);
	list_del_init(&lun->lun_tg_pt_gp_link);
	tg_pt_gp->tg_pt_gp_members--;
	spin_unlock(&tg_pt_gp->tg_pt_gp_lock);

	lun->lun_tg_pt_gp = NULL;
}

void target_detach_tg_pt_gp(struct se_lun *lun)
{
	struct t10_alua_tg_pt_gp *tg_pt_gp;

	spin_lock(&lun->lun_tg_pt_gp_lock);
	tg_pt_gp = lun->lun_tg_pt_gp;
	if (tg_pt_gp)
		__target_detach_tg_pt_gp(lun, tg_pt_gp);
	spin_unlock(&lun->lun_tg_pt_gp_lock);
}

ssize_t core_alua_show_tg_pt_gp_info(struct se_lun *lun, char *page)
{
	struct config_item *tg_pt_ci;
	struct t10_alua_tg_pt_gp *tg_pt_gp;
	ssize_t len = 0;

	spin_lock(&lun->lun_tg_pt_gp_lock);
	tg_pt_gp = lun->lun_tg_pt_gp;
	if (tg_pt_gp) {
		tg_pt_ci = &tg_pt_gp->tg_pt_gp_group.cg_item;
		len += sprintf(page, "TG Port Alias: %s\nTG Port Group ID:"
			" %hu\nTG Port Primary Access State: %s\nTG Port "
			"Primary Access Status: %s\nTG Port Secondary Access"
			" State: %s\nTG Port Secondary Access Status: %s\n",
			config_item_name(tg_pt_ci), tg_pt_gp->tg_pt_gp_id,
			core_alua_dump_state(
				tg_pt_gp->tg_pt_gp_alua_access_state),
			core_alua_dump_status(
				tg_pt_gp->tg_pt_gp_alua_access_status),
			atomic_read(&lun->lun_tg_pt_secondary_offline) ?
			"Offline" : "None",
			core_alua_dump_status(lun->lun_tg_pt_secondary_stat));
	}
	spin_unlock(&lun->lun_tg_pt_gp_lock);

	return len;
}

ssize_t core_alua_store_tg_pt_gp_info(
	struct se_lun *lun,
	const char *page,
	size_t count)
{
	struct se_portal_group *tpg = lun->lun_tpg;
	/*
	 * rcu_dereference_raw protected by se_lun->lun_group symlink
	 * reference to se_device->dev_group.
	 */
	struct se_device *dev = rcu_dereference_raw(lun->lun_se_dev);
	struct t10_alua_tg_pt_gp *tg_pt_gp = NULL, *tg_pt_gp_new = NULL;
	unsigned char buf[TG_PT_GROUP_NAME_BUF];
	int move = 0;

	if (dev->transport->transport_flags & TRANSPORT_FLAG_PASSTHROUGH_ALUA ||
	    (dev->se_hba->hba_flags & HBA_FLAGS_INTERNAL_USE))
		return -ENODEV;

	if (count > TG_PT_GROUP_NAME_BUF) {
		pr_err("ALUA Target Port Group alias too large!\n");
		return -EINVAL;
	}
	memset(buf, 0, TG_PT_GROUP_NAME_BUF);
	memcpy(buf, page, count);
	/*
	 * Any ALUA target port group alias besides "NULL" means we will be
	 * making a new group association.
	 */
	if (strcmp(strstrip(buf), "NULL")) {
		/*
		 * core_alua_get_tg_pt_gp_by_name() will increment reference to
		 * struct t10_alua_tg_pt_gp.  This reference is released with
		 * core_alua_put_tg_pt_gp_from_name() below.
		 */
		tg_pt_gp_new = core_alua_get_tg_pt_gp_by_name(dev,
					strstrip(buf));
		if (!tg_pt_gp_new)
			return -ENODEV;
	}

	spin_lock(&lun->lun_tg_pt_gp_lock);
	tg_pt_gp = lun->lun_tg_pt_gp;
	if (tg_pt_gp) {
		/*
		 * Clearing an existing tg_pt_gp association, and replacing
		 * with the default_tg_pt_gp.
		 */
		if (!tg_pt_gp_new) {
			pr_debug("Target_Core_ConfigFS: Moving"
				" %s/tpgt_%hu/%s from ALUA Target Port Group:"
				" alua/%s, ID: %hu back to"
				" default_tg_pt_gp\n",
				tpg->se_tpg_tfo->tpg_get_wwn(tpg),
				tpg->se_tpg_tfo->tpg_get_tag(tpg),
				config_item_name(&lun->lun_group.cg_item),
				config_item_name(
					&tg_pt_gp->tg_pt_gp_group.cg_item),
				tg_pt_gp->tg_pt_gp_id);

			__target_detach_tg_pt_gp(lun, tg_pt_gp);
			__target_attach_tg_pt_gp(lun,
					dev->t10_alua.default_tg_pt_gp);
			spin_unlock(&lun->lun_tg_pt_gp_lock);

			return count;
		}
		__target_detach_tg_pt_gp(lun, tg_pt_gp);
		move = 1;
	}

	__target_attach_tg_pt_gp(lun, tg_pt_gp_new);
	spin_unlock(&lun->lun_tg_pt_gp_lock);
	pr_debug("Target_Core_ConfigFS: %s %s/tpgt_%hu/%s to ALUA"
		" Target Port Group: alua/%s, ID: %hu\n", (move) ?
		"Moving" : "Adding", tpg->se_tpg_tfo->tpg_get_wwn(tpg),
		tpg->se_tpg_tfo->tpg_get_tag(tpg),
		config_item_name(&lun->lun_group.cg_item),
		config_item_name(&tg_pt_gp_new->tg_pt_gp_group.cg_item),
		tg_pt_gp_new->tg_pt_gp_id);

	core_alua_put_tg_pt_gp_from_name(tg_pt_gp_new);
	return count;
}

ssize_t core_alua_show_access_type(
	struct t10_alua_tg_pt_gp *tg_pt_gp,
	char *page)
{
	if ((tg_pt_gp->tg_pt_gp_alua_access_type & TPGS_EXPLICIT_ALUA) &&
	    (tg_pt_gp->tg_pt_gp_alua_access_type & TPGS_IMPLICIT_ALUA))
		return sprintf(page, "Implicit and Explicit\n");
	else if (tg_pt_gp->tg_pt_gp_alua_access_type & TPGS_IMPLICIT_ALUA)
		return sprintf(page, "Implicit\n");
	else if (tg_pt_gp->tg_pt_gp_alua_access_type & TPGS_EXPLICIT_ALUA)
		return sprintf(page, "Explicit\n");
	else
		return sprintf(page, "None\n");
}

ssize_t core_alua_store_access_type(
	struct t10_alua_tg_pt_gp *tg_pt_gp,
	const char *page,
	size_t count)
{
	unsigned long tmp;
	int ret;

	ret = kstrtoul(page, 0, &tmp);
	if (ret < 0) {
		pr_err("Unable to extract alua_access_type\n");
		return ret;
	}
	if ((tmp != 0) && (tmp != 1) && (tmp != 2) && (tmp != 3)) {
		pr_err("Illegal value for alua_access_type:"
				" %lu\n", tmp);
		return -EINVAL;
	}
	if (tmp == 3)
		tg_pt_gp->tg_pt_gp_alua_access_type =
			TPGS_IMPLICIT_ALUA | TPGS_EXPLICIT_ALUA;
	else if (tmp == 2)
		tg_pt_gp->tg_pt_gp_alua_access_type = TPGS_EXPLICIT_ALUA;
	else if (tmp == 1)
		tg_pt_gp->tg_pt_gp_alua_access_type = TPGS_IMPLICIT_ALUA;
	else
		tg_pt_gp->tg_pt_gp_alua_access_type = 0;

	return count;
}

ssize_t core_alua_show_nonop_delay_msecs(
	struct t10_alua_tg_pt_gp *tg_pt_gp,
	char *page)
{
	return sprintf(page, "%d\n", tg_pt_gp->tg_pt_gp_nonop_delay_msecs);
}

ssize_t core_alua_store_nonop_delay_msecs(
	struct t10_alua_tg_pt_gp *tg_pt_gp,
	const char *page,
	size_t count)
{
	unsigned long tmp;
	int ret;

	ret = kstrtoul(page, 0, &tmp);
	if (ret < 0) {
		pr_err("Unable to extract nonop_delay_msecs\n");
		return ret;
	}
	if (tmp > ALUA_MAX_NONOP_DELAY_MSECS) {
		pr_err("Passed nonop_delay_msecs: %lu, exceeds"
			" ALUA_MAX_NONOP_DELAY_MSECS: %d\n", tmp,
			ALUA_MAX_NONOP_DELAY_MSECS);
		return -EINVAL;
	}
	tg_pt_gp->tg_pt_gp_nonop_delay_msecs = (int)tmp;

	return count;
}

ssize_t core_alua_show_trans_delay_msecs(
	struct t10_alua_tg_pt_gp *tg_pt_gp,
	char *page)
{
	return sprintf(page, "%d\n", tg_pt_gp->tg_pt_gp_trans_delay_msecs);
}

ssize_t core_alua_store_trans_delay_msecs(
	struct t10_alua_tg_pt_gp *tg_pt_gp,
	const char *page,
	size_t count)
{
	unsigned long tmp;
	int ret;

	ret = kstrtoul(page, 0, &tmp);
	if (ret < 0) {
		pr_err("Unable to extract trans_delay_msecs\n");
		return ret;
	}
	if (tmp > ALUA_MAX_TRANS_DELAY_MSECS) {
		pr_err("Passed trans_delay_msecs: %lu, exceeds"
			" ALUA_MAX_TRANS_DELAY_MSECS: %d\n", tmp,
			ALUA_MAX_TRANS_DELAY_MSECS);
		return -EINVAL;
	}
	tg_pt_gp->tg_pt_gp_trans_delay_msecs = (int)tmp;

	return count;
}

ssize_t core_alua_show_implicit_trans_secs(
	struct t10_alua_tg_pt_gp *tg_pt_gp,
	char *page)
{
	return sprintf(page, "%d\n", tg_pt_gp->tg_pt_gp_implicit_trans_secs);
}

ssize_t core_alua_store_implicit_trans_secs(
	struct t10_alua_tg_pt_gp *tg_pt_gp,
	const char *page,
	size_t count)
{
	unsigned long tmp;
	int ret;

	ret = kstrtoul(page, 0, &tmp);
	if (ret < 0) {
		pr_err("Unable to extract implicit_trans_secs\n");
		return ret;
	}
	if (tmp > ALUA_MAX_IMPLICIT_TRANS_SECS) {
		pr_err("Passed implicit_trans_secs: %lu, exceeds"
			" ALUA_MAX_IMPLICIT_TRANS_SECS: %d\n", tmp,
			ALUA_MAX_IMPLICIT_TRANS_SECS);
		return  -EINVAL;
	}
	tg_pt_gp->tg_pt_gp_implicit_trans_secs = (int)tmp;

	return count;
}

ssize_t core_alua_show_preferred_bit(
	struct t10_alua_tg_pt_gp *tg_pt_gp,
	char *page)
{
	return sprintf(page, "%d\n", tg_pt_gp->tg_pt_gp_pref);
}

ssize_t core_alua_store_preferred_bit(
	struct t10_alua_tg_pt_gp *tg_pt_gp,
	const char *page,
	size_t count)
{
	unsigned long tmp;
	int ret;

	ret = kstrtoul(page, 0, &tmp);
	if (ret < 0) {
		pr_err("Unable to extract preferred ALUA value\n");
		return ret;
	}
	if ((tmp != 0) && (tmp != 1)) {
		pr_err("Illegal value for preferred ALUA: %lu\n", tmp);
		return -EINVAL;
	}
	tg_pt_gp->tg_pt_gp_pref = (int)tmp;

	return count;
}

ssize_t core_alua_show_offline_bit(struct se_lun *lun, char *page)
{
	return sprintf(page, "%d\n",
		atomic_read(&lun->lun_tg_pt_secondary_offline));
}

ssize_t core_alua_store_offline_bit(
	struct se_lun *lun,
	const char *page,
	size_t count)
{
	/*
	 * rcu_dereference_raw protected by se_lun->lun_group symlink
	 * reference to se_device->dev_group.
	 */
	struct se_device *dev = rcu_dereference_raw(lun->lun_se_dev);
	unsigned long tmp;
	int ret;

	if (dev->transport->transport_flags & TRANSPORT_FLAG_PASSTHROUGH_ALUA ||
	    (dev->se_hba->hba_flags & HBA_FLAGS_INTERNAL_USE))
		return -ENODEV;

	ret = kstrtoul(page, 0, &tmp);
	if (ret < 0) {
		pr_err("Unable to extract alua_tg_pt_offline value\n");
		return ret;
	}
	if ((tmp != 0) && (tmp != 1)) {
		pr_err("Illegal value for alua_tg_pt_offline: %lu\n",
				tmp);
		return -EINVAL;
	}

	ret = core_alua_set_tg_pt_secondary_state(lun, 0, (int)tmp);
	if (ret < 0)
		return -EINVAL;

	return count;
}

ssize_t core_alua_show_secondary_status(
	struct se_lun *lun,
	char *page)
{
	return sprintf(page, "%d\n", lun->lun_tg_pt_secondary_stat);
}

ssize_t core_alua_store_secondary_status(
	struct se_lun *lun,
	const char *page,
	size_t count)
{
	unsigned long tmp;
	int ret;

	ret = kstrtoul(page, 0, &tmp);
	if (ret < 0) {
		pr_err("Unable to extract alua_tg_pt_status\n");
		return ret;
	}
	if ((tmp != ALUA_STATUS_NONE) &&
	    (tmp != ALUA_STATUS_ALTERED_BY_EXPLICIT_STPG) &&
	    (tmp != ALUA_STATUS_ALTERED_BY_IMPLICIT_ALUA)) {
		pr_err("Illegal value for alua_tg_pt_status: %lu\n",
				tmp);
		return -EINVAL;
	}
	lun->lun_tg_pt_secondary_stat = (int)tmp;

	return count;
}

ssize_t core_alua_show_secondary_write_metadata(
	struct se_lun *lun,
	char *page)
{
	return sprintf(page, "%d\n", lun->lun_tg_pt_secondary_write_md);
}

ssize_t core_alua_store_secondary_write_metadata(
	struct se_lun *lun,
	const char *page,
	size_t count)
{
	unsigned long tmp;
	int ret;

	ret = kstrtoul(page, 0, &tmp);
	if (ret < 0) {
		pr_err("Unable to extract alua_tg_pt_write_md\n");
		return ret;
	}
	if ((tmp != 0) && (tmp != 1)) {
		pr_err("Illegal value for alua_tg_pt_write_md:"
				" %lu\n", tmp);
		return -EINVAL;
	}
	lun->lun_tg_pt_secondary_write_md = (int)tmp;

	return count;
}

int core_setup_alua(struct se_device *dev)
{
	if (!(dev->transport->transport_flags &
	     TRANSPORT_FLAG_PASSTHROUGH_ALUA) &&
	    !(dev->se_hba->hba_flags & HBA_FLAGS_INTERNAL_USE)) {
		struct t10_alua_lu_gp_member *lu_gp_mem;

		/*
		 * Associate this struct se_device with the default ALUA
		 * LUN Group.
		 */
		lu_gp_mem = core_alua_allocate_lu_gp_mem(dev);
		if (IS_ERR(lu_gp_mem))
			return PTR_ERR(lu_gp_mem);

		spin_lock(&lu_gp_mem->lu_gp_mem_lock);
		__core_alua_attach_lu_gp_mem(lu_gp_mem,
				default_lu_gp);
		spin_unlock(&lu_gp_mem->lu_gp_mem_lock);

		pr_debug("%s: Adding to default ALUA LU Group:"
			" core/alua/lu_gps/default_lu_gp\n",
			dev->transport->name);
	}

	return 0;
}

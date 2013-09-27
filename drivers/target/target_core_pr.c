/*******************************************************************************
 * Filename:  target_core_pr.c
 *
 * This file contains SPC-3 compliant persistent reservations and
 * legacy SPC-2 reservations with compatible reservation handling (CRH=1)
 *
 * (c) Copyright 2009-2012 RisingTide Systems LLC.
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
#include <linux/list.h>
#include <linux/file.h>
#include <scsi/scsi.h>
#include <scsi/scsi_cmnd.h>
#include <asm/unaligned.h>

#include <target/target_core_base.h>
#include <target/target_core_backend.h>
#include <target/target_core_fabric.h>
#include <target/target_core_configfs.h>

#include "target_core_internal.h"
#include "target_core_pr.h"
#include "target_core_ua.h"

/*
 * Used for Specify Initiator Ports Capable Bit (SPEC_I_PT)
 */
struct pr_transport_id_holder {
	int dest_local_nexus;
	struct t10_pr_registration *dest_pr_reg;
	struct se_portal_group *dest_tpg;
	struct se_node_acl *dest_node_acl;
	struct se_dev_entry *dest_se_deve;
	struct list_head dest_list;
};

void core_pr_dump_initiator_port(
	struct t10_pr_registration *pr_reg,
	char *buf,
	u32 size)
{
	if (!pr_reg->isid_present_at_reg)
		buf[0] = '\0';

	snprintf(buf, size, ",i,0x%s", pr_reg->pr_reg_isid);
}

enum register_type {
	REGISTER,
	REGISTER_AND_IGNORE_EXISTING_KEY,
	REGISTER_AND_MOVE,
};

enum preempt_type {
	PREEMPT,
	PREEMPT_AND_ABORT,
};

static void __core_scsi3_complete_pro_release(struct se_device *, struct se_node_acl *,
			struct t10_pr_registration *, int);

static sense_reason_t
target_scsi2_reservation_check(struct se_cmd *cmd)
{
	struct se_device *dev = cmd->se_dev;
	struct se_session *sess = cmd->se_sess;

	switch (cmd->t_task_cdb[0]) {
	case INQUIRY:
	case RELEASE:
	case RELEASE_10:
		return 0;
	default:
		break;
	}

	if (!dev->dev_reserved_node_acl || !sess)
		return 0;

	if (dev->dev_reserved_node_acl != sess->se_node_acl)
		return TCM_RESERVATION_CONFLICT;

	if (dev->dev_reservation_flags & DRF_SPC2_RESERVATIONS_WITH_ISID) {
		if (dev->dev_res_bin_isid != sess->sess_bin_isid)
			return TCM_RESERVATION_CONFLICT;
	}

	return 0;
}

static struct t10_pr_registration *core_scsi3_locate_pr_reg(struct se_device *,
					struct se_node_acl *, struct se_session *);
static void core_scsi3_put_pr_reg(struct t10_pr_registration *);

static int target_check_scsi2_reservation_conflict(struct se_cmd *cmd)
{
	struct se_session *se_sess = cmd->se_sess;
	struct se_device *dev = cmd->se_dev;
	struct t10_pr_registration *pr_reg;
	struct t10_reservation *pr_tmpl = &dev->t10_pr;
	int conflict = 0;

	pr_reg = core_scsi3_locate_pr_reg(cmd->se_dev, se_sess->se_node_acl,
			se_sess);
	if (pr_reg) {
		/*
		 * From spc4r17 5.7.3 Exceptions to SPC-2 RESERVE and RELEASE
		 * behavior
		 *
		 * A RESERVE(6) or RESERVE(10) command shall complete with GOOD
		 * status, but no reservation shall be established and the
		 * persistent reservation shall not be changed, if the command
		 * is received from a) and b) below.
		 *
		 * A RELEASE(6) or RELEASE(10) command shall complete with GOOD
		 * status, but the persistent reservation shall not be released,
		 * if the command is received from a) and b)
		 *
		 * a) An I_T nexus that is a persistent reservation holder; or
		 * b) An I_T nexus that is registered if a registrants only or
		 *    all registrants type persistent reservation is present.
		 *
		 * In all other cases, a RESERVE(6) command, RESERVE(10) command,
		 * RELEASE(6) command, or RELEASE(10) command shall be processed
		 * as defined in SPC-2.
		 */
		if (pr_reg->pr_res_holder) {
			core_scsi3_put_pr_reg(pr_reg);
			return 1;
		}
		if ((pr_reg->pr_res_type == PR_TYPE_WRITE_EXCLUSIVE_REGONLY) ||
		    (pr_reg->pr_res_type == PR_TYPE_EXCLUSIVE_ACCESS_REGONLY) ||
		    (pr_reg->pr_res_type == PR_TYPE_WRITE_EXCLUSIVE_ALLREG) ||
		    (pr_reg->pr_res_type == PR_TYPE_EXCLUSIVE_ACCESS_ALLREG)) {
			core_scsi3_put_pr_reg(pr_reg);
			return 1;
		}
		core_scsi3_put_pr_reg(pr_reg);
		conflict = 1;
	} else {
		/*
		 * Following spc2r20 5.5.1 Reservations overview:
		 *
		 * If a logical unit has executed a PERSISTENT RESERVE OUT
		 * command with the REGISTER or the REGISTER AND IGNORE
		 * EXISTING KEY service action and is still registered by any
		 * initiator, all RESERVE commands and all RELEASE commands
		 * regardless of initiator shall conflict and shall terminate
		 * with a RESERVATION CONFLICT status.
		 */
		spin_lock(&pr_tmpl->registration_lock);
		conflict = (list_empty(&pr_tmpl->registration_list)) ? 0 : 1;
		spin_unlock(&pr_tmpl->registration_lock);
	}

	if (conflict) {
		pr_err("Received legacy SPC-2 RESERVE/RELEASE"
			" while active SPC-3 registrations exist,"
			" returning RESERVATION_CONFLICT\n");
		return -EBUSY;
	}

	return 0;
}

sense_reason_t
target_scsi2_reservation_release(struct se_cmd *cmd)
{
	struct se_device *dev = cmd->se_dev;
	struct se_session *sess = cmd->se_sess;
	struct se_portal_group *tpg;
	int rc;

	if (!sess || !sess->se_tpg)
		goto out;
	rc = target_check_scsi2_reservation_conflict(cmd);
	if (rc == 1)
		goto out;
	if (rc < 0)
		return TCM_RESERVATION_CONFLICT;

	spin_lock(&dev->dev_reservation_lock);
	if (!dev->dev_reserved_node_acl || !sess)
		goto out_unlock;

	if (dev->dev_reserved_node_acl != sess->se_node_acl)
		goto out_unlock;

	if (dev->dev_res_bin_isid != sess->sess_bin_isid)
		goto out_unlock;

	dev->dev_reserved_node_acl = NULL;
	dev->dev_reservation_flags &= ~DRF_SPC2_RESERVATIONS;
	if (dev->dev_reservation_flags & DRF_SPC2_RESERVATIONS_WITH_ISID) {
		dev->dev_res_bin_isid = 0;
		dev->dev_reservation_flags &= ~DRF_SPC2_RESERVATIONS_WITH_ISID;
	}
	tpg = sess->se_tpg;
	pr_debug("SCSI-2 Released reservation for %s LUN: %u ->"
		" MAPPED LUN: %u for %s\n", tpg->se_tpg_tfo->get_fabric_name(),
		cmd->se_lun->unpacked_lun, cmd->se_deve->mapped_lun,
		sess->se_node_acl->initiatorname);

out_unlock:
	spin_unlock(&dev->dev_reservation_lock);
out:
	target_complete_cmd(cmd, GOOD);
	return 0;
}

sense_reason_t
target_scsi2_reservation_reserve(struct se_cmd *cmd)
{
	struct se_device *dev = cmd->se_dev;
	struct se_session *sess = cmd->se_sess;
	struct se_portal_group *tpg;
	sense_reason_t ret = 0;
	int rc;

	if ((cmd->t_task_cdb[1] & 0x01) &&
	    (cmd->t_task_cdb[1] & 0x02)) {
		pr_err("LongIO and Obselete Bits set, returning"
				" ILLEGAL_REQUEST\n");
		return TCM_UNSUPPORTED_SCSI_OPCODE;
	}
	/*
	 * This is currently the case for target_core_mod passthrough struct se_cmd
	 * ops
	 */
	if (!sess || !sess->se_tpg)
		goto out;
	rc = target_check_scsi2_reservation_conflict(cmd);
	if (rc == 1)
		goto out;

	if (rc < 0)
		return TCM_RESERVATION_CONFLICT;

	tpg = sess->se_tpg;
	spin_lock(&dev->dev_reservation_lock);
	if (dev->dev_reserved_node_acl &&
	   (dev->dev_reserved_node_acl != sess->se_node_acl)) {
		pr_err("SCSI-2 RESERVATION CONFLIFT for %s fabric\n",
			tpg->se_tpg_tfo->get_fabric_name());
		pr_err("Original reserver LUN: %u %s\n",
			cmd->se_lun->unpacked_lun,
			dev->dev_reserved_node_acl->initiatorname);
		pr_err("Current attempt - LUN: %u -> MAPPED LUN: %u"
			" from %s \n", cmd->se_lun->unpacked_lun,
			cmd->se_deve->mapped_lun,
			sess->se_node_acl->initiatorname);
		ret = TCM_RESERVATION_CONFLICT;
		goto out_unlock;
	}

	dev->dev_reserved_node_acl = sess->se_node_acl;
	dev->dev_reservation_flags |= DRF_SPC2_RESERVATIONS;
	if (sess->sess_bin_isid != 0) {
		dev->dev_res_bin_isid = sess->sess_bin_isid;
		dev->dev_reservation_flags |= DRF_SPC2_RESERVATIONS_WITH_ISID;
	}
	pr_debug("SCSI-2 Reserved %s LUN: %u -> MAPPED LUN: %u"
		" for %s\n", tpg->se_tpg_tfo->get_fabric_name(),
		cmd->se_lun->unpacked_lun, cmd->se_deve->mapped_lun,
		sess->se_node_acl->initiatorname);

out_unlock:
	spin_unlock(&dev->dev_reservation_lock);
out:
	if (!ret)
		target_complete_cmd(cmd, GOOD);
	return ret;
}


/*
 * Begin SPC-3/SPC-4 Persistent Reservations emulation support
 *
 * This function is called by those initiator ports who are *NOT*
 * the active PR reservation holder when a reservation is present.
 */
static int core_scsi3_pr_seq_non_holder(
	struct se_cmd *cmd,
	u32 pr_reg_type)
{
	unsigned char *cdb = cmd->t_task_cdb;
	struct se_dev_entry *se_deve;
	struct se_session *se_sess = cmd->se_sess;
	int other_cdb = 0, ignore_reg;
	int registered_nexus = 0, ret = 1; /* Conflict by default */
	int all_reg = 0, reg_only = 0; /* ALL_REG, REG_ONLY */
	int we = 0; /* Write Exclusive */
	int legacy = 0; /* Act like a legacy device and return
			 * RESERVATION CONFLICT on some CDBs */

	se_deve = se_sess->se_node_acl->device_list[cmd->orig_fe_lun];
	/*
	 * Determine if the registration should be ignored due to
	 * non-matching ISIDs in target_scsi3_pr_reservation_check().
	 */
	ignore_reg = (pr_reg_type & 0x80000000);
	if (ignore_reg)
		pr_reg_type &= ~0x80000000;

	switch (pr_reg_type) {
	case PR_TYPE_WRITE_EXCLUSIVE:
		we = 1;
	case PR_TYPE_EXCLUSIVE_ACCESS:
		/*
		 * Some commands are only allowed for the persistent reservation
		 * holder.
		 */
		if ((se_deve->def_pr_registered) && !(ignore_reg))
			registered_nexus = 1;
		break;
	case PR_TYPE_WRITE_EXCLUSIVE_REGONLY:
		we = 1;
	case PR_TYPE_EXCLUSIVE_ACCESS_REGONLY:
		/*
		 * Some commands are only allowed for registered I_T Nexuses.
		 */
		reg_only = 1;
		if ((se_deve->def_pr_registered) && !(ignore_reg))
			registered_nexus = 1;
		break;
	case PR_TYPE_WRITE_EXCLUSIVE_ALLREG:
		we = 1;
	case PR_TYPE_EXCLUSIVE_ACCESS_ALLREG:
		/*
		 * Each registered I_T Nexus is a reservation holder.
		 */
		all_reg = 1;
		if ((se_deve->def_pr_registered) && !(ignore_reg))
			registered_nexus = 1;
		break;
	default:
		return -EINVAL;
	}
	/*
	 * Referenced from spc4r17 table 45 for *NON* PR holder access
	 */
	switch (cdb[0]) {
	case SECURITY_PROTOCOL_IN:
		if (registered_nexus)
			return 0;
		ret = (we) ? 0 : 1;
		break;
	case MODE_SENSE:
	case MODE_SENSE_10:
	case READ_ATTRIBUTE:
	case READ_BUFFER:
	case RECEIVE_DIAGNOSTIC:
		if (legacy) {
			ret = 1;
			break;
		}
		if (registered_nexus) {
			ret = 0;
			break;
		}
		ret = (we) ? 0 : 1; /* Allowed Write Exclusive */
		break;
	case PERSISTENT_RESERVE_OUT:
		/*
		 * This follows PERSISTENT_RESERVE_OUT service actions that
		 * are allowed in the presence of various reservations.
		 * See spc4r17, table 46
		 */
		switch (cdb[1] & 0x1f) {
		case PRO_CLEAR:
		case PRO_PREEMPT:
		case PRO_PREEMPT_AND_ABORT:
			ret = (registered_nexus) ? 0 : 1;
			break;
		case PRO_REGISTER:
		case PRO_REGISTER_AND_IGNORE_EXISTING_KEY:
			ret = 0;
			break;
		case PRO_REGISTER_AND_MOVE:
		case PRO_RESERVE:
			ret = 1;
			break;
		case PRO_RELEASE:
			ret = (registered_nexus) ? 0 : 1;
			break;
		default:
			pr_err("Unknown PERSISTENT_RESERVE_OUT service"
				" action: 0x%02x\n", cdb[1] & 0x1f);
			return -EINVAL;
		}
		break;
	case RELEASE:
	case RELEASE_10:
		/* Handled by CRH=1 in target_scsi2_reservation_release() */
		ret = 0;
		break;
	case RESERVE:
	case RESERVE_10:
		/* Handled by CRH=1 in target_scsi2_reservation_reserve() */
		ret = 0;
		break;
	case TEST_UNIT_READY:
		ret = (legacy) ? 1 : 0; /* Conflict for legacy */
		break;
	case MAINTENANCE_IN:
		switch (cdb[1] & 0x1f) {
		case MI_MANAGEMENT_PROTOCOL_IN:
			if (registered_nexus) {
				ret = 0;
				break;
			}
			ret = (we) ? 0 : 1; /* Allowed Write Exclusive */
			break;
		case MI_REPORT_SUPPORTED_OPERATION_CODES:
		case MI_REPORT_SUPPORTED_TASK_MANAGEMENT_FUNCTIONS:
			if (legacy) {
				ret = 1;
				break;
			}
			if (registered_nexus) {
				ret = 0;
				break;
			}
			ret = (we) ? 0 : 1; /* Allowed Write Exclusive */
			break;
		case MI_REPORT_ALIASES:
		case MI_REPORT_IDENTIFYING_INFORMATION:
		case MI_REPORT_PRIORITY:
		case MI_REPORT_TARGET_PGS:
		case MI_REPORT_TIMESTAMP:
			ret = 0; /* Allowed */
			break;
		default:
			pr_err("Unknown MI Service Action: 0x%02x\n",
				(cdb[1] & 0x1f));
			return -EINVAL;
		}
		break;
	case ACCESS_CONTROL_IN:
	case ACCESS_CONTROL_OUT:
	case INQUIRY:
	case LOG_SENSE:
	case READ_MEDIA_SERIAL_NUMBER:
	case REPORT_LUNS:
	case REQUEST_SENSE:
	case PERSISTENT_RESERVE_IN:
		ret = 0; /*/ Allowed CDBs */
		break;
	default:
		other_cdb = 1;
		break;
	}
	/*
	 * Case where the CDB is explicitly allowed in the above switch
	 * statement.
	 */
	if (!ret && !other_cdb) {
		pr_debug("Allowing explict CDB: 0x%02x for %s"
			" reservation holder\n", cdb[0],
			core_scsi3_pr_dump_type(pr_reg_type));

		return ret;
	}
	/*
	 * Check if write exclusive initiator ports *NOT* holding the
	 * WRITE_EXCLUSIVE_* reservation.
	 */
	if (we && !registered_nexus) {
		if (cmd->data_direction == DMA_TO_DEVICE) {
			/*
			 * Conflict for write exclusive
			 */
			pr_debug("%s Conflict for unregistered nexus"
				" %s CDB: 0x%02x to %s reservation\n",
				transport_dump_cmd_direction(cmd),
				se_sess->se_node_acl->initiatorname, cdb[0],
				core_scsi3_pr_dump_type(pr_reg_type));
			return 1;
		} else {
			/*
			 * Allow non WRITE CDBs for all Write Exclusive
			 * PR TYPEs to pass for registered and
			 * non-registered_nexuxes NOT holding the reservation.
			 *
			 * We only make noise for the unregisterd nexuses,
			 * as we expect registered non-reservation holding
			 * nexuses to issue CDBs.
			 */

			if (!registered_nexus) {
				pr_debug("Allowing implict CDB: 0x%02x"
					" for %s reservation on unregistered"
					" nexus\n", cdb[0],
					core_scsi3_pr_dump_type(pr_reg_type));
			}

			return 0;
		}
	} else if ((reg_only) || (all_reg)) {
		if (registered_nexus) {
			/*
			 * For PR_*_REG_ONLY and PR_*_ALL_REG reservations,
			 * allow commands from registered nexuses.
			 */

			pr_debug("Allowing implict CDB: 0x%02x for %s"
				" reservation\n", cdb[0],
				core_scsi3_pr_dump_type(pr_reg_type));

			return 0;
		}
	}
	pr_debug("%s Conflict for %sregistered nexus %s CDB: 0x%2x"
		" for %s reservation\n", transport_dump_cmd_direction(cmd),
		(registered_nexus) ? "" : "un",
		se_sess->se_node_acl->initiatorname, cdb[0],
		core_scsi3_pr_dump_type(pr_reg_type));

	return 1; /* Conflict by default */
}

static sense_reason_t
target_scsi3_pr_reservation_check(struct se_cmd *cmd)
{
	struct se_device *dev = cmd->se_dev;
	struct se_session *sess = cmd->se_sess;
	u32 pr_reg_type;

	if (!dev->dev_pr_res_holder)
		return 0;

	pr_reg_type = dev->dev_pr_res_holder->pr_res_type;
	cmd->pr_res_key = dev->dev_pr_res_holder->pr_res_key;
	if (dev->dev_pr_res_holder->pr_reg_nacl != sess->se_node_acl)
		goto check_nonholder;

	if (dev->dev_pr_res_holder->isid_present_at_reg) {
		if (dev->dev_pr_res_holder->pr_reg_bin_isid !=
		    sess->sess_bin_isid) {
			pr_reg_type |= 0x80000000;
			goto check_nonholder;
		}
	}

	return 0;

check_nonholder:
	if (core_scsi3_pr_seq_non_holder(cmd, pr_reg_type))
		return TCM_RESERVATION_CONFLICT;
	return 0;
}

static u32 core_scsi3_pr_generation(struct se_device *dev)
{
	u32 prg;

	/*
	 * PRGeneration field shall contain the value of a 32-bit wrapping
	 * counter mainted by the device server.
	 *
	 * Note that this is done regardless of Active Persist across
	 * Target PowerLoss (APTPL)
	 *
	 * See spc4r17 section 6.3.12 READ_KEYS service action
	 */
	spin_lock(&dev->dev_reservation_lock);
	prg = dev->t10_pr.pr_generation++;
	spin_unlock(&dev->dev_reservation_lock);

	return prg;
}

static struct t10_pr_registration *__core_scsi3_do_alloc_registration(
	struct se_device *dev,
	struct se_node_acl *nacl,
	struct se_dev_entry *deve,
	unsigned char *isid,
	u64 sa_res_key,
	int all_tg_pt,
	int aptpl)
{
	struct t10_pr_registration *pr_reg;

	pr_reg = kmem_cache_zalloc(t10_pr_reg_cache, GFP_ATOMIC);
	if (!pr_reg) {
		pr_err("Unable to allocate struct t10_pr_registration\n");
		return NULL;
	}

	INIT_LIST_HEAD(&pr_reg->pr_reg_list);
	INIT_LIST_HEAD(&pr_reg->pr_reg_abort_list);
	INIT_LIST_HEAD(&pr_reg->pr_reg_aptpl_list);
	INIT_LIST_HEAD(&pr_reg->pr_reg_atp_list);
	INIT_LIST_HEAD(&pr_reg->pr_reg_atp_mem_list);
	atomic_set(&pr_reg->pr_res_holders, 0);
	pr_reg->pr_reg_nacl = nacl;
	pr_reg->pr_reg_deve = deve;
	pr_reg->pr_res_mapped_lun = deve->mapped_lun;
	pr_reg->pr_aptpl_target_lun = deve->se_lun->unpacked_lun;
	pr_reg->pr_res_key = sa_res_key;
	pr_reg->pr_reg_all_tg_pt = all_tg_pt;
	pr_reg->pr_reg_aptpl = aptpl;
	pr_reg->pr_reg_tg_pt_lun = deve->se_lun;
	/*
	 * If an ISID value for this SCSI Initiator Port exists,
	 * save it to the registration now.
	 */
	if (isid != NULL) {
		pr_reg->pr_reg_bin_isid = get_unaligned_be64(isid);
		snprintf(pr_reg->pr_reg_isid, PR_REG_ISID_LEN, "%s", isid);
		pr_reg->isid_present_at_reg = 1;
	}

	return pr_reg;
}

static int core_scsi3_lunacl_depend_item(struct se_dev_entry *);
static void core_scsi3_lunacl_undepend_item(struct se_dev_entry *);

/*
 * Function used for handling PR registrations for ALL_TG_PT=1 and ALL_TG_PT=0
 * modes.
 */
static struct t10_pr_registration *__core_scsi3_alloc_registration(
	struct se_device *dev,
	struct se_node_acl *nacl,
	struct se_dev_entry *deve,
	unsigned char *isid,
	u64 sa_res_key,
	int all_tg_pt,
	int aptpl)
{
	struct se_dev_entry *deve_tmp;
	struct se_node_acl *nacl_tmp;
	struct se_port *port, *port_tmp;
	struct target_core_fabric_ops *tfo = nacl->se_tpg->se_tpg_tfo;
	struct t10_pr_registration *pr_reg, *pr_reg_atp, *pr_reg_tmp, *pr_reg_tmp_safe;
	int ret;
	/*
	 * Create a registration for the I_T Nexus upon which the
	 * PROUT REGISTER was received.
	 */
	pr_reg = __core_scsi3_do_alloc_registration(dev, nacl, deve, isid,
			sa_res_key, all_tg_pt, aptpl);
	if (!pr_reg)
		return NULL;
	/*
	 * Return pointer to pr_reg for ALL_TG_PT=0
	 */
	if (!all_tg_pt)
		return pr_reg;
	/*
	 * Create list of matching SCSI Initiator Port registrations
	 * for ALL_TG_PT=1
	 */
	spin_lock(&dev->se_port_lock);
	list_for_each_entry_safe(port, port_tmp, &dev->dev_sep_list, sep_list) {
		atomic_inc(&port->sep_tg_pt_ref_cnt);
		smp_mb__after_atomic_inc();
		spin_unlock(&dev->se_port_lock);

		spin_lock_bh(&port->sep_alua_lock);
		list_for_each_entry(deve_tmp, &port->sep_alua_list,
					alua_port_list) {
			/*
			 * This pointer will be NULL for demo mode MappedLUNs
			 * that have not been make explict via a ConfigFS
			 * MappedLUN group for the SCSI Initiator Node ACL.
			 */
			if (!deve_tmp->se_lun_acl)
				continue;

			nacl_tmp = deve_tmp->se_lun_acl->se_lun_nacl;
			/*
			 * Skip the matching struct se_node_acl that is allocated
			 * above..
			 */
			if (nacl == nacl_tmp)
				continue;
			/*
			 * Only perform PR registrations for target ports on
			 * the same fabric module as the REGISTER w/ ALL_TG_PT=1
			 * arrived.
			 */
			if (tfo != nacl_tmp->se_tpg->se_tpg_tfo)
				continue;
			/*
			 * Look for a matching Initiator Node ACL in ASCII format
			 */
			if (strcmp(nacl->initiatorname, nacl_tmp->initiatorname))
				continue;

			atomic_inc(&deve_tmp->pr_ref_count);
			smp_mb__after_atomic_inc();
			spin_unlock_bh(&port->sep_alua_lock);
			/*
			 * Grab a configfs group dependency that is released
			 * for the exception path at label out: below, or upon
			 * completion of adding ALL_TG_PT=1 registrations in
			 * __core_scsi3_add_registration()
			 */
			ret = core_scsi3_lunacl_depend_item(deve_tmp);
			if (ret < 0) {
				pr_err("core_scsi3_lunacl_depend"
						"_item() failed\n");
				atomic_dec(&port->sep_tg_pt_ref_cnt);
				smp_mb__after_atomic_dec();
				atomic_dec(&deve_tmp->pr_ref_count);
				smp_mb__after_atomic_dec();
				goto out;
			}
			/*
			 * Located a matching SCSI Initiator Port on a different
			 * port, allocate the pr_reg_atp and attach it to the
			 * pr_reg->pr_reg_atp_list that will be processed once
			 * the original *pr_reg is processed in
			 * __core_scsi3_add_registration()
			 */
			pr_reg_atp = __core_scsi3_do_alloc_registration(dev,
						nacl_tmp, deve_tmp, NULL,
						sa_res_key, all_tg_pt, aptpl);
			if (!pr_reg_atp) {
				atomic_dec(&port->sep_tg_pt_ref_cnt);
				smp_mb__after_atomic_dec();
				atomic_dec(&deve_tmp->pr_ref_count);
				smp_mb__after_atomic_dec();
				core_scsi3_lunacl_undepend_item(deve_tmp);
				goto out;
			}

			list_add_tail(&pr_reg_atp->pr_reg_atp_mem_list,
				      &pr_reg->pr_reg_atp_list);
			spin_lock_bh(&port->sep_alua_lock);
		}
		spin_unlock_bh(&port->sep_alua_lock);

		spin_lock(&dev->se_port_lock);
		atomic_dec(&port->sep_tg_pt_ref_cnt);
		smp_mb__after_atomic_dec();
	}
	spin_unlock(&dev->se_port_lock);

	return pr_reg;
out:
	list_for_each_entry_safe(pr_reg_tmp, pr_reg_tmp_safe,
			&pr_reg->pr_reg_atp_list, pr_reg_atp_mem_list) {
		list_del(&pr_reg_tmp->pr_reg_atp_mem_list);
		core_scsi3_lunacl_undepend_item(pr_reg_tmp->pr_reg_deve);
		kmem_cache_free(t10_pr_reg_cache, pr_reg_tmp);
	}
	kmem_cache_free(t10_pr_reg_cache, pr_reg);
	return NULL;
}

int core_scsi3_alloc_aptpl_registration(
	struct t10_reservation *pr_tmpl,
	u64 sa_res_key,
	unsigned char *i_port,
	unsigned char *isid,
	u32 mapped_lun,
	unsigned char *t_port,
	u16 tpgt,
	u32 target_lun,
	int res_holder,
	int all_tg_pt,
	u8 type)
{
	struct t10_pr_registration *pr_reg;

	if (!i_port || !t_port || !sa_res_key) {
		pr_err("Illegal parameters for APTPL registration\n");
		return -EINVAL;
	}

	pr_reg = kmem_cache_zalloc(t10_pr_reg_cache, GFP_KERNEL);
	if (!pr_reg) {
		pr_err("Unable to allocate struct t10_pr_registration\n");
		return -ENOMEM;
	}

	INIT_LIST_HEAD(&pr_reg->pr_reg_list);
	INIT_LIST_HEAD(&pr_reg->pr_reg_abort_list);
	INIT_LIST_HEAD(&pr_reg->pr_reg_aptpl_list);
	INIT_LIST_HEAD(&pr_reg->pr_reg_atp_list);
	INIT_LIST_HEAD(&pr_reg->pr_reg_atp_mem_list);
	atomic_set(&pr_reg->pr_res_holders, 0);
	pr_reg->pr_reg_nacl = NULL;
	pr_reg->pr_reg_deve = NULL;
	pr_reg->pr_res_mapped_lun = mapped_lun;
	pr_reg->pr_aptpl_target_lun = target_lun;
	pr_reg->pr_res_key = sa_res_key;
	pr_reg->pr_reg_all_tg_pt = all_tg_pt;
	pr_reg->pr_reg_aptpl = 1;
	pr_reg->pr_reg_tg_pt_lun = NULL;
	pr_reg->pr_res_scope = 0; /* Always LUN_SCOPE */
	pr_reg->pr_res_type = type;
	/*
	 * If an ISID value had been saved in APTPL metadata for this
	 * SCSI Initiator Port, restore it now.
	 */
	if (isid != NULL) {
		pr_reg->pr_reg_bin_isid = get_unaligned_be64(isid);
		snprintf(pr_reg->pr_reg_isid, PR_REG_ISID_LEN, "%s", isid);
		pr_reg->isid_present_at_reg = 1;
	}
	/*
	 * Copy the i_port and t_port information from caller.
	 */
	snprintf(pr_reg->pr_iport, PR_APTPL_MAX_IPORT_LEN, "%s", i_port);
	snprintf(pr_reg->pr_tport, PR_APTPL_MAX_TPORT_LEN, "%s", t_port);
	pr_reg->pr_reg_tpgt = tpgt;
	/*
	 * Set pr_res_holder from caller, the pr_reg who is the reservation
	 * holder will get it's pointer set in core_scsi3_aptpl_reserve() once
	 * the Initiator Node LUN ACL from the fabric module is created for
	 * this registration.
	 */
	pr_reg->pr_res_holder = res_holder;

	list_add_tail(&pr_reg->pr_reg_aptpl_list, &pr_tmpl->aptpl_reg_list);
	pr_debug("SPC-3 PR APTPL Successfully added registration%s from"
			" metadata\n", (res_holder) ? "+reservation" : "");
	return 0;
}

static void core_scsi3_aptpl_reserve(
	struct se_device *dev,
	struct se_portal_group *tpg,
	struct se_node_acl *node_acl,
	struct t10_pr_registration *pr_reg)
{
	char i_buf[PR_REG_ISID_ID_LEN];

	memset(i_buf, 0, PR_REG_ISID_ID_LEN);
	core_pr_dump_initiator_port(pr_reg, i_buf, PR_REG_ISID_ID_LEN);

	spin_lock(&dev->dev_reservation_lock);
	dev->dev_pr_res_holder = pr_reg;
	spin_unlock(&dev->dev_reservation_lock);

	pr_debug("SPC-3 PR [%s] Service Action: APTPL RESERVE created"
		" new reservation holder TYPE: %s ALL_TG_PT: %d\n",
		tpg->se_tpg_tfo->get_fabric_name(),
		core_scsi3_pr_dump_type(pr_reg->pr_res_type),
		(pr_reg->pr_reg_all_tg_pt) ? 1 : 0);
	pr_debug("SPC-3 PR [%s] RESERVE Node: %s%s\n",
		tpg->se_tpg_tfo->get_fabric_name(), node_acl->initiatorname,
		i_buf);
}

static void __core_scsi3_add_registration(struct se_device *, struct se_node_acl *,
				struct t10_pr_registration *, enum register_type, int);

static int __core_scsi3_check_aptpl_registration(
	struct se_device *dev,
	struct se_portal_group *tpg,
	struct se_lun *lun,
	u32 target_lun,
	struct se_node_acl *nacl,
	struct se_dev_entry *deve)
{
	struct t10_pr_registration *pr_reg, *pr_reg_tmp;
	struct t10_reservation *pr_tmpl = &dev->t10_pr;
	unsigned char i_port[PR_APTPL_MAX_IPORT_LEN];
	unsigned char t_port[PR_APTPL_MAX_TPORT_LEN];
	u16 tpgt;

	memset(i_port, 0, PR_APTPL_MAX_IPORT_LEN);
	memset(t_port, 0, PR_APTPL_MAX_TPORT_LEN);
	/*
	 * Copy Initiator Port information from struct se_node_acl
	 */
	snprintf(i_port, PR_APTPL_MAX_IPORT_LEN, "%s", nacl->initiatorname);
	snprintf(t_port, PR_APTPL_MAX_TPORT_LEN, "%s",
			tpg->se_tpg_tfo->tpg_get_wwn(tpg));
	tpgt = tpg->se_tpg_tfo->tpg_get_tag(tpg);
	/*
	 * Look for the matching registrations+reservation from those
	 * created from APTPL metadata.  Note that multiple registrations
	 * may exist for fabrics that use ISIDs in their SCSI Initiator Port
	 * TransportIDs.
	 */
	spin_lock(&pr_tmpl->aptpl_reg_lock);
	list_for_each_entry_safe(pr_reg, pr_reg_tmp, &pr_tmpl->aptpl_reg_list,
				pr_reg_aptpl_list) {
		if (!strcmp(pr_reg->pr_iport, i_port) &&
		     (pr_reg->pr_res_mapped_lun == deve->mapped_lun) &&
		    !(strcmp(pr_reg->pr_tport, t_port)) &&
		     (pr_reg->pr_reg_tpgt == tpgt) &&
		     (pr_reg->pr_aptpl_target_lun == target_lun)) {

			pr_reg->pr_reg_nacl = nacl;
			pr_reg->pr_reg_deve = deve;
			pr_reg->pr_reg_tg_pt_lun = lun;

			list_del(&pr_reg->pr_reg_aptpl_list);
			spin_unlock(&pr_tmpl->aptpl_reg_lock);
			/*
			 * At this point all of the pointers in *pr_reg will
			 * be setup, so go ahead and add the registration.
			 */

			__core_scsi3_add_registration(dev, nacl, pr_reg, 0, 0);
			/*
			 * If this registration is the reservation holder,
			 * make that happen now..
			 */
			if (pr_reg->pr_res_holder)
				core_scsi3_aptpl_reserve(dev, tpg,
						nacl, pr_reg);
			/*
			 * Reenable pr_aptpl_active to accept new metadata
			 * updates once the SCSI device is active again..
			 */
			spin_lock(&pr_tmpl->aptpl_reg_lock);
			pr_tmpl->pr_aptpl_active = 1;
		}
	}
	spin_unlock(&pr_tmpl->aptpl_reg_lock);

	return 0;
}

int core_scsi3_check_aptpl_registration(
	struct se_device *dev,
	struct se_portal_group *tpg,
	struct se_lun *lun,
	struct se_lun_acl *lun_acl)
{
	struct se_node_acl *nacl = lun_acl->se_lun_nacl;
	struct se_dev_entry *deve = nacl->device_list[lun_acl->mapped_lun];

	if (dev->dev_reservation_flags & DRF_SPC2_RESERVATIONS)
		return 0;

	return __core_scsi3_check_aptpl_registration(dev, tpg, lun,
				lun->unpacked_lun, nacl, deve);
}

static void __core_scsi3_dump_registration(
	struct target_core_fabric_ops *tfo,
	struct se_device *dev,
	struct se_node_acl *nacl,
	struct t10_pr_registration *pr_reg,
	enum register_type register_type)
{
	struct se_portal_group *se_tpg = nacl->se_tpg;
	char i_buf[PR_REG_ISID_ID_LEN];

	memset(&i_buf[0], 0, PR_REG_ISID_ID_LEN);
	core_pr_dump_initiator_port(pr_reg, i_buf, PR_REG_ISID_ID_LEN);

	pr_debug("SPC-3 PR [%s] Service Action: REGISTER%s Initiator"
		" Node: %s%s\n", tfo->get_fabric_name(), (register_type == REGISTER_AND_MOVE) ?
		"_AND_MOVE" : (register_type == REGISTER_AND_IGNORE_EXISTING_KEY) ?
		"_AND_IGNORE_EXISTING_KEY" : "", nacl->initiatorname,
		i_buf);
	pr_debug("SPC-3 PR [%s] registration on Target Port: %s,0x%04x\n",
		 tfo->get_fabric_name(), tfo->tpg_get_wwn(se_tpg),
		tfo->tpg_get_tag(se_tpg));
	pr_debug("SPC-3 PR [%s] for %s TCM Subsystem %s Object Target"
		" Port(s)\n",  tfo->get_fabric_name(),
		(pr_reg->pr_reg_all_tg_pt) ? "ALL" : "SINGLE",
		dev->transport->name);
	pr_debug("SPC-3 PR [%s] SA Res Key: 0x%016Lx PRgeneration:"
		" 0x%08x  APTPL: %d\n", tfo->get_fabric_name(),
		pr_reg->pr_res_key, pr_reg->pr_res_generation,
		pr_reg->pr_reg_aptpl);
}

/*
 * this function can be called with struct se_device->dev_reservation_lock
 * when register_move = 1
 */
static void __core_scsi3_add_registration(
	struct se_device *dev,
	struct se_node_acl *nacl,
	struct t10_pr_registration *pr_reg,
	enum register_type register_type,
	int register_move)
{
	struct target_core_fabric_ops *tfo = nacl->se_tpg->se_tpg_tfo;
	struct t10_pr_registration *pr_reg_tmp, *pr_reg_tmp_safe;
	struct t10_reservation *pr_tmpl = &dev->t10_pr;

	/*
	 * Increment PRgeneration counter for struct se_device upon a successful
	 * REGISTER, see spc4r17 section 6.3.2 READ_KEYS service action
	 *
	 * Also, when register_move = 1 for PROUT REGISTER_AND_MOVE service
	 * action, the struct se_device->dev_reservation_lock will already be held,
	 * so we do not call core_scsi3_pr_generation() which grabs the lock
	 * for the REGISTER.
	 */
	pr_reg->pr_res_generation = (register_move) ?
			dev->t10_pr.pr_generation++ :
			core_scsi3_pr_generation(dev);

	spin_lock(&pr_tmpl->registration_lock);
	list_add_tail(&pr_reg->pr_reg_list, &pr_tmpl->registration_list);
	pr_reg->pr_reg_deve->def_pr_registered = 1;

	__core_scsi3_dump_registration(tfo, dev, nacl, pr_reg, register_type);
	spin_unlock(&pr_tmpl->registration_lock);
	/*
	 * Skip extra processing for ALL_TG_PT=0 or REGISTER_AND_MOVE.
	 */
	if (!pr_reg->pr_reg_all_tg_pt || register_move)
		return;
	/*
	 * Walk pr_reg->pr_reg_atp_list and add registrations for ALL_TG_PT=1
	 * allocated in __core_scsi3_alloc_registration()
	 */
	list_for_each_entry_safe(pr_reg_tmp, pr_reg_tmp_safe,
			&pr_reg->pr_reg_atp_list, pr_reg_atp_mem_list) {
		list_del(&pr_reg_tmp->pr_reg_atp_mem_list);

		pr_reg_tmp->pr_res_generation = core_scsi3_pr_generation(dev);

		spin_lock(&pr_tmpl->registration_lock);
		list_add_tail(&pr_reg_tmp->pr_reg_list,
			      &pr_tmpl->registration_list);
		pr_reg_tmp->pr_reg_deve->def_pr_registered = 1;

		__core_scsi3_dump_registration(tfo, dev,
				pr_reg_tmp->pr_reg_nacl, pr_reg_tmp,
				register_type);
		spin_unlock(&pr_tmpl->registration_lock);
		/*
		 * Drop configfs group dependency reference from
		 * __core_scsi3_alloc_registration()
		 */
		core_scsi3_lunacl_undepend_item(pr_reg_tmp->pr_reg_deve);
	}
}

static int core_scsi3_alloc_registration(
	struct se_device *dev,
	struct se_node_acl *nacl,
	struct se_dev_entry *deve,
	unsigned char *isid,
	u64 sa_res_key,
	int all_tg_pt,
	int aptpl,
	enum register_type register_type,
	int register_move)
{
	struct t10_pr_registration *pr_reg;

	pr_reg = __core_scsi3_alloc_registration(dev, nacl, deve, isid,
			sa_res_key, all_tg_pt, aptpl);
	if (!pr_reg)
		return -EPERM;

	__core_scsi3_add_registration(dev, nacl, pr_reg,
			register_type, register_move);
	return 0;
}

static struct t10_pr_registration *__core_scsi3_locate_pr_reg(
	struct se_device *dev,
	struct se_node_acl *nacl,
	unsigned char *isid)
{
	struct t10_reservation *pr_tmpl = &dev->t10_pr;
	struct t10_pr_registration *pr_reg, *pr_reg_tmp;
	struct se_portal_group *tpg;

	spin_lock(&pr_tmpl->registration_lock);
	list_for_each_entry_safe(pr_reg, pr_reg_tmp,
			&pr_tmpl->registration_list, pr_reg_list) {
		/*
		 * First look for a matching struct se_node_acl
		 */
		if (pr_reg->pr_reg_nacl != nacl)
			continue;

		tpg = pr_reg->pr_reg_nacl->se_tpg;
		/*
		 * If this registration does NOT contain a fabric provided
		 * ISID, then we have found a match.
		 */
		if (!pr_reg->isid_present_at_reg) {
			/*
			 * Determine if this SCSI device server requires that
			 * SCSI Intiatior TransportID w/ ISIDs is enforced
			 * for fabric modules (iSCSI) requiring them.
			 */
			if (tpg->se_tpg_tfo->sess_get_initiator_sid != NULL) {
				if (dev->dev_attrib.enforce_pr_isids)
					continue;
			}
			atomic_inc(&pr_reg->pr_res_holders);
			smp_mb__after_atomic_inc();
			spin_unlock(&pr_tmpl->registration_lock);
			return pr_reg;
		}
		/*
		 * If the *pr_reg contains a fabric defined ISID for multi-value
		 * SCSI Initiator Port TransportIDs, then we expect a valid
		 * matching ISID to be provided by the local SCSI Initiator Port.
		 */
		if (!isid)
			continue;
		if (strcmp(isid, pr_reg->pr_reg_isid))
			continue;

		atomic_inc(&pr_reg->pr_res_holders);
		smp_mb__after_atomic_inc();
		spin_unlock(&pr_tmpl->registration_lock);
		return pr_reg;
	}
	spin_unlock(&pr_tmpl->registration_lock);

	return NULL;
}

static struct t10_pr_registration *core_scsi3_locate_pr_reg(
	struct se_device *dev,
	struct se_node_acl *nacl,
	struct se_session *sess)
{
	struct se_portal_group *tpg = nacl->se_tpg;
	unsigned char buf[PR_REG_ISID_LEN], *isid_ptr = NULL;

	if (tpg->se_tpg_tfo->sess_get_initiator_sid != NULL) {
		memset(&buf[0], 0, PR_REG_ISID_LEN);
		tpg->se_tpg_tfo->sess_get_initiator_sid(sess, &buf[0],
					PR_REG_ISID_LEN);
		isid_ptr = &buf[0];
	}

	return __core_scsi3_locate_pr_reg(dev, nacl, isid_ptr);
}

static void core_scsi3_put_pr_reg(struct t10_pr_registration *pr_reg)
{
	atomic_dec(&pr_reg->pr_res_holders);
	smp_mb__after_atomic_dec();
}

static int core_scsi3_check_implict_release(
	struct se_device *dev,
	struct t10_pr_registration *pr_reg)
{
	struct se_node_acl *nacl = pr_reg->pr_reg_nacl;
	struct t10_pr_registration *pr_res_holder;
	int ret = 0;

	spin_lock(&dev->dev_reservation_lock);
	pr_res_holder = dev->dev_pr_res_holder;
	if (!pr_res_holder) {
		spin_unlock(&dev->dev_reservation_lock);
		return ret;
	}
	if (pr_res_holder == pr_reg) {
		/*
		 * Perform an implict RELEASE if the registration that
		 * is being released is holding the reservation.
		 *
		 * From spc4r17, section 5.7.11.1:
		 *
		 * e) If the I_T nexus is the persistent reservation holder
		 *    and the persistent reservation is not an all registrants
		 *    type, then a PERSISTENT RESERVE OUT command with REGISTER
		 *    service action or REGISTER AND  IGNORE EXISTING KEY
		 *    service action with the SERVICE ACTION RESERVATION KEY
		 *    field set to zero (see 5.7.11.3).
		 */
		__core_scsi3_complete_pro_release(dev, nacl, pr_reg, 0);
		ret = 1;
		/*
		 * For 'All Registrants' reservation types, all existing
		 * registrations are still processed as reservation holders
		 * in core_scsi3_pr_seq_non_holder() after the initial
		 * reservation holder is implictly released here.
		 */
	} else if (pr_reg->pr_reg_all_tg_pt &&
		  (!strcmp(pr_res_holder->pr_reg_nacl->initiatorname,
			  pr_reg->pr_reg_nacl->initiatorname)) &&
		  (pr_res_holder->pr_res_key == pr_reg->pr_res_key)) {
		pr_err("SPC-3 PR: Unable to perform ALL_TG_PT=1"
			" UNREGISTER while existing reservation with matching"
			" key 0x%016Lx is present from another SCSI Initiator"
			" Port\n", pr_reg->pr_res_key);
		ret = -EPERM;
	}
	spin_unlock(&dev->dev_reservation_lock);

	return ret;
}

/*
 * Called with struct t10_reservation->registration_lock held.
 */
static void __core_scsi3_free_registration(
	struct se_device *dev,
	struct t10_pr_registration *pr_reg,
	struct list_head *preempt_and_abort_list,
	int dec_holders)
{
	struct target_core_fabric_ops *tfo =
			pr_reg->pr_reg_nacl->se_tpg->se_tpg_tfo;
	struct t10_reservation *pr_tmpl = &dev->t10_pr;
	char i_buf[PR_REG_ISID_ID_LEN];

	memset(i_buf, 0, PR_REG_ISID_ID_LEN);
	core_pr_dump_initiator_port(pr_reg, i_buf, PR_REG_ISID_ID_LEN);

	pr_reg->pr_reg_deve->def_pr_registered = 0;
	pr_reg->pr_reg_deve->pr_res_key = 0;
	list_del(&pr_reg->pr_reg_list);
	/*
	 * Caller accessing *pr_reg using core_scsi3_locate_pr_reg(),
	 * so call core_scsi3_put_pr_reg() to decrement our reference.
	 */
	if (dec_holders)
		core_scsi3_put_pr_reg(pr_reg);
	/*
	 * Wait until all reference from any other I_T nexuses for this
	 * *pr_reg have been released.  Because list_del() is called above,
	 * the last core_scsi3_put_pr_reg(pr_reg) will release this reference
	 * count back to zero, and we release *pr_reg.
	 */
	while (atomic_read(&pr_reg->pr_res_holders) != 0) {
		spin_unlock(&pr_tmpl->registration_lock);
		pr_debug("SPC-3 PR [%s] waiting for pr_res_holders\n",
				tfo->get_fabric_name());
		cpu_relax();
		spin_lock(&pr_tmpl->registration_lock);
	}

	pr_debug("SPC-3 PR [%s] Service Action: UNREGISTER Initiator"
		" Node: %s%s\n", tfo->get_fabric_name(),
		pr_reg->pr_reg_nacl->initiatorname,
		i_buf);
	pr_debug("SPC-3 PR [%s] for %s TCM Subsystem %s Object Target"
		" Port(s)\n", tfo->get_fabric_name(),
		(pr_reg->pr_reg_all_tg_pt) ? "ALL" : "SINGLE",
		dev->transport->name);
	pr_debug("SPC-3 PR [%s] SA Res Key: 0x%016Lx PRgeneration:"
		" 0x%08x\n", tfo->get_fabric_name(), pr_reg->pr_res_key,
		pr_reg->pr_res_generation);

	if (!preempt_and_abort_list) {
		pr_reg->pr_reg_deve = NULL;
		pr_reg->pr_reg_nacl = NULL;
		kmem_cache_free(t10_pr_reg_cache, pr_reg);
		return;
	}
	/*
	 * For PREEMPT_AND_ABORT, the list of *pr_reg in preempt_and_abort_list
	 * are released once the ABORT_TASK_SET has completed..
	 */
	list_add_tail(&pr_reg->pr_reg_abort_list, preempt_and_abort_list);
}

void core_scsi3_free_pr_reg_from_nacl(
	struct se_device *dev,
	struct se_node_acl *nacl)
{
	struct t10_reservation *pr_tmpl = &dev->t10_pr;
	struct t10_pr_registration *pr_reg, *pr_reg_tmp, *pr_res_holder;
	/*
	 * If the passed se_node_acl matches the reservation holder,
	 * release the reservation.
	 */
	spin_lock(&dev->dev_reservation_lock);
	pr_res_holder = dev->dev_pr_res_holder;
	if ((pr_res_holder != NULL) &&
	    (pr_res_holder->pr_reg_nacl == nacl))
		__core_scsi3_complete_pro_release(dev, nacl, pr_res_holder, 0);
	spin_unlock(&dev->dev_reservation_lock);
	/*
	 * Release any registration associated with the struct se_node_acl.
	 */
	spin_lock(&pr_tmpl->registration_lock);
	list_for_each_entry_safe(pr_reg, pr_reg_tmp,
			&pr_tmpl->registration_list, pr_reg_list) {

		if (pr_reg->pr_reg_nacl != nacl)
			continue;

		__core_scsi3_free_registration(dev, pr_reg, NULL, 0);
	}
	spin_unlock(&pr_tmpl->registration_lock);
}

void core_scsi3_free_all_registrations(
	struct se_device *dev)
{
	struct t10_reservation *pr_tmpl = &dev->t10_pr;
	struct t10_pr_registration *pr_reg, *pr_reg_tmp, *pr_res_holder;

	spin_lock(&dev->dev_reservation_lock);
	pr_res_holder = dev->dev_pr_res_holder;
	if (pr_res_holder != NULL) {
		struct se_node_acl *pr_res_nacl = pr_res_holder->pr_reg_nacl;
		__core_scsi3_complete_pro_release(dev, pr_res_nacl,
				pr_res_holder, 0);
	}
	spin_unlock(&dev->dev_reservation_lock);

	spin_lock(&pr_tmpl->registration_lock);
	list_for_each_entry_safe(pr_reg, pr_reg_tmp,
			&pr_tmpl->registration_list, pr_reg_list) {

		__core_scsi3_free_registration(dev, pr_reg, NULL, 0);
	}
	spin_unlock(&pr_tmpl->registration_lock);

	spin_lock(&pr_tmpl->aptpl_reg_lock);
	list_for_each_entry_safe(pr_reg, pr_reg_tmp, &pr_tmpl->aptpl_reg_list,
				pr_reg_aptpl_list) {
		list_del(&pr_reg->pr_reg_aptpl_list);
		kmem_cache_free(t10_pr_reg_cache, pr_reg);
	}
	spin_unlock(&pr_tmpl->aptpl_reg_lock);
}

static int core_scsi3_tpg_depend_item(struct se_portal_group *tpg)
{
	return configfs_depend_item(tpg->se_tpg_tfo->tf_subsys,
			&tpg->tpg_group.cg_item);
}

static void core_scsi3_tpg_undepend_item(struct se_portal_group *tpg)
{
	configfs_undepend_item(tpg->se_tpg_tfo->tf_subsys,
			&tpg->tpg_group.cg_item);

	atomic_dec(&tpg->tpg_pr_ref_count);
	smp_mb__after_atomic_dec();
}

static int core_scsi3_nodeacl_depend_item(struct se_node_acl *nacl)
{
	struct se_portal_group *tpg = nacl->se_tpg;

	if (nacl->dynamic_node_acl)
		return 0;

	return configfs_depend_item(tpg->se_tpg_tfo->tf_subsys,
			&nacl->acl_group.cg_item);
}

static void core_scsi3_nodeacl_undepend_item(struct se_node_acl *nacl)
{
	struct se_portal_group *tpg = nacl->se_tpg;

	if (nacl->dynamic_node_acl) {
		atomic_dec(&nacl->acl_pr_ref_count);
		smp_mb__after_atomic_dec();
		return;
	}

	configfs_undepend_item(tpg->se_tpg_tfo->tf_subsys,
			&nacl->acl_group.cg_item);

	atomic_dec(&nacl->acl_pr_ref_count);
	smp_mb__after_atomic_dec();
}

static int core_scsi3_lunacl_depend_item(struct se_dev_entry *se_deve)
{
	struct se_lun_acl *lun_acl = se_deve->se_lun_acl;
	struct se_node_acl *nacl;
	struct se_portal_group *tpg;
	/*
	 * For nacl->dynamic_node_acl=1
	 */
	if (!lun_acl)
		return 0;

	nacl = lun_acl->se_lun_nacl;
	tpg = nacl->se_tpg;

	return configfs_depend_item(tpg->se_tpg_tfo->tf_subsys,
			&lun_acl->se_lun_group.cg_item);
}

static void core_scsi3_lunacl_undepend_item(struct se_dev_entry *se_deve)
{
	struct se_lun_acl *lun_acl = se_deve->se_lun_acl;
	struct se_node_acl *nacl;
	struct se_portal_group *tpg;
	/*
	 * For nacl->dynamic_node_acl=1
	 */
	if (!lun_acl) {
		atomic_dec(&se_deve->pr_ref_count);
		smp_mb__after_atomic_dec();
		return;
	}
	nacl = lun_acl->se_lun_nacl;
	tpg = nacl->se_tpg;

	configfs_undepend_item(tpg->se_tpg_tfo->tf_subsys,
			&lun_acl->se_lun_group.cg_item);

	atomic_dec(&se_deve->pr_ref_count);
	smp_mb__after_atomic_dec();
}

static sense_reason_t
core_scsi3_decode_spec_i_port(
	struct se_cmd *cmd,
	struct se_portal_group *tpg,
	unsigned char *l_isid,
	u64 sa_res_key,
	int all_tg_pt,
	int aptpl)
{
	struct se_device *dev = cmd->se_dev;
	struct se_port *tmp_port;
	struct se_portal_group *dest_tpg = NULL, *tmp_tpg;
	struct se_session *se_sess = cmd->se_sess;
	struct se_node_acl *dest_node_acl = NULL;
	struct se_dev_entry *dest_se_deve = NULL, *local_se_deve;
	struct t10_pr_registration *dest_pr_reg, *local_pr_reg, *pr_reg_e;
	struct t10_pr_registration *pr_reg_tmp, *pr_reg_tmp_safe;
	LIST_HEAD(tid_dest_list);
	struct pr_transport_id_holder *tidh_new, *tidh, *tidh_tmp;
	struct target_core_fabric_ops *tmp_tf_ops;
	unsigned char *buf;
	unsigned char *ptr, *i_str = NULL, proto_ident, tmp_proto_ident;
	char *iport_ptr = NULL, dest_iport[64], i_buf[PR_REG_ISID_ID_LEN];
	sense_reason_t ret;
	u32 tpdl, tid_len = 0;
	int dest_local_nexus;
	u32 dest_rtpi = 0;

	memset(dest_iport, 0, 64);

	local_se_deve = se_sess->se_node_acl->device_list[cmd->orig_fe_lun];
	/*
	 * Allocate a struct pr_transport_id_holder and setup the
	 * local_node_acl and local_se_deve pointers and add to
	 * struct list_head tid_dest_list for add registration
	 * processing in the loop of tid_dest_list below.
	 */
	tidh_new = kzalloc(sizeof(struct pr_transport_id_holder), GFP_KERNEL);
	if (!tidh_new) {
		pr_err("Unable to allocate tidh_new\n");
		return TCM_LOGICAL_UNIT_COMMUNICATION_FAILURE;
	}
	INIT_LIST_HEAD(&tidh_new->dest_list);
	tidh_new->dest_tpg = tpg;
	tidh_new->dest_node_acl = se_sess->se_node_acl;
	tidh_new->dest_se_deve = local_se_deve;

	local_pr_reg = __core_scsi3_alloc_registration(cmd->se_dev,
				se_sess->se_node_acl, local_se_deve, l_isid,
				sa_res_key, all_tg_pt, aptpl);
	if (!local_pr_reg) {
		kfree(tidh_new);
		return TCM_LOGICAL_UNIT_COMMUNICATION_FAILURE;
	}
	tidh_new->dest_pr_reg = local_pr_reg;
	/*
	 * The local I_T nexus does not hold any configfs dependances,
	 * so we set tid_h->dest_local_nexus=1 to prevent the
	 * configfs_undepend_item() calls in the tid_dest_list loops below.
	 */
	tidh_new->dest_local_nexus = 1;
	list_add_tail(&tidh_new->dest_list, &tid_dest_list);

	if (cmd->data_length < 28) {
		pr_warn("SPC-PR: Received PR OUT parameter list"
			" length too small: %u\n", cmd->data_length);
		ret = TCM_INVALID_PARAMETER_LIST;
		goto out;
	}

	buf = transport_kmap_data_sg(cmd);
	if (!buf) {
		ret = TCM_LOGICAL_UNIT_COMMUNICATION_FAILURE;
		goto out;
	}

	/*
	 * For a PERSISTENT RESERVE OUT specify initiator ports payload,
	 * first extract TransportID Parameter Data Length, and make sure
	 * the value matches up to the SCSI expected data transfer length.
	 */
	tpdl = (buf[24] & 0xff) << 24;
	tpdl |= (buf[25] & 0xff) << 16;
	tpdl |= (buf[26] & 0xff) << 8;
	tpdl |= buf[27] & 0xff;

	if ((tpdl + 28) != cmd->data_length) {
		pr_err("SPC-3 PR: Illegal tpdl: %u + 28 byte header"
			" does not equal CDB data_length: %u\n", tpdl,
			cmd->data_length);
		ret = TCM_INVALID_PARAMETER_LIST;
		goto out_unmap;
	}
	/*
	 * Start processing the received transport IDs using the
	 * receiving I_T Nexus portal's fabric dependent methods to
	 * obtain the SCSI Initiator Port/Device Identifiers.
	 */
	ptr = &buf[28];

	while (tpdl > 0) {
		proto_ident = (ptr[0] & 0x0f);
		dest_tpg = NULL;

		spin_lock(&dev->se_port_lock);
		list_for_each_entry(tmp_port, &dev->dev_sep_list, sep_list) {
			tmp_tpg = tmp_port->sep_tpg;
			if (!tmp_tpg)
				continue;
			tmp_tf_ops = tmp_tpg->se_tpg_tfo;
			if (!tmp_tf_ops)
				continue;
			if (!tmp_tf_ops->get_fabric_proto_ident ||
			    !tmp_tf_ops->tpg_parse_pr_out_transport_id)
				continue;
			/*
			 * Look for the matching proto_ident provided by
			 * the received TransportID
			 */
			tmp_proto_ident = tmp_tf_ops->get_fabric_proto_ident(tmp_tpg);
			if (tmp_proto_ident != proto_ident)
				continue;
			dest_rtpi = tmp_port->sep_rtpi;

			i_str = tmp_tf_ops->tpg_parse_pr_out_transport_id(
					tmp_tpg, (const char *)ptr, &tid_len,
					&iport_ptr);
			if (!i_str)
				continue;

			atomic_inc(&tmp_tpg->tpg_pr_ref_count);
			smp_mb__after_atomic_inc();
			spin_unlock(&dev->se_port_lock);

			if (core_scsi3_tpg_depend_item(tmp_tpg)) {
				pr_err(" core_scsi3_tpg_depend_item()"
					" for tmp_tpg\n");
				atomic_dec(&tmp_tpg->tpg_pr_ref_count);
				smp_mb__after_atomic_dec();
				ret = TCM_LOGICAL_UNIT_COMMUNICATION_FAILURE;
				goto out_unmap;
			}
			/*
			 * Locate the destination initiator ACL to be registered
			 * from the decoded fabric module specific TransportID
			 * at *i_str.
			 */
			spin_lock_irq(&tmp_tpg->acl_node_lock);
			dest_node_acl = __core_tpg_get_initiator_node_acl(
						tmp_tpg, i_str);
			if (dest_node_acl) {
				atomic_inc(&dest_node_acl->acl_pr_ref_count);
				smp_mb__after_atomic_inc();
			}
			spin_unlock_irq(&tmp_tpg->acl_node_lock);

			if (!dest_node_acl) {
				core_scsi3_tpg_undepend_item(tmp_tpg);
				spin_lock(&dev->se_port_lock);
				continue;
			}

			if (core_scsi3_nodeacl_depend_item(dest_node_acl)) {
				pr_err("configfs_depend_item() failed"
					" for dest_node_acl->acl_group\n");
				atomic_dec(&dest_node_acl->acl_pr_ref_count);
				smp_mb__after_atomic_dec();
				core_scsi3_tpg_undepend_item(tmp_tpg);
				ret = TCM_LOGICAL_UNIT_COMMUNICATION_FAILURE;
				goto out_unmap;
			}

			dest_tpg = tmp_tpg;
			pr_debug("SPC-3 PR SPEC_I_PT: Located %s Node:"
				" %s Port RTPI: %hu\n",
				dest_tpg->se_tpg_tfo->get_fabric_name(),
				dest_node_acl->initiatorname, dest_rtpi);

			spin_lock(&dev->se_port_lock);
			break;
		}
		spin_unlock(&dev->se_port_lock);

		if (!dest_tpg) {
			pr_err("SPC-3 PR SPEC_I_PT: Unable to locate"
					" dest_tpg\n");
			ret = TCM_INVALID_PARAMETER_LIST;
			goto out_unmap;
		}

		pr_debug("SPC-3 PR SPEC_I_PT: Got %s data_length: %u tpdl: %u"
			" tid_len: %d for %s + %s\n",
			dest_tpg->se_tpg_tfo->get_fabric_name(), cmd->data_length,
			tpdl, tid_len, i_str, iport_ptr);

		if (tid_len > tpdl) {
			pr_err("SPC-3 PR SPEC_I_PT: Illegal tid_len:"
				" %u for Transport ID: %s\n", tid_len, ptr);
			core_scsi3_nodeacl_undepend_item(dest_node_acl);
			core_scsi3_tpg_undepend_item(dest_tpg);
			ret = TCM_INVALID_PARAMETER_LIST;
			goto out_unmap;
		}
		/*
		 * Locate the desintation struct se_dev_entry pointer for matching
		 * RELATIVE TARGET PORT IDENTIFIER on the receiving I_T Nexus
		 * Target Port.
		 */
		dest_se_deve = core_get_se_deve_from_rtpi(dest_node_acl,
					dest_rtpi);
		if (!dest_se_deve) {
			pr_err("Unable to locate %s dest_se_deve"
				" from destination RTPI: %hu\n",
				dest_tpg->se_tpg_tfo->get_fabric_name(),
				dest_rtpi);

			core_scsi3_nodeacl_undepend_item(dest_node_acl);
			core_scsi3_tpg_undepend_item(dest_tpg);
			ret = TCM_INVALID_PARAMETER_LIST;
			goto out_unmap;
		}

		if (core_scsi3_lunacl_depend_item(dest_se_deve)) {
			pr_err("core_scsi3_lunacl_depend_item()"
					" failed\n");
			atomic_dec(&dest_se_deve->pr_ref_count);
			smp_mb__after_atomic_dec();
			core_scsi3_nodeacl_undepend_item(dest_node_acl);
			core_scsi3_tpg_undepend_item(dest_tpg);
			ret = TCM_LOGICAL_UNIT_COMMUNICATION_FAILURE;
			goto out_unmap;
		}

		pr_debug("SPC-3 PR SPEC_I_PT: Located %s Node: %s"
			" dest_se_deve mapped_lun: %u\n",
			dest_tpg->se_tpg_tfo->get_fabric_name(),
			dest_node_acl->initiatorname, dest_se_deve->mapped_lun);

		/*
		 * Skip any TransportIDs that already have a registration for
		 * this target port.
		 */
		pr_reg_e = __core_scsi3_locate_pr_reg(dev, dest_node_acl,
					iport_ptr);
		if (pr_reg_e) {
			core_scsi3_put_pr_reg(pr_reg_e);
			core_scsi3_lunacl_undepend_item(dest_se_deve);
			core_scsi3_nodeacl_undepend_item(dest_node_acl);
			core_scsi3_tpg_undepend_item(dest_tpg);
			ptr += tid_len;
			tpdl -= tid_len;
			tid_len = 0;
			continue;
		}
		/*
		 * Allocate a struct pr_transport_id_holder and setup
		 * the dest_node_acl and dest_se_deve pointers for the
		 * loop below.
		 */
		tidh_new = kzalloc(sizeof(struct pr_transport_id_holder),
				GFP_KERNEL);
		if (!tidh_new) {
			pr_err("Unable to allocate tidh_new\n");
			core_scsi3_lunacl_undepend_item(dest_se_deve);
			core_scsi3_nodeacl_undepend_item(dest_node_acl);
			core_scsi3_tpg_undepend_item(dest_tpg);
			ret = TCM_LOGICAL_UNIT_COMMUNICATION_FAILURE;
			goto out_unmap;
		}
		INIT_LIST_HEAD(&tidh_new->dest_list);
		tidh_new->dest_tpg = dest_tpg;
		tidh_new->dest_node_acl = dest_node_acl;
		tidh_new->dest_se_deve = dest_se_deve;

		/*
		 * Allocate, but do NOT add the registration for the
		 * TransportID referenced SCSI Initiator port.  This
		 * done because of the following from spc4r17 in section
		 * 6.14.3 wrt SPEC_I_PT:
		 *
		 * "If a registration fails for any initiator port (e.g., if th
		 * logical unit does not have enough resources available to
		 * hold the registration information), no registrations shall be
		 * made, and the command shall be terminated with
		 * CHECK CONDITION status."
		 *
		 * That means we call __core_scsi3_alloc_registration() here,
		 * and then call __core_scsi3_add_registration() in the
		 * 2nd loop which will never fail.
		 */
		dest_pr_reg = __core_scsi3_alloc_registration(cmd->se_dev,
				dest_node_acl, dest_se_deve, iport_ptr,
				sa_res_key, all_tg_pt, aptpl);
		if (!dest_pr_reg) {
			core_scsi3_lunacl_undepend_item(dest_se_deve);
			core_scsi3_nodeacl_undepend_item(dest_node_acl);
			core_scsi3_tpg_undepend_item(dest_tpg);
			kfree(tidh_new);
			ret = TCM_INVALID_PARAMETER_LIST;
			goto out_unmap;
		}
		tidh_new->dest_pr_reg = dest_pr_reg;
		list_add_tail(&tidh_new->dest_list, &tid_dest_list);

		ptr += tid_len;
		tpdl -= tid_len;
		tid_len = 0;

	}

	transport_kunmap_data_sg(cmd);

	/*
	 * Go ahead and create a registrations from tid_dest_list for the
	 * SPEC_I_PT provided TransportID for the *tidh referenced dest_node_acl
	 * and dest_se_deve.
	 *
	 * The SA Reservation Key from the PROUT is set for the
	 * registration, and ALL_TG_PT is also passed.  ALL_TG_PT=1
	 * means that the TransportID Initiator port will be
	 * registered on all of the target ports in the SCSI target device
	 * ALL_TG_PT=0 means the registration will only be for the
	 * SCSI target port the PROUT REGISTER with SPEC_I_PT=1
	 * was received.
	 */
	list_for_each_entry_safe(tidh, tidh_tmp, &tid_dest_list, dest_list) {
		dest_tpg = tidh->dest_tpg;
		dest_node_acl = tidh->dest_node_acl;
		dest_se_deve = tidh->dest_se_deve;
		dest_pr_reg = tidh->dest_pr_reg;
		dest_local_nexus = tidh->dest_local_nexus;

		list_del(&tidh->dest_list);
		kfree(tidh);

		memset(i_buf, 0, PR_REG_ISID_ID_LEN);
		core_pr_dump_initiator_port(dest_pr_reg, i_buf, PR_REG_ISID_ID_LEN);

		__core_scsi3_add_registration(cmd->se_dev, dest_node_acl,
					dest_pr_reg, 0, 0);

		pr_debug("SPC-3 PR [%s] SPEC_I_PT: Successfully"
			" registered Transport ID for Node: %s%s Mapped LUN:"
			" %u\n", dest_tpg->se_tpg_tfo->get_fabric_name(),
			dest_node_acl->initiatorname, i_buf, dest_se_deve->mapped_lun);

		if (dest_local_nexus)
			continue;

		core_scsi3_lunacl_undepend_item(dest_se_deve);
		core_scsi3_nodeacl_undepend_item(dest_node_acl);
		core_scsi3_tpg_undepend_item(dest_tpg);
	}

	return 0;
out_unmap:
	transport_kunmap_data_sg(cmd);
out:
	/*
	 * For the failure case, release everything from tid_dest_list
	 * including *dest_pr_reg and the configfs dependances..
	 */
	list_for_each_entry_safe(tidh, tidh_tmp, &tid_dest_list, dest_list) {
		dest_tpg = tidh->dest_tpg;
		dest_node_acl = tidh->dest_node_acl;
		dest_se_deve = tidh->dest_se_deve;
		dest_pr_reg = tidh->dest_pr_reg;
		dest_local_nexus = tidh->dest_local_nexus;

		list_del(&tidh->dest_list);
		kfree(tidh);
		/*
		 * Release any extra ALL_TG_PT=1 registrations for
		 * the SPEC_I_PT=1 case.
		 */
		list_for_each_entry_safe(pr_reg_tmp, pr_reg_tmp_safe,
				&dest_pr_reg->pr_reg_atp_list,
				pr_reg_atp_mem_list) {
			list_del(&pr_reg_tmp->pr_reg_atp_mem_list);
			core_scsi3_lunacl_undepend_item(pr_reg_tmp->pr_reg_deve);
			kmem_cache_free(t10_pr_reg_cache, pr_reg_tmp);
		}

		kmem_cache_free(t10_pr_reg_cache, dest_pr_reg);

		if (dest_local_nexus)
			continue;

		core_scsi3_lunacl_undepend_item(dest_se_deve);
		core_scsi3_nodeacl_undepend_item(dest_node_acl);
		core_scsi3_tpg_undepend_item(dest_tpg);
	}
	return ret;
}

static int core_scsi3_update_aptpl_buf(
	struct se_device *dev,
	unsigned char *buf,
	u32 pr_aptpl_buf_len)
{
	struct se_lun *lun;
	struct se_portal_group *tpg;
	struct t10_pr_registration *pr_reg;
	unsigned char tmp[512], isid_buf[32];
	ssize_t len = 0;
	int reg_count = 0;
	int ret = 0;

	spin_lock(&dev->dev_reservation_lock);
	spin_lock(&dev->t10_pr.registration_lock);
	/*
	 * Walk the registration list..
	 */
	list_for_each_entry(pr_reg, &dev->t10_pr.registration_list,
			pr_reg_list) {

		tmp[0] = '\0';
		isid_buf[0] = '\0';
		tpg = pr_reg->pr_reg_nacl->se_tpg;
		lun = pr_reg->pr_reg_tg_pt_lun;
		/*
		 * Write out any ISID value to APTPL metadata that was included
		 * in the original registration.
		 */
		if (pr_reg->isid_present_at_reg)
			snprintf(isid_buf, 32, "initiator_sid=%s\n",
					pr_reg->pr_reg_isid);
		/*
		 * Include special metadata if the pr_reg matches the
		 * reservation holder.
		 */
		if (dev->dev_pr_res_holder == pr_reg) {
			snprintf(tmp, 512, "PR_REG_START: %d"
				"\ninitiator_fabric=%s\n"
				"initiator_node=%s\n%s"
				"sa_res_key=%llu\n"
				"res_holder=1\nres_type=%02x\n"
				"res_scope=%02x\nres_all_tg_pt=%d\n"
				"mapped_lun=%u\n", reg_count,
				tpg->se_tpg_tfo->get_fabric_name(),
				pr_reg->pr_reg_nacl->initiatorname, isid_buf,
				pr_reg->pr_res_key, pr_reg->pr_res_type,
				pr_reg->pr_res_scope, pr_reg->pr_reg_all_tg_pt,
				pr_reg->pr_res_mapped_lun);
		} else {
			snprintf(tmp, 512, "PR_REG_START: %d\n"
				"initiator_fabric=%s\ninitiator_node=%s\n%s"
				"sa_res_key=%llu\nres_holder=0\n"
				"res_all_tg_pt=%d\nmapped_lun=%u\n",
				reg_count, tpg->se_tpg_tfo->get_fabric_name(),
				pr_reg->pr_reg_nacl->initiatorname, isid_buf,
				pr_reg->pr_res_key, pr_reg->pr_reg_all_tg_pt,
				pr_reg->pr_res_mapped_lun);
		}

		if ((len + strlen(tmp) >= pr_aptpl_buf_len)) {
			pr_err("Unable to update renaming"
				" APTPL metadata\n");
			ret = -EMSGSIZE;
			goto out;
		}
		len += sprintf(buf+len, "%s", tmp);

		/*
		 * Include information about the associated SCSI target port.
		 */
		snprintf(tmp, 512, "target_fabric=%s\ntarget_node=%s\n"
			"tpgt=%hu\nport_rtpi=%hu\ntarget_lun=%u\nPR_REG_END:"
			" %d\n", tpg->se_tpg_tfo->get_fabric_name(),
			tpg->se_tpg_tfo->tpg_get_wwn(tpg),
			tpg->se_tpg_tfo->tpg_get_tag(tpg),
			lun->lun_sep->sep_rtpi, lun->unpacked_lun, reg_count);

		if ((len + strlen(tmp) >= pr_aptpl_buf_len)) {
			pr_err("Unable to update renaming"
				" APTPL metadata\n");
			ret = -EMSGSIZE;
			goto out;
		}
		len += sprintf(buf+len, "%s", tmp);
		reg_count++;
	}

	if (!reg_count)
		len += sprintf(buf+len, "No Registrations or Reservations");

out:
	spin_unlock(&dev->t10_pr.registration_lock);
	spin_unlock(&dev->dev_reservation_lock);

	return ret;
}

static int __core_scsi3_write_aptpl_to_file(
	struct se_device *dev,
	unsigned char *buf)
{
	struct t10_wwn *wwn = &dev->t10_wwn;
	struct file *file;
	int flags = O_RDWR | O_CREAT | O_TRUNC;
	char path[512];
	u32 pr_aptpl_buf_len;
	int ret;

	memset(path, 0, 512);

	if (strlen(&wwn->unit_serial[0]) >= 512) {
		pr_err("WWN value for struct se_device does not fit"
			" into path buffer\n");
		return -EMSGSIZE;
	}

	snprintf(path, 512, "/var/target/pr/aptpl_%s", &wwn->unit_serial[0]);
	file = filp_open(path, flags, 0600);
	if (IS_ERR(file)) {
		pr_err("filp_open(%s) for APTPL metadata"
			" failed\n", path);
		return PTR_ERR(file);
	}

	pr_aptpl_buf_len = (strlen(buf) + 1); /* Add extra for NULL */

	ret = kernel_write(file, buf, pr_aptpl_buf_len, 0);

	if (ret < 0)
		pr_debug("Error writing APTPL metadata file: %s\n", path);
	fput(file);

	return (ret < 0) ? -EIO : 0;
}

/*
 * Clear the APTPL metadata if APTPL has been disabled, otherwise
 * write out the updated metadata to struct file for this SCSI device.
 */
static sense_reason_t core_scsi3_update_and_write_aptpl(struct se_device *dev, bool aptpl)
{
	unsigned char *buf;
	int rc;

	if (!aptpl) {
		char *null_buf = "No Registrations or Reservations\n";

		rc = __core_scsi3_write_aptpl_to_file(dev, null_buf);
		dev->t10_pr.pr_aptpl_active = 0;
		pr_debug("SPC-3 PR: Set APTPL Bit Deactivated\n");

		if (rc)
			return TCM_LOGICAL_UNIT_COMMUNICATION_FAILURE;

		return 0;
	}

	buf = kzalloc(PR_APTPL_BUF_LEN, GFP_KERNEL);
	if (!buf)
		return TCM_OUT_OF_RESOURCES;

	rc = core_scsi3_update_aptpl_buf(dev, buf, PR_APTPL_BUF_LEN);
	if (rc < 0) {
		kfree(buf);
		return TCM_OUT_OF_RESOURCES;
	}

	rc = __core_scsi3_write_aptpl_to_file(dev, buf);
	if (rc != 0) {
		pr_err("SPC-3 PR: Could not update APTPL\n");
		kfree(buf);
		return TCM_LOGICAL_UNIT_COMMUNICATION_FAILURE;
	}
	dev->t10_pr.pr_aptpl_active = 1;
	kfree(buf);
	pr_debug("SPC-3 PR: Set APTPL Bit Activated\n");
	return 0;
}

static sense_reason_t
core_scsi3_emulate_pro_register(struct se_cmd *cmd, u64 res_key, u64 sa_res_key,
		bool aptpl, bool all_tg_pt, bool spec_i_pt, enum register_type register_type)
{
	struct se_session *se_sess = cmd->se_sess;
	struct se_device *dev = cmd->se_dev;
	struct se_dev_entry *se_deve;
	struct se_lun *se_lun = cmd->se_lun;
	struct se_portal_group *se_tpg;
	struct t10_pr_registration *pr_reg, *pr_reg_p, *pr_reg_tmp;
	struct t10_reservation *pr_tmpl = &dev->t10_pr;
	unsigned char isid_buf[PR_REG_ISID_LEN], *isid_ptr = NULL;
	sense_reason_t ret = TCM_NO_SENSE;
	int pr_holder = 0;

	if (!se_sess || !se_lun) {
		pr_err("SPC-3 PR: se_sess || struct se_lun is NULL!\n");
		return TCM_LOGICAL_UNIT_COMMUNICATION_FAILURE;
	}
	se_tpg = se_sess->se_tpg;
	se_deve = se_sess->se_node_acl->device_list[cmd->orig_fe_lun];

	if (se_tpg->se_tpg_tfo->sess_get_initiator_sid) {
		memset(&isid_buf[0], 0, PR_REG_ISID_LEN);
		se_tpg->se_tpg_tfo->sess_get_initiator_sid(se_sess, &isid_buf[0],
				PR_REG_ISID_LEN);
		isid_ptr = &isid_buf[0];
	}
	/*
	 * Follow logic from spc4r17 Section 5.7.7, Register Behaviors Table 47
	 */
	pr_reg = core_scsi3_locate_pr_reg(dev, se_sess->se_node_acl, se_sess);
	if (!pr_reg) {
		if (res_key) {
			pr_warn("SPC-3 PR: Reservation Key non-zero"
				" for SA REGISTER, returning CONFLICT\n");
			return TCM_RESERVATION_CONFLICT;
		}
		/*
		 * Do nothing but return GOOD status.
		 */
		if (!sa_res_key)
			return 0;

		if (!spec_i_pt) {
			/*
			 * Perform the Service Action REGISTER on the Initiator
			 * Port Endpoint that the PRO was received from on the
			 * Logical Unit of the SCSI device server.
			 */
			if (core_scsi3_alloc_registration(cmd->se_dev,
					se_sess->se_node_acl, se_deve, isid_ptr,
					sa_res_key, all_tg_pt, aptpl,
					register_type, 0)) {
				pr_err("Unable to allocate"
					" struct t10_pr_registration\n");
				return TCM_INVALID_PARAMETER_LIST;
			}
		} else {
			/*
			 * Register both the Initiator port that received
			 * PROUT SA REGISTER + SPEC_I_PT=1 and extract SCSI
			 * TransportID from Parameter list and loop through
			 * fabric dependent parameter list while calling
			 * logic from of core_scsi3_alloc_registration() for
			 * each TransportID provided SCSI Initiator Port/Device
			 */
			ret = core_scsi3_decode_spec_i_port(cmd, se_tpg,
					isid_ptr, sa_res_key, all_tg_pt, aptpl);
			if (ret != 0)
				return ret;
		}

		return core_scsi3_update_and_write_aptpl(dev, aptpl);
	}

	/* ok, existing registration */

	if ((register_type == REGISTER) && (res_key != pr_reg->pr_res_key)) {
		pr_err("SPC-3 PR REGISTER: Received"
		       " res_key: 0x%016Lx does not match"
		       " existing SA REGISTER res_key:"
		       " 0x%016Lx\n", res_key,
		       pr_reg->pr_res_key);
		ret = TCM_RESERVATION_CONFLICT;
		goto out;
	}

	if (spec_i_pt) {
		pr_err("SPC-3 PR REGISTER: SPEC_I_PT"
			" set on a registered nexus\n");
		ret = TCM_INVALID_PARAMETER_LIST;
		goto out;
	}

	/*
	 * An existing ALL_TG_PT=1 registration being released
	 * must also set ALL_TG_PT=1 in the incoming PROUT.
	 */
	if (pr_reg->pr_reg_all_tg_pt && !all_tg_pt) {
		pr_err("SPC-3 PR REGISTER: ALL_TG_PT=1"
			" registration exists, but ALL_TG_PT=1 bit not"
			" present in received PROUT\n");
		ret = TCM_INVALID_CDB_FIELD;
		goto out;
	}

	/*
	 * sa_res_key=1 Change Reservation Key for registered I_T Nexus.
	 */
	if (sa_res_key) {
		/*
		 * Increment PRgeneration counter for struct se_device"
		 * upon a successful REGISTER, see spc4r17 section 6.3.2
		 * READ_KEYS service action.
		 */
		pr_reg->pr_res_generation = core_scsi3_pr_generation(cmd->se_dev);
		pr_reg->pr_res_key = sa_res_key;
		pr_debug("SPC-3 PR [%s] REGISTER%s: Changed Reservation"
			 " Key for %s to: 0x%016Lx PRgeneration:"
			 " 0x%08x\n", cmd->se_tfo->get_fabric_name(),
			 (register_type == REGISTER_AND_IGNORE_EXISTING_KEY) ? "_AND_IGNORE_EXISTING_KEY" : "",
			 pr_reg->pr_reg_nacl->initiatorname,
			 pr_reg->pr_res_key, pr_reg->pr_res_generation);

	} else {
		/*
		 * sa_res_key=0 Unregister Reservation Key for registered I_T Nexus.
		 */
		pr_holder = core_scsi3_check_implict_release(
				cmd->se_dev, pr_reg);
		if (pr_holder < 0) {
			ret = TCM_RESERVATION_CONFLICT;
			goto out;
		}

		spin_lock(&pr_tmpl->registration_lock);
		/*
		 * Release all ALL_TG_PT=1 for the matching SCSI Initiator Port
		 * and matching pr_res_key.
		 */
		if (pr_reg->pr_reg_all_tg_pt) {
			list_for_each_entry_safe(pr_reg_p, pr_reg_tmp,
					&pr_tmpl->registration_list,
					pr_reg_list) {

				if (!pr_reg_p->pr_reg_all_tg_pt)
					continue;
				if (pr_reg_p->pr_res_key != res_key)
					continue;
				if (pr_reg == pr_reg_p)
					continue;
				if (strcmp(pr_reg->pr_reg_nacl->initiatorname,
					   pr_reg_p->pr_reg_nacl->initiatorname))
					continue;

				__core_scsi3_free_registration(dev,
						pr_reg_p, NULL, 0);
			}
		}

		/*
		 * Release the calling I_T Nexus registration now..
		 */
		__core_scsi3_free_registration(cmd->se_dev, pr_reg, NULL, 1);

		/*
		 * From spc4r17, section 5.7.11.3 Unregistering
		 *
		 * If the persistent reservation is a registrants only
		 * type, the device server shall establish a unit
		 * attention condition for the initiator port associated
		 * with every registered I_T nexus except for the I_T
		 * nexus on which the PERSISTENT RESERVE OUT command was
		 * received, with the additional sense code set to
		 * RESERVATIONS RELEASED.
		 */
		if (pr_holder &&
		    (pr_reg->pr_res_type == PR_TYPE_WRITE_EXCLUSIVE_REGONLY ||
		     pr_reg->pr_res_type == PR_TYPE_EXCLUSIVE_ACCESS_REGONLY)) {
			list_for_each_entry(pr_reg_p,
					&pr_tmpl->registration_list,
					pr_reg_list) {

				core_scsi3_ua_allocate(
					pr_reg_p->pr_reg_nacl,
					pr_reg_p->pr_res_mapped_lun,
					0x2A,
					ASCQ_2AH_RESERVATIONS_RELEASED);
			}
		}

		spin_unlock(&pr_tmpl->registration_lock);
	}

	ret = core_scsi3_update_and_write_aptpl(dev, aptpl);

out:
	core_scsi3_put_pr_reg(pr_reg);
	return ret;
}

unsigned char *core_scsi3_pr_dump_type(int type)
{
	switch (type) {
	case PR_TYPE_WRITE_EXCLUSIVE:
		return "Write Exclusive Access";
	case PR_TYPE_EXCLUSIVE_ACCESS:
		return "Exclusive Access";
	case PR_TYPE_WRITE_EXCLUSIVE_REGONLY:
		return "Write Exclusive Access, Registrants Only";
	case PR_TYPE_EXCLUSIVE_ACCESS_REGONLY:
		return "Exclusive Access, Registrants Only";
	case PR_TYPE_WRITE_EXCLUSIVE_ALLREG:
		return "Write Exclusive Access, All Registrants";
	case PR_TYPE_EXCLUSIVE_ACCESS_ALLREG:
		return "Exclusive Access, All Registrants";
	default:
		break;
	}

	return "Unknown SPC-3 PR Type";
}

static sense_reason_t
core_scsi3_pro_reserve(struct se_cmd *cmd, int type, int scope, u64 res_key)
{
	struct se_device *dev = cmd->se_dev;
	struct se_session *se_sess = cmd->se_sess;
	struct se_lun *se_lun = cmd->se_lun;
	struct t10_pr_registration *pr_reg, *pr_res_holder;
	struct t10_reservation *pr_tmpl = &dev->t10_pr;
	char i_buf[PR_REG_ISID_ID_LEN];
	sense_reason_t ret;

	memset(i_buf, 0, PR_REG_ISID_ID_LEN);

	if (!se_sess || !se_lun) {
		pr_err("SPC-3 PR: se_sess || struct se_lun is NULL!\n");
		return TCM_LOGICAL_UNIT_COMMUNICATION_FAILURE;
	}
	/*
	 * Locate the existing *pr_reg via struct se_node_acl pointers
	 */
	pr_reg = core_scsi3_locate_pr_reg(cmd->se_dev, se_sess->se_node_acl,
				se_sess);
	if (!pr_reg) {
		pr_err("SPC-3 PR: Unable to locate"
			" PR_REGISTERED *pr_reg for RESERVE\n");
		return TCM_LOGICAL_UNIT_COMMUNICATION_FAILURE;
	}
	/*
	 * From spc4r17 Section 5.7.9: Reserving:
	 *
	 * An application client creates a persistent reservation by issuing
	 * a PERSISTENT RESERVE OUT command with RESERVE service action through
	 * a registered I_T nexus with the following parameters:
	 *    a) RESERVATION KEY set to the value of the reservation key that is
	 * 	 registered with the logical unit for the I_T nexus; and
	 */
	if (res_key != pr_reg->pr_res_key) {
		pr_err("SPC-3 PR RESERVE: Received res_key: 0x%016Lx"
			" does not match existing SA REGISTER res_key:"
			" 0x%016Lx\n", res_key, pr_reg->pr_res_key);
		ret = TCM_RESERVATION_CONFLICT;
		goto out_put_pr_reg;
	}
	/*
	 * From spc4r17 Section 5.7.9: Reserving:
	 *
	 * From above:
	 *  b) TYPE field and SCOPE field set to the persistent reservation
	 *     being created.
	 *
	 * Only one persistent reservation is allowed at a time per logical unit
	 * and that persistent reservation has a scope of LU_SCOPE.
	 */
	if (scope != PR_SCOPE_LU_SCOPE) {
		pr_err("SPC-3 PR: Illegal SCOPE: 0x%02x\n", scope);
		ret = TCM_INVALID_PARAMETER_LIST;
		goto out_put_pr_reg;
	}
	/*
	 * See if we have an existing PR reservation holder pointer at
	 * struct se_device->dev_pr_res_holder in the form struct t10_pr_registration
	 * *pr_res_holder.
	 */
	spin_lock(&dev->dev_reservation_lock);
	pr_res_holder = dev->dev_pr_res_holder;
	if (pr_res_holder) {
		/*
		 * From spc4r17 Section 5.7.9: Reserving:
		 *
		 * If the device server receives a PERSISTENT RESERVE OUT
		 * command from an I_T nexus other than a persistent reservation
		 * holder (see 5.7.10) that attempts to create a persistent
		 * reservation when a persistent reservation already exists for
		 * the logical unit, then the command shall be completed with
		 * RESERVATION CONFLICT status.
		 */
		if (pr_res_holder != pr_reg) {
			struct se_node_acl *pr_res_nacl = pr_res_holder->pr_reg_nacl;
			pr_err("SPC-3 PR: Attempted RESERVE from"
				" [%s]: %s while reservation already held by"
				" [%s]: %s, returning RESERVATION_CONFLICT\n",
				cmd->se_tfo->get_fabric_name(),
				se_sess->se_node_acl->initiatorname,
				pr_res_nacl->se_tpg->se_tpg_tfo->get_fabric_name(),
				pr_res_holder->pr_reg_nacl->initiatorname);

			spin_unlock(&dev->dev_reservation_lock);
			ret = TCM_RESERVATION_CONFLICT;
			goto out_put_pr_reg;
		}
		/*
		 * From spc4r17 Section 5.7.9: Reserving:
		 *
		 * If a persistent reservation holder attempts to modify the
		 * type or scope of an existing persistent reservation, the
		 * command shall be completed with RESERVATION CONFLICT status.
		 */
		if ((pr_res_holder->pr_res_type != type) ||
		    (pr_res_holder->pr_res_scope != scope)) {
			struct se_node_acl *pr_res_nacl = pr_res_holder->pr_reg_nacl;
			pr_err("SPC-3 PR: Attempted RESERVE from"
				" [%s]: %s trying to change TYPE and/or SCOPE,"
				" while reservation already held by [%s]: %s,"
				" returning RESERVATION_CONFLICT\n",
				cmd->se_tfo->get_fabric_name(),
				se_sess->se_node_acl->initiatorname,
				pr_res_nacl->se_tpg->se_tpg_tfo->get_fabric_name(),
				pr_res_holder->pr_reg_nacl->initiatorname);

			spin_unlock(&dev->dev_reservation_lock);
			ret = TCM_RESERVATION_CONFLICT;
			goto out_put_pr_reg;
		}
		/*
		 * From spc4r17 Section 5.7.9: Reserving:
		 *
		 * If the device server receives a PERSISTENT RESERVE OUT
		 * command with RESERVE service action where the TYPE field and
		 * the SCOPE field contain the same values as the existing type
		 * and scope from a persistent reservation holder, it shall not
		 * make any change to the existing persistent reservation and
		 * shall completethe command with GOOD status.
		 */
		spin_unlock(&dev->dev_reservation_lock);
		ret = 0;
		goto out_put_pr_reg;
	}
	/*
	 * Otherwise, our *pr_reg becomes the PR reservation holder for said
	 * TYPE/SCOPE.  Also set the received scope and type in *pr_reg.
	 */
	pr_reg->pr_res_scope = scope;
	pr_reg->pr_res_type = type;
	pr_reg->pr_res_holder = 1;
	dev->dev_pr_res_holder = pr_reg;
	core_pr_dump_initiator_port(pr_reg, i_buf, PR_REG_ISID_ID_LEN);

	pr_debug("SPC-3 PR [%s] Service Action: RESERVE created new"
		" reservation holder TYPE: %s ALL_TG_PT: %d\n",
		cmd->se_tfo->get_fabric_name(), core_scsi3_pr_dump_type(type),
		(pr_reg->pr_reg_all_tg_pt) ? 1 : 0);
	pr_debug("SPC-3 PR [%s] RESERVE Node: %s%s\n",
			cmd->se_tfo->get_fabric_name(),
			se_sess->se_node_acl->initiatorname,
			i_buf);
	spin_unlock(&dev->dev_reservation_lock);

	if (pr_tmpl->pr_aptpl_active)
		core_scsi3_update_and_write_aptpl(cmd->se_dev, true);

	ret = 0;
out_put_pr_reg:
	core_scsi3_put_pr_reg(pr_reg);
	return ret;
}

static sense_reason_t
core_scsi3_emulate_pro_reserve(struct se_cmd *cmd, int type, int scope,
		u64 res_key)
{
	switch (type) {
	case PR_TYPE_WRITE_EXCLUSIVE:
	case PR_TYPE_EXCLUSIVE_ACCESS:
	case PR_TYPE_WRITE_EXCLUSIVE_REGONLY:
	case PR_TYPE_EXCLUSIVE_ACCESS_REGONLY:
	case PR_TYPE_WRITE_EXCLUSIVE_ALLREG:
	case PR_TYPE_EXCLUSIVE_ACCESS_ALLREG:
		return core_scsi3_pro_reserve(cmd, type, scope, res_key);
	default:
		pr_err("SPC-3 PR: Unknown Service Action RESERVE Type:"
			" 0x%02x\n", type);
		return TCM_INVALID_CDB_FIELD;
	}
}

/*
 * Called with struct se_device->dev_reservation_lock held.
 */
static void __core_scsi3_complete_pro_release(
	struct se_device *dev,
	struct se_node_acl *se_nacl,
	struct t10_pr_registration *pr_reg,
	int explict)
{
	struct target_core_fabric_ops *tfo = se_nacl->se_tpg->se_tpg_tfo;
	char i_buf[PR_REG_ISID_ID_LEN];

	memset(i_buf, 0, PR_REG_ISID_ID_LEN);
	core_pr_dump_initiator_port(pr_reg, i_buf, PR_REG_ISID_ID_LEN);
	/*
	 * Go ahead and release the current PR reservation holder.
	 */
	dev->dev_pr_res_holder = NULL;

	pr_debug("SPC-3 PR [%s] Service Action: %s RELEASE cleared"
		" reservation holder TYPE: %s ALL_TG_PT: %d\n",
		tfo->get_fabric_name(), (explict) ? "explict" : "implict",
		core_scsi3_pr_dump_type(pr_reg->pr_res_type),
		(pr_reg->pr_reg_all_tg_pt) ? 1 : 0);
	pr_debug("SPC-3 PR [%s] RELEASE Node: %s%s\n",
		tfo->get_fabric_name(), se_nacl->initiatorname,
		i_buf);
	/*
	 * Clear TYPE and SCOPE for the next PROUT Service Action: RESERVE
	 */
	pr_reg->pr_res_holder = pr_reg->pr_res_type = pr_reg->pr_res_scope = 0;
}

static sense_reason_t
core_scsi3_emulate_pro_release(struct se_cmd *cmd, int type, int scope,
		u64 res_key)
{
	struct se_device *dev = cmd->se_dev;
	struct se_session *se_sess = cmd->se_sess;
	struct se_lun *se_lun = cmd->se_lun;
	struct t10_pr_registration *pr_reg, *pr_reg_p, *pr_res_holder;
	struct t10_reservation *pr_tmpl = &dev->t10_pr;
	int all_reg = 0;
	sense_reason_t ret = 0;

	if (!se_sess || !se_lun) {
		pr_err("SPC-3 PR: se_sess || struct se_lun is NULL!\n");
		return TCM_LOGICAL_UNIT_COMMUNICATION_FAILURE;
	}
	/*
	 * Locate the existing *pr_reg via struct se_node_acl pointers
	 */
	pr_reg = core_scsi3_locate_pr_reg(dev, se_sess->se_node_acl, se_sess);
	if (!pr_reg) {
		pr_err("SPC-3 PR: Unable to locate"
			" PR_REGISTERED *pr_reg for RELEASE\n");
		return TCM_LOGICAL_UNIT_COMMUNICATION_FAILURE;
	}
	/*
	 * From spc4r17 Section 5.7.11.2 Releasing:
	 *
	 * If there is no persistent reservation or in response to a persistent
	 * reservation release request from a registered I_T nexus that is not a
	 * persistent reservation holder (see 5.7.10), the device server shall
	 * do the following:
	 *
	 *     a) Not release the persistent reservation, if any;
	 *     b) Not remove any registrations; and
	 *     c) Complete the command with GOOD status.
	 */
	spin_lock(&dev->dev_reservation_lock);
	pr_res_holder = dev->dev_pr_res_holder;
	if (!pr_res_holder) {
		/*
		 * No persistent reservation, return GOOD status.
		 */
		spin_unlock(&dev->dev_reservation_lock);
		goto out_put_pr_reg;
	}
	if ((pr_res_holder->pr_res_type == PR_TYPE_WRITE_EXCLUSIVE_ALLREG) ||
	    (pr_res_holder->pr_res_type == PR_TYPE_EXCLUSIVE_ACCESS_ALLREG))
		all_reg = 1;

	if ((all_reg == 0) && (pr_res_holder != pr_reg)) {
		/*
		 * Non 'All Registrants' PR Type cases..
		 * Release request from a registered I_T nexus that is not a
		 * persistent reservation holder. return GOOD status.
		 */
		spin_unlock(&dev->dev_reservation_lock);
		goto out_put_pr_reg;
	}

	/*
	 * From spc4r17 Section 5.7.11.2 Releasing:
	 *
	 * Only the persistent reservation holder (see 5.7.10) is allowed to
	 * release a persistent reservation.
	 *
	 * An application client releases the persistent reservation by issuing
	 * a PERSISTENT RESERVE OUT command with RELEASE service action through
	 * an I_T nexus that is a persistent reservation holder with the
	 * following parameters:
	 *
	 *     a) RESERVATION KEY field set to the value of the reservation key
	 *	  that is registered with the logical unit for the I_T nexus;
	 */
	if (res_key != pr_reg->pr_res_key) {
		pr_err("SPC-3 PR RELEASE: Received res_key: 0x%016Lx"
			" does not match existing SA REGISTER res_key:"
			" 0x%016Lx\n", res_key, pr_reg->pr_res_key);
		spin_unlock(&dev->dev_reservation_lock);
		ret = TCM_RESERVATION_CONFLICT;
		goto out_put_pr_reg;
	}
	/*
	 * From spc4r17 Section 5.7.11.2 Releasing and above:
	 *
	 * b) TYPE field and SCOPE field set to match the persistent
	 *    reservation being released.
	 */
	if ((pr_res_holder->pr_res_type != type) ||
	    (pr_res_holder->pr_res_scope != scope)) {
		struct se_node_acl *pr_res_nacl = pr_res_holder->pr_reg_nacl;
		pr_err("SPC-3 PR RELEASE: Attempted to release"
			" reservation from [%s]: %s with different TYPE "
			"and/or SCOPE  while reservation already held by"
			" [%s]: %s, returning RESERVATION_CONFLICT\n",
			cmd->se_tfo->get_fabric_name(),
			se_sess->se_node_acl->initiatorname,
			pr_res_nacl->se_tpg->se_tpg_tfo->get_fabric_name(),
			pr_res_holder->pr_reg_nacl->initiatorname);

		spin_unlock(&dev->dev_reservation_lock);
		ret = TCM_RESERVATION_CONFLICT;
		goto out_put_pr_reg;
	}
	/*
	 * In response to a persistent reservation release request from the
	 * persistent reservation holder the device server shall perform a
	 * release by doing the following as an uninterrupted series of actions:
	 * a) Release the persistent reservation;
	 * b) Not remove any registration(s);
	 * c) If the released persistent reservation is a registrants only type
	 * or all registrants type persistent reservation,
	 *    the device server shall establish a unit attention condition for
	 *    the initiator port associated with every regis-
	 *    tered I_T nexus other than I_T nexus on which the PERSISTENT
	 *    RESERVE OUT command with RELEASE service action was received,
	 *    with the additional sense code set to RESERVATIONS RELEASED; and
	 * d) If the persistent reservation is of any other type, the device
	 *    server shall not establish a unit attention condition.
	 */
	__core_scsi3_complete_pro_release(dev, se_sess->se_node_acl,
			pr_reg, 1);

	spin_unlock(&dev->dev_reservation_lock);

	if ((type != PR_TYPE_WRITE_EXCLUSIVE_REGONLY) &&
	    (type != PR_TYPE_EXCLUSIVE_ACCESS_REGONLY) &&
	    (type != PR_TYPE_WRITE_EXCLUSIVE_ALLREG) &&
	    (type != PR_TYPE_EXCLUSIVE_ACCESS_ALLREG)) {
		/*
		 * If no UNIT ATTENTION conditions will be established for
		 * PR_TYPE_WRITE_EXCLUSIVE or PR_TYPE_EXCLUSIVE_ACCESS
		 * go ahead and check for APTPL=1 update+write below
		 */
		goto write_aptpl;
	}

	spin_lock(&pr_tmpl->registration_lock);
	list_for_each_entry(pr_reg_p, &pr_tmpl->registration_list,
			pr_reg_list) {
		/*
		 * Do not establish a UNIT ATTENTION condition
		 * for the calling I_T Nexus
		 */
		if (pr_reg_p == pr_reg)
			continue;

		core_scsi3_ua_allocate(pr_reg_p->pr_reg_nacl,
				pr_reg_p->pr_res_mapped_lun,
				0x2A, ASCQ_2AH_RESERVATIONS_RELEASED);
	}
	spin_unlock(&pr_tmpl->registration_lock);

write_aptpl:
	if (pr_tmpl->pr_aptpl_active)
		core_scsi3_update_and_write_aptpl(cmd->se_dev, true);

out_put_pr_reg:
	core_scsi3_put_pr_reg(pr_reg);
	return ret;
}

static sense_reason_t
core_scsi3_emulate_pro_clear(struct se_cmd *cmd, u64 res_key)
{
	struct se_device *dev = cmd->se_dev;
	struct se_node_acl *pr_reg_nacl;
	struct se_session *se_sess = cmd->se_sess;
	struct t10_reservation *pr_tmpl = &dev->t10_pr;
	struct t10_pr_registration *pr_reg, *pr_reg_tmp, *pr_reg_n, *pr_res_holder;
	u32 pr_res_mapped_lun = 0;
	int calling_it_nexus = 0;
	/*
	 * Locate the existing *pr_reg via struct se_node_acl pointers
	 */
	pr_reg_n = core_scsi3_locate_pr_reg(cmd->se_dev,
			se_sess->se_node_acl, se_sess);
	if (!pr_reg_n) {
		pr_err("SPC-3 PR: Unable to locate"
			" PR_REGISTERED *pr_reg for CLEAR\n");
		return TCM_LOGICAL_UNIT_COMMUNICATION_FAILURE;
	}
	/*
	 * From spc4r17 section 5.7.11.6, Clearing:
	 *
	 * Any application client may release the persistent reservation and
	 * remove all registrations from a device server by issuing a
	 * PERSISTENT RESERVE OUT command with CLEAR service action through a
	 * registered I_T nexus with the following parameter:
	 *
	 *	a) RESERVATION KEY field set to the value of the reservation key
	 * 	   that is registered with the logical unit for the I_T nexus.
	 */
	if (res_key != pr_reg_n->pr_res_key) {
		pr_err("SPC-3 PR REGISTER: Received"
			" res_key: 0x%016Lx does not match"
			" existing SA REGISTER res_key:"
			" 0x%016Lx\n", res_key, pr_reg_n->pr_res_key);
		core_scsi3_put_pr_reg(pr_reg_n);
		return TCM_RESERVATION_CONFLICT;
	}
	/*
	 * a) Release the persistent reservation, if any;
	 */
	spin_lock(&dev->dev_reservation_lock);
	pr_res_holder = dev->dev_pr_res_holder;
	if (pr_res_holder) {
		struct se_node_acl *pr_res_nacl = pr_res_holder->pr_reg_nacl;
		__core_scsi3_complete_pro_release(dev, pr_res_nacl,
			pr_res_holder, 0);
	}
	spin_unlock(&dev->dev_reservation_lock);
	/*
	 * b) Remove all registration(s) (see spc4r17 5.7.7);
	 */
	spin_lock(&pr_tmpl->registration_lock);
	list_for_each_entry_safe(pr_reg, pr_reg_tmp,
			&pr_tmpl->registration_list, pr_reg_list) {

		calling_it_nexus = (pr_reg_n == pr_reg) ? 1 : 0;
		pr_reg_nacl = pr_reg->pr_reg_nacl;
		pr_res_mapped_lun = pr_reg->pr_res_mapped_lun;
		__core_scsi3_free_registration(dev, pr_reg, NULL,
					calling_it_nexus);
		/*
		 * e) Establish a unit attention condition for the initiator
		 *    port associated with every registered I_T nexus other
		 *    than the I_T nexus on which the PERSISTENT RESERVE OUT
		 *    command with CLEAR service action was received, with the
		 *    additional sense code set to RESERVATIONS PREEMPTED.
		 */
		if (!calling_it_nexus)
			core_scsi3_ua_allocate(pr_reg_nacl, pr_res_mapped_lun,
				0x2A, ASCQ_2AH_RESERVATIONS_PREEMPTED);
	}
	spin_unlock(&pr_tmpl->registration_lock);

	pr_debug("SPC-3 PR [%s] Service Action: CLEAR complete\n",
		cmd->se_tfo->get_fabric_name());

	core_scsi3_update_and_write_aptpl(cmd->se_dev, false);

	core_scsi3_pr_generation(dev);
	return 0;
}

/*
 * Called with struct se_device->dev_reservation_lock held.
 */
static void __core_scsi3_complete_pro_preempt(
	struct se_device *dev,
	struct t10_pr_registration *pr_reg,
	struct list_head *preempt_and_abort_list,
	int type,
	int scope,
	enum preempt_type preempt_type)
{
	struct se_node_acl *nacl = pr_reg->pr_reg_nacl;
	struct target_core_fabric_ops *tfo = nacl->se_tpg->se_tpg_tfo;
	char i_buf[PR_REG_ISID_ID_LEN];

	memset(i_buf, 0, PR_REG_ISID_ID_LEN);
	core_pr_dump_initiator_port(pr_reg, i_buf, PR_REG_ISID_ID_LEN);
	/*
	 * Do an implict RELEASE of the existing reservation.
	 */
	if (dev->dev_pr_res_holder)
		__core_scsi3_complete_pro_release(dev, nacl,
				dev->dev_pr_res_holder, 0);

	dev->dev_pr_res_holder = pr_reg;
	pr_reg->pr_res_holder = 1;
	pr_reg->pr_res_type = type;
	pr_reg->pr_res_scope = scope;

	pr_debug("SPC-3 PR [%s] Service Action: PREEMPT%s created new"
		" reservation holder TYPE: %s ALL_TG_PT: %d\n",
		tfo->get_fabric_name(), (preempt_type == PREEMPT_AND_ABORT) ? "_AND_ABORT" : "",
		core_scsi3_pr_dump_type(type),
		(pr_reg->pr_reg_all_tg_pt) ? 1 : 0);
	pr_debug("SPC-3 PR [%s] PREEMPT%s from Node: %s%s\n",
		tfo->get_fabric_name(), (preempt_type == PREEMPT_AND_ABORT) ? "_AND_ABORT" : "",
		nacl->initiatorname, i_buf);
	/*
	 * For PREEMPT_AND_ABORT, add the preempting reservation's
	 * struct t10_pr_registration to the list that will be compared
	 * against received CDBs..
	 */
	if (preempt_and_abort_list)
		list_add_tail(&pr_reg->pr_reg_abort_list,
				preempt_and_abort_list);
}

static void core_scsi3_release_preempt_and_abort(
	struct list_head *preempt_and_abort_list,
	struct t10_pr_registration *pr_reg_holder)
{
	struct t10_pr_registration *pr_reg, *pr_reg_tmp;

	list_for_each_entry_safe(pr_reg, pr_reg_tmp, preempt_and_abort_list,
				pr_reg_abort_list) {

		list_del(&pr_reg->pr_reg_abort_list);
		if (pr_reg_holder == pr_reg)
			continue;
		if (pr_reg->pr_res_holder) {
			pr_warn("pr_reg->pr_res_holder still set\n");
			continue;
		}

		pr_reg->pr_reg_deve = NULL;
		pr_reg->pr_reg_nacl = NULL;
		kmem_cache_free(t10_pr_reg_cache, pr_reg);
	}
}

static sense_reason_t
core_scsi3_pro_preempt(struct se_cmd *cmd, int type, int scope, u64 res_key,
		u64 sa_res_key, enum preempt_type preempt_type)
{
	struct se_device *dev = cmd->se_dev;
	struct se_node_acl *pr_reg_nacl;
	struct se_session *se_sess = cmd->se_sess;
	LIST_HEAD(preempt_and_abort_list);
	struct t10_pr_registration *pr_reg, *pr_reg_tmp, *pr_reg_n, *pr_res_holder;
	struct t10_reservation *pr_tmpl = &dev->t10_pr;
	u32 pr_res_mapped_lun = 0;
	int all_reg = 0, calling_it_nexus = 0, released_regs = 0;
	int prh_type = 0, prh_scope = 0;

	if (!se_sess)
		return TCM_LOGICAL_UNIT_COMMUNICATION_FAILURE;

	pr_reg_n = core_scsi3_locate_pr_reg(cmd->se_dev, se_sess->se_node_acl,
				se_sess);
	if (!pr_reg_n) {
		pr_err("SPC-3 PR: Unable to locate"
			" PR_REGISTERED *pr_reg for PREEMPT%s\n",
			(preempt_type == PREEMPT_AND_ABORT) ? "_AND_ABORT" : "");
		return TCM_RESERVATION_CONFLICT;
	}
	if (pr_reg_n->pr_res_key != res_key) {
		core_scsi3_put_pr_reg(pr_reg_n);
		return TCM_RESERVATION_CONFLICT;
	}
	if (scope != PR_SCOPE_LU_SCOPE) {
		pr_err("SPC-3 PR: Illegal SCOPE: 0x%02x\n", scope);
		core_scsi3_put_pr_reg(pr_reg_n);
		return TCM_INVALID_PARAMETER_LIST;
	}

	spin_lock(&dev->dev_reservation_lock);
	pr_res_holder = dev->dev_pr_res_holder;
	if (pr_res_holder &&
	   ((pr_res_holder->pr_res_type == PR_TYPE_WRITE_EXCLUSIVE_ALLREG) ||
	    (pr_res_holder->pr_res_type == PR_TYPE_EXCLUSIVE_ACCESS_ALLREG)))
		all_reg = 1;

	if (!all_reg && !sa_res_key) {
		spin_unlock(&dev->dev_reservation_lock);
		core_scsi3_put_pr_reg(pr_reg_n);
		return TCM_INVALID_PARAMETER_LIST;
	}
	/*
	 * From spc4r17, section 5.7.11.4.4 Removing Registrations:
	 *
	 * If the SERVICE ACTION RESERVATION KEY field does not identify a
	 * persistent reservation holder or there is no persistent reservation
	 * holder (i.e., there is no persistent reservation), then the device
	 * server shall perform a preempt by doing the following in an
	 * uninterrupted series of actions. (See below..)
	 */
	if (!pr_res_holder || (pr_res_holder->pr_res_key != sa_res_key)) {
		/*
		 * No existing or SA Reservation Key matching reservations..
		 *
		 * PROUT SA PREEMPT with All Registrant type reservations are
		 * allowed to be processed without a matching SA Reservation Key
		 */
		spin_lock(&pr_tmpl->registration_lock);
		list_for_each_entry_safe(pr_reg, pr_reg_tmp,
				&pr_tmpl->registration_list, pr_reg_list) {
			/*
			 * Removing of registrations in non all registrants
			 * type reservations without a matching SA reservation
			 * key.
			 *
			 * a) Remove the registrations for all I_T nexuses
			 *    specified by the SERVICE ACTION RESERVATION KEY
			 *    field;
			 * b) Ignore the contents of the SCOPE and TYPE fields;
			 * c) Process tasks as defined in 5.7.1; and
			 * d) Establish a unit attention condition for the
			 *    initiator port associated with every I_T nexus
			 *    that lost its registration other than the I_T
			 *    nexus on which the PERSISTENT RESERVE OUT command
			 *    was received, with the additional sense code set
			 *    to REGISTRATIONS PREEMPTED.
			 */
			if (!all_reg) {
				if (pr_reg->pr_res_key != sa_res_key)
					continue;

				calling_it_nexus = (pr_reg_n == pr_reg) ? 1 : 0;
				pr_reg_nacl = pr_reg->pr_reg_nacl;
				pr_res_mapped_lun = pr_reg->pr_res_mapped_lun;
				__core_scsi3_free_registration(dev, pr_reg,
					(preempt_type == PREEMPT_AND_ABORT) ? &preempt_and_abort_list :
						NULL, calling_it_nexus);
				released_regs++;
			} else {
				/*
				 * Case for any existing all registrants type
				 * reservation, follow logic in spc4r17 section
				 * 5.7.11.4 Preempting, Table 52 and Figure 7.
				 *
				 * For a ZERO SA Reservation key, release
				 * all other registrations and do an implict
				 * release of active persistent reservation.
				 *
				 * For a non-ZERO SA Reservation key, only
				 * release the matching reservation key from
				 * registrations.
				 */
				if ((sa_res_key) &&
				     (pr_reg->pr_res_key != sa_res_key))
					continue;

				calling_it_nexus = (pr_reg_n == pr_reg) ? 1 : 0;
				if (calling_it_nexus)
					continue;

				pr_reg_nacl = pr_reg->pr_reg_nacl;
				pr_res_mapped_lun = pr_reg->pr_res_mapped_lun;
				__core_scsi3_free_registration(dev, pr_reg,
					(preempt_type == PREEMPT_AND_ABORT) ? &preempt_and_abort_list :
						NULL, 0);
				released_regs++;
			}
			if (!calling_it_nexus)
				core_scsi3_ua_allocate(pr_reg_nacl,
					pr_res_mapped_lun, 0x2A,
					ASCQ_2AH_REGISTRATIONS_PREEMPTED);
		}
		spin_unlock(&pr_tmpl->registration_lock);
		/*
		 * If a PERSISTENT RESERVE OUT with a PREEMPT service action or
		 * a PREEMPT AND ABORT service action sets the SERVICE ACTION
		 * RESERVATION KEY field to a value that does not match any
		 * registered reservation key, then the device server shall
		 * complete the command with RESERVATION CONFLICT status.
		 */
		if (!released_regs) {
			spin_unlock(&dev->dev_reservation_lock);
			core_scsi3_put_pr_reg(pr_reg_n);
			return TCM_RESERVATION_CONFLICT;
		}
		/*
		 * For an existing all registrants type reservation
		 * with a zero SA rservation key, preempt the existing
		 * reservation with the new PR type and scope.
		 */
		if (pr_res_holder && all_reg && !(sa_res_key)) {
			__core_scsi3_complete_pro_preempt(dev, pr_reg_n,
				(preempt_type == PREEMPT_AND_ABORT) ? &preempt_and_abort_list : NULL,
				type, scope, preempt_type);

			if (preempt_type == PREEMPT_AND_ABORT)
				core_scsi3_release_preempt_and_abort(
					&preempt_and_abort_list, pr_reg_n);
		}
		spin_unlock(&dev->dev_reservation_lock);

		if (pr_tmpl->pr_aptpl_active)
			core_scsi3_update_and_write_aptpl(cmd->se_dev, true);

		core_scsi3_put_pr_reg(pr_reg_n);
		core_scsi3_pr_generation(cmd->se_dev);
		return 0;
	}
	/*
	 * The PREEMPTing SA reservation key matches that of the
	 * existing persistent reservation, first, we check if
	 * we are preempting our own reservation.
	 * From spc4r17, section 5.7.11.4.3 Preempting
	 * persistent reservations and registration handling
	 *
	 * If an all registrants persistent reservation is not
	 * present, it is not an error for the persistent
	 * reservation holder to preempt itself (i.e., a
	 * PERSISTENT RESERVE OUT with a PREEMPT service action
	 * or a PREEMPT AND ABORT service action with the
	 * SERVICE ACTION RESERVATION KEY value equal to the
	 * persistent reservation holder's reservation key that
	 * is received from the persistent reservation holder).
	 * In that case, the device server shall establish the
	 * new persistent reservation and maintain the
	 * registration.
	 */
	prh_type = pr_res_holder->pr_res_type;
	prh_scope = pr_res_holder->pr_res_scope;
	/*
	 * If the SERVICE ACTION RESERVATION KEY field identifies a
	 * persistent reservation holder (see 5.7.10), the device
	 * server shall perform a preempt by doing the following as
	 * an uninterrupted series of actions:
	 *
	 * a) Release the persistent reservation for the holder
	 *    identified by the SERVICE ACTION RESERVATION KEY field;
	 */
	if (pr_reg_n != pr_res_holder)
		__core_scsi3_complete_pro_release(dev,
				pr_res_holder->pr_reg_nacl,
				dev->dev_pr_res_holder, 0);
	/*
	 * b) Remove the registrations for all I_T nexuses identified
	 *    by the SERVICE ACTION RESERVATION KEY field, except the
	 *    I_T nexus that is being used for the PERSISTENT RESERVE
	 *    OUT command. If an all registrants persistent reservation
	 *    is present and the SERVICE ACTION RESERVATION KEY field
	 *    is set to zero, then all registrations shall be removed
	 *    except for that of the I_T nexus that is being used for
	 *    the PERSISTENT RESERVE OUT command;
	 */
	spin_lock(&pr_tmpl->registration_lock);
	list_for_each_entry_safe(pr_reg, pr_reg_tmp,
			&pr_tmpl->registration_list, pr_reg_list) {

		calling_it_nexus = (pr_reg_n == pr_reg) ? 1 : 0;
		if (calling_it_nexus)
			continue;

		if (pr_reg->pr_res_key != sa_res_key)
			continue;

		pr_reg_nacl = pr_reg->pr_reg_nacl;
		pr_res_mapped_lun = pr_reg->pr_res_mapped_lun;
		__core_scsi3_free_registration(dev, pr_reg,
				(preempt_type == PREEMPT_AND_ABORT) ? &preempt_and_abort_list : NULL,
				calling_it_nexus);
		/*
		 * e) Establish a unit attention condition for the initiator
		 *    port associated with every I_T nexus that lost its
		 *    persistent reservation and/or registration, with the
		 *    additional sense code set to REGISTRATIONS PREEMPTED;
		 */
		core_scsi3_ua_allocate(pr_reg_nacl, pr_res_mapped_lun, 0x2A,
				ASCQ_2AH_REGISTRATIONS_PREEMPTED);
	}
	spin_unlock(&pr_tmpl->registration_lock);
	/*
	 * c) Establish a persistent reservation for the preempting
	 *    I_T nexus using the contents of the SCOPE and TYPE fields;
	 */
	__core_scsi3_complete_pro_preempt(dev, pr_reg_n,
			(preempt_type == PREEMPT_AND_ABORT) ? &preempt_and_abort_list : NULL,
			type, scope, preempt_type);
	/*
	 * d) Process tasks as defined in 5.7.1;
	 * e) See above..
	 * f) If the type or scope has changed, then for every I_T nexus
	 *    whose reservation key was not removed, except for the I_T
	 *    nexus on which the PERSISTENT RESERVE OUT command was
	 *    received, the device server shall establish a unit
	 *    attention condition for the initiator port associated with
	 *    that I_T nexus, with the additional sense code set to
	 *    RESERVATIONS RELEASED. If the type or scope have not
	 *    changed, then no unit attention condition(s) shall be
	 *    established for this reason.
	 */
	if ((prh_type != type) || (prh_scope != scope)) {
		spin_lock(&pr_tmpl->registration_lock);
		list_for_each_entry_safe(pr_reg, pr_reg_tmp,
				&pr_tmpl->registration_list, pr_reg_list) {

			calling_it_nexus = (pr_reg_n == pr_reg) ? 1 : 0;
			if (calling_it_nexus)
				continue;

			core_scsi3_ua_allocate(pr_reg->pr_reg_nacl,
					pr_reg->pr_res_mapped_lun, 0x2A,
					ASCQ_2AH_RESERVATIONS_RELEASED);
		}
		spin_unlock(&pr_tmpl->registration_lock);
	}
	spin_unlock(&dev->dev_reservation_lock);
	/*
	 * Call LUN_RESET logic upon list of struct t10_pr_registration,
	 * All received CDBs for the matching existing reservation and
	 * registrations undergo ABORT_TASK logic.
	 *
	 * From there, core_scsi3_release_preempt_and_abort() will
	 * release every registration in the list (which have already
	 * been removed from the primary pr_reg list), except the
	 * new persistent reservation holder, the calling Initiator Port.
	 */
	if (preempt_type == PREEMPT_AND_ABORT) {
		core_tmr_lun_reset(dev, NULL, &preempt_and_abort_list, cmd);
		core_scsi3_release_preempt_and_abort(&preempt_and_abort_list,
						pr_reg_n);
	}

	if (pr_tmpl->pr_aptpl_active)
		core_scsi3_update_and_write_aptpl(cmd->se_dev, true);

	core_scsi3_put_pr_reg(pr_reg_n);
	core_scsi3_pr_generation(cmd->se_dev);
	return 0;
}

static sense_reason_t
core_scsi3_emulate_pro_preempt(struct se_cmd *cmd, int type, int scope,
		u64 res_key, u64 sa_res_key, enum preempt_type preempt_type)
{
	switch (type) {
	case PR_TYPE_WRITE_EXCLUSIVE:
	case PR_TYPE_EXCLUSIVE_ACCESS:
	case PR_TYPE_WRITE_EXCLUSIVE_REGONLY:
	case PR_TYPE_EXCLUSIVE_ACCESS_REGONLY:
	case PR_TYPE_WRITE_EXCLUSIVE_ALLREG:
	case PR_TYPE_EXCLUSIVE_ACCESS_ALLREG:
		return core_scsi3_pro_preempt(cmd, type, scope, res_key,
					      sa_res_key, preempt_type);
	default:
		pr_err("SPC-3 PR: Unknown Service Action PREEMPT%s"
			" Type: 0x%02x\n", (preempt_type == PREEMPT_AND_ABORT) ? "_AND_ABORT" : "", type);
		return TCM_INVALID_CDB_FIELD;
	}
}


static sense_reason_t
core_scsi3_emulate_pro_register_and_move(struct se_cmd *cmd, u64 res_key,
		u64 sa_res_key, int aptpl, int unreg)
{
	struct se_session *se_sess = cmd->se_sess;
	struct se_device *dev = cmd->se_dev;
	struct se_dev_entry *dest_se_deve = NULL;
	struct se_lun *se_lun = cmd->se_lun;
	struct se_node_acl *pr_res_nacl, *pr_reg_nacl, *dest_node_acl = NULL;
	struct se_port *se_port;
	struct se_portal_group *se_tpg, *dest_se_tpg = NULL;
	struct target_core_fabric_ops *dest_tf_ops = NULL, *tf_ops;
	struct t10_pr_registration *pr_reg, *pr_res_holder, *dest_pr_reg;
	struct t10_reservation *pr_tmpl = &dev->t10_pr;
	unsigned char *buf;
	unsigned char *initiator_str;
	char *iport_ptr = NULL, dest_iport[64], i_buf[PR_REG_ISID_ID_LEN];
	u32 tid_len, tmp_tid_len;
	int new_reg = 0, type, scope, matching_iname;
	sense_reason_t ret;
	unsigned short rtpi;
	unsigned char proto_ident;

	if (!se_sess || !se_lun) {
		pr_err("SPC-3 PR: se_sess || struct se_lun is NULL!\n");
		return TCM_LOGICAL_UNIT_COMMUNICATION_FAILURE;
	}

	memset(dest_iport, 0, 64);
	memset(i_buf, 0, PR_REG_ISID_ID_LEN);
	se_tpg = se_sess->se_tpg;
	tf_ops = se_tpg->se_tpg_tfo;
	/*
	 * Follow logic from spc4r17 Section 5.7.8, Table 50 --
	 *	Register behaviors for a REGISTER AND MOVE service action
	 *
	 * Locate the existing *pr_reg via struct se_node_acl pointers
	 */
	pr_reg = core_scsi3_locate_pr_reg(cmd->se_dev, se_sess->se_node_acl,
				se_sess);
	if (!pr_reg) {
		pr_err("SPC-3 PR: Unable to locate PR_REGISTERED"
			" *pr_reg for REGISTER_AND_MOVE\n");
		return TCM_LOGICAL_UNIT_COMMUNICATION_FAILURE;
	}
	/*
	 * The provided reservation key much match the existing reservation key
	 * provided during this initiator's I_T nexus registration.
	 */
	if (res_key != pr_reg->pr_res_key) {
		pr_warn("SPC-3 PR REGISTER_AND_MOVE: Received"
			" res_key: 0x%016Lx does not match existing SA REGISTER"
			" res_key: 0x%016Lx\n", res_key, pr_reg->pr_res_key);
		ret = TCM_RESERVATION_CONFLICT;
		goto out_put_pr_reg;
	}
	/*
	 * The service active reservation key needs to be non zero
	 */
	if (!sa_res_key) {
		pr_warn("SPC-3 PR REGISTER_AND_MOVE: Received zero"
			" sa_res_key\n");
		ret = TCM_INVALID_PARAMETER_LIST;
		goto out_put_pr_reg;
	}

	/*
	 * Determine the Relative Target Port Identifier where the reservation
	 * will be moved to for the TransportID containing SCSI initiator WWN
	 * information.
	 */
	buf = transport_kmap_data_sg(cmd);
	if (!buf) {
		ret = TCM_LOGICAL_UNIT_COMMUNICATION_FAILURE;
		goto out_put_pr_reg;
	}

	rtpi = (buf[18] & 0xff) << 8;
	rtpi |= buf[19] & 0xff;
	tid_len = (buf[20] & 0xff) << 24;
	tid_len |= (buf[21] & 0xff) << 16;
	tid_len |= (buf[22] & 0xff) << 8;
	tid_len |= buf[23] & 0xff;
	transport_kunmap_data_sg(cmd);
	buf = NULL;

	if ((tid_len + 24) != cmd->data_length) {
		pr_err("SPC-3 PR: Illegal tid_len: %u + 24 byte header"
			" does not equal CDB data_length: %u\n", tid_len,
			cmd->data_length);
		ret = TCM_INVALID_PARAMETER_LIST;
		goto out_put_pr_reg;
	}

	spin_lock(&dev->se_port_lock);
	list_for_each_entry(se_port, &dev->dev_sep_list, sep_list) {
		if (se_port->sep_rtpi != rtpi)
			continue;
		dest_se_tpg = se_port->sep_tpg;
		if (!dest_se_tpg)
			continue;
		dest_tf_ops = dest_se_tpg->se_tpg_tfo;
		if (!dest_tf_ops)
			continue;

		atomic_inc(&dest_se_tpg->tpg_pr_ref_count);
		smp_mb__after_atomic_inc();
		spin_unlock(&dev->se_port_lock);

		if (core_scsi3_tpg_depend_item(dest_se_tpg)) {
			pr_err("core_scsi3_tpg_depend_item() failed"
				" for dest_se_tpg\n");
			atomic_dec(&dest_se_tpg->tpg_pr_ref_count);
			smp_mb__after_atomic_dec();
			ret = TCM_LOGICAL_UNIT_COMMUNICATION_FAILURE;
			goto out_put_pr_reg;
		}

		spin_lock(&dev->se_port_lock);
		break;
	}
	spin_unlock(&dev->se_port_lock);

	if (!dest_se_tpg || !dest_tf_ops) {
		pr_err("SPC-3 PR REGISTER_AND_MOVE: Unable to locate"
			" fabric ops from Relative Target Port Identifier:"
			" %hu\n", rtpi);
		ret = TCM_INVALID_PARAMETER_LIST;
		goto out_put_pr_reg;
	}

	buf = transport_kmap_data_sg(cmd);
	if (!buf) {
		ret = TCM_LOGICAL_UNIT_COMMUNICATION_FAILURE;
		goto out_put_pr_reg;
	}
	proto_ident = (buf[24] & 0x0f);

	pr_debug("SPC-3 PR REGISTER_AND_MOVE: Extracted Protocol Identifier:"
			" 0x%02x\n", proto_ident);

	if (proto_ident != dest_tf_ops->get_fabric_proto_ident(dest_se_tpg)) {
		pr_err("SPC-3 PR REGISTER_AND_MOVE: Received"
			" proto_ident: 0x%02x does not match ident: 0x%02x"
			" from fabric: %s\n", proto_ident,
			dest_tf_ops->get_fabric_proto_ident(dest_se_tpg),
			dest_tf_ops->get_fabric_name());
		ret = TCM_INVALID_PARAMETER_LIST;
		goto out;
	}
	if (dest_tf_ops->tpg_parse_pr_out_transport_id == NULL) {
		pr_err("SPC-3 PR REGISTER_AND_MOVE: Fabric does not"
			" containg a valid tpg_parse_pr_out_transport_id"
			" function pointer\n");
		ret = TCM_LOGICAL_UNIT_COMMUNICATION_FAILURE;
		goto out;
	}
	initiator_str = dest_tf_ops->tpg_parse_pr_out_transport_id(dest_se_tpg,
			(const char *)&buf[24], &tmp_tid_len, &iport_ptr);
	if (!initiator_str) {
		pr_err("SPC-3 PR REGISTER_AND_MOVE: Unable to locate"
			" initiator_str from Transport ID\n");
		ret = TCM_INVALID_PARAMETER_LIST;
		goto out;
	}

	transport_kunmap_data_sg(cmd);
	buf = NULL;

	pr_debug("SPC-3 PR [%s] Extracted initiator %s identifier: %s"
		" %s\n", dest_tf_ops->get_fabric_name(), (iport_ptr != NULL) ?
		"port" : "device", initiator_str, (iport_ptr != NULL) ?
		iport_ptr : "");
	/*
	 * If a PERSISTENT RESERVE OUT command with a REGISTER AND MOVE service
	 * action specifies a TransportID that is the same as the initiator port
	 * of the I_T nexus for the command received, then the command shall
	 * be terminated with CHECK CONDITION status, with the sense key set to
	 * ILLEGAL REQUEST, and the additional sense code set to INVALID FIELD
	 * IN PARAMETER LIST.
	 */
	pr_reg_nacl = pr_reg->pr_reg_nacl;
	matching_iname = (!strcmp(initiator_str,
				  pr_reg_nacl->initiatorname)) ? 1 : 0;
	if (!matching_iname)
		goto after_iport_check;

	if (!iport_ptr || !pr_reg->isid_present_at_reg) {
		pr_err("SPC-3 PR REGISTER_AND_MOVE: TransportID: %s"
			" matches: %s on received I_T Nexus\n", initiator_str,
			pr_reg_nacl->initiatorname);
		ret = TCM_INVALID_PARAMETER_LIST;
		goto out;
	}
	if (!strcmp(iport_ptr, pr_reg->pr_reg_isid)) {
		pr_err("SPC-3 PR REGISTER_AND_MOVE: TransportID: %s %s"
			" matches: %s %s on received I_T Nexus\n",
			initiator_str, iport_ptr, pr_reg_nacl->initiatorname,
			pr_reg->pr_reg_isid);
		ret = TCM_INVALID_PARAMETER_LIST;
		goto out;
	}
after_iport_check:
	/*
	 * Locate the destination struct se_node_acl from the received Transport ID
	 */
	spin_lock_irq(&dest_se_tpg->acl_node_lock);
	dest_node_acl = __core_tpg_get_initiator_node_acl(dest_se_tpg,
				initiator_str);
	if (dest_node_acl) {
		atomic_inc(&dest_node_acl->acl_pr_ref_count);
		smp_mb__after_atomic_inc();
	}
	spin_unlock_irq(&dest_se_tpg->acl_node_lock);

	if (!dest_node_acl) {
		pr_err("Unable to locate %s dest_node_acl for"
			" TransportID%s\n", dest_tf_ops->get_fabric_name(),
			initiator_str);
		ret = TCM_INVALID_PARAMETER_LIST;
		goto out;
	}

	if (core_scsi3_nodeacl_depend_item(dest_node_acl)) {
		pr_err("core_scsi3_nodeacl_depend_item() for"
			" dest_node_acl\n");
		atomic_dec(&dest_node_acl->acl_pr_ref_count);
		smp_mb__after_atomic_dec();
		dest_node_acl = NULL;
		ret = TCM_INVALID_PARAMETER_LIST;
		goto out;
	}

	pr_debug("SPC-3 PR REGISTER_AND_MOVE: Found %s dest_node_acl:"
		" %s from TransportID\n", dest_tf_ops->get_fabric_name(),
		dest_node_acl->initiatorname);

	/*
	 * Locate the struct se_dev_entry pointer for the matching RELATIVE TARGET
	 * PORT IDENTIFIER.
	 */
	dest_se_deve = core_get_se_deve_from_rtpi(dest_node_acl, rtpi);
	if (!dest_se_deve) {
		pr_err("Unable to locate %s dest_se_deve from RTPI:"
			" %hu\n",  dest_tf_ops->get_fabric_name(), rtpi);
		ret = TCM_INVALID_PARAMETER_LIST;
		goto out;
	}

	if (core_scsi3_lunacl_depend_item(dest_se_deve)) {
		pr_err("core_scsi3_lunacl_depend_item() failed\n");
		atomic_dec(&dest_se_deve->pr_ref_count);
		smp_mb__after_atomic_dec();
		dest_se_deve = NULL;
		ret = TCM_LOGICAL_UNIT_COMMUNICATION_FAILURE;
		goto out;
	}

	pr_debug("SPC-3 PR REGISTER_AND_MOVE: Located %s node %s LUN"
		" ACL for dest_se_deve->mapped_lun: %u\n",
		dest_tf_ops->get_fabric_name(), dest_node_acl->initiatorname,
		dest_se_deve->mapped_lun);

	/*
	 * A persistent reservation needs to already existing in order to
	 * successfully complete the REGISTER_AND_MOVE service action..
	 */
	spin_lock(&dev->dev_reservation_lock);
	pr_res_holder = dev->dev_pr_res_holder;
	if (!pr_res_holder) {
		pr_warn("SPC-3 PR REGISTER_AND_MOVE: No reservation"
			" currently held\n");
		spin_unlock(&dev->dev_reservation_lock);
		ret = TCM_INVALID_CDB_FIELD;
		goto out;
	}
	/*
	 * The received on I_T Nexus must be the reservation holder.
	 *
	 * From spc4r17 section 5.7.8  Table 50 --
	 * 	Register behaviors for a REGISTER AND MOVE service action
	 */
	if (pr_res_holder != pr_reg) {
		pr_warn("SPC-3 PR REGISTER_AND_MOVE: Calling I_T"
			" Nexus is not reservation holder\n");
		spin_unlock(&dev->dev_reservation_lock);
		ret = TCM_RESERVATION_CONFLICT;
		goto out;
	}
	/*
	 * From spc4r17 section 5.7.8: registering and moving reservation
	 *
	 * If a PERSISTENT RESERVE OUT command with a REGISTER AND MOVE service
	 * action is received and the established persistent reservation is a
	 * Write Exclusive - All Registrants type or Exclusive Access -
	 * All Registrants type reservation, then the command shall be completed
	 * with RESERVATION CONFLICT status.
	 */
	if ((pr_res_holder->pr_res_type == PR_TYPE_WRITE_EXCLUSIVE_ALLREG) ||
	    (pr_res_holder->pr_res_type == PR_TYPE_EXCLUSIVE_ACCESS_ALLREG)) {
		pr_warn("SPC-3 PR REGISTER_AND_MOVE: Unable to move"
			" reservation for type: %s\n",
			core_scsi3_pr_dump_type(pr_res_holder->pr_res_type));
		spin_unlock(&dev->dev_reservation_lock);
		ret = TCM_RESERVATION_CONFLICT;
		goto out;
	}
	pr_res_nacl = pr_res_holder->pr_reg_nacl;
	/*
	 * b) Ignore the contents of the (received) SCOPE and TYPE fields;
	 */
	type = pr_res_holder->pr_res_type;
	scope = pr_res_holder->pr_res_type;
	/*
	 * c) Associate the reservation key specified in the SERVICE ACTION
	 *    RESERVATION KEY field with the I_T nexus specified as the
	 *    destination of the register and move, where:
	 *    A) The I_T nexus is specified by the TransportID and the
	 *	 RELATIVE TARGET PORT IDENTIFIER field (see 6.14.4); and
	 *    B) Regardless of the TransportID format used, the association for
	 *       the initiator port is based on either the initiator port name
	 *       (see 3.1.71) on SCSI transport protocols where port names are
	 *       required or the initiator port identifier (see 3.1.70) on SCSI
	 *       transport protocols where port names are not required;
	 * d) Register the reservation key specified in the SERVICE ACTION
	 *    RESERVATION KEY field;
	 * e) Retain the reservation key specified in the SERVICE ACTION
	 *    RESERVATION KEY field and associated information;
	 *
	 * Also, It is not an error for a REGISTER AND MOVE service action to
	 * register an I_T nexus that is already registered with the same
	 * reservation key or a different reservation key.
	 */
	dest_pr_reg = __core_scsi3_locate_pr_reg(dev, dest_node_acl,
					iport_ptr);
	if (!dest_pr_reg) {
		if (core_scsi3_alloc_registration(cmd->se_dev,
				dest_node_acl, dest_se_deve, iport_ptr,
				sa_res_key, 0, aptpl, 2, 1)) {
			spin_unlock(&dev->dev_reservation_lock);
			ret = TCM_INVALID_PARAMETER_LIST;
			goto out;
		}
		dest_pr_reg = __core_scsi3_locate_pr_reg(dev, dest_node_acl,
						iport_ptr);
		new_reg = 1;
	}
	/*
	 * f) Release the persistent reservation for the persistent reservation
	 *    holder (i.e., the I_T nexus on which the
	 */
	__core_scsi3_complete_pro_release(dev, pr_res_nacl,
			dev->dev_pr_res_holder, 0);
	/*
	 * g) Move the persistent reservation to the specified I_T nexus using
	 *    the same scope and type as the persistent reservation released in
	 *    item f); and
	 */
	dev->dev_pr_res_holder = dest_pr_reg;
	dest_pr_reg->pr_res_holder = 1;
	dest_pr_reg->pr_res_type = type;
	pr_reg->pr_res_scope = scope;
	core_pr_dump_initiator_port(pr_reg, i_buf, PR_REG_ISID_ID_LEN);
	/*
	 * Increment PRGeneration for existing registrations..
	 */
	if (!new_reg)
		dest_pr_reg->pr_res_generation = pr_tmpl->pr_generation++;
	spin_unlock(&dev->dev_reservation_lock);

	pr_debug("SPC-3 PR [%s] Service Action: REGISTER_AND_MOVE"
		" created new reservation holder TYPE: %s on object RTPI:"
		" %hu  PRGeneration: 0x%08x\n", dest_tf_ops->get_fabric_name(),
		core_scsi3_pr_dump_type(type), rtpi,
		dest_pr_reg->pr_res_generation);
	pr_debug("SPC-3 PR Successfully moved reservation from"
		" %s Fabric Node: %s%s -> %s Fabric Node: %s %s\n",
		tf_ops->get_fabric_name(), pr_reg_nacl->initiatorname,
		i_buf, dest_tf_ops->get_fabric_name(),
		dest_node_acl->initiatorname, (iport_ptr != NULL) ?
		iport_ptr : "");
	/*
	 * It is now safe to release configfs group dependencies for destination
	 * of Transport ID Initiator Device/Port Identifier
	 */
	core_scsi3_lunacl_undepend_item(dest_se_deve);
	core_scsi3_nodeacl_undepend_item(dest_node_acl);
	core_scsi3_tpg_undepend_item(dest_se_tpg);
	/*
	 * h) If the UNREG bit is set to one, unregister (see 5.7.11.3) the I_T
	 * nexus on which PERSISTENT RESERVE OUT command was received.
	 */
	if (unreg) {
		spin_lock(&pr_tmpl->registration_lock);
		__core_scsi3_free_registration(dev, pr_reg, NULL, 1);
		spin_unlock(&pr_tmpl->registration_lock);
	} else
		core_scsi3_put_pr_reg(pr_reg);

	core_scsi3_update_and_write_aptpl(cmd->se_dev, aptpl);

	transport_kunmap_data_sg(cmd);

	core_scsi3_put_pr_reg(dest_pr_reg);
	return 0;
out:
	if (buf)
		transport_kunmap_data_sg(cmd);
	if (dest_se_deve)
		core_scsi3_lunacl_undepend_item(dest_se_deve);
	if (dest_node_acl)
		core_scsi3_nodeacl_undepend_item(dest_node_acl);
	core_scsi3_tpg_undepend_item(dest_se_tpg);

out_put_pr_reg:
	core_scsi3_put_pr_reg(pr_reg);
	return ret;
}

static unsigned long long core_scsi3_extract_reservation_key(unsigned char *cdb)
{
	unsigned int __v1, __v2;

	__v1 = (cdb[0] << 24) | (cdb[1] << 16) | (cdb[2] << 8) | cdb[3];
	__v2 = (cdb[4] << 24) | (cdb[5] << 16) | (cdb[6] << 8) | cdb[7];

	return ((unsigned long long)__v2) | (unsigned long long)__v1 << 32;
}

/*
 * See spc4r17 section 6.14 Table 170
 */
sense_reason_t
target_scsi3_emulate_pr_out(struct se_cmd *cmd)
{
	unsigned char *cdb = &cmd->t_task_cdb[0];
	unsigned char *buf;
	u64 res_key, sa_res_key;
	int sa, scope, type, aptpl;
	int spec_i_pt = 0, all_tg_pt = 0, unreg = 0;
	sense_reason_t ret;

	/*
	 * Following spc2r20 5.5.1 Reservations overview:
	 *
	 * If a logical unit has been reserved by any RESERVE command and is
	 * still reserved by any initiator, all PERSISTENT RESERVE IN and all
	 * PERSISTENT RESERVE OUT commands shall conflict regardless of
	 * initiator or service action and shall terminate with a RESERVATION
	 * CONFLICT status.
	 */
	if (cmd->se_dev->dev_reservation_flags & DRF_SPC2_RESERVATIONS) {
		pr_err("Received PERSISTENT_RESERVE CDB while legacy"
			" SPC-2 reservation is held, returning"
			" RESERVATION_CONFLICT\n");
		return TCM_RESERVATION_CONFLICT;
	}

	/*
	 * FIXME: A NULL struct se_session pointer means an this is not coming from
	 * a $FABRIC_MOD's nexus, but from internal passthrough ops.
	 */
	if (!cmd->se_sess)
		return TCM_LOGICAL_UNIT_COMMUNICATION_FAILURE;

	if (cmd->data_length < 24) {
		pr_warn("SPC-PR: Received PR OUT parameter list"
			" length too small: %u\n", cmd->data_length);
		return TCM_INVALID_PARAMETER_LIST;
	}

	/*
	 * From the PERSISTENT_RESERVE_OUT command descriptor block (CDB)
	 */
	sa = (cdb[1] & 0x1f);
	scope = (cdb[2] & 0xf0);
	type = (cdb[2] & 0x0f);

	buf = transport_kmap_data_sg(cmd);
	if (!buf)
		return TCM_LOGICAL_UNIT_COMMUNICATION_FAILURE;

	/*
	 * From PERSISTENT_RESERVE_OUT parameter list (payload)
	 */
	res_key = core_scsi3_extract_reservation_key(&buf[0]);
	sa_res_key = core_scsi3_extract_reservation_key(&buf[8]);
	/*
	 * REGISTER_AND_MOVE uses a different SA parameter list containing
	 * SCSI TransportIDs.
	 */
	if (sa != PRO_REGISTER_AND_MOVE) {
		spec_i_pt = (buf[20] & 0x08);
		all_tg_pt = (buf[20] & 0x04);
		aptpl = (buf[20] & 0x01);
	} else {
		aptpl = (buf[17] & 0x01);
		unreg = (buf[17] & 0x02);
	}
	transport_kunmap_data_sg(cmd);
	buf = NULL;

	/*
	 * SPEC_I_PT=1 is only valid for Service action: REGISTER
	 */
	if (spec_i_pt && ((cdb[1] & 0x1f) != PRO_REGISTER))
		return TCM_INVALID_PARAMETER_LIST;

	/*
	 * From spc4r17 section 6.14:
	 *
	 * If the SPEC_I_PT bit is set to zero, the service action is not
	 * REGISTER AND MOVE, and the parameter list length is not 24, then
	 * the command shall be terminated with CHECK CONDITION status, with
	 * the sense key set to ILLEGAL REQUEST, and the additional sense
	 * code set to PARAMETER LIST LENGTH ERROR.
	 */
	if (!spec_i_pt && ((cdb[1] & 0x1f) != PRO_REGISTER_AND_MOVE) &&
	    (cmd->data_length != 24)) {
		pr_warn("SPC-PR: Received PR OUT illegal parameter"
			" list length: %u\n", cmd->data_length);
		return TCM_INVALID_PARAMETER_LIST;
	}

	/*
	 * (core_scsi3_emulate_pro_* function parameters
	 * are defined by spc4r17 Table 174:
	 * PERSISTENT_RESERVE_OUT service actions and valid parameters.
	 */
	switch (sa) {
	case PRO_REGISTER:
		ret = core_scsi3_emulate_pro_register(cmd,
			res_key, sa_res_key, aptpl, all_tg_pt, spec_i_pt, REGISTER);
		break;
	case PRO_RESERVE:
		ret = core_scsi3_emulate_pro_reserve(cmd, type, scope, res_key);
		break;
	case PRO_RELEASE:
		ret = core_scsi3_emulate_pro_release(cmd, type, scope, res_key);
		break;
	case PRO_CLEAR:
		ret = core_scsi3_emulate_pro_clear(cmd, res_key);
		break;
	case PRO_PREEMPT:
		ret = core_scsi3_emulate_pro_preempt(cmd, type, scope,
					res_key, sa_res_key, PREEMPT);
		break;
	case PRO_PREEMPT_AND_ABORT:
		ret = core_scsi3_emulate_pro_preempt(cmd, type, scope,
					res_key, sa_res_key, PREEMPT_AND_ABORT);
		break;
	case PRO_REGISTER_AND_IGNORE_EXISTING_KEY:
		ret = core_scsi3_emulate_pro_register(cmd,
			0, sa_res_key, aptpl, all_tg_pt, spec_i_pt, REGISTER_AND_IGNORE_EXISTING_KEY);
		break;
	case PRO_REGISTER_AND_MOVE:
		ret = core_scsi3_emulate_pro_register_and_move(cmd, res_key,
				sa_res_key, aptpl, unreg);
		break;
	default:
		pr_err("Unknown PERSISTENT_RESERVE_OUT service"
			" action: 0x%02x\n", cdb[1] & 0x1f);
		return TCM_INVALID_CDB_FIELD;
	}

	if (!ret)
		target_complete_cmd(cmd, GOOD);
	return ret;
}

/*
 * PERSISTENT_RESERVE_IN Service Action READ_KEYS
 *
 * See spc4r17 section 5.7.6.2 and section 6.13.2, Table 160
 */
static sense_reason_t
core_scsi3_pri_read_keys(struct se_cmd *cmd)
{
	struct se_device *dev = cmd->se_dev;
	struct t10_pr_registration *pr_reg;
	unsigned char *buf;
	u32 add_len = 0, off = 8;

	if (cmd->data_length < 8) {
		pr_err("PRIN SA READ_KEYS SCSI Data Length: %u"
			" too small\n", cmd->data_length);
		return TCM_INVALID_CDB_FIELD;
	}

	buf = transport_kmap_data_sg(cmd);
	if (!buf)
		return TCM_LOGICAL_UNIT_COMMUNICATION_FAILURE;

	buf[0] = ((dev->t10_pr.pr_generation >> 24) & 0xff);
	buf[1] = ((dev->t10_pr.pr_generation >> 16) & 0xff);
	buf[2] = ((dev->t10_pr.pr_generation >> 8) & 0xff);
	buf[3] = (dev->t10_pr.pr_generation & 0xff);

	spin_lock(&dev->t10_pr.registration_lock);
	list_for_each_entry(pr_reg, &dev->t10_pr.registration_list,
			pr_reg_list) {
		/*
		 * Check for overflow of 8byte PRI READ_KEYS payload and
		 * next reservation key list descriptor.
		 */
		if ((add_len + 8) > (cmd->data_length - 8))
			break;

		buf[off++] = ((pr_reg->pr_res_key >> 56) & 0xff);
		buf[off++] = ((pr_reg->pr_res_key >> 48) & 0xff);
		buf[off++] = ((pr_reg->pr_res_key >> 40) & 0xff);
		buf[off++] = ((pr_reg->pr_res_key >> 32) & 0xff);
		buf[off++] = ((pr_reg->pr_res_key >> 24) & 0xff);
		buf[off++] = ((pr_reg->pr_res_key >> 16) & 0xff);
		buf[off++] = ((pr_reg->pr_res_key >> 8) & 0xff);
		buf[off++] = (pr_reg->pr_res_key & 0xff);

		add_len += 8;
	}
	spin_unlock(&dev->t10_pr.registration_lock);

	buf[4] = ((add_len >> 24) & 0xff);
	buf[5] = ((add_len >> 16) & 0xff);
	buf[6] = ((add_len >> 8) & 0xff);
	buf[7] = (add_len & 0xff);

	transport_kunmap_data_sg(cmd);

	return 0;
}

/*
 * PERSISTENT_RESERVE_IN Service Action READ_RESERVATION
 *
 * See spc4r17 section 5.7.6.3 and section 6.13.3.2 Table 161 and 162
 */
static sense_reason_t
core_scsi3_pri_read_reservation(struct se_cmd *cmd)
{
	struct se_device *dev = cmd->se_dev;
	struct t10_pr_registration *pr_reg;
	unsigned char *buf;
	u64 pr_res_key;
	u32 add_len = 16; /* Hardcoded to 16 when a reservation is held. */

	if (cmd->data_length < 8) {
		pr_err("PRIN SA READ_RESERVATIONS SCSI Data Length: %u"
			" too small\n", cmd->data_length);
		return TCM_INVALID_CDB_FIELD;
	}

	buf = transport_kmap_data_sg(cmd);
	if (!buf)
		return TCM_LOGICAL_UNIT_COMMUNICATION_FAILURE;

	buf[0] = ((dev->t10_pr.pr_generation >> 24) & 0xff);
	buf[1] = ((dev->t10_pr.pr_generation >> 16) & 0xff);
	buf[2] = ((dev->t10_pr.pr_generation >> 8) & 0xff);
	buf[3] = (dev->t10_pr.pr_generation & 0xff);

	spin_lock(&dev->dev_reservation_lock);
	pr_reg = dev->dev_pr_res_holder;
	if (pr_reg) {
		/*
		 * Set the hardcoded Additional Length
		 */
		buf[4] = ((add_len >> 24) & 0xff);
		buf[5] = ((add_len >> 16) & 0xff);
		buf[6] = ((add_len >> 8) & 0xff);
		buf[7] = (add_len & 0xff);

		if (cmd->data_length < 22)
			goto err;

		/*
		 * Set the Reservation key.
		 *
		 * From spc4r17, section 5.7.10:
		 * A persistent reservation holder has its reservation key
		 * returned in the parameter data from a PERSISTENT
		 * RESERVE IN command with READ RESERVATION service action as
		 * follows:
		 * a) For a persistent reservation of the type Write Exclusive
		 *    - All Registrants or Exclusive Access ­ All Regitrants,
		 *      the reservation key shall be set to zero; or
		 * b) For all other persistent reservation types, the
		 *    reservation key shall be set to the registered
		 *    reservation key for the I_T nexus that holds the
		 *    persistent reservation.
		 */
		if ((pr_reg->pr_res_type == PR_TYPE_WRITE_EXCLUSIVE_ALLREG) ||
		    (pr_reg->pr_res_type == PR_TYPE_EXCLUSIVE_ACCESS_ALLREG))
			pr_res_key = 0;
		else
			pr_res_key = pr_reg->pr_res_key;

		buf[8] = ((pr_res_key >> 56) & 0xff);
		buf[9] = ((pr_res_key >> 48) & 0xff);
		buf[10] = ((pr_res_key >> 40) & 0xff);
		buf[11] = ((pr_res_key >> 32) & 0xff);
		buf[12] = ((pr_res_key >> 24) & 0xff);
		buf[13] = ((pr_res_key >> 16) & 0xff);
		buf[14] = ((pr_res_key >> 8) & 0xff);
		buf[15] = (pr_res_key & 0xff);
		/*
		 * Set the SCOPE and TYPE
		 */
		buf[21] = (pr_reg->pr_res_scope & 0xf0) |
			  (pr_reg->pr_res_type & 0x0f);
	}

err:
	spin_unlock(&dev->dev_reservation_lock);
	transport_kunmap_data_sg(cmd);

	return 0;
}

/*
 * PERSISTENT_RESERVE_IN Service Action REPORT_CAPABILITIES
 *
 * See spc4r17 section 6.13.4 Table 165
 */
static sense_reason_t
core_scsi3_pri_report_capabilities(struct se_cmd *cmd)
{
	struct se_device *dev = cmd->se_dev;
	struct t10_reservation *pr_tmpl = &dev->t10_pr;
	unsigned char *buf;
	u16 add_len = 8; /* Hardcoded to 8. */

	if (cmd->data_length < 6) {
		pr_err("PRIN SA REPORT_CAPABILITIES SCSI Data Length:"
			" %u too small\n", cmd->data_length);
		return TCM_INVALID_CDB_FIELD;
	}

	buf = transport_kmap_data_sg(cmd);
	if (!buf)
		return TCM_LOGICAL_UNIT_COMMUNICATION_FAILURE;

	buf[0] = ((add_len << 8) & 0xff);
	buf[1] = (add_len & 0xff);
	buf[2] |= 0x10; /* CRH: Compatible Reservation Hanlding bit. */
	buf[2] |= 0x08; /* SIP_C: Specify Initiator Ports Capable bit */
	buf[2] |= 0x04; /* ATP_C: All Target Ports Capable bit */
	buf[2] |= 0x01; /* PTPL_C: Persistence across Target Power Loss bit */
	/*
	 * We are filling in the PERSISTENT RESERVATION TYPE MASK below, so
	 * set the TMV: Task Mask Valid bit.
	 */
	buf[3] |= 0x80;
	/*
	 * Change ALLOW COMMANDs to 0x20 or 0x40 later from Table 166
	 */
	buf[3] |= 0x10; /* ALLOW COMMANDs field 001b */
	/*
	 * PTPL_A: Persistence across Target Power Loss Active bit
	 */
	if (pr_tmpl->pr_aptpl_active)
		buf[3] |= 0x01;
	/*
	 * Setup the PERSISTENT RESERVATION TYPE MASK from Table 167
	 */
	buf[4] |= 0x80; /* PR_TYPE_EXCLUSIVE_ACCESS_ALLREG */
	buf[4] |= 0x40; /* PR_TYPE_EXCLUSIVE_ACCESS_REGONLY */
	buf[4] |= 0x20; /* PR_TYPE_WRITE_EXCLUSIVE_REGONLY */
	buf[4] |= 0x08; /* PR_TYPE_EXCLUSIVE_ACCESS */
	buf[4] |= 0x02; /* PR_TYPE_WRITE_EXCLUSIVE */
	buf[5] |= 0x01; /* PR_TYPE_EXCLUSIVE_ACCESS_ALLREG */

	transport_kunmap_data_sg(cmd);

	return 0;
}

/*
 * PERSISTENT_RESERVE_IN Service Action READ_FULL_STATUS
 *
 * See spc4r17 section 6.13.5 Table 168 and 169
 */
static sense_reason_t
core_scsi3_pri_read_full_status(struct se_cmd *cmd)
{
	struct se_device *dev = cmd->se_dev;
	struct se_node_acl *se_nacl;
	struct se_portal_group *se_tpg;
	struct t10_pr_registration *pr_reg, *pr_reg_tmp;
	struct t10_reservation *pr_tmpl = &dev->t10_pr;
	unsigned char *buf;
	u32 add_desc_len = 0, add_len = 0, desc_len, exp_desc_len;
	u32 off = 8; /* off into first Full Status descriptor */
	int format_code = 0;

	if (cmd->data_length < 8) {
		pr_err("PRIN SA READ_FULL_STATUS SCSI Data Length: %u"
			" too small\n", cmd->data_length);
		return TCM_INVALID_CDB_FIELD;
	}

	buf = transport_kmap_data_sg(cmd);
	if (!buf)
		return TCM_LOGICAL_UNIT_COMMUNICATION_FAILURE;

	buf[0] = ((dev->t10_pr.pr_generation >> 24) & 0xff);
	buf[1] = ((dev->t10_pr.pr_generation >> 16) & 0xff);
	buf[2] = ((dev->t10_pr.pr_generation >> 8) & 0xff);
	buf[3] = (dev->t10_pr.pr_generation & 0xff);

	spin_lock(&pr_tmpl->registration_lock);
	list_for_each_entry_safe(pr_reg, pr_reg_tmp,
			&pr_tmpl->registration_list, pr_reg_list) {

		se_nacl = pr_reg->pr_reg_nacl;
		se_tpg = pr_reg->pr_reg_nacl->se_tpg;
		add_desc_len = 0;

		atomic_inc(&pr_reg->pr_res_holders);
		smp_mb__after_atomic_inc();
		spin_unlock(&pr_tmpl->registration_lock);
		/*
		 * Determine expected length of $FABRIC_MOD specific
		 * TransportID full status descriptor..
		 */
		exp_desc_len = se_tpg->se_tpg_tfo->tpg_get_pr_transport_id_len(
				se_tpg, se_nacl, pr_reg, &format_code);

		if ((exp_desc_len + add_len) > cmd->data_length) {
			pr_warn("SPC-3 PRIN READ_FULL_STATUS ran"
				" out of buffer: %d\n", cmd->data_length);
			spin_lock(&pr_tmpl->registration_lock);
			atomic_dec(&pr_reg->pr_res_holders);
			smp_mb__after_atomic_dec();
			break;
		}
		/*
		 * Set RESERVATION KEY
		 */
		buf[off++] = ((pr_reg->pr_res_key >> 56) & 0xff);
		buf[off++] = ((pr_reg->pr_res_key >> 48) & 0xff);
		buf[off++] = ((pr_reg->pr_res_key >> 40) & 0xff);
		buf[off++] = ((pr_reg->pr_res_key >> 32) & 0xff);
		buf[off++] = ((pr_reg->pr_res_key >> 24) & 0xff);
		buf[off++] = ((pr_reg->pr_res_key >> 16) & 0xff);
		buf[off++] = ((pr_reg->pr_res_key >> 8) & 0xff);
		buf[off++] = (pr_reg->pr_res_key & 0xff);
		off += 4; /* Skip Over Reserved area */

		/*
		 * Set ALL_TG_PT bit if PROUT SA REGISTER had this set.
		 */
		if (pr_reg->pr_reg_all_tg_pt)
			buf[off] = 0x02;
		/*
		 * The struct se_lun pointer will be present for the
		 * reservation holder for PR_HOLDER bit.
		 *
		 * Also, if this registration is the reservation
		 * holder, fill in SCOPE and TYPE in the next byte.
		 */
		if (pr_reg->pr_res_holder) {
			buf[off++] |= 0x01;
			buf[off++] = (pr_reg->pr_res_scope & 0xf0) |
				     (pr_reg->pr_res_type & 0x0f);
		} else
			off += 2;

		off += 4; /* Skip over reserved area */
		/*
		 * From spc4r17 6.3.15:
		 *
		 * If the ALL_TG_PT bit set to zero, the RELATIVE TARGET PORT
		 * IDENTIFIER field contains the relative port identifier (see
		 * 3.1.120) of the target port that is part of the I_T nexus
		 * described by this full status descriptor. If the ALL_TG_PT
		 * bit is set to one, the contents of the RELATIVE TARGET PORT
		 * IDENTIFIER field are not defined by this standard.
		 */
		if (!pr_reg->pr_reg_all_tg_pt) {
			struct se_port *port = pr_reg->pr_reg_tg_pt_lun->lun_sep;

			buf[off++] = ((port->sep_rtpi >> 8) & 0xff);
			buf[off++] = (port->sep_rtpi & 0xff);
		} else
			off += 2; /* Skip over RELATIVE TARGET PORT IDENTIFIER */

		/*
		 * Now, have the $FABRIC_MOD fill in the protocol identifier
		 */
		desc_len = se_tpg->se_tpg_tfo->tpg_get_pr_transport_id(se_tpg,
				se_nacl, pr_reg, &format_code, &buf[off+4]);

		spin_lock(&pr_tmpl->registration_lock);
		atomic_dec(&pr_reg->pr_res_holders);
		smp_mb__after_atomic_dec();
		/*
		 * Set the ADDITIONAL DESCRIPTOR LENGTH
		 */
		buf[off++] = ((desc_len >> 24) & 0xff);
		buf[off++] = ((desc_len >> 16) & 0xff);
		buf[off++] = ((desc_len >> 8) & 0xff);
		buf[off++] = (desc_len & 0xff);
		/*
		 * Size of full desctipor header minus TransportID
		 * containing $FABRIC_MOD specific) initiator device/port
		 * WWN information.
		 *
		 *  See spc4r17 Section 6.13.5 Table 169
		 */
		add_desc_len = (24 + desc_len);

		off += desc_len;
		add_len += add_desc_len;
	}
	spin_unlock(&pr_tmpl->registration_lock);
	/*
	 * Set ADDITIONAL_LENGTH
	 */
	buf[4] = ((add_len >> 24) & 0xff);
	buf[5] = ((add_len >> 16) & 0xff);
	buf[6] = ((add_len >> 8) & 0xff);
	buf[7] = (add_len & 0xff);

	transport_kunmap_data_sg(cmd);

	return 0;
}

sense_reason_t
target_scsi3_emulate_pr_in(struct se_cmd *cmd)
{
	sense_reason_t ret;

	/*
	 * Following spc2r20 5.5.1 Reservations overview:
	 *
	 * If a logical unit has been reserved by any RESERVE command and is
	 * still reserved by any initiator, all PERSISTENT RESERVE IN and all
	 * PERSISTENT RESERVE OUT commands shall conflict regardless of
	 * initiator or service action and shall terminate with a RESERVATION
	 * CONFLICT status.
	 */
	if (cmd->se_dev->dev_reservation_flags & DRF_SPC2_RESERVATIONS) {
		pr_err("Received PERSISTENT_RESERVE CDB while legacy"
			" SPC-2 reservation is held, returning"
			" RESERVATION_CONFLICT\n");
		return TCM_RESERVATION_CONFLICT;
	}

	switch (cmd->t_task_cdb[1] & 0x1f) {
	case PRI_READ_KEYS:
		ret = core_scsi3_pri_read_keys(cmd);
		break;
	case PRI_READ_RESERVATION:
		ret = core_scsi3_pri_read_reservation(cmd);
		break;
	case PRI_REPORT_CAPABILITIES:
		ret = core_scsi3_pri_report_capabilities(cmd);
		break;
	case PRI_READ_FULL_STATUS:
		ret = core_scsi3_pri_read_full_status(cmd);
		break;
	default:
		pr_err("Unknown PERSISTENT_RESERVE_IN service"
			" action: 0x%02x\n", cmd->t_task_cdb[1] & 0x1f);
		return TCM_INVALID_CDB_FIELD;
	}

	if (!ret)
		target_complete_cmd(cmd, GOOD);
	return ret;
}

sense_reason_t
target_check_reservation(struct se_cmd *cmd)
{
	struct se_device *dev = cmd->se_dev;
	sense_reason_t ret;

	if (!cmd->se_sess)
		return 0;
	if (dev->se_hba->hba_flags & HBA_FLAGS_INTERNAL_USE)
		return 0;
	if (dev->transport->transport_type == TRANSPORT_PLUGIN_PHBA_PDEV)
		return 0;

	spin_lock(&dev->dev_reservation_lock);
	if (dev->dev_reservation_flags & DRF_SPC2_RESERVATIONS)
		ret = target_scsi2_reservation_check(cmd);
	else
		ret = target_scsi3_pr_reservation_check(cmd);
	spin_unlock(&dev->dev_reservation_lock);

	return ret;
}

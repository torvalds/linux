/*******************************************************************************
 * Filename: target_core_ua.c
 *
 * This file contains logic for SPC-3 Unit Attention emulation
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
#include <scsi/scsi.h>
#include <scsi/scsi_cmnd.h>

#include <target/target_core_base.h>
#include <target/target_core_fabric.h>

#include "target_core_internal.h"
#include "target_core_alua.h"
#include "target_core_pr.h"
#include "target_core_ua.h"

sense_reason_t
target_scsi3_ua_check(struct se_cmd *cmd)
{
	struct se_dev_entry *deve;
	struct se_session *sess = cmd->se_sess;
	struct se_node_acl *nacl;

	if (!sess)
		return 0;

	nacl = sess->se_node_acl;
	if (!nacl)
		return 0;

	rcu_read_lock();
	deve = target_nacl_find_deve(nacl, cmd->orig_fe_lun);
	if (!deve) {
		rcu_read_unlock();
		return 0;
	}
	if (!atomic_read(&deve->ua_count)) {
		rcu_read_unlock();
		return 0;
	}
	rcu_read_unlock();
	/*
	 * From sam4r14, section 5.14 Unit attention condition:
	 *
	 * a) if an INQUIRY command enters the enabled command state, the
	 *    device server shall process the INQUIRY command and shall neither
	 *    report nor clear any unit attention condition;
	 * b) if a REPORT LUNS command enters the enabled command state, the
	 *    device server shall process the REPORT LUNS command and shall not
	 *    report any unit attention condition;
	 * e) if a REQUEST SENSE command enters the enabled command state while
	 *    a unit attention condition exists for the SCSI initiator port
	 *    associated with the I_T nexus on which the REQUEST SENSE command
	 *    was received, then the device server shall process the command
	 *    and either:
	 */
	switch (cmd->t_task_cdb[0]) {
	case INQUIRY:
	case REPORT_LUNS:
	case REQUEST_SENSE:
		return 0;
	default:
		return TCM_CHECK_CONDITION_UNIT_ATTENTION;
	}
}

int core_scsi3_ua_allocate(
	struct se_dev_entry *deve,
	u8 asc,
	u8 ascq)
{
	struct se_ua *ua, *ua_p, *ua_tmp;

	ua = kmem_cache_zalloc(se_ua_cache, GFP_ATOMIC);
	if (!ua) {
		pr_err("Unable to allocate struct se_ua\n");
		return -ENOMEM;
	}
	INIT_LIST_HEAD(&ua->ua_nacl_list);

	ua->ua_asc = asc;
	ua->ua_ascq = ascq;

	spin_lock(&deve->ua_lock);
	list_for_each_entry_safe(ua_p, ua_tmp, &deve->ua_list, ua_nacl_list) {
		/*
		 * Do not report the same UNIT ATTENTION twice..
		 */
		if ((ua_p->ua_asc == asc) && (ua_p->ua_ascq == ascq)) {
			spin_unlock(&deve->ua_lock);
			kmem_cache_free(se_ua_cache, ua);
			return 0;
		}
		/*
		 * Attach the highest priority Unit Attention to
		 * the head of the list following sam4r14,
		 * Section 5.14 Unit Attention Condition:
		 *
		 * POWER ON, RESET, OR BUS DEVICE RESET OCCURRED highest
		 * POWER ON OCCURRED or
		 * DEVICE INTERNAL RESET
		 * SCSI BUS RESET OCCURRED or
		 * MICROCODE HAS BEEN CHANGED or
		 * protocol specific
		 * BUS DEVICE RESET FUNCTION OCCURRED
		 * I_T NEXUS LOSS OCCURRED
		 * COMMANDS CLEARED BY POWER LOSS NOTIFICATION
		 * all others                                    Lowest
		 *
		 * Each of the ASCQ codes listed above are defined in
		 * the 29h ASC family, see spc4r17 Table D.1
		 */
		if (ua_p->ua_asc == 0x29) {
			if ((asc == 0x29) && (ascq > ua_p->ua_ascq))
				list_add(&ua->ua_nacl_list,
						&deve->ua_list);
			else
				list_add_tail(&ua->ua_nacl_list,
						&deve->ua_list);
		} else if (ua_p->ua_asc == 0x2a) {
			/*
			 * Incoming Family 29h ASCQ codes will override
			 * Family 2AHh ASCQ codes for Unit Attention condition.
			 */
			if ((asc == 0x29) || (ascq > ua_p->ua_asc))
				list_add(&ua->ua_nacl_list,
					&deve->ua_list);
			else
				list_add_tail(&ua->ua_nacl_list,
						&deve->ua_list);
		} else
			list_add_tail(&ua->ua_nacl_list,
				&deve->ua_list);
		spin_unlock(&deve->ua_lock);

		atomic_inc_mb(&deve->ua_count);
		return 0;
	}
	list_add_tail(&ua->ua_nacl_list, &deve->ua_list);
	spin_unlock(&deve->ua_lock);

	pr_debug("Allocated UNIT ATTENTION, mapped LUN: %llu, ASC:"
		" 0x%02x, ASCQ: 0x%02x\n", deve->mapped_lun,
		asc, ascq);

	atomic_inc_mb(&deve->ua_count);
	return 0;
}

void target_ua_allocate_lun(struct se_node_acl *nacl,
			    u32 unpacked_lun, u8 asc, u8 ascq)
{
	struct se_dev_entry *deve;

	if (!nacl)
		return;

	rcu_read_lock();
	deve = target_nacl_find_deve(nacl, unpacked_lun);
	if (!deve) {
		rcu_read_unlock();
		return;
	}

	core_scsi3_ua_allocate(deve, asc, ascq);
	rcu_read_unlock();
}

void core_scsi3_ua_release_all(
	struct se_dev_entry *deve)
{
	struct se_ua *ua, *ua_p;

	spin_lock(&deve->ua_lock);
	list_for_each_entry_safe(ua, ua_p, &deve->ua_list, ua_nacl_list) {
		list_del(&ua->ua_nacl_list);
		kmem_cache_free(se_ua_cache, ua);

		atomic_dec_mb(&deve->ua_count);
	}
	spin_unlock(&deve->ua_lock);
}

void core_scsi3_ua_for_check_condition(
	struct se_cmd *cmd,
	u8 *asc,
	u8 *ascq)
{
	struct se_device *dev = cmd->se_dev;
	struct se_dev_entry *deve;
	struct se_session *sess = cmd->se_sess;
	struct se_node_acl *nacl;
	struct se_ua *ua = NULL, *ua_p;
	int head = 1;

	if (!sess)
		return;

	nacl = sess->se_node_acl;
	if (!nacl)
		return;

	rcu_read_lock();
	deve = target_nacl_find_deve(nacl, cmd->orig_fe_lun);
	if (!deve) {
		rcu_read_unlock();
		return;
	}
	if (!atomic_read(&deve->ua_count)) {
		rcu_read_unlock();
		return;
	}
	/*
	 * The highest priority Unit Attentions are placed at the head of the
	 * struct se_dev_entry->ua_list, and will be returned in CHECK_CONDITION +
	 * sense data for the received CDB.
	 */
	spin_lock(&deve->ua_lock);
	list_for_each_entry_safe(ua, ua_p, &deve->ua_list, ua_nacl_list) {
		/*
		 * For ua_intlck_ctrl code not equal to 00b, only report the
		 * highest priority UNIT_ATTENTION and ASC/ASCQ without
		 * clearing it.
		 */
		if (dev->dev_attrib.emulate_ua_intlck_ctrl != 0) {
			*asc = ua->ua_asc;
			*ascq = ua->ua_ascq;
			break;
		}
		/*
		 * Otherwise for the default 00b, release the UNIT ATTENTION
		 * condition.  Return the ASC/ASCQ of the highest priority UA
		 * (head of the list) in the outgoing CHECK_CONDITION + sense.
		 */
		if (head) {
			*asc = ua->ua_asc;
			*ascq = ua->ua_ascq;
			head = 0;
		}
		list_del(&ua->ua_nacl_list);
		kmem_cache_free(se_ua_cache, ua);

		atomic_dec_mb(&deve->ua_count);
	}
	spin_unlock(&deve->ua_lock);
	rcu_read_unlock();

	pr_debug("[%s]: %s UNIT ATTENTION condition with"
		" INTLCK_CTRL: %d, mapped LUN: %llu, got CDB: 0x%02x"
		" reported ASC: 0x%02x, ASCQ: 0x%02x\n",
		nacl->se_tpg->se_tpg_tfo->get_fabric_name(),
		(dev->dev_attrib.emulate_ua_intlck_ctrl != 0) ? "Reporting" :
		"Releasing", dev->dev_attrib.emulate_ua_intlck_ctrl,
		cmd->orig_fe_lun, cmd->t_task_cdb[0], *asc, *ascq);
}

int core_scsi3_ua_clear_for_request_sense(
	struct se_cmd *cmd,
	u8 *asc,
	u8 *ascq)
{
	struct se_dev_entry *deve;
	struct se_session *sess = cmd->se_sess;
	struct se_node_acl *nacl;
	struct se_ua *ua = NULL, *ua_p;
	int head = 1;

	if (!sess)
		return -EINVAL;

	nacl = sess->se_node_acl;
	if (!nacl)
		return -EINVAL;

	rcu_read_lock();
	deve = target_nacl_find_deve(nacl, cmd->orig_fe_lun);
	if (!deve) {
		rcu_read_unlock();
		return -EINVAL;
	}
	if (!atomic_read(&deve->ua_count)) {
		rcu_read_unlock();
		return -EPERM;
	}
	/*
	 * The highest priority Unit Attentions are placed at the head of the
	 * struct se_dev_entry->ua_list.  The First (and hence highest priority)
	 * ASC/ASCQ will be returned in REQUEST_SENSE payload data for the
	 * matching struct se_lun.
	 *
	 * Once the returning ASC/ASCQ values are set, we go ahead and
	 * release all of the Unit Attention conditions for the associated
	 * struct se_lun.
	 */
	spin_lock(&deve->ua_lock);
	list_for_each_entry_safe(ua, ua_p, &deve->ua_list, ua_nacl_list) {
		if (head) {
			*asc = ua->ua_asc;
			*ascq = ua->ua_ascq;
			head = 0;
		}
		list_del(&ua->ua_nacl_list);
		kmem_cache_free(se_ua_cache, ua);

		atomic_dec_mb(&deve->ua_count);
	}
	spin_unlock(&deve->ua_lock);
	rcu_read_unlock();

	pr_debug("[%s]: Released UNIT ATTENTION condition, mapped"
		" LUN: %llu, got REQUEST_SENSE reported ASC: 0x%02x,"
		" ASCQ: 0x%02x\n", nacl->se_tpg->se_tpg_tfo->get_fabric_name(),
		cmd->orig_fe_lun, *asc, *ascq);

	return (head) ? -EPERM : 0;
}

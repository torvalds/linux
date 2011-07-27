/*******************************************************************************
 * Filename:  target_core_tmr.c
 *
 * This file contains SPC-3 task management infrastructure
 *
 * Copyright (c) 2009,2010 Rising Tide Systems
 * Copyright (c) 2009,2010 Linux-iSCSI.org
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

#include <linux/version.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/list.h>
#include <scsi/scsi.h>
#include <scsi/scsi_cmnd.h>

#include <target/target_core_base.h>
#include <target/target_core_device.h>
#include <target/target_core_tmr.h>
#include <target/target_core_transport.h>
#include <target/target_core_fabric_ops.h>
#include <target/target_core_configfs.h>

#include "target_core_alua.h"
#include "target_core_pr.h"

#define DEBUG_LUN_RESET
#ifdef DEBUG_LUN_RESET
#define DEBUG_LR(x...) printk(KERN_INFO x)
#else
#define DEBUG_LR(x...)
#endif

struct se_tmr_req *core_tmr_alloc_req(
	struct se_cmd *se_cmd,
	void *fabric_tmr_ptr,
	u8 function)
{
	struct se_tmr_req *tmr;

	tmr = kmem_cache_zalloc(se_tmr_req_cache, (in_interrupt()) ?
					GFP_ATOMIC : GFP_KERNEL);
	if (!(tmr)) {
		printk(KERN_ERR "Unable to allocate struct se_tmr_req\n");
		return ERR_PTR(-ENOMEM);
	}
	tmr->task_cmd = se_cmd;
	tmr->fabric_tmr_ptr = fabric_tmr_ptr;
	tmr->function = function;
	INIT_LIST_HEAD(&tmr->tmr_list);

	return tmr;
}
EXPORT_SYMBOL(core_tmr_alloc_req);

void core_tmr_release_req(
	struct se_tmr_req *tmr)
{
	struct se_device *dev = tmr->tmr_dev;

	spin_lock(&dev->se_tmr_lock);
	list_del(&tmr->tmr_list);
	kmem_cache_free(se_tmr_req_cache, tmr);
	spin_unlock(&dev->se_tmr_lock);
}

static void core_tmr_handle_tas_abort(
	struct se_node_acl *tmr_nacl,
	struct se_cmd *cmd,
	int tas,
	int fe_count)
{
	if (!(fe_count)) {
		transport_cmd_finish_abort(cmd, 1);
		return;
	}
	/*
	 * TASK ABORTED status (TAS) bit support
	*/
	if (((tmr_nacl != NULL) &&
	     (tmr_nacl == cmd->se_sess->se_node_acl)) || tas)
		transport_send_task_abort(cmd);

	transport_cmd_finish_abort(cmd, 0);
}

int core_tmr_lun_reset(
	struct se_device *dev,
	struct se_tmr_req *tmr,
	struct list_head *preempt_and_abort_list,
	struct se_cmd *prout_cmd)
{
	struct se_cmd *cmd;
	struct se_queue_req *qr, *qr_tmp;
	struct se_node_acl *tmr_nacl = NULL;
	struct se_portal_group *tmr_tpg = NULL;
	struct se_queue_obj *qobj = dev->dev_queue_obj;
	struct se_tmr_req *tmr_p, *tmr_pp;
	struct se_task *task, *task_tmp;
	unsigned long flags;
	int fe_count, state, tas;
	/*
	 * TASK_ABORTED status bit, this is configurable via ConfigFS
	 * struct se_device attributes.  spc4r17 section 7.4.6 Control mode page
	 *
	 * A task aborted status (TAS) bit set to zero specifies that aborted
	 * tasks shall be terminated by the device server without any response
	 * to the application client. A TAS bit set to one specifies that tasks
	 * aborted by the actions of an I_T nexus other than the I_T nexus on
	 * which the command was received shall be completed with TASK ABORTED
	 * status (see SAM-4).
	 */
	tas = DEV_ATTRIB(dev)->emulate_tas;
	/*
	 * Determine if this se_tmr is coming from a $FABRIC_MOD
	 * or struct se_device passthrough..
	 */
	if (tmr && tmr->task_cmd && tmr->task_cmd->se_sess) {
		tmr_nacl = tmr->task_cmd->se_sess->se_node_acl;
		tmr_tpg = tmr->task_cmd->se_sess->se_tpg;
		if (tmr_nacl && tmr_tpg) {
			DEBUG_LR("LUN_RESET: TMR caller fabric: %s"
				" initiator port %s\n",
				TPG_TFO(tmr_tpg)->get_fabric_name(),
				tmr_nacl->initiatorname);
		}
	}
	DEBUG_LR("LUN_RESET: %s starting for [%s], tas: %d\n",
		(preempt_and_abort_list) ? "Preempt" : "TMR",
		TRANSPORT(dev)->name, tas);
	/*
	 * Release all pending and outgoing TMRs aside from the received
	 * LUN_RESET tmr..
	 */
	spin_lock(&dev->se_tmr_lock);
	list_for_each_entry_safe(tmr_p, tmr_pp, &dev->dev_tmr_list, tmr_list) {
		/*
		 * Allow the received TMR to return with FUNCTION_COMPLETE.
		 */
		if (tmr && (tmr_p == tmr))
			continue;

		cmd = tmr_p->task_cmd;
		if (!(cmd)) {
			printk(KERN_ERR "Unable to locate struct se_cmd for TMR\n");
			continue;
		}
		/*
		 * If this function was called with a valid pr_res_key
		 * parameter (eg: for PROUT PREEMPT_AND_ABORT service action
		 * skip non regisration key matching TMRs.
		 */
		if ((preempt_and_abort_list != NULL) &&
		    (core_scsi3_check_cdb_abort_and_preempt(
					preempt_and_abort_list, cmd) != 0))
			continue;
		spin_unlock(&dev->se_tmr_lock);

		spin_lock_irqsave(&T_TASK(cmd)->t_state_lock, flags);
		if (!(atomic_read(&T_TASK(cmd)->t_transport_active))) {
			spin_unlock_irqrestore(&T_TASK(cmd)->t_state_lock, flags);
			spin_lock(&dev->se_tmr_lock);
			continue;
		}
		if (cmd->t_state == TRANSPORT_ISTATE_PROCESSING) {
			spin_unlock_irqrestore(&T_TASK(cmd)->t_state_lock, flags);
			spin_lock(&dev->se_tmr_lock);
			continue;
		}
		DEBUG_LR("LUN_RESET: %s releasing TMR %p Function: 0x%02x,"
			" Response: 0x%02x, t_state: %d\n",
			(preempt_and_abort_list) ? "Preempt" : "", tmr_p,
			tmr_p->function, tmr_p->response, cmd->t_state);
		spin_unlock_irqrestore(&T_TASK(cmd)->t_state_lock, flags);

		transport_cmd_finish_abort_tmr(cmd);
		spin_lock(&dev->se_tmr_lock);
	}
	spin_unlock(&dev->se_tmr_lock);
	/*
	 * Complete outstanding struct se_task CDBs with TASK_ABORTED SAM status.
	 * This is following sam4r17, section 5.6 Aborting commands, Table 38
	 * for TMR LUN_RESET:
	 *
	 * a) "Yes" indicates that each command that is aborted on an I_T nexus
	 * other than the one that caused the SCSI device condition is
	 * completed with TASK ABORTED status, if the TAS bit is set to one in
	 * the Control mode page (see SPC-4). "No" indicates that no status is
	 * returned for aborted commands.
	 *
	 * d) If the logical unit reset is caused by a particular I_T nexus
	 * (e.g., by a LOGICAL UNIT RESET task management function), then "yes"
	 * (TASK_ABORTED status) applies.
	 *
	 * Otherwise (e.g., if triggered by a hard reset), "no"
	 * (no TASK_ABORTED SAM status) applies.
	 *
	 * Note that this seems to be independent of TAS (Task Aborted Status)
	 * in the Control Mode Page.
	 */
	spin_lock_irqsave(&dev->execute_task_lock, flags);
	list_for_each_entry_safe(task, task_tmp, &dev->state_task_list,
				t_state_list) {
		if (!(TASK_CMD(task))) {
			printk(KERN_ERR "TASK_CMD(task) is NULL!\n");
			continue;
		}
		cmd = TASK_CMD(task);

		if (!T_TASK(cmd)) {
			printk(KERN_ERR "T_TASK(cmd) is NULL for task: %p cmd:"
				" %p ITT: 0x%08x\n", task, cmd,
				CMD_TFO(cmd)->get_task_tag(cmd));
			continue;
		}
		/*
		 * For PREEMPT_AND_ABORT usage, only process commands
		 * with a matching reservation key.
		 */
		if ((preempt_and_abort_list != NULL) &&
		    (core_scsi3_check_cdb_abort_and_preempt(
					preempt_and_abort_list, cmd) != 0))
			continue;
		/*
		 * Not aborting PROUT PREEMPT_AND_ABORT CDB..
		 */
		if (prout_cmd == cmd)
			continue;

		list_del(&task->t_state_list);
		atomic_set(&task->task_state_active, 0);
		spin_unlock_irqrestore(&dev->execute_task_lock, flags);

		spin_lock_irqsave(&T_TASK(cmd)->t_state_lock, flags);
		DEBUG_LR("LUN_RESET: %s cmd: %p task: %p"
			" ITT/CmdSN: 0x%08x/0x%08x, i_state: %d, t_state/"
			"def_t_state: %d/%d cdb: 0x%02x\n",
			(preempt_and_abort_list) ? "Preempt" : "", cmd, task,
			CMD_TFO(cmd)->get_task_tag(cmd), 0,
			CMD_TFO(cmd)->get_cmd_state(cmd), cmd->t_state,
			cmd->deferred_t_state, T_TASK(cmd)->t_task_cdb[0]);
		DEBUG_LR("LUN_RESET: ITT[0x%08x] - pr_res_key: 0x%016Lx"
			" t_task_cdbs: %d t_task_cdbs_left: %d"
			" t_task_cdbs_sent: %d -- t_transport_active: %d"
			" t_transport_stop: %d t_transport_sent: %d\n",
			CMD_TFO(cmd)->get_task_tag(cmd), cmd->pr_res_key,
			T_TASK(cmd)->t_task_cdbs,
			atomic_read(&T_TASK(cmd)->t_task_cdbs_left),
			atomic_read(&T_TASK(cmd)->t_task_cdbs_sent),
			atomic_read(&T_TASK(cmd)->t_transport_active),
			atomic_read(&T_TASK(cmd)->t_transport_stop),
			atomic_read(&T_TASK(cmd)->t_transport_sent));

		if (atomic_read(&task->task_active)) {
			atomic_set(&task->task_stop, 1);
			spin_unlock_irqrestore(
				&T_TASK(cmd)->t_state_lock, flags);

			DEBUG_LR("LUN_RESET: Waiting for task: %p to shutdown"
				" for dev: %p\n", task, dev);
			wait_for_completion(&task->task_stop_comp);
			DEBUG_LR("LUN_RESET Completed task: %p shutdown for"
				" dev: %p\n", task, dev);
			spin_lock_irqsave(&T_TASK(cmd)->t_state_lock, flags);
			atomic_dec(&T_TASK(cmd)->t_task_cdbs_left);

			atomic_set(&task->task_active, 0);
			atomic_set(&task->task_stop, 0);
		} else {
			if (atomic_read(&task->task_execute_queue) != 0)
				transport_remove_task_from_execute_queue(task, dev);
		}
		__transport_stop_task_timer(task, &flags);

		if (!(atomic_dec_and_test(&T_TASK(cmd)->t_task_cdbs_ex_left))) {
			spin_unlock_irqrestore(
					&T_TASK(cmd)->t_state_lock, flags);
			DEBUG_LR("LUN_RESET: Skipping task: %p, dev: %p for"
				" t_task_cdbs_ex_left: %d\n", task, dev,
				atomic_read(&T_TASK(cmd)->t_task_cdbs_ex_left));

			spin_lock_irqsave(&dev->execute_task_lock, flags);
			continue;
		}
		fe_count = atomic_read(&T_TASK(cmd)->t_fe_count);

		if (atomic_read(&T_TASK(cmd)->t_transport_active)) {
			DEBUG_LR("LUN_RESET: got t_transport_active = 1 for"
				" task: %p, t_fe_count: %d dev: %p\n", task,
				fe_count, dev);
			atomic_set(&T_TASK(cmd)->t_transport_aborted, 1);
			spin_unlock_irqrestore(&T_TASK(cmd)->t_state_lock,
						flags);
			core_tmr_handle_tas_abort(tmr_nacl, cmd, tas, fe_count);

			spin_lock_irqsave(&dev->execute_task_lock, flags);
			continue;
		}
		DEBUG_LR("LUN_RESET: Got t_transport_active = 0 for task: %p,"
			" t_fe_count: %d dev: %p\n", task, fe_count, dev);
		atomic_set(&T_TASK(cmd)->t_transport_aborted, 1);
		spin_unlock_irqrestore(&T_TASK(cmd)->t_state_lock, flags);
		core_tmr_handle_tas_abort(tmr_nacl, cmd, tas, fe_count);

		spin_lock_irqsave(&dev->execute_task_lock, flags);
	}
	spin_unlock_irqrestore(&dev->execute_task_lock, flags);
	/*
	 * Release all commands remaining in the struct se_device cmd queue.
	 *
	 * This follows the same logic as above for the struct se_device
	 * struct se_task state list, where commands are returned with
	 * TASK_ABORTED status, if there is an outstanding $FABRIC_MOD
	 * reference, otherwise the struct se_cmd is released.
	 */
	spin_lock_irqsave(&qobj->cmd_queue_lock, flags);
	list_for_each_entry_safe(qr, qr_tmp, &qobj->qobj_list, qr_list) {
		cmd = (struct se_cmd *)qr->cmd;
		if (!(cmd)) {
			/*
			 * Skip these for non PREEMPT_AND_ABORT usage..
			 */
			if (preempt_and_abort_list != NULL)
				continue;

			atomic_dec(&qobj->queue_cnt);
			list_del(&qr->qr_list);
			kfree(qr);
			continue;
		}
		/*
		 * For PREEMPT_AND_ABORT usage, only process commands
		 * with a matching reservation key.
		 */
		if ((preempt_and_abort_list != NULL) &&
		    (core_scsi3_check_cdb_abort_and_preempt(
					preempt_and_abort_list, cmd) != 0))
			continue;
		/*
		 * Not aborting PROUT PREEMPT_AND_ABORT CDB..
		 */
		if (prout_cmd == cmd)
			continue;

		atomic_dec(&T_TASK(cmd)->t_transport_queue_active);
		atomic_dec(&qobj->queue_cnt);
		list_del(&qr->qr_list);
		spin_unlock_irqrestore(&qobj->cmd_queue_lock, flags);

		state = qr->state;
		kfree(qr);

		DEBUG_LR("LUN_RESET: %s from Device Queue: cmd: %p t_state:"
			" %d t_fe_count: %d\n", (preempt_and_abort_list) ?
			"Preempt" : "", cmd, state,
			atomic_read(&T_TASK(cmd)->t_fe_count));
		/*
		 * Signal that the command has failed via cmd->se_cmd_flags,
		 * and call TFO->new_cmd_failure() to wakeup any fabric
		 * dependent code used to wait for unsolicited data out
		 * allocation to complete.  The fabric module is expected
		 * to dump any remaining unsolicited data out for the aborted
		 * command at this point.
		 */
		transport_new_cmd_failure(cmd);

		core_tmr_handle_tas_abort(tmr_nacl, cmd, tas,
				atomic_read(&T_TASK(cmd)->t_fe_count));
		spin_lock_irqsave(&qobj->cmd_queue_lock, flags);
	}
	spin_unlock_irqrestore(&qobj->cmd_queue_lock, flags);
	/*
	 * Clear any legacy SPC-2 reservation when called during
	 * LOGICAL UNIT RESET
	 */
	if (!(preempt_and_abort_list) &&
	     (dev->dev_flags & DF_SPC2_RESERVATIONS)) {
		spin_lock(&dev->dev_reservation_lock);
		dev->dev_reserved_node_acl = NULL;
		dev->dev_flags &= ~DF_SPC2_RESERVATIONS;
		spin_unlock(&dev->dev_reservation_lock);
		printk(KERN_INFO "LUN_RESET: SCSI-2 Released reservation\n");
	}

	spin_lock_irq(&dev->stats_lock);
	dev->num_resets++;
	spin_unlock_irq(&dev->stats_lock);

	DEBUG_LR("LUN_RESET: %s for [%s] Complete\n",
			(preempt_and_abort_list) ? "Preempt" : "TMR",
			TRANSPORT(dev)->name);
	return 0;
}

/*******************************************************************************
 * Filename:  target_core_transport.c
 *
 * This file contains the Generic Target Engine Core.
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
 *
 ******************************************************************************/

#include <linux/net.h>
#include <linux/delay.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/slab.h>
#include <linux/blkdev.h>
#include <linux/spinlock.h>
#include <linux/kthread.h>
#include <linux/in.h>
#include <linux/cdrom.h>
#include <linux/module.h>
#include <linux/ratelimit.h>
#include <asm/unaligned.h>
#include <net/sock.h>
#include <net/tcp.h>
#include <scsi/scsi.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_tcq.h>

#include <target/target_core_base.h>
#include <target/target_core_backend.h>
#include <target/target_core_fabric.h>
#include <target/target_core_configfs.h>

#include "target_core_internal.h"
#include "target_core_alua.h"
#include "target_core_pr.h"
#include "target_core_ua.h"

static int sub_api_initialized;

static struct workqueue_struct *target_completion_wq;
static struct kmem_cache *se_sess_cache;
struct kmem_cache *se_ua_cache;
struct kmem_cache *t10_pr_reg_cache;
struct kmem_cache *t10_alua_lu_gp_cache;
struct kmem_cache *t10_alua_lu_gp_mem_cache;
struct kmem_cache *t10_alua_tg_pt_gp_cache;
struct kmem_cache *t10_alua_tg_pt_gp_mem_cache;

static int transport_generic_write_pending(struct se_cmd *);
static int transport_processing_thread(void *param);
static int __transport_execute_tasks(struct se_device *dev, struct se_cmd *);
static void transport_complete_task_attr(struct se_cmd *cmd);
static void transport_handle_queue_full(struct se_cmd *cmd,
		struct se_device *dev);
static int transport_generic_get_mem(struct se_cmd *cmd);
static void transport_put_cmd(struct se_cmd *cmd);
static void transport_remove_cmd_from_queue(struct se_cmd *cmd);
static int transport_set_sense_codes(struct se_cmd *cmd, u8 asc, u8 ascq);
static void target_complete_ok_work(struct work_struct *work);

int init_se_kmem_caches(void)
{
	se_sess_cache = kmem_cache_create("se_sess_cache",
			sizeof(struct se_session), __alignof__(struct se_session),
			0, NULL);
	if (!se_sess_cache) {
		pr_err("kmem_cache_create() for struct se_session"
				" failed\n");
		goto out;
	}
	se_ua_cache = kmem_cache_create("se_ua_cache",
			sizeof(struct se_ua), __alignof__(struct se_ua),
			0, NULL);
	if (!se_ua_cache) {
		pr_err("kmem_cache_create() for struct se_ua failed\n");
		goto out_free_sess_cache;
	}
	t10_pr_reg_cache = kmem_cache_create("t10_pr_reg_cache",
			sizeof(struct t10_pr_registration),
			__alignof__(struct t10_pr_registration), 0, NULL);
	if (!t10_pr_reg_cache) {
		pr_err("kmem_cache_create() for struct t10_pr_registration"
				" failed\n");
		goto out_free_ua_cache;
	}
	t10_alua_lu_gp_cache = kmem_cache_create("t10_alua_lu_gp_cache",
			sizeof(struct t10_alua_lu_gp), __alignof__(struct t10_alua_lu_gp),
			0, NULL);
	if (!t10_alua_lu_gp_cache) {
		pr_err("kmem_cache_create() for t10_alua_lu_gp_cache"
				" failed\n");
		goto out_free_pr_reg_cache;
	}
	t10_alua_lu_gp_mem_cache = kmem_cache_create("t10_alua_lu_gp_mem_cache",
			sizeof(struct t10_alua_lu_gp_member),
			__alignof__(struct t10_alua_lu_gp_member), 0, NULL);
	if (!t10_alua_lu_gp_mem_cache) {
		pr_err("kmem_cache_create() for t10_alua_lu_gp_mem_"
				"cache failed\n");
		goto out_free_lu_gp_cache;
	}
	t10_alua_tg_pt_gp_cache = kmem_cache_create("t10_alua_tg_pt_gp_cache",
			sizeof(struct t10_alua_tg_pt_gp),
			__alignof__(struct t10_alua_tg_pt_gp), 0, NULL);
	if (!t10_alua_tg_pt_gp_cache) {
		pr_err("kmem_cache_create() for t10_alua_tg_pt_gp_"
				"cache failed\n");
		goto out_free_lu_gp_mem_cache;
	}
	t10_alua_tg_pt_gp_mem_cache = kmem_cache_create(
			"t10_alua_tg_pt_gp_mem_cache",
			sizeof(struct t10_alua_tg_pt_gp_member),
			__alignof__(struct t10_alua_tg_pt_gp_member),
			0, NULL);
	if (!t10_alua_tg_pt_gp_mem_cache) {
		pr_err("kmem_cache_create() for t10_alua_tg_pt_gp_"
				"mem_t failed\n");
		goto out_free_tg_pt_gp_cache;
	}

	target_completion_wq = alloc_workqueue("target_completion",
					       WQ_MEM_RECLAIM, 0);
	if (!target_completion_wq)
		goto out_free_tg_pt_gp_mem_cache;

	return 0;

out_free_tg_pt_gp_mem_cache:
	kmem_cache_destroy(t10_alua_tg_pt_gp_mem_cache);
out_free_tg_pt_gp_cache:
	kmem_cache_destroy(t10_alua_tg_pt_gp_cache);
out_free_lu_gp_mem_cache:
	kmem_cache_destroy(t10_alua_lu_gp_mem_cache);
out_free_lu_gp_cache:
	kmem_cache_destroy(t10_alua_lu_gp_cache);
out_free_pr_reg_cache:
	kmem_cache_destroy(t10_pr_reg_cache);
out_free_ua_cache:
	kmem_cache_destroy(se_ua_cache);
out_free_sess_cache:
	kmem_cache_destroy(se_sess_cache);
out:
	return -ENOMEM;
}

void release_se_kmem_caches(void)
{
	destroy_workqueue(target_completion_wq);
	kmem_cache_destroy(se_sess_cache);
	kmem_cache_destroy(se_ua_cache);
	kmem_cache_destroy(t10_pr_reg_cache);
	kmem_cache_destroy(t10_alua_lu_gp_cache);
	kmem_cache_destroy(t10_alua_lu_gp_mem_cache);
	kmem_cache_destroy(t10_alua_tg_pt_gp_cache);
	kmem_cache_destroy(t10_alua_tg_pt_gp_mem_cache);
}

/* This code ensures unique mib indexes are handed out. */
static DEFINE_SPINLOCK(scsi_mib_index_lock);
static u32 scsi_mib_index[SCSI_INDEX_TYPE_MAX];

/*
 * Allocate a new row index for the entry type specified
 */
u32 scsi_get_new_index(scsi_index_t type)
{
	u32 new_index;

	BUG_ON((type < 0) || (type >= SCSI_INDEX_TYPE_MAX));

	spin_lock(&scsi_mib_index_lock);
	new_index = ++scsi_mib_index[type];
	spin_unlock(&scsi_mib_index_lock);

	return new_index;
}

static void transport_init_queue_obj(struct se_queue_obj *qobj)
{
	atomic_set(&qobj->queue_cnt, 0);
	INIT_LIST_HEAD(&qobj->qobj_list);
	init_waitqueue_head(&qobj->thread_wq);
	spin_lock_init(&qobj->cmd_queue_lock);
}

void transport_subsystem_check_init(void)
{
	int ret;

	if (sub_api_initialized)
		return;

	ret = request_module("target_core_iblock");
	if (ret != 0)
		pr_err("Unable to load target_core_iblock\n");

	ret = request_module("target_core_file");
	if (ret != 0)
		pr_err("Unable to load target_core_file\n");

	ret = request_module("target_core_pscsi");
	if (ret != 0)
		pr_err("Unable to load target_core_pscsi\n");

	ret = request_module("target_core_stgt");
	if (ret != 0)
		pr_err("Unable to load target_core_stgt\n");

	sub_api_initialized = 1;
	return;
}

struct se_session *transport_init_session(void)
{
	struct se_session *se_sess;

	se_sess = kmem_cache_zalloc(se_sess_cache, GFP_KERNEL);
	if (!se_sess) {
		pr_err("Unable to allocate struct se_session from"
				" se_sess_cache\n");
		return ERR_PTR(-ENOMEM);
	}
	INIT_LIST_HEAD(&se_sess->sess_list);
	INIT_LIST_HEAD(&se_sess->sess_acl_list);
	INIT_LIST_HEAD(&se_sess->sess_cmd_list);
	INIT_LIST_HEAD(&se_sess->sess_wait_list);
	spin_lock_init(&se_sess->sess_cmd_lock);
	kref_init(&se_sess->sess_kref);

	return se_sess;
}
EXPORT_SYMBOL(transport_init_session);

/*
 * Called with spin_lock_irqsave(&struct se_portal_group->session_lock called.
 */
void __transport_register_session(
	struct se_portal_group *se_tpg,
	struct se_node_acl *se_nacl,
	struct se_session *se_sess,
	void *fabric_sess_ptr)
{
	unsigned char buf[PR_REG_ISID_LEN];

	se_sess->se_tpg = se_tpg;
	se_sess->fabric_sess_ptr = fabric_sess_ptr;
	/*
	 * Used by struct se_node_acl's under ConfigFS to locate active se_session-t
	 *
	 * Only set for struct se_session's that will actually be moving I/O.
	 * eg: *NOT* discovery sessions.
	 */
	if (se_nacl) {
		/*
		 * If the fabric module supports an ISID based TransportID,
		 * save this value in binary from the fabric I_T Nexus now.
		 */
		if (se_tpg->se_tpg_tfo->sess_get_initiator_sid != NULL) {
			memset(&buf[0], 0, PR_REG_ISID_LEN);
			se_tpg->se_tpg_tfo->sess_get_initiator_sid(se_sess,
					&buf[0], PR_REG_ISID_LEN);
			se_sess->sess_bin_isid = get_unaligned_be64(&buf[0]);
		}
		kref_get(&se_nacl->acl_kref);

		spin_lock_irq(&se_nacl->nacl_sess_lock);
		/*
		 * The se_nacl->nacl_sess pointer will be set to the
		 * last active I_T Nexus for each struct se_node_acl.
		 */
		se_nacl->nacl_sess = se_sess;

		list_add_tail(&se_sess->sess_acl_list,
			      &se_nacl->acl_sess_list);
		spin_unlock_irq(&se_nacl->nacl_sess_lock);
	}
	list_add_tail(&se_sess->sess_list, &se_tpg->tpg_sess_list);

	pr_debug("TARGET_CORE[%s]: Registered fabric_sess_ptr: %p\n",
		se_tpg->se_tpg_tfo->get_fabric_name(), se_sess->fabric_sess_ptr);
}
EXPORT_SYMBOL(__transport_register_session);

void transport_register_session(
	struct se_portal_group *se_tpg,
	struct se_node_acl *se_nacl,
	struct se_session *se_sess,
	void *fabric_sess_ptr)
{
	unsigned long flags;

	spin_lock_irqsave(&se_tpg->session_lock, flags);
	__transport_register_session(se_tpg, se_nacl, se_sess, fabric_sess_ptr);
	spin_unlock_irqrestore(&se_tpg->session_lock, flags);
}
EXPORT_SYMBOL(transport_register_session);

static void target_release_session(struct kref *kref)
{
	struct se_session *se_sess = container_of(kref,
			struct se_session, sess_kref);
	struct se_portal_group *se_tpg = se_sess->se_tpg;

	se_tpg->se_tpg_tfo->close_session(se_sess);
}

void target_get_session(struct se_session *se_sess)
{
	kref_get(&se_sess->sess_kref);
}
EXPORT_SYMBOL(target_get_session);

void target_put_session(struct se_session *se_sess)
{
	kref_put(&se_sess->sess_kref, target_release_session);
}
EXPORT_SYMBOL(target_put_session);

static void target_complete_nacl(struct kref *kref)
{
	struct se_node_acl *nacl = container_of(kref,
				struct se_node_acl, acl_kref);

	complete(&nacl->acl_free_comp);
}

void target_put_nacl(struct se_node_acl *nacl)
{
	kref_put(&nacl->acl_kref, target_complete_nacl);
}

void transport_deregister_session_configfs(struct se_session *se_sess)
{
	struct se_node_acl *se_nacl;
	unsigned long flags;
	/*
	 * Used by struct se_node_acl's under ConfigFS to locate active struct se_session
	 */
	se_nacl = se_sess->se_node_acl;
	if (se_nacl) {
		spin_lock_irqsave(&se_nacl->nacl_sess_lock, flags);
		if (se_nacl->acl_stop == 0)
			list_del(&se_sess->sess_acl_list);
		/*
		 * If the session list is empty, then clear the pointer.
		 * Otherwise, set the struct se_session pointer from the tail
		 * element of the per struct se_node_acl active session list.
		 */
		if (list_empty(&se_nacl->acl_sess_list))
			se_nacl->nacl_sess = NULL;
		else {
			se_nacl->nacl_sess = container_of(
					se_nacl->acl_sess_list.prev,
					struct se_session, sess_acl_list);
		}
		spin_unlock_irqrestore(&se_nacl->nacl_sess_lock, flags);
	}
}
EXPORT_SYMBOL(transport_deregister_session_configfs);

void transport_free_session(struct se_session *se_sess)
{
	kmem_cache_free(se_sess_cache, se_sess);
}
EXPORT_SYMBOL(transport_free_session);

void transport_deregister_session(struct se_session *se_sess)
{
	struct se_portal_group *se_tpg = se_sess->se_tpg;
	struct target_core_fabric_ops *se_tfo;
	struct se_node_acl *se_nacl;
	unsigned long flags;
	bool comp_nacl = true;

	if (!se_tpg) {
		transport_free_session(se_sess);
		return;
	}
	se_tfo = se_tpg->se_tpg_tfo;

	spin_lock_irqsave(&se_tpg->session_lock, flags);
	list_del(&se_sess->sess_list);
	se_sess->se_tpg = NULL;
	se_sess->fabric_sess_ptr = NULL;
	spin_unlock_irqrestore(&se_tpg->session_lock, flags);

	/*
	 * Determine if we need to do extra work for this initiator node's
	 * struct se_node_acl if it had been previously dynamically generated.
	 */
	se_nacl = se_sess->se_node_acl;

	spin_lock_irqsave(&se_tpg->acl_node_lock, flags);
	if (se_nacl && se_nacl->dynamic_node_acl) {
		if (!se_tfo->tpg_check_demo_mode_cache(se_tpg)) {
			list_del(&se_nacl->acl_list);
			se_tpg->num_node_acls--;
			spin_unlock_irqrestore(&se_tpg->acl_node_lock, flags);
			core_tpg_wait_for_nacl_pr_ref(se_nacl);
			core_free_device_list_for_node(se_nacl, se_tpg);
			se_tfo->tpg_release_fabric_acl(se_tpg, se_nacl);

			comp_nacl = false;
			spin_lock_irqsave(&se_tpg->acl_node_lock, flags);
		}
	}
	spin_unlock_irqrestore(&se_tpg->acl_node_lock, flags);

	pr_debug("TARGET_CORE[%s]: Deregistered fabric_sess\n",
		se_tpg->se_tpg_tfo->get_fabric_name());
	/*
	 * If last kref is dropping now for an explict NodeACL, awake sleeping
	 * ->acl_free_comp caller to wakeup configfs se_node_acl->acl_group
	 * removal context.
	 */
	if (se_nacl && comp_nacl == true)
		target_put_nacl(se_nacl);

	transport_free_session(se_sess);
}
EXPORT_SYMBOL(transport_deregister_session);

/*
 * Called with cmd->t_state_lock held.
 */
static void target_remove_from_state_list(struct se_cmd *cmd)
{
	struct se_device *dev = cmd->se_dev;
	unsigned long flags;

	if (!dev)
		return;

	if (cmd->transport_state & CMD_T_BUSY)
		return;

	spin_lock_irqsave(&dev->execute_task_lock, flags);
	if (cmd->state_active) {
		list_del(&cmd->state_list);
		cmd->state_active = false;
	}
	spin_unlock_irqrestore(&dev->execute_task_lock, flags);
}

/*	transport_cmd_check_stop():
 *
 *	'transport_off = 1' determines if CMD_T_ACTIVE should be cleared.
 *	'transport_off = 2' determines if task_dev_state should be removed.
 *
 *	A non-zero u8 t_state sets cmd->t_state.
 *	Returns 1 when command is stopped, else 0.
 */
static int transport_cmd_check_stop(
	struct se_cmd *cmd,
	int transport_off,
	u8 t_state)
{
	unsigned long flags;

	spin_lock_irqsave(&cmd->t_state_lock, flags);
	/*
	 * Determine if IOCTL context caller in requesting the stopping of this
	 * command for LUN shutdown purposes.
	 */
	if (cmd->transport_state & CMD_T_LUN_STOP) {
		pr_debug("%s:%d CMD_T_LUN_STOP for ITT: 0x%08x\n",
			__func__, __LINE__, cmd->se_tfo->get_task_tag(cmd));

		cmd->transport_state &= ~CMD_T_ACTIVE;
		if (transport_off == 2)
			target_remove_from_state_list(cmd);
		spin_unlock_irqrestore(&cmd->t_state_lock, flags);

		complete(&cmd->transport_lun_stop_comp);
		return 1;
	}
	/*
	 * Determine if frontend context caller is requesting the stopping of
	 * this command for frontend exceptions.
	 */
	if (cmd->transport_state & CMD_T_STOP) {
		pr_debug("%s:%d CMD_T_STOP for ITT: 0x%08x\n",
			__func__, __LINE__,
			cmd->se_tfo->get_task_tag(cmd));

		if (transport_off == 2)
			target_remove_from_state_list(cmd);

		/*
		 * Clear struct se_cmd->se_lun before the transport_off == 2 handoff
		 * to FE.
		 */
		if (transport_off == 2)
			cmd->se_lun = NULL;
		spin_unlock_irqrestore(&cmd->t_state_lock, flags);

		complete(&cmd->t_transport_stop_comp);
		return 1;
	}
	if (transport_off) {
		cmd->transport_state &= ~CMD_T_ACTIVE;
		if (transport_off == 2) {
			target_remove_from_state_list(cmd);
			/*
			 * Clear struct se_cmd->se_lun before the transport_off == 2
			 * handoff to fabric module.
			 */
			cmd->se_lun = NULL;
			/*
			 * Some fabric modules like tcm_loop can release
			 * their internally allocated I/O reference now and
			 * struct se_cmd now.
			 *
			 * Fabric modules are expected to return '1' here if the
			 * se_cmd being passed is released at this point,
			 * or zero if not being released.
			 */
			if (cmd->se_tfo->check_stop_free != NULL) {
				spin_unlock_irqrestore(
					&cmd->t_state_lock, flags);

				return cmd->se_tfo->check_stop_free(cmd);
			}
		}
		spin_unlock_irqrestore(&cmd->t_state_lock, flags);

		return 0;
	} else if (t_state)
		cmd->t_state = t_state;
	spin_unlock_irqrestore(&cmd->t_state_lock, flags);

	return 0;
}

static int transport_cmd_check_stop_to_fabric(struct se_cmd *cmd)
{
	return transport_cmd_check_stop(cmd, 2, 0);
}

static void transport_lun_remove_cmd(struct se_cmd *cmd)
{
	struct se_lun *lun = cmd->se_lun;
	unsigned long flags;

	if (!lun)
		return;

	spin_lock_irqsave(&cmd->t_state_lock, flags);
	if (cmd->transport_state & CMD_T_DEV_ACTIVE) {
		cmd->transport_state &= ~CMD_T_DEV_ACTIVE;
		target_remove_from_state_list(cmd);
	}
	spin_unlock_irqrestore(&cmd->t_state_lock, flags);

	spin_lock_irqsave(&lun->lun_cmd_lock, flags);
	if (!list_empty(&cmd->se_lun_node))
		list_del_init(&cmd->se_lun_node);
	spin_unlock_irqrestore(&lun->lun_cmd_lock, flags);
}

void transport_cmd_finish_abort(struct se_cmd *cmd, int remove)
{
	if (!(cmd->se_cmd_flags & SCF_SCSI_TMR_CDB))
		transport_lun_remove_cmd(cmd);

	if (transport_cmd_check_stop_to_fabric(cmd))
		return;
	if (remove) {
		transport_remove_cmd_from_queue(cmd);
		transport_put_cmd(cmd);
	}
}

static void transport_add_cmd_to_queue(struct se_cmd *cmd, int t_state,
		bool at_head)
{
	struct se_device *dev = cmd->se_dev;
	struct se_queue_obj *qobj = &dev->dev_queue_obj;
	unsigned long flags;

	if (t_state) {
		spin_lock_irqsave(&cmd->t_state_lock, flags);
		cmd->t_state = t_state;
		cmd->transport_state |= CMD_T_ACTIVE;
		spin_unlock_irqrestore(&cmd->t_state_lock, flags);
	}

	spin_lock_irqsave(&qobj->cmd_queue_lock, flags);

	/* If the cmd is already on the list, remove it before we add it */
	if (!list_empty(&cmd->se_queue_node))
		list_del(&cmd->se_queue_node);
	else
		atomic_inc(&qobj->queue_cnt);

	if (at_head)
		list_add(&cmd->se_queue_node, &qobj->qobj_list);
	else
		list_add_tail(&cmd->se_queue_node, &qobj->qobj_list);
	cmd->transport_state |= CMD_T_QUEUED;
	spin_unlock_irqrestore(&qobj->cmd_queue_lock, flags);

	wake_up_interruptible(&qobj->thread_wq);
}

static struct se_cmd *
transport_get_cmd_from_queue(struct se_queue_obj *qobj)
{
	struct se_cmd *cmd;
	unsigned long flags;

	spin_lock_irqsave(&qobj->cmd_queue_lock, flags);
	if (list_empty(&qobj->qobj_list)) {
		spin_unlock_irqrestore(&qobj->cmd_queue_lock, flags);
		return NULL;
	}
	cmd = list_first_entry(&qobj->qobj_list, struct se_cmd, se_queue_node);

	cmd->transport_state &= ~CMD_T_QUEUED;
	list_del_init(&cmd->se_queue_node);
	atomic_dec(&qobj->queue_cnt);
	spin_unlock_irqrestore(&qobj->cmd_queue_lock, flags);

	return cmd;
}

static void transport_remove_cmd_from_queue(struct se_cmd *cmd)
{
	struct se_queue_obj *qobj = &cmd->se_dev->dev_queue_obj;
	unsigned long flags;

	spin_lock_irqsave(&qobj->cmd_queue_lock, flags);
	if (!(cmd->transport_state & CMD_T_QUEUED)) {
		spin_unlock_irqrestore(&qobj->cmd_queue_lock, flags);
		return;
	}
	cmd->transport_state &= ~CMD_T_QUEUED;
	atomic_dec(&qobj->queue_cnt);
	list_del_init(&cmd->se_queue_node);
	spin_unlock_irqrestore(&qobj->cmd_queue_lock, flags);
}

static void target_complete_failure_work(struct work_struct *work)
{
	struct se_cmd *cmd = container_of(work, struct se_cmd, work);

	transport_generic_request_failure(cmd);
}

void target_complete_cmd(struct se_cmd *cmd, u8 scsi_status)
{
	struct se_device *dev = cmd->se_dev;
	int success = scsi_status == GOOD;
	unsigned long flags;

	cmd->scsi_status = scsi_status;


	spin_lock_irqsave(&cmd->t_state_lock, flags);
	cmd->transport_state &= ~CMD_T_BUSY;

	if (dev && dev->transport->transport_complete) {
		if (dev->transport->transport_complete(cmd,
				cmd->t_data_sg) != 0) {
			cmd->se_cmd_flags |= SCF_TRANSPORT_TASK_SENSE;
			success = 1;
		}
	}

	/*
	 * See if we are waiting to complete for an exception condition.
	 */
	if (cmd->transport_state & CMD_T_REQUEST_STOP) {
		spin_unlock_irqrestore(&cmd->t_state_lock, flags);
		complete(&cmd->task_stop_comp);
		return;
	}

	if (!success)
		cmd->transport_state |= CMD_T_FAILED;

	/*
	 * Check for case where an explict ABORT_TASK has been received
	 * and transport_wait_for_tasks() will be waiting for completion..
	 */
	if (cmd->transport_state & CMD_T_ABORTED &&
	    cmd->transport_state & CMD_T_STOP) {
		spin_unlock_irqrestore(&cmd->t_state_lock, flags);
		complete(&cmd->t_transport_stop_comp);
		return;
	} else if (cmd->transport_state & CMD_T_FAILED) {
		cmd->scsi_sense_reason = TCM_LOGICAL_UNIT_COMMUNICATION_FAILURE;
		INIT_WORK(&cmd->work, target_complete_failure_work);
	} else {
		INIT_WORK(&cmd->work, target_complete_ok_work);
	}

	cmd->t_state = TRANSPORT_COMPLETE;
	cmd->transport_state |= (CMD_T_COMPLETE | CMD_T_ACTIVE);
	spin_unlock_irqrestore(&cmd->t_state_lock, flags);

	queue_work(target_completion_wq, &cmd->work);
}
EXPORT_SYMBOL(target_complete_cmd);

static void target_add_to_state_list(struct se_cmd *cmd)
{
	struct se_device *dev = cmd->se_dev;
	unsigned long flags;

	spin_lock_irqsave(&dev->execute_task_lock, flags);
	if (!cmd->state_active) {
		list_add_tail(&cmd->state_list, &dev->state_list);
		cmd->state_active = true;
	}
	spin_unlock_irqrestore(&dev->execute_task_lock, flags);
}

static void __target_add_to_execute_list(struct se_cmd *cmd)
{
	struct se_device *dev = cmd->se_dev;
	bool head_of_queue = false;

	if (!list_empty(&cmd->execute_list))
		return;

	if (dev->dev_task_attr_type == SAM_TASK_ATTR_EMULATED &&
	    cmd->sam_task_attr == MSG_HEAD_TAG)
		head_of_queue = true;

	if (head_of_queue)
		list_add(&cmd->execute_list, &dev->execute_list);
	else
		list_add_tail(&cmd->execute_list, &dev->execute_list);

	atomic_inc(&dev->execute_tasks);

	if (cmd->state_active)
		return;

	if (head_of_queue)
		list_add(&cmd->state_list, &dev->state_list);
	else
		list_add_tail(&cmd->state_list, &dev->state_list);

	cmd->state_active = true;
}

static void target_add_to_execute_list(struct se_cmd *cmd)
{
	unsigned long flags;
	struct se_device *dev = cmd->se_dev;

	spin_lock_irqsave(&dev->execute_task_lock, flags);
	__target_add_to_execute_list(cmd);
	spin_unlock_irqrestore(&dev->execute_task_lock, flags);
}

void __target_remove_from_execute_list(struct se_cmd *cmd)
{
	list_del_init(&cmd->execute_list);
	atomic_dec(&cmd->se_dev->execute_tasks);
}

static void target_remove_from_execute_list(struct se_cmd *cmd)
{
	struct se_device *dev = cmd->se_dev;
	unsigned long flags;

	if (WARN_ON(list_empty(&cmd->execute_list)))
		return;

	spin_lock_irqsave(&dev->execute_task_lock, flags);
	__target_remove_from_execute_list(cmd);
	spin_unlock_irqrestore(&dev->execute_task_lock, flags);
}

/*
 * Handle QUEUE_FULL / -EAGAIN and -ENOMEM status
 */

static void target_qf_do_work(struct work_struct *work)
{
	struct se_device *dev = container_of(work, struct se_device,
					qf_work_queue);
	LIST_HEAD(qf_cmd_list);
	struct se_cmd *cmd, *cmd_tmp;

	spin_lock_irq(&dev->qf_cmd_lock);
	list_splice_init(&dev->qf_cmd_list, &qf_cmd_list);
	spin_unlock_irq(&dev->qf_cmd_lock);

	list_for_each_entry_safe(cmd, cmd_tmp, &qf_cmd_list, se_qf_node) {
		list_del(&cmd->se_qf_node);
		atomic_dec(&dev->dev_qf_count);
		smp_mb__after_atomic_dec();

		pr_debug("Processing %s cmd: %p QUEUE_FULL in work queue"
			" context: %s\n", cmd->se_tfo->get_fabric_name(), cmd,
			(cmd->t_state == TRANSPORT_COMPLETE_QF_OK) ? "COMPLETE_OK" :
			(cmd->t_state == TRANSPORT_COMPLETE_QF_WP) ? "WRITE_PENDING"
			: "UNKNOWN");

		transport_add_cmd_to_queue(cmd, cmd->t_state, true);
	}
}

unsigned char *transport_dump_cmd_direction(struct se_cmd *cmd)
{
	switch (cmd->data_direction) {
	case DMA_NONE:
		return "NONE";
	case DMA_FROM_DEVICE:
		return "READ";
	case DMA_TO_DEVICE:
		return "WRITE";
	case DMA_BIDIRECTIONAL:
		return "BIDI";
	default:
		break;
	}

	return "UNKNOWN";
}

void transport_dump_dev_state(
	struct se_device *dev,
	char *b,
	int *bl)
{
	*bl += sprintf(b + *bl, "Status: ");
	switch (dev->dev_status) {
	case TRANSPORT_DEVICE_ACTIVATED:
		*bl += sprintf(b + *bl, "ACTIVATED");
		break;
	case TRANSPORT_DEVICE_DEACTIVATED:
		*bl += sprintf(b + *bl, "DEACTIVATED");
		break;
	case TRANSPORT_DEVICE_SHUTDOWN:
		*bl += sprintf(b + *bl, "SHUTDOWN");
		break;
	case TRANSPORT_DEVICE_OFFLINE_ACTIVATED:
	case TRANSPORT_DEVICE_OFFLINE_DEACTIVATED:
		*bl += sprintf(b + *bl, "OFFLINE");
		break;
	default:
		*bl += sprintf(b + *bl, "UNKNOWN=%d", dev->dev_status);
		break;
	}

	*bl += sprintf(b + *bl, "  Execute/Max Queue Depth: %d/%d",
		atomic_read(&dev->execute_tasks), dev->queue_depth);
	*bl += sprintf(b + *bl, "  SectorSize: %u  HwMaxSectors: %u\n",
		dev->se_sub_dev->se_dev_attrib.block_size,
		dev->se_sub_dev->se_dev_attrib.hw_max_sectors);
	*bl += sprintf(b + *bl, "        ");
}

void transport_dump_vpd_proto_id(
	struct t10_vpd *vpd,
	unsigned char *p_buf,
	int p_buf_len)
{
	unsigned char buf[VPD_TMP_BUF_SIZE];
	int len;

	memset(buf, 0, VPD_TMP_BUF_SIZE);
	len = sprintf(buf, "T10 VPD Protocol Identifier: ");

	switch (vpd->protocol_identifier) {
	case 0x00:
		sprintf(buf+len, "Fibre Channel\n");
		break;
	case 0x10:
		sprintf(buf+len, "Parallel SCSI\n");
		break;
	case 0x20:
		sprintf(buf+len, "SSA\n");
		break;
	case 0x30:
		sprintf(buf+len, "IEEE 1394\n");
		break;
	case 0x40:
		sprintf(buf+len, "SCSI Remote Direct Memory Access"
				" Protocol\n");
		break;
	case 0x50:
		sprintf(buf+len, "Internet SCSI (iSCSI)\n");
		break;
	case 0x60:
		sprintf(buf+len, "SAS Serial SCSI Protocol\n");
		break;
	case 0x70:
		sprintf(buf+len, "Automation/Drive Interface Transport"
				" Protocol\n");
		break;
	case 0x80:
		sprintf(buf+len, "AT Attachment Interface ATA/ATAPI\n");
		break;
	default:
		sprintf(buf+len, "Unknown 0x%02x\n",
				vpd->protocol_identifier);
		break;
	}

	if (p_buf)
		strncpy(p_buf, buf, p_buf_len);
	else
		pr_debug("%s", buf);
}

void
transport_set_vpd_proto_id(struct t10_vpd *vpd, unsigned char *page_83)
{
	/*
	 * Check if the Protocol Identifier Valid (PIV) bit is set..
	 *
	 * from spc3r23.pdf section 7.5.1
	 */
	 if (page_83[1] & 0x80) {
		vpd->protocol_identifier = (page_83[0] & 0xf0);
		vpd->protocol_identifier_set = 1;
		transport_dump_vpd_proto_id(vpd, NULL, 0);
	}
}
EXPORT_SYMBOL(transport_set_vpd_proto_id);

int transport_dump_vpd_assoc(
	struct t10_vpd *vpd,
	unsigned char *p_buf,
	int p_buf_len)
{
	unsigned char buf[VPD_TMP_BUF_SIZE];
	int ret = 0;
	int len;

	memset(buf, 0, VPD_TMP_BUF_SIZE);
	len = sprintf(buf, "T10 VPD Identifier Association: ");

	switch (vpd->association) {
	case 0x00:
		sprintf(buf+len, "addressed logical unit\n");
		break;
	case 0x10:
		sprintf(buf+len, "target port\n");
		break;
	case 0x20:
		sprintf(buf+len, "SCSI target device\n");
		break;
	default:
		sprintf(buf+len, "Unknown 0x%02x\n", vpd->association);
		ret = -EINVAL;
		break;
	}

	if (p_buf)
		strncpy(p_buf, buf, p_buf_len);
	else
		pr_debug("%s", buf);

	return ret;
}

int transport_set_vpd_assoc(struct t10_vpd *vpd, unsigned char *page_83)
{
	/*
	 * The VPD identification association..
	 *
	 * from spc3r23.pdf Section 7.6.3.1 Table 297
	 */
	vpd->association = (page_83[1] & 0x30);
	return transport_dump_vpd_assoc(vpd, NULL, 0);
}
EXPORT_SYMBOL(transport_set_vpd_assoc);

int transport_dump_vpd_ident_type(
	struct t10_vpd *vpd,
	unsigned char *p_buf,
	int p_buf_len)
{
	unsigned char buf[VPD_TMP_BUF_SIZE];
	int ret = 0;
	int len;

	memset(buf, 0, VPD_TMP_BUF_SIZE);
	len = sprintf(buf, "T10 VPD Identifier Type: ");

	switch (vpd->device_identifier_type) {
	case 0x00:
		sprintf(buf+len, "Vendor specific\n");
		break;
	case 0x01:
		sprintf(buf+len, "T10 Vendor ID based\n");
		break;
	case 0x02:
		sprintf(buf+len, "EUI-64 based\n");
		break;
	case 0x03:
		sprintf(buf+len, "NAA\n");
		break;
	case 0x04:
		sprintf(buf+len, "Relative target port identifier\n");
		break;
	case 0x08:
		sprintf(buf+len, "SCSI name string\n");
		break;
	default:
		sprintf(buf+len, "Unsupported: 0x%02x\n",
				vpd->device_identifier_type);
		ret = -EINVAL;
		break;
	}

	if (p_buf) {
		if (p_buf_len < strlen(buf)+1)
			return -EINVAL;
		strncpy(p_buf, buf, p_buf_len);
	} else {
		pr_debug("%s", buf);
	}

	return ret;
}

int transport_set_vpd_ident_type(struct t10_vpd *vpd, unsigned char *page_83)
{
	/*
	 * The VPD identifier type..
	 *
	 * from spc3r23.pdf Section 7.6.3.1 Table 298
	 */
	vpd->device_identifier_type = (page_83[1] & 0x0f);
	return transport_dump_vpd_ident_type(vpd, NULL, 0);
}
EXPORT_SYMBOL(transport_set_vpd_ident_type);

int transport_dump_vpd_ident(
	struct t10_vpd *vpd,
	unsigned char *p_buf,
	int p_buf_len)
{
	unsigned char buf[VPD_TMP_BUF_SIZE];
	int ret = 0;

	memset(buf, 0, VPD_TMP_BUF_SIZE);

	switch (vpd->device_identifier_code_set) {
	case 0x01: /* Binary */
		sprintf(buf, "T10 VPD Binary Device Identifier: %s\n",
			&vpd->device_identifier[0]);
		break;
	case 0x02: /* ASCII */
		sprintf(buf, "T10 VPD ASCII Device Identifier: %s\n",
			&vpd->device_identifier[0]);
		break;
	case 0x03: /* UTF-8 */
		sprintf(buf, "T10 VPD UTF-8 Device Identifier: %s\n",
			&vpd->device_identifier[0]);
		break;
	default:
		sprintf(buf, "T10 VPD Device Identifier encoding unsupported:"
			" 0x%02x", vpd->device_identifier_code_set);
		ret = -EINVAL;
		break;
	}

	if (p_buf)
		strncpy(p_buf, buf, p_buf_len);
	else
		pr_debug("%s", buf);

	return ret;
}

int
transport_set_vpd_ident(struct t10_vpd *vpd, unsigned char *page_83)
{
	static const char hex_str[] = "0123456789abcdef";
	int j = 0, i = 4; /* offset to start of the identifer */

	/*
	 * The VPD Code Set (encoding)
	 *
	 * from spc3r23.pdf Section 7.6.3.1 Table 296
	 */
	vpd->device_identifier_code_set = (page_83[0] & 0x0f);
	switch (vpd->device_identifier_code_set) {
	case 0x01: /* Binary */
		vpd->device_identifier[j++] =
				hex_str[vpd->device_identifier_type];
		while (i < (4 + page_83[3])) {
			vpd->device_identifier[j++] =
				hex_str[(page_83[i] & 0xf0) >> 4];
			vpd->device_identifier[j++] =
				hex_str[page_83[i] & 0x0f];
			i++;
		}
		break;
	case 0x02: /* ASCII */
	case 0x03: /* UTF-8 */
		while (i < (4 + page_83[3]))
			vpd->device_identifier[j++] = page_83[i++];
		break;
	default:
		break;
	}

	return transport_dump_vpd_ident(vpd, NULL, 0);
}
EXPORT_SYMBOL(transport_set_vpd_ident);

static void core_setup_task_attr_emulation(struct se_device *dev)
{
	/*
	 * If this device is from Target_Core_Mod/pSCSI, disable the
	 * SAM Task Attribute emulation.
	 *
	 * This is currently not available in upsream Linux/SCSI Target
	 * mode code, and is assumed to be disabled while using TCM/pSCSI.
	 */
	if (dev->transport->transport_type == TRANSPORT_PLUGIN_PHBA_PDEV) {
		dev->dev_task_attr_type = SAM_TASK_ATTR_PASSTHROUGH;
		return;
	}

	dev->dev_task_attr_type = SAM_TASK_ATTR_EMULATED;
	pr_debug("%s: Using SAM_TASK_ATTR_EMULATED for SPC: 0x%02x"
		" device\n", dev->transport->name,
		dev->transport->get_device_rev(dev));
}

static void scsi_dump_inquiry(struct se_device *dev)
{
	struct t10_wwn *wwn = &dev->se_sub_dev->t10_wwn;
	char buf[17];
	int i, device_type;
	/*
	 * Print Linux/SCSI style INQUIRY formatting to the kernel ring buffer
	 */
	for (i = 0; i < 8; i++)
		if (wwn->vendor[i] >= 0x20)
			buf[i] = wwn->vendor[i];
		else
			buf[i] = ' ';
	buf[i] = '\0';
	pr_debug("  Vendor: %s\n", buf);

	for (i = 0; i < 16; i++)
		if (wwn->model[i] >= 0x20)
			buf[i] = wwn->model[i];
		else
			buf[i] = ' ';
	buf[i] = '\0';
	pr_debug("  Model: %s\n", buf);

	for (i = 0; i < 4; i++)
		if (wwn->revision[i] >= 0x20)
			buf[i] = wwn->revision[i];
		else
			buf[i] = ' ';
	buf[i] = '\0';
	pr_debug("  Revision: %s\n", buf);

	device_type = dev->transport->get_device_type(dev);
	pr_debug("  Type:   %s ", scsi_device_type(device_type));
	pr_debug("                 ANSI SCSI revision: %02x\n",
				dev->transport->get_device_rev(dev));
}

struct se_device *transport_add_device_to_core_hba(
	struct se_hba *hba,
	struct se_subsystem_api *transport,
	struct se_subsystem_dev *se_dev,
	u32 device_flags,
	void *transport_dev,
	struct se_dev_limits *dev_limits,
	const char *inquiry_prod,
	const char *inquiry_rev)
{
	int force_pt;
	struct se_device  *dev;

	dev = kzalloc(sizeof(struct se_device), GFP_KERNEL);
	if (!dev) {
		pr_err("Unable to allocate memory for se_dev_t\n");
		return NULL;
	}

	transport_init_queue_obj(&dev->dev_queue_obj);
	dev->dev_flags		= device_flags;
	dev->dev_status		|= TRANSPORT_DEVICE_DEACTIVATED;
	dev->dev_ptr		= transport_dev;
	dev->se_hba		= hba;
	dev->se_sub_dev		= se_dev;
	dev->transport		= transport;
	INIT_LIST_HEAD(&dev->dev_list);
	INIT_LIST_HEAD(&dev->dev_sep_list);
	INIT_LIST_HEAD(&dev->dev_tmr_list);
	INIT_LIST_HEAD(&dev->execute_list);
	INIT_LIST_HEAD(&dev->delayed_cmd_list);
	INIT_LIST_HEAD(&dev->state_list);
	INIT_LIST_HEAD(&dev->qf_cmd_list);
	spin_lock_init(&dev->execute_task_lock);
	spin_lock_init(&dev->delayed_cmd_lock);
	spin_lock_init(&dev->dev_reservation_lock);
	spin_lock_init(&dev->dev_status_lock);
	spin_lock_init(&dev->se_port_lock);
	spin_lock_init(&dev->se_tmr_lock);
	spin_lock_init(&dev->qf_cmd_lock);
	atomic_set(&dev->dev_ordered_id, 0);

	se_dev_set_default_attribs(dev, dev_limits);

	dev->dev_index = scsi_get_new_index(SCSI_DEVICE_INDEX);
	dev->creation_time = get_jiffies_64();
	spin_lock_init(&dev->stats_lock);

	spin_lock(&hba->device_lock);
	list_add_tail(&dev->dev_list, &hba->hba_dev_list);
	hba->dev_count++;
	spin_unlock(&hba->device_lock);
	/*
	 * Setup the SAM Task Attribute emulation for struct se_device
	 */
	core_setup_task_attr_emulation(dev);
	/*
	 * Force PR and ALUA passthrough emulation with internal object use.
	 */
	force_pt = (hba->hba_flags & HBA_FLAGS_INTERNAL_USE);
	/*
	 * Setup the Reservations infrastructure for struct se_device
	 */
	core_setup_reservations(dev, force_pt);
	/*
	 * Setup the Asymmetric Logical Unit Assignment for struct se_device
	 */
	if (core_setup_alua(dev, force_pt) < 0)
		goto out;

	/*
	 * Startup the struct se_device processing thread
	 */
	dev->process_thread = kthread_run(transport_processing_thread, dev,
					  "LIO_%s", dev->transport->name);
	if (IS_ERR(dev->process_thread)) {
		pr_err("Unable to create kthread: LIO_%s\n",
			dev->transport->name);
		goto out;
	}
	/*
	 * Setup work_queue for QUEUE_FULL
	 */
	INIT_WORK(&dev->qf_work_queue, target_qf_do_work);
	/*
	 * Preload the initial INQUIRY const values if we are doing
	 * anything virtual (IBLOCK, FILEIO, RAMDISK), but not for TCM/pSCSI
	 * passthrough because this is being provided by the backend LLD.
	 * This is required so that transport_get_inquiry() copies these
	 * originals once back into DEV_T10_WWN(dev) for the virtual device
	 * setup.
	 */
	if (dev->transport->transport_type != TRANSPORT_PLUGIN_PHBA_PDEV) {
		if (!inquiry_prod || !inquiry_rev) {
			pr_err("All non TCM/pSCSI plugins require"
				" INQUIRY consts\n");
			goto out;
		}

		strncpy(&dev->se_sub_dev->t10_wwn.vendor[0], "LIO-ORG", 8);
		strncpy(&dev->se_sub_dev->t10_wwn.model[0], inquiry_prod, 16);
		strncpy(&dev->se_sub_dev->t10_wwn.revision[0], inquiry_rev, 4);
	}
	scsi_dump_inquiry(dev);

	return dev;
out:
	kthread_stop(dev->process_thread);

	spin_lock(&hba->device_lock);
	list_del(&dev->dev_list);
	hba->dev_count--;
	spin_unlock(&hba->device_lock);

	se_release_vpd_for_dev(dev);

	kfree(dev);

	return NULL;
}
EXPORT_SYMBOL(transport_add_device_to_core_hba);

/*	transport_generic_prepare_cdb():
 *
 *	Since the Initiator sees iSCSI devices as LUNs,  the SCSI CDB will
 *	contain the iSCSI LUN in bits 7-5 of byte 1 as per SAM-2.
 *	The point of this is since we are mapping iSCSI LUNs to
 *	SCSI Target IDs having a non-zero LUN in the CDB will throw the
 *	devices and HBAs for a loop.
 */
static inline void transport_generic_prepare_cdb(
	unsigned char *cdb)
{
	switch (cdb[0]) {
	case READ_10: /* SBC - RDProtect */
	case READ_12: /* SBC - RDProtect */
	case READ_16: /* SBC - RDProtect */
	case SEND_DIAGNOSTIC: /* SPC - SELF-TEST Code */
	case VERIFY: /* SBC - VRProtect */
	case VERIFY_16: /* SBC - VRProtect */
	case WRITE_VERIFY: /* SBC - VRProtect */
	case WRITE_VERIFY_12: /* SBC - VRProtect */
		break;
	default:
		cdb[1] &= 0x1f; /* clear logical unit number */
		break;
	}
}

static int transport_generic_cmd_sequencer(struct se_cmd *, unsigned char *);

/*
 * Used by fabric modules containing a local struct se_cmd within their
 * fabric dependent per I/O descriptor.
 */
void transport_init_se_cmd(
	struct se_cmd *cmd,
	struct target_core_fabric_ops *tfo,
	struct se_session *se_sess,
	u32 data_length,
	int data_direction,
	int task_attr,
	unsigned char *sense_buffer)
{
	INIT_LIST_HEAD(&cmd->se_lun_node);
	INIT_LIST_HEAD(&cmd->se_delayed_node);
	INIT_LIST_HEAD(&cmd->se_qf_node);
	INIT_LIST_HEAD(&cmd->se_queue_node);
	INIT_LIST_HEAD(&cmd->se_cmd_list);
	INIT_LIST_HEAD(&cmd->execute_list);
	INIT_LIST_HEAD(&cmd->state_list);
	init_completion(&cmd->transport_lun_fe_stop_comp);
	init_completion(&cmd->transport_lun_stop_comp);
	init_completion(&cmd->t_transport_stop_comp);
	init_completion(&cmd->cmd_wait_comp);
	init_completion(&cmd->task_stop_comp);
	spin_lock_init(&cmd->t_state_lock);
	cmd->transport_state = CMD_T_DEV_ACTIVE;

	cmd->se_tfo = tfo;
	cmd->se_sess = se_sess;
	cmd->data_length = data_length;
	cmd->data_direction = data_direction;
	cmd->sam_task_attr = task_attr;
	cmd->sense_buffer = sense_buffer;

	cmd->state_active = false;
}
EXPORT_SYMBOL(transport_init_se_cmd);

static int transport_check_alloc_task_attr(struct se_cmd *cmd)
{
	/*
	 * Check if SAM Task Attribute emulation is enabled for this
	 * struct se_device storage object
	 */
	if (cmd->se_dev->dev_task_attr_type != SAM_TASK_ATTR_EMULATED)
		return 0;

	if (cmd->sam_task_attr == MSG_ACA_TAG) {
		pr_debug("SAM Task Attribute ACA"
			" emulation is not supported\n");
		return -EINVAL;
	}
	/*
	 * Used to determine when ORDERED commands should go from
	 * Dormant to Active status.
	 */
	cmd->se_ordered_id = atomic_inc_return(&cmd->se_dev->dev_ordered_id);
	smp_mb__after_atomic_inc();
	pr_debug("Allocated se_ordered_id: %u for Task Attr: 0x%02x on %s\n",
			cmd->se_ordered_id, cmd->sam_task_attr,
			cmd->se_dev->transport->name);
	return 0;
}

/*	target_setup_cmd_from_cdb():
 *
 *	Called from fabric RX Thread.
 */
int target_setup_cmd_from_cdb(
	struct se_cmd *cmd,
	unsigned char *cdb)
{
	int ret;

	transport_generic_prepare_cdb(cdb);
	/*
	 * Ensure that the received CDB is less than the max (252 + 8) bytes
	 * for VARIABLE_LENGTH_CMD
	 */
	if (scsi_command_size(cdb) > SCSI_MAX_VARLEN_CDB_SIZE) {
		pr_err("Received SCSI CDB with command_size: %d that"
			" exceeds SCSI_MAX_VARLEN_CDB_SIZE: %d\n",
			scsi_command_size(cdb), SCSI_MAX_VARLEN_CDB_SIZE);
		cmd->se_cmd_flags |= SCF_SCSI_CDB_EXCEPTION;
		cmd->scsi_sense_reason = TCM_INVALID_CDB_FIELD;
		return -EINVAL;
	}
	/*
	 * If the received CDB is larger than TCM_MAX_COMMAND_SIZE,
	 * allocate the additional extended CDB buffer now..  Otherwise
	 * setup the pointer from __t_task_cdb to t_task_cdb.
	 */
	if (scsi_command_size(cdb) > sizeof(cmd->__t_task_cdb)) {
		cmd->t_task_cdb = kzalloc(scsi_command_size(cdb),
						GFP_KERNEL);
		if (!cmd->t_task_cdb) {
			pr_err("Unable to allocate cmd->t_task_cdb"
				" %u > sizeof(cmd->__t_task_cdb): %lu ops\n",
				scsi_command_size(cdb),
				(unsigned long)sizeof(cmd->__t_task_cdb));
			cmd->se_cmd_flags |= SCF_SCSI_CDB_EXCEPTION;
			cmd->scsi_sense_reason =
					TCM_LOGICAL_UNIT_COMMUNICATION_FAILURE;
			return -ENOMEM;
		}
	} else
		cmd->t_task_cdb = &cmd->__t_task_cdb[0];
	/*
	 * Copy the original CDB into cmd->
	 */
	memcpy(cmd->t_task_cdb, cdb, scsi_command_size(cdb));
	/*
	 * Setup the received CDB based on SCSI defined opcodes and
	 * perform unit attention, persistent reservations and ALUA
	 * checks for virtual device backends.  The cmd->t_task_cdb
	 * pointer is expected to be setup before we reach this point.
	 */
	ret = transport_generic_cmd_sequencer(cmd, cdb);
	if (ret < 0)
		return ret;
	/*
	 * Check for SAM Task Attribute Emulation
	 */
	if (transport_check_alloc_task_attr(cmd) < 0) {
		cmd->se_cmd_flags |= SCF_SCSI_CDB_EXCEPTION;
		cmd->scsi_sense_reason = TCM_INVALID_CDB_FIELD;
		return -EINVAL;
	}
	spin_lock(&cmd->se_lun->lun_sep_lock);
	if (cmd->se_lun->lun_sep)
		cmd->se_lun->lun_sep->sep_stats.cmd_pdus++;
	spin_unlock(&cmd->se_lun->lun_sep_lock);
	return 0;
}
EXPORT_SYMBOL(target_setup_cmd_from_cdb);

/*
 * Used by fabric module frontends to queue tasks directly.
 * Many only be used from process context only
 */
int transport_handle_cdb_direct(
	struct se_cmd *cmd)
{
	int ret;

	if (!cmd->se_lun) {
		dump_stack();
		pr_err("cmd->se_lun is NULL\n");
		return -EINVAL;
	}
	if (in_interrupt()) {
		dump_stack();
		pr_err("transport_generic_handle_cdb cannot be called"
				" from interrupt context\n");
		return -EINVAL;
	}
	/*
	 * Set TRANSPORT_NEW_CMD state and CMD_T_ACTIVE following
	 * transport_generic_handle_cdb*() -> transport_add_cmd_to_queue()
	 * in existing usage to ensure that outstanding descriptors are handled
	 * correctly during shutdown via transport_wait_for_tasks()
	 *
	 * Also, we don't take cmd->t_state_lock here as we only expect
	 * this to be called for initial descriptor submission.
	 */
	cmd->t_state = TRANSPORT_NEW_CMD;
	cmd->transport_state |= CMD_T_ACTIVE;

	/*
	 * transport_generic_new_cmd() is already handling QUEUE_FULL,
	 * so follow TRANSPORT_NEW_CMD processing thread context usage
	 * and call transport_generic_request_failure() if necessary..
	 */
	ret = transport_generic_new_cmd(cmd);
	if (ret < 0)
		transport_generic_request_failure(cmd);

	return 0;
}
EXPORT_SYMBOL(transport_handle_cdb_direct);

/**
 * target_submit_cmd - lookup unpacked lun and submit uninitialized se_cmd
 *
 * @se_cmd: command descriptor to submit
 * @se_sess: associated se_sess for endpoint
 * @cdb: pointer to SCSI CDB
 * @sense: pointer to SCSI sense buffer
 * @unpacked_lun: unpacked LUN to reference for struct se_lun
 * @data_length: fabric expected data transfer length
 * @task_addr: SAM task attribute
 * @data_dir: DMA data direction
 * @flags: flags for command submission from target_sc_flags_tables
 *
 * This may only be called from process context, and also currently
 * assumes internal allocation of fabric payload buffer by target-core.
 **/
void target_submit_cmd(struct se_cmd *se_cmd, struct se_session *se_sess,
		unsigned char *cdb, unsigned char *sense, u32 unpacked_lun,
		u32 data_length, int task_attr, int data_dir, int flags)
{
	struct se_portal_group *se_tpg;
	int rc;

	se_tpg = se_sess->se_tpg;
	BUG_ON(!se_tpg);
	BUG_ON(se_cmd->se_tfo || se_cmd->se_sess);
	BUG_ON(in_interrupt());
	/*
	 * Initialize se_cmd for target operation.  From this point
	 * exceptions are handled by sending exception status via
	 * target_core_fabric_ops->queue_status() callback
	 */
	transport_init_se_cmd(se_cmd, se_tpg->se_tpg_tfo, se_sess,
				data_length, data_dir, task_attr, sense);
	if (flags & TARGET_SCF_UNKNOWN_SIZE)
		se_cmd->unknown_data_length = 1;
	/*
	 * Obtain struct se_cmd->cmd_kref reference and add new cmd to
	 * se_sess->sess_cmd_list.  A second kref_get here is necessary
	 * for fabrics using TARGET_SCF_ACK_KREF that expect a second
	 * kref_put() to happen during fabric packet acknowledgement.
	 */
	target_get_sess_cmd(se_sess, se_cmd, (flags & TARGET_SCF_ACK_KREF));
	/*
	 * Signal bidirectional data payloads to target-core
	 */
	if (flags & TARGET_SCF_BIDI_OP)
		se_cmd->se_cmd_flags |= SCF_BIDI;
	/*
	 * Locate se_lun pointer and attach it to struct se_cmd
	 */
	if (transport_lookup_cmd_lun(se_cmd, unpacked_lun) < 0) {
		transport_send_check_condition_and_sense(se_cmd,
				se_cmd->scsi_sense_reason, 0);
		target_put_sess_cmd(se_sess, se_cmd);
		return;
	}
	/*
	 * Sanitize CDBs via transport_generic_cmd_sequencer() and
	 * allocate the necessary tasks to complete the received CDB+data
	 */
	rc = target_setup_cmd_from_cdb(se_cmd, cdb);
	if (rc != 0) {
		transport_generic_request_failure(se_cmd);
		return;
	}

	/*
	 * Check if we need to delay processing because of ALUA
	 * Active/NonOptimized primary access state..
	 */
	core_alua_check_nonop_delay(se_cmd);

	/*
	 * Dispatch se_cmd descriptor to se_lun->lun_se_dev backend
	 * for immediate execution of READs, otherwise wait for
	 * transport_generic_handle_data() to be called for WRITEs
	 * when fabric has filled the incoming buffer.
	 */
	transport_handle_cdb_direct(se_cmd);
	return;
}
EXPORT_SYMBOL(target_submit_cmd);

static void target_complete_tmr_failure(struct work_struct *work)
{
	struct se_cmd *se_cmd = container_of(work, struct se_cmd, work);

	se_cmd->se_tmr_req->response = TMR_LUN_DOES_NOT_EXIST;
	se_cmd->se_tfo->queue_tm_rsp(se_cmd);
	transport_generic_free_cmd(se_cmd, 0);
}

/**
 * target_submit_tmr - lookup unpacked lun and submit uninitialized se_cmd
 *                     for TMR CDBs
 *
 * @se_cmd: command descriptor to submit
 * @se_sess: associated se_sess for endpoint
 * @sense: pointer to SCSI sense buffer
 * @unpacked_lun: unpacked LUN to reference for struct se_lun
 * @fabric_context: fabric context for TMR req
 * @tm_type: Type of TM request
 * @gfp: gfp type for caller
 * @tag: referenced task tag for TMR_ABORT_TASK
 * @flags: submit cmd flags
 *
 * Callable from all contexts.
 **/

int target_submit_tmr(struct se_cmd *se_cmd, struct se_session *se_sess,
		unsigned char *sense, u32 unpacked_lun,
		void *fabric_tmr_ptr, unsigned char tm_type,
		gfp_t gfp, unsigned int tag, int flags)
{
	struct se_portal_group *se_tpg;
	int ret;

	se_tpg = se_sess->se_tpg;
	BUG_ON(!se_tpg);

	transport_init_se_cmd(se_cmd, se_tpg->se_tpg_tfo, se_sess,
			      0, DMA_NONE, MSG_SIMPLE_TAG, sense);
	/*
	 * FIXME: Currently expect caller to handle se_cmd->se_tmr_req
	 * allocation failure.
	 */
	ret = core_tmr_alloc_req(se_cmd, fabric_tmr_ptr, tm_type, gfp);
	if (ret < 0)
		return -ENOMEM;

	if (tm_type == TMR_ABORT_TASK)
		se_cmd->se_tmr_req->ref_task_tag = tag;

	/* See target_submit_cmd for commentary */
	target_get_sess_cmd(se_sess, se_cmd, (flags & TARGET_SCF_ACK_KREF));

	ret = transport_lookup_tmr_lun(se_cmd, unpacked_lun);
	if (ret) {
		/*
		 * For callback during failure handling, push this work off
		 * to process context with TMR_LUN_DOES_NOT_EXIST status.
		 */
		INIT_WORK(&se_cmd->work, target_complete_tmr_failure);
		schedule_work(&se_cmd->work);
		return 0;
	}
	transport_generic_handle_tmr(se_cmd);
	return 0;
}
EXPORT_SYMBOL(target_submit_tmr);

/*
 * Used by fabric module frontends defining a TFO->new_cmd_map() caller
 * to  queue up a newly setup se_cmd w/ TRANSPORT_NEW_CMD_MAP in order to
 * complete setup in TCM process context w/ TFO->new_cmd_map().
 */
int transport_generic_handle_cdb_map(
	struct se_cmd *cmd)
{
	if (!cmd->se_lun) {
		dump_stack();
		pr_err("cmd->se_lun is NULL\n");
		return -EINVAL;
	}

	transport_add_cmd_to_queue(cmd, TRANSPORT_NEW_CMD_MAP, false);
	return 0;
}
EXPORT_SYMBOL(transport_generic_handle_cdb_map);

/*	transport_generic_handle_data():
 *
 *
 */
int transport_generic_handle_data(
	struct se_cmd *cmd)
{
	/*
	 * For the software fabric case, then we assume the nexus is being
	 * failed/shutdown when signals are pending from the kthread context
	 * caller, so we return a failure.  For the HW target mode case running
	 * in interrupt code, the signal_pending() check is skipped.
	 */
	if (!in_interrupt() && signal_pending(current))
		return -EPERM;
	/*
	 * If the received CDB has aleady been ABORTED by the generic
	 * target engine, we now call transport_check_aborted_status()
	 * to queue any delated TASK_ABORTED status for the received CDB to the
	 * fabric module as we are expecting no further incoming DATA OUT
	 * sequences at this point.
	 */
	if (transport_check_aborted_status(cmd, 1) != 0)
		return 0;

	transport_add_cmd_to_queue(cmd, TRANSPORT_PROCESS_WRITE, false);
	return 0;
}
EXPORT_SYMBOL(transport_generic_handle_data);

/*	transport_generic_handle_tmr():
 *
 *
 */
int transport_generic_handle_tmr(
	struct se_cmd *cmd)
{
	transport_add_cmd_to_queue(cmd, TRANSPORT_PROCESS_TMR, false);
	return 0;
}
EXPORT_SYMBOL(transport_generic_handle_tmr);

/*
 * If the cmd is active, request it to be stopped and sleep until it
 * has completed.
 */
bool target_stop_cmd(struct se_cmd *cmd, unsigned long *flags)
{
	bool was_active = false;

	if (cmd->transport_state & CMD_T_BUSY) {
		cmd->transport_state |= CMD_T_REQUEST_STOP;
		spin_unlock_irqrestore(&cmd->t_state_lock, *flags);

		pr_debug("cmd %p waiting to complete\n", cmd);
		wait_for_completion(&cmd->task_stop_comp);
		pr_debug("cmd %p stopped successfully\n", cmd);

		spin_lock_irqsave(&cmd->t_state_lock, *flags);
		cmd->transport_state &= ~CMD_T_REQUEST_STOP;
		cmd->transport_state &= ~CMD_T_BUSY;
		was_active = true;
	}

	return was_active;
}

/*
 * Handle SAM-esque emulation for generic transport request failures.
 */
void transport_generic_request_failure(struct se_cmd *cmd)
{
	int ret = 0;

	pr_debug("-----[ Storage Engine Exception for cmd: %p ITT: 0x%08x"
		" CDB: 0x%02x\n", cmd, cmd->se_tfo->get_task_tag(cmd),
		cmd->t_task_cdb[0]);
	pr_debug("-----[ i_state: %d t_state: %d scsi_sense_reason: %d\n",
		cmd->se_tfo->get_cmd_state(cmd),
		cmd->t_state, cmd->scsi_sense_reason);
	pr_debug("-----[ CMD_T_ACTIVE: %d CMD_T_STOP: %d CMD_T_SENT: %d\n",
		(cmd->transport_state & CMD_T_ACTIVE) != 0,
		(cmd->transport_state & CMD_T_STOP) != 0,
		(cmd->transport_state & CMD_T_SENT) != 0);

	/*
	 * For SAM Task Attribute emulation for failed struct se_cmd
	 */
	if (cmd->se_dev->dev_task_attr_type == SAM_TASK_ATTR_EMULATED)
		transport_complete_task_attr(cmd);

	switch (cmd->scsi_sense_reason) {
	case TCM_NON_EXISTENT_LUN:
	case TCM_UNSUPPORTED_SCSI_OPCODE:
	case TCM_INVALID_CDB_FIELD:
	case TCM_INVALID_PARAMETER_LIST:
	case TCM_LOGICAL_UNIT_COMMUNICATION_FAILURE:
	case TCM_UNKNOWN_MODE_PAGE:
	case TCM_WRITE_PROTECTED:
	case TCM_CHECK_CONDITION_ABORT_CMD:
	case TCM_CHECK_CONDITION_UNIT_ATTENTION:
	case TCM_CHECK_CONDITION_NOT_READY:
		break;
	case TCM_RESERVATION_CONFLICT:
		/*
		 * No SENSE Data payload for this case, set SCSI Status
		 * and queue the response to $FABRIC_MOD.
		 *
		 * Uses linux/include/scsi/scsi.h SAM status codes defs
		 */
		cmd->scsi_status = SAM_STAT_RESERVATION_CONFLICT;
		/*
		 * For UA Interlock Code 11b, a RESERVATION CONFLICT will
		 * establish a UNIT ATTENTION with PREVIOUS RESERVATION
		 * CONFLICT STATUS.
		 *
		 * See spc4r17, section 7.4.6 Control Mode Page, Table 349
		 */
		if (cmd->se_sess &&
		    cmd->se_dev->se_sub_dev->se_dev_attrib.emulate_ua_intlck_ctrl == 2)
			core_scsi3_ua_allocate(cmd->se_sess->se_node_acl,
				cmd->orig_fe_lun, 0x2C,
				ASCQ_2CH_PREVIOUS_RESERVATION_CONFLICT_STATUS);

		ret = cmd->se_tfo->queue_status(cmd);
		if (ret == -EAGAIN || ret == -ENOMEM)
			goto queue_full;
		goto check_stop;
	default:
		pr_err("Unknown transport error for CDB 0x%02x: %d\n",
			cmd->t_task_cdb[0], cmd->scsi_sense_reason);
		cmd->scsi_sense_reason = TCM_UNSUPPORTED_SCSI_OPCODE;
		break;
	}
	/*
	 * If a fabric does not define a cmd->se_tfo->new_cmd_map caller,
	 * make the call to transport_send_check_condition_and_sense()
	 * directly.  Otherwise expect the fabric to make the call to
	 * transport_send_check_condition_and_sense() after handling
	 * possible unsoliticied write data payloads.
	 */
	ret = transport_send_check_condition_and_sense(cmd,
			cmd->scsi_sense_reason, 0);
	if (ret == -EAGAIN || ret == -ENOMEM)
		goto queue_full;

check_stop:
	transport_lun_remove_cmd(cmd);
	if (!transport_cmd_check_stop_to_fabric(cmd))
		;
	return;

queue_full:
	cmd->t_state = TRANSPORT_COMPLETE_QF_OK;
	transport_handle_queue_full(cmd, cmd->se_dev);
}
EXPORT_SYMBOL(transport_generic_request_failure);

static inline u32 transport_lba_21(unsigned char *cdb)
{
	return ((cdb[1] & 0x1f) << 16) | (cdb[2] << 8) | cdb[3];
}

static inline u32 transport_lba_32(unsigned char *cdb)
{
	return (cdb[2] << 24) | (cdb[3] << 16) | (cdb[4] << 8) | cdb[5];
}

static inline unsigned long long transport_lba_64(unsigned char *cdb)
{
	unsigned int __v1, __v2;

	__v1 = (cdb[2] << 24) | (cdb[3] << 16) | (cdb[4] << 8) | cdb[5];
	__v2 = (cdb[6] << 24) | (cdb[7] << 16) | (cdb[8] << 8) | cdb[9];

	return ((unsigned long long)__v2) | (unsigned long long)__v1 << 32;
}

/*
 * For VARIABLE_LENGTH_CDB w/ 32 byte extended CDBs
 */
static inline unsigned long long transport_lba_64_ext(unsigned char *cdb)
{
	unsigned int __v1, __v2;

	__v1 = (cdb[12] << 24) | (cdb[13] << 16) | (cdb[14] << 8) | cdb[15];
	__v2 = (cdb[16] << 24) | (cdb[17] << 16) | (cdb[18] << 8) | cdb[19];

	return ((unsigned long long)__v2) | (unsigned long long)__v1 << 32;
}

static void transport_set_supported_SAM_opcode(struct se_cmd *se_cmd)
{
	unsigned long flags;

	spin_lock_irqsave(&se_cmd->t_state_lock, flags);
	se_cmd->se_cmd_flags |= SCF_SUPPORTED_SAM_OPCODE;
	spin_unlock_irqrestore(&se_cmd->t_state_lock, flags);
}

/*
 * Called from Fabric Module context from transport_execute_tasks()
 *
 * The return of this function determins if the tasks from struct se_cmd
 * get added to the execution queue in transport_execute_tasks(),
 * or are added to the delayed or ordered lists here.
 */
static inline int transport_execute_task_attr(struct se_cmd *cmd)
{
	if (cmd->se_dev->dev_task_attr_type != SAM_TASK_ATTR_EMULATED)
		return 1;
	/*
	 * Check for the existence of HEAD_OF_QUEUE, and if true return 1
	 * to allow the passed struct se_cmd list of tasks to the front of the list.
	 */
	 if (cmd->sam_task_attr == MSG_HEAD_TAG) {
		pr_debug("Added HEAD_OF_QUEUE for CDB:"
			" 0x%02x, se_ordered_id: %u\n",
			cmd->t_task_cdb[0],
			cmd->se_ordered_id);
		return 1;
	} else if (cmd->sam_task_attr == MSG_ORDERED_TAG) {
		atomic_inc(&cmd->se_dev->dev_ordered_sync);
		smp_mb__after_atomic_inc();

		pr_debug("Added ORDERED for CDB: 0x%02x to ordered"
				" list, se_ordered_id: %u\n",
				cmd->t_task_cdb[0],
				cmd->se_ordered_id);
		/*
		 * Add ORDERED command to tail of execution queue if
		 * no other older commands exist that need to be
		 * completed first.
		 */
		if (!atomic_read(&cmd->se_dev->simple_cmds))
			return 1;
	} else {
		/*
		 * For SIMPLE and UNTAGGED Task Attribute commands
		 */
		atomic_inc(&cmd->se_dev->simple_cmds);
		smp_mb__after_atomic_inc();
	}
	/*
	 * Otherwise if one or more outstanding ORDERED task attribute exist,
	 * add the dormant task(s) built for the passed struct se_cmd to the
	 * execution queue and become in Active state for this struct se_device.
	 */
	if (atomic_read(&cmd->se_dev->dev_ordered_sync) != 0) {
		/*
		 * Otherwise, add cmd w/ tasks to delayed cmd queue that
		 * will be drained upon completion of HEAD_OF_QUEUE task.
		 */
		spin_lock(&cmd->se_dev->delayed_cmd_lock);
		cmd->se_cmd_flags |= SCF_DELAYED_CMD_FROM_SAM_ATTR;
		list_add_tail(&cmd->se_delayed_node,
				&cmd->se_dev->delayed_cmd_list);
		spin_unlock(&cmd->se_dev->delayed_cmd_lock);

		pr_debug("Added CDB: 0x%02x Task Attr: 0x%02x to"
			" delayed CMD list, se_ordered_id: %u\n",
			cmd->t_task_cdb[0], cmd->sam_task_attr,
			cmd->se_ordered_id);
		/*
		 * Return zero to let transport_execute_tasks() know
		 * not to add the delayed tasks to the execution list.
		 */
		return 0;
	}
	/*
	 * Otherwise, no ORDERED task attributes exist..
	 */
	return 1;
}

/*
 * Called from fabric module context in transport_generic_new_cmd() and
 * transport_generic_process_write()
 */
static void transport_execute_tasks(struct se_cmd *cmd)
{
	int add_tasks;
	struct se_device *se_dev = cmd->se_dev;
	/*
	 * Call transport_cmd_check_stop() to see if a fabric exception
	 * has occurred that prevents execution.
	 */
	if (!transport_cmd_check_stop(cmd, 0, TRANSPORT_PROCESSING)) {
		/*
		 * Check for SAM Task Attribute emulation and HEAD_OF_QUEUE
		 * attribute for the tasks of the received struct se_cmd CDB
		 */
		add_tasks = transport_execute_task_attr(cmd);
		if (add_tasks) {
			__transport_execute_tasks(se_dev, cmd);
			return;
		}
	}
	__transport_execute_tasks(se_dev, NULL);
}

static int __transport_execute_tasks(struct se_device *dev, struct se_cmd *new_cmd)
{
	int error;
	struct se_cmd *cmd = NULL;
	unsigned long flags;

check_depth:
	spin_lock_irq(&dev->execute_task_lock);
	if (new_cmd != NULL)
		__target_add_to_execute_list(new_cmd);

	if (list_empty(&dev->execute_list)) {
		spin_unlock_irq(&dev->execute_task_lock);
		return 0;
	}
	cmd = list_first_entry(&dev->execute_list, struct se_cmd, execute_list);
	__target_remove_from_execute_list(cmd);
	spin_unlock_irq(&dev->execute_task_lock);

	spin_lock_irqsave(&cmd->t_state_lock, flags);
	cmd->transport_state |= CMD_T_BUSY;
	cmd->transport_state |= CMD_T_SENT;

	spin_unlock_irqrestore(&cmd->t_state_lock, flags);

	if (cmd->execute_cmd)
		error = cmd->execute_cmd(cmd);
	else {
		error = dev->transport->execute_cmd(cmd, cmd->t_data_sg,
				cmd->t_data_nents, cmd->data_direction);
	}

	if (error != 0) {
		spin_lock_irqsave(&cmd->t_state_lock, flags);
		cmd->transport_state &= ~CMD_T_BUSY;
		cmd->transport_state &= ~CMD_T_SENT;
		spin_unlock_irqrestore(&cmd->t_state_lock, flags);

		transport_generic_request_failure(cmd);
	}

	new_cmd = NULL;
	goto check_depth;

	return 0;
}

static inline u32 transport_get_sectors_6(
	unsigned char *cdb,
	struct se_cmd *cmd,
	int *ret)
{
	struct se_device *dev = cmd->se_dev;

	/*
	 * Assume TYPE_DISK for non struct se_device objects.
	 * Use 8-bit sector value.
	 */
	if (!dev)
		goto type_disk;

	/*
	 * Use 24-bit allocation length for TYPE_TAPE.
	 */
	if (dev->transport->get_device_type(dev) == TYPE_TAPE)
		return (u32)(cdb[2] << 16) + (cdb[3] << 8) + cdb[4];

	/*
	 * Everything else assume TYPE_DISK Sector CDB location.
	 * Use 8-bit sector value.  SBC-3 says:
	 *
	 *   A TRANSFER LENGTH field set to zero specifies that 256
	 *   logical blocks shall be written.  Any other value
	 *   specifies the number of logical blocks that shall be
	 *   written.
	 */
type_disk:
	return cdb[4] ? : 256;
}

static inline u32 transport_get_sectors_10(
	unsigned char *cdb,
	struct se_cmd *cmd,
	int *ret)
{
	struct se_device *dev = cmd->se_dev;

	/*
	 * Assume TYPE_DISK for non struct se_device objects.
	 * Use 16-bit sector value.
	 */
	if (!dev)
		goto type_disk;

	/*
	 * XXX_10 is not defined in SSC, throw an exception
	 */
	if (dev->transport->get_device_type(dev) == TYPE_TAPE) {
		*ret = -EINVAL;
		return 0;
	}

	/*
	 * Everything else assume TYPE_DISK Sector CDB location.
	 * Use 16-bit sector value.
	 */
type_disk:
	return (u32)(cdb[7] << 8) + cdb[8];
}

static inline u32 transport_get_sectors_12(
	unsigned char *cdb,
	struct se_cmd *cmd,
	int *ret)
{
	struct se_device *dev = cmd->se_dev;

	/*
	 * Assume TYPE_DISK for non struct se_device objects.
	 * Use 32-bit sector value.
	 */
	if (!dev)
		goto type_disk;

	/*
	 * XXX_12 is not defined in SSC, throw an exception
	 */
	if (dev->transport->get_device_type(dev) == TYPE_TAPE) {
		*ret = -EINVAL;
		return 0;
	}

	/*
	 * Everything else assume TYPE_DISK Sector CDB location.
	 * Use 32-bit sector value.
	 */
type_disk:
	return (u32)(cdb[6] << 24) + (cdb[7] << 16) + (cdb[8] << 8) + cdb[9];
}

static inline u32 transport_get_sectors_16(
	unsigned char *cdb,
	struct se_cmd *cmd,
	int *ret)
{
	struct se_device *dev = cmd->se_dev;

	/*
	 * Assume TYPE_DISK for non struct se_device objects.
	 * Use 32-bit sector value.
	 */
	if (!dev)
		goto type_disk;

	/*
	 * Use 24-bit allocation length for TYPE_TAPE.
	 */
	if (dev->transport->get_device_type(dev) == TYPE_TAPE)
		return (u32)(cdb[12] << 16) + (cdb[13] << 8) + cdb[14];

type_disk:
	return (u32)(cdb[10] << 24) + (cdb[11] << 16) +
		    (cdb[12] << 8) + cdb[13];
}

/*
 * Used for VARIABLE_LENGTH_CDB WRITE_32 and READ_32 variants
 */
static inline u32 transport_get_sectors_32(
	unsigned char *cdb,
	struct se_cmd *cmd,
	int *ret)
{
	/*
	 * Assume TYPE_DISK for non struct se_device objects.
	 * Use 32-bit sector value.
	 */
	return (u32)(cdb[28] << 24) + (cdb[29] << 16) +
		    (cdb[30] << 8) + cdb[31];

}

static inline u32 transport_get_size(
	u32 sectors,
	unsigned char *cdb,
	struct se_cmd *cmd)
{
	struct se_device *dev = cmd->se_dev;

	if (dev->transport->get_device_type(dev) == TYPE_TAPE) {
		if (cdb[1] & 1) { /* sectors */
			return dev->se_sub_dev->se_dev_attrib.block_size * sectors;
		} else /* bytes */
			return sectors;
	}

	pr_debug("Returning block_size: %u, sectors: %u == %u for"
		" %s object\n", dev->se_sub_dev->se_dev_attrib.block_size,
		sectors, dev->se_sub_dev->se_dev_attrib.block_size * sectors,
		dev->transport->name);

	return dev->se_sub_dev->se_dev_attrib.block_size * sectors;
}

static void transport_xor_callback(struct se_cmd *cmd)
{
	unsigned char *buf, *addr;
	struct scatterlist *sg;
	unsigned int offset;
	int i;
	int count;
	/*
	 * From sbc3r22.pdf section 5.48 XDWRITEREAD (10) command
	 *
	 * 1) read the specified logical block(s);
	 * 2) transfer logical blocks from the data-out buffer;
	 * 3) XOR the logical blocks transferred from the data-out buffer with
	 *    the logical blocks read, storing the resulting XOR data in a buffer;
	 * 4) if the DISABLE WRITE bit is set to zero, then write the logical
	 *    blocks transferred from the data-out buffer; and
	 * 5) transfer the resulting XOR data to the data-in buffer.
	 */
	buf = kmalloc(cmd->data_length, GFP_KERNEL);
	if (!buf) {
		pr_err("Unable to allocate xor_callback buf\n");
		return;
	}
	/*
	 * Copy the scatterlist WRITE buffer located at cmd->t_data_sg
	 * into the locally allocated *buf
	 */
	sg_copy_to_buffer(cmd->t_data_sg,
			  cmd->t_data_nents,
			  buf,
			  cmd->data_length);

	/*
	 * Now perform the XOR against the BIDI read memory located at
	 * cmd->t_mem_bidi_list
	 */

	offset = 0;
	for_each_sg(cmd->t_bidi_data_sg, sg, cmd->t_bidi_data_nents, count) {
		addr = kmap_atomic(sg_page(sg));
		if (!addr)
			goto out;

		for (i = 0; i < sg->length; i++)
			*(addr + sg->offset + i) ^= *(buf + offset + i);

		offset += sg->length;
		kunmap_atomic(addr);
	}

out:
	kfree(buf);
}

/*
 * Used to obtain Sense Data from underlying Linux/SCSI struct scsi_cmnd
 */
static int transport_get_sense_data(struct se_cmd *cmd)
{
	unsigned char *buffer = cmd->sense_buffer, *sense_buffer = NULL;
	struct se_device *dev = cmd->se_dev;
	unsigned long flags;
	u32 offset = 0;

	WARN_ON(!cmd->se_lun);

	if (!dev)
		return 0;

	spin_lock_irqsave(&cmd->t_state_lock, flags);
	if (cmd->se_cmd_flags & SCF_SENT_CHECK_CONDITION) {
		spin_unlock_irqrestore(&cmd->t_state_lock, flags);
		return 0;
	}

	if (!(cmd->se_cmd_flags & SCF_TRANSPORT_TASK_SENSE))
		goto out;

	if (!dev->transport->get_sense_buffer) {
		pr_err("dev->transport->get_sense_buffer is NULL\n");
		goto out;
	}

	sense_buffer = dev->transport->get_sense_buffer(cmd);
	if (!sense_buffer) {
		pr_err("ITT 0x%08x cmd %p: Unable to locate"
			" sense buffer for task with sense\n",
			cmd->se_tfo->get_task_tag(cmd), cmd);
		goto out;
	}

	spin_unlock_irqrestore(&cmd->t_state_lock, flags);

	offset = cmd->se_tfo->set_fabric_sense_len(cmd, TRANSPORT_SENSE_BUFFER);

	memcpy(&buffer[offset], sense_buffer, TRANSPORT_SENSE_BUFFER);

	/* Automatically padded */
	cmd->scsi_sense_length = TRANSPORT_SENSE_BUFFER + offset;

	pr_debug("HBA_[%u]_PLUG[%s]: Set SAM STATUS: 0x%02x and sense\n",
		dev->se_hba->hba_id, dev->transport->name, cmd->scsi_status);
	return 0;

out:
	spin_unlock_irqrestore(&cmd->t_state_lock, flags);
	return -1;
}

static inline long long transport_dev_end_lba(struct se_device *dev)
{
	return dev->transport->get_blocks(dev) + 1;
}

static int transport_cmd_get_valid_sectors(struct se_cmd *cmd)
{
	struct se_device *dev = cmd->se_dev;
	u32 sectors;

	if (dev->transport->get_device_type(dev) != TYPE_DISK)
		return 0;

	sectors = (cmd->data_length / dev->se_sub_dev->se_dev_attrib.block_size);

	if ((cmd->t_task_lba + sectors) > transport_dev_end_lba(dev)) {
		pr_err("LBA: %llu Sectors: %u exceeds"
			" transport_dev_end_lba(): %llu\n",
			cmd->t_task_lba, sectors,
			transport_dev_end_lba(dev));
		return -EINVAL;
	}

	return 0;
}

static int target_check_write_same_discard(unsigned char *flags, struct se_device *dev)
{
	/*
	 * Determine if the received WRITE_SAME is used to for direct
	 * passthrough into Linux/SCSI with struct request via TCM/pSCSI
	 * or we are signaling the use of internal WRITE_SAME + UNMAP=1
	 * emulation for -> Linux/BLOCK disbard with TCM/IBLOCK code.
	 */
	int passthrough = (dev->transport->transport_type ==
				TRANSPORT_PLUGIN_PHBA_PDEV);

	if (!passthrough) {
		if ((flags[0] & 0x04) || (flags[0] & 0x02)) {
			pr_err("WRITE_SAME PBDATA and LBDATA"
				" bits not supported for Block Discard"
				" Emulation\n");
			return -ENOSYS;
		}
		/*
		 * Currently for the emulated case we only accept
		 * tpws with the UNMAP=1 bit set.
		 */
		if (!(flags[0] & 0x08)) {
			pr_err("WRITE_SAME w/o UNMAP bit not"
				" supported for Block Discard Emulation\n");
			return -ENOSYS;
		}
	}

	return 0;
}

/*	transport_generic_cmd_sequencer():
 *
 *	Generic Command Sequencer that should work for most DAS transport
 *	drivers.
 *
 *	Called from target_setup_cmd_from_cdb() in the $FABRIC_MOD
 *	RX Thread.
 *
 *	FIXME: Need to support other SCSI OPCODES where as well.
 */
static int transport_generic_cmd_sequencer(
	struct se_cmd *cmd,
	unsigned char *cdb)
{
	struct se_device *dev = cmd->se_dev;
	struct se_subsystem_dev *su_dev = dev->se_sub_dev;
	int ret = 0, sector_ret = 0, passthrough;
	u32 sectors = 0, size = 0, pr_reg_type = 0;
	u16 service_action;
	u8 alua_ascq = 0;
	/*
	 * Check for an existing UNIT ATTENTION condition
	 */
	if (core_scsi3_ua_check(cmd, cdb) < 0) {
		cmd->se_cmd_flags |= SCF_SCSI_CDB_EXCEPTION;
		cmd->scsi_sense_reason = TCM_CHECK_CONDITION_UNIT_ATTENTION;
		return -EINVAL;
	}
	/*
	 * Check status of Asymmetric Logical Unit Assignment port
	 */
	ret = su_dev->t10_alua.alua_state_check(cmd, cdb, &alua_ascq);
	if (ret != 0) {
		/*
		 * Set SCSI additional sense code (ASC) to 'LUN Not Accessible';
		 * The ALUA additional sense code qualifier (ASCQ) is determined
		 * by the ALUA primary or secondary access state..
		 */
		if (ret > 0) {
			pr_debug("[%s]: ALUA TG Port not available,"
				" SenseKey: NOT_READY, ASC/ASCQ: 0x04/0x%02x\n",
				cmd->se_tfo->get_fabric_name(), alua_ascq);

			transport_set_sense_codes(cmd, 0x04, alua_ascq);
			cmd->se_cmd_flags |= SCF_SCSI_CDB_EXCEPTION;
			cmd->scsi_sense_reason = TCM_CHECK_CONDITION_NOT_READY;
			return -EINVAL;
		}
		goto out_invalid_cdb_field;
	}
	/*
	 * Check status for SPC-3 Persistent Reservations
	 */
	if (su_dev->t10_pr.pr_ops.t10_reservation_check(cmd, &pr_reg_type) != 0) {
		if (su_dev->t10_pr.pr_ops.t10_seq_non_holder(
					cmd, cdb, pr_reg_type) != 0) {
			cmd->se_cmd_flags |= SCF_SCSI_CDB_EXCEPTION;
			cmd->se_cmd_flags |= SCF_SCSI_RESERVATION_CONFLICT;
			cmd->scsi_status = SAM_STAT_RESERVATION_CONFLICT;
			cmd->scsi_sense_reason = TCM_RESERVATION_CONFLICT;
			return -EBUSY;
		}
		/*
		 * This means the CDB is allowed for the SCSI Initiator port
		 * when said port is *NOT* holding the legacy SPC-2 or
		 * SPC-3 Persistent Reservation.
		 */
	}

	/*
	 * If we operate in passthrough mode we skip most CDB emulation and
	 * instead hand the commands down to the physical SCSI device.
	 */
	passthrough =
		(dev->transport->transport_type == TRANSPORT_PLUGIN_PHBA_PDEV);

	switch (cdb[0]) {
	case READ_6:
		sectors = transport_get_sectors_6(cdb, cmd, &sector_ret);
		if (sector_ret)
			goto out_unsupported_cdb;
		size = transport_get_size(sectors, cdb, cmd);
		cmd->t_task_lba = transport_lba_21(cdb);
		cmd->se_cmd_flags |= SCF_SCSI_DATA_SG_IO_CDB;
		break;
	case READ_10:
		sectors = transport_get_sectors_10(cdb, cmd, &sector_ret);
		if (sector_ret)
			goto out_unsupported_cdb;
		size = transport_get_size(sectors, cdb, cmd);
		cmd->t_task_lba = transport_lba_32(cdb);
		cmd->se_cmd_flags |= SCF_SCSI_DATA_SG_IO_CDB;
		break;
	case READ_12:
		sectors = transport_get_sectors_12(cdb, cmd, &sector_ret);
		if (sector_ret)
			goto out_unsupported_cdb;
		size = transport_get_size(sectors, cdb, cmd);
		cmd->t_task_lba = transport_lba_32(cdb);
		cmd->se_cmd_flags |= SCF_SCSI_DATA_SG_IO_CDB;
		break;
	case READ_16:
		sectors = transport_get_sectors_16(cdb, cmd, &sector_ret);
		if (sector_ret)
			goto out_unsupported_cdb;
		size = transport_get_size(sectors, cdb, cmd);
		cmd->t_task_lba = transport_lba_64(cdb);
		cmd->se_cmd_flags |= SCF_SCSI_DATA_SG_IO_CDB;
		break;
	case WRITE_6:
		sectors = transport_get_sectors_6(cdb, cmd, &sector_ret);
		if (sector_ret)
			goto out_unsupported_cdb;
		size = transport_get_size(sectors, cdb, cmd);
		cmd->t_task_lba = transport_lba_21(cdb);
		cmd->se_cmd_flags |= SCF_SCSI_DATA_SG_IO_CDB;
		break;
	case WRITE_10:
	case WRITE_VERIFY:
		sectors = transport_get_sectors_10(cdb, cmd, &sector_ret);
		if (sector_ret)
			goto out_unsupported_cdb;
		size = transport_get_size(sectors, cdb, cmd);
		cmd->t_task_lba = transport_lba_32(cdb);
		if (cdb[1] & 0x8)
			cmd->se_cmd_flags |= SCF_FUA;
		cmd->se_cmd_flags |= SCF_SCSI_DATA_SG_IO_CDB;
		break;
	case WRITE_12:
		sectors = transport_get_sectors_12(cdb, cmd, &sector_ret);
		if (sector_ret)
			goto out_unsupported_cdb;
		size = transport_get_size(sectors, cdb, cmd);
		cmd->t_task_lba = transport_lba_32(cdb);
		if (cdb[1] & 0x8)
			cmd->se_cmd_flags |= SCF_FUA;
		cmd->se_cmd_flags |= SCF_SCSI_DATA_SG_IO_CDB;
		break;
	case WRITE_16:
		sectors = transport_get_sectors_16(cdb, cmd, &sector_ret);
		if (sector_ret)
			goto out_unsupported_cdb;
		size = transport_get_size(sectors, cdb, cmd);
		cmd->t_task_lba = transport_lba_64(cdb);
		if (cdb[1] & 0x8)
			cmd->se_cmd_flags |= SCF_FUA;
		cmd->se_cmd_flags |= SCF_SCSI_DATA_SG_IO_CDB;
		break;
	case XDWRITEREAD_10:
		if ((cmd->data_direction != DMA_TO_DEVICE) ||
		    !(cmd->se_cmd_flags & SCF_BIDI))
			goto out_invalid_cdb_field;
		sectors = transport_get_sectors_10(cdb, cmd, &sector_ret);
		if (sector_ret)
			goto out_unsupported_cdb;
		size = transport_get_size(sectors, cdb, cmd);
		cmd->t_task_lba = transport_lba_32(cdb);
		cmd->se_cmd_flags |= SCF_SCSI_DATA_SG_IO_CDB;

		/*
		 * Do now allow BIDI commands for passthrough mode.
		 */
		if (passthrough)
			goto out_unsupported_cdb;

		/*
		 * Setup BIDI XOR callback to be run after I/O completion.
		 */
		cmd->transport_complete_callback = &transport_xor_callback;
		if (cdb[1] & 0x8)
			cmd->se_cmd_flags |= SCF_FUA;
		break;
	case VARIABLE_LENGTH_CMD:
		service_action = get_unaligned_be16(&cdb[8]);
		switch (service_action) {
		case XDWRITEREAD_32:
			sectors = transport_get_sectors_32(cdb, cmd, &sector_ret);
			if (sector_ret)
				goto out_unsupported_cdb;
			size = transport_get_size(sectors, cdb, cmd);
			/*
			 * Use WRITE_32 and READ_32 opcodes for the emulated
			 * XDWRITE_READ_32 logic.
			 */
			cmd->t_task_lba = transport_lba_64_ext(cdb);
			cmd->se_cmd_flags |= SCF_SCSI_DATA_SG_IO_CDB;

			/*
			 * Do now allow BIDI commands for passthrough mode.
			 */
			if (passthrough)
				goto out_unsupported_cdb;

			/*
			 * Setup BIDI XOR callback to be run during after I/O
			 * completion.
			 */
			cmd->transport_complete_callback = &transport_xor_callback;
			if (cdb[1] & 0x8)
				cmd->se_cmd_flags |= SCF_FUA;
			break;
		case WRITE_SAME_32:
			sectors = transport_get_sectors_32(cdb, cmd, &sector_ret);
			if (sector_ret)
				goto out_unsupported_cdb;

			if (sectors)
				size = transport_get_size(1, cdb, cmd);
			else {
				pr_err("WSNZ=1, WRITE_SAME w/sectors=0 not"
				       " supported\n");
				goto out_invalid_cdb_field;
			}

			cmd->t_task_lba = get_unaligned_be64(&cdb[12]);
			cmd->se_cmd_flags |= SCF_SCSI_CONTROL_SG_IO_CDB;

			if (target_check_write_same_discard(&cdb[10], dev) < 0)
				goto out_unsupported_cdb;
			if (!passthrough)
				cmd->execute_cmd = target_emulate_write_same;
			break;
		default:
			pr_err("VARIABLE_LENGTH_CMD service action"
				" 0x%04x not supported\n", service_action);
			goto out_unsupported_cdb;
		}
		break;
	case MAINTENANCE_IN:
		if (dev->transport->get_device_type(dev) != TYPE_ROM) {
			/* MAINTENANCE_IN from SCC-2 */
			/*
			 * Check for emulated MI_REPORT_TARGET_PGS.
			 */
			if (cdb[1] == MI_REPORT_TARGET_PGS &&
			    su_dev->t10_alua.alua_type == SPC3_ALUA_EMULATED) {
				cmd->execute_cmd =
					target_emulate_report_target_port_groups;
			}
			size = (cdb[6] << 24) | (cdb[7] << 16) |
			       (cdb[8] << 8) | cdb[9];
		} else {
			/* GPCMD_SEND_KEY from multi media commands */
			size = (cdb[8] << 8) + cdb[9];
		}
		cmd->se_cmd_flags |= SCF_SCSI_CONTROL_SG_IO_CDB;
		break;
	case MODE_SELECT:
		size = cdb[4];
		cmd->se_cmd_flags |= SCF_SCSI_CONTROL_SG_IO_CDB;
		break;
	case MODE_SELECT_10:
		size = (cdb[7] << 8) + cdb[8];
		cmd->se_cmd_flags |= SCF_SCSI_CONTROL_SG_IO_CDB;
		break;
	case MODE_SENSE:
		size = cdb[4];
		cmd->se_cmd_flags |= SCF_SCSI_CONTROL_SG_IO_CDB;
		if (!passthrough)
			cmd->execute_cmd = target_emulate_modesense;
		break;
	case MODE_SENSE_10:
		size = (cdb[7] << 8) + cdb[8];
		cmd->se_cmd_flags |= SCF_SCSI_CONTROL_SG_IO_CDB;
		if (!passthrough)
			cmd->execute_cmd = target_emulate_modesense;
		break;
	case GPCMD_READ_BUFFER_CAPACITY:
	case GPCMD_SEND_OPC:
	case LOG_SELECT:
	case LOG_SENSE:
		size = (cdb[7] << 8) + cdb[8];
		cmd->se_cmd_flags |= SCF_SCSI_CONTROL_SG_IO_CDB;
		break;
	case READ_BLOCK_LIMITS:
		size = READ_BLOCK_LEN;
		cmd->se_cmd_flags |= SCF_SCSI_CONTROL_SG_IO_CDB;
		break;
	case GPCMD_GET_CONFIGURATION:
	case GPCMD_READ_FORMAT_CAPACITIES:
	case GPCMD_READ_DISC_INFO:
	case GPCMD_READ_TRACK_RZONE_INFO:
		size = (cdb[7] << 8) + cdb[8];
		cmd->se_cmd_flags |= SCF_SCSI_CONTROL_SG_IO_CDB;
		break;
	case PERSISTENT_RESERVE_IN:
		if (su_dev->t10_pr.res_type == SPC3_PERSISTENT_RESERVATIONS)
			cmd->execute_cmd = target_scsi3_emulate_pr_in;
		size = (cdb[7] << 8) + cdb[8];
		cmd->se_cmd_flags |= SCF_SCSI_CONTROL_SG_IO_CDB;
		break;
	case PERSISTENT_RESERVE_OUT:
		if (su_dev->t10_pr.res_type == SPC3_PERSISTENT_RESERVATIONS)
			cmd->execute_cmd = target_scsi3_emulate_pr_out;
		size = (cdb[7] << 8) + cdb[8];
		cmd->se_cmd_flags |= SCF_SCSI_CONTROL_SG_IO_CDB;
		break;
	case GPCMD_MECHANISM_STATUS:
	case GPCMD_READ_DVD_STRUCTURE:
		size = (cdb[8] << 8) + cdb[9];
		cmd->se_cmd_flags |= SCF_SCSI_CONTROL_SG_IO_CDB;
		break;
	case READ_POSITION:
		size = READ_POSITION_LEN;
		cmd->se_cmd_flags |= SCF_SCSI_CONTROL_SG_IO_CDB;
		break;
	case MAINTENANCE_OUT:
		if (dev->transport->get_device_type(dev) != TYPE_ROM) {
			/* MAINTENANCE_OUT from SCC-2
			 *
			 * Check for emulated MO_SET_TARGET_PGS.
			 */
			if (cdb[1] == MO_SET_TARGET_PGS &&
			    su_dev->t10_alua.alua_type == SPC3_ALUA_EMULATED) {
				cmd->execute_cmd =
					target_emulate_set_target_port_groups;
			}

			size = (cdb[6] << 24) | (cdb[7] << 16) |
			       (cdb[8] << 8) | cdb[9];
		} else  {
			/* GPCMD_REPORT_KEY from multi media commands */
			size = (cdb[8] << 8) + cdb[9];
		}
		cmd->se_cmd_flags |= SCF_SCSI_CONTROL_SG_IO_CDB;
		break;
	case INQUIRY:
		size = (cdb[3] << 8) + cdb[4];
		/*
		 * Do implict HEAD_OF_QUEUE processing for INQUIRY.
		 * See spc4r17 section 5.3
		 */
		if (cmd->se_dev->dev_task_attr_type == SAM_TASK_ATTR_EMULATED)
			cmd->sam_task_attr = MSG_HEAD_TAG;
		cmd->se_cmd_flags |= SCF_SCSI_CONTROL_SG_IO_CDB;
		if (!passthrough)
			cmd->execute_cmd = target_emulate_inquiry;
		break;
	case READ_BUFFER:
		size = (cdb[6] << 16) + (cdb[7] << 8) + cdb[8];
		cmd->se_cmd_flags |= SCF_SCSI_CONTROL_SG_IO_CDB;
		break;
	case READ_CAPACITY:
		size = READ_CAP_LEN;
		cmd->se_cmd_flags |= SCF_SCSI_CONTROL_SG_IO_CDB;
		if (!passthrough)
			cmd->execute_cmd = target_emulate_readcapacity;
		break;
	case READ_MEDIA_SERIAL_NUMBER:
	case SECURITY_PROTOCOL_IN:
	case SECURITY_PROTOCOL_OUT:
		size = (cdb[6] << 24) | (cdb[7] << 16) | (cdb[8] << 8) | cdb[9];
		cmd->se_cmd_flags |= SCF_SCSI_CONTROL_SG_IO_CDB;
		break;
	case SERVICE_ACTION_IN:
		switch (cmd->t_task_cdb[1] & 0x1f) {
		case SAI_READ_CAPACITY_16:
			if (!passthrough)
				cmd->execute_cmd =
					target_emulate_readcapacity_16;
			break;
		default:
			if (passthrough)
				break;

			pr_err("Unsupported SA: 0x%02x\n",
				cmd->t_task_cdb[1] & 0x1f);
			goto out_invalid_cdb_field;
		}
		/*FALLTHROUGH*/
	case ACCESS_CONTROL_IN:
	case ACCESS_CONTROL_OUT:
	case EXTENDED_COPY:
	case READ_ATTRIBUTE:
	case RECEIVE_COPY_RESULTS:
	case WRITE_ATTRIBUTE:
		size = (cdb[10] << 24) | (cdb[11] << 16) |
		       (cdb[12] << 8) | cdb[13];
		cmd->se_cmd_flags |= SCF_SCSI_CONTROL_SG_IO_CDB;
		break;
	case RECEIVE_DIAGNOSTIC:
	case SEND_DIAGNOSTIC:
		size = (cdb[3] << 8) | cdb[4];
		cmd->se_cmd_flags |= SCF_SCSI_CONTROL_SG_IO_CDB;
		break;
/* #warning FIXME: Figure out correct GPCMD_READ_CD blocksize. */
#if 0
	case GPCMD_READ_CD:
		sectors = (cdb[6] << 16) + (cdb[7] << 8) + cdb[8];
		size = (2336 * sectors);
		cmd->se_cmd_flags |= SCF_SCSI_CONTROL_SG_IO_CDB;
		break;
#endif
	case READ_TOC:
		size = cdb[8];
		cmd->se_cmd_flags |= SCF_SCSI_CONTROL_SG_IO_CDB;
		break;
	case REQUEST_SENSE:
		size = cdb[4];
		cmd->se_cmd_flags |= SCF_SCSI_CONTROL_SG_IO_CDB;
		if (!passthrough)
			cmd->execute_cmd = target_emulate_request_sense;
		break;
	case READ_ELEMENT_STATUS:
		size = 65536 * cdb[7] + 256 * cdb[8] + cdb[9];
		cmd->se_cmd_flags |= SCF_SCSI_CONTROL_SG_IO_CDB;
		break;
	case WRITE_BUFFER:
		size = (cdb[6] << 16) + (cdb[7] << 8) + cdb[8];
		cmd->se_cmd_flags |= SCF_SCSI_CONTROL_SG_IO_CDB;
		break;
	case RESERVE:
	case RESERVE_10:
		/*
		 * The SPC-2 RESERVE does not contain a size in the SCSI CDB.
		 * Assume the passthrough or $FABRIC_MOD will tell us about it.
		 */
		if (cdb[0] == RESERVE_10)
			size = (cdb[7] << 8) | cdb[8];
		else
			size = cmd->data_length;

		/*
		 * Setup the legacy emulated handler for SPC-2 and
		 * >= SPC-3 compatible reservation handling (CRH=1)
		 * Otherwise, we assume the underlying SCSI logic is
		 * is running in SPC_PASSTHROUGH, and wants reservations
		 * emulation disabled.
		 */
		if (su_dev->t10_pr.res_type != SPC_PASSTHROUGH)
			cmd->execute_cmd = target_scsi2_reservation_reserve;
		cmd->se_cmd_flags |= SCF_SCSI_NON_DATA_CDB;
		break;
	case RELEASE:
	case RELEASE_10:
		/*
		 * The SPC-2 RELEASE does not contain a size in the SCSI CDB.
		 * Assume the passthrough or $FABRIC_MOD will tell us about it.
		*/
		if (cdb[0] == RELEASE_10)
			size = (cdb[7] << 8) | cdb[8];
		else
			size = cmd->data_length;

		if (su_dev->t10_pr.res_type != SPC_PASSTHROUGH)
			cmd->execute_cmd = target_scsi2_reservation_release;
		cmd->se_cmd_flags |= SCF_SCSI_NON_DATA_CDB;
		break;
	case SYNCHRONIZE_CACHE:
	case SYNCHRONIZE_CACHE_16:
		/*
		 * Extract LBA and range to be flushed for emulated SYNCHRONIZE_CACHE
		 */
		if (cdb[0] == SYNCHRONIZE_CACHE) {
			sectors = transport_get_sectors_10(cdb, cmd, &sector_ret);
			cmd->t_task_lba = transport_lba_32(cdb);
		} else {
			sectors = transport_get_sectors_16(cdb, cmd, &sector_ret);
			cmd->t_task_lba = transport_lba_64(cdb);
		}
		if (sector_ret)
			goto out_unsupported_cdb;

		size = transport_get_size(sectors, cdb, cmd);
		cmd->se_cmd_flags |= SCF_SCSI_NON_DATA_CDB;

		if (passthrough)
			break;

		/*
		 * Check to ensure that LBA + Range does not exceed past end of
		 * device for IBLOCK and FILEIO ->do_sync_cache() backend calls
		 */
		if ((cmd->t_task_lba != 0) || (sectors != 0)) {
			if (transport_cmd_get_valid_sectors(cmd) < 0)
				goto out_invalid_cdb_field;
		}
		cmd->execute_cmd = target_emulate_synchronize_cache;
		break;
	case UNMAP:
		size = get_unaligned_be16(&cdb[7]);
		cmd->se_cmd_flags |= SCF_SCSI_CONTROL_SG_IO_CDB;
		if (!passthrough)
			cmd->execute_cmd = target_emulate_unmap;
		break;
	case WRITE_SAME_16:
		sectors = transport_get_sectors_16(cdb, cmd, &sector_ret);
		if (sector_ret)
			goto out_unsupported_cdb;

		if (sectors)
			size = transport_get_size(1, cdb, cmd);
		else {
			pr_err("WSNZ=1, WRITE_SAME w/sectors=0 not supported\n");
			goto out_invalid_cdb_field;
		}

		cmd->t_task_lba = get_unaligned_be64(&cdb[2]);
		cmd->se_cmd_flags |= SCF_SCSI_CONTROL_SG_IO_CDB;

		if (target_check_write_same_discard(&cdb[1], dev) < 0)
			goto out_unsupported_cdb;
		if (!passthrough)
			cmd->execute_cmd = target_emulate_write_same;
		break;
	case WRITE_SAME:
		sectors = transport_get_sectors_10(cdb, cmd, &sector_ret);
		if (sector_ret)
			goto out_unsupported_cdb;

		if (sectors)
			size = transport_get_size(1, cdb, cmd);
		else {
			pr_err("WSNZ=1, WRITE_SAME w/sectors=0 not supported\n");
			goto out_invalid_cdb_field;
		}

		cmd->t_task_lba = get_unaligned_be32(&cdb[2]);
		cmd->se_cmd_flags |= SCF_SCSI_CONTROL_SG_IO_CDB;
		/*
		 * Follow sbcr26 with WRITE_SAME (10) and check for the existence
		 * of byte 1 bit 3 UNMAP instead of original reserved field
		 */
		if (target_check_write_same_discard(&cdb[1], dev) < 0)
			goto out_unsupported_cdb;
		if (!passthrough)
			cmd->execute_cmd = target_emulate_write_same;
		break;
	case ALLOW_MEDIUM_REMOVAL:
	case ERASE:
	case REZERO_UNIT:
	case SEEK_10:
	case SPACE:
	case START_STOP:
	case TEST_UNIT_READY:
	case VERIFY:
	case WRITE_FILEMARKS:
		cmd->se_cmd_flags |= SCF_SCSI_NON_DATA_CDB;
		if (!passthrough)
			cmd->execute_cmd = target_emulate_noop;
		break;
	case GPCMD_CLOSE_TRACK:
	case INITIALIZE_ELEMENT_STATUS:
	case GPCMD_LOAD_UNLOAD:
	case GPCMD_SET_SPEED:
	case MOVE_MEDIUM:
		cmd->se_cmd_flags |= SCF_SCSI_NON_DATA_CDB;
		break;
	case REPORT_LUNS:
		cmd->execute_cmd = target_report_luns;
		size = (cdb[6] << 24) | (cdb[7] << 16) | (cdb[8] << 8) | cdb[9];
		/*
		 * Do implict HEAD_OF_QUEUE processing for REPORT_LUNS
		 * See spc4r17 section 5.3
		 */
		if (cmd->se_dev->dev_task_attr_type == SAM_TASK_ATTR_EMULATED)
			cmd->sam_task_attr = MSG_HEAD_TAG;
		cmd->se_cmd_flags |= SCF_SCSI_CONTROL_SG_IO_CDB;
		break;
	case GET_EVENT_STATUS_NOTIFICATION:
		size = (cdb[7] << 8) | cdb[8];
		cmd->se_cmd_flags |= SCF_SCSI_CONTROL_SG_IO_CDB;
		break;
	default:
		pr_warn("TARGET_CORE[%s]: Unsupported SCSI Opcode"
			" 0x%02x, sending CHECK_CONDITION.\n",
			cmd->se_tfo->get_fabric_name(), cdb[0]);
		goto out_unsupported_cdb;
	}

	if (cmd->unknown_data_length)
		cmd->data_length = size;

	if (size != cmd->data_length) {
		pr_warn("TARGET_CORE[%s]: Expected Transfer Length:"
			" %u does not match SCSI CDB Length: %u for SAM Opcode:"
			" 0x%02x\n", cmd->se_tfo->get_fabric_name(),
				cmd->data_length, size, cdb[0]);

		cmd->cmd_spdtl = size;

		if (cmd->data_direction == DMA_TO_DEVICE) {
			pr_err("Rejecting underflow/overflow"
					" WRITE data\n");
			goto out_invalid_cdb_field;
		}
		/*
		 * Reject READ_* or WRITE_* with overflow/underflow for
		 * type SCF_SCSI_DATA_SG_IO_CDB.
		 */
		if (!ret && (dev->se_sub_dev->se_dev_attrib.block_size != 512))  {
			pr_err("Failing OVERFLOW/UNDERFLOW for LBA op"
				" CDB on non 512-byte sector setup subsystem"
				" plugin: %s\n", dev->transport->name);
			/* Returns CHECK_CONDITION + INVALID_CDB_FIELD */
			goto out_invalid_cdb_field;
		}

		if (size > cmd->data_length) {
			cmd->se_cmd_flags |= SCF_OVERFLOW_BIT;
			cmd->residual_count = (size - cmd->data_length);
		} else {
			cmd->se_cmd_flags |= SCF_UNDERFLOW_BIT;
			cmd->residual_count = (cmd->data_length - size);
		}
		cmd->data_length = size;
	}

	if (cmd->se_cmd_flags & SCF_SCSI_DATA_SG_IO_CDB) {
		if (sectors > su_dev->se_dev_attrib.fabric_max_sectors) {
			printk_ratelimited(KERN_ERR "SCSI OP %02xh with too"
				" big sectors %u exceeds fabric_max_sectors:"
				" %u\n", cdb[0], sectors,
				su_dev->se_dev_attrib.fabric_max_sectors);
			goto out_invalid_cdb_field;
		}
		if (sectors > su_dev->se_dev_attrib.hw_max_sectors) {
			printk_ratelimited(KERN_ERR "SCSI OP %02xh with too"
				" big sectors %u exceeds backend hw_max_sectors:"
				" %u\n", cdb[0], sectors,
				su_dev->se_dev_attrib.hw_max_sectors);
			goto out_invalid_cdb_field;
		}
	}

	/* reject any command that we don't have a handler for */
	if (!(passthrough || cmd->execute_cmd ||
	     (cmd->se_cmd_flags & SCF_SCSI_DATA_SG_IO_CDB)))
		goto out_unsupported_cdb;

	transport_set_supported_SAM_opcode(cmd);
	return ret;

out_unsupported_cdb:
	cmd->se_cmd_flags |= SCF_SCSI_CDB_EXCEPTION;
	cmd->scsi_sense_reason = TCM_UNSUPPORTED_SCSI_OPCODE;
	return -EINVAL;
out_invalid_cdb_field:
	cmd->se_cmd_flags |= SCF_SCSI_CDB_EXCEPTION;
	cmd->scsi_sense_reason = TCM_INVALID_CDB_FIELD;
	return -EINVAL;
}

/*
 * Called from I/O completion to determine which dormant/delayed
 * and ordered cmds need to have their tasks added to the execution queue.
 */
static void transport_complete_task_attr(struct se_cmd *cmd)
{
	struct se_device *dev = cmd->se_dev;
	struct se_cmd *cmd_p, *cmd_tmp;
	int new_active_tasks = 0;

	if (cmd->sam_task_attr == MSG_SIMPLE_TAG) {
		atomic_dec(&dev->simple_cmds);
		smp_mb__after_atomic_dec();
		dev->dev_cur_ordered_id++;
		pr_debug("Incremented dev->dev_cur_ordered_id: %u for"
			" SIMPLE: %u\n", dev->dev_cur_ordered_id,
			cmd->se_ordered_id);
	} else if (cmd->sam_task_attr == MSG_HEAD_TAG) {
		dev->dev_cur_ordered_id++;
		pr_debug("Incremented dev_cur_ordered_id: %u for"
			" HEAD_OF_QUEUE: %u\n", dev->dev_cur_ordered_id,
			cmd->se_ordered_id);
	} else if (cmd->sam_task_attr == MSG_ORDERED_TAG) {
		atomic_dec(&dev->dev_ordered_sync);
		smp_mb__after_atomic_dec();

		dev->dev_cur_ordered_id++;
		pr_debug("Incremented dev_cur_ordered_id: %u for ORDERED:"
			" %u\n", dev->dev_cur_ordered_id, cmd->se_ordered_id);
	}
	/*
	 * Process all commands up to the last received
	 * ORDERED task attribute which requires another blocking
	 * boundary
	 */
	spin_lock(&dev->delayed_cmd_lock);
	list_for_each_entry_safe(cmd_p, cmd_tmp,
			&dev->delayed_cmd_list, se_delayed_node) {

		list_del(&cmd_p->se_delayed_node);
		spin_unlock(&dev->delayed_cmd_lock);

		pr_debug("Calling add_tasks() for"
			" cmd_p: 0x%02x Task Attr: 0x%02x"
			" Dormant -> Active, se_ordered_id: %u\n",
			cmd_p->t_task_cdb[0],
			cmd_p->sam_task_attr, cmd_p->se_ordered_id);

		target_add_to_execute_list(cmd_p);
		new_active_tasks++;

		spin_lock(&dev->delayed_cmd_lock);
		if (cmd_p->sam_task_attr == MSG_ORDERED_TAG)
			break;
	}
	spin_unlock(&dev->delayed_cmd_lock);
	/*
	 * If new tasks have become active, wake up the transport thread
	 * to do the processing of the Active tasks.
	 */
	if (new_active_tasks != 0)
		wake_up_interruptible(&dev->dev_queue_obj.thread_wq);
}

static void transport_complete_qf(struct se_cmd *cmd)
{
	int ret = 0;

	if (cmd->se_dev->dev_task_attr_type == SAM_TASK_ATTR_EMULATED)
		transport_complete_task_attr(cmd);

	if (cmd->se_cmd_flags & SCF_TRANSPORT_TASK_SENSE) {
		ret = cmd->se_tfo->queue_status(cmd);
		if (ret)
			goto out;
	}

	switch (cmd->data_direction) {
	case DMA_FROM_DEVICE:
		ret = cmd->se_tfo->queue_data_in(cmd);
		break;
	case DMA_TO_DEVICE:
		if (cmd->t_bidi_data_sg) {
			ret = cmd->se_tfo->queue_data_in(cmd);
			if (ret < 0)
				break;
		}
		/* Fall through for DMA_TO_DEVICE */
	case DMA_NONE:
		ret = cmd->se_tfo->queue_status(cmd);
		break;
	default:
		break;
	}

out:
	if (ret < 0) {
		transport_handle_queue_full(cmd, cmd->se_dev);
		return;
	}
	transport_lun_remove_cmd(cmd);
	transport_cmd_check_stop_to_fabric(cmd);
}

static void transport_handle_queue_full(
	struct se_cmd *cmd,
	struct se_device *dev)
{
	spin_lock_irq(&dev->qf_cmd_lock);
	list_add_tail(&cmd->se_qf_node, &cmd->se_dev->qf_cmd_list);
	atomic_inc(&dev->dev_qf_count);
	smp_mb__after_atomic_inc();
	spin_unlock_irq(&cmd->se_dev->qf_cmd_lock);

	schedule_work(&cmd->se_dev->qf_work_queue);
}

static void target_complete_ok_work(struct work_struct *work)
{
	struct se_cmd *cmd = container_of(work, struct se_cmd, work);
	int reason = 0, ret;

	/*
	 * Check if we need to move delayed/dormant tasks from cmds on the
	 * delayed execution list after a HEAD_OF_QUEUE or ORDERED Task
	 * Attribute.
	 */
	if (cmd->se_dev->dev_task_attr_type == SAM_TASK_ATTR_EMULATED)
		transport_complete_task_attr(cmd);
	/*
	 * Check to schedule QUEUE_FULL work, or execute an existing
	 * cmd->transport_qf_callback()
	 */
	if (atomic_read(&cmd->se_dev->dev_qf_count) != 0)
		schedule_work(&cmd->se_dev->qf_work_queue);

	/*
	 * Check if we need to retrieve a sense buffer from
	 * the struct se_cmd in question.
	 */
	if (cmd->se_cmd_flags & SCF_TRANSPORT_TASK_SENSE) {
		if (transport_get_sense_data(cmd) < 0)
			reason = TCM_NON_EXISTENT_LUN;

		if (cmd->scsi_status) {
			ret = transport_send_check_condition_and_sense(
					cmd, reason, 1);
			if (ret == -EAGAIN || ret == -ENOMEM)
				goto queue_full;

			transport_lun_remove_cmd(cmd);
			transport_cmd_check_stop_to_fabric(cmd);
			return;
		}
	}
	/*
	 * Check for a callback, used by amongst other things
	 * XDWRITE_READ_10 emulation.
	 */
	if (cmd->transport_complete_callback)
		cmd->transport_complete_callback(cmd);

	switch (cmd->data_direction) {
	case DMA_FROM_DEVICE:
		spin_lock(&cmd->se_lun->lun_sep_lock);
		if (cmd->se_lun->lun_sep) {
			cmd->se_lun->lun_sep->sep_stats.tx_data_octets +=
					cmd->data_length;
		}
		spin_unlock(&cmd->se_lun->lun_sep_lock);

		ret = cmd->se_tfo->queue_data_in(cmd);
		if (ret == -EAGAIN || ret == -ENOMEM)
			goto queue_full;
		break;
	case DMA_TO_DEVICE:
		spin_lock(&cmd->se_lun->lun_sep_lock);
		if (cmd->se_lun->lun_sep) {
			cmd->se_lun->lun_sep->sep_stats.rx_data_octets +=
				cmd->data_length;
		}
		spin_unlock(&cmd->se_lun->lun_sep_lock);
		/*
		 * Check if we need to send READ payload for BIDI-COMMAND
		 */
		if (cmd->t_bidi_data_sg) {
			spin_lock(&cmd->se_lun->lun_sep_lock);
			if (cmd->se_lun->lun_sep) {
				cmd->se_lun->lun_sep->sep_stats.tx_data_octets +=
					cmd->data_length;
			}
			spin_unlock(&cmd->se_lun->lun_sep_lock);
			ret = cmd->se_tfo->queue_data_in(cmd);
			if (ret == -EAGAIN || ret == -ENOMEM)
				goto queue_full;
			break;
		}
		/* Fall through for DMA_TO_DEVICE */
	case DMA_NONE:
		ret = cmd->se_tfo->queue_status(cmd);
		if (ret == -EAGAIN || ret == -ENOMEM)
			goto queue_full;
		break;
	default:
		break;
	}

	transport_lun_remove_cmd(cmd);
	transport_cmd_check_stop_to_fabric(cmd);
	return;

queue_full:
	pr_debug("Handling complete_ok QUEUE_FULL: se_cmd: %p,"
		" data_direction: %d\n", cmd, cmd->data_direction);
	cmd->t_state = TRANSPORT_COMPLETE_QF_OK;
	transport_handle_queue_full(cmd, cmd->se_dev);
}

static inline void transport_free_sgl(struct scatterlist *sgl, int nents)
{
	struct scatterlist *sg;
	int count;

	for_each_sg(sgl, sg, nents, count)
		__free_page(sg_page(sg));

	kfree(sgl);
}

static inline void transport_free_pages(struct se_cmd *cmd)
{
	if (cmd->se_cmd_flags & SCF_PASSTHROUGH_SG_TO_MEM_NOALLOC)
		return;

	transport_free_sgl(cmd->t_data_sg, cmd->t_data_nents);
	cmd->t_data_sg = NULL;
	cmd->t_data_nents = 0;

	transport_free_sgl(cmd->t_bidi_data_sg, cmd->t_bidi_data_nents);
	cmd->t_bidi_data_sg = NULL;
	cmd->t_bidi_data_nents = 0;
}

/**
 * transport_release_cmd - free a command
 * @cmd:       command to free
 *
 * This routine unconditionally frees a command, and reference counting
 * or list removal must be done in the caller.
 */
static void transport_release_cmd(struct se_cmd *cmd)
{
	BUG_ON(!cmd->se_tfo);

	if (cmd->se_cmd_flags & SCF_SCSI_TMR_CDB)
		core_tmr_release_req(cmd->se_tmr_req);
	if (cmd->t_task_cdb != cmd->__t_task_cdb)
		kfree(cmd->t_task_cdb);
	/*
	 * If this cmd has been setup with target_get_sess_cmd(), drop
	 * the kref and call ->release_cmd() in kref callback.
	 */
	 if (cmd->check_release != 0) {
		target_put_sess_cmd(cmd->se_sess, cmd);
		return;
	}
	cmd->se_tfo->release_cmd(cmd);
}

/**
 * transport_put_cmd - release a reference to a command
 * @cmd:       command to release
 *
 * This routine releases our reference to the command and frees it if possible.
 */
static void transport_put_cmd(struct se_cmd *cmd)
{
	unsigned long flags;

	spin_lock_irqsave(&cmd->t_state_lock, flags);
	if (atomic_read(&cmd->t_fe_count)) {
		if (!atomic_dec_and_test(&cmd->t_fe_count))
			goto out_busy;
	}

	if (cmd->transport_state & CMD_T_DEV_ACTIVE) {
		cmd->transport_state &= ~CMD_T_DEV_ACTIVE;
		target_remove_from_state_list(cmd);
	}
	spin_unlock_irqrestore(&cmd->t_state_lock, flags);

	transport_free_pages(cmd);
	transport_release_cmd(cmd);
	return;
out_busy:
	spin_unlock_irqrestore(&cmd->t_state_lock, flags);
}

/*
 * transport_generic_map_mem_to_cmd - Use fabric-alloced pages instead of
 * allocating in the core.
 * @cmd:  Associated se_cmd descriptor
 * @mem:  SGL style memory for TCM WRITE / READ
 * @sg_mem_num: Number of SGL elements
 * @mem_bidi_in: SGL style memory for TCM BIDI READ
 * @sg_mem_bidi_num: Number of BIDI READ SGL elements
 *
 * Return: nonzero return cmd was rejected for -ENOMEM or inproper usage
 * of parameters.
 */
int transport_generic_map_mem_to_cmd(
	struct se_cmd *cmd,
	struct scatterlist *sgl,
	u32 sgl_count,
	struct scatterlist *sgl_bidi,
	u32 sgl_bidi_count)
{
	if (!sgl || !sgl_count)
		return 0;

	if ((cmd->se_cmd_flags & SCF_SCSI_DATA_SG_IO_CDB) ||
	    (cmd->se_cmd_flags & SCF_SCSI_CONTROL_SG_IO_CDB)) {
		/*
		 * Reject SCSI data overflow with map_mem_to_cmd() as incoming
		 * scatterlists already have been set to follow what the fabric
		 * passes for the original expected data transfer length.
		 */
		if (cmd->se_cmd_flags & SCF_OVERFLOW_BIT) {
			pr_warn("Rejecting SCSI DATA overflow for fabric using"
				" SCF_PASSTHROUGH_SG_TO_MEM_NOALLOC\n");
			cmd->se_cmd_flags |= SCF_SCSI_CDB_EXCEPTION;
			cmd->scsi_sense_reason = TCM_INVALID_CDB_FIELD;
			return -EINVAL;
		}

		cmd->t_data_sg = sgl;
		cmd->t_data_nents = sgl_count;

		if (sgl_bidi && sgl_bidi_count) {
			cmd->t_bidi_data_sg = sgl_bidi;
			cmd->t_bidi_data_nents = sgl_bidi_count;
		}
		cmd->se_cmd_flags |= SCF_PASSTHROUGH_SG_TO_MEM_NOALLOC;
	}

	return 0;
}
EXPORT_SYMBOL(transport_generic_map_mem_to_cmd);

void *transport_kmap_data_sg(struct se_cmd *cmd)
{
	struct scatterlist *sg = cmd->t_data_sg;
	struct page **pages;
	int i;

	BUG_ON(!sg);
	/*
	 * We need to take into account a possible offset here for fabrics like
	 * tcm_loop who may be using a contig buffer from the SCSI midlayer for
	 * control CDBs passed as SGLs via transport_generic_map_mem_to_cmd()
	 */
	if (!cmd->t_data_nents)
		return NULL;
	else if (cmd->t_data_nents == 1)
		return kmap(sg_page(sg)) + sg->offset;

	/* >1 page. use vmap */
	pages = kmalloc(sizeof(*pages) * cmd->t_data_nents, GFP_KERNEL);
	if (!pages)
		return NULL;

	/* convert sg[] to pages[] */
	for_each_sg(cmd->t_data_sg, sg, cmd->t_data_nents, i) {
		pages[i] = sg_page(sg);
	}

	cmd->t_data_vmap = vmap(pages, cmd->t_data_nents,  VM_MAP, PAGE_KERNEL);
	kfree(pages);
	if (!cmd->t_data_vmap)
		return NULL;

	return cmd->t_data_vmap + cmd->t_data_sg[0].offset;
}
EXPORT_SYMBOL(transport_kmap_data_sg);

void transport_kunmap_data_sg(struct se_cmd *cmd)
{
	if (!cmd->t_data_nents) {
		return;
	} else if (cmd->t_data_nents == 1) {
		kunmap(sg_page(cmd->t_data_sg));
		return;
	}

	vunmap(cmd->t_data_vmap);
	cmd->t_data_vmap = NULL;
}
EXPORT_SYMBOL(transport_kunmap_data_sg);

static int
transport_generic_get_mem(struct se_cmd *cmd)
{
	u32 length = cmd->data_length;
	unsigned int nents;
	struct page *page;
	gfp_t zero_flag;
	int i = 0;

	nents = DIV_ROUND_UP(length, PAGE_SIZE);
	cmd->t_data_sg = kmalloc(sizeof(struct scatterlist) * nents, GFP_KERNEL);
	if (!cmd->t_data_sg)
		return -ENOMEM;

	cmd->t_data_nents = nents;
	sg_init_table(cmd->t_data_sg, nents);

	zero_flag = cmd->se_cmd_flags & SCF_SCSI_DATA_SG_IO_CDB ? 0 : __GFP_ZERO;

	while (length) {
		u32 page_len = min_t(u32, length, PAGE_SIZE);
		page = alloc_page(GFP_KERNEL | zero_flag);
		if (!page)
			goto out;

		sg_set_page(&cmd->t_data_sg[i], page, page_len, 0);
		length -= page_len;
		i++;
	}
	return 0;

out:
	while (i >= 0) {
		__free_page(sg_page(&cmd->t_data_sg[i]));
		i--;
	}
	kfree(cmd->t_data_sg);
	cmd->t_data_sg = NULL;
	return -ENOMEM;
}

/*
 * Allocate any required resources to execute the command.  For writes we
 * might not have the payload yet, so notify the fabric via a call to
 * ->write_pending instead. Otherwise place it on the execution queue.
 */
int transport_generic_new_cmd(struct se_cmd *cmd)
{
	struct se_device *dev = cmd->se_dev;
	int ret = 0;

	/*
	 * Determine is the TCM fabric module has already allocated physical
	 * memory, and is directly calling transport_generic_map_mem_to_cmd()
	 * beforehand.
	 */
	if (!(cmd->se_cmd_flags & SCF_PASSTHROUGH_SG_TO_MEM_NOALLOC) &&
	    cmd->data_length) {
		ret = transport_generic_get_mem(cmd);
		if (ret < 0)
			goto out_fail;
	}

	/* Workaround for handling zero-length control CDBs */
	if ((cmd->se_cmd_flags & SCF_SCSI_CONTROL_SG_IO_CDB) &&
	    !cmd->data_length) {
		spin_lock_irq(&cmd->t_state_lock);
		cmd->t_state = TRANSPORT_COMPLETE;
		cmd->transport_state |= CMD_T_ACTIVE;
		spin_unlock_irq(&cmd->t_state_lock);

		if (cmd->t_task_cdb[0] == REQUEST_SENSE) {
			u8 ua_asc = 0, ua_ascq = 0;

			core_scsi3_ua_clear_for_request_sense(cmd,
					&ua_asc, &ua_ascq);
		}

		INIT_WORK(&cmd->work, target_complete_ok_work);
		queue_work(target_completion_wq, &cmd->work);
		return 0;
	}

	if (cmd->se_cmd_flags & SCF_SCSI_DATA_SG_IO_CDB) {
		struct se_dev_attrib *attr = &dev->se_sub_dev->se_dev_attrib;

		if (transport_cmd_get_valid_sectors(cmd) < 0)
			return -EINVAL;

		BUG_ON(cmd->data_length % attr->block_size);
		BUG_ON(DIV_ROUND_UP(cmd->data_length, attr->block_size) >
			attr->hw_max_sectors);
	}

	atomic_inc(&cmd->t_fe_count);

	/*
	 * For WRITEs, let the fabric know its buffer is ready.
	 *
	 * The command will be added to the execution queue after its write
	 * data has arrived.
	 */
	if (cmd->data_direction == DMA_TO_DEVICE) {
		target_add_to_state_list(cmd);
		return transport_generic_write_pending(cmd);
	}
	/*
	 * Everything else but a WRITE, add the command to the execution queue.
	 */
	transport_execute_tasks(cmd);
	return 0;

out_fail:
	cmd->se_cmd_flags |= SCF_SCSI_CDB_EXCEPTION;
	cmd->scsi_sense_reason = TCM_LOGICAL_UNIT_COMMUNICATION_FAILURE;
	return -EINVAL;
}
EXPORT_SYMBOL(transport_generic_new_cmd);

/*	transport_generic_process_write():
 *
 *
 */
void transport_generic_process_write(struct se_cmd *cmd)
{
	transport_execute_tasks(cmd);
}
EXPORT_SYMBOL(transport_generic_process_write);

static void transport_write_pending_qf(struct se_cmd *cmd)
{
	int ret;

	ret = cmd->se_tfo->write_pending(cmd);
	if (ret == -EAGAIN || ret == -ENOMEM) {
		pr_debug("Handling write_pending QUEUE__FULL: se_cmd: %p\n",
			 cmd);
		transport_handle_queue_full(cmd, cmd->se_dev);
	}
}

static int transport_generic_write_pending(struct se_cmd *cmd)
{
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&cmd->t_state_lock, flags);
	cmd->t_state = TRANSPORT_WRITE_PENDING;
	spin_unlock_irqrestore(&cmd->t_state_lock, flags);

	/*
	 * Clear the se_cmd for WRITE_PENDING status in order to set
	 * CMD_T_ACTIVE so that transport_generic_handle_data can be called
	 * from HW target mode interrupt code.  This is safe to be called
	 * with transport_off=1 before the cmd->se_tfo->write_pending
	 * because the se_cmd->se_lun pointer is not being cleared.
	 */
	transport_cmd_check_stop(cmd, 1, 0);

	/*
	 * Call the fabric write_pending function here to let the
	 * frontend know that WRITE buffers are ready.
	 */
	ret = cmd->se_tfo->write_pending(cmd);
	if (ret == -EAGAIN || ret == -ENOMEM)
		goto queue_full;
	else if (ret < 0)
		return ret;

	return 1;

queue_full:
	pr_debug("Handling write_pending QUEUE__FULL: se_cmd: %p\n", cmd);
	cmd->t_state = TRANSPORT_COMPLETE_QF_WP;
	transport_handle_queue_full(cmd, cmd->se_dev);
	return 0;
}

void transport_generic_free_cmd(struct se_cmd *cmd, int wait_for_tasks)
{
	if (!(cmd->se_cmd_flags & SCF_SE_LUN_CMD)) {
		if (wait_for_tasks && (cmd->se_cmd_flags & SCF_SCSI_TMR_CDB))
			 transport_wait_for_tasks(cmd);

		transport_release_cmd(cmd);
	} else {
		if (wait_for_tasks)
			transport_wait_for_tasks(cmd);

		core_dec_lacl_count(cmd->se_sess->se_node_acl, cmd);

		if (cmd->se_lun)
			transport_lun_remove_cmd(cmd);

		transport_put_cmd(cmd);
	}
}
EXPORT_SYMBOL(transport_generic_free_cmd);

/* target_get_sess_cmd - Add command to active ->sess_cmd_list
 * @se_sess:	session to reference
 * @se_cmd:	command descriptor to add
 * @ack_kref:	Signal that fabric will perform an ack target_put_sess_cmd()
 */
void target_get_sess_cmd(struct se_session *se_sess, struct se_cmd *se_cmd,
			bool ack_kref)
{
	unsigned long flags;

	kref_init(&se_cmd->cmd_kref);
	/*
	 * Add a second kref if the fabric caller is expecting to handle
	 * fabric acknowledgement that requires two target_put_sess_cmd()
	 * invocations before se_cmd descriptor release.
	 */
	if (ack_kref == true) {
		kref_get(&se_cmd->cmd_kref);
		se_cmd->se_cmd_flags |= SCF_ACK_KREF;
	}

	spin_lock_irqsave(&se_sess->sess_cmd_lock, flags);
	list_add_tail(&se_cmd->se_cmd_list, &se_sess->sess_cmd_list);
	se_cmd->check_release = 1;
	spin_unlock_irqrestore(&se_sess->sess_cmd_lock, flags);
}
EXPORT_SYMBOL(target_get_sess_cmd);

static void target_release_cmd_kref(struct kref *kref)
{
	struct se_cmd *se_cmd = container_of(kref, struct se_cmd, cmd_kref);
	struct se_session *se_sess = se_cmd->se_sess;
	unsigned long flags;

	spin_lock_irqsave(&se_sess->sess_cmd_lock, flags);
	if (list_empty(&se_cmd->se_cmd_list)) {
		spin_unlock_irqrestore(&se_sess->sess_cmd_lock, flags);
		se_cmd->se_tfo->release_cmd(se_cmd);
		return;
	}
	if (se_sess->sess_tearing_down && se_cmd->cmd_wait_set) {
		spin_unlock_irqrestore(&se_sess->sess_cmd_lock, flags);
		complete(&se_cmd->cmd_wait_comp);
		return;
	}
	list_del(&se_cmd->se_cmd_list);
	spin_unlock_irqrestore(&se_sess->sess_cmd_lock, flags);

	se_cmd->se_tfo->release_cmd(se_cmd);
}

/* target_put_sess_cmd - Check for active I/O shutdown via kref_put
 * @se_sess:	session to reference
 * @se_cmd:	command descriptor to drop
 */
int target_put_sess_cmd(struct se_session *se_sess, struct se_cmd *se_cmd)
{
	return kref_put(&se_cmd->cmd_kref, target_release_cmd_kref);
}
EXPORT_SYMBOL(target_put_sess_cmd);

/* target_splice_sess_cmd_list - Split active cmds into sess_wait_list
 * @se_sess:	session to split
 */
void target_splice_sess_cmd_list(struct se_session *se_sess)
{
	struct se_cmd *se_cmd;
	unsigned long flags;

	WARN_ON(!list_empty(&se_sess->sess_wait_list));
	INIT_LIST_HEAD(&se_sess->sess_wait_list);

	spin_lock_irqsave(&se_sess->sess_cmd_lock, flags);
	se_sess->sess_tearing_down = 1;

	list_splice_init(&se_sess->sess_cmd_list, &se_sess->sess_wait_list);

	list_for_each_entry(se_cmd, &se_sess->sess_wait_list, se_cmd_list)
		se_cmd->cmd_wait_set = 1;

	spin_unlock_irqrestore(&se_sess->sess_cmd_lock, flags);
}
EXPORT_SYMBOL(target_splice_sess_cmd_list);

/* target_wait_for_sess_cmds - Wait for outstanding descriptors
 * @se_sess:    session to wait for active I/O
 * @wait_for_tasks:	Make extra transport_wait_for_tasks call
 */
void target_wait_for_sess_cmds(
	struct se_session *se_sess,
	int wait_for_tasks)
{
	struct se_cmd *se_cmd, *tmp_cmd;
	bool rc = false;

	list_for_each_entry_safe(se_cmd, tmp_cmd,
				&se_sess->sess_wait_list, se_cmd_list) {
		list_del(&se_cmd->se_cmd_list);

		pr_debug("Waiting for se_cmd: %p t_state: %d, fabric state:"
			" %d\n", se_cmd, se_cmd->t_state,
			se_cmd->se_tfo->get_cmd_state(se_cmd));

		if (wait_for_tasks) {
			pr_debug("Calling transport_wait_for_tasks se_cmd: %p t_state: %d,"
				" fabric state: %d\n", se_cmd, se_cmd->t_state,
				se_cmd->se_tfo->get_cmd_state(se_cmd));

			rc = transport_wait_for_tasks(se_cmd);

			pr_debug("After transport_wait_for_tasks se_cmd: %p t_state: %d,"
				" fabric state: %d\n", se_cmd, se_cmd->t_state,
				se_cmd->se_tfo->get_cmd_state(se_cmd));
		}

		if (!rc) {
			wait_for_completion(&se_cmd->cmd_wait_comp);
			pr_debug("After cmd_wait_comp: se_cmd: %p t_state: %d"
				" fabric state: %d\n", se_cmd, se_cmd->t_state,
				se_cmd->se_tfo->get_cmd_state(se_cmd));
		}

		se_cmd->se_tfo->release_cmd(se_cmd);
	}
}
EXPORT_SYMBOL(target_wait_for_sess_cmds);

/*	transport_lun_wait_for_tasks():
 *
 *	Called from ConfigFS context to stop the passed struct se_cmd to allow
 *	an struct se_lun to be successfully shutdown.
 */
static int transport_lun_wait_for_tasks(struct se_cmd *cmd, struct se_lun *lun)
{
	unsigned long flags;
	int ret = 0;

	/*
	 * If the frontend has already requested this struct se_cmd to
	 * be stopped, we can safely ignore this struct se_cmd.
	 */
	spin_lock_irqsave(&cmd->t_state_lock, flags);
	if (cmd->transport_state & CMD_T_STOP) {
		cmd->transport_state &= ~CMD_T_LUN_STOP;

		pr_debug("ConfigFS ITT[0x%08x] - CMD_T_STOP, skipping\n",
			 cmd->se_tfo->get_task_tag(cmd));
		spin_unlock_irqrestore(&cmd->t_state_lock, flags);
		transport_cmd_check_stop(cmd, 1, 0);
		return -EPERM;
	}
	cmd->transport_state |= CMD_T_LUN_FE_STOP;
	spin_unlock_irqrestore(&cmd->t_state_lock, flags);

	wake_up_interruptible(&cmd->se_dev->dev_queue_obj.thread_wq);

	// XXX: audit task_flags checks.
	spin_lock_irqsave(&cmd->t_state_lock, flags);
	if ((cmd->transport_state & CMD_T_BUSY) &&
	    (cmd->transport_state & CMD_T_SENT)) {
		if (!target_stop_cmd(cmd, &flags))
			ret++;
		spin_unlock_irqrestore(&cmd->t_state_lock, flags);
	} else {
		spin_unlock_irqrestore(&cmd->t_state_lock,
				flags);
		target_remove_from_execute_list(cmd);
	}

	pr_debug("ConfigFS: cmd: %p stop tasks ret:"
			" %d\n", cmd, ret);
	if (!ret) {
		pr_debug("ConfigFS: ITT[0x%08x] - stopping cmd....\n",
				cmd->se_tfo->get_task_tag(cmd));
		wait_for_completion(&cmd->transport_lun_stop_comp);
		pr_debug("ConfigFS: ITT[0x%08x] - stopped cmd....\n",
				cmd->se_tfo->get_task_tag(cmd));
	}
	transport_remove_cmd_from_queue(cmd);

	return 0;
}

static void __transport_clear_lun_from_sessions(struct se_lun *lun)
{
	struct se_cmd *cmd = NULL;
	unsigned long lun_flags, cmd_flags;
	/*
	 * Do exception processing and return CHECK_CONDITION status to the
	 * Initiator Port.
	 */
	spin_lock_irqsave(&lun->lun_cmd_lock, lun_flags);
	while (!list_empty(&lun->lun_cmd_list)) {
		cmd = list_first_entry(&lun->lun_cmd_list,
		       struct se_cmd, se_lun_node);
		list_del_init(&cmd->se_lun_node);

		/*
		 * This will notify iscsi_target_transport.c:
		 * transport_cmd_check_stop() that a LUN shutdown is in
		 * progress for the iscsi_cmd_t.
		 */
		spin_lock(&cmd->t_state_lock);
		pr_debug("SE_LUN[%d] - Setting cmd->transport"
			"_lun_stop for  ITT: 0x%08x\n",
			cmd->se_lun->unpacked_lun,
			cmd->se_tfo->get_task_tag(cmd));
		cmd->transport_state |= CMD_T_LUN_STOP;
		spin_unlock(&cmd->t_state_lock);

		spin_unlock_irqrestore(&lun->lun_cmd_lock, lun_flags);

		if (!cmd->se_lun) {
			pr_err("ITT: 0x%08x, [i,t]_state: %u/%u\n",
				cmd->se_tfo->get_task_tag(cmd),
				cmd->se_tfo->get_cmd_state(cmd), cmd->t_state);
			BUG();
		}
		/*
		 * If the Storage engine still owns the iscsi_cmd_t, determine
		 * and/or stop its context.
		 */
		pr_debug("SE_LUN[%d] - ITT: 0x%08x before transport"
			"_lun_wait_for_tasks()\n", cmd->se_lun->unpacked_lun,
			cmd->se_tfo->get_task_tag(cmd));

		if (transport_lun_wait_for_tasks(cmd, cmd->se_lun) < 0) {
			spin_lock_irqsave(&lun->lun_cmd_lock, lun_flags);
			continue;
		}

		pr_debug("SE_LUN[%d] - ITT: 0x%08x after transport_lun"
			"_wait_for_tasks(): SUCCESS\n",
			cmd->se_lun->unpacked_lun,
			cmd->se_tfo->get_task_tag(cmd));

		spin_lock_irqsave(&cmd->t_state_lock, cmd_flags);
		if (!(cmd->transport_state & CMD_T_DEV_ACTIVE)) {
			spin_unlock_irqrestore(&cmd->t_state_lock, cmd_flags);
			goto check_cond;
		}
		cmd->transport_state &= ~CMD_T_DEV_ACTIVE;
		target_remove_from_state_list(cmd);
		spin_unlock_irqrestore(&cmd->t_state_lock, cmd_flags);

		/*
		 * The Storage engine stopped this struct se_cmd before it was
		 * send to the fabric frontend for delivery back to the
		 * Initiator Node.  Return this SCSI CDB back with an
		 * CHECK_CONDITION status.
		 */
check_cond:
		transport_send_check_condition_and_sense(cmd,
				TCM_NON_EXISTENT_LUN, 0);
		/*
		 *  If the fabric frontend is waiting for this iscsi_cmd_t to
		 * be released, notify the waiting thread now that LU has
		 * finished accessing it.
		 */
		spin_lock_irqsave(&cmd->t_state_lock, cmd_flags);
		if (cmd->transport_state & CMD_T_LUN_FE_STOP) {
			pr_debug("SE_LUN[%d] - Detected FE stop for"
				" struct se_cmd: %p ITT: 0x%08x\n",
				lun->unpacked_lun,
				cmd, cmd->se_tfo->get_task_tag(cmd));

			spin_unlock_irqrestore(&cmd->t_state_lock,
					cmd_flags);
			transport_cmd_check_stop(cmd, 1, 0);
			complete(&cmd->transport_lun_fe_stop_comp);
			spin_lock_irqsave(&lun->lun_cmd_lock, lun_flags);
			continue;
		}
		pr_debug("SE_LUN[%d] - ITT: 0x%08x finished processing\n",
			lun->unpacked_lun, cmd->se_tfo->get_task_tag(cmd));

		spin_unlock_irqrestore(&cmd->t_state_lock, cmd_flags);
		spin_lock_irqsave(&lun->lun_cmd_lock, lun_flags);
	}
	spin_unlock_irqrestore(&lun->lun_cmd_lock, lun_flags);
}

static int transport_clear_lun_thread(void *p)
{
	struct se_lun *lun = p;

	__transport_clear_lun_from_sessions(lun);
	complete(&lun->lun_shutdown_comp);

	return 0;
}

int transport_clear_lun_from_sessions(struct se_lun *lun)
{
	struct task_struct *kt;

	kt = kthread_run(transport_clear_lun_thread, lun,
			"tcm_cl_%u", lun->unpacked_lun);
	if (IS_ERR(kt)) {
		pr_err("Unable to start clear_lun thread\n");
		return PTR_ERR(kt);
	}
	wait_for_completion(&lun->lun_shutdown_comp);

	return 0;
}

/**
 * transport_wait_for_tasks - wait for completion to occur
 * @cmd:	command to wait
 *
 * Called from frontend fabric context to wait for storage engine
 * to pause and/or release frontend generated struct se_cmd.
 */
bool transport_wait_for_tasks(struct se_cmd *cmd)
{
	unsigned long flags;

	spin_lock_irqsave(&cmd->t_state_lock, flags);
	if (!(cmd->se_cmd_flags & SCF_SE_LUN_CMD) &&
	    !(cmd->se_cmd_flags & SCF_SCSI_TMR_CDB)) {
		spin_unlock_irqrestore(&cmd->t_state_lock, flags);
		return false;
	}
	/*
	 * Only perform a possible wait_for_tasks if SCF_SUPPORTED_SAM_OPCODE
	 * has been set in transport_set_supported_SAM_opcode().
	 */
	if (!(cmd->se_cmd_flags & SCF_SUPPORTED_SAM_OPCODE) &&
	    !(cmd->se_cmd_flags & SCF_SCSI_TMR_CDB)) {
		spin_unlock_irqrestore(&cmd->t_state_lock, flags);
		return false;
	}
	/*
	 * If we are already stopped due to an external event (ie: LUN shutdown)
	 * sleep until the connection can have the passed struct se_cmd back.
	 * The cmd->transport_lun_stopped_sem will be upped by
	 * transport_clear_lun_from_sessions() once the ConfigFS context caller
	 * has completed its operation on the struct se_cmd.
	 */
	if (cmd->transport_state & CMD_T_LUN_STOP) {
		pr_debug("wait_for_tasks: Stopping"
			" wait_for_completion(&cmd->t_tasktransport_lun_fe"
			"_stop_comp); for ITT: 0x%08x\n",
			cmd->se_tfo->get_task_tag(cmd));
		/*
		 * There is a special case for WRITES where a FE exception +
		 * LUN shutdown means ConfigFS context is still sleeping on
		 * transport_lun_stop_comp in transport_lun_wait_for_tasks().
		 * We go ahead and up transport_lun_stop_comp just to be sure
		 * here.
		 */
		spin_unlock_irqrestore(&cmd->t_state_lock, flags);
		complete(&cmd->transport_lun_stop_comp);
		wait_for_completion(&cmd->transport_lun_fe_stop_comp);
		spin_lock_irqsave(&cmd->t_state_lock, flags);

		target_remove_from_state_list(cmd);
		/*
		 * At this point, the frontend who was the originator of this
		 * struct se_cmd, now owns the structure and can be released through
		 * normal means below.
		 */
		pr_debug("wait_for_tasks: Stopped"
			" wait_for_completion(&cmd->t_tasktransport_lun_fe_"
			"stop_comp); for ITT: 0x%08x\n",
			cmd->se_tfo->get_task_tag(cmd));

		cmd->transport_state &= ~CMD_T_LUN_STOP;
	}

	if (!(cmd->transport_state & CMD_T_ACTIVE)) {
		spin_unlock_irqrestore(&cmd->t_state_lock, flags);
		return false;
	}

	cmd->transport_state |= CMD_T_STOP;

	pr_debug("wait_for_tasks: Stopping %p ITT: 0x%08x"
		" i_state: %d, t_state: %d, CMD_T_STOP\n",
		cmd, cmd->se_tfo->get_task_tag(cmd),
		cmd->se_tfo->get_cmd_state(cmd), cmd->t_state);

	spin_unlock_irqrestore(&cmd->t_state_lock, flags);

	wake_up_interruptible(&cmd->se_dev->dev_queue_obj.thread_wq);

	wait_for_completion(&cmd->t_transport_stop_comp);

	spin_lock_irqsave(&cmd->t_state_lock, flags);
	cmd->transport_state &= ~(CMD_T_ACTIVE | CMD_T_STOP);

	pr_debug("wait_for_tasks: Stopped wait_for_compltion("
		"&cmd->t_transport_stop_comp) for ITT: 0x%08x\n",
		cmd->se_tfo->get_task_tag(cmd));

	spin_unlock_irqrestore(&cmd->t_state_lock, flags);

	return true;
}
EXPORT_SYMBOL(transport_wait_for_tasks);

static int transport_get_sense_codes(
	struct se_cmd *cmd,
	u8 *asc,
	u8 *ascq)
{
	*asc = cmd->scsi_asc;
	*ascq = cmd->scsi_ascq;

	return 0;
}

static int transport_set_sense_codes(
	struct se_cmd *cmd,
	u8 asc,
	u8 ascq)
{
	cmd->scsi_asc = asc;
	cmd->scsi_ascq = ascq;

	return 0;
}

int transport_send_check_condition_and_sense(
	struct se_cmd *cmd,
	u8 reason,
	int from_transport)
{
	unsigned char *buffer = cmd->sense_buffer;
	unsigned long flags;
	int offset;
	u8 asc = 0, ascq = 0;

	spin_lock_irqsave(&cmd->t_state_lock, flags);
	if (cmd->se_cmd_flags & SCF_SENT_CHECK_CONDITION) {
		spin_unlock_irqrestore(&cmd->t_state_lock, flags);
		return 0;
	}
	cmd->se_cmd_flags |= SCF_SENT_CHECK_CONDITION;
	spin_unlock_irqrestore(&cmd->t_state_lock, flags);

	if (!reason && from_transport)
		goto after_reason;

	if (!from_transport)
		cmd->se_cmd_flags |= SCF_EMULATED_TASK_SENSE;
	/*
	 * Data Segment and SenseLength of the fabric response PDU.
	 *
	 * TRANSPORT_SENSE_BUFFER is now set to SCSI_SENSE_BUFFERSIZE
	 * from include/scsi/scsi_cmnd.h
	 */
	offset = cmd->se_tfo->set_fabric_sense_len(cmd,
				TRANSPORT_SENSE_BUFFER);
	/*
	 * Actual SENSE DATA, see SPC-3 7.23.2  SPC_SENSE_KEY_OFFSET uses
	 * SENSE KEY values from include/scsi/scsi.h
	 */
	switch (reason) {
	case TCM_NON_EXISTENT_LUN:
		/* CURRENT ERROR */
		buffer[offset] = 0x70;
		buffer[offset+SPC_ADD_SENSE_LEN_OFFSET] = 10;
		/* ILLEGAL REQUEST */
		buffer[offset+SPC_SENSE_KEY_OFFSET] = ILLEGAL_REQUEST;
		/* LOGICAL UNIT NOT SUPPORTED */
		buffer[offset+SPC_ASC_KEY_OFFSET] = 0x25;
		break;
	case TCM_UNSUPPORTED_SCSI_OPCODE:
	case TCM_SECTOR_COUNT_TOO_MANY:
		/* CURRENT ERROR */
		buffer[offset] = 0x70;
		buffer[offset+SPC_ADD_SENSE_LEN_OFFSET] = 10;
		/* ILLEGAL REQUEST */
		buffer[offset+SPC_SENSE_KEY_OFFSET] = ILLEGAL_REQUEST;
		/* INVALID COMMAND OPERATION CODE */
		buffer[offset+SPC_ASC_KEY_OFFSET] = 0x20;
		break;
	case TCM_UNKNOWN_MODE_PAGE:
		/* CURRENT ERROR */
		buffer[offset] = 0x70;
		buffer[offset+SPC_ADD_SENSE_LEN_OFFSET] = 10;
		/* ILLEGAL REQUEST */
		buffer[offset+SPC_SENSE_KEY_OFFSET] = ILLEGAL_REQUEST;
		/* INVALID FIELD IN CDB */
		buffer[offset+SPC_ASC_KEY_OFFSET] = 0x24;
		break;
	case TCM_CHECK_CONDITION_ABORT_CMD:
		/* CURRENT ERROR */
		buffer[offset] = 0x70;
		buffer[offset+SPC_ADD_SENSE_LEN_OFFSET] = 10;
		/* ABORTED COMMAND */
		buffer[offset+SPC_SENSE_KEY_OFFSET] = ABORTED_COMMAND;
		/* BUS DEVICE RESET FUNCTION OCCURRED */
		buffer[offset+SPC_ASC_KEY_OFFSET] = 0x29;
		buffer[offset+SPC_ASCQ_KEY_OFFSET] = 0x03;
		break;
	case TCM_INCORRECT_AMOUNT_OF_DATA:
		/* CURRENT ERROR */
		buffer[offset] = 0x70;
		buffer[offset+SPC_ADD_SENSE_LEN_OFFSET] = 10;
		/* ABORTED COMMAND */
		buffer[offset+SPC_SENSE_KEY_OFFSET] = ABORTED_COMMAND;
		/* WRITE ERROR */
		buffer[offset+SPC_ASC_KEY_OFFSET] = 0x0c;
		/* NOT ENOUGH UNSOLICITED DATA */
		buffer[offset+SPC_ASCQ_KEY_OFFSET] = 0x0d;
		break;
	case TCM_INVALID_CDB_FIELD:
		/* CURRENT ERROR */
		buffer[offset] = 0x70;
		buffer[offset+SPC_ADD_SENSE_LEN_OFFSET] = 10;
		/* ILLEGAL REQUEST */
		buffer[offset+SPC_SENSE_KEY_OFFSET] = ILLEGAL_REQUEST;
		/* INVALID FIELD IN CDB */
		buffer[offset+SPC_ASC_KEY_OFFSET] = 0x24;
		break;
	case TCM_INVALID_PARAMETER_LIST:
		/* CURRENT ERROR */
		buffer[offset] = 0x70;
		buffer[offset+SPC_ADD_SENSE_LEN_OFFSET] = 10;
		/* ILLEGAL REQUEST */
		buffer[offset+SPC_SENSE_KEY_OFFSET] = ILLEGAL_REQUEST;
		/* INVALID FIELD IN PARAMETER LIST */
		buffer[offset+SPC_ASC_KEY_OFFSET] = 0x26;
		break;
	case TCM_UNEXPECTED_UNSOLICITED_DATA:
		/* CURRENT ERROR */
		buffer[offset] = 0x70;
		buffer[offset+SPC_ADD_SENSE_LEN_OFFSET] = 10;
		/* ABORTED COMMAND */
		buffer[offset+SPC_SENSE_KEY_OFFSET] = ABORTED_COMMAND;
		/* WRITE ERROR */
		buffer[offset+SPC_ASC_KEY_OFFSET] = 0x0c;
		/* UNEXPECTED_UNSOLICITED_DATA */
		buffer[offset+SPC_ASCQ_KEY_OFFSET] = 0x0c;
		break;
	case TCM_SERVICE_CRC_ERROR:
		/* CURRENT ERROR */
		buffer[offset] = 0x70;
		buffer[offset+SPC_ADD_SENSE_LEN_OFFSET] = 10;
		/* ABORTED COMMAND */
		buffer[offset+SPC_SENSE_KEY_OFFSET] = ABORTED_COMMAND;
		/* PROTOCOL SERVICE CRC ERROR */
		buffer[offset+SPC_ASC_KEY_OFFSET] = 0x47;
		/* N/A */
		buffer[offset+SPC_ASCQ_KEY_OFFSET] = 0x05;
		break;
	case TCM_SNACK_REJECTED:
		/* CURRENT ERROR */
		buffer[offset] = 0x70;
		buffer[offset+SPC_ADD_SENSE_LEN_OFFSET] = 10;
		/* ABORTED COMMAND */
		buffer[offset+SPC_SENSE_KEY_OFFSET] = ABORTED_COMMAND;
		/* READ ERROR */
		buffer[offset+SPC_ASC_KEY_OFFSET] = 0x11;
		/* FAILED RETRANSMISSION REQUEST */
		buffer[offset+SPC_ASCQ_KEY_OFFSET] = 0x13;
		break;
	case TCM_WRITE_PROTECTED:
		/* CURRENT ERROR */
		buffer[offset] = 0x70;
		buffer[offset+SPC_ADD_SENSE_LEN_OFFSET] = 10;
		/* DATA PROTECT */
		buffer[offset+SPC_SENSE_KEY_OFFSET] = DATA_PROTECT;
		/* WRITE PROTECTED */
		buffer[offset+SPC_ASC_KEY_OFFSET] = 0x27;
		break;
	case TCM_CHECK_CONDITION_UNIT_ATTENTION:
		/* CURRENT ERROR */
		buffer[offset] = 0x70;
		buffer[offset+SPC_ADD_SENSE_LEN_OFFSET] = 10;
		/* UNIT ATTENTION */
		buffer[offset+SPC_SENSE_KEY_OFFSET] = UNIT_ATTENTION;
		core_scsi3_ua_for_check_condition(cmd, &asc, &ascq);
		buffer[offset+SPC_ASC_KEY_OFFSET] = asc;
		buffer[offset+SPC_ASCQ_KEY_OFFSET] = ascq;
		break;
	case TCM_CHECK_CONDITION_NOT_READY:
		/* CURRENT ERROR */
		buffer[offset] = 0x70;
		buffer[offset+SPC_ADD_SENSE_LEN_OFFSET] = 10;
		/* Not Ready */
		buffer[offset+SPC_SENSE_KEY_OFFSET] = NOT_READY;
		transport_get_sense_codes(cmd, &asc, &ascq);
		buffer[offset+SPC_ASC_KEY_OFFSET] = asc;
		buffer[offset+SPC_ASCQ_KEY_OFFSET] = ascq;
		break;
	case TCM_LOGICAL_UNIT_COMMUNICATION_FAILURE:
	default:
		/* CURRENT ERROR */
		buffer[offset] = 0x70;
		buffer[offset+SPC_ADD_SENSE_LEN_OFFSET] = 10;
		/* ILLEGAL REQUEST */
		buffer[offset+SPC_SENSE_KEY_OFFSET] = ILLEGAL_REQUEST;
		/* LOGICAL UNIT COMMUNICATION FAILURE */
		buffer[offset+SPC_ASC_KEY_OFFSET] = 0x80;
		break;
	}
	/*
	 * This code uses linux/include/scsi/scsi.h SAM status codes!
	 */
	cmd->scsi_status = SAM_STAT_CHECK_CONDITION;
	/*
	 * Automatically padded, this value is encoded in the fabric's
	 * data_length response PDU containing the SCSI defined sense data.
	 */
	cmd->scsi_sense_length  = TRANSPORT_SENSE_BUFFER + offset;

after_reason:
	return cmd->se_tfo->queue_status(cmd);
}
EXPORT_SYMBOL(transport_send_check_condition_and_sense);

int transport_check_aborted_status(struct se_cmd *cmd, int send_status)
{
	int ret = 0;

	if (cmd->transport_state & CMD_T_ABORTED) {
		if (!send_status ||
		     (cmd->se_cmd_flags & SCF_SENT_DELAYED_TAS))
			return 1;

		pr_debug("Sending delayed SAM_STAT_TASK_ABORTED"
			" status for CDB: 0x%02x ITT: 0x%08x\n",
			cmd->t_task_cdb[0],
			cmd->se_tfo->get_task_tag(cmd));

		cmd->se_cmd_flags |= SCF_SENT_DELAYED_TAS;
		cmd->se_tfo->queue_status(cmd);
		ret = 1;
	}
	return ret;
}
EXPORT_SYMBOL(transport_check_aborted_status);

void transport_send_task_abort(struct se_cmd *cmd)
{
	unsigned long flags;

	spin_lock_irqsave(&cmd->t_state_lock, flags);
	if (cmd->se_cmd_flags & SCF_SENT_CHECK_CONDITION) {
		spin_unlock_irqrestore(&cmd->t_state_lock, flags);
		return;
	}
	spin_unlock_irqrestore(&cmd->t_state_lock, flags);

	/*
	 * If there are still expected incoming fabric WRITEs, we wait
	 * until until they have completed before sending a TASK_ABORTED
	 * response.  This response with TASK_ABORTED status will be
	 * queued back to fabric module by transport_check_aborted_status().
	 */
	if (cmd->data_direction == DMA_TO_DEVICE) {
		if (cmd->se_tfo->write_pending_status(cmd) != 0) {
			cmd->transport_state |= CMD_T_ABORTED;
			smp_mb__after_atomic_inc();
		}
	}
	cmd->scsi_status = SAM_STAT_TASK_ABORTED;

	pr_debug("Setting SAM_STAT_TASK_ABORTED status for CDB: 0x%02x,"
		" ITT: 0x%08x\n", cmd->t_task_cdb[0],
		cmd->se_tfo->get_task_tag(cmd));

	cmd->se_tfo->queue_status(cmd);
}

static int transport_generic_do_tmr(struct se_cmd *cmd)
{
	struct se_device *dev = cmd->se_dev;
	struct se_tmr_req *tmr = cmd->se_tmr_req;
	int ret;

	switch (tmr->function) {
	case TMR_ABORT_TASK:
		core_tmr_abort_task(dev, tmr, cmd->se_sess);
		break;
	case TMR_ABORT_TASK_SET:
	case TMR_CLEAR_ACA:
	case TMR_CLEAR_TASK_SET:
		tmr->response = TMR_TASK_MGMT_FUNCTION_NOT_SUPPORTED;
		break;
	case TMR_LUN_RESET:
		ret = core_tmr_lun_reset(dev, tmr, NULL, NULL);
		tmr->response = (!ret) ? TMR_FUNCTION_COMPLETE :
					 TMR_FUNCTION_REJECTED;
		break;
	case TMR_TARGET_WARM_RESET:
		tmr->response = TMR_FUNCTION_REJECTED;
		break;
	case TMR_TARGET_COLD_RESET:
		tmr->response = TMR_FUNCTION_REJECTED;
		break;
	default:
		pr_err("Uknown TMR function: 0x%02x.\n",
				tmr->function);
		tmr->response = TMR_FUNCTION_REJECTED;
		break;
	}

	cmd->t_state = TRANSPORT_ISTATE_PROCESSING;
	cmd->se_tfo->queue_tm_rsp(cmd);

	transport_cmd_check_stop_to_fabric(cmd);
	return 0;
}

/*	transport_processing_thread():
 *
 *
 */
static int transport_processing_thread(void *param)
{
	int ret;
	struct se_cmd *cmd;
	struct se_device *dev = param;

	while (!kthread_should_stop()) {
		ret = wait_event_interruptible(dev->dev_queue_obj.thread_wq,
				atomic_read(&dev->dev_queue_obj.queue_cnt) ||
				kthread_should_stop());
		if (ret < 0)
			goto out;

get_cmd:
		cmd = transport_get_cmd_from_queue(&dev->dev_queue_obj);
		if (!cmd)
			continue;

		switch (cmd->t_state) {
		case TRANSPORT_NEW_CMD:
			BUG();
			break;
		case TRANSPORT_NEW_CMD_MAP:
			if (!cmd->se_tfo->new_cmd_map) {
				pr_err("cmd->se_tfo->new_cmd_map is"
					" NULL for TRANSPORT_NEW_CMD_MAP\n");
				BUG();
			}
			ret = cmd->se_tfo->new_cmd_map(cmd);
			if (ret < 0) {
				transport_generic_request_failure(cmd);
				break;
			}
			ret = transport_generic_new_cmd(cmd);
			if (ret < 0) {
				transport_generic_request_failure(cmd);
				break;
			}
			break;
		case TRANSPORT_PROCESS_WRITE:
			transport_generic_process_write(cmd);
			break;
		case TRANSPORT_PROCESS_TMR:
			transport_generic_do_tmr(cmd);
			break;
		case TRANSPORT_COMPLETE_QF_WP:
			transport_write_pending_qf(cmd);
			break;
		case TRANSPORT_COMPLETE_QF_OK:
			transport_complete_qf(cmd);
			break;
		default:
			pr_err("Unknown t_state: %d  for ITT: 0x%08x "
				"i_state: %d on SE LUN: %u\n",
				cmd->t_state,
				cmd->se_tfo->get_task_tag(cmd),
				cmd->se_tfo->get_cmd_state(cmd),
				cmd->se_lun->unpacked_lun);
			BUG();
		}

		goto get_cmd;
	}

out:
	WARN_ON(!list_empty(&dev->state_list));
	WARN_ON(!list_empty(&dev->dev_queue_obj.qobj_list));
	dev->process_thread = NULL;
	return 0;
}

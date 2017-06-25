/*******************************************************************************
 * Filename:  target_core_transport.c
 *
 * This file contains the Generic Target Engine Core.
 *
 * (c) Copyright 2002-2013 Datera, Inc.
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
#include <linux/spinlock.h>
#include <linux/kthread.h>
#include <linux/in.h>
#include <linux/cdrom.h>
#include <linux/module.h>
#include <linux/ratelimit.h>
#include <linux/vmalloc.h>
#include <asm/unaligned.h>
#include <net/sock.h>
#include <net/tcp.h>
#include <scsi/scsi_proto.h>
#include <scsi/scsi_common.h>

#include <target/target_core_base.h>
#include <target/target_core_backend.h>
#include <target/target_core_fabric.h>

#include "target_core_internal.h"
#include "target_core_alua.h"
#include "target_core_pr.h"
#include "target_core_ua.h"

#define CREATE_TRACE_POINTS
#include <trace/events/target.h>

static struct workqueue_struct *target_completion_wq;
static struct kmem_cache *se_sess_cache;
struct kmem_cache *se_ua_cache;
struct kmem_cache *t10_pr_reg_cache;
struct kmem_cache *t10_alua_lu_gp_cache;
struct kmem_cache *t10_alua_lu_gp_mem_cache;
struct kmem_cache *t10_alua_tg_pt_gp_cache;
struct kmem_cache *t10_alua_lba_map_cache;
struct kmem_cache *t10_alua_lba_map_mem_cache;

static void transport_complete_task_attr(struct se_cmd *cmd);
static void transport_handle_queue_full(struct se_cmd *cmd,
		struct se_device *dev);
static int transport_put_cmd(struct se_cmd *cmd);
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
	t10_alua_lba_map_cache = kmem_cache_create(
			"t10_alua_lba_map_cache",
			sizeof(struct t10_alua_lba_map),
			__alignof__(struct t10_alua_lba_map), 0, NULL);
	if (!t10_alua_lba_map_cache) {
		pr_err("kmem_cache_create() for t10_alua_lba_map_"
				"cache failed\n");
		goto out_free_tg_pt_gp_cache;
	}
	t10_alua_lba_map_mem_cache = kmem_cache_create(
			"t10_alua_lba_map_mem_cache",
			sizeof(struct t10_alua_lba_map_member),
			__alignof__(struct t10_alua_lba_map_member), 0, NULL);
	if (!t10_alua_lba_map_mem_cache) {
		pr_err("kmem_cache_create() for t10_alua_lba_map_mem_"
				"cache failed\n");
		goto out_free_lba_map_cache;
	}

	target_completion_wq = alloc_workqueue("target_completion",
					       WQ_MEM_RECLAIM, 0);
	if (!target_completion_wq)
		goto out_free_lba_map_mem_cache;

	return 0;

out_free_lba_map_mem_cache:
	kmem_cache_destroy(t10_alua_lba_map_mem_cache);
out_free_lba_map_cache:
	kmem_cache_destroy(t10_alua_lba_map_cache);
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
	kmem_cache_destroy(t10_alua_lba_map_cache);
	kmem_cache_destroy(t10_alua_lba_map_mem_cache);
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

void transport_subsystem_check_init(void)
{
	int ret;
	static int sub_api_initialized;

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

	ret = request_module("target_core_user");
	if (ret != 0)
		pr_err("Unable to load target_core_user\n");

	sub_api_initialized = 1;
}

struct se_session *transport_init_session(enum target_prot_op sup_prot_ops)
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
	se_sess->sup_prot_ops = sup_prot_ops;

	return se_sess;
}
EXPORT_SYMBOL(transport_init_session);

int transport_alloc_session_tags(struct se_session *se_sess,
			         unsigned int tag_num, unsigned int tag_size)
{
	int rc;

	se_sess->sess_cmd_map = kzalloc(tag_num * tag_size,
					GFP_KERNEL | __GFP_NOWARN | __GFP_REPEAT);
	if (!se_sess->sess_cmd_map) {
		se_sess->sess_cmd_map = vzalloc(tag_num * tag_size);
		if (!se_sess->sess_cmd_map) {
			pr_err("Unable to allocate se_sess->sess_cmd_map\n");
			return -ENOMEM;
		}
	}

	rc = percpu_ida_init(&se_sess->sess_tag_pool, tag_num);
	if (rc < 0) {
		pr_err("Unable to init se_sess->sess_tag_pool,"
			" tag_num: %u\n", tag_num);
		kvfree(se_sess->sess_cmd_map);
		se_sess->sess_cmd_map = NULL;
		return -ENOMEM;
	}

	return 0;
}
EXPORT_SYMBOL(transport_alloc_session_tags);

struct se_session *transport_init_session_tags(unsigned int tag_num,
					       unsigned int tag_size,
					       enum target_prot_op sup_prot_ops)
{
	struct se_session *se_sess;
	int rc;

	if (tag_num != 0 && !tag_size) {
		pr_err("init_session_tags called with percpu-ida tag_num:"
		       " %u, but zero tag_size\n", tag_num);
		return ERR_PTR(-EINVAL);
	}
	if (!tag_num && tag_size) {
		pr_err("init_session_tags called with percpu-ida tag_size:"
		       " %u, but zero tag_num\n", tag_size);
		return ERR_PTR(-EINVAL);
	}

	se_sess = transport_init_session(sup_prot_ops);
	if (IS_ERR(se_sess))
		return se_sess;

	rc = transport_alloc_session_tags(se_sess, tag_num, tag_size);
	if (rc < 0) {
		transport_free_session(se_sess);
		return ERR_PTR(-ENOMEM);
	}

	return se_sess;
}
EXPORT_SYMBOL(transport_init_session_tags);

/*
 * Called with spin_lock_irqsave(&struct se_portal_group->session_lock called.
 */
void __transport_register_session(
	struct se_portal_group *se_tpg,
	struct se_node_acl *se_nacl,
	struct se_session *se_sess,
	void *fabric_sess_ptr)
{
	const struct target_core_fabric_ops *tfo = se_tpg->se_tpg_tfo;
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
		 *
		 * Determine if fabric allows for T10-PI feature bits exposed to
		 * initiators for device backends with !dev->dev_attrib.pi_prot_type.
		 *
		 * If so, then always save prot_type on a per se_node_acl node
		 * basis and re-instate the previous sess_prot_type to avoid
		 * disabling PI from below any previously initiator side
		 * registered LUNs.
		 */
		if (se_nacl->saved_prot_type)
			se_sess->sess_prot_type = se_nacl->saved_prot_type;
		else if (tfo->tpg_check_prot_fabric_only)
			se_sess->sess_prot_type = se_nacl->saved_prot_type =
					tfo->tpg_check_prot_fabric_only(se_tpg);
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

struct se_session *
target_alloc_session(struct se_portal_group *tpg,
		     unsigned int tag_num, unsigned int tag_size,
		     enum target_prot_op prot_op,
		     const char *initiatorname, void *private,
		     int (*callback)(struct se_portal_group *,
				     struct se_session *, void *))
{
	struct se_session *sess;

	/*
	 * If the fabric driver is using percpu-ida based pre allocation
	 * of I/O descriptor tags, go ahead and perform that setup now..
	 */
	if (tag_num != 0)
		sess = transport_init_session_tags(tag_num, tag_size, prot_op);
	else
		sess = transport_init_session(prot_op);

	if (IS_ERR(sess))
		return sess;

	sess->se_node_acl = core_tpg_check_initiator_node_acl(tpg,
					(unsigned char *)initiatorname);
	if (!sess->se_node_acl) {
		transport_free_session(sess);
		return ERR_PTR(-EACCES);
	}
	/*
	 * Go ahead and perform any remaining fabric setup that is
	 * required before transport_register_session().
	 */
	if (callback != NULL) {
		int rc = callback(tpg, sess, private);
		if (rc) {
			transport_free_session(sess);
			return ERR_PTR(rc);
		}
	}

	transport_register_session(tpg, sess->se_node_acl, sess, private);
	return sess;
}
EXPORT_SYMBOL(target_alloc_session);

ssize_t target_show_dynamic_sessions(struct se_portal_group *se_tpg, char *page)
{
	struct se_session *se_sess;
	ssize_t len = 0;

	spin_lock_bh(&se_tpg->session_lock);
	list_for_each_entry(se_sess, &se_tpg->tpg_sess_list, sess_list) {
		if (!se_sess->se_node_acl)
			continue;
		if (!se_sess->se_node_acl->dynamic_node_acl)
			continue;
		if (strlen(se_sess->se_node_acl->initiatorname) + 1 + len > PAGE_SIZE)
			break;

		len += snprintf(page + len, PAGE_SIZE - len, "%s\n",
				se_sess->se_node_acl->initiatorname);
		len += 1; /* Include NULL terminator */
	}
	spin_unlock_bh(&se_tpg->session_lock);

	return len;
}
EXPORT_SYMBOL(target_show_dynamic_sessions);

static void target_complete_nacl(struct kref *kref)
{
	struct se_node_acl *nacl = container_of(kref,
				struct se_node_acl, acl_kref);
	struct se_portal_group *se_tpg = nacl->se_tpg;

	if (!nacl->dynamic_stop) {
		complete(&nacl->acl_free_comp);
		return;
	}

	mutex_lock(&se_tpg->acl_node_mutex);
	list_del(&nacl->acl_list);
	mutex_unlock(&se_tpg->acl_node_mutex);

	core_tpg_wait_for_nacl_pr_ref(nacl);
	core_free_device_list_for_node(nacl, se_tpg);
	kfree(nacl);
}

void target_put_nacl(struct se_node_acl *nacl)
{
	kref_put(&nacl->acl_kref, target_complete_nacl);
}
EXPORT_SYMBOL(target_put_nacl);

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
		if (!list_empty(&se_sess->sess_acl_list))
			list_del_init(&se_sess->sess_acl_list);
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
	struct se_node_acl *se_nacl = se_sess->se_node_acl;

	/*
	 * Drop the se_node_acl->nacl_kref obtained from within
	 * core_tpg_get_initiator_node_acl().
	 */
	if (se_nacl) {
		struct se_portal_group *se_tpg = se_nacl->se_tpg;
		const struct target_core_fabric_ops *se_tfo = se_tpg->se_tpg_tfo;
		unsigned long flags;

		se_sess->se_node_acl = NULL;

		/*
		 * Also determine if we need to drop the extra ->cmd_kref if
		 * it had been previously dynamically generated, and
		 * the endpoint is not caching dynamic ACLs.
		 */
		mutex_lock(&se_tpg->acl_node_mutex);
		if (se_nacl->dynamic_node_acl &&
		    !se_tfo->tpg_check_demo_mode_cache(se_tpg)) {
			spin_lock_irqsave(&se_nacl->nacl_sess_lock, flags);
			if (list_empty(&se_nacl->acl_sess_list))
				se_nacl->dynamic_stop = true;
			spin_unlock_irqrestore(&se_nacl->nacl_sess_lock, flags);

			if (se_nacl->dynamic_stop)
				list_del(&se_nacl->acl_list);
		}
		mutex_unlock(&se_tpg->acl_node_mutex);

		if (se_nacl->dynamic_stop)
			target_put_nacl(se_nacl);

		target_put_nacl(se_nacl);
	}
	if (se_sess->sess_cmd_map) {
		percpu_ida_destroy(&se_sess->sess_tag_pool);
		kvfree(se_sess->sess_cmd_map);
	}
	kmem_cache_free(se_sess_cache, se_sess);
}
EXPORT_SYMBOL(transport_free_session);

void transport_deregister_session(struct se_session *se_sess)
{
	struct se_portal_group *se_tpg = se_sess->se_tpg;
	unsigned long flags;

	if (!se_tpg) {
		transport_free_session(se_sess);
		return;
	}

	spin_lock_irqsave(&se_tpg->session_lock, flags);
	list_del(&se_sess->sess_list);
	se_sess->se_tpg = NULL;
	se_sess->fabric_sess_ptr = NULL;
	spin_unlock_irqrestore(&se_tpg->session_lock, flags);

	pr_debug("TARGET_CORE[%s]: Deregistered fabric_sess\n",
		se_tpg->se_tpg_tfo->get_fabric_name());
	/*
	 * If last kref is dropping now for an explicit NodeACL, awake sleeping
	 * ->acl_free_comp caller to wakeup configfs se_node_acl->acl_group
	 * removal context from within transport_free_session() code.
	 *
	 * For dynamic ACL, target_put_nacl() uses target_complete_nacl()
	 * to release all remaining generate_node_acl=1 created ACL resources.
	 */

	transport_free_session(se_sess);
}
EXPORT_SYMBOL(transport_deregister_session);

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

static int transport_cmd_check_stop(struct se_cmd *cmd, bool remove_from_lists,
				    bool write_pending)
{
	unsigned long flags;

	if (remove_from_lists) {
		target_remove_from_state_list(cmd);

		/*
		 * Clear struct se_cmd->se_lun before the handoff to FE.
		 */
		cmd->se_lun = NULL;
	}

	spin_lock_irqsave(&cmd->t_state_lock, flags);
	if (write_pending)
		cmd->t_state = TRANSPORT_WRITE_PENDING;

	/*
	 * Determine if frontend context caller is requesting the stopping of
	 * this command for frontend exceptions.
	 */
	if (cmd->transport_state & CMD_T_STOP) {
		pr_debug("%s:%d CMD_T_STOP for ITT: 0x%08llx\n",
			__func__, __LINE__, cmd->tag);

		spin_unlock_irqrestore(&cmd->t_state_lock, flags);

		complete_all(&cmd->t_transport_stop_comp);
		return 1;
	}

	cmd->transport_state &= ~CMD_T_ACTIVE;
	if (remove_from_lists) {
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
			spin_unlock_irqrestore(&cmd->t_state_lock, flags);
			return cmd->se_tfo->check_stop_free(cmd);
		}
	}

	spin_unlock_irqrestore(&cmd->t_state_lock, flags);
	return 0;
}

static int transport_cmd_check_stop_to_fabric(struct se_cmd *cmd)
{
	return transport_cmd_check_stop(cmd, true, false);
}

static void transport_lun_remove_cmd(struct se_cmd *cmd)
{
	struct se_lun *lun = cmd->se_lun;

	if (!lun)
		return;

	if (cmpxchg(&cmd->lun_ref_active, true, false))
		percpu_ref_put(&lun->lun_ref);
}

int transport_cmd_finish_abort(struct se_cmd *cmd, int remove)
{
	bool ack_kref = (cmd->se_cmd_flags & SCF_ACK_KREF);
	int ret = 0;

	if (cmd->se_cmd_flags & SCF_SE_LUN_CMD)
		transport_lun_remove_cmd(cmd);
	/*
	 * Allow the fabric driver to unmap any resources before
	 * releasing the descriptor via TFO->release_cmd()
	 */
	if (remove)
		cmd->se_tfo->aborted_task(cmd);

	if (transport_cmd_check_stop_to_fabric(cmd))
		return 1;
	if (remove && ack_kref)
		ret = transport_put_cmd(cmd);

	return ret;
}

static void target_complete_failure_work(struct work_struct *work)
{
	struct se_cmd *cmd = container_of(work, struct se_cmd, work);

	transport_generic_request_failure(cmd,
			TCM_LOGICAL_UNIT_COMMUNICATION_FAILURE);
}

/*
 * Used when asking transport to copy Sense Data from the underlying
 * Linux/SCSI struct scsi_cmnd
 */
static unsigned char *transport_get_sense_buffer(struct se_cmd *cmd)
{
	struct se_device *dev = cmd->se_dev;

	WARN_ON(!cmd->se_lun);

	if (!dev)
		return NULL;

	if (cmd->se_cmd_flags & SCF_SENT_CHECK_CONDITION)
		return NULL;

	cmd->scsi_sense_length = TRANSPORT_SENSE_BUFFER;

	pr_debug("HBA_[%u]_PLUG[%s]: Requesting sense for SAM STATUS: 0x%02x\n",
		dev->se_hba->hba_id, dev->transport->name, cmd->scsi_status);
	return cmd->sense_buffer;
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
		dev->transport->transport_complete(cmd,
				cmd->t_data_sg,
				transport_get_sense_buffer(cmd));
		if (cmd->se_cmd_flags & SCF_TRANSPORT_TASK_SENSE)
			success = 1;
	}

	/*
	 * Check for case where an explicit ABORT_TASK has been received
	 * and transport_wait_for_tasks() will be waiting for completion..
	 */
	if (cmd->transport_state & CMD_T_ABORTED ||
	    cmd->transport_state & CMD_T_STOP) {
		spin_unlock_irqrestore(&cmd->t_state_lock, flags);
		/*
		 * If COMPARE_AND_WRITE was stopped by __transport_wait_for_tasks(),
		 * release se_device->caw_sem obtained by sbc_compare_and_write()
		 * since target_complete_ok_work() or target_complete_failure_work()
		 * won't be called to invoke the normal CAW completion callbacks.
		 */
		if (cmd->se_cmd_flags & SCF_COMPARE_AND_WRITE) {
			up(&dev->caw_sem);
		}
		complete_all(&cmd->t_transport_stop_comp);
		return;
	} else if (!success) {
		INIT_WORK(&cmd->work, target_complete_failure_work);
	} else {
		INIT_WORK(&cmd->work, target_complete_ok_work);
	}

	cmd->t_state = TRANSPORT_COMPLETE;
	cmd->transport_state |= (CMD_T_COMPLETE | CMD_T_ACTIVE);
	spin_unlock_irqrestore(&cmd->t_state_lock, flags);

	if (cmd->se_cmd_flags & SCF_USE_CPUID)
		queue_work_on(cmd->cpuid, target_completion_wq, &cmd->work);
	else
		queue_work(target_completion_wq, &cmd->work);
}
EXPORT_SYMBOL(target_complete_cmd);

void target_complete_cmd_with_length(struct se_cmd *cmd, u8 scsi_status, int length)
{
	if (scsi_status == SAM_STAT_GOOD && length < cmd->data_length) {
		if (cmd->se_cmd_flags & SCF_UNDERFLOW_BIT) {
			cmd->residual_count += cmd->data_length - length;
		} else {
			cmd->se_cmd_flags |= SCF_UNDERFLOW_BIT;
			cmd->residual_count = cmd->data_length - length;
		}

		cmd->data_length = length;
	}

	target_complete_cmd(cmd, scsi_status);
}
EXPORT_SYMBOL(target_complete_cmd_with_length);

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

/*
 * Handle QUEUE_FULL / -EAGAIN and -ENOMEM status
 */
static void transport_write_pending_qf(struct se_cmd *cmd);
static void transport_complete_qf(struct se_cmd *cmd);

void target_qf_do_work(struct work_struct *work)
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
		atomic_dec_mb(&dev->dev_qf_count);

		pr_debug("Processing %s cmd: %p QUEUE_FULL in work queue"
			" context: %s\n", cmd->se_tfo->get_fabric_name(), cmd,
			(cmd->t_state == TRANSPORT_COMPLETE_QF_OK) ? "COMPLETE_OK" :
			(cmd->t_state == TRANSPORT_COMPLETE_QF_WP) ? "WRITE_PENDING"
			: "UNKNOWN");

		if (cmd->t_state == TRANSPORT_COMPLETE_QF_WP)
			transport_write_pending_qf(cmd);
		else if (cmd->t_state == TRANSPORT_COMPLETE_QF_OK)
			transport_complete_qf(cmd);
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
	if (dev->export_count)
		*bl += sprintf(b + *bl, "ACTIVATED");
	else
		*bl += sprintf(b + *bl, "DEACTIVATED");

	*bl += sprintf(b + *bl, "  Max Queue Depth: %d", dev->queue_depth);
	*bl += sprintf(b + *bl, "  SectorSize: %u  HwMaxSectors: %u\n",
		dev->dev_attrib.block_size,
		dev->dev_attrib.hw_max_sectors);
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
		snprintf(buf, sizeof(buf),
			"T10 VPD Binary Device Identifier: %s\n",
			&vpd->device_identifier[0]);
		break;
	case 0x02: /* ASCII */
		snprintf(buf, sizeof(buf),
			"T10 VPD ASCII Device Identifier: %s\n",
			&vpd->device_identifier[0]);
		break;
	case 0x03: /* UTF-8 */
		snprintf(buf, sizeof(buf),
			"T10 VPD UTF-8 Device Identifier: %s\n",
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
	int j = 0, i = 4; /* offset to start of the identifier */

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

static sense_reason_t
target_check_max_data_sg_nents(struct se_cmd *cmd, struct se_device *dev,
			       unsigned int size)
{
	u32 mtl;

	if (!cmd->se_tfo->max_data_sg_nents)
		return TCM_NO_SENSE;
	/*
	 * Check if fabric enforced maximum SGL entries per I/O descriptor
	 * exceeds se_cmd->data_length.  If true, set SCF_UNDERFLOW_BIT +
	 * residual_count and reduce original cmd->data_length to maximum
	 * length based on single PAGE_SIZE entry scatter-lists.
	 */
	mtl = (cmd->se_tfo->max_data_sg_nents * PAGE_SIZE);
	if (cmd->data_length > mtl) {
		/*
		 * If an existing CDB overflow is present, calculate new residual
		 * based on CDB size minus fabric maximum transfer length.
		 *
		 * If an existing CDB underflow is present, calculate new residual
		 * based on original cmd->data_length minus fabric maximum transfer
		 * length.
		 *
		 * Otherwise, set the underflow residual based on cmd->data_length
		 * minus fabric maximum transfer length.
		 */
		if (cmd->se_cmd_flags & SCF_OVERFLOW_BIT) {
			cmd->residual_count = (size - mtl);
		} else if (cmd->se_cmd_flags & SCF_UNDERFLOW_BIT) {
			u32 orig_dl = size + cmd->residual_count;
			cmd->residual_count = (orig_dl - mtl);
		} else {
			cmd->se_cmd_flags |= SCF_UNDERFLOW_BIT;
			cmd->residual_count = (cmd->data_length - mtl);
		}
		cmd->data_length = mtl;
		/*
		 * Reset sbc_check_prot() calculated protection payload
		 * length based upon the new smaller MTL.
		 */
		if (cmd->prot_length) {
			u32 sectors = (mtl / dev->dev_attrib.block_size);
			cmd->prot_length = dev->prot_length * sectors;
		}
	}
	return TCM_NO_SENSE;
}

sense_reason_t
target_cmd_size_check(struct se_cmd *cmd, unsigned int size)
{
	struct se_device *dev = cmd->se_dev;

	if (cmd->unknown_data_length) {
		cmd->data_length = size;
	} else if (size != cmd->data_length) {
		pr_warn_ratelimited("TARGET_CORE[%s]: Expected Transfer Length:"
			" %u does not match SCSI CDB Length: %u for SAM Opcode:"
			" 0x%02x\n", cmd->se_tfo->get_fabric_name(),
				cmd->data_length, size, cmd->t_task_cdb[0]);

		if (cmd->data_direction == DMA_TO_DEVICE) {
			if (cmd->se_cmd_flags & SCF_SCSI_DATA_CDB) {
				pr_err_ratelimited("Rejecting underflow/overflow"
						   " for WRITE data CDB\n");
				return TCM_INVALID_CDB_FIELD;
			}
			/*
			 * Some fabric drivers like iscsi-target still expect to
			 * always reject overflow writes.  Reject this case until
			 * full fabric driver level support for overflow writes
			 * is introduced tree-wide.
			 */
			if (size > cmd->data_length) {
				pr_err_ratelimited("Rejecting overflow for"
						   " WRITE control CDB\n");
				return TCM_INVALID_CDB_FIELD;
			}
		}
		/*
		 * Reject READ_* or WRITE_* with overflow/underflow for
		 * type SCF_SCSI_DATA_CDB.
		 */
		if (dev->dev_attrib.block_size != 512)  {
			pr_err("Failing OVERFLOW/UNDERFLOW for LBA op"
				" CDB on non 512-byte sector setup subsystem"
				" plugin: %s\n", dev->transport->name);
			/* Returns CHECK_CONDITION + INVALID_CDB_FIELD */
			return TCM_INVALID_CDB_FIELD;
		}
		/*
		 * For the overflow case keep the existing fabric provided
		 * ->data_length.  Otherwise for the underflow case, reset
		 * ->data_length to the smaller SCSI expected data transfer
		 * length.
		 */
		if (size > cmd->data_length) {
			cmd->se_cmd_flags |= SCF_OVERFLOW_BIT;
			cmd->residual_count = (size - cmd->data_length);
		} else {
			cmd->se_cmd_flags |= SCF_UNDERFLOW_BIT;
			cmd->residual_count = (cmd->data_length - size);
			cmd->data_length = size;
		}
	}

	return target_check_max_data_sg_nents(cmd, dev, size);

}

/*
 * Used by fabric modules containing a local struct se_cmd within their
 * fabric dependent per I/O descriptor.
 *
 * Preserves the value of @cmd->tag.
 */
void transport_init_se_cmd(
	struct se_cmd *cmd,
	const struct target_core_fabric_ops *tfo,
	struct se_session *se_sess,
	u32 data_length,
	int data_direction,
	int task_attr,
	unsigned char *sense_buffer)
{
	INIT_LIST_HEAD(&cmd->se_delayed_node);
	INIT_LIST_HEAD(&cmd->se_qf_node);
	INIT_LIST_HEAD(&cmd->se_cmd_list);
	INIT_LIST_HEAD(&cmd->state_list);
	init_completion(&cmd->t_transport_stop_comp);
	init_completion(&cmd->cmd_wait_comp);
	spin_lock_init(&cmd->t_state_lock);
	kref_init(&cmd->cmd_kref);
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

static sense_reason_t
transport_check_alloc_task_attr(struct se_cmd *cmd)
{
	struct se_device *dev = cmd->se_dev;

	/*
	 * Check if SAM Task Attribute emulation is enabled for this
	 * struct se_device storage object
	 */
	if (dev->transport->transport_flags & TRANSPORT_FLAG_PASSTHROUGH)
		return 0;

	if (cmd->sam_task_attr == TCM_ACA_TAG) {
		pr_debug("SAM Task Attribute ACA"
			" emulation is not supported\n");
		return TCM_INVALID_CDB_FIELD;
	}

	return 0;
}

sense_reason_t
target_setup_cmd_from_cdb(struct se_cmd *cmd, unsigned char *cdb)
{
	struct se_device *dev = cmd->se_dev;
	sense_reason_t ret;

	/*
	 * Ensure that the received CDB is less than the max (252 + 8) bytes
	 * for VARIABLE_LENGTH_CMD
	 */
	if (scsi_command_size(cdb) > SCSI_MAX_VARLEN_CDB_SIZE) {
		pr_err("Received SCSI CDB with command_size: %d that"
			" exceeds SCSI_MAX_VARLEN_CDB_SIZE: %d\n",
			scsi_command_size(cdb), SCSI_MAX_VARLEN_CDB_SIZE);
		return TCM_INVALID_CDB_FIELD;
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
			return TCM_OUT_OF_RESOURCES;
		}
	} else
		cmd->t_task_cdb = &cmd->__t_task_cdb[0];
	/*
	 * Copy the original CDB into cmd->
	 */
	memcpy(cmd->t_task_cdb, cdb, scsi_command_size(cdb));

	trace_target_sequencer_start(cmd);

	ret = dev->transport->parse_cdb(cmd);
	if (ret == TCM_UNSUPPORTED_SCSI_OPCODE)
		pr_warn_ratelimited("%s/%s: Unsupported SCSI Opcode 0x%02x, sending CHECK_CONDITION.\n",
				    cmd->se_tfo->get_fabric_name(),
				    cmd->se_sess->se_node_acl->initiatorname,
				    cmd->t_task_cdb[0]);
	if (ret)
		return ret;

	ret = transport_check_alloc_task_attr(cmd);
	if (ret)
		return ret;

	cmd->se_cmd_flags |= SCF_SUPPORTED_SAM_OPCODE;
	atomic_long_inc(&cmd->se_lun->lun_stats.cmd_pdus);
	return 0;
}
EXPORT_SYMBOL(target_setup_cmd_from_cdb);

/*
 * Used by fabric module frontends to queue tasks directly.
 * May only be used from process context.
 */
int transport_handle_cdb_direct(
	struct se_cmd *cmd)
{
	sense_reason_t ret;

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
	 * Set TRANSPORT_NEW_CMD state and CMD_T_ACTIVE to ensure that
	 * outstanding descriptors are handled correctly during shutdown via
	 * transport_wait_for_tasks()
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
	if (ret)
		transport_generic_request_failure(cmd, ret);
	return 0;
}
EXPORT_SYMBOL(transport_handle_cdb_direct);

sense_reason_t
transport_generic_map_mem_to_cmd(struct se_cmd *cmd, struct scatterlist *sgl,
		u32 sgl_count, struct scatterlist *sgl_bidi, u32 sgl_bidi_count)
{
	if (!sgl || !sgl_count)
		return 0;

	/*
	 * Reject SCSI data overflow with map_mem_to_cmd() as incoming
	 * scatterlists already have been set to follow what the fabric
	 * passes for the original expected data transfer length.
	 */
	if (cmd->se_cmd_flags & SCF_OVERFLOW_BIT) {
		pr_warn("Rejecting SCSI DATA overflow for fabric using"
			" SCF_PASSTHROUGH_SG_TO_MEM_NOALLOC\n");
		return TCM_INVALID_CDB_FIELD;
	}

	cmd->t_data_sg = sgl;
	cmd->t_data_nents = sgl_count;
	cmd->t_bidi_data_sg = sgl_bidi;
	cmd->t_bidi_data_nents = sgl_bidi_count;

	cmd->se_cmd_flags |= SCF_PASSTHROUGH_SG_TO_MEM_NOALLOC;
	return 0;
}

/*
 * target_submit_cmd_map_sgls - lookup unpacked lun and submit uninitialized
 * 			 se_cmd + use pre-allocated SGL memory.
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
 * @sgl: struct scatterlist memory for unidirectional mapping
 * @sgl_count: scatterlist count for unidirectional mapping
 * @sgl_bidi: struct scatterlist memory for bidirectional READ mapping
 * @sgl_bidi_count: scatterlist count for bidirectional READ mapping
 * @sgl_prot: struct scatterlist memory protection information
 * @sgl_prot_count: scatterlist count for protection information
 *
 * Task tags are supported if the caller has set @se_cmd->tag.
 *
 * Returns non zero to signal active I/O shutdown failure.  All other
 * setup exceptions will be returned as a SCSI CHECK_CONDITION response,
 * but still return zero here.
 *
 * This may only be called from process context, and also currently
 * assumes internal allocation of fabric payload buffer by target-core.
 */
int target_submit_cmd_map_sgls(struct se_cmd *se_cmd, struct se_session *se_sess,
		unsigned char *cdb, unsigned char *sense, u64 unpacked_lun,
		u32 data_length, int task_attr, int data_dir, int flags,
		struct scatterlist *sgl, u32 sgl_count,
		struct scatterlist *sgl_bidi, u32 sgl_bidi_count,
		struct scatterlist *sgl_prot, u32 sgl_prot_count)
{
	struct se_portal_group *se_tpg;
	sense_reason_t rc;
	int ret;

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

	if (flags & TARGET_SCF_USE_CPUID)
		se_cmd->se_cmd_flags |= SCF_USE_CPUID;
	else
		se_cmd->cpuid = WORK_CPU_UNBOUND;

	if (flags & TARGET_SCF_UNKNOWN_SIZE)
		se_cmd->unknown_data_length = 1;
	/*
	 * Obtain struct se_cmd->cmd_kref reference and add new cmd to
	 * se_sess->sess_cmd_list.  A second kref_get here is necessary
	 * for fabrics using TARGET_SCF_ACK_KREF that expect a second
	 * kref_put() to happen during fabric packet acknowledgement.
	 */
	ret = target_get_sess_cmd(se_cmd, flags & TARGET_SCF_ACK_KREF);
	if (ret)
		return ret;
	/*
	 * Signal bidirectional data payloads to target-core
	 */
	if (flags & TARGET_SCF_BIDI_OP)
		se_cmd->se_cmd_flags |= SCF_BIDI;
	/*
	 * Locate se_lun pointer and attach it to struct se_cmd
	 */
	rc = transport_lookup_cmd_lun(se_cmd, unpacked_lun);
	if (rc) {
		transport_send_check_condition_and_sense(se_cmd, rc, 0);
		target_put_sess_cmd(se_cmd);
		return 0;
	}

	rc = target_setup_cmd_from_cdb(se_cmd, cdb);
	if (rc != 0) {
		transport_generic_request_failure(se_cmd, rc);
		return 0;
	}

	/*
	 * Save pointers for SGLs containing protection information,
	 * if present.
	 */
	if (sgl_prot_count) {
		se_cmd->t_prot_sg = sgl_prot;
		se_cmd->t_prot_nents = sgl_prot_count;
		se_cmd->se_cmd_flags |= SCF_PASSTHROUGH_PROT_SG_TO_MEM_NOALLOC;
	}

	/*
	 * When a non zero sgl_count has been passed perform SGL passthrough
	 * mapping for pre-allocated fabric memory instead of having target
	 * core perform an internal SGL allocation..
	 */
	if (sgl_count != 0) {
		BUG_ON(!sgl);

		/*
		 * A work-around for tcm_loop as some userspace code via
		 * scsi-generic do not memset their associated read buffers,
		 * so go ahead and do that here for type non-data CDBs.  Also
		 * note that this is currently guaranteed to be a single SGL
		 * for this case by target core in target_setup_cmd_from_cdb()
		 * -> transport_generic_cmd_sequencer().
		 */
		if (!(se_cmd->se_cmd_flags & SCF_SCSI_DATA_CDB) &&
		     se_cmd->data_direction == DMA_FROM_DEVICE) {
			unsigned char *buf = NULL;

			if (sgl)
				buf = kmap(sg_page(sgl)) + sgl->offset;

			if (buf) {
				memset(buf, 0, sgl->length);
				kunmap(sg_page(sgl));
			}
		}

		rc = transport_generic_map_mem_to_cmd(se_cmd, sgl, sgl_count,
				sgl_bidi, sgl_bidi_count);
		if (rc != 0) {
			transport_generic_request_failure(se_cmd, rc);
			return 0;
		}
	}

	/*
	 * Check if we need to delay processing because of ALUA
	 * Active/NonOptimized primary access state..
	 */
	core_alua_check_nonop_delay(se_cmd);

	transport_handle_cdb_direct(se_cmd);
	return 0;
}
EXPORT_SYMBOL(target_submit_cmd_map_sgls);

/*
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
 * Task tags are supported if the caller has set @se_cmd->tag.
 *
 * Returns non zero to signal active I/O shutdown failure.  All other
 * setup exceptions will be returned as a SCSI CHECK_CONDITION response,
 * but still return zero here.
 *
 * This may only be called from process context, and also currently
 * assumes internal allocation of fabric payload buffer by target-core.
 *
 * It also assumes interal target core SGL memory allocation.
 */
int target_submit_cmd(struct se_cmd *se_cmd, struct se_session *se_sess,
		unsigned char *cdb, unsigned char *sense, u64 unpacked_lun,
		u32 data_length, int task_attr, int data_dir, int flags)
{
	return target_submit_cmd_map_sgls(se_cmd, se_sess, cdb, sense,
			unpacked_lun, data_length, task_attr, data_dir,
			flags, NULL, 0, NULL, 0, NULL, 0);
}
EXPORT_SYMBOL(target_submit_cmd);

static void target_complete_tmr_failure(struct work_struct *work)
{
	struct se_cmd *se_cmd = container_of(work, struct se_cmd, work);

	se_cmd->se_tmr_req->response = TMR_LUN_DOES_NOT_EXIST;
	se_cmd->se_tfo->queue_tm_rsp(se_cmd);

	transport_cmd_check_stop_to_fabric(se_cmd);
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
		unsigned char *sense, u64 unpacked_lun,
		void *fabric_tmr_ptr, unsigned char tm_type,
		gfp_t gfp, u64 tag, int flags)
{
	struct se_portal_group *se_tpg;
	int ret;

	se_tpg = se_sess->se_tpg;
	BUG_ON(!se_tpg);

	transport_init_se_cmd(se_cmd, se_tpg->se_tpg_tfo, se_sess,
			      0, DMA_NONE, TCM_SIMPLE_TAG, sense);
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
	ret = target_get_sess_cmd(se_cmd, flags & TARGET_SCF_ACK_KREF);
	if (ret) {
		core_tmr_release_req(se_cmd->se_tmr_req);
		return ret;
	}

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
 * Handle SAM-esque emulation for generic transport request failures.
 */
void transport_generic_request_failure(struct se_cmd *cmd,
		sense_reason_t sense_reason)
{
	int ret = 0, post_ret = 0;

	pr_debug("-----[ Storage Engine Exception for cmd: %p ITT: 0x%08llx"
		" CDB: 0x%02x\n", cmd, cmd->tag, cmd->t_task_cdb[0]);
	pr_debug("-----[ i_state: %d t_state: %d sense_reason: %d\n",
		cmd->se_tfo->get_cmd_state(cmd),
		cmd->t_state, sense_reason);
	pr_debug("-----[ CMD_T_ACTIVE: %d CMD_T_STOP: %d CMD_T_SENT: %d\n",
		(cmd->transport_state & CMD_T_ACTIVE) != 0,
		(cmd->transport_state & CMD_T_STOP) != 0,
		(cmd->transport_state & CMD_T_SENT) != 0);

	/*
	 * For SAM Task Attribute emulation for failed struct se_cmd
	 */
	transport_complete_task_attr(cmd);
	/*
	 * Handle special case for COMPARE_AND_WRITE failure, where the
	 * callback is expected to drop the per device ->caw_sem.
	 */
	if ((cmd->se_cmd_flags & SCF_COMPARE_AND_WRITE) &&
	     cmd->transport_complete_callback)
		cmd->transport_complete_callback(cmd, false, &post_ret);

	switch (sense_reason) {
	case TCM_NON_EXISTENT_LUN:
	case TCM_UNSUPPORTED_SCSI_OPCODE:
	case TCM_INVALID_CDB_FIELD:
	case TCM_INVALID_PARAMETER_LIST:
	case TCM_PARAMETER_LIST_LENGTH_ERROR:
	case TCM_LOGICAL_UNIT_COMMUNICATION_FAILURE:
	case TCM_UNKNOWN_MODE_PAGE:
	case TCM_WRITE_PROTECTED:
	case TCM_ADDRESS_OUT_OF_RANGE:
	case TCM_CHECK_CONDITION_ABORT_CMD:
	case TCM_CHECK_CONDITION_UNIT_ATTENTION:
	case TCM_CHECK_CONDITION_NOT_READY:
	case TCM_LOGICAL_BLOCK_GUARD_CHECK_FAILED:
	case TCM_LOGICAL_BLOCK_APP_TAG_CHECK_FAILED:
	case TCM_LOGICAL_BLOCK_REF_TAG_CHECK_FAILED:
	case TCM_COPY_TARGET_DEVICE_NOT_REACHABLE:
		break;
	case TCM_OUT_OF_RESOURCES:
		sense_reason = TCM_LOGICAL_UNIT_COMMUNICATION_FAILURE;
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
		    cmd->se_dev->dev_attrib.emulate_ua_intlck_ctrl == 2) {
			target_ua_allocate_lun(cmd->se_sess->se_node_acl,
					       cmd->orig_fe_lun, 0x2C,
					ASCQ_2CH_PREVIOUS_RESERVATION_CONFLICT_STATUS);
		}
		trace_target_cmd_complete(cmd);
		ret = cmd->se_tfo->queue_status(cmd);
		if (ret == -EAGAIN || ret == -ENOMEM)
			goto queue_full;
		goto check_stop;
	default:
		pr_err("Unknown transport error for CDB 0x%02x: %d\n",
			cmd->t_task_cdb[0], sense_reason);
		sense_reason = TCM_UNSUPPORTED_SCSI_OPCODE;
		break;
	}

	ret = transport_send_check_condition_and_sense(cmd, sense_reason, 0);
	if (ret == -EAGAIN || ret == -ENOMEM)
		goto queue_full;

check_stop:
	transport_lun_remove_cmd(cmd);
	transport_cmd_check_stop_to_fabric(cmd);
	return;

queue_full:
	cmd->t_state = TRANSPORT_COMPLETE_QF_OK;
	transport_handle_queue_full(cmd, cmd->se_dev);
}
EXPORT_SYMBOL(transport_generic_request_failure);

void __target_execute_cmd(struct se_cmd *cmd, bool do_checks)
{
	sense_reason_t ret;

	if (!cmd->execute_cmd) {
		ret = TCM_LOGICAL_UNIT_COMMUNICATION_FAILURE;
		goto err;
	}
	if (do_checks) {
		/*
		 * Check for an existing UNIT ATTENTION condition after
		 * target_handle_task_attr() has done SAM task attr
		 * checking, and possibly have already defered execution
		 * out to target_restart_delayed_cmds() context.
		 */
		ret = target_scsi3_ua_check(cmd);
		if (ret)
			goto err;

		ret = target_alua_state_check(cmd);
		if (ret)
			goto err;

		ret = target_check_reservation(cmd);
		if (ret) {
			cmd->scsi_status = SAM_STAT_RESERVATION_CONFLICT;
			goto err;
		}
	}

	ret = cmd->execute_cmd(cmd);
	if (!ret)
		return;
err:
	spin_lock_irq(&cmd->t_state_lock);
	cmd->transport_state &= ~(CMD_T_BUSY|CMD_T_SENT);
	spin_unlock_irq(&cmd->t_state_lock);

	transport_generic_request_failure(cmd, ret);
}

static int target_write_prot_action(struct se_cmd *cmd)
{
	u32 sectors;
	/*
	 * Perform WRITE_INSERT of PI using software emulation when backend
	 * device has PI enabled, if the transport has not already generated
	 * PI using hardware WRITE_INSERT offload.
	 */
	switch (cmd->prot_op) {
	case TARGET_PROT_DOUT_INSERT:
		if (!(cmd->se_sess->sup_prot_ops & TARGET_PROT_DOUT_INSERT))
			sbc_dif_generate(cmd);
		break;
	case TARGET_PROT_DOUT_STRIP:
		if (cmd->se_sess->sup_prot_ops & TARGET_PROT_DOUT_STRIP)
			break;

		sectors = cmd->data_length >> ilog2(cmd->se_dev->dev_attrib.block_size);
		cmd->pi_err = sbc_dif_verify(cmd, cmd->t_task_lba,
					     sectors, 0, cmd->t_prot_sg, 0);
		if (unlikely(cmd->pi_err)) {
			spin_lock_irq(&cmd->t_state_lock);
			cmd->transport_state &= ~(CMD_T_BUSY|CMD_T_SENT);
			spin_unlock_irq(&cmd->t_state_lock);
			transport_generic_request_failure(cmd, cmd->pi_err);
			return -1;
		}
		break;
	default:
		break;
	}

	return 0;
}

static bool target_handle_task_attr(struct se_cmd *cmd)
{
	struct se_device *dev = cmd->se_dev;

	if (dev->transport->transport_flags & TRANSPORT_FLAG_PASSTHROUGH)
		return false;

	cmd->se_cmd_flags |= SCF_TASK_ATTR_SET;

	/*
	 * Check for the existence of HEAD_OF_QUEUE, and if true return 1
	 * to allow the passed struct se_cmd list of tasks to the front of the list.
	 */
	switch (cmd->sam_task_attr) {
	case TCM_HEAD_TAG:
		pr_debug("Added HEAD_OF_QUEUE for CDB: 0x%02x\n",
			 cmd->t_task_cdb[0]);
		return false;
	case TCM_ORDERED_TAG:
		atomic_inc_mb(&dev->dev_ordered_sync);

		pr_debug("Added ORDERED for CDB: 0x%02x to ordered list\n",
			 cmd->t_task_cdb[0]);

		/*
		 * Execute an ORDERED command if no other older commands
		 * exist that need to be completed first.
		 */
		if (!atomic_read(&dev->simple_cmds))
			return false;
		break;
	default:
		/*
		 * For SIMPLE and UNTAGGED Task Attribute commands
		 */
		atomic_inc_mb(&dev->simple_cmds);
		break;
	}

	if (atomic_read(&dev->dev_ordered_sync) == 0)
		return false;

	spin_lock(&dev->delayed_cmd_lock);
	list_add_tail(&cmd->se_delayed_node, &dev->delayed_cmd_list);
	spin_unlock(&dev->delayed_cmd_lock);

	pr_debug("Added CDB: 0x%02x Task Attr: 0x%02x to delayed CMD listn",
		cmd->t_task_cdb[0], cmd->sam_task_attr);
	return true;
}

static int __transport_check_aborted_status(struct se_cmd *, int);

void target_execute_cmd(struct se_cmd *cmd)
{
	/*
	 * Determine if frontend context caller is requesting the stopping of
	 * this command for frontend exceptions.
	 *
	 * If the received CDB has aleady been aborted stop processing it here.
	 */
	spin_lock_irq(&cmd->t_state_lock);
	if (__transport_check_aborted_status(cmd, 1)) {
		spin_unlock_irq(&cmd->t_state_lock);
		return;
	}
	if (cmd->transport_state & CMD_T_STOP) {
		pr_debug("%s:%d CMD_T_STOP for ITT: 0x%08llx\n",
			__func__, __LINE__, cmd->tag);

		spin_unlock_irq(&cmd->t_state_lock);
		complete_all(&cmd->t_transport_stop_comp);
		return;
	}

	cmd->t_state = TRANSPORT_PROCESSING;
	cmd->transport_state |= CMD_T_ACTIVE|CMD_T_BUSY|CMD_T_SENT;
	spin_unlock_irq(&cmd->t_state_lock);

	if (target_write_prot_action(cmd))
		return;

	if (target_handle_task_attr(cmd)) {
		spin_lock_irq(&cmd->t_state_lock);
		cmd->transport_state &= ~(CMD_T_BUSY | CMD_T_SENT);
		spin_unlock_irq(&cmd->t_state_lock);
		return;
	}

	__target_execute_cmd(cmd, true);
}
EXPORT_SYMBOL(target_execute_cmd);

/*
 * Process all commands up to the last received ORDERED task attribute which
 * requires another blocking boundary
 */
static void target_restart_delayed_cmds(struct se_device *dev)
{
	for (;;) {
		struct se_cmd *cmd;

		spin_lock(&dev->delayed_cmd_lock);
		if (list_empty(&dev->delayed_cmd_list)) {
			spin_unlock(&dev->delayed_cmd_lock);
			break;
		}

		cmd = list_entry(dev->delayed_cmd_list.next,
				 struct se_cmd, se_delayed_node);
		list_del(&cmd->se_delayed_node);
		spin_unlock(&dev->delayed_cmd_lock);

		__target_execute_cmd(cmd, true);

		if (cmd->sam_task_attr == TCM_ORDERED_TAG)
			break;
	}
}

/*
 * Called from I/O completion to determine which dormant/delayed
 * and ordered cmds need to have their tasks added to the execution queue.
 */
static void transport_complete_task_attr(struct se_cmd *cmd)
{
	struct se_device *dev = cmd->se_dev;

	if (dev->transport->transport_flags & TRANSPORT_FLAG_PASSTHROUGH)
		return;

	if (!(cmd->se_cmd_flags & SCF_TASK_ATTR_SET))
		goto restart;

	if (cmd->sam_task_attr == TCM_SIMPLE_TAG) {
		atomic_dec_mb(&dev->simple_cmds);
		dev->dev_cur_ordered_id++;
		pr_debug("Incremented dev->dev_cur_ordered_id: %u for SIMPLE\n",
			 dev->dev_cur_ordered_id);
	} else if (cmd->sam_task_attr == TCM_HEAD_TAG) {
		dev->dev_cur_ordered_id++;
		pr_debug("Incremented dev_cur_ordered_id: %u for HEAD_OF_QUEUE\n",
			 dev->dev_cur_ordered_id);
	} else if (cmd->sam_task_attr == TCM_ORDERED_TAG) {
		atomic_dec_mb(&dev->dev_ordered_sync);

		dev->dev_cur_ordered_id++;
		pr_debug("Incremented dev_cur_ordered_id: %u for ORDERED\n",
			 dev->dev_cur_ordered_id);
	}
restart:
	target_restart_delayed_cmds(dev);
}

static void transport_complete_qf(struct se_cmd *cmd)
{
	int ret = 0;

	transport_complete_task_attr(cmd);

	if (cmd->se_cmd_flags & SCF_TRANSPORT_TASK_SENSE) {
		trace_target_cmd_complete(cmd);
		ret = cmd->se_tfo->queue_status(cmd);
		goto out;
	}

	switch (cmd->data_direction) {
	case DMA_FROM_DEVICE:
		if (cmd->scsi_status)
			goto queue_status;

		trace_target_cmd_complete(cmd);
		ret = cmd->se_tfo->queue_data_in(cmd);
		break;
	case DMA_TO_DEVICE:
		if (cmd->se_cmd_flags & SCF_BIDI) {
			ret = cmd->se_tfo->queue_data_in(cmd);
			break;
		}
		/* Fall through for DMA_TO_DEVICE */
	case DMA_NONE:
queue_status:
		trace_target_cmd_complete(cmd);
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
	atomic_inc_mb(&dev->dev_qf_count);
	spin_unlock_irq(&cmd->se_dev->qf_cmd_lock);

	schedule_work(&cmd->se_dev->qf_work_queue);
}

static bool target_read_prot_action(struct se_cmd *cmd)
{
	switch (cmd->prot_op) {
	case TARGET_PROT_DIN_STRIP:
		if (!(cmd->se_sess->sup_prot_ops & TARGET_PROT_DIN_STRIP)) {
			u32 sectors = cmd->data_length >>
				  ilog2(cmd->se_dev->dev_attrib.block_size);

			cmd->pi_err = sbc_dif_verify(cmd, cmd->t_task_lba,
						     sectors, 0, cmd->t_prot_sg,
						     0);
			if (cmd->pi_err)
				return true;
		}
		break;
	case TARGET_PROT_DIN_INSERT:
		if (cmd->se_sess->sup_prot_ops & TARGET_PROT_DIN_INSERT)
			break;

		sbc_dif_generate(cmd);
		break;
	default:
		break;
	}

	return false;
}

static void target_complete_ok_work(struct work_struct *work)
{
	struct se_cmd *cmd = container_of(work, struct se_cmd, work);
	int ret;

	/*
	 * Check if we need to move delayed/dormant tasks from cmds on the
	 * delayed execution list after a HEAD_OF_QUEUE or ORDERED Task
	 * Attribute.
	 */
	transport_complete_task_attr(cmd);

	/*
	 * Check to schedule QUEUE_FULL work, or execute an existing
	 * cmd->transport_qf_callback()
	 */
	if (atomic_read(&cmd->se_dev->dev_qf_count) != 0)
		schedule_work(&cmd->se_dev->qf_work_queue);

	/*
	 * Check if we need to send a sense buffer from
	 * the struct se_cmd in question.
	 */
	if (cmd->se_cmd_flags & SCF_TRANSPORT_TASK_SENSE) {
		WARN_ON(!cmd->scsi_status);
		ret = transport_send_check_condition_and_sense(
					cmd, 0, 1);
		if (ret == -EAGAIN || ret == -ENOMEM)
			goto queue_full;

		transport_lun_remove_cmd(cmd);
		transport_cmd_check_stop_to_fabric(cmd);
		return;
	}
	/*
	 * Check for a callback, used by amongst other things
	 * XDWRITE_READ_10 and COMPARE_AND_WRITE emulation.
	 */
	if (cmd->transport_complete_callback) {
		sense_reason_t rc;
		bool caw = (cmd->se_cmd_flags & SCF_COMPARE_AND_WRITE);
		bool zero_dl = !(cmd->data_length);
		int post_ret = 0;

		rc = cmd->transport_complete_callback(cmd, true, &post_ret);
		if (!rc && !post_ret) {
			if (caw && zero_dl)
				goto queue_rsp;

			return;
		} else if (rc) {
			ret = transport_send_check_condition_and_sense(cmd,
						rc, 0);
			if (ret == -EAGAIN || ret == -ENOMEM)
				goto queue_full;

			transport_lun_remove_cmd(cmd);
			transport_cmd_check_stop_to_fabric(cmd);
			return;
		}
	}

queue_rsp:
	switch (cmd->data_direction) {
	case DMA_FROM_DEVICE:
		if (cmd->scsi_status)
			goto queue_status;

		atomic_long_add(cmd->data_length,
				&cmd->se_lun->lun_stats.tx_data_octets);
		/*
		 * Perform READ_STRIP of PI using software emulation when
		 * backend had PI enabled, if the transport will not be
		 * performing hardware READ_STRIP offload.
		 */
		if (target_read_prot_action(cmd)) {
			ret = transport_send_check_condition_and_sense(cmd,
						cmd->pi_err, 0);
			if (ret == -EAGAIN || ret == -ENOMEM)
				goto queue_full;

			transport_lun_remove_cmd(cmd);
			transport_cmd_check_stop_to_fabric(cmd);
			return;
		}

		trace_target_cmd_complete(cmd);
		ret = cmd->se_tfo->queue_data_in(cmd);
		if (ret == -EAGAIN || ret == -ENOMEM)
			goto queue_full;
		break;
	case DMA_TO_DEVICE:
		atomic_long_add(cmd->data_length,
				&cmd->se_lun->lun_stats.rx_data_octets);
		/*
		 * Check if we need to send READ payload for BIDI-COMMAND
		 */
		if (cmd->se_cmd_flags & SCF_BIDI) {
			atomic_long_add(cmd->data_length,
					&cmd->se_lun->lun_stats.tx_data_octets);
			ret = cmd->se_tfo->queue_data_in(cmd);
			if (ret == -EAGAIN || ret == -ENOMEM)
				goto queue_full;
			break;
		}
		/* Fall through for DMA_TO_DEVICE */
	case DMA_NONE:
queue_status:
		trace_target_cmd_complete(cmd);
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

void target_free_sgl(struct scatterlist *sgl, int nents)
{
	struct scatterlist *sg;
	int count;

	for_each_sg(sgl, sg, nents, count)
		__free_page(sg_page(sg));

	kfree(sgl);
}
EXPORT_SYMBOL(target_free_sgl);

static inline void transport_reset_sgl_orig(struct se_cmd *cmd)
{
	/*
	 * Check for saved t_data_sg that may be used for COMPARE_AND_WRITE
	 * emulation, and free + reset pointers if necessary..
	 */
	if (!cmd->t_data_sg_orig)
		return;

	kfree(cmd->t_data_sg);
	cmd->t_data_sg = cmd->t_data_sg_orig;
	cmd->t_data_sg_orig = NULL;
	cmd->t_data_nents = cmd->t_data_nents_orig;
	cmd->t_data_nents_orig = 0;
}

static inline void transport_free_pages(struct se_cmd *cmd)
{
	if (!(cmd->se_cmd_flags & SCF_PASSTHROUGH_PROT_SG_TO_MEM_NOALLOC)) {
		target_free_sgl(cmd->t_prot_sg, cmd->t_prot_nents);
		cmd->t_prot_sg = NULL;
		cmd->t_prot_nents = 0;
	}

	if (cmd->se_cmd_flags & SCF_PASSTHROUGH_SG_TO_MEM_NOALLOC) {
		/*
		 * Release special case READ buffer payload required for
		 * SG_TO_MEM_NOALLOC to function with COMPARE_AND_WRITE
		 */
		if (cmd->se_cmd_flags & SCF_COMPARE_AND_WRITE) {
			target_free_sgl(cmd->t_bidi_data_sg,
					   cmd->t_bidi_data_nents);
			cmd->t_bidi_data_sg = NULL;
			cmd->t_bidi_data_nents = 0;
		}
		transport_reset_sgl_orig(cmd);
		return;
	}
	transport_reset_sgl_orig(cmd);

	target_free_sgl(cmd->t_data_sg, cmd->t_data_nents);
	cmd->t_data_sg = NULL;
	cmd->t_data_nents = 0;

	target_free_sgl(cmd->t_bidi_data_sg, cmd->t_bidi_data_nents);
	cmd->t_bidi_data_sg = NULL;
	cmd->t_bidi_data_nents = 0;
}

/**
 * transport_put_cmd - release a reference to a command
 * @cmd:       command to release
 *
 * This routine releases our reference to the command and frees it if possible.
 */
static int transport_put_cmd(struct se_cmd *cmd)
{
	BUG_ON(!cmd->se_tfo);
	/*
	 * If this cmd has been setup with target_get_sess_cmd(), drop
	 * the kref and call ->release_cmd() in kref callback.
	 */
	return target_put_sess_cmd(cmd);
}

void *transport_kmap_data_sg(struct se_cmd *cmd)
{
	struct scatterlist *sg = cmd->t_data_sg;
	struct page **pages;
	int i;

	/*
	 * We need to take into account a possible offset here for fabrics like
	 * tcm_loop who may be using a contig buffer from the SCSI midlayer for
	 * control CDBs passed as SGLs via transport_generic_map_mem_to_cmd()
	 */
	if (!cmd->t_data_nents)
		return NULL;

	BUG_ON(!sg);
	if (cmd->t_data_nents == 1)
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

int
target_alloc_sgl(struct scatterlist **sgl, unsigned int *nents, u32 length,
		 bool zero_page, bool chainable)
{
	struct scatterlist *sg;
	struct page *page;
	gfp_t zero_flag = (zero_page) ? __GFP_ZERO : 0;
	unsigned int nalloc, nent;
	int i = 0;

	nalloc = nent = DIV_ROUND_UP(length, PAGE_SIZE);
	if (chainable)
		nalloc++;
	sg = kmalloc_array(nalloc, sizeof(struct scatterlist), GFP_KERNEL);
	if (!sg)
		return -ENOMEM;

	sg_init_table(sg, nalloc);

	while (length) {
		u32 page_len = min_t(u32, length, PAGE_SIZE);
		page = alloc_page(GFP_KERNEL | zero_flag);
		if (!page)
			goto out;

		sg_set_page(&sg[i], page, page_len, 0);
		length -= page_len;
		i++;
	}
	*sgl = sg;
	*nents = nent;
	return 0;

out:
	while (i > 0) {
		i--;
		__free_page(sg_page(&sg[i]));
	}
	kfree(sg);
	return -ENOMEM;
}
EXPORT_SYMBOL(target_alloc_sgl);

/*
 * Allocate any required resources to execute the command.  For writes we
 * might not have the payload yet, so notify the fabric via a call to
 * ->write_pending instead. Otherwise place it on the execution queue.
 */
sense_reason_t
transport_generic_new_cmd(struct se_cmd *cmd)
{
	int ret = 0;
	bool zero_flag = !(cmd->se_cmd_flags & SCF_SCSI_DATA_CDB);

	if (cmd->prot_op != TARGET_PROT_NORMAL &&
	    !(cmd->se_cmd_flags & SCF_PASSTHROUGH_PROT_SG_TO_MEM_NOALLOC)) {
		ret = target_alloc_sgl(&cmd->t_prot_sg, &cmd->t_prot_nents,
				       cmd->prot_length, true, false);
		if (ret < 0)
			return TCM_LOGICAL_UNIT_COMMUNICATION_FAILURE;
	}

	/*
	 * Determine is the TCM fabric module has already allocated physical
	 * memory, and is directly calling transport_generic_map_mem_to_cmd()
	 * beforehand.
	 */
	if (!(cmd->se_cmd_flags & SCF_PASSTHROUGH_SG_TO_MEM_NOALLOC) &&
	    cmd->data_length) {

		if ((cmd->se_cmd_flags & SCF_BIDI) ||
		    (cmd->se_cmd_flags & SCF_COMPARE_AND_WRITE)) {
			u32 bidi_length;

			if (cmd->se_cmd_flags & SCF_COMPARE_AND_WRITE)
				bidi_length = cmd->t_task_nolb *
					      cmd->se_dev->dev_attrib.block_size;
			else
				bidi_length = cmd->data_length;

			ret = target_alloc_sgl(&cmd->t_bidi_data_sg,
					       &cmd->t_bidi_data_nents,
					       bidi_length, zero_flag, false);
			if (ret < 0)
				return TCM_LOGICAL_UNIT_COMMUNICATION_FAILURE;
		}

		ret = target_alloc_sgl(&cmd->t_data_sg, &cmd->t_data_nents,
				       cmd->data_length, zero_flag, false);
		if (ret < 0)
			return TCM_LOGICAL_UNIT_COMMUNICATION_FAILURE;
	} else if ((cmd->se_cmd_flags & SCF_COMPARE_AND_WRITE) &&
		    cmd->data_length) {
		/*
		 * Special case for COMPARE_AND_WRITE with fabrics
		 * using SCF_PASSTHROUGH_SG_TO_MEM_NOALLOC.
		 */
		u32 caw_length = cmd->t_task_nolb *
				 cmd->se_dev->dev_attrib.block_size;

		ret = target_alloc_sgl(&cmd->t_bidi_data_sg,
				       &cmd->t_bidi_data_nents,
				       caw_length, zero_flag, false);
		if (ret < 0)
			return TCM_LOGICAL_UNIT_COMMUNICATION_FAILURE;
	}
	/*
	 * If this command is not a write we can execute it right here,
	 * for write buffers we need to notify the fabric driver first
	 * and let it call back once the write buffers are ready.
	 */
	target_add_to_state_list(cmd);
	if (cmd->data_direction != DMA_TO_DEVICE || cmd->data_length == 0) {
		target_execute_cmd(cmd);
		return 0;
	}
	transport_cmd_check_stop(cmd, false, true);

	ret = cmd->se_tfo->write_pending(cmd);
	if (ret == -EAGAIN || ret == -ENOMEM)
		goto queue_full;

	/* fabric drivers should only return -EAGAIN or -ENOMEM as error */
	WARN_ON(ret);

	return (!ret) ? 0 : TCM_LOGICAL_UNIT_COMMUNICATION_FAILURE;

queue_full:
	pr_debug("Handling write_pending QUEUE__FULL: se_cmd: %p\n", cmd);
	cmd->t_state = TRANSPORT_COMPLETE_QF_WP;
	transport_handle_queue_full(cmd, cmd->se_dev);
	return 0;
}
EXPORT_SYMBOL(transport_generic_new_cmd);

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

static bool
__transport_wait_for_tasks(struct se_cmd *, bool, bool *, bool *,
			   unsigned long *flags);

static void target_wait_free_cmd(struct se_cmd *cmd, bool *aborted, bool *tas)
{
	unsigned long flags;

	spin_lock_irqsave(&cmd->t_state_lock, flags);
	__transport_wait_for_tasks(cmd, true, aborted, tas, &flags);
	spin_unlock_irqrestore(&cmd->t_state_lock, flags);
}

int transport_generic_free_cmd(struct se_cmd *cmd, int wait_for_tasks)
{
	int ret = 0;
	bool aborted = false, tas = false;

	if (!(cmd->se_cmd_flags & SCF_SE_LUN_CMD)) {
		if (wait_for_tasks && (cmd->se_cmd_flags & SCF_SCSI_TMR_CDB))
			target_wait_free_cmd(cmd, &aborted, &tas);

		if (!aborted || tas)
			ret = transport_put_cmd(cmd);
	} else {
		if (wait_for_tasks)
			target_wait_free_cmd(cmd, &aborted, &tas);
		/*
		 * Handle WRITE failure case where transport_generic_new_cmd()
		 * has already added se_cmd to state_list, but fabric has
		 * failed command before I/O submission.
		 */
		if (cmd->state_active)
			target_remove_from_state_list(cmd);

		if (cmd->se_lun)
			transport_lun_remove_cmd(cmd);

		if (!aborted || tas)
			ret = transport_put_cmd(cmd);
	}
	/*
	 * If the task has been internally aborted due to TMR ABORT_TASK
	 * or LUN_RESET, target_core_tmr.c is responsible for performing
	 * the remaining calls to target_put_sess_cmd(), and not the
	 * callers of this function.
	 */
	if (aborted) {
		pr_debug("Detected CMD_T_ABORTED for ITT: %llu\n", cmd->tag);
		wait_for_completion(&cmd->cmd_wait_comp);
		cmd->se_tfo->release_cmd(cmd);
		ret = 1;
	}
	return ret;
}
EXPORT_SYMBOL(transport_generic_free_cmd);

/* target_get_sess_cmd - Add command to active ->sess_cmd_list
 * @se_cmd:	command descriptor to add
 * @ack_kref:	Signal that fabric will perform an ack target_put_sess_cmd()
 */
int target_get_sess_cmd(struct se_cmd *se_cmd, bool ack_kref)
{
	struct se_session *se_sess = se_cmd->se_sess;
	unsigned long flags;
	int ret = 0;

	/*
	 * Add a second kref if the fabric caller is expecting to handle
	 * fabric acknowledgement that requires two target_put_sess_cmd()
	 * invocations before se_cmd descriptor release.
	 */
	if (ack_kref) {
		if (!kref_get_unless_zero(&se_cmd->cmd_kref))
			return -EINVAL;

		se_cmd->se_cmd_flags |= SCF_ACK_KREF;
	}

	spin_lock_irqsave(&se_sess->sess_cmd_lock, flags);
	if (se_sess->sess_tearing_down) {
		ret = -ESHUTDOWN;
		goto out;
	}
	list_add_tail(&se_cmd->se_cmd_list, &se_sess->sess_cmd_list);
out:
	spin_unlock_irqrestore(&se_sess->sess_cmd_lock, flags);

	if (ret && ack_kref)
		target_put_sess_cmd(se_cmd);

	return ret;
}
EXPORT_SYMBOL(target_get_sess_cmd);

static void target_free_cmd_mem(struct se_cmd *cmd)
{
	transport_free_pages(cmd);

	if (cmd->se_cmd_flags & SCF_SCSI_TMR_CDB)
		core_tmr_release_req(cmd->se_tmr_req);
	if (cmd->t_task_cdb != cmd->__t_task_cdb)
		kfree(cmd->t_task_cdb);
}

static void target_release_cmd_kref(struct kref *kref)
{
	struct se_cmd *se_cmd = container_of(kref, struct se_cmd, cmd_kref);
	struct se_session *se_sess = se_cmd->se_sess;
	unsigned long flags;
	bool fabric_stop;

	spin_lock_irqsave(&se_sess->sess_cmd_lock, flags);

	spin_lock(&se_cmd->t_state_lock);
	fabric_stop = (se_cmd->transport_state & CMD_T_FABRIC_STOP) &&
		      (se_cmd->transport_state & CMD_T_ABORTED);
	spin_unlock(&se_cmd->t_state_lock);

	if (se_cmd->cmd_wait_set || fabric_stop) {
		list_del_init(&se_cmd->se_cmd_list);
		spin_unlock_irqrestore(&se_sess->sess_cmd_lock, flags);
		target_free_cmd_mem(se_cmd);
		complete(&se_cmd->cmd_wait_comp);
		return;
	}
	list_del_init(&se_cmd->se_cmd_list);
	spin_unlock_irqrestore(&se_sess->sess_cmd_lock, flags);

	target_free_cmd_mem(se_cmd);
	se_cmd->se_tfo->release_cmd(se_cmd);
}

/* target_put_sess_cmd - Check for active I/O shutdown via kref_put
 * @se_cmd:	command descriptor to drop
 */
int target_put_sess_cmd(struct se_cmd *se_cmd)
{
	struct se_session *se_sess = se_cmd->se_sess;

	if (!se_sess) {
		target_free_cmd_mem(se_cmd);
		se_cmd->se_tfo->release_cmd(se_cmd);
		return 1;
	}
	return kref_put(&se_cmd->cmd_kref, target_release_cmd_kref);
}
EXPORT_SYMBOL(target_put_sess_cmd);

/* target_sess_cmd_list_set_waiting - Flag all commands in
 *         sess_cmd_list to complete cmd_wait_comp.  Set
 *         sess_tearing_down so no more commands are queued.
 * @se_sess:	session to flag
 */
void target_sess_cmd_list_set_waiting(struct se_session *se_sess)
{
	struct se_cmd *se_cmd, *tmp_cmd;
	unsigned long flags;
	int rc;

	spin_lock_irqsave(&se_sess->sess_cmd_lock, flags);
	if (se_sess->sess_tearing_down) {
		spin_unlock_irqrestore(&se_sess->sess_cmd_lock, flags);
		return;
	}
	se_sess->sess_tearing_down = 1;
	list_splice_init(&se_sess->sess_cmd_list, &se_sess->sess_wait_list);

	list_for_each_entry_safe(se_cmd, tmp_cmd,
				 &se_sess->sess_wait_list, se_cmd_list) {
		rc = kref_get_unless_zero(&se_cmd->cmd_kref);
		if (rc) {
			se_cmd->cmd_wait_set = 1;
			spin_lock(&se_cmd->t_state_lock);
			se_cmd->transport_state |= CMD_T_FABRIC_STOP;
			spin_unlock(&se_cmd->t_state_lock);
		} else
			list_del_init(&se_cmd->se_cmd_list);
	}

	spin_unlock_irqrestore(&se_sess->sess_cmd_lock, flags);
}
EXPORT_SYMBOL(target_sess_cmd_list_set_waiting);

/* target_wait_for_sess_cmds - Wait for outstanding descriptors
 * @se_sess:    session to wait for active I/O
 */
void target_wait_for_sess_cmds(struct se_session *se_sess)
{
	struct se_cmd *se_cmd, *tmp_cmd;
	unsigned long flags;
	bool tas;

	list_for_each_entry_safe(se_cmd, tmp_cmd,
				&se_sess->sess_wait_list, se_cmd_list) {
		pr_debug("Waiting for se_cmd: %p t_state: %d, fabric state:"
			" %d\n", se_cmd, se_cmd->t_state,
			se_cmd->se_tfo->get_cmd_state(se_cmd));

		spin_lock_irqsave(&se_cmd->t_state_lock, flags);
		tas = (se_cmd->transport_state & CMD_T_TAS);
		spin_unlock_irqrestore(&se_cmd->t_state_lock, flags);

		if (!target_put_sess_cmd(se_cmd)) {
			if (tas)
				target_put_sess_cmd(se_cmd);
		}

		wait_for_completion(&se_cmd->cmd_wait_comp);
		pr_debug("After cmd_wait_comp: se_cmd: %p t_state: %d"
			" fabric state: %d\n", se_cmd, se_cmd->t_state,
			se_cmd->se_tfo->get_cmd_state(se_cmd));

		se_cmd->se_tfo->release_cmd(se_cmd);
	}

	spin_lock_irqsave(&se_sess->sess_cmd_lock, flags);
	WARN_ON(!list_empty(&se_sess->sess_cmd_list));
	spin_unlock_irqrestore(&se_sess->sess_cmd_lock, flags);

}
EXPORT_SYMBOL(target_wait_for_sess_cmds);

static void target_lun_confirm(struct percpu_ref *ref)
{
	struct se_lun *lun = container_of(ref, struct se_lun, lun_ref);

	complete(&lun->lun_ref_comp);
}

void transport_clear_lun_ref(struct se_lun *lun)
{
	/*
	 * Mark the percpu-ref as DEAD, switch to atomic_t mode, drop
	 * the initial reference and schedule confirm kill to be
	 * executed after one full RCU grace period has completed.
	 */
	percpu_ref_kill_and_confirm(&lun->lun_ref, target_lun_confirm);
	/*
	 * The first completion waits for percpu_ref_switch_to_atomic_rcu()
	 * to call target_lun_confirm after lun->lun_ref has been marked
	 * as __PERCPU_REF_DEAD on all CPUs, and switches to atomic_t
	 * mode so that percpu_ref_tryget_live() lookup of lun->lun_ref
	 * fails for all new incoming I/O.
	 */
	wait_for_completion(&lun->lun_ref_comp);
	/*
	 * The second completion waits for percpu_ref_put_many() to
	 * invoke ->release() after lun->lun_ref has switched to
	 * atomic_t mode, and lun->lun_ref.count has reached zero.
	 *
	 * At this point all target-core lun->lun_ref references have
	 * been dropped via transport_lun_remove_cmd(), and it's safe
	 * to proceed with the remaining LUN shutdown.
	 */
	wait_for_completion(&lun->lun_shutdown_comp);
}

static bool
__transport_wait_for_tasks(struct se_cmd *cmd, bool fabric_stop,
			   bool *aborted, bool *tas, unsigned long *flags)
	__releases(&cmd->t_state_lock)
	__acquires(&cmd->t_state_lock)
{

	assert_spin_locked(&cmd->t_state_lock);
	WARN_ON_ONCE(!irqs_disabled());

	if (fabric_stop)
		cmd->transport_state |= CMD_T_FABRIC_STOP;

	if (cmd->transport_state & CMD_T_ABORTED)
		*aborted = true;

	if (cmd->transport_state & CMD_T_TAS)
		*tas = true;

	if (!(cmd->se_cmd_flags & SCF_SE_LUN_CMD) &&
	    !(cmd->se_cmd_flags & SCF_SCSI_TMR_CDB))
		return false;

	if (!(cmd->se_cmd_flags & SCF_SUPPORTED_SAM_OPCODE) &&
	    !(cmd->se_cmd_flags & SCF_SCSI_TMR_CDB))
		return false;

	if (!(cmd->transport_state & CMD_T_ACTIVE))
		return false;

	if (fabric_stop && *aborted)
		return false;

	cmd->transport_state |= CMD_T_STOP;

	pr_debug("wait_for_tasks: Stopping %p ITT: 0x%08llx i_state: %d,"
		 " t_state: %d, CMD_T_STOP\n", cmd, cmd->tag,
		 cmd->se_tfo->get_cmd_state(cmd), cmd->t_state);

	spin_unlock_irqrestore(&cmd->t_state_lock, *flags);

	wait_for_completion(&cmd->t_transport_stop_comp);

	spin_lock_irqsave(&cmd->t_state_lock, *flags);
	cmd->transport_state &= ~(CMD_T_ACTIVE | CMD_T_STOP);

	pr_debug("wait_for_tasks: Stopped wait_for_completion(&cmd->"
		 "t_transport_stop_comp) for ITT: 0x%08llx\n", cmd->tag);

	return true;
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
	bool ret, aborted = false, tas = false;

	spin_lock_irqsave(&cmd->t_state_lock, flags);
	ret = __transport_wait_for_tasks(cmd, false, &aborted, &tas, &flags);
	spin_unlock_irqrestore(&cmd->t_state_lock, flags);

	return ret;
}
EXPORT_SYMBOL(transport_wait_for_tasks);

struct sense_info {
	u8 key;
	u8 asc;
	u8 ascq;
	bool add_sector_info;
};

static const struct sense_info sense_info_table[] = {
	[TCM_NO_SENSE] = {
		.key = NOT_READY
	},
	[TCM_NON_EXISTENT_LUN] = {
		.key = ILLEGAL_REQUEST,
		.asc = 0x25 /* LOGICAL UNIT NOT SUPPORTED */
	},
	[TCM_UNSUPPORTED_SCSI_OPCODE] = {
		.key = ILLEGAL_REQUEST,
		.asc = 0x20, /* INVALID COMMAND OPERATION CODE */
	},
	[TCM_SECTOR_COUNT_TOO_MANY] = {
		.key = ILLEGAL_REQUEST,
		.asc = 0x20, /* INVALID COMMAND OPERATION CODE */
	},
	[TCM_UNKNOWN_MODE_PAGE] = {
		.key = ILLEGAL_REQUEST,
		.asc = 0x24, /* INVALID FIELD IN CDB */
	},
	[TCM_CHECK_CONDITION_ABORT_CMD] = {
		.key = ABORTED_COMMAND,
		.asc = 0x29, /* BUS DEVICE RESET FUNCTION OCCURRED */
		.ascq = 0x03,
	},
	[TCM_INCORRECT_AMOUNT_OF_DATA] = {
		.key = ABORTED_COMMAND,
		.asc = 0x0c, /* WRITE ERROR */
		.ascq = 0x0d, /* NOT ENOUGH UNSOLICITED DATA */
	},
	[TCM_INVALID_CDB_FIELD] = {
		.key = ILLEGAL_REQUEST,
		.asc = 0x24, /* INVALID FIELD IN CDB */
	},
	[TCM_INVALID_PARAMETER_LIST] = {
		.key = ILLEGAL_REQUEST,
		.asc = 0x26, /* INVALID FIELD IN PARAMETER LIST */
	},
	[TCM_PARAMETER_LIST_LENGTH_ERROR] = {
		.key = ILLEGAL_REQUEST,
		.asc = 0x1a, /* PARAMETER LIST LENGTH ERROR */
	},
	[TCM_UNEXPECTED_UNSOLICITED_DATA] = {
		.key = ILLEGAL_REQUEST,
		.asc = 0x0c, /* WRITE ERROR */
		.ascq = 0x0c, /* UNEXPECTED_UNSOLICITED_DATA */
	},
	[TCM_SERVICE_CRC_ERROR] = {
		.key = ABORTED_COMMAND,
		.asc = 0x47, /* PROTOCOL SERVICE CRC ERROR */
		.ascq = 0x05, /* N/A */
	},
	[TCM_SNACK_REJECTED] = {
		.key = ABORTED_COMMAND,
		.asc = 0x11, /* READ ERROR */
		.ascq = 0x13, /* FAILED RETRANSMISSION REQUEST */
	},
	[TCM_WRITE_PROTECTED] = {
		.key = DATA_PROTECT,
		.asc = 0x27, /* WRITE PROTECTED */
	},
	[TCM_ADDRESS_OUT_OF_RANGE] = {
		.key = ILLEGAL_REQUEST,
		.asc = 0x21, /* LOGICAL BLOCK ADDRESS OUT OF RANGE */
	},
	[TCM_CHECK_CONDITION_UNIT_ATTENTION] = {
		.key = UNIT_ATTENTION,
	},
	[TCM_CHECK_CONDITION_NOT_READY] = {
		.key = NOT_READY,
	},
	[TCM_MISCOMPARE_VERIFY] = {
		.key = MISCOMPARE,
		.asc = 0x1d, /* MISCOMPARE DURING VERIFY OPERATION */
		.ascq = 0x00,
	},
	[TCM_LOGICAL_BLOCK_GUARD_CHECK_FAILED] = {
		.key = ABORTED_COMMAND,
		.asc = 0x10,
		.ascq = 0x01, /* LOGICAL BLOCK GUARD CHECK FAILED */
		.add_sector_info = true,
	},
	[TCM_LOGICAL_BLOCK_APP_TAG_CHECK_FAILED] = {
		.key = ABORTED_COMMAND,
		.asc = 0x10,
		.ascq = 0x02, /* LOGICAL BLOCK APPLICATION TAG CHECK FAILED */
		.add_sector_info = true,
	},
	[TCM_LOGICAL_BLOCK_REF_TAG_CHECK_FAILED] = {
		.key = ABORTED_COMMAND,
		.asc = 0x10,
		.ascq = 0x03, /* LOGICAL BLOCK REFERENCE TAG CHECK FAILED */
		.add_sector_info = true,
	},
	[TCM_COPY_TARGET_DEVICE_NOT_REACHABLE] = {
		.key = COPY_ABORTED,
		.asc = 0x0d,
		.ascq = 0x02, /* COPY TARGET DEVICE NOT REACHABLE */

	},
	[TCM_LOGICAL_UNIT_COMMUNICATION_FAILURE] = {
		/*
		 * Returning ILLEGAL REQUEST would cause immediate IO errors on
		 * Solaris initiators.  Returning NOT READY instead means the
		 * operations will be retried a finite number of times and we
		 * can survive intermittent errors.
		 */
		.key = NOT_READY,
		.asc = 0x08, /* LOGICAL UNIT COMMUNICATION FAILURE */
	},
};

static int translate_sense_reason(struct se_cmd *cmd, sense_reason_t reason)
{
	const struct sense_info *si;
	u8 *buffer = cmd->sense_buffer;
	int r = (__force int)reason;
	u8 asc, ascq;
	bool desc_format = target_sense_desc_format(cmd->se_dev);

	if (r < ARRAY_SIZE(sense_info_table) && sense_info_table[r].key)
		si = &sense_info_table[r];
	else
		si = &sense_info_table[(__force int)
				       TCM_LOGICAL_UNIT_COMMUNICATION_FAILURE];

	if (reason == TCM_CHECK_CONDITION_UNIT_ATTENTION) {
		core_scsi3_ua_for_check_condition(cmd, &asc, &ascq);
		WARN_ON_ONCE(asc == 0);
	} else if (si->asc == 0) {
		WARN_ON_ONCE(cmd->scsi_asc == 0);
		asc = cmd->scsi_asc;
		ascq = cmd->scsi_ascq;
	} else {
		asc = si->asc;
		ascq = si->ascq;
	}

	scsi_build_sense_buffer(desc_format, buffer, si->key, asc, ascq);
	if (si->add_sector_info)
		return scsi_set_sense_information(buffer,
						  cmd->scsi_sense_length,
						  cmd->bad_sector);

	return 0;
}

int
transport_send_check_condition_and_sense(struct se_cmd *cmd,
		sense_reason_t reason, int from_transport)
{
	unsigned long flags;

	spin_lock_irqsave(&cmd->t_state_lock, flags);
	if (cmd->se_cmd_flags & SCF_SENT_CHECK_CONDITION) {
		spin_unlock_irqrestore(&cmd->t_state_lock, flags);
		return 0;
	}
	cmd->se_cmd_flags |= SCF_SENT_CHECK_CONDITION;
	spin_unlock_irqrestore(&cmd->t_state_lock, flags);

	if (!from_transport) {
		int rc;

		cmd->se_cmd_flags |= SCF_EMULATED_TASK_SENSE;
		cmd->scsi_status = SAM_STAT_CHECK_CONDITION;
		cmd->scsi_sense_length  = TRANSPORT_SENSE_BUFFER;
		rc = translate_sense_reason(cmd, reason);
		if (rc)
			return rc;
	}

	trace_target_cmd_complete(cmd);
	return cmd->se_tfo->queue_status(cmd);
}
EXPORT_SYMBOL(transport_send_check_condition_and_sense);

static int __transport_check_aborted_status(struct se_cmd *cmd, int send_status)
	__releases(&cmd->t_state_lock)
	__acquires(&cmd->t_state_lock)
{
	assert_spin_locked(&cmd->t_state_lock);
	WARN_ON_ONCE(!irqs_disabled());

	if (!(cmd->transport_state & CMD_T_ABORTED))
		return 0;
	/*
	 * If cmd has been aborted but either no status is to be sent or it has
	 * already been sent, just return
	 */
	if (!send_status || !(cmd->se_cmd_flags & SCF_SEND_DELAYED_TAS)) {
		if (send_status)
			cmd->se_cmd_flags |= SCF_SEND_DELAYED_TAS;
		return 1;
	}

	pr_debug("Sending delayed SAM_STAT_TASK_ABORTED status for CDB:"
		" 0x%02x ITT: 0x%08llx\n", cmd->t_task_cdb[0], cmd->tag);

	cmd->se_cmd_flags &= ~SCF_SEND_DELAYED_TAS;
	cmd->scsi_status = SAM_STAT_TASK_ABORTED;
	trace_target_cmd_complete(cmd);

	spin_unlock_irq(&cmd->t_state_lock);
	cmd->se_tfo->queue_status(cmd);
	spin_lock_irq(&cmd->t_state_lock);

	return 1;
}

int transport_check_aborted_status(struct se_cmd *cmd, int send_status)
{
	int ret;

	spin_lock_irq(&cmd->t_state_lock);
	ret = __transport_check_aborted_status(cmd, send_status);
	spin_unlock_irq(&cmd->t_state_lock);

	return ret;
}
EXPORT_SYMBOL(transport_check_aborted_status);

void transport_send_task_abort(struct se_cmd *cmd)
{
	unsigned long flags;

	spin_lock_irqsave(&cmd->t_state_lock, flags);
	if (cmd->se_cmd_flags & (SCF_SENT_CHECK_CONDITION)) {
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
			spin_lock_irqsave(&cmd->t_state_lock, flags);
			if (cmd->se_cmd_flags & SCF_SEND_DELAYED_TAS) {
				spin_unlock_irqrestore(&cmd->t_state_lock, flags);
				goto send_abort;
			}
			cmd->se_cmd_flags |= SCF_SEND_DELAYED_TAS;
			spin_unlock_irqrestore(&cmd->t_state_lock, flags);
			return;
		}
	}
send_abort:
	cmd->scsi_status = SAM_STAT_TASK_ABORTED;

	transport_lun_remove_cmd(cmd);

	pr_debug("Setting SAM_STAT_TASK_ABORTED status for CDB: 0x%02x, ITT: 0x%08llx\n",
		 cmd->t_task_cdb[0], cmd->tag);

	trace_target_cmd_complete(cmd);
	cmd->se_tfo->queue_status(cmd);
}

static void target_tmr_work(struct work_struct *work)
{
	struct se_cmd *cmd = container_of(work, struct se_cmd, work);
	struct se_device *dev = cmd->se_dev;
	struct se_tmr_req *tmr = cmd->se_tmr_req;
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&cmd->t_state_lock, flags);
	if (cmd->transport_state & CMD_T_ABORTED) {
		tmr->response = TMR_FUNCTION_REJECTED;
		spin_unlock_irqrestore(&cmd->t_state_lock, flags);
		goto check_stop;
	}
	spin_unlock_irqrestore(&cmd->t_state_lock, flags);

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
		if (tmr->response == TMR_FUNCTION_COMPLETE) {
			target_ua_allocate_lun(cmd->se_sess->se_node_acl,
					       cmd->orig_fe_lun, 0x29,
					       ASCQ_29H_BUS_DEVICE_RESET_FUNCTION_OCCURRED);
		}
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

	spin_lock_irqsave(&cmd->t_state_lock, flags);
	if (cmd->transport_state & CMD_T_ABORTED) {
		spin_unlock_irqrestore(&cmd->t_state_lock, flags);
		goto check_stop;
	}
	spin_unlock_irqrestore(&cmd->t_state_lock, flags);

	cmd->se_tfo->queue_tm_rsp(cmd);

check_stop:
	transport_cmd_check_stop_to_fabric(cmd);
}

int transport_generic_handle_tmr(
	struct se_cmd *cmd)
{
	unsigned long flags;
	bool aborted = false;

	spin_lock_irqsave(&cmd->t_state_lock, flags);
	if (cmd->transport_state & CMD_T_ABORTED) {
		aborted = true;
	} else {
		cmd->t_state = TRANSPORT_ISTATE_PROCESSING;
		cmd->transport_state |= CMD_T_ACTIVE;
	}
	spin_unlock_irqrestore(&cmd->t_state_lock, flags);

	if (aborted) {
		pr_warn_ratelimited("handle_tmr caught CMD_T_ABORTED TMR %d"
			"ref_tag: %llu tag: %llu\n", cmd->se_tmr_req->function,
			cmd->se_tmr_req->ref_task_tag, cmd->tag);
		transport_cmd_check_stop_to_fabric(cmd);
		return 0;
	}

	INIT_WORK(&cmd->work, target_tmr_work);
	queue_work(cmd->se_dev->tmr_wq, &cmd->work);
	return 0;
}
EXPORT_SYMBOL(transport_generic_handle_tmr);

bool
target_check_wce(struct se_device *dev)
{
	bool wce = false;

	if (dev->transport->get_write_cache)
		wce = dev->transport->get_write_cache(dev);
	else if (dev->dev_attrib.emulate_write_cache > 0)
		wce = true;

	return wce;
}

bool
target_check_fua(struct se_device *dev)
{
	return target_check_wce(dev) && dev->dev_attrib.emulate_fua_write > 0;
}

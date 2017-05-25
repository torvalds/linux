/*******************************************************************************
 * Filename:  target_core_tpg.c
 *
 * This file contains generic Target Portal Group related functions.
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
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/in.h>
#include <linux/export.h>
#include <net/sock.h>
#include <net/tcp.h>
#include <scsi/scsi_proto.h>

#include <target/target_core_base.h>
#include <target/target_core_backend.h>
#include <target/target_core_fabric.h>

#include "target_core_internal.h"
#include "target_core_alua.h"
#include "target_core_pr.h"
#include "target_core_ua.h"

extern struct se_device *g_lun0_dev;

static DEFINE_SPINLOCK(tpg_lock);
static LIST_HEAD(tpg_list);

/*	__core_tpg_get_initiator_node_acl():
 *
 *	mutex_lock(&tpg->acl_node_mutex); must be held when calling
 */
struct se_node_acl *__core_tpg_get_initiator_node_acl(
	struct se_portal_group *tpg,
	const char *initiatorname)
{
	struct se_node_acl *acl;

	list_for_each_entry(acl, &tpg->acl_node_list, acl_list) {
		if (!strcmp(acl->initiatorname, initiatorname))
			return acl;
	}

	return NULL;
}

/*	core_tpg_get_initiator_node_acl():
 *
 *
 */
struct se_node_acl *core_tpg_get_initiator_node_acl(
	struct se_portal_group *tpg,
	unsigned char *initiatorname)
{
	struct se_node_acl *acl;
	/*
	 * Obtain se_node_acl->acl_kref using fabric driver provided
	 * initiatorname[] during node acl endpoint lookup driven by
	 * new se_session login.
	 *
	 * The reference is held until se_session shutdown -> release
	 * occurs via fabric driver invoked transport_deregister_session()
	 * or transport_free_session() code.
	 */
	mutex_lock(&tpg->acl_node_mutex);
	acl = __core_tpg_get_initiator_node_acl(tpg, initiatorname);
	if (acl) {
		if (!kref_get_unless_zero(&acl->acl_kref))
			acl = NULL;
	}
	mutex_unlock(&tpg->acl_node_mutex);

	return acl;
}
EXPORT_SYMBOL(core_tpg_get_initiator_node_acl);

void core_allocate_nexus_loss_ua(
	struct se_node_acl *nacl)
{
	struct se_dev_entry *deve;

	if (!nacl)
		return;

	rcu_read_lock();
	hlist_for_each_entry_rcu(deve, &nacl->lun_entry_hlist, link)
		core_scsi3_ua_allocate(deve, 0x29,
			ASCQ_29H_NEXUS_LOSS_OCCURRED);
	rcu_read_unlock();
}
EXPORT_SYMBOL(core_allocate_nexus_loss_ua);

/*	core_tpg_add_node_to_devs():
 *
 *
 */
void core_tpg_add_node_to_devs(
	struct se_node_acl *acl,
	struct se_portal_group *tpg,
	struct se_lun *lun_orig)
{
	u32 lun_access = 0;
	struct se_lun *lun;
	struct se_device *dev;

	mutex_lock(&tpg->tpg_lun_mutex);
	hlist_for_each_entry_rcu(lun, &tpg->tpg_lun_hlist, link) {
		if (lun_orig && lun != lun_orig)
			continue;

		dev = rcu_dereference_check(lun->lun_se_dev,
					    lockdep_is_held(&tpg->tpg_lun_mutex));
		/*
		 * By default in LIO-Target $FABRIC_MOD,
		 * demo_mode_write_protect is ON, or READ_ONLY;
		 */
		if (!tpg->se_tpg_tfo->tpg_check_demo_mode_write_protect(tpg)) {
			lun_access = TRANSPORT_LUNFLAGS_READ_WRITE;
		} else {
			/*
			 * Allow only optical drives to issue R/W in default RO
			 * demo mode.
			 */
			if (dev->transport->get_device_type(dev) == TYPE_DISK)
				lun_access = TRANSPORT_LUNFLAGS_READ_ONLY;
			else
				lun_access = TRANSPORT_LUNFLAGS_READ_WRITE;
		}

		pr_debug("TARGET_CORE[%s]->TPG[%u]_LUN[%llu] - Adding %s"
			" access for LUN in Demo Mode\n",
			tpg->se_tpg_tfo->get_fabric_name(),
			tpg->se_tpg_tfo->tpg_get_tag(tpg), lun->unpacked_lun,
			(lun_access == TRANSPORT_LUNFLAGS_READ_WRITE) ?
			"READ-WRITE" : "READ-ONLY");

		core_enable_device_list_for_node(lun, NULL, lun->unpacked_lun,
						 lun_access, acl, tpg);
		/*
		 * Check to see if there are any existing persistent reservation
		 * APTPL pre-registrations that need to be enabled for this dynamic
		 * LUN ACL now..
		 */
		core_scsi3_check_aptpl_registration(dev, tpg, lun, acl,
						    lun->unpacked_lun);
	}
	mutex_unlock(&tpg->tpg_lun_mutex);
}

static void
target_set_nacl_queue_depth(struct se_portal_group *tpg,
			    struct se_node_acl *acl, u32 queue_depth)
{
	acl->queue_depth = queue_depth;

	if (!acl->queue_depth) {
		pr_warn("Queue depth for %s Initiator Node: %s is 0,"
			"defaulting to 1.\n", tpg->se_tpg_tfo->get_fabric_name(),
			acl->initiatorname);
		acl->queue_depth = 1;
	}
}

static struct se_node_acl *target_alloc_node_acl(struct se_portal_group *tpg,
		const unsigned char *initiatorname)
{
	struct se_node_acl *acl;
	u32 queue_depth;

	acl = kzalloc(max(sizeof(*acl), tpg->se_tpg_tfo->node_acl_size),
			GFP_KERNEL);
	if (!acl)
		return NULL;

	INIT_LIST_HEAD(&acl->acl_list);
	INIT_LIST_HEAD(&acl->acl_sess_list);
	INIT_HLIST_HEAD(&acl->lun_entry_hlist);
	kref_init(&acl->acl_kref);
	init_completion(&acl->acl_free_comp);
	spin_lock_init(&acl->nacl_sess_lock);
	mutex_init(&acl->lun_entry_mutex);
	atomic_set(&acl->acl_pr_ref_count, 0);

	if (tpg->se_tpg_tfo->tpg_get_default_depth)
		queue_depth = tpg->se_tpg_tfo->tpg_get_default_depth(tpg);
	else
		queue_depth = 1;
	target_set_nacl_queue_depth(tpg, acl, queue_depth);

	snprintf(acl->initiatorname, TRANSPORT_IQN_LEN, "%s", initiatorname);
	acl->se_tpg = tpg;
	acl->acl_index = scsi_get_new_index(SCSI_AUTH_INTR_INDEX);

	tpg->se_tpg_tfo->set_default_node_attributes(acl);

	return acl;
}

static void target_add_node_acl(struct se_node_acl *acl)
{
	struct se_portal_group *tpg = acl->se_tpg;

	mutex_lock(&tpg->acl_node_mutex);
	list_add_tail(&acl->acl_list, &tpg->acl_node_list);
	tpg->num_node_acls++;
	mutex_unlock(&tpg->acl_node_mutex);

	pr_debug("%s_TPG[%hu] - Added %s ACL with TCQ Depth: %d for %s"
		" Initiator Node: %s\n",
		tpg->se_tpg_tfo->get_fabric_name(),
		tpg->se_tpg_tfo->tpg_get_tag(tpg),
		acl->dynamic_node_acl ? "DYNAMIC" : "",
		acl->queue_depth,
		tpg->se_tpg_tfo->get_fabric_name(),
		acl->initiatorname);
}

bool target_tpg_has_node_acl(struct se_portal_group *tpg,
			     const char *initiatorname)
{
	struct se_node_acl *acl;
	bool found = false;

	mutex_lock(&tpg->acl_node_mutex);
	list_for_each_entry(acl, &tpg->acl_node_list, acl_list) {
		if (!strcmp(acl->initiatorname, initiatorname)) {
			found = true;
			break;
		}
	}
	mutex_unlock(&tpg->acl_node_mutex);

	return found;
}
EXPORT_SYMBOL(target_tpg_has_node_acl);

struct se_node_acl *core_tpg_check_initiator_node_acl(
	struct se_portal_group *tpg,
	unsigned char *initiatorname)
{
	struct se_node_acl *acl;

	acl = core_tpg_get_initiator_node_acl(tpg, initiatorname);
	if (acl)
		return acl;

	if (!tpg->se_tpg_tfo->tpg_check_demo_mode(tpg))
		return NULL;

	acl = target_alloc_node_acl(tpg, initiatorname);
	if (!acl)
		return NULL;
	/*
	 * When allocating a dynamically generated node_acl, go ahead
	 * and take the extra kref now before returning to the fabric
	 * driver caller.
	 *
	 * Note this reference will be released at session shutdown
	 * time within transport_free_session() code.
	 */
	kref_get(&acl->acl_kref);
	acl->dynamic_node_acl = 1;

	/*
	 * Here we only create demo-mode MappedLUNs from the active
	 * TPG LUNs if the fabric is not explicitly asking for
	 * tpg_check_demo_mode_login_only() == 1.
	 */
	if ((tpg->se_tpg_tfo->tpg_check_demo_mode_login_only == NULL) ||
	    (tpg->se_tpg_tfo->tpg_check_demo_mode_login_only(tpg) != 1))
		core_tpg_add_node_to_devs(acl, tpg, NULL);

	target_add_node_acl(acl);
	return acl;
}
EXPORT_SYMBOL(core_tpg_check_initiator_node_acl);

void core_tpg_wait_for_nacl_pr_ref(struct se_node_acl *nacl)
{
	while (atomic_read(&nacl->acl_pr_ref_count) != 0)
		cpu_relax();
}

struct se_node_acl *core_tpg_add_initiator_node_acl(
	struct se_portal_group *tpg,
	const char *initiatorname)
{
	struct se_node_acl *acl;

	mutex_lock(&tpg->acl_node_mutex);
	acl = __core_tpg_get_initiator_node_acl(tpg, initiatorname);
	if (acl) {
		if (acl->dynamic_node_acl) {
			acl->dynamic_node_acl = 0;
			pr_debug("%s_TPG[%u] - Replacing dynamic ACL"
				" for %s\n", tpg->se_tpg_tfo->get_fabric_name(),
				tpg->se_tpg_tfo->tpg_get_tag(tpg), initiatorname);
			mutex_unlock(&tpg->acl_node_mutex);
			return acl;
		}

		pr_err("ACL entry for %s Initiator"
			" Node %s already exists for TPG %u, ignoring"
			" request.\n",  tpg->se_tpg_tfo->get_fabric_name(),
			initiatorname, tpg->se_tpg_tfo->tpg_get_tag(tpg));
		mutex_unlock(&tpg->acl_node_mutex);
		return ERR_PTR(-EEXIST);
	}
	mutex_unlock(&tpg->acl_node_mutex);

	acl = target_alloc_node_acl(tpg, initiatorname);
	if (!acl)
		return ERR_PTR(-ENOMEM);

	target_add_node_acl(acl);
	return acl;
}

void core_tpg_del_initiator_node_acl(struct se_node_acl *acl)
{
	struct se_portal_group *tpg = acl->se_tpg;
	LIST_HEAD(sess_list);
	struct se_session *sess, *sess_tmp;
	unsigned long flags;
	int rc;

	mutex_lock(&tpg->acl_node_mutex);
	if (acl->dynamic_node_acl) {
		acl->dynamic_node_acl = 0;
	}
	list_del(&acl->acl_list);
	tpg->num_node_acls--;
	mutex_unlock(&tpg->acl_node_mutex);

	spin_lock_irqsave(&acl->nacl_sess_lock, flags);
	acl->acl_stop = 1;

	list_for_each_entry_safe(sess, sess_tmp, &acl->acl_sess_list,
				sess_acl_list) {
		if (sess->sess_tearing_down != 0)
			continue;

		if (!target_get_session(sess))
			continue;
		list_move(&sess->sess_acl_list, &sess_list);
	}
	spin_unlock_irqrestore(&acl->nacl_sess_lock, flags);

	list_for_each_entry_safe(sess, sess_tmp, &sess_list, sess_acl_list) {
		list_del(&sess->sess_acl_list);

		rc = tpg->se_tpg_tfo->shutdown_session(sess);
		target_put_session(sess);
		if (!rc)
			continue;
		target_put_session(sess);
	}
	target_put_nacl(acl);
	/*
	 * Wait for last target_put_nacl() to complete in target_complete_nacl()
	 * for active fabric session transport_deregister_session() callbacks.
	 */
	wait_for_completion(&acl->acl_free_comp);

	core_tpg_wait_for_nacl_pr_ref(acl);
	core_free_device_list_for_node(acl, tpg);

	pr_debug("%s_TPG[%hu] - Deleted ACL with TCQ Depth: %d for %s"
		" Initiator Node: %s\n", tpg->se_tpg_tfo->get_fabric_name(),
		tpg->se_tpg_tfo->tpg_get_tag(tpg), acl->queue_depth,
		tpg->se_tpg_tfo->get_fabric_name(), acl->initiatorname);

	kfree(acl);
}

/*	core_tpg_set_initiator_node_queue_depth():
 *
 *
 */
int core_tpg_set_initiator_node_queue_depth(
	struct se_node_acl *acl,
	u32 queue_depth)
{
	LIST_HEAD(sess_list);
	struct se_portal_group *tpg = acl->se_tpg;
	struct se_session *sess, *sess_tmp;
	unsigned long flags;
	int rc;

	/*
	 * User has requested to change the queue depth for a Initiator Node.
	 * Change the value in the Node's struct se_node_acl, and call
	 * target_set_nacl_queue_depth() to set the new queue depth.
	 */
	target_set_nacl_queue_depth(tpg, acl, queue_depth);

	spin_lock_irqsave(&acl->nacl_sess_lock, flags);
	list_for_each_entry_safe(sess, sess_tmp, &acl->acl_sess_list,
				 sess_acl_list) {
		if (sess->sess_tearing_down != 0)
			continue;
		if (!target_get_session(sess))
			continue;
		spin_unlock_irqrestore(&acl->nacl_sess_lock, flags);

		/*
		 * Finally call tpg->se_tpg_tfo->close_session() to force session
		 * reinstatement to occur if there is an active session for the
		 * $FABRIC_MOD Initiator Node in question.
		 */
		rc = tpg->se_tpg_tfo->shutdown_session(sess);
		target_put_session(sess);
		if (!rc) {
			spin_lock_irqsave(&acl->nacl_sess_lock, flags);
			continue;
		}
		target_put_session(sess);
		spin_lock_irqsave(&acl->nacl_sess_lock, flags);
	}
	spin_unlock_irqrestore(&acl->nacl_sess_lock, flags);

	pr_debug("Successfully changed queue depth to: %d for Initiator"
		" Node: %s on %s Target Portal Group: %u\n", acl->queue_depth,
		acl->initiatorname, tpg->se_tpg_tfo->get_fabric_name(),
		tpg->se_tpg_tfo->tpg_get_tag(tpg));

	return 0;
}
EXPORT_SYMBOL(core_tpg_set_initiator_node_queue_depth);

/*	core_tpg_set_initiator_node_tag():
 *
 *	Initiator nodeacl tags are not used internally, but may be used by
 *	userspace to emulate aliases or groups.
 *	Returns length of newly-set tag or -EINVAL.
 */
int core_tpg_set_initiator_node_tag(
	struct se_portal_group *tpg,
	struct se_node_acl *acl,
	const char *new_tag)
{
	if (strlen(new_tag) >= MAX_ACL_TAG_SIZE)
		return -EINVAL;

	if (!strncmp("NULL", new_tag, 4)) {
		acl->acl_tag[0] = '\0';
		return 0;
	}

	return snprintf(acl->acl_tag, MAX_ACL_TAG_SIZE, "%s", new_tag);
}
EXPORT_SYMBOL(core_tpg_set_initiator_node_tag);

static void core_tpg_lun_ref_release(struct percpu_ref *ref)
{
	struct se_lun *lun = container_of(ref, struct se_lun, lun_ref);

	complete(&lun->lun_shutdown_comp);
}

int core_tpg_register(
	struct se_wwn *se_wwn,
	struct se_portal_group *se_tpg,
	int proto_id)
{
	int ret;

	if (!se_tpg)
		return -EINVAL;
	/*
	 * For the typical case where core_tpg_register() is called by a
	 * fabric driver from target_core_fabric_ops->fabric_make_tpg()
	 * configfs context, use the original tf_ops pointer already saved
	 * by target-core in target_fabric_make_wwn().
	 *
	 * Otherwise, for special cases like iscsi-target discovery TPGs
	 * the caller is responsible for setting ->se_tpg_tfo ahead of
	 * calling core_tpg_register().
	 */
	if (se_wwn)
		se_tpg->se_tpg_tfo = se_wwn->wwn_tf->tf_ops;

	if (!se_tpg->se_tpg_tfo) {
		pr_err("Unable to locate se_tpg->se_tpg_tfo pointer\n");
		return -EINVAL;
	}

	INIT_HLIST_HEAD(&se_tpg->tpg_lun_hlist);
	se_tpg->proto_id = proto_id;
	se_tpg->se_tpg_wwn = se_wwn;
	atomic_set(&se_tpg->tpg_pr_ref_count, 0);
	INIT_LIST_HEAD(&se_tpg->acl_node_list);
	INIT_LIST_HEAD(&se_tpg->se_tpg_node);
	INIT_LIST_HEAD(&se_tpg->tpg_sess_list);
	spin_lock_init(&se_tpg->session_lock);
	mutex_init(&se_tpg->tpg_lun_mutex);
	mutex_init(&se_tpg->acl_node_mutex);

	if (se_tpg->proto_id >= 0) {
		se_tpg->tpg_virt_lun0 = core_tpg_alloc_lun(se_tpg, 0);
		if (IS_ERR(se_tpg->tpg_virt_lun0))
			return PTR_ERR(se_tpg->tpg_virt_lun0);

		ret = core_tpg_add_lun(se_tpg, se_tpg->tpg_virt_lun0,
				TRANSPORT_LUNFLAGS_READ_ONLY, g_lun0_dev);
		if (ret < 0) {
			kfree(se_tpg->tpg_virt_lun0);
			return ret;
		}
	}

	spin_lock_bh(&tpg_lock);
	list_add_tail(&se_tpg->se_tpg_node, &tpg_list);
	spin_unlock_bh(&tpg_lock);

	pr_debug("TARGET_CORE[%s]: Allocated portal_group for endpoint: %s, "
		 "Proto: %d, Portal Tag: %u\n", se_tpg->se_tpg_tfo->get_fabric_name(),
		se_tpg->se_tpg_tfo->tpg_get_wwn(se_tpg) ?
		se_tpg->se_tpg_tfo->tpg_get_wwn(se_tpg) : NULL,
		se_tpg->proto_id, se_tpg->se_tpg_tfo->tpg_get_tag(se_tpg));

	return 0;
}
EXPORT_SYMBOL(core_tpg_register);

int core_tpg_deregister(struct se_portal_group *se_tpg)
{
	const struct target_core_fabric_ops *tfo = se_tpg->se_tpg_tfo;
	struct se_node_acl *nacl, *nacl_tmp;
	LIST_HEAD(node_list);

	pr_debug("TARGET_CORE[%s]: Deallocating portal_group for endpoint: %s, "
		 "Proto: %d, Portal Tag: %u\n", tfo->get_fabric_name(),
		tfo->tpg_get_wwn(se_tpg) ? tfo->tpg_get_wwn(se_tpg) : NULL,
		se_tpg->proto_id, tfo->tpg_get_tag(se_tpg));

	spin_lock_bh(&tpg_lock);
	list_del(&se_tpg->se_tpg_node);
	spin_unlock_bh(&tpg_lock);

	while (atomic_read(&se_tpg->tpg_pr_ref_count) != 0)
		cpu_relax();

	mutex_lock(&se_tpg->acl_node_mutex);
	list_splice_init(&se_tpg->acl_node_list, &node_list);
	mutex_unlock(&se_tpg->acl_node_mutex);
	/*
	 * Release any remaining demo-mode generated se_node_acl that have
	 * not been released because of TFO->tpg_check_demo_mode_cache() == 1
	 * in transport_deregister_session().
	 */
	list_for_each_entry_safe(nacl, nacl_tmp, &node_list, acl_list) {
		list_del(&nacl->acl_list);
		se_tpg->num_node_acls--;

		core_tpg_wait_for_nacl_pr_ref(nacl);
		core_free_device_list_for_node(nacl, se_tpg);
		kfree(nacl);
	}

	if (se_tpg->proto_id >= 0) {
		core_tpg_remove_lun(se_tpg, se_tpg->tpg_virt_lun0);
		kfree_rcu(se_tpg->tpg_virt_lun0, rcu_head);
	}

	return 0;
}
EXPORT_SYMBOL(core_tpg_deregister);

struct se_lun *core_tpg_alloc_lun(
	struct se_portal_group *tpg,
	u64 unpacked_lun)
{
	struct se_lun *lun;

	lun = kzalloc(sizeof(*lun), GFP_KERNEL);
	if (!lun) {
		pr_err("Unable to allocate se_lun memory\n");
		return ERR_PTR(-ENOMEM);
	}
	lun->unpacked_lun = unpacked_lun;
	lun->lun_link_magic = SE_LUN_LINK_MAGIC;
	atomic_set(&lun->lun_acl_count, 0);
	init_completion(&lun->lun_ref_comp);
	init_completion(&lun->lun_shutdown_comp);
	INIT_LIST_HEAD(&lun->lun_deve_list);
	INIT_LIST_HEAD(&lun->lun_dev_link);
	atomic_set(&lun->lun_tg_pt_secondary_offline, 0);
	spin_lock_init(&lun->lun_deve_lock);
	mutex_init(&lun->lun_tg_pt_md_mutex);
	INIT_LIST_HEAD(&lun->lun_tg_pt_gp_link);
	spin_lock_init(&lun->lun_tg_pt_gp_lock);
	lun->lun_tpg = tpg;

	return lun;
}

int core_tpg_add_lun(
	struct se_portal_group *tpg,
	struct se_lun *lun,
	u32 lun_access,
	struct se_device *dev)
{
	int ret;

	ret = percpu_ref_init(&lun->lun_ref, core_tpg_lun_ref_release, 0,
			      GFP_KERNEL);
	if (ret < 0)
		goto out;

	ret = core_alloc_rtpi(lun, dev);
	if (ret)
		goto out_kill_ref;

	if (!(dev->transport->transport_flags & TRANSPORT_FLAG_PASSTHROUGH) &&
	    !(dev->se_hba->hba_flags & HBA_FLAGS_INTERNAL_USE))
		target_attach_tg_pt_gp(lun, dev->t10_alua.default_tg_pt_gp);

	mutex_lock(&tpg->tpg_lun_mutex);

	spin_lock(&dev->se_port_lock);
	lun->lun_index = dev->dev_index;
	rcu_assign_pointer(lun->lun_se_dev, dev);
	dev->export_count++;
	list_add_tail(&lun->lun_dev_link, &dev->dev_sep_list);
	spin_unlock(&dev->se_port_lock);

	if (dev->dev_flags & DF_READ_ONLY)
		lun->lun_access = TRANSPORT_LUNFLAGS_READ_ONLY;
	else
		lun->lun_access = lun_access;
	if (!(dev->se_hba->hba_flags & HBA_FLAGS_INTERNAL_USE))
		hlist_add_head_rcu(&lun->link, &tpg->tpg_lun_hlist);
	mutex_unlock(&tpg->tpg_lun_mutex);

	return 0;

out_kill_ref:
	percpu_ref_exit(&lun->lun_ref);
out:
	return ret;
}

void core_tpg_remove_lun(
	struct se_portal_group *tpg,
	struct se_lun *lun)
{
	/*
	 * rcu_dereference_raw protected by se_lun->lun_group symlink
	 * reference to se_device->dev_group.
	 */
	struct se_device *dev = rcu_dereference_raw(lun->lun_se_dev);

	core_clear_lun_from_tpg(lun, tpg);
	/*
	 * Wait for any active I/O references to percpu se_lun->lun_ref to
	 * be released.  Also, se_lun->lun_ref is now used by PR and ALUA
	 * logic when referencing a remote target port during ALL_TGT_PT=1
	 * and generating UNIT_ATTENTIONs for ALUA access state transition.
	 */
	transport_clear_lun_ref(lun);

	mutex_lock(&tpg->tpg_lun_mutex);
	if (lun->lun_se_dev) {
		target_detach_tg_pt_gp(lun);

		spin_lock(&dev->se_port_lock);
		list_del(&lun->lun_dev_link);
		dev->export_count--;
		rcu_assign_pointer(lun->lun_se_dev, NULL);
		spin_unlock(&dev->se_port_lock);
	}
	if (!(dev->se_hba->hba_flags & HBA_FLAGS_INTERNAL_USE))
		hlist_del_rcu(&lun->link);
	mutex_unlock(&tpg->tpg_lun_mutex);

	percpu_ref_exit(&lun->lun_ref);
}

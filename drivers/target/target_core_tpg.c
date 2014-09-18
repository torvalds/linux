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
#include <scsi/scsi.h>
#include <scsi/scsi_cmnd.h>

#include <target/target_core_base.h>
#include <target/target_core_backend.h>
#include <target/target_core_fabric.h>

#include "target_core_internal.h"

extern struct se_device *g_lun0_dev;

static DEFINE_SPINLOCK(tpg_lock);
static LIST_HEAD(tpg_list);

/*	core_clear_initiator_node_from_tpg():
 *
 *
 */
static void core_clear_initiator_node_from_tpg(
	struct se_node_acl *nacl,
	struct se_portal_group *tpg)
{
	int i;
	struct se_dev_entry *deve;
	struct se_lun *lun;

	spin_lock_irq(&nacl->device_list_lock);
	for (i = 0; i < TRANSPORT_MAX_LUNS_PER_TPG; i++) {
		deve = nacl->device_list[i];

		if (!(deve->lun_flags & TRANSPORT_LUNFLAGS_INITIATOR_ACCESS))
			continue;

		if (!deve->se_lun) {
			pr_err("%s device entries device pointer is"
				" NULL, but Initiator has access.\n",
				tpg->se_tpg_tfo->get_fabric_name());
			continue;
		}

		lun = deve->se_lun;
		spin_unlock_irq(&nacl->device_list_lock);
		core_disable_device_list_for_node(lun, NULL, deve->mapped_lun,
			TRANSPORT_LUNFLAGS_NO_ACCESS, nacl, tpg);

		spin_lock_irq(&nacl->device_list_lock);
	}
	spin_unlock_irq(&nacl->device_list_lock);
}

/*	__core_tpg_get_initiator_node_acl():
 *
 *	spin_lock_bh(&tpg->acl_node_lock); must be held when calling
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

	spin_lock_irq(&tpg->acl_node_lock);
	acl = __core_tpg_get_initiator_node_acl(tpg, initiatorname);
	spin_unlock_irq(&tpg->acl_node_lock);

	return acl;
}
EXPORT_SYMBOL(core_tpg_get_initiator_node_acl);

/*	core_tpg_add_node_to_devs():
 *
 *
 */
void core_tpg_add_node_to_devs(
	struct se_node_acl *acl,
	struct se_portal_group *tpg)
{
	int i = 0;
	u32 lun_access = 0;
	struct se_lun *lun;
	struct se_device *dev;

	spin_lock(&tpg->tpg_lun_lock);
	for (i = 0; i < TRANSPORT_MAX_LUNS_PER_TPG; i++) {
		lun = tpg->tpg_lun_list[i];
		if (lun->lun_status != TRANSPORT_LUN_STATUS_ACTIVE)
			continue;

		spin_unlock(&tpg->tpg_lun_lock);

		dev = lun->lun_se_dev;
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

		pr_debug("TARGET_CORE[%s]->TPG[%u]_LUN[%u] - Adding %s"
			" access for LUN in Demo Mode\n",
			tpg->se_tpg_tfo->get_fabric_name(),
			tpg->se_tpg_tfo->tpg_get_tag(tpg), lun->unpacked_lun,
			(lun_access == TRANSPORT_LUNFLAGS_READ_WRITE) ?
			"READ-WRITE" : "READ-ONLY");

		core_enable_device_list_for_node(lun, NULL, lun->unpacked_lun,
				lun_access, acl, tpg);
		spin_lock(&tpg->tpg_lun_lock);
	}
	spin_unlock(&tpg->tpg_lun_lock);
}

/*      core_set_queue_depth_for_node():
 *
 *
 */
static int core_set_queue_depth_for_node(
	struct se_portal_group *tpg,
	struct se_node_acl *acl)
{
	if (!acl->queue_depth) {
		pr_err("Queue depth for %s Initiator Node: %s is 0,"
			"defaulting to 1.\n", tpg->se_tpg_tfo->get_fabric_name(),
			acl->initiatorname);
		acl->queue_depth = 1;
	}

	return 0;
}

void array_free(void *array, int n)
{
	void **a = array;
	int i;

	for (i = 0; i < n; i++)
		kfree(a[i]);
	kfree(a);
}

static void *array_zalloc(int n, size_t size, gfp_t flags)
{
	void **a;
	int i;

	a = kzalloc(n * sizeof(void*), flags);
	if (!a)
		return NULL;
	for (i = 0; i < n; i++) {
		a[i] = kzalloc(size, flags);
		if (!a[i]) {
			array_free(a, n);
			return NULL;
		}
	}
	return a;
}

/*      core_create_device_list_for_node():
 *
 *
 */
static int core_create_device_list_for_node(struct se_node_acl *nacl)
{
	struct se_dev_entry *deve;
	int i;

	nacl->device_list = array_zalloc(TRANSPORT_MAX_LUNS_PER_TPG,
			sizeof(struct se_dev_entry), GFP_KERNEL);
	if (!nacl->device_list) {
		pr_err("Unable to allocate memory for"
			" struct se_node_acl->device_list\n");
		return -ENOMEM;
	}
	for (i = 0; i < TRANSPORT_MAX_LUNS_PER_TPG; i++) {
		deve = nacl->device_list[i];

		atomic_set(&deve->ua_count, 0);
		atomic_set(&deve->pr_ref_count, 0);
		spin_lock_init(&deve->ua_lock);
		INIT_LIST_HEAD(&deve->alua_port_list);
		INIT_LIST_HEAD(&deve->ua_list);
	}

	return 0;
}

/*	core_tpg_check_initiator_node_acl()
 *
 *
 */
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

	acl =  tpg->se_tpg_tfo->tpg_alloc_fabric_acl(tpg);
	if (!acl)
		return NULL;

	INIT_LIST_HEAD(&acl->acl_list);
	INIT_LIST_HEAD(&acl->acl_sess_list);
	kref_init(&acl->acl_kref);
	init_completion(&acl->acl_free_comp);
	spin_lock_init(&acl->device_list_lock);
	spin_lock_init(&acl->nacl_sess_lock);
	atomic_set(&acl->acl_pr_ref_count, 0);
	acl->queue_depth = tpg->se_tpg_tfo->tpg_get_default_depth(tpg);
	snprintf(acl->initiatorname, TRANSPORT_IQN_LEN, "%s", initiatorname);
	acl->se_tpg = tpg;
	acl->acl_index = scsi_get_new_index(SCSI_AUTH_INTR_INDEX);
	acl->dynamic_node_acl = 1;

	tpg->se_tpg_tfo->set_default_node_attributes(acl);

	if (core_create_device_list_for_node(acl) < 0) {
		tpg->se_tpg_tfo->tpg_release_fabric_acl(tpg, acl);
		return NULL;
	}

	if (core_set_queue_depth_for_node(tpg, acl) < 0) {
		core_free_device_list_for_node(acl, tpg);
		tpg->se_tpg_tfo->tpg_release_fabric_acl(tpg, acl);
		return NULL;
	}
	/*
	 * Here we only create demo-mode MappedLUNs from the active
	 * TPG LUNs if the fabric is not explicitly asking for
	 * tpg_check_demo_mode_login_only() == 1.
	 */
	if ((tpg->se_tpg_tfo->tpg_check_demo_mode_login_only == NULL) ||
	    (tpg->se_tpg_tfo->tpg_check_demo_mode_login_only(tpg) != 1))
		core_tpg_add_node_to_devs(acl, tpg);

	spin_lock_irq(&tpg->acl_node_lock);
	list_add_tail(&acl->acl_list, &tpg->acl_node_list);
	tpg->num_node_acls++;
	spin_unlock_irq(&tpg->acl_node_lock);

	pr_debug("%s_TPG[%u] - Added DYNAMIC ACL with TCQ Depth: %d for %s"
		" Initiator Node: %s\n", tpg->se_tpg_tfo->get_fabric_name(),
		tpg->se_tpg_tfo->tpg_get_tag(tpg), acl->queue_depth,
		tpg->se_tpg_tfo->get_fabric_name(), initiatorname);

	return acl;
}
EXPORT_SYMBOL(core_tpg_check_initiator_node_acl);

void core_tpg_wait_for_nacl_pr_ref(struct se_node_acl *nacl)
{
	while (atomic_read(&nacl->acl_pr_ref_count) != 0)
		cpu_relax();
}

void core_tpg_clear_object_luns(struct se_portal_group *tpg)
{
	int i;
	struct se_lun *lun;

	spin_lock(&tpg->tpg_lun_lock);
	for (i = 0; i < TRANSPORT_MAX_LUNS_PER_TPG; i++) {
		lun = tpg->tpg_lun_list[i];

		if ((lun->lun_status != TRANSPORT_LUN_STATUS_ACTIVE) ||
		    (lun->lun_se_dev == NULL))
			continue;

		spin_unlock(&tpg->tpg_lun_lock);
		core_dev_del_lun(tpg, lun->unpacked_lun);
		spin_lock(&tpg->tpg_lun_lock);
	}
	spin_unlock(&tpg->tpg_lun_lock);
}
EXPORT_SYMBOL(core_tpg_clear_object_luns);

/*	core_tpg_add_initiator_node_acl():
 *
 *
 */
struct se_node_acl *core_tpg_add_initiator_node_acl(
	struct se_portal_group *tpg,
	struct se_node_acl *se_nacl,
	const char *initiatorname,
	u32 queue_depth)
{
	struct se_node_acl *acl = NULL;

	spin_lock_irq(&tpg->acl_node_lock);
	acl = __core_tpg_get_initiator_node_acl(tpg, initiatorname);
	if (acl) {
		if (acl->dynamic_node_acl) {
			acl->dynamic_node_acl = 0;
			pr_debug("%s_TPG[%u] - Replacing dynamic ACL"
				" for %s\n", tpg->se_tpg_tfo->get_fabric_name(),
				tpg->se_tpg_tfo->tpg_get_tag(tpg), initiatorname);
			spin_unlock_irq(&tpg->acl_node_lock);
			/*
			 * Release the locally allocated struct se_node_acl
			 * because * core_tpg_add_initiator_node_acl() returned
			 * a pointer to an existing demo mode node ACL.
			 */
			if (se_nacl)
				tpg->se_tpg_tfo->tpg_release_fabric_acl(tpg,
							se_nacl);
			goto done;
		}

		pr_err("ACL entry for %s Initiator"
			" Node %s already exists for TPG %u, ignoring"
			" request.\n",  tpg->se_tpg_tfo->get_fabric_name(),
			initiatorname, tpg->se_tpg_tfo->tpg_get_tag(tpg));
		spin_unlock_irq(&tpg->acl_node_lock);
		return ERR_PTR(-EEXIST);
	}
	spin_unlock_irq(&tpg->acl_node_lock);

	if (!se_nacl) {
		pr_err("struct se_node_acl pointer is NULL\n");
		return ERR_PTR(-EINVAL);
	}
	/*
	 * For v4.x logic the se_node_acl_s is hanging off a fabric
	 * dependent structure allocated via
	 * struct target_core_fabric_ops->fabric_make_nodeacl()
	 */
	acl = se_nacl;

	INIT_LIST_HEAD(&acl->acl_list);
	INIT_LIST_HEAD(&acl->acl_sess_list);
	kref_init(&acl->acl_kref);
	init_completion(&acl->acl_free_comp);
	spin_lock_init(&acl->device_list_lock);
	spin_lock_init(&acl->nacl_sess_lock);
	atomic_set(&acl->acl_pr_ref_count, 0);
	acl->queue_depth = queue_depth;
	snprintf(acl->initiatorname, TRANSPORT_IQN_LEN, "%s", initiatorname);
	acl->se_tpg = tpg;
	acl->acl_index = scsi_get_new_index(SCSI_AUTH_INTR_INDEX);

	tpg->se_tpg_tfo->set_default_node_attributes(acl);

	if (core_create_device_list_for_node(acl) < 0) {
		tpg->se_tpg_tfo->tpg_release_fabric_acl(tpg, acl);
		return ERR_PTR(-ENOMEM);
	}

	if (core_set_queue_depth_for_node(tpg, acl) < 0) {
		core_free_device_list_for_node(acl, tpg);
		tpg->se_tpg_tfo->tpg_release_fabric_acl(tpg, acl);
		return ERR_PTR(-EINVAL);
	}

	spin_lock_irq(&tpg->acl_node_lock);
	list_add_tail(&acl->acl_list, &tpg->acl_node_list);
	tpg->num_node_acls++;
	spin_unlock_irq(&tpg->acl_node_lock);

done:
	pr_debug("%s_TPG[%hu] - Added ACL with TCQ Depth: %d for %s"
		" Initiator Node: %s\n", tpg->se_tpg_tfo->get_fabric_name(),
		tpg->se_tpg_tfo->tpg_get_tag(tpg), acl->queue_depth,
		tpg->se_tpg_tfo->get_fabric_name(), initiatorname);

	return acl;
}
EXPORT_SYMBOL(core_tpg_add_initiator_node_acl);

/*	core_tpg_del_initiator_node_acl():
 *
 *
 */
int core_tpg_del_initiator_node_acl(
	struct se_portal_group *tpg,
	struct se_node_acl *acl,
	int force)
{
	LIST_HEAD(sess_list);
	struct se_session *sess, *sess_tmp;
	unsigned long flags;
	int rc;

	spin_lock_irq(&tpg->acl_node_lock);
	if (acl->dynamic_node_acl) {
		acl->dynamic_node_acl = 0;
	}
	list_del(&acl->acl_list);
	tpg->num_node_acls--;
	spin_unlock_irq(&tpg->acl_node_lock);

	spin_lock_irqsave(&acl->nacl_sess_lock, flags);
	acl->acl_stop = 1;

	list_for_each_entry_safe(sess, sess_tmp, &acl->acl_sess_list,
				sess_acl_list) {
		if (sess->sess_tearing_down != 0)
			continue;

		target_get_session(sess);
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
	core_clear_initiator_node_from_tpg(acl, tpg);
	core_free_device_list_for_node(acl, tpg);

	pr_debug("%s_TPG[%hu] - Deleted ACL with TCQ Depth: %d for %s"
		" Initiator Node: %s\n", tpg->se_tpg_tfo->get_fabric_name(),
		tpg->se_tpg_tfo->tpg_get_tag(tpg), acl->queue_depth,
		tpg->se_tpg_tfo->get_fabric_name(), acl->initiatorname);

	return 0;
}
EXPORT_SYMBOL(core_tpg_del_initiator_node_acl);

/*	core_tpg_set_initiator_node_queue_depth():
 *
 *
 */
int core_tpg_set_initiator_node_queue_depth(
	struct se_portal_group *tpg,
	unsigned char *initiatorname,
	u32 queue_depth,
	int force)
{
	struct se_session *sess, *init_sess = NULL;
	struct se_node_acl *acl;
	unsigned long flags;
	int dynamic_acl = 0;

	spin_lock_irq(&tpg->acl_node_lock);
	acl = __core_tpg_get_initiator_node_acl(tpg, initiatorname);
	if (!acl) {
		pr_err("Access Control List entry for %s Initiator"
			" Node %s does not exists for TPG %hu, ignoring"
			" request.\n", tpg->se_tpg_tfo->get_fabric_name(),
			initiatorname, tpg->se_tpg_tfo->tpg_get_tag(tpg));
		spin_unlock_irq(&tpg->acl_node_lock);
		return -ENODEV;
	}
	if (acl->dynamic_node_acl) {
		acl->dynamic_node_acl = 0;
		dynamic_acl = 1;
	}
	spin_unlock_irq(&tpg->acl_node_lock);

	spin_lock_irqsave(&tpg->session_lock, flags);
	list_for_each_entry(sess, &tpg->tpg_sess_list, sess_list) {
		if (sess->se_node_acl != acl)
			continue;

		if (!force) {
			pr_err("Unable to change queue depth for %s"
				" Initiator Node: %s while session is"
				" operational.  To forcefully change the queue"
				" depth and force session reinstatement"
				" use the \"force=1\" parameter.\n",
				tpg->se_tpg_tfo->get_fabric_name(), initiatorname);
			spin_unlock_irqrestore(&tpg->session_lock, flags);

			spin_lock_irq(&tpg->acl_node_lock);
			if (dynamic_acl)
				acl->dynamic_node_acl = 1;
			spin_unlock_irq(&tpg->acl_node_lock);
			return -EEXIST;
		}
		/*
		 * Determine if the session needs to be closed by our context.
		 */
		if (!tpg->se_tpg_tfo->shutdown_session(sess))
			continue;

		init_sess = sess;
		break;
	}

	/*
	 * User has requested to change the queue depth for a Initiator Node.
	 * Change the value in the Node's struct se_node_acl, and call
	 * core_set_queue_depth_for_node() to add the requested queue depth.
	 *
	 * Finally call  tpg->se_tpg_tfo->close_session() to force session
	 * reinstatement to occur if there is an active session for the
	 * $FABRIC_MOD Initiator Node in question.
	 */
	acl->queue_depth = queue_depth;

	if (core_set_queue_depth_for_node(tpg, acl) < 0) {
		spin_unlock_irqrestore(&tpg->session_lock, flags);
		/*
		 * Force session reinstatement if
		 * core_set_queue_depth_for_node() failed, because we assume
		 * the $FABRIC_MOD has already the set session reinstatement
		 * bit from tpg->se_tpg_tfo->shutdown_session() called above.
		 */
		if (init_sess)
			tpg->se_tpg_tfo->close_session(init_sess);

		spin_lock_irq(&tpg->acl_node_lock);
		if (dynamic_acl)
			acl->dynamic_node_acl = 1;
		spin_unlock_irq(&tpg->acl_node_lock);
		return -EINVAL;
	}
	spin_unlock_irqrestore(&tpg->session_lock, flags);
	/*
	 * If the $FABRIC_MOD session for the Initiator Node ACL exists,
	 * forcefully shutdown the $FABRIC_MOD session/nexus.
	 */
	if (init_sess)
		tpg->se_tpg_tfo->close_session(init_sess);

	pr_debug("Successfully changed queue depth to: %d for Initiator"
		" Node: %s on %s Target Portal Group: %u\n", queue_depth,
		initiatorname, tpg->se_tpg_tfo->get_fabric_name(),
		tpg->se_tpg_tfo->tpg_get_tag(tpg));

	spin_lock_irq(&tpg->acl_node_lock);
	if (dynamic_acl)
		acl->dynamic_node_acl = 1;
	spin_unlock_irq(&tpg->acl_node_lock);

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

	complete(&lun->lun_ref_comp);
}

static int core_tpg_setup_virtual_lun0(struct se_portal_group *se_tpg)
{
	/* Set in core_dev_setup_virtual_lun0() */
	struct se_device *dev = g_lun0_dev;
	struct se_lun *lun = &se_tpg->tpg_virt_lun0;
	u32 lun_access = TRANSPORT_LUNFLAGS_READ_ONLY;
	int ret;

	lun->unpacked_lun = 0;
	lun->lun_status = TRANSPORT_LUN_STATUS_FREE;
	atomic_set(&lun->lun_acl_count, 0);
	init_completion(&lun->lun_shutdown_comp);
	INIT_LIST_HEAD(&lun->lun_acl_list);
	spin_lock_init(&lun->lun_acl_lock);
	spin_lock_init(&lun->lun_sep_lock);
	init_completion(&lun->lun_ref_comp);

	ret = core_tpg_add_lun(se_tpg, lun, lun_access, dev);
	if (ret < 0)
		return ret;

	return 0;
}

static void core_tpg_release_virtual_lun0(struct se_portal_group *se_tpg)
{
	struct se_lun *lun = &se_tpg->tpg_virt_lun0;

	core_tpg_post_dellun(se_tpg, lun);
}

int core_tpg_register(
	struct target_core_fabric_ops *tfo,
	struct se_wwn *se_wwn,
	struct se_portal_group *se_tpg,
	void *tpg_fabric_ptr,
	int se_tpg_type)
{
	struct se_lun *lun;
	u32 i;

	se_tpg->tpg_lun_list = array_zalloc(TRANSPORT_MAX_LUNS_PER_TPG,
			sizeof(struct se_lun), GFP_KERNEL);
	if (!se_tpg->tpg_lun_list) {
		pr_err("Unable to allocate struct se_portal_group->"
				"tpg_lun_list\n");
		return -ENOMEM;
	}

	for (i = 0; i < TRANSPORT_MAX_LUNS_PER_TPG; i++) {
		lun = se_tpg->tpg_lun_list[i];
		lun->unpacked_lun = i;
		lun->lun_link_magic = SE_LUN_LINK_MAGIC;
		lun->lun_status = TRANSPORT_LUN_STATUS_FREE;
		atomic_set(&lun->lun_acl_count, 0);
		init_completion(&lun->lun_shutdown_comp);
		INIT_LIST_HEAD(&lun->lun_acl_list);
		spin_lock_init(&lun->lun_acl_lock);
		spin_lock_init(&lun->lun_sep_lock);
		init_completion(&lun->lun_ref_comp);
	}

	se_tpg->se_tpg_type = se_tpg_type;
	se_tpg->se_tpg_fabric_ptr = tpg_fabric_ptr;
	se_tpg->se_tpg_tfo = tfo;
	se_tpg->se_tpg_wwn = se_wwn;
	atomic_set(&se_tpg->tpg_pr_ref_count, 0);
	INIT_LIST_HEAD(&se_tpg->acl_node_list);
	INIT_LIST_HEAD(&se_tpg->se_tpg_node);
	INIT_LIST_HEAD(&se_tpg->tpg_sess_list);
	spin_lock_init(&se_tpg->acl_node_lock);
	spin_lock_init(&se_tpg->session_lock);
	spin_lock_init(&se_tpg->tpg_lun_lock);

	if (se_tpg->se_tpg_type == TRANSPORT_TPG_TYPE_NORMAL) {
		if (core_tpg_setup_virtual_lun0(se_tpg) < 0) {
			array_free(se_tpg->tpg_lun_list,
				   TRANSPORT_MAX_LUNS_PER_TPG);
			return -ENOMEM;
		}
	}

	spin_lock_bh(&tpg_lock);
	list_add_tail(&se_tpg->se_tpg_node, &tpg_list);
	spin_unlock_bh(&tpg_lock);

	pr_debug("TARGET_CORE[%s]: Allocated %s struct se_portal_group for"
		" endpoint: %s, Portal Tag: %u\n", tfo->get_fabric_name(),
		(se_tpg->se_tpg_type == TRANSPORT_TPG_TYPE_NORMAL) ?
		"Normal" : "Discovery", (tfo->tpg_get_wwn(se_tpg) == NULL) ?
		"None" : tfo->tpg_get_wwn(se_tpg), tfo->tpg_get_tag(se_tpg));

	return 0;
}
EXPORT_SYMBOL(core_tpg_register);

int core_tpg_deregister(struct se_portal_group *se_tpg)
{
	struct se_node_acl *nacl, *nacl_tmp;

	pr_debug("TARGET_CORE[%s]: Deallocating %s struct se_portal_group"
		" for endpoint: %s Portal Tag %u\n",
		(se_tpg->se_tpg_type == TRANSPORT_TPG_TYPE_NORMAL) ?
		"Normal" : "Discovery", se_tpg->se_tpg_tfo->get_fabric_name(),
		se_tpg->se_tpg_tfo->tpg_get_wwn(se_tpg),
		se_tpg->se_tpg_tfo->tpg_get_tag(se_tpg));

	spin_lock_bh(&tpg_lock);
	list_del(&se_tpg->se_tpg_node);
	spin_unlock_bh(&tpg_lock);

	while (atomic_read(&se_tpg->tpg_pr_ref_count) != 0)
		cpu_relax();
	/*
	 * Release any remaining demo-mode generated se_node_acl that have
	 * not been released because of TFO->tpg_check_demo_mode_cache() == 1
	 * in transport_deregister_session().
	 */
	spin_lock_irq(&se_tpg->acl_node_lock);
	list_for_each_entry_safe(nacl, nacl_tmp, &se_tpg->acl_node_list,
			acl_list) {
		list_del(&nacl->acl_list);
		se_tpg->num_node_acls--;
		spin_unlock_irq(&se_tpg->acl_node_lock);

		core_tpg_wait_for_nacl_pr_ref(nacl);
		core_free_device_list_for_node(nacl, se_tpg);
		se_tpg->se_tpg_tfo->tpg_release_fabric_acl(se_tpg, nacl);

		spin_lock_irq(&se_tpg->acl_node_lock);
	}
	spin_unlock_irq(&se_tpg->acl_node_lock);

	if (se_tpg->se_tpg_type == TRANSPORT_TPG_TYPE_NORMAL)
		core_tpg_release_virtual_lun0(se_tpg);

	se_tpg->se_tpg_fabric_ptr = NULL;
	array_free(se_tpg->tpg_lun_list, TRANSPORT_MAX_LUNS_PER_TPG);
	return 0;
}
EXPORT_SYMBOL(core_tpg_deregister);

struct se_lun *core_tpg_alloc_lun(
	struct se_portal_group *tpg,
	u32 unpacked_lun)
{
	struct se_lun *lun;

	if (unpacked_lun > (TRANSPORT_MAX_LUNS_PER_TPG-1)) {
		pr_err("%s LUN: %u exceeds TRANSPORT_MAX_LUNS_PER_TPG"
			"-1: %u for Target Portal Group: %u\n",
			tpg->se_tpg_tfo->get_fabric_name(),
			unpacked_lun, TRANSPORT_MAX_LUNS_PER_TPG-1,
			tpg->se_tpg_tfo->tpg_get_tag(tpg));
		return ERR_PTR(-EOVERFLOW);
	}

	spin_lock(&tpg->tpg_lun_lock);
	lun = tpg->tpg_lun_list[unpacked_lun];
	if (lun->lun_status == TRANSPORT_LUN_STATUS_ACTIVE) {
		pr_err("TPG Logical Unit Number: %u is already active"
			" on %s Target Portal Group: %u, ignoring request.\n",
			unpacked_lun, tpg->se_tpg_tfo->get_fabric_name(),
			tpg->se_tpg_tfo->tpg_get_tag(tpg));
		spin_unlock(&tpg->tpg_lun_lock);
		return ERR_PTR(-EINVAL);
	}
	spin_unlock(&tpg->tpg_lun_lock);

	return lun;
}

int core_tpg_add_lun(
	struct se_portal_group *tpg,
	struct se_lun *lun,
	u32 lun_access,
	struct se_device *dev)
{
	int ret;

	ret = percpu_ref_init(&lun->lun_ref, core_tpg_lun_ref_release);
	if (ret < 0)
		return ret;

	ret = core_dev_export(dev, tpg, lun);
	if (ret < 0) {
		percpu_ref_exit(&lun->lun_ref);
		return ret;
	}

	spin_lock(&tpg->tpg_lun_lock);
	lun->lun_access = lun_access;
	lun->lun_status = TRANSPORT_LUN_STATUS_ACTIVE;
	spin_unlock(&tpg->tpg_lun_lock);

	return 0;
}

struct se_lun *core_tpg_pre_dellun(
	struct se_portal_group *tpg,
	u32 unpacked_lun)
{
	struct se_lun *lun;

	if (unpacked_lun > (TRANSPORT_MAX_LUNS_PER_TPG-1)) {
		pr_err("%s LUN: %u exceeds TRANSPORT_MAX_LUNS_PER_TPG"
			"-1: %u for Target Portal Group: %u\n",
			tpg->se_tpg_tfo->get_fabric_name(), unpacked_lun,
			TRANSPORT_MAX_LUNS_PER_TPG-1,
			tpg->se_tpg_tfo->tpg_get_tag(tpg));
		return ERR_PTR(-EOVERFLOW);
	}

	spin_lock(&tpg->tpg_lun_lock);
	lun = tpg->tpg_lun_list[unpacked_lun];
	if (lun->lun_status != TRANSPORT_LUN_STATUS_ACTIVE) {
		pr_err("%s Logical Unit Number: %u is not active on"
			" Target Portal Group: %u, ignoring request.\n",
			tpg->se_tpg_tfo->get_fabric_name(), unpacked_lun,
			tpg->se_tpg_tfo->tpg_get_tag(tpg));
		spin_unlock(&tpg->tpg_lun_lock);
		return ERR_PTR(-ENODEV);
	}
	spin_unlock(&tpg->tpg_lun_lock);

	return lun;
}

int core_tpg_post_dellun(
	struct se_portal_group *tpg,
	struct se_lun *lun)
{
	core_clear_lun_from_tpg(lun, tpg);
	transport_clear_lun_ref(lun);

	core_dev_unexport(lun->lun_se_dev, tpg, lun);

	spin_lock(&tpg->tpg_lun_lock);
	lun->lun_status = TRANSPORT_LUN_STATUS_FREE;
	spin_unlock(&tpg->tpg_lun_lock);

	percpu_ref_exit(&lun->lun_ref);

	return 0;
}

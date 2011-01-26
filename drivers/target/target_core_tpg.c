/*******************************************************************************
 * Filename:  target_core_tpg.c
 *
 * This file contains generic Target Portal Group related functions.
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
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/smp_lock.h>
#include <linux/in.h>
#include <net/sock.h>
#include <net/tcp.h>
#include <scsi/scsi.h>
#include <scsi/scsi_cmnd.h>

#include <target/target_core_base.h>
#include <target/target_core_device.h>
#include <target/target_core_tpg.h>
#include <target/target_core_transport.h>
#include <target/target_core_fabric_ops.h>

#include "target_core_hba.h"

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
	struct se_lun_acl *acl, *acl_tmp;

	spin_lock_irq(&nacl->device_list_lock);
	for (i = 0; i < TRANSPORT_MAX_LUNS_PER_TPG; i++) {
		deve = &nacl->device_list[i];

		if (!(deve->lun_flags & TRANSPORT_LUNFLAGS_INITIATOR_ACCESS))
			continue;

		if (!deve->se_lun) {
			printk(KERN_ERR "%s device entries device pointer is"
				" NULL, but Initiator has access.\n",
				TPG_TFO(tpg)->get_fabric_name());
			continue;
		}

		lun = deve->se_lun;
		spin_unlock_irq(&nacl->device_list_lock);
		core_update_device_list_for_node(lun, NULL, deve->mapped_lun,
			TRANSPORT_LUNFLAGS_NO_ACCESS, nacl, tpg, 0);

		spin_lock(&lun->lun_acl_lock);
		list_for_each_entry_safe(acl, acl_tmp,
					&lun->lun_acl_list, lacl_list) {
			if (!(strcmp(acl->initiatorname,
					nacl->initiatorname)) &&
			     (acl->mapped_lun == deve->mapped_lun))
				break;
		}

		if (!acl) {
			printk(KERN_ERR "Unable to locate struct se_lun_acl for %s,"
				" mapped_lun: %u\n", nacl->initiatorname,
				deve->mapped_lun);
			spin_unlock(&lun->lun_acl_lock);
			spin_lock_irq(&nacl->device_list_lock);
			continue;
		}

		list_del(&acl->lacl_list);
		spin_unlock(&lun->lun_acl_lock);

		spin_lock_irq(&nacl->device_list_lock);
		kfree(acl);
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
		if (!(strcmp(acl->initiatorname, initiatorname)))
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

	spin_lock_bh(&tpg->acl_node_lock);
	list_for_each_entry(acl, &tpg->acl_node_list, acl_list) {
		if (!(strcmp(acl->initiatorname, initiatorname)) &&
		   (!(acl->dynamic_node_acl))) {
			spin_unlock_bh(&tpg->acl_node_lock);
			return acl;
		}
	}
	spin_unlock_bh(&tpg->acl_node_lock);

	return NULL;
}

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
		lun = &tpg->tpg_lun_list[i];
		if (lun->lun_status != TRANSPORT_LUN_STATUS_ACTIVE)
			continue;

		spin_unlock(&tpg->tpg_lun_lock);

		dev = lun->lun_se_dev;
		/*
		 * By default in LIO-Target $FABRIC_MOD,
		 * demo_mode_write_protect is ON, or READ_ONLY;
		 */
		if (!(TPG_TFO(tpg)->tpg_check_demo_mode_write_protect(tpg))) {
			if (dev->dev_flags & DF_READ_ONLY)
				lun_access = TRANSPORT_LUNFLAGS_READ_ONLY;
			else
				lun_access = TRANSPORT_LUNFLAGS_READ_WRITE;
		} else {
			/*
			 * Allow only optical drives to issue R/W in default RO
			 * demo mode.
			 */
			if (TRANSPORT(dev)->get_device_type(dev) == TYPE_DISK)
				lun_access = TRANSPORT_LUNFLAGS_READ_ONLY;
			else
				lun_access = TRANSPORT_LUNFLAGS_READ_WRITE;
		}

		printk(KERN_INFO "TARGET_CORE[%s]->TPG[%u]_LUN[%u] - Adding %s"
			" access for LUN in Demo Mode\n",
			TPG_TFO(tpg)->get_fabric_name(),
			TPG_TFO(tpg)->tpg_get_tag(tpg), lun->unpacked_lun,
			(lun_access == TRANSPORT_LUNFLAGS_READ_WRITE) ?
			"READ-WRITE" : "READ-ONLY");

		core_update_device_list_for_node(lun, NULL, lun->unpacked_lun,
				lun_access, acl, tpg, 1);
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
		printk(KERN_ERR "Queue depth for %s Initiator Node: %s is 0,"
			"defaulting to 1.\n", TPG_TFO(tpg)->get_fabric_name(),
			acl->initiatorname);
		acl->queue_depth = 1;
	}

	return 0;
}

/*      core_create_device_list_for_node():
 *
 *
 */
static int core_create_device_list_for_node(struct se_node_acl *nacl)
{
	struct se_dev_entry *deve;
	int i;

	nacl->device_list = kzalloc(sizeof(struct se_dev_entry) *
				TRANSPORT_MAX_LUNS_PER_TPG, GFP_KERNEL);
	if (!(nacl->device_list)) {
		printk(KERN_ERR "Unable to allocate memory for"
			" struct se_node_acl->device_list\n");
		return -1;
	}
	for (i = 0; i < TRANSPORT_MAX_LUNS_PER_TPG; i++) {
		deve = &nacl->device_list[i];

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
	if ((acl))
		return acl;

	if (!(TPG_TFO(tpg)->tpg_check_demo_mode(tpg)))
		return NULL;

	acl =  TPG_TFO(tpg)->tpg_alloc_fabric_acl(tpg);
	if (!(acl))
		return NULL;

	INIT_LIST_HEAD(&acl->acl_list);
	INIT_LIST_HEAD(&acl->acl_sess_list);
	spin_lock_init(&acl->device_list_lock);
	spin_lock_init(&acl->nacl_sess_lock);
	atomic_set(&acl->acl_pr_ref_count, 0);
	atomic_set(&acl->mib_ref_count, 0);
	acl->queue_depth = TPG_TFO(tpg)->tpg_get_default_depth(tpg);
	snprintf(acl->initiatorname, TRANSPORT_IQN_LEN, "%s", initiatorname);
	acl->se_tpg = tpg;
	acl->acl_index = scsi_get_new_index(SCSI_AUTH_INTR_INDEX);
	spin_lock_init(&acl->stats_lock);
	acl->dynamic_node_acl = 1;

	TPG_TFO(tpg)->set_default_node_attributes(acl);

	if (core_create_device_list_for_node(acl) < 0) {
		TPG_TFO(tpg)->tpg_release_fabric_acl(tpg, acl);
		return NULL;
	}

	if (core_set_queue_depth_for_node(tpg, acl) < 0) {
		core_free_device_list_for_node(acl, tpg);
		TPG_TFO(tpg)->tpg_release_fabric_acl(tpg, acl);
		return NULL;
	}

	core_tpg_add_node_to_devs(acl, tpg);

	spin_lock_bh(&tpg->acl_node_lock);
	list_add_tail(&acl->acl_list, &tpg->acl_node_list);
	tpg->num_node_acls++;
	spin_unlock_bh(&tpg->acl_node_lock);

	printk("%s_TPG[%u] - Added DYNAMIC ACL with TCQ Depth: %d for %s"
		" Initiator Node: %s\n", TPG_TFO(tpg)->get_fabric_name(),
		TPG_TFO(tpg)->tpg_get_tag(tpg), acl->queue_depth,
		TPG_TFO(tpg)->get_fabric_name(), initiatorname);

	return acl;
}
EXPORT_SYMBOL(core_tpg_check_initiator_node_acl);

void core_tpg_wait_for_nacl_pr_ref(struct se_node_acl *nacl)
{
	while (atomic_read(&nacl->acl_pr_ref_count) != 0)
		cpu_relax();
}

void core_tpg_wait_for_mib_ref(struct se_node_acl *nacl)
{
	while (atomic_read(&nacl->mib_ref_count) != 0)
		cpu_relax();
}

void core_tpg_clear_object_luns(struct se_portal_group *tpg)
{
	int i, ret;
	struct se_lun *lun;

	spin_lock(&tpg->tpg_lun_lock);
	for (i = 0; i < TRANSPORT_MAX_LUNS_PER_TPG; i++) {
		lun = &tpg->tpg_lun_list[i];

		if ((lun->lun_status != TRANSPORT_LUN_STATUS_ACTIVE) ||
		    (lun->lun_se_dev == NULL))
			continue;

		spin_unlock(&tpg->tpg_lun_lock);
		ret = core_dev_del_lun(tpg, lun->unpacked_lun);
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

	spin_lock_bh(&tpg->acl_node_lock);
	acl = __core_tpg_get_initiator_node_acl(tpg, initiatorname);
	if ((acl)) {
		if (acl->dynamic_node_acl) {
			acl->dynamic_node_acl = 0;
			printk(KERN_INFO "%s_TPG[%u] - Replacing dynamic ACL"
				" for %s\n", TPG_TFO(tpg)->get_fabric_name(),
				TPG_TFO(tpg)->tpg_get_tag(tpg), initiatorname);
			spin_unlock_bh(&tpg->acl_node_lock);
			/*
			 * Release the locally allocated struct se_node_acl
			 * because * core_tpg_add_initiator_node_acl() returned
			 * a pointer to an existing demo mode node ACL.
			 */
			if (se_nacl)
				TPG_TFO(tpg)->tpg_release_fabric_acl(tpg,
							se_nacl);
			goto done;
		}

		printk(KERN_ERR "ACL entry for %s Initiator"
			" Node %s already exists for TPG %u, ignoring"
			" request.\n",  TPG_TFO(tpg)->get_fabric_name(),
			initiatorname, TPG_TFO(tpg)->tpg_get_tag(tpg));
		spin_unlock_bh(&tpg->acl_node_lock);
		return ERR_PTR(-EEXIST);
	}
	spin_unlock_bh(&tpg->acl_node_lock);

	if (!(se_nacl)) {
		printk("struct se_node_acl pointer is NULL\n");
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
	spin_lock_init(&acl->device_list_lock);
	spin_lock_init(&acl->nacl_sess_lock);
	atomic_set(&acl->acl_pr_ref_count, 0);
	acl->queue_depth = queue_depth;
	snprintf(acl->initiatorname, TRANSPORT_IQN_LEN, "%s", initiatorname);
	acl->se_tpg = tpg;
	acl->acl_index = scsi_get_new_index(SCSI_AUTH_INTR_INDEX);
	spin_lock_init(&acl->stats_lock);

	TPG_TFO(tpg)->set_default_node_attributes(acl);

	if (core_create_device_list_for_node(acl) < 0) {
		TPG_TFO(tpg)->tpg_release_fabric_acl(tpg, acl);
		return ERR_PTR(-ENOMEM);
	}

	if (core_set_queue_depth_for_node(tpg, acl) < 0) {
		core_free_device_list_for_node(acl, tpg);
		TPG_TFO(tpg)->tpg_release_fabric_acl(tpg, acl);
		return ERR_PTR(-EINVAL);
	}

	spin_lock_bh(&tpg->acl_node_lock);
	list_add_tail(&acl->acl_list, &tpg->acl_node_list);
	tpg->num_node_acls++;
	spin_unlock_bh(&tpg->acl_node_lock);

done:
	printk(KERN_INFO "%s_TPG[%hu] - Added ACL with TCQ Depth: %d for %s"
		" Initiator Node: %s\n", TPG_TFO(tpg)->get_fabric_name(),
		TPG_TFO(tpg)->tpg_get_tag(tpg), acl->queue_depth,
		TPG_TFO(tpg)->get_fabric_name(), initiatorname);

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
	struct se_session *sess, *sess_tmp;
	int dynamic_acl = 0;

	spin_lock_bh(&tpg->acl_node_lock);
	if (acl->dynamic_node_acl) {
		acl->dynamic_node_acl = 0;
		dynamic_acl = 1;
	}
	list_del(&acl->acl_list);
	tpg->num_node_acls--;
	spin_unlock_bh(&tpg->acl_node_lock);

	spin_lock_bh(&tpg->session_lock);
	list_for_each_entry_safe(sess, sess_tmp,
				&tpg->tpg_sess_list, sess_list) {
		if (sess->se_node_acl != acl)
			continue;
		/*
		 * Determine if the session needs to be closed by our context.
		 */
		if (!(TPG_TFO(tpg)->shutdown_session(sess)))
			continue;

		spin_unlock_bh(&tpg->session_lock);
		/*
		 * If the $FABRIC_MOD session for the Initiator Node ACL exists,
		 * forcefully shutdown the $FABRIC_MOD session/nexus.
		 */
		TPG_TFO(tpg)->close_session(sess);

		spin_lock_bh(&tpg->session_lock);
	}
	spin_unlock_bh(&tpg->session_lock);

	core_tpg_wait_for_nacl_pr_ref(acl);
	core_tpg_wait_for_mib_ref(acl);
	core_clear_initiator_node_from_tpg(acl, tpg);
	core_free_device_list_for_node(acl, tpg);

	printk(KERN_INFO "%s_TPG[%hu] - Deleted ACL with TCQ Depth: %d for %s"
		" Initiator Node: %s\n", TPG_TFO(tpg)->get_fabric_name(),
		TPG_TFO(tpg)->tpg_get_tag(tpg), acl->queue_depth,
		TPG_TFO(tpg)->get_fabric_name(), acl->initiatorname);

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
	int dynamic_acl = 0;

	spin_lock_bh(&tpg->acl_node_lock);
	acl = __core_tpg_get_initiator_node_acl(tpg, initiatorname);
	if (!(acl)) {
		printk(KERN_ERR "Access Control List entry for %s Initiator"
			" Node %s does not exists for TPG %hu, ignoring"
			" request.\n", TPG_TFO(tpg)->get_fabric_name(),
			initiatorname, TPG_TFO(tpg)->tpg_get_tag(tpg));
		spin_unlock_bh(&tpg->acl_node_lock);
		return -ENODEV;
	}
	if (acl->dynamic_node_acl) {
		acl->dynamic_node_acl = 0;
		dynamic_acl = 1;
	}
	spin_unlock_bh(&tpg->acl_node_lock);

	spin_lock_bh(&tpg->session_lock);
	list_for_each_entry(sess, &tpg->tpg_sess_list, sess_list) {
		if (sess->se_node_acl != acl)
			continue;

		if (!force) {
			printk(KERN_ERR "Unable to change queue depth for %s"
				" Initiator Node: %s while session is"
				" operational.  To forcefully change the queue"
				" depth and force session reinstatement"
				" use the \"force=1\" parameter.\n",
				TPG_TFO(tpg)->get_fabric_name(), initiatorname);
			spin_unlock_bh(&tpg->session_lock);

			spin_lock_bh(&tpg->acl_node_lock);
			if (dynamic_acl)
				acl->dynamic_node_acl = 1;
			spin_unlock_bh(&tpg->acl_node_lock);
			return -EEXIST;
		}
		/*
		 * Determine if the session needs to be closed by our context.
		 */
		if (!(TPG_TFO(tpg)->shutdown_session(sess)))
			continue;

		init_sess = sess;
		break;
	}

	/*
	 * User has requested to change the queue depth for a Initiator Node.
	 * Change the value in the Node's struct se_node_acl, and call
	 * core_set_queue_depth_for_node() to add the requested queue depth.
	 *
	 * Finally call  TPG_TFO(tpg)->close_session() to force session
	 * reinstatement to occur if there is an active session for the
	 * $FABRIC_MOD Initiator Node in question.
	 */
	acl->queue_depth = queue_depth;

	if (core_set_queue_depth_for_node(tpg, acl) < 0) {
		spin_unlock_bh(&tpg->session_lock);
		/*
		 * Force session reinstatement if
		 * core_set_queue_depth_for_node() failed, because we assume
		 * the $FABRIC_MOD has already the set session reinstatement
		 * bit from TPG_TFO(tpg)->shutdown_session() called above.
		 */
		if (init_sess)
			TPG_TFO(tpg)->close_session(init_sess);

		spin_lock_bh(&tpg->acl_node_lock);
		if (dynamic_acl)
			acl->dynamic_node_acl = 1;
		spin_unlock_bh(&tpg->acl_node_lock);
		return -EINVAL;
	}
	spin_unlock_bh(&tpg->session_lock);
	/*
	 * If the $FABRIC_MOD session for the Initiator Node ACL exists,
	 * forcefully shutdown the $FABRIC_MOD session/nexus.
	 */
	if (init_sess)
		TPG_TFO(tpg)->close_session(init_sess);

	printk(KERN_INFO "Successfuly changed queue depth to: %d for Initiator"
		" Node: %s on %s Target Portal Group: %u\n", queue_depth,
		initiatorname, TPG_TFO(tpg)->get_fabric_name(),
		TPG_TFO(tpg)->tpg_get_tag(tpg));

	spin_lock_bh(&tpg->acl_node_lock);
	if (dynamic_acl)
		acl->dynamic_node_acl = 1;
	spin_unlock_bh(&tpg->acl_node_lock);

	return 0;
}
EXPORT_SYMBOL(core_tpg_set_initiator_node_queue_depth);

static int core_tpg_setup_virtual_lun0(struct se_portal_group *se_tpg)
{
	/* Set in core_dev_setup_virtual_lun0() */
	struct se_device *dev = se_global->g_lun0_dev;
	struct se_lun *lun = &se_tpg->tpg_virt_lun0;
	u32 lun_access = TRANSPORT_LUNFLAGS_READ_ONLY;
	int ret;

	lun->unpacked_lun = 0;
	lun->lun_status = TRANSPORT_LUN_STATUS_FREE;
	atomic_set(&lun->lun_acl_count, 0);
	init_completion(&lun->lun_shutdown_comp);
	INIT_LIST_HEAD(&lun->lun_acl_list);
	INIT_LIST_HEAD(&lun->lun_cmd_list);
	spin_lock_init(&lun->lun_acl_lock);
	spin_lock_init(&lun->lun_cmd_lock);
	spin_lock_init(&lun->lun_sep_lock);

	ret = core_tpg_post_addlun(se_tpg, lun, lun_access, dev);
	if (ret < 0)
		return -1;

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

	se_tpg->tpg_lun_list = kzalloc((sizeof(struct se_lun) *
				TRANSPORT_MAX_LUNS_PER_TPG), GFP_KERNEL);
	if (!(se_tpg->tpg_lun_list)) {
		printk(KERN_ERR "Unable to allocate struct se_portal_group->"
				"tpg_lun_list\n");
		return -ENOMEM;
	}

	for (i = 0; i < TRANSPORT_MAX_LUNS_PER_TPG; i++) {
		lun = &se_tpg->tpg_lun_list[i];
		lun->unpacked_lun = i;
		lun->lun_status = TRANSPORT_LUN_STATUS_FREE;
		atomic_set(&lun->lun_acl_count, 0);
		init_completion(&lun->lun_shutdown_comp);
		INIT_LIST_HEAD(&lun->lun_acl_list);
		INIT_LIST_HEAD(&lun->lun_cmd_list);
		spin_lock_init(&lun->lun_acl_lock);
		spin_lock_init(&lun->lun_cmd_lock);
		spin_lock_init(&lun->lun_sep_lock);
	}

	se_tpg->se_tpg_type = se_tpg_type;
	se_tpg->se_tpg_fabric_ptr = tpg_fabric_ptr;
	se_tpg->se_tpg_tfo = tfo;
	se_tpg->se_tpg_wwn = se_wwn;
	atomic_set(&se_tpg->tpg_pr_ref_count, 0);
	INIT_LIST_HEAD(&se_tpg->acl_node_list);
	INIT_LIST_HEAD(&se_tpg->se_tpg_list);
	INIT_LIST_HEAD(&se_tpg->tpg_sess_list);
	spin_lock_init(&se_tpg->acl_node_lock);
	spin_lock_init(&se_tpg->session_lock);
	spin_lock_init(&se_tpg->tpg_lun_lock);

	if (se_tpg->se_tpg_type == TRANSPORT_TPG_TYPE_NORMAL) {
		if (core_tpg_setup_virtual_lun0(se_tpg) < 0) {
			kfree(se_tpg);
			return -ENOMEM;
		}
	}

	spin_lock_bh(&se_global->se_tpg_lock);
	list_add_tail(&se_tpg->se_tpg_list, &se_global->g_se_tpg_list);
	spin_unlock_bh(&se_global->se_tpg_lock);

	printk(KERN_INFO "TARGET_CORE[%s]: Allocated %s struct se_portal_group for"
		" endpoint: %s, Portal Tag: %u\n", tfo->get_fabric_name(),
		(se_tpg->se_tpg_type == TRANSPORT_TPG_TYPE_NORMAL) ?
		"Normal" : "Discovery", (tfo->tpg_get_wwn(se_tpg) == NULL) ?
		"None" : tfo->tpg_get_wwn(se_tpg), tfo->tpg_get_tag(se_tpg));

	return 0;
}
EXPORT_SYMBOL(core_tpg_register);

int core_tpg_deregister(struct se_portal_group *se_tpg)
{
	printk(KERN_INFO "TARGET_CORE[%s]: Deallocating %s struct se_portal_group"
		" for endpoint: %s Portal Tag %u\n",
		(se_tpg->se_tpg_type == TRANSPORT_TPG_TYPE_NORMAL) ?
		"Normal" : "Discovery", TPG_TFO(se_tpg)->get_fabric_name(),
		TPG_TFO(se_tpg)->tpg_get_wwn(se_tpg),
		TPG_TFO(se_tpg)->tpg_get_tag(se_tpg));

	spin_lock_bh(&se_global->se_tpg_lock);
	list_del(&se_tpg->se_tpg_list);
	spin_unlock_bh(&se_global->se_tpg_lock);

	while (atomic_read(&se_tpg->tpg_pr_ref_count) != 0)
		cpu_relax();

	if (se_tpg->se_tpg_type == TRANSPORT_TPG_TYPE_NORMAL)
		core_tpg_release_virtual_lun0(se_tpg);

	se_tpg->se_tpg_fabric_ptr = NULL;
	kfree(se_tpg->tpg_lun_list);
	return 0;
}
EXPORT_SYMBOL(core_tpg_deregister);

struct se_lun *core_tpg_pre_addlun(
	struct se_portal_group *tpg,
	u32 unpacked_lun)
{
	struct se_lun *lun;

	if (unpacked_lun > (TRANSPORT_MAX_LUNS_PER_TPG-1)) {
		printk(KERN_ERR "%s LUN: %u exceeds TRANSPORT_MAX_LUNS_PER_TPG"
			"-1: %u for Target Portal Group: %u\n",
			TPG_TFO(tpg)->get_fabric_name(),
			unpacked_lun, TRANSPORT_MAX_LUNS_PER_TPG-1,
			TPG_TFO(tpg)->tpg_get_tag(tpg));
		return ERR_PTR(-EOVERFLOW);
	}

	spin_lock(&tpg->tpg_lun_lock);
	lun = &tpg->tpg_lun_list[unpacked_lun];
	if (lun->lun_status == TRANSPORT_LUN_STATUS_ACTIVE) {
		printk(KERN_ERR "TPG Logical Unit Number: %u is already active"
			" on %s Target Portal Group: %u, ignoring request.\n",
			unpacked_lun, TPG_TFO(tpg)->get_fabric_name(),
			TPG_TFO(tpg)->tpg_get_tag(tpg));
		spin_unlock(&tpg->tpg_lun_lock);
		return ERR_PTR(-EINVAL);
	}
	spin_unlock(&tpg->tpg_lun_lock);

	return lun;
}

int core_tpg_post_addlun(
	struct se_portal_group *tpg,
	struct se_lun *lun,
	u32 lun_access,
	void *lun_ptr)
{
	if (core_dev_export(lun_ptr, tpg, lun) < 0)
		return -1;

	spin_lock(&tpg->tpg_lun_lock);
	lun->lun_access = lun_access;
	lun->lun_status = TRANSPORT_LUN_STATUS_ACTIVE;
	spin_unlock(&tpg->tpg_lun_lock);

	return 0;
}

static void core_tpg_shutdown_lun(
	struct se_portal_group *tpg,
	struct se_lun *lun)
{
	core_clear_lun_from_tpg(lun, tpg);
	transport_clear_lun_from_sessions(lun);
}

struct se_lun *core_tpg_pre_dellun(
	struct se_portal_group *tpg,
	u32 unpacked_lun,
	int *ret)
{
	struct se_lun *lun;

	if (unpacked_lun > (TRANSPORT_MAX_LUNS_PER_TPG-1)) {
		printk(KERN_ERR "%s LUN: %u exceeds TRANSPORT_MAX_LUNS_PER_TPG"
			"-1: %u for Target Portal Group: %u\n",
			TPG_TFO(tpg)->get_fabric_name(), unpacked_lun,
			TRANSPORT_MAX_LUNS_PER_TPG-1,
			TPG_TFO(tpg)->tpg_get_tag(tpg));
		return ERR_PTR(-EOVERFLOW);
	}

	spin_lock(&tpg->tpg_lun_lock);
	lun = &tpg->tpg_lun_list[unpacked_lun];
	if (lun->lun_status != TRANSPORT_LUN_STATUS_ACTIVE) {
		printk(KERN_ERR "%s Logical Unit Number: %u is not active on"
			" Target Portal Group: %u, ignoring request.\n",
			TPG_TFO(tpg)->get_fabric_name(), unpacked_lun,
			TPG_TFO(tpg)->tpg_get_tag(tpg));
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
	core_tpg_shutdown_lun(tpg, lun);

	core_dev_unexport(lun->lun_se_dev, tpg, lun);

	spin_lock(&tpg->tpg_lun_lock);
	lun->lun_status = TRANSPORT_LUN_STATUS_FREE;
	spin_unlock(&tpg->tpg_lun_lock);

	return 0;
}

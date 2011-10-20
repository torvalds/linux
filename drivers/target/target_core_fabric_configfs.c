/*******************************************************************************
* Filename: target_core_fabric_configfs.c
 *
 * This file contains generic fabric module configfs infrastructure for
 * TCM v4.x code
 *
 * Copyright (c) 2010,2011 Rising Tide Systems
 * Copyright (c) 2010,2011 Linux-iSCSI.org
 *
 * Copyright (c) Nicholas A. Bellinger <nab@linux-iscsi.org>
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
 ****************************************************************************/

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/version.h>
#include <generated/utsrelease.h>
#include <linux/utsname.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/namei.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/delay.h>
#include <linux/unistd.h>
#include <linux/string.h>
#include <linux/syscalls.h>
#include <linux/configfs.h>

#include <target/target_core_base.h>
#include <target/target_core_device.h>
#include <target/target_core_tpg.h>
#include <target/target_core_transport.h>
#include <target/target_core_fabric_ops.h>
#include <target/target_core_fabric_configfs.h>
#include <target/target_core_configfs.h>
#include <target/configfs_macros.h>

#include "target_core_alua.h"
#include "target_core_hba.h"
#include "target_core_pr.h"
#include "target_core_stat.h"

#define TF_CIT_SETUP(_name, _item_ops, _group_ops, _attrs)		\
static void target_fabric_setup_##_name##_cit(struct target_fabric_configfs *tf) \
{									\
	struct target_fabric_configfs_template *tfc = &tf->tf_cit_tmpl;	\
	struct config_item_type *cit = &tfc->tfc_##_name##_cit;		\
									\
	cit->ct_item_ops = _item_ops;					\
	cit->ct_group_ops = _group_ops;					\
	cit->ct_attrs = _attrs;						\
	cit->ct_owner = tf->tf_module;					\
	pr_debug("Setup generic %s\n", __stringify(_name));		\
}

/* Start of tfc_tpg_mappedlun_cit */

static int target_fabric_mappedlun_link(
	struct config_item *lun_acl_ci,
	struct config_item *lun_ci)
{
	struct se_dev_entry *deve;
	struct se_lun *lun = container_of(to_config_group(lun_ci),
			struct se_lun, lun_group);
	struct se_lun_acl *lacl = container_of(to_config_group(lun_acl_ci),
			struct se_lun_acl, se_lun_group);
	struct se_portal_group *se_tpg;
	struct config_item *nacl_ci, *tpg_ci, *tpg_ci_s, *wwn_ci, *wwn_ci_s;
	int ret = 0, lun_access;
	/*
	 * Ensure that the source port exists
	 */
	if (!lun->lun_sep || !lun->lun_sep->sep_tpg) {
		pr_err("Source se_lun->lun_sep or lun->lun_sep->sep"
				"_tpg does not exist\n");
		return -EINVAL;
	}
	se_tpg = lun->lun_sep->sep_tpg;

	nacl_ci = &lun_acl_ci->ci_parent->ci_group->cg_item;
	tpg_ci = &nacl_ci->ci_group->cg_item;
	wwn_ci = &tpg_ci->ci_group->cg_item;
	tpg_ci_s = &lun_ci->ci_parent->ci_group->cg_item;
	wwn_ci_s = &tpg_ci_s->ci_group->cg_item;
	/*
	 * Make sure the SymLink is going to the same $FABRIC/$WWN/tpgt_$TPGT
	 */
	if (strcmp(config_item_name(wwn_ci), config_item_name(wwn_ci_s))) {
		pr_err("Illegal Initiator ACL SymLink outside of %s\n",
			config_item_name(wwn_ci));
		return -EINVAL;
	}
	if (strcmp(config_item_name(tpg_ci), config_item_name(tpg_ci_s))) {
		pr_err("Illegal Initiator ACL Symlink outside of %s"
			" TPGT: %s\n", config_item_name(wwn_ci),
			config_item_name(tpg_ci));
		return -EINVAL;
	}
	/*
	 * If this struct se_node_acl was dynamically generated with
	 * tpg_1/attrib/generate_node_acls=1, use the existing deve->lun_flags,
	 * which be will write protected (READ-ONLY) when
	 * tpg_1/attrib/demo_mode_write_protect=1
	 */
	spin_lock_irq(&lacl->se_lun_nacl->device_list_lock);
	deve = &lacl->se_lun_nacl->device_list[lacl->mapped_lun];
	if (deve->lun_flags & TRANSPORT_LUNFLAGS_INITIATOR_ACCESS)
		lun_access = deve->lun_flags;
	else
		lun_access =
			(se_tpg->se_tpg_tfo->tpg_check_prod_mode_write_protect(
				se_tpg)) ? TRANSPORT_LUNFLAGS_READ_ONLY :
					   TRANSPORT_LUNFLAGS_READ_WRITE;
	spin_unlock_irq(&lacl->se_lun_nacl->device_list_lock);
	/*
	 * Determine the actual mapped LUN value user wants..
	 *
	 * This value is what the SCSI Initiator actually sees the
	 * iscsi/$IQN/$TPGT/lun/lun_* as on their SCSI Initiator Ports.
	 */
	ret = core_dev_add_initiator_node_lun_acl(se_tpg, lacl,
			lun->unpacked_lun, lun_access);

	return (ret < 0) ? -EINVAL : 0;
}

static int target_fabric_mappedlun_unlink(
	struct config_item *lun_acl_ci,
	struct config_item *lun_ci)
{
	struct se_lun *lun;
	struct se_lun_acl *lacl = container_of(to_config_group(lun_acl_ci),
			struct se_lun_acl, se_lun_group);
	struct se_node_acl *nacl = lacl->se_lun_nacl;
	struct se_dev_entry *deve = &nacl->device_list[lacl->mapped_lun];
	struct se_portal_group *se_tpg;
	/*
	 * Determine if the underlying MappedLUN has already been released..
	 */
	if (!deve->se_lun)
		return 0;

	lun = container_of(to_config_group(lun_ci), struct se_lun, lun_group);
	se_tpg = lun->lun_sep->sep_tpg;

	core_dev_del_initiator_node_lun_acl(se_tpg, lun, lacl);
	return 0;
}

CONFIGFS_EATTR_STRUCT(target_fabric_mappedlun, se_lun_acl);
#define TCM_MAPPEDLUN_ATTR(_name, _mode)				\
static struct target_fabric_mappedlun_attribute target_fabric_mappedlun_##_name = \
	__CONFIGFS_EATTR(_name, _mode,					\
	target_fabric_mappedlun_show_##_name,				\
	target_fabric_mappedlun_store_##_name);

static ssize_t target_fabric_mappedlun_show_write_protect(
	struct se_lun_acl *lacl,
	char *page)
{
	struct se_node_acl *se_nacl = lacl->se_lun_nacl;
	struct se_dev_entry *deve;
	ssize_t len;

	spin_lock_irq(&se_nacl->device_list_lock);
	deve = &se_nacl->device_list[lacl->mapped_lun];
	len = sprintf(page, "%d\n",
			(deve->lun_flags & TRANSPORT_LUNFLAGS_READ_ONLY) ?
			1 : 0);
	spin_unlock_irq(&se_nacl->device_list_lock);

	return len;
}

static ssize_t target_fabric_mappedlun_store_write_protect(
	struct se_lun_acl *lacl,
	const char *page,
	size_t count)
{
	struct se_node_acl *se_nacl = lacl->se_lun_nacl;
	struct se_portal_group *se_tpg = se_nacl->se_tpg;
	unsigned long op;

	if (strict_strtoul(page, 0, &op))
		return -EINVAL;

	if ((op != 1) && (op != 0))
		return -EINVAL;

	core_update_device_list_access(lacl->mapped_lun, (op) ?
			TRANSPORT_LUNFLAGS_READ_ONLY :
			TRANSPORT_LUNFLAGS_READ_WRITE,
			lacl->se_lun_nacl);

	pr_debug("%s_ConfigFS: Changed Initiator ACL: %s"
		" Mapped LUN: %u Write Protect bit to %s\n",
		se_tpg->se_tpg_tfo->get_fabric_name(),
		lacl->initiatorname, lacl->mapped_lun, (op) ? "ON" : "OFF");

	return count;

}

TCM_MAPPEDLUN_ATTR(write_protect, S_IRUGO | S_IWUSR);

CONFIGFS_EATTR_OPS(target_fabric_mappedlun, se_lun_acl, se_lun_group);

static void target_fabric_mappedlun_release(struct config_item *item)
{
	struct se_lun_acl *lacl = container_of(to_config_group(item),
				struct se_lun_acl, se_lun_group);
	struct se_portal_group *se_tpg = lacl->se_lun_nacl->se_tpg;

	core_dev_free_initiator_node_lun_acl(se_tpg, lacl);
}

static struct configfs_attribute *target_fabric_mappedlun_attrs[] = {
	&target_fabric_mappedlun_write_protect.attr,
	NULL,
};

static struct configfs_item_operations target_fabric_mappedlun_item_ops = {
	.release		= target_fabric_mappedlun_release,
	.show_attribute		= target_fabric_mappedlun_attr_show,
	.store_attribute	= target_fabric_mappedlun_attr_store,
	.allow_link		= target_fabric_mappedlun_link,
	.drop_link		= target_fabric_mappedlun_unlink,
};

TF_CIT_SETUP(tpg_mappedlun, &target_fabric_mappedlun_item_ops, NULL,
		target_fabric_mappedlun_attrs);

/* End of tfc_tpg_mappedlun_cit */

/* Start of tfc_tpg_mappedlun_port_cit */

static struct config_group *target_core_mappedlun_stat_mkdir(
	struct config_group *group,
	const char *name)
{
	return ERR_PTR(-ENOSYS);
}

static void target_core_mappedlun_stat_rmdir(
	struct config_group *group,
	struct config_item *item)
{
	return;
}

static struct configfs_group_operations target_fabric_mappedlun_stat_group_ops = {
	.make_group		= target_core_mappedlun_stat_mkdir,
	.drop_item		= target_core_mappedlun_stat_rmdir,
};

TF_CIT_SETUP(tpg_mappedlun_stat, NULL, &target_fabric_mappedlun_stat_group_ops,
		NULL);

/* End of tfc_tpg_mappedlun_port_cit */

/* Start of tfc_tpg_nacl_attrib_cit */

CONFIGFS_EATTR_OPS(target_fabric_nacl_attrib, se_node_acl, acl_attrib_group);

static struct configfs_item_operations target_fabric_nacl_attrib_item_ops = {
	.show_attribute		= target_fabric_nacl_attrib_attr_show,
	.store_attribute	= target_fabric_nacl_attrib_attr_store,
};

TF_CIT_SETUP(tpg_nacl_attrib, &target_fabric_nacl_attrib_item_ops, NULL, NULL);

/* End of tfc_tpg_nacl_attrib_cit */

/* Start of tfc_tpg_nacl_auth_cit */

CONFIGFS_EATTR_OPS(target_fabric_nacl_auth, se_node_acl, acl_auth_group);

static struct configfs_item_operations target_fabric_nacl_auth_item_ops = {
	.show_attribute		= target_fabric_nacl_auth_attr_show,
	.store_attribute	= target_fabric_nacl_auth_attr_store,
};

TF_CIT_SETUP(tpg_nacl_auth, &target_fabric_nacl_auth_item_ops, NULL, NULL);

/* End of tfc_tpg_nacl_auth_cit */

/* Start of tfc_tpg_nacl_param_cit */

CONFIGFS_EATTR_OPS(target_fabric_nacl_param, se_node_acl, acl_param_group);

static struct configfs_item_operations target_fabric_nacl_param_item_ops = {
	.show_attribute		= target_fabric_nacl_param_attr_show,
	.store_attribute	= target_fabric_nacl_param_attr_store,
};

TF_CIT_SETUP(tpg_nacl_param, &target_fabric_nacl_param_item_ops, NULL, NULL);

/* End of tfc_tpg_nacl_param_cit */

/* Start of tfc_tpg_nacl_base_cit */

CONFIGFS_EATTR_OPS(target_fabric_nacl_base, se_node_acl, acl_group);

static struct config_group *target_fabric_make_mappedlun(
	struct config_group *group,
	const char *name)
{
	struct se_node_acl *se_nacl = container_of(group,
			struct se_node_acl, acl_group);
	struct se_portal_group *se_tpg = se_nacl->se_tpg;
	struct target_fabric_configfs *tf = se_tpg->se_tpg_wwn->wwn_tf;
	struct se_lun_acl *lacl;
	struct config_item *acl_ci;
	struct config_group *lacl_cg = NULL, *ml_stat_grp = NULL;
	char *buf;
	unsigned long mapped_lun;
	int ret = 0;

	acl_ci = &group->cg_item;
	if (!acl_ci) {
		pr_err("Unable to locatel acl_ci\n");
		return NULL;
	}

	buf = kzalloc(strlen(name) + 1, GFP_KERNEL);
	if (!buf) {
		pr_err("Unable to allocate memory for name buf\n");
		return ERR_PTR(-ENOMEM);
	}
	snprintf(buf, strlen(name) + 1, "%s", name);
	/*
	 * Make sure user is creating iscsi/$IQN/$TPGT/acls/$INITIATOR/lun_$ID.
	 */
	if (strstr(buf, "lun_") != buf) {
		pr_err("Unable to locate \"lun_\" from buf: %s"
			" name: %s\n", buf, name);
		ret = -EINVAL;
		goto out;
	}
	/*
	 * Determine the Mapped LUN value.  This is what the SCSI Initiator
	 * Port will actually see.
	 */
	if (strict_strtoul(buf + 4, 0, &mapped_lun) || mapped_lun > UINT_MAX) {
		ret = -EINVAL;
		goto out;
	}

	lacl = core_dev_init_initiator_node_lun_acl(se_tpg, mapped_lun,
			config_item_name(acl_ci), &ret);
	if (!lacl) {
		ret = -EINVAL;
		goto out;
	}

	lacl_cg = &lacl->se_lun_group;
	lacl_cg->default_groups = kzalloc(sizeof(struct config_group) * 2,
				GFP_KERNEL);
	if (!lacl_cg->default_groups) {
		pr_err("Unable to allocate lacl_cg->default_groups\n");
		ret = -ENOMEM;
		goto out;
	}

	config_group_init_type_name(&lacl->se_lun_group, name,
			&TF_CIT_TMPL(tf)->tfc_tpg_mappedlun_cit);
	config_group_init_type_name(&lacl->ml_stat_grps.stat_group,
			"statistics", &TF_CIT_TMPL(tf)->tfc_tpg_mappedlun_stat_cit);
	lacl_cg->default_groups[0] = &lacl->ml_stat_grps.stat_group;
	lacl_cg->default_groups[1] = NULL;

	ml_stat_grp = &lacl->ml_stat_grps.stat_group;
	ml_stat_grp->default_groups = kzalloc(sizeof(struct config_group) * 3,
				GFP_KERNEL);
	if (!ml_stat_grp->default_groups) {
		pr_err("Unable to allocate ml_stat_grp->default_groups\n");
		ret = -ENOMEM;
		goto out;
	}
	target_stat_setup_mappedlun_default_groups(lacl);

	kfree(buf);
	return &lacl->se_lun_group;
out:
	if (lacl_cg)
		kfree(lacl_cg->default_groups);
	kfree(buf);
	return ERR_PTR(ret);
}

static void target_fabric_drop_mappedlun(
	struct config_group *group,
	struct config_item *item)
{
	struct se_lun_acl *lacl = container_of(to_config_group(item),
			struct se_lun_acl, se_lun_group);
	struct config_item *df_item;
	struct config_group *lacl_cg = NULL, *ml_stat_grp = NULL;
	int i;

	ml_stat_grp = &lacl->ml_stat_grps.stat_group;
	for (i = 0; ml_stat_grp->default_groups[i]; i++) {
		df_item = &ml_stat_grp->default_groups[i]->cg_item;
		ml_stat_grp->default_groups[i] = NULL;
		config_item_put(df_item);
	}
	kfree(ml_stat_grp->default_groups);

	lacl_cg = &lacl->se_lun_group;
	for (i = 0; lacl_cg->default_groups[i]; i++) {
		df_item = &lacl_cg->default_groups[i]->cg_item;
		lacl_cg->default_groups[i] = NULL;
		config_item_put(df_item);
	}
	kfree(lacl_cg->default_groups);

	config_item_put(item);
}

static void target_fabric_nacl_base_release(struct config_item *item)
{
	struct se_node_acl *se_nacl = container_of(to_config_group(item),
			struct se_node_acl, acl_group);
	struct se_portal_group *se_tpg = se_nacl->se_tpg;
	struct target_fabric_configfs *tf = se_tpg->se_tpg_wwn->wwn_tf;

	tf->tf_ops.fabric_drop_nodeacl(se_nacl);
}

static struct configfs_item_operations target_fabric_nacl_base_item_ops = {
	.release		= target_fabric_nacl_base_release,
	.show_attribute		= target_fabric_nacl_base_attr_show,
	.store_attribute	= target_fabric_nacl_base_attr_store,
};

static struct configfs_group_operations target_fabric_nacl_base_group_ops = {
	.make_group		= target_fabric_make_mappedlun,
	.drop_item		= target_fabric_drop_mappedlun,
};

TF_CIT_SETUP(tpg_nacl_base, &target_fabric_nacl_base_item_ops,
		&target_fabric_nacl_base_group_ops, NULL);

/* End of tfc_tpg_nacl_base_cit */

/* Start of tfc_node_fabric_stats_cit */
/*
 * This is used as a placeholder for struct se_node_acl->acl_fabric_stat_group
 * to allow fabrics access to ->acl_fabric_stat_group->default_groups[]
 */
TF_CIT_SETUP(tpg_nacl_stat, NULL, NULL, NULL);

/* End of tfc_wwn_fabric_stats_cit */

/* Start of tfc_tpg_nacl_cit */

static struct config_group *target_fabric_make_nodeacl(
	struct config_group *group,
	const char *name)
{
	struct se_portal_group *se_tpg = container_of(group,
			struct se_portal_group, tpg_acl_group);
	struct target_fabric_configfs *tf = se_tpg->se_tpg_wwn->wwn_tf;
	struct se_node_acl *se_nacl;
	struct config_group *nacl_cg;

	if (!tf->tf_ops.fabric_make_nodeacl) {
		pr_err("tf->tf_ops.fabric_make_nodeacl is NULL\n");
		return ERR_PTR(-ENOSYS);
	}

	se_nacl = tf->tf_ops.fabric_make_nodeacl(se_tpg, group, name);
	if (IS_ERR(se_nacl))
		return ERR_CAST(se_nacl);

	nacl_cg = &se_nacl->acl_group;
	nacl_cg->default_groups = se_nacl->acl_default_groups;
	nacl_cg->default_groups[0] = &se_nacl->acl_attrib_group;
	nacl_cg->default_groups[1] = &se_nacl->acl_auth_group;
	nacl_cg->default_groups[2] = &se_nacl->acl_param_group;
	nacl_cg->default_groups[3] = &se_nacl->acl_fabric_stat_group;
	nacl_cg->default_groups[4] = NULL;

	config_group_init_type_name(&se_nacl->acl_group, name,
			&TF_CIT_TMPL(tf)->tfc_tpg_nacl_base_cit);
	config_group_init_type_name(&se_nacl->acl_attrib_group, "attrib",
			&TF_CIT_TMPL(tf)->tfc_tpg_nacl_attrib_cit);
	config_group_init_type_name(&se_nacl->acl_auth_group, "auth",
			&TF_CIT_TMPL(tf)->tfc_tpg_nacl_auth_cit);
	config_group_init_type_name(&se_nacl->acl_param_group, "param",
			&TF_CIT_TMPL(tf)->tfc_tpg_nacl_param_cit);
	config_group_init_type_name(&se_nacl->acl_fabric_stat_group,
			"fabric_statistics",
			&TF_CIT_TMPL(tf)->tfc_tpg_nacl_stat_cit);

	return &se_nacl->acl_group;
}

static void target_fabric_drop_nodeacl(
	struct config_group *group,
	struct config_item *item)
{
	struct se_node_acl *se_nacl = container_of(to_config_group(item),
			struct se_node_acl, acl_group);
	struct config_item *df_item;
	struct config_group *nacl_cg;
	int i;

	nacl_cg = &se_nacl->acl_group;
	for (i = 0; nacl_cg->default_groups[i]; i++) {
		df_item = &nacl_cg->default_groups[i]->cg_item;
		nacl_cg->default_groups[i] = NULL;
		config_item_put(df_item);
	}
	/*
	 * struct se_node_acl free is done in target_fabric_nacl_base_release()
	 */
	config_item_put(item);
}

static struct configfs_group_operations target_fabric_nacl_group_ops = {
	.make_group	= target_fabric_make_nodeacl,
	.drop_item	= target_fabric_drop_nodeacl,
};

TF_CIT_SETUP(tpg_nacl, NULL, &target_fabric_nacl_group_ops, NULL);

/* End of tfc_tpg_nacl_cit */

/* Start of tfc_tpg_np_base_cit */

CONFIGFS_EATTR_OPS(target_fabric_np_base, se_tpg_np, tpg_np_group);

static void target_fabric_np_base_release(struct config_item *item)
{
	struct se_tpg_np *se_tpg_np = container_of(to_config_group(item),
				struct se_tpg_np, tpg_np_group);
	struct se_portal_group *se_tpg = se_tpg_np->tpg_np_parent;
	struct target_fabric_configfs *tf = se_tpg->se_tpg_wwn->wwn_tf;

	tf->tf_ops.fabric_drop_np(se_tpg_np);
}

static struct configfs_item_operations target_fabric_np_base_item_ops = {
	.release		= target_fabric_np_base_release,
	.show_attribute		= target_fabric_np_base_attr_show,
	.store_attribute	= target_fabric_np_base_attr_store,
};

TF_CIT_SETUP(tpg_np_base, &target_fabric_np_base_item_ops, NULL, NULL);

/* End of tfc_tpg_np_base_cit */

/* Start of tfc_tpg_np_cit */

static struct config_group *target_fabric_make_np(
	struct config_group *group,
	const char *name)
{
	struct se_portal_group *se_tpg = container_of(group,
				struct se_portal_group, tpg_np_group);
	struct target_fabric_configfs *tf = se_tpg->se_tpg_wwn->wwn_tf;
	struct se_tpg_np *se_tpg_np;

	if (!tf->tf_ops.fabric_make_np) {
		pr_err("tf->tf_ops.fabric_make_np is NULL\n");
		return ERR_PTR(-ENOSYS);
	}

	se_tpg_np = tf->tf_ops.fabric_make_np(se_tpg, group, name);
	if (!se_tpg_np || IS_ERR(se_tpg_np))
		return ERR_PTR(-EINVAL);

	se_tpg_np->tpg_np_parent = se_tpg;
	config_group_init_type_name(&se_tpg_np->tpg_np_group, name,
			&TF_CIT_TMPL(tf)->tfc_tpg_np_base_cit);

	return &se_tpg_np->tpg_np_group;
}

static void target_fabric_drop_np(
	struct config_group *group,
	struct config_item *item)
{
	/*
	 * struct se_tpg_np is released via target_fabric_np_base_release()
	 */
	config_item_put(item);
}

static struct configfs_group_operations target_fabric_np_group_ops = {
	.make_group	= &target_fabric_make_np,
	.drop_item	= &target_fabric_drop_np,
};

TF_CIT_SETUP(tpg_np, NULL, &target_fabric_np_group_ops, NULL);

/* End of tfc_tpg_np_cit */

/* Start of tfc_tpg_port_cit */

CONFIGFS_EATTR_STRUCT(target_fabric_port, se_lun);
#define TCM_PORT_ATTR(_name, _mode)					\
static struct target_fabric_port_attribute target_fabric_port_##_name =	\
	__CONFIGFS_EATTR(_name, _mode,					\
	target_fabric_port_show_attr_##_name,				\
	target_fabric_port_store_attr_##_name);

#define TCM_PORT_ATTOR_RO(_name)					\
	__CONFIGFS_EATTR_RO(_name,					\
	target_fabric_port_show_attr_##_name);

/*
 * alua_tg_pt_gp
 */
static ssize_t target_fabric_port_show_attr_alua_tg_pt_gp(
	struct se_lun *lun,
	char *page)
{
	if (!lun || !lun->lun_sep)
		return -ENODEV;

	return core_alua_show_tg_pt_gp_info(lun->lun_sep, page);
}

static ssize_t target_fabric_port_store_attr_alua_tg_pt_gp(
	struct se_lun *lun,
	const char *page,
	size_t count)
{
	if (!lun || !lun->lun_sep)
		return -ENODEV;

	return core_alua_store_tg_pt_gp_info(lun->lun_sep, page, count);
}

TCM_PORT_ATTR(alua_tg_pt_gp, S_IRUGO | S_IWUSR);

/*
 * alua_tg_pt_offline
 */
static ssize_t target_fabric_port_show_attr_alua_tg_pt_offline(
	struct se_lun *lun,
	char *page)
{
	if (!lun || !lun->lun_sep)
		return -ENODEV;

	return core_alua_show_offline_bit(lun, page);
}

static ssize_t target_fabric_port_store_attr_alua_tg_pt_offline(
	struct se_lun *lun,
	const char *page,
	size_t count)
{
	if (!lun || !lun->lun_sep)
		return -ENODEV;

	return core_alua_store_offline_bit(lun, page, count);
}

TCM_PORT_ATTR(alua_tg_pt_offline, S_IRUGO | S_IWUSR);

/*
 * alua_tg_pt_status
 */
static ssize_t target_fabric_port_show_attr_alua_tg_pt_status(
	struct se_lun *lun,
	char *page)
{
	if (!lun || !lun->lun_sep)
		return -ENODEV;

	return core_alua_show_secondary_status(lun, page);
}

static ssize_t target_fabric_port_store_attr_alua_tg_pt_status(
	struct se_lun *lun,
	const char *page,
	size_t count)
{
	if (!lun || !lun->lun_sep)
		return -ENODEV;

	return core_alua_store_secondary_status(lun, page, count);
}

TCM_PORT_ATTR(alua_tg_pt_status, S_IRUGO | S_IWUSR);

/*
 * alua_tg_pt_write_md
 */
static ssize_t target_fabric_port_show_attr_alua_tg_pt_write_md(
	struct se_lun *lun,
	char *page)
{
	if (!lun || !lun->lun_sep)
		return -ENODEV;

	return core_alua_show_secondary_write_metadata(lun, page);
}

static ssize_t target_fabric_port_store_attr_alua_tg_pt_write_md(
	struct se_lun *lun,
	const char *page,
	size_t count)
{
	if (!lun || !lun->lun_sep)
		return -ENODEV;

	return core_alua_store_secondary_write_metadata(lun, page, count);
}

TCM_PORT_ATTR(alua_tg_pt_write_md, S_IRUGO | S_IWUSR);


static struct configfs_attribute *target_fabric_port_attrs[] = {
	&target_fabric_port_alua_tg_pt_gp.attr,
	&target_fabric_port_alua_tg_pt_offline.attr,
	&target_fabric_port_alua_tg_pt_status.attr,
	&target_fabric_port_alua_tg_pt_write_md.attr,
	NULL,
};

CONFIGFS_EATTR_OPS(target_fabric_port, se_lun, lun_group);

static int target_fabric_port_link(
	struct config_item *lun_ci,
	struct config_item *se_dev_ci)
{
	struct config_item *tpg_ci;
	struct se_device *dev;
	struct se_lun *lun = container_of(to_config_group(lun_ci),
				struct se_lun, lun_group);
	struct se_lun *lun_p;
	struct se_portal_group *se_tpg;
	struct se_subsystem_dev *se_dev = container_of(
				to_config_group(se_dev_ci), struct se_subsystem_dev,
				se_dev_group);
	struct target_fabric_configfs *tf;
	int ret;

	tpg_ci = &lun_ci->ci_parent->ci_group->cg_item;
	se_tpg = container_of(to_config_group(tpg_ci),
				struct se_portal_group, tpg_group);
	tf = se_tpg->se_tpg_wwn->wwn_tf;

	if (lun->lun_se_dev !=  NULL) {
		pr_err("Port Symlink already exists\n");
		return -EEXIST;
	}

	dev = se_dev->se_dev_ptr;
	if (!dev) {
		pr_err("Unable to locate struct se_device pointer from"
			" %s\n", config_item_name(se_dev_ci));
		ret = -ENODEV;
		goto out;
	}

	lun_p = core_dev_add_lun(se_tpg, dev->se_hba, dev,
				lun->unpacked_lun);
	if (IS_ERR(lun_p) || !lun_p) {
		pr_err("core_dev_add_lun() failed\n");
		ret = -EINVAL;
		goto out;
	}

	if (tf->tf_ops.fabric_post_link) {
		/*
		 * Call the optional fabric_post_link() to allow a
		 * fabric module to setup any additional state once
		 * core_dev_add_lun() has been called..
		 */
		tf->tf_ops.fabric_post_link(se_tpg, lun);
	}

	return 0;
out:
	return ret;
}

static int target_fabric_port_unlink(
	struct config_item *lun_ci,
	struct config_item *se_dev_ci)
{
	struct se_lun *lun = container_of(to_config_group(lun_ci),
				struct se_lun, lun_group);
	struct se_portal_group *se_tpg = lun->lun_sep->sep_tpg;
	struct target_fabric_configfs *tf = se_tpg->se_tpg_wwn->wwn_tf;

	if (tf->tf_ops.fabric_pre_unlink) {
		/*
		 * Call the optional fabric_pre_unlink() to allow a
		 * fabric module to release any additional stat before
		 * core_dev_del_lun() is called.
		*/
		tf->tf_ops.fabric_pre_unlink(se_tpg, lun);
	}

	core_dev_del_lun(se_tpg, lun->unpacked_lun);
	return 0;
}

static struct configfs_item_operations target_fabric_port_item_ops = {
	.show_attribute		= target_fabric_port_attr_show,
	.store_attribute	= target_fabric_port_attr_store,
	.allow_link		= target_fabric_port_link,
	.drop_link		= target_fabric_port_unlink,
};

TF_CIT_SETUP(tpg_port, &target_fabric_port_item_ops, NULL, target_fabric_port_attrs);

/* End of tfc_tpg_port_cit */

/* Start of tfc_tpg_port_stat_cit */

static struct config_group *target_core_port_stat_mkdir(
	struct config_group *group,
	const char *name)
{
	return ERR_PTR(-ENOSYS);
}

static void target_core_port_stat_rmdir(
	struct config_group *group,
	struct config_item *item)
{
	return;
}

static struct configfs_group_operations target_fabric_port_stat_group_ops = {
	.make_group		= target_core_port_stat_mkdir,
	.drop_item		= target_core_port_stat_rmdir,
};

TF_CIT_SETUP(tpg_port_stat, NULL, &target_fabric_port_stat_group_ops, NULL);

/* End of tfc_tpg_port_stat_cit */

/* Start of tfc_tpg_lun_cit */

static struct config_group *target_fabric_make_lun(
	struct config_group *group,
	const char *name)
{
	struct se_lun *lun;
	struct se_portal_group *se_tpg = container_of(group,
			struct se_portal_group, tpg_lun_group);
	struct target_fabric_configfs *tf = se_tpg->se_tpg_wwn->wwn_tf;
	struct config_group *lun_cg = NULL, *port_stat_grp = NULL;
	unsigned long unpacked_lun;
	int errno;

	if (strstr(name, "lun_") != name) {
		pr_err("Unable to locate \'_\" in"
				" \"lun_$LUN_NUMBER\"\n");
		return ERR_PTR(-EINVAL);
	}
	if (strict_strtoul(name + 4, 0, &unpacked_lun) || unpacked_lun > UINT_MAX)
		return ERR_PTR(-EINVAL);

	lun = core_get_lun_from_tpg(se_tpg, unpacked_lun);
	if (!lun)
		return ERR_PTR(-EINVAL);

	lun_cg = &lun->lun_group;
	lun_cg->default_groups = kzalloc(sizeof(struct config_group) * 2,
				GFP_KERNEL);
	if (!lun_cg->default_groups) {
		pr_err("Unable to allocate lun_cg->default_groups\n");
		return ERR_PTR(-ENOMEM);
	}

	config_group_init_type_name(&lun->lun_group, name,
			&TF_CIT_TMPL(tf)->tfc_tpg_port_cit);
	config_group_init_type_name(&lun->port_stat_grps.stat_group,
			"statistics", &TF_CIT_TMPL(tf)->tfc_tpg_port_stat_cit);
	lun_cg->default_groups[0] = &lun->port_stat_grps.stat_group;
	lun_cg->default_groups[1] = NULL;

	port_stat_grp = &lun->port_stat_grps.stat_group;
	port_stat_grp->default_groups =  kzalloc(sizeof(struct config_group) * 3,
				GFP_KERNEL);
	if (!port_stat_grp->default_groups) {
		pr_err("Unable to allocate port_stat_grp->default_groups\n");
		errno = -ENOMEM;
		goto out;
	}
	target_stat_setup_port_default_groups(lun);

	return &lun->lun_group;
out:
	if (lun_cg)
		kfree(lun_cg->default_groups);
	return ERR_PTR(errno);
}

static void target_fabric_drop_lun(
	struct config_group *group,
	struct config_item *item)
{
	struct se_lun *lun = container_of(to_config_group(item),
				struct se_lun, lun_group);
	struct config_item *df_item;
	struct config_group *lun_cg, *port_stat_grp;
	int i;

	port_stat_grp = &lun->port_stat_grps.stat_group;
	for (i = 0; port_stat_grp->default_groups[i]; i++) {
		df_item = &port_stat_grp->default_groups[i]->cg_item;
		port_stat_grp->default_groups[i] = NULL;
		config_item_put(df_item);
	}
	kfree(port_stat_grp->default_groups);

	lun_cg = &lun->lun_group;
	for (i = 0; lun_cg->default_groups[i]; i++) {
		df_item = &lun_cg->default_groups[i]->cg_item;
		lun_cg->default_groups[i] = NULL;
		config_item_put(df_item);
	}
	kfree(lun_cg->default_groups);

	config_item_put(item);
}

static struct configfs_group_operations target_fabric_lun_group_ops = {
	.make_group	= &target_fabric_make_lun,
	.drop_item	= &target_fabric_drop_lun,
};

TF_CIT_SETUP(tpg_lun, NULL, &target_fabric_lun_group_ops, NULL);

/* End of tfc_tpg_lun_cit */

/* Start of tfc_tpg_attrib_cit */

CONFIGFS_EATTR_OPS(target_fabric_tpg_attrib, se_portal_group, tpg_attrib_group);

static struct configfs_item_operations target_fabric_tpg_attrib_item_ops = {
	.show_attribute		= target_fabric_tpg_attrib_attr_show,
	.store_attribute	= target_fabric_tpg_attrib_attr_store,
};

TF_CIT_SETUP(tpg_attrib, &target_fabric_tpg_attrib_item_ops, NULL, NULL);

/* End of tfc_tpg_attrib_cit */

/* Start of tfc_tpg_param_cit */

CONFIGFS_EATTR_OPS(target_fabric_tpg_param, se_portal_group, tpg_param_group);

static struct configfs_item_operations target_fabric_tpg_param_item_ops = {
	.show_attribute		= target_fabric_tpg_param_attr_show,
	.store_attribute	= target_fabric_tpg_param_attr_store,
};

TF_CIT_SETUP(tpg_param, &target_fabric_tpg_param_item_ops, NULL, NULL);

/* End of tfc_tpg_param_cit */

/* Start of tfc_tpg_base_cit */
/*
 * For use with TF_TPG_ATTR() and TF_TPG_ATTR_RO()
 */
CONFIGFS_EATTR_OPS(target_fabric_tpg, se_portal_group, tpg_group);

static void target_fabric_tpg_release(struct config_item *item)
{
	struct se_portal_group *se_tpg = container_of(to_config_group(item),
			struct se_portal_group, tpg_group);
	struct se_wwn *wwn = se_tpg->se_tpg_wwn;
	struct target_fabric_configfs *tf = wwn->wwn_tf;

	tf->tf_ops.fabric_drop_tpg(se_tpg);
}

static struct configfs_item_operations target_fabric_tpg_base_item_ops = {
	.release		= target_fabric_tpg_release,
	.show_attribute		= target_fabric_tpg_attr_show,
	.store_attribute	= target_fabric_tpg_attr_store,
};

TF_CIT_SETUP(tpg_base, &target_fabric_tpg_base_item_ops, NULL, NULL);

/* End of tfc_tpg_base_cit */

/* Start of tfc_tpg_cit */

static struct config_group *target_fabric_make_tpg(
	struct config_group *group,
	const char *name)
{
	struct se_wwn *wwn = container_of(group, struct se_wwn, wwn_group);
	struct target_fabric_configfs *tf = wwn->wwn_tf;
	struct se_portal_group *se_tpg;

	if (!tf->tf_ops.fabric_make_tpg) {
		pr_err("tf->tf_ops.fabric_make_tpg is NULL\n");
		return ERR_PTR(-ENOSYS);
	}

	se_tpg = tf->tf_ops.fabric_make_tpg(wwn, group, name);
	if (!se_tpg || IS_ERR(se_tpg))
		return ERR_PTR(-EINVAL);
	/*
	 * Setup default groups from pre-allocated se_tpg->tpg_default_groups
	 */
	se_tpg->tpg_group.default_groups = se_tpg->tpg_default_groups;
	se_tpg->tpg_group.default_groups[0] = &se_tpg->tpg_lun_group;
	se_tpg->tpg_group.default_groups[1] = &se_tpg->tpg_np_group;
	se_tpg->tpg_group.default_groups[2] = &se_tpg->tpg_acl_group;
	se_tpg->tpg_group.default_groups[3] = &se_tpg->tpg_attrib_group;
	se_tpg->tpg_group.default_groups[4] = &se_tpg->tpg_param_group;
	se_tpg->tpg_group.default_groups[5] = NULL;

	config_group_init_type_name(&se_tpg->tpg_group, name,
			&TF_CIT_TMPL(tf)->tfc_tpg_base_cit);
	config_group_init_type_name(&se_tpg->tpg_lun_group, "lun",
			&TF_CIT_TMPL(tf)->tfc_tpg_lun_cit);
	config_group_init_type_name(&se_tpg->tpg_np_group, "np",
			&TF_CIT_TMPL(tf)->tfc_tpg_np_cit);
	config_group_init_type_name(&se_tpg->tpg_acl_group, "acls",
			&TF_CIT_TMPL(tf)->tfc_tpg_nacl_cit);
	config_group_init_type_name(&se_tpg->tpg_attrib_group, "attrib",
			&TF_CIT_TMPL(tf)->tfc_tpg_attrib_cit);
	config_group_init_type_name(&se_tpg->tpg_param_group, "param",
			&TF_CIT_TMPL(tf)->tfc_tpg_param_cit);

	return &se_tpg->tpg_group;
}

static void target_fabric_drop_tpg(
	struct config_group *group,
	struct config_item *item)
{
	struct se_portal_group *se_tpg = container_of(to_config_group(item),
				struct se_portal_group, tpg_group);
	struct config_group *tpg_cg = &se_tpg->tpg_group;
	struct config_item *df_item;
	int i;
	/*
	 * Release default groups, but do not release tpg_cg->default_groups
	 * memory as it is statically allocated at se_tpg->tpg_default_groups.
	 */
	for (i = 0; tpg_cg->default_groups[i]; i++) {
		df_item = &tpg_cg->default_groups[i]->cg_item;
		tpg_cg->default_groups[i] = NULL;
		config_item_put(df_item);
	}

	config_item_put(item);
}

static void target_fabric_release_wwn(struct config_item *item)
{
	struct se_wwn *wwn = container_of(to_config_group(item),
				struct se_wwn, wwn_group);
	struct target_fabric_configfs *tf = wwn->wwn_tf;

	tf->tf_ops.fabric_drop_wwn(wwn);
}

static struct configfs_item_operations target_fabric_tpg_item_ops = {
	.release	= target_fabric_release_wwn,
};

static struct configfs_group_operations target_fabric_tpg_group_ops = {
	.make_group	= target_fabric_make_tpg,
	.drop_item	= target_fabric_drop_tpg,
};

TF_CIT_SETUP(tpg, &target_fabric_tpg_item_ops, &target_fabric_tpg_group_ops,
		NULL);

/* End of tfc_tpg_cit */

/* Start of tfc_wwn_fabric_stats_cit */
/*
 * This is used as a placeholder for struct se_wwn->fabric_stat_group
 * to allow fabrics access to ->fabric_stat_group->default_groups[]
 */
TF_CIT_SETUP(wwn_fabric_stats, NULL, NULL, NULL);

/* End of tfc_wwn_fabric_stats_cit */

/* Start of tfc_wwn_cit */

static struct config_group *target_fabric_make_wwn(
	struct config_group *group,
	const char *name)
{
	struct target_fabric_configfs *tf = container_of(group,
				struct target_fabric_configfs, tf_group);
	struct se_wwn *wwn;

	if (!tf->tf_ops.fabric_make_wwn) {
		pr_err("tf->tf_ops.fabric_make_wwn is NULL\n");
		return ERR_PTR(-ENOSYS);
	}

	wwn = tf->tf_ops.fabric_make_wwn(tf, group, name);
	if (!wwn || IS_ERR(wwn))
		return ERR_PTR(-EINVAL);

	wwn->wwn_tf = tf;
	/*
	 * Setup default groups from pre-allocated wwn->wwn_default_groups
	 */
	wwn->wwn_group.default_groups = wwn->wwn_default_groups;
	wwn->wwn_group.default_groups[0] = &wwn->fabric_stat_group;
	wwn->wwn_group.default_groups[1] = NULL;

	config_group_init_type_name(&wwn->wwn_group, name,
			&TF_CIT_TMPL(tf)->tfc_tpg_cit);
	config_group_init_type_name(&wwn->fabric_stat_group, "fabric_statistics",
			&TF_CIT_TMPL(tf)->tfc_wwn_fabric_stats_cit);

	return &wwn->wwn_group;
}

static void target_fabric_drop_wwn(
	struct config_group *group,
	struct config_item *item)
{
	struct se_wwn *wwn = container_of(to_config_group(item),
				struct se_wwn, wwn_group);
	struct config_item *df_item;
	struct config_group *cg = &wwn->wwn_group;
	int i;

	for (i = 0; cg->default_groups[i]; i++) {
		df_item = &cg->default_groups[i]->cg_item;
		cg->default_groups[i] = NULL;
		config_item_put(df_item);
	}

	config_item_put(item);
}

static struct configfs_group_operations target_fabric_wwn_group_ops = {
	.make_group	= target_fabric_make_wwn,
	.drop_item	= target_fabric_drop_wwn,
};
/*
 * For use with TF_WWN_ATTR() and TF_WWN_ATTR_RO()
 */
CONFIGFS_EATTR_OPS(target_fabric_wwn, target_fabric_configfs, tf_group);

static struct configfs_item_operations target_fabric_wwn_item_ops = {
	.show_attribute		= target_fabric_wwn_attr_show,
	.store_attribute	= target_fabric_wwn_attr_store,
};

TF_CIT_SETUP(wwn, &target_fabric_wwn_item_ops, &target_fabric_wwn_group_ops, NULL);

/* End of tfc_wwn_cit */

/* Start of tfc_discovery_cit */

CONFIGFS_EATTR_OPS(target_fabric_discovery, target_fabric_configfs,
		tf_disc_group);

static struct configfs_item_operations target_fabric_discovery_item_ops = {
	.show_attribute		= target_fabric_discovery_attr_show,
	.store_attribute	= target_fabric_discovery_attr_store,
};

TF_CIT_SETUP(discovery, &target_fabric_discovery_item_ops, NULL, NULL);

/* End of tfc_discovery_cit */

int target_fabric_setup_cits(struct target_fabric_configfs *tf)
{
	target_fabric_setup_discovery_cit(tf);
	target_fabric_setup_wwn_cit(tf);
	target_fabric_setup_wwn_fabric_stats_cit(tf);
	target_fabric_setup_tpg_cit(tf);
	target_fabric_setup_tpg_base_cit(tf);
	target_fabric_setup_tpg_port_cit(tf);
	target_fabric_setup_tpg_port_stat_cit(tf);
	target_fabric_setup_tpg_lun_cit(tf);
	target_fabric_setup_tpg_np_cit(tf);
	target_fabric_setup_tpg_np_base_cit(tf);
	target_fabric_setup_tpg_attrib_cit(tf);
	target_fabric_setup_tpg_param_cit(tf);
	target_fabric_setup_tpg_nacl_cit(tf);
	target_fabric_setup_tpg_nacl_base_cit(tf);
	target_fabric_setup_tpg_nacl_attrib_cit(tf);
	target_fabric_setup_tpg_nacl_auth_cit(tf);
	target_fabric_setup_tpg_nacl_param_cit(tf);
	target_fabric_setup_tpg_nacl_stat_cit(tf);
	target_fabric_setup_tpg_mappedlun_cit(tf);
	target_fabric_setup_tpg_mappedlun_stat_cit(tf);

	return 0;
}

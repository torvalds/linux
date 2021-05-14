// SPDX-License-Identifier: GPL-2.0-or-later
/*******************************************************************************
* Filename: target_core_fabric_configfs.c
 *
 * This file contains generic fabric module configfs infrastructure for
 * TCM v4.x code
 *
 * (c) Copyright 2010-2013 Datera, Inc.
 *
 * Nicholas A. Bellinger <nab@linux-iscsi.org>
*
 ****************************************************************************/

#include <linux/module.h>
#include <linux/moduleparam.h>
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
#include <target/target_core_backend.h>
#include <target/target_core_fabric.h>

#include "target_core_internal.h"
#include "target_core_alua.h"
#include "target_core_pr.h"

#define TF_CIT_SETUP(_name, _item_ops, _group_ops, _attrs)		\
static void target_fabric_setup_##_name##_cit(struct target_fabric_configfs *tf) \
{									\
	struct config_item_type *cit = &tf->tf_##_name##_cit;		\
									\
	cit->ct_item_ops = _item_ops;					\
	cit->ct_group_ops = _group_ops;					\
	cit->ct_attrs = _attrs;						\
	cit->ct_owner = tf->tf_ops->module;				\
	pr_debug("Setup generic %s\n", __stringify(_name));		\
}

#define TF_CIT_SETUP_DRV(_name, _item_ops, _group_ops)		\
static void target_fabric_setup_##_name##_cit(struct target_fabric_configfs *tf) \
{									\
	struct config_item_type *cit = &tf->tf_##_name##_cit;		\
	struct configfs_attribute **attrs = tf->tf_ops->tfc_##_name##_attrs; \
									\
	cit->ct_item_ops = _item_ops;					\
	cit->ct_group_ops = _group_ops;					\
	cit->ct_attrs = attrs;						\
	cit->ct_owner = tf->tf_ops->module;				\
	pr_debug("Setup generic %s\n", __stringify(_name));		\
}

static struct configfs_item_operations target_fabric_port_item_ops;

/* Start of tfc_tpg_mappedlun_cit */

static int target_fabric_mappedlun_link(
	struct config_item *lun_acl_ci,
	struct config_item *lun_ci)
{
	struct se_dev_entry *deve;
	struct se_lun *lun;
	struct se_lun_acl *lacl = container_of(to_config_group(lun_acl_ci),
			struct se_lun_acl, se_lun_group);
	struct se_portal_group *se_tpg;
	struct config_item *nacl_ci, *tpg_ci, *tpg_ci_s, *wwn_ci, *wwn_ci_s;
	bool lun_access_ro;

	if (!lun_ci->ci_type ||
	    lun_ci->ci_type->ct_item_ops != &target_fabric_port_item_ops) {
		pr_err("Bad lun_ci, not a valid lun_ci pointer: %p\n", lun_ci);
		return -EFAULT;
	}
	lun = container_of(to_config_group(lun_ci), struct se_lun, lun_group);

	/*
	 * Ensure that the source port exists
	 */
	if (!lun->lun_se_dev) {
		pr_err("Source se_lun->lun_se_dev does not exist\n");
		return -EINVAL;
	}
	if (lun->lun_shutdown) {
		pr_err("Unable to create mappedlun symlink because"
			" lun->lun_shutdown=true\n");
		return -EINVAL;
	}
	se_tpg = lun->lun_tpg;

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
	 * tpg_1/attrib/generate_node_acls=1, use the existing
	 * deve->lun_access_ro value, which will be true when
	 * tpg_1/attrib/demo_mode_write_protect=1
	 */
	rcu_read_lock();
	deve = target_nacl_find_deve(lacl->se_lun_nacl, lacl->mapped_lun);
	if (deve)
		lun_access_ro = deve->lun_access_ro;
	else
		lun_access_ro =
			(se_tpg->se_tpg_tfo->tpg_check_prod_mode_write_protect(
				se_tpg)) ? true : false;
	rcu_read_unlock();
	/*
	 * Determine the actual mapped LUN value user wants..
	 *
	 * This value is what the SCSI Initiator actually sees the
	 * $FABRIC/$WWPN/$TPGT/lun/lun_* as on their SCSI Initiator Ports.
	 */
	return core_dev_add_initiator_node_lun_acl(se_tpg, lacl, lun, lun_access_ro);
}

static void target_fabric_mappedlun_unlink(
	struct config_item *lun_acl_ci,
	struct config_item *lun_ci)
{
	struct se_lun_acl *lacl = container_of(to_config_group(lun_acl_ci),
			struct se_lun_acl, se_lun_group);
	struct se_lun *lun = container_of(to_config_group(lun_ci),
			struct se_lun, lun_group);

	core_dev_del_initiator_node_lun_acl(lun, lacl);
}

static struct se_lun_acl *item_to_lun_acl(struct config_item *item)
{
	return container_of(to_config_group(item), struct se_lun_acl,
			se_lun_group);
}

static ssize_t target_fabric_mappedlun_write_protect_show(
		struct config_item *item, char *page)
{
	struct se_lun_acl *lacl = item_to_lun_acl(item);
	struct se_node_acl *se_nacl = lacl->se_lun_nacl;
	struct se_dev_entry *deve;
	ssize_t len = 0;

	rcu_read_lock();
	deve = target_nacl_find_deve(se_nacl, lacl->mapped_lun);
	if (deve) {
		len = sprintf(page, "%d\n", deve->lun_access_ro);
	}
	rcu_read_unlock();

	return len;
}

static ssize_t target_fabric_mappedlun_write_protect_store(
		struct config_item *item, const char *page, size_t count)
{
	struct se_lun_acl *lacl = item_to_lun_acl(item);
	struct se_node_acl *se_nacl = lacl->se_lun_nacl;
	struct se_portal_group *se_tpg = se_nacl->se_tpg;
	unsigned long wp;
	int ret;

	ret = kstrtoul(page, 0, &wp);
	if (ret)
		return ret;

	if ((wp != 1) && (wp != 0))
		return -EINVAL;

	/* wp=1 means lun_access_ro=true */
	core_update_device_list_access(lacl->mapped_lun, wp, lacl->se_lun_nacl);

	pr_debug("%s_ConfigFS: Changed Initiator ACL: %s"
		" Mapped LUN: %llu Write Protect bit to %s\n",
		se_tpg->se_tpg_tfo->fabric_name,
		se_nacl->initiatorname, lacl->mapped_lun, (wp) ? "ON" : "OFF");

	return count;

}

CONFIGFS_ATTR(target_fabric_mappedlun_, write_protect);

static struct configfs_attribute *target_fabric_mappedlun_attrs[] = {
	&target_fabric_mappedlun_attr_write_protect,
	NULL,
};

static void target_fabric_mappedlun_release(struct config_item *item)
{
	struct se_lun_acl *lacl = container_of(to_config_group(item),
				struct se_lun_acl, se_lun_group);
	struct se_portal_group *se_tpg = lacl->se_lun_nacl->se_tpg;

	core_dev_free_initiator_node_lun_acl(se_tpg, lacl);
}

static struct configfs_item_operations target_fabric_mappedlun_item_ops = {
	.release		= target_fabric_mappedlun_release,
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

TF_CIT_SETUP_DRV(tpg_nacl_attrib, NULL, NULL);
TF_CIT_SETUP_DRV(tpg_nacl_auth, NULL, NULL);
TF_CIT_SETUP_DRV(tpg_nacl_param, NULL, NULL);

/* Start of tfc_tpg_nacl_base_cit */

static struct config_group *target_fabric_make_mappedlun(
	struct config_group *group,
	const char *name)
{
	struct se_node_acl *se_nacl = container_of(group,
			struct se_node_acl, acl_group);
	struct se_portal_group *se_tpg = se_nacl->se_tpg;
	struct target_fabric_configfs *tf = se_tpg->se_tpg_wwn->wwn_tf;
	struct se_lun_acl *lacl = NULL;
	char *buf;
	unsigned long long mapped_lun;
	int ret = 0;

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
	ret = kstrtoull(buf + 4, 0, &mapped_lun);
	if (ret)
		goto out;

	lacl = core_dev_init_initiator_node_lun_acl(se_tpg, se_nacl,
			mapped_lun, &ret);
	if (!lacl) {
		ret = -EINVAL;
		goto out;
	}

	config_group_init_type_name(&lacl->se_lun_group, name,
			&tf->tf_tpg_mappedlun_cit);

	config_group_init_type_name(&lacl->ml_stat_grps.stat_group,
			"statistics", &tf->tf_tpg_mappedlun_stat_cit);
	configfs_add_default_group(&lacl->ml_stat_grps.stat_group,
			&lacl->se_lun_group);

	target_stat_setup_mappedlun_default_groups(lacl);

	kfree(buf);
	return &lacl->se_lun_group;
out:
	kfree(lacl);
	kfree(buf);
	return ERR_PTR(ret);
}

static void target_fabric_drop_mappedlun(
	struct config_group *group,
	struct config_item *item)
{
	struct se_lun_acl *lacl = container_of(to_config_group(item),
			struct se_lun_acl, se_lun_group);

	configfs_remove_default_groups(&lacl->ml_stat_grps.stat_group);
	configfs_remove_default_groups(&lacl->se_lun_group);

	config_item_put(item);
}

static void target_fabric_nacl_base_release(struct config_item *item)
{
	struct se_node_acl *se_nacl = container_of(to_config_group(item),
			struct se_node_acl, acl_group);

	configfs_remove_default_groups(&se_nacl->acl_fabric_stat_group);
	core_tpg_del_initiator_node_acl(se_nacl);
}

static struct configfs_item_operations target_fabric_nacl_base_item_ops = {
	.release		= target_fabric_nacl_base_release,
};

static struct configfs_group_operations target_fabric_nacl_base_group_ops = {
	.make_group		= target_fabric_make_mappedlun,
	.drop_item		= target_fabric_drop_mappedlun,
};

TF_CIT_SETUP_DRV(tpg_nacl_base, &target_fabric_nacl_base_item_ops,
		&target_fabric_nacl_base_group_ops);

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

	se_nacl = core_tpg_add_initiator_node_acl(se_tpg, name);
	if (IS_ERR(se_nacl))
		return ERR_CAST(se_nacl);

	config_group_init_type_name(&se_nacl->acl_group, name,
			&tf->tf_tpg_nacl_base_cit);

	config_group_init_type_name(&se_nacl->acl_attrib_group, "attrib",
			&tf->tf_tpg_nacl_attrib_cit);
	configfs_add_default_group(&se_nacl->acl_attrib_group,
			&se_nacl->acl_group);

	config_group_init_type_name(&se_nacl->acl_auth_group, "auth",
			&tf->tf_tpg_nacl_auth_cit);
	configfs_add_default_group(&se_nacl->acl_auth_group,
			&se_nacl->acl_group);

	config_group_init_type_name(&se_nacl->acl_param_group, "param",
			&tf->tf_tpg_nacl_param_cit);
	configfs_add_default_group(&se_nacl->acl_param_group,
			&se_nacl->acl_group);

	config_group_init_type_name(&se_nacl->acl_fabric_stat_group,
			"fabric_statistics", &tf->tf_tpg_nacl_stat_cit);
	configfs_add_default_group(&se_nacl->acl_fabric_stat_group,
			&se_nacl->acl_group);

	if (tf->tf_ops->fabric_init_nodeacl) {
		int ret = tf->tf_ops->fabric_init_nodeacl(se_nacl, name);
		if (ret) {
			configfs_remove_default_groups(&se_nacl->acl_fabric_stat_group);
			core_tpg_del_initiator_node_acl(se_nacl);
			return ERR_PTR(ret);
		}
	}

	return &se_nacl->acl_group;
}

static void target_fabric_drop_nodeacl(
	struct config_group *group,
	struct config_item *item)
{
	struct se_node_acl *se_nacl = container_of(to_config_group(item),
			struct se_node_acl, acl_group);

	configfs_remove_default_groups(&se_nacl->acl_group);

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

static void target_fabric_np_base_release(struct config_item *item)
{
	struct se_tpg_np *se_tpg_np = container_of(to_config_group(item),
				struct se_tpg_np, tpg_np_group);
	struct se_portal_group *se_tpg = se_tpg_np->tpg_np_parent;
	struct target_fabric_configfs *tf = se_tpg->se_tpg_wwn->wwn_tf;

	tf->tf_ops->fabric_drop_np(se_tpg_np);
}

static struct configfs_item_operations target_fabric_np_base_item_ops = {
	.release		= target_fabric_np_base_release,
};

TF_CIT_SETUP_DRV(tpg_np_base, &target_fabric_np_base_item_ops, NULL);

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

	if (!tf->tf_ops->fabric_make_np) {
		pr_err("tf->tf_ops.fabric_make_np is NULL\n");
		return ERR_PTR(-ENOSYS);
	}

	se_tpg_np = tf->tf_ops->fabric_make_np(se_tpg, group, name);
	if (!se_tpg_np || IS_ERR(se_tpg_np))
		return ERR_PTR(-EINVAL);

	se_tpg_np->tpg_np_parent = se_tpg;
	config_group_init_type_name(&se_tpg_np->tpg_np_group, name,
			&tf->tf_tpg_np_base_cit);

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

static struct se_lun *item_to_lun(struct config_item *item)
{
	return container_of(to_config_group(item), struct se_lun,
			lun_group);
}

static ssize_t target_fabric_port_alua_tg_pt_gp_show(struct config_item *item,
		char *page)
{
	struct se_lun *lun = item_to_lun(item);

	if (!lun || !lun->lun_se_dev)
		return -ENODEV;

	return core_alua_show_tg_pt_gp_info(lun, page);
}

static ssize_t target_fabric_port_alua_tg_pt_gp_store(struct config_item *item,
		const char *page, size_t count)
{
	struct se_lun *lun = item_to_lun(item);

	if (!lun || !lun->lun_se_dev)
		return -ENODEV;

	return core_alua_store_tg_pt_gp_info(lun, page, count);
}

static ssize_t target_fabric_port_alua_tg_pt_offline_show(
		struct config_item *item, char *page)
{
	struct se_lun *lun = item_to_lun(item);

	if (!lun || !lun->lun_se_dev)
		return -ENODEV;

	return core_alua_show_offline_bit(lun, page);
}

static ssize_t target_fabric_port_alua_tg_pt_offline_store(
		struct config_item *item, const char *page, size_t count)
{
	struct se_lun *lun = item_to_lun(item);

	if (!lun || !lun->lun_se_dev)
		return -ENODEV;

	return core_alua_store_offline_bit(lun, page, count);
}

static ssize_t target_fabric_port_alua_tg_pt_status_show(
		struct config_item *item, char *page)
{
	struct se_lun *lun = item_to_lun(item);

	if (!lun || !lun->lun_se_dev)
		return -ENODEV;

	return core_alua_show_secondary_status(lun, page);
}

static ssize_t target_fabric_port_alua_tg_pt_status_store(
		struct config_item *item, const char *page, size_t count)
{
	struct se_lun *lun = item_to_lun(item);

	if (!lun || !lun->lun_se_dev)
		return -ENODEV;

	return core_alua_store_secondary_status(lun, page, count);
}

static ssize_t target_fabric_port_alua_tg_pt_write_md_show(
		struct config_item *item, char *page)
{
	struct se_lun *lun = item_to_lun(item);

	if (!lun || !lun->lun_se_dev)
		return -ENODEV;

	return core_alua_show_secondary_write_metadata(lun, page);
}

static ssize_t target_fabric_port_alua_tg_pt_write_md_store(
		struct config_item *item, const char *page, size_t count)
{
	struct se_lun *lun = item_to_lun(item);

	if (!lun || !lun->lun_se_dev)
		return -ENODEV;

	return core_alua_store_secondary_write_metadata(lun, page, count);
}

CONFIGFS_ATTR(target_fabric_port_, alua_tg_pt_gp);
CONFIGFS_ATTR(target_fabric_port_, alua_tg_pt_offline);
CONFIGFS_ATTR(target_fabric_port_, alua_tg_pt_status);
CONFIGFS_ATTR(target_fabric_port_, alua_tg_pt_write_md);

static struct configfs_attribute *target_fabric_port_attrs[] = {
	&target_fabric_port_attr_alua_tg_pt_gp,
	&target_fabric_port_attr_alua_tg_pt_offline,
	&target_fabric_port_attr_alua_tg_pt_status,
	&target_fabric_port_attr_alua_tg_pt_write_md,
	NULL,
};

static int target_fabric_port_link(
	struct config_item *lun_ci,
	struct config_item *se_dev_ci)
{
	struct config_item *tpg_ci;
	struct se_lun *lun = container_of(to_config_group(lun_ci),
				struct se_lun, lun_group);
	struct se_portal_group *se_tpg;
	struct se_device *dev;
	struct target_fabric_configfs *tf;
	int ret;

	if (!se_dev_ci->ci_type ||
	    se_dev_ci->ci_type->ct_item_ops != &target_core_dev_item_ops) {
		pr_err("Bad se_dev_ci, not a valid se_dev_ci pointer: %p\n", se_dev_ci);
		return -EFAULT;
	}
	dev = container_of(to_config_group(se_dev_ci), struct se_device, dev_group);

	if (!target_dev_configured(dev)) {
		pr_err("se_device not configured yet, cannot port link\n");
		return -ENODEV;
	}

	tpg_ci = &lun_ci->ci_parent->ci_group->cg_item;
	se_tpg = container_of(to_config_group(tpg_ci),
				struct se_portal_group, tpg_group);
	tf = se_tpg->se_tpg_wwn->wwn_tf;

	if (lun->lun_se_dev !=  NULL) {
		pr_err("Port Symlink already exists\n");
		return -EEXIST;
	}

	ret = core_dev_add_lun(se_tpg, dev, lun);
	if (ret) {
		pr_err("core_dev_add_lun() failed: %d\n", ret);
		goto out;
	}

	if (tf->tf_ops->fabric_post_link) {
		/*
		 * Call the optional fabric_post_link() to allow a
		 * fabric module to setup any additional state once
		 * core_dev_add_lun() has been called..
		 */
		tf->tf_ops->fabric_post_link(se_tpg, lun);
	}

	return 0;
out:
	return ret;
}

static void target_fabric_port_unlink(
	struct config_item *lun_ci,
	struct config_item *se_dev_ci)
{
	struct se_lun *lun = container_of(to_config_group(lun_ci),
				struct se_lun, lun_group);
	struct se_portal_group *se_tpg = lun->lun_tpg;
	struct target_fabric_configfs *tf = se_tpg->se_tpg_wwn->wwn_tf;

	if (tf->tf_ops->fabric_pre_unlink) {
		/*
		 * Call the optional fabric_pre_unlink() to allow a
		 * fabric module to release any additional stat before
		 * core_dev_del_lun() is called.
		*/
		tf->tf_ops->fabric_pre_unlink(se_tpg, lun);
	}

	core_dev_del_lun(se_tpg, lun);
}

static void target_fabric_port_release(struct config_item *item)
{
	struct se_lun *lun = container_of(to_config_group(item),
					  struct se_lun, lun_group);

	kfree_rcu(lun, rcu_head);
}

static struct configfs_item_operations target_fabric_port_item_ops = {
	.release		= target_fabric_port_release,
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
	unsigned long long unpacked_lun;
	int errno;

	if (strstr(name, "lun_") != name) {
		pr_err("Unable to locate \'_\" in"
				" \"lun_$LUN_NUMBER\"\n");
		return ERR_PTR(-EINVAL);
	}
	errno = kstrtoull(name + 4, 0, &unpacked_lun);
	if (errno)
		return ERR_PTR(errno);

	lun = core_tpg_alloc_lun(se_tpg, unpacked_lun);
	if (IS_ERR(lun))
		return ERR_CAST(lun);

	config_group_init_type_name(&lun->lun_group, name,
			&tf->tf_tpg_port_cit);

	config_group_init_type_name(&lun->port_stat_grps.stat_group,
			"statistics", &tf->tf_tpg_port_stat_cit);
	configfs_add_default_group(&lun->port_stat_grps.stat_group,
			&lun->lun_group);

	target_stat_setup_port_default_groups(lun);

	return &lun->lun_group;
}

static void target_fabric_drop_lun(
	struct config_group *group,
	struct config_item *item)
{
	struct se_lun *lun = container_of(to_config_group(item),
				struct se_lun, lun_group);

	configfs_remove_default_groups(&lun->port_stat_grps.stat_group);
	configfs_remove_default_groups(&lun->lun_group);

	config_item_put(item);
}

static struct configfs_group_operations target_fabric_lun_group_ops = {
	.make_group	= &target_fabric_make_lun,
	.drop_item	= &target_fabric_drop_lun,
};

TF_CIT_SETUP(tpg_lun, NULL, &target_fabric_lun_group_ops, NULL);

/* End of tfc_tpg_lun_cit */

TF_CIT_SETUP_DRV(tpg_attrib, NULL, NULL);
TF_CIT_SETUP_DRV(tpg_auth, NULL, NULL);
TF_CIT_SETUP_DRV(tpg_param, NULL, NULL);

/* Start of tfc_tpg_base_cit */

static void target_fabric_tpg_release(struct config_item *item)
{
	struct se_portal_group *se_tpg = container_of(to_config_group(item),
			struct se_portal_group, tpg_group);
	struct se_wwn *wwn = se_tpg->se_tpg_wwn;
	struct target_fabric_configfs *tf = wwn->wwn_tf;

	tf->tf_ops->fabric_drop_tpg(se_tpg);
}

static struct configfs_item_operations target_fabric_tpg_base_item_ops = {
	.release		= target_fabric_tpg_release,
};

TF_CIT_SETUP_DRV(tpg_base, &target_fabric_tpg_base_item_ops, NULL);

/* End of tfc_tpg_base_cit */

/* Start of tfc_tpg_cit */

static struct config_group *target_fabric_make_tpg(
	struct config_group *group,
	const char *name)
{
	struct se_wwn *wwn = container_of(group, struct se_wwn, wwn_group);
	struct target_fabric_configfs *tf = wwn->wwn_tf;
	struct se_portal_group *se_tpg;

	if (!tf->tf_ops->fabric_make_tpg) {
		pr_err("tf->tf_ops->fabric_make_tpg is NULL\n");
		return ERR_PTR(-ENOSYS);
	}

	se_tpg = tf->tf_ops->fabric_make_tpg(wwn, name);
	if (!se_tpg || IS_ERR(se_tpg))
		return ERR_PTR(-EINVAL);

	config_group_init_type_name(&se_tpg->tpg_group, name,
			&tf->tf_tpg_base_cit);

	config_group_init_type_name(&se_tpg->tpg_lun_group, "lun",
			&tf->tf_tpg_lun_cit);
	configfs_add_default_group(&se_tpg->tpg_lun_group,
			&se_tpg->tpg_group);

	config_group_init_type_name(&se_tpg->tpg_np_group, "np",
			&tf->tf_tpg_np_cit);
	configfs_add_default_group(&se_tpg->tpg_np_group,
			&se_tpg->tpg_group);

	config_group_init_type_name(&se_tpg->tpg_acl_group, "acls",
			&tf->tf_tpg_nacl_cit);
	configfs_add_default_group(&se_tpg->tpg_acl_group,
			&se_tpg->tpg_group);

	config_group_init_type_name(&se_tpg->tpg_attrib_group, "attrib",
			&tf->tf_tpg_attrib_cit);
	configfs_add_default_group(&se_tpg->tpg_attrib_group,
			&se_tpg->tpg_group);

	config_group_init_type_name(&se_tpg->tpg_auth_group, "auth",
			&tf->tf_tpg_auth_cit);
	configfs_add_default_group(&se_tpg->tpg_auth_group,
			&se_tpg->tpg_group);

	config_group_init_type_name(&se_tpg->tpg_param_group, "param",
			&tf->tf_tpg_param_cit);
	configfs_add_default_group(&se_tpg->tpg_param_group,
			&se_tpg->tpg_group);

	return &se_tpg->tpg_group;
}

static void target_fabric_drop_tpg(
	struct config_group *group,
	struct config_item *item)
{
	struct se_portal_group *se_tpg = container_of(to_config_group(item),
				struct se_portal_group, tpg_group);

	configfs_remove_default_groups(&se_tpg->tpg_group);
	config_item_put(item);
}

static void target_fabric_release_wwn(struct config_item *item)
{
	struct se_wwn *wwn = container_of(to_config_group(item),
				struct se_wwn, wwn_group);
	struct target_fabric_configfs *tf = wwn->wwn_tf;

	configfs_remove_default_groups(&wwn->fabric_stat_group);
	configfs_remove_default_groups(&wwn->param_group);
	tf->tf_ops->fabric_drop_wwn(wwn);
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

static ssize_t
target_fabric_wwn_cmd_completion_affinity_show(struct config_item *item,
					       char *page)
{
	struct se_wwn *wwn = container_of(to_config_group(item), struct se_wwn,
					  param_group);
	return sprintf(page, "%d\n",
		       wwn->cmd_compl_affinity == WORK_CPU_UNBOUND ?
		       SE_COMPL_AFFINITY_CURR_CPU : wwn->cmd_compl_affinity);
}

static ssize_t
target_fabric_wwn_cmd_completion_affinity_store(struct config_item *item,
						const char *page, size_t count)
{
	struct se_wwn *wwn = container_of(to_config_group(item), struct se_wwn,
					  param_group);
	int compl_val;

	if (kstrtoint(page, 0, &compl_val))
		return -EINVAL;

	switch (compl_val) {
	case SE_COMPL_AFFINITY_CPUID:
		wwn->cmd_compl_affinity = compl_val;
		break;
	case SE_COMPL_AFFINITY_CURR_CPU:
		wwn->cmd_compl_affinity = WORK_CPU_UNBOUND;
		break;
	default:
		if (compl_val < 0 || compl_val >= nr_cpu_ids ||
		    !cpu_online(compl_val)) {
			pr_err("Command completion value must be between %d and %d or an online CPU.\n",
			       SE_COMPL_AFFINITY_CPUID,
			       SE_COMPL_AFFINITY_CURR_CPU);
			return -EINVAL;
		}
		wwn->cmd_compl_affinity = compl_val;
	}

	return count;
}
CONFIGFS_ATTR(target_fabric_wwn_, cmd_completion_affinity);

static struct configfs_attribute *target_fabric_wwn_param_attrs[] = {
	&target_fabric_wwn_attr_cmd_completion_affinity,
	NULL,
};

TF_CIT_SETUP(wwn_param, NULL, NULL, target_fabric_wwn_param_attrs);

/* Start of tfc_wwn_cit */

static struct config_group *target_fabric_make_wwn(
	struct config_group *group,
	const char *name)
{
	struct target_fabric_configfs *tf = container_of(group,
				struct target_fabric_configfs, tf_group);
	struct se_wwn *wwn;

	if (!tf->tf_ops->fabric_make_wwn) {
		pr_err("tf->tf_ops.fabric_make_wwn is NULL\n");
		return ERR_PTR(-ENOSYS);
	}

	wwn = tf->tf_ops->fabric_make_wwn(tf, group, name);
	if (!wwn || IS_ERR(wwn))
		return ERR_PTR(-EINVAL);

	wwn->cmd_compl_affinity = SE_COMPL_AFFINITY_CPUID;
	wwn->wwn_tf = tf;

	config_group_init_type_name(&wwn->wwn_group, name, &tf->tf_tpg_cit);

	config_group_init_type_name(&wwn->fabric_stat_group, "fabric_statistics",
			&tf->tf_wwn_fabric_stats_cit);
	configfs_add_default_group(&wwn->fabric_stat_group, &wwn->wwn_group);

	config_group_init_type_name(&wwn->param_group, "param",
			&tf->tf_wwn_param_cit);
	configfs_add_default_group(&wwn->param_group, &wwn->wwn_group);

	if (tf->tf_ops->add_wwn_groups)
		tf->tf_ops->add_wwn_groups(wwn);
	return &wwn->wwn_group;
}

static void target_fabric_drop_wwn(
	struct config_group *group,
	struct config_item *item)
{
	struct se_wwn *wwn = container_of(to_config_group(item),
				struct se_wwn, wwn_group);

	configfs_remove_default_groups(&wwn->wwn_group);
	config_item_put(item);
}

static struct configfs_group_operations target_fabric_wwn_group_ops = {
	.make_group	= target_fabric_make_wwn,
	.drop_item	= target_fabric_drop_wwn,
};

TF_CIT_SETUP_DRV(wwn, NULL, &target_fabric_wwn_group_ops);
TF_CIT_SETUP_DRV(discovery, NULL, NULL);

int target_fabric_setup_cits(struct target_fabric_configfs *tf)
{
	target_fabric_setup_discovery_cit(tf);
	target_fabric_setup_wwn_cit(tf);
	target_fabric_setup_wwn_fabric_stats_cit(tf);
	target_fabric_setup_wwn_param_cit(tf);
	target_fabric_setup_tpg_cit(tf);
	target_fabric_setup_tpg_base_cit(tf);
	target_fabric_setup_tpg_port_cit(tf);
	target_fabric_setup_tpg_port_stat_cit(tf);
	target_fabric_setup_tpg_lun_cit(tf);
	target_fabric_setup_tpg_np_cit(tf);
	target_fabric_setup_tpg_np_base_cit(tf);
	target_fabric_setup_tpg_attrib_cit(tf);
	target_fabric_setup_tpg_auth_cit(tf);
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

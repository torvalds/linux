/*******************************************************************************
 * Filename:  target_core_configfs.c
 *
 * This file contains ConfigFS logic for the Generic Target Engine project.
 *
 * (c) Copyright 2008-2013 Datera, Inc.
 *
 * Nicholas A. Bellinger <nab@kernel.org>
 *
 * based on configfs Copyright (C) 2005 Oracle.  All rights reserved.
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
#include <linux/parser.h>
#include <linux/syscalls.h>
#include <linux/configfs.h>
#include <linux/spinlock.h>

#include <target/target_core_base.h>
#include <target/target_core_backend.h>
#include <target/target_core_fabric.h>

#include "target_core_internal.h"
#include "target_core_alua.h"
#include "target_core_pr.h"
#include "target_core_rd.h"
#include "target_core_xcopy.h"

#define TB_CIT_SETUP(_name, _item_ops, _group_ops, _attrs)		\
static void target_core_setup_##_name##_cit(struct target_backend *tb)	\
{									\
	struct config_item_type *cit = &tb->tb_##_name##_cit;		\
									\
	cit->ct_item_ops = _item_ops;					\
	cit->ct_group_ops = _group_ops;					\
	cit->ct_attrs = _attrs;						\
	cit->ct_owner = tb->ops->owner;					\
	pr_debug("Setup generic %s\n", __stringify(_name));		\
}

#define TB_CIT_SETUP_DRV(_name, _item_ops, _group_ops)			\
static void target_core_setup_##_name##_cit(struct target_backend *tb)	\
{									\
	struct config_item_type *cit = &tb->tb_##_name##_cit;		\
									\
	cit->ct_item_ops = _item_ops;					\
	cit->ct_group_ops = _group_ops;					\
	cit->ct_attrs = tb->ops->tb_##_name##_attrs;			\
	cit->ct_owner = tb->ops->owner;					\
	pr_debug("Setup generic %s\n", __stringify(_name));		\
}

extern struct t10_alua_lu_gp *default_lu_gp;

static LIST_HEAD(g_tf_list);
static DEFINE_MUTEX(g_tf_lock);

static struct config_group target_core_hbagroup;
static struct config_group alua_group;
static struct config_group alua_lu_gps_group;

static inline struct se_hba *
item_to_hba(struct config_item *item)
{
	return container_of(to_config_group(item), struct se_hba, hba_group);
}

/*
 * Attributes for /sys/kernel/config/target/
 */
static ssize_t target_core_item_version_show(struct config_item *item,
		char *page)
{
	return sprintf(page, "Target Engine Core ConfigFS Infrastructure %s"
		" on %s/%s on "UTS_RELEASE"\n", TARGET_CORE_VERSION,
		utsname()->sysname, utsname()->machine);
}

CONFIGFS_ATTR_RO(target_core_item_, version);

char db_root[DB_ROOT_LEN] = DB_ROOT_DEFAULT;
static char db_root_stage[DB_ROOT_LEN];

static ssize_t target_core_item_dbroot_show(struct config_item *item,
					    char *page)
{
	return sprintf(page, "%s\n", db_root);
}

static ssize_t target_core_item_dbroot_store(struct config_item *item,
					const char *page, size_t count)
{
	ssize_t read_bytes;
	struct file *fp;

	mutex_lock(&g_tf_lock);
	if (!list_empty(&g_tf_list)) {
		mutex_unlock(&g_tf_lock);
		pr_err("db_root: cannot be changed: target drivers registered");
		return -EINVAL;
	}

	if (count > (DB_ROOT_LEN - 1)) {
		mutex_unlock(&g_tf_lock);
		pr_err("db_root: count %d exceeds DB_ROOT_LEN-1: %u\n",
		       (int)count, DB_ROOT_LEN - 1);
		return -EINVAL;
	}

	read_bytes = snprintf(db_root_stage, DB_ROOT_LEN, "%s", page);
	if (!read_bytes) {
		mutex_unlock(&g_tf_lock);
		return -EINVAL;
	}
	if (db_root_stage[read_bytes - 1] == '\n')
		db_root_stage[read_bytes - 1] = '\0';

	/* validate new db root before accepting it */
	fp = filp_open(db_root_stage, O_RDONLY, 0);
	if (IS_ERR(fp)) {
		mutex_unlock(&g_tf_lock);
		pr_err("db_root: cannot open: %s\n", db_root_stage);
		return -EINVAL;
	}
	if (!S_ISDIR(file_inode(fp)->i_mode)) {
		filp_close(fp, NULL);
		mutex_unlock(&g_tf_lock);
		pr_err("db_root: not a directory: %s\n", db_root_stage);
		return -EINVAL;
	}
	filp_close(fp, NULL);

	strncpy(db_root, db_root_stage, read_bytes);

	mutex_unlock(&g_tf_lock);

	pr_debug("Target_Core_ConfigFS: db_root set to %s\n", db_root);

	return read_bytes;
}

CONFIGFS_ATTR(target_core_item_, dbroot);

static struct target_fabric_configfs *target_core_get_fabric(
	const char *name)
{
	struct target_fabric_configfs *tf;

	if (!name)
		return NULL;

	mutex_lock(&g_tf_lock);
	list_for_each_entry(tf, &g_tf_list, tf_list) {
		if (!strcmp(tf->tf_ops->name, name)) {
			atomic_inc(&tf->tf_access_cnt);
			mutex_unlock(&g_tf_lock);
			return tf;
		}
	}
	mutex_unlock(&g_tf_lock);

	return NULL;
}

/*
 * Called from struct target_core_group_ops->make_group()
 */
static struct config_group *target_core_register_fabric(
	struct config_group *group,
	const char *name)
{
	struct target_fabric_configfs *tf;
	int ret;

	pr_debug("Target_Core_ConfigFS: REGISTER -> group: %p name:"
			" %s\n", group, name);

	tf = target_core_get_fabric(name);
	if (!tf) {
		pr_debug("target_core_register_fabric() trying autoload for %s\n",
			 name);

		/*
		 * Below are some hardcoded request_module() calls to automatically
		 * local fabric modules when the following is called:
		 *
		 * mkdir -p /sys/kernel/config/target/$MODULE_NAME
		 *
		 * Note that this does not limit which TCM fabric module can be
		 * registered, but simply provids auto loading logic for modules with
		 * mkdir(2) system calls with known TCM fabric modules.
		 */

		if (!strncmp(name, "iscsi", 5)) {
			/*
			 * Automatically load the LIO Target fabric module when the
			 * following is called:
			 *
			 * mkdir -p $CONFIGFS/target/iscsi
			 */
			ret = request_module("iscsi_target_mod");
			if (ret < 0) {
				pr_debug("request_module() failed for"
				         " iscsi_target_mod.ko: %d\n", ret);
				return ERR_PTR(-EINVAL);
			}
		} else if (!strncmp(name, "loopback", 8)) {
			/*
			 * Automatically load the tcm_loop fabric module when the
			 * following is called:
			 *
			 * mkdir -p $CONFIGFS/target/loopback
			 */
			ret = request_module("tcm_loop");
			if (ret < 0) {
				pr_debug("request_module() failed for"
				         " tcm_loop.ko: %d\n", ret);
				return ERR_PTR(-EINVAL);
			}
		}

		tf = target_core_get_fabric(name);
	}

	if (!tf) {
		pr_debug("target_core_get_fabric() failed for %s\n",
		         name);
		return ERR_PTR(-EINVAL);
	}
	pr_debug("Target_Core_ConfigFS: REGISTER -> Located fabric:"
			" %s\n", tf->tf_ops->name);
	/*
	 * On a successful target_core_get_fabric() look, the returned
	 * struct target_fabric_configfs *tf will contain a usage reference.
	 */
	pr_debug("Target_Core_ConfigFS: REGISTER tfc_wwn_cit -> %p\n",
			&tf->tf_wwn_cit);

	config_group_init_type_name(&tf->tf_group, name, &tf->tf_wwn_cit);

	config_group_init_type_name(&tf->tf_disc_group, "discovery_auth",
			&tf->tf_discovery_cit);
	configfs_add_default_group(&tf->tf_disc_group, &tf->tf_group);

	pr_debug("Target_Core_ConfigFS: REGISTER -> Allocated Fabric:"
			" %s\n", tf->tf_group.cg_item.ci_name);
	return &tf->tf_group;
}

/*
 * Called from struct target_core_group_ops->drop_item()
 */
static void target_core_deregister_fabric(
	struct config_group *group,
	struct config_item *item)
{
	struct target_fabric_configfs *tf = container_of(
		to_config_group(item), struct target_fabric_configfs, tf_group);

	pr_debug("Target_Core_ConfigFS: DEREGISTER -> Looking up %s in"
		" tf list\n", config_item_name(item));

	pr_debug("Target_Core_ConfigFS: DEREGISTER -> located fabric:"
			" %s\n", tf->tf_ops->name);
	atomic_dec(&tf->tf_access_cnt);

	pr_debug("Target_Core_ConfigFS: DEREGISTER -> Releasing ci"
			" %s\n", config_item_name(item));

	configfs_remove_default_groups(&tf->tf_group);
	config_item_put(item);
}

static struct configfs_group_operations target_core_fabric_group_ops = {
	.make_group	= &target_core_register_fabric,
	.drop_item	= &target_core_deregister_fabric,
};

/*
 * All item attributes appearing in /sys/kernel/target/ appear here.
 */
static struct configfs_attribute *target_core_fabric_item_attrs[] = {
	&target_core_item_attr_version,
	&target_core_item_attr_dbroot,
	NULL,
};

/*
 * Provides Fabrics Groups and Item Attributes for /sys/kernel/config/target/
 */
static const struct config_item_type target_core_fabrics_item = {
	.ct_group_ops	= &target_core_fabric_group_ops,
	.ct_attrs	= target_core_fabric_item_attrs,
	.ct_owner	= THIS_MODULE,
};

static struct configfs_subsystem target_core_fabrics = {
	.su_group = {
		.cg_item = {
			.ci_namebuf = "target",
			.ci_type = &target_core_fabrics_item,
		},
	},
};

int target_depend_item(struct config_item *item)
{
	return configfs_depend_item(&target_core_fabrics, item);
}
EXPORT_SYMBOL(target_depend_item);

void target_undepend_item(struct config_item *item)
{
	return configfs_undepend_item(item);
}
EXPORT_SYMBOL(target_undepend_item);

/*##############################################################################
// Start functions called by external Target Fabrics Modules
//############################################################################*/

static int target_fabric_tf_ops_check(const struct target_core_fabric_ops *tfo)
{
	if (!tfo->name) {
		pr_err("Missing tfo->name\n");
		return -EINVAL;
	}
	if (strlen(tfo->name) >= TARGET_FABRIC_NAME_SIZE) {
		pr_err("Passed name: %s exceeds TARGET_FABRIC"
			"_NAME_SIZE\n", tfo->name);
		return -EINVAL;
	}
	if (!tfo->get_fabric_name) {
		pr_err("Missing tfo->get_fabric_name()\n");
		return -EINVAL;
	}
	if (!tfo->tpg_get_wwn) {
		pr_err("Missing tfo->tpg_get_wwn()\n");
		return -EINVAL;
	}
	if (!tfo->tpg_get_tag) {
		pr_err("Missing tfo->tpg_get_tag()\n");
		return -EINVAL;
	}
	if (!tfo->tpg_check_demo_mode) {
		pr_err("Missing tfo->tpg_check_demo_mode()\n");
		return -EINVAL;
	}
	if (!tfo->tpg_check_demo_mode_cache) {
		pr_err("Missing tfo->tpg_check_demo_mode_cache()\n");
		return -EINVAL;
	}
	if (!tfo->tpg_check_demo_mode_write_protect) {
		pr_err("Missing tfo->tpg_check_demo_mode_write_protect()\n");
		return -EINVAL;
	}
	if (!tfo->tpg_check_prod_mode_write_protect) {
		pr_err("Missing tfo->tpg_check_prod_mode_write_protect()\n");
		return -EINVAL;
	}
	if (!tfo->tpg_get_inst_index) {
		pr_err("Missing tfo->tpg_get_inst_index()\n");
		return -EINVAL;
	}
	if (!tfo->release_cmd) {
		pr_err("Missing tfo->release_cmd()\n");
		return -EINVAL;
	}
	if (!tfo->sess_get_index) {
		pr_err("Missing tfo->sess_get_index()\n");
		return -EINVAL;
	}
	if (!tfo->write_pending) {
		pr_err("Missing tfo->write_pending()\n");
		return -EINVAL;
	}
	if (!tfo->write_pending_status) {
		pr_err("Missing tfo->write_pending_status()\n");
		return -EINVAL;
	}
	if (!tfo->set_default_node_attributes) {
		pr_err("Missing tfo->set_default_node_attributes()\n");
		return -EINVAL;
	}
	if (!tfo->get_cmd_state) {
		pr_err("Missing tfo->get_cmd_state()\n");
		return -EINVAL;
	}
	if (!tfo->queue_data_in) {
		pr_err("Missing tfo->queue_data_in()\n");
		return -EINVAL;
	}
	if (!tfo->queue_status) {
		pr_err("Missing tfo->queue_status()\n");
		return -EINVAL;
	}
	if (!tfo->queue_tm_rsp) {
		pr_err("Missing tfo->queue_tm_rsp()\n");
		return -EINVAL;
	}
	if (!tfo->aborted_task) {
		pr_err("Missing tfo->aborted_task()\n");
		return -EINVAL;
	}
	if (!tfo->check_stop_free) {
		pr_err("Missing tfo->check_stop_free()\n");
		return -EINVAL;
	}
	/*
	 * We at least require tfo->fabric_make_wwn(), tfo->fabric_drop_wwn()
	 * tfo->fabric_make_tpg() and tfo->fabric_drop_tpg() in
	 * target_core_fabric_configfs.c WWN+TPG group context code.
	 */
	if (!tfo->fabric_make_wwn) {
		pr_err("Missing tfo->fabric_make_wwn()\n");
		return -EINVAL;
	}
	if (!tfo->fabric_drop_wwn) {
		pr_err("Missing tfo->fabric_drop_wwn()\n");
		return -EINVAL;
	}
	if (!tfo->fabric_make_tpg) {
		pr_err("Missing tfo->fabric_make_tpg()\n");
		return -EINVAL;
	}
	if (!tfo->fabric_drop_tpg) {
		pr_err("Missing tfo->fabric_drop_tpg()\n");
		return -EINVAL;
	}

	return 0;
}

int target_register_template(const struct target_core_fabric_ops *fo)
{
	struct target_fabric_configfs *tf;
	int ret;

	ret = target_fabric_tf_ops_check(fo);
	if (ret)
		return ret;

	tf = kzalloc(sizeof(struct target_fabric_configfs), GFP_KERNEL);
	if (!tf) {
		pr_err("%s: could not allocate memory!\n", __func__);
		return -ENOMEM;
	}

	INIT_LIST_HEAD(&tf->tf_list);
	atomic_set(&tf->tf_access_cnt, 0);
	tf->tf_ops = fo;
	target_fabric_setup_cits(tf);

	mutex_lock(&g_tf_lock);
	list_add_tail(&tf->tf_list, &g_tf_list);
	mutex_unlock(&g_tf_lock);

	return 0;
}
EXPORT_SYMBOL(target_register_template);

void target_unregister_template(const struct target_core_fabric_ops *fo)
{
	struct target_fabric_configfs *t;

	mutex_lock(&g_tf_lock);
	list_for_each_entry(t, &g_tf_list, tf_list) {
		if (!strcmp(t->tf_ops->name, fo->name)) {
			BUG_ON(atomic_read(&t->tf_access_cnt));
			list_del(&t->tf_list);
			mutex_unlock(&g_tf_lock);
			/*
			 * Wait for any outstanding fabric se_deve_entry->rcu_head
			 * callbacks to complete post kfree_rcu(), before allowing
			 * fabric driver unload of TFO->module to proceed.
			 */
			rcu_barrier();
			kfree(t);
			return;
		}
	}
	mutex_unlock(&g_tf_lock);
}
EXPORT_SYMBOL(target_unregister_template);

/*##############################################################################
// Stop functions called by external Target Fabrics Modules
//############################################################################*/

static inline struct se_dev_attrib *to_attrib(struct config_item *item)
{
	return container_of(to_config_group(item), struct se_dev_attrib,
			da_group);
}

/* Start functions for struct config_item_type tb_dev_attrib_cit */
#define DEF_CONFIGFS_ATTRIB_SHOW(_name)					\
static ssize_t _name##_show(struct config_item *item, char *page)	\
{									\
	return snprintf(page, PAGE_SIZE, "%u\n", to_attrib(item)->_name); \
}

DEF_CONFIGFS_ATTRIB_SHOW(emulate_model_alias);
DEF_CONFIGFS_ATTRIB_SHOW(emulate_dpo);
DEF_CONFIGFS_ATTRIB_SHOW(emulate_fua_write);
DEF_CONFIGFS_ATTRIB_SHOW(emulate_fua_read);
DEF_CONFIGFS_ATTRIB_SHOW(emulate_write_cache);
DEF_CONFIGFS_ATTRIB_SHOW(emulate_ua_intlck_ctrl);
DEF_CONFIGFS_ATTRIB_SHOW(emulate_tas);
DEF_CONFIGFS_ATTRIB_SHOW(emulate_tpu);
DEF_CONFIGFS_ATTRIB_SHOW(emulate_tpws);
DEF_CONFIGFS_ATTRIB_SHOW(emulate_caw);
DEF_CONFIGFS_ATTRIB_SHOW(emulate_3pc);
DEF_CONFIGFS_ATTRIB_SHOW(pi_prot_type);
DEF_CONFIGFS_ATTRIB_SHOW(hw_pi_prot_type);
DEF_CONFIGFS_ATTRIB_SHOW(pi_prot_format);
DEF_CONFIGFS_ATTRIB_SHOW(pi_prot_verify);
DEF_CONFIGFS_ATTRIB_SHOW(enforce_pr_isids);
DEF_CONFIGFS_ATTRIB_SHOW(is_nonrot);
DEF_CONFIGFS_ATTRIB_SHOW(emulate_rest_reord);
DEF_CONFIGFS_ATTRIB_SHOW(force_pr_aptpl);
DEF_CONFIGFS_ATTRIB_SHOW(hw_block_size);
DEF_CONFIGFS_ATTRIB_SHOW(block_size);
DEF_CONFIGFS_ATTRIB_SHOW(hw_max_sectors);
DEF_CONFIGFS_ATTRIB_SHOW(optimal_sectors);
DEF_CONFIGFS_ATTRIB_SHOW(hw_queue_depth);
DEF_CONFIGFS_ATTRIB_SHOW(queue_depth);
DEF_CONFIGFS_ATTRIB_SHOW(max_unmap_lba_count);
DEF_CONFIGFS_ATTRIB_SHOW(max_unmap_block_desc_count);
DEF_CONFIGFS_ATTRIB_SHOW(unmap_granularity);
DEF_CONFIGFS_ATTRIB_SHOW(unmap_granularity_alignment);
DEF_CONFIGFS_ATTRIB_SHOW(unmap_zeroes_data);
DEF_CONFIGFS_ATTRIB_SHOW(max_write_same_len);

#define DEF_CONFIGFS_ATTRIB_STORE_U32(_name)				\
static ssize_t _name##_store(struct config_item *item, const char *page,\
		size_t count)						\
{									\
	struct se_dev_attrib *da = to_attrib(item);			\
	u32 val;							\
	int ret;							\
									\
	ret = kstrtou32(page, 0, &val);					\
	if (ret < 0)							\
		return ret;						\
	da->_name = val;						\
	return count;							\
}

DEF_CONFIGFS_ATTRIB_STORE_U32(max_unmap_lba_count);
DEF_CONFIGFS_ATTRIB_STORE_U32(max_unmap_block_desc_count);
DEF_CONFIGFS_ATTRIB_STORE_U32(unmap_granularity);
DEF_CONFIGFS_ATTRIB_STORE_U32(unmap_granularity_alignment);
DEF_CONFIGFS_ATTRIB_STORE_U32(max_write_same_len);

#define DEF_CONFIGFS_ATTRIB_STORE_BOOL(_name)				\
static ssize_t _name##_store(struct config_item *item, const char *page,	\
		size_t count)						\
{									\
	struct se_dev_attrib *da = to_attrib(item);			\
	bool flag;							\
	int ret;							\
									\
	ret = strtobool(page, &flag);					\
	if (ret < 0)							\
		return ret;						\
	da->_name = flag;						\
	return count;							\
}

DEF_CONFIGFS_ATTRIB_STORE_BOOL(emulate_fua_write);
DEF_CONFIGFS_ATTRIB_STORE_BOOL(emulate_caw);
DEF_CONFIGFS_ATTRIB_STORE_BOOL(emulate_3pc);
DEF_CONFIGFS_ATTRIB_STORE_BOOL(enforce_pr_isids);
DEF_CONFIGFS_ATTRIB_STORE_BOOL(is_nonrot);

#define DEF_CONFIGFS_ATTRIB_STORE_STUB(_name)				\
static ssize_t _name##_store(struct config_item *item, const char *page,\
		size_t count)						\
{									\
	printk_once(KERN_WARNING					\
		"ignoring deprecated %s attribute\n",			\
		__stringify(_name));					\
	return count;							\
}

DEF_CONFIGFS_ATTRIB_STORE_STUB(emulate_dpo);
DEF_CONFIGFS_ATTRIB_STORE_STUB(emulate_fua_read);

static void dev_set_t10_wwn_model_alias(struct se_device *dev)
{
	const char *configname;

	configname = config_item_name(&dev->dev_group.cg_item);
	if (strlen(configname) >= 16) {
		pr_warn("dev[%p]: Backstore name '%s' is too long for "
			"INQUIRY_MODEL, truncating to 16 bytes\n", dev,
			configname);
	}
	snprintf(&dev->t10_wwn.model[0], 16, "%s", configname);
}

static ssize_t emulate_model_alias_store(struct config_item *item,
		const char *page, size_t count)
{
	struct se_dev_attrib *da = to_attrib(item);
	struct se_device *dev = da->da_dev;
	bool flag;
	int ret;

	if (dev->export_count) {
		pr_err("dev[%p]: Unable to change model alias"
			" while export_count is %d\n",
			dev, dev->export_count);
		return -EINVAL;
	}

	ret = strtobool(page, &flag);
	if (ret < 0)
		return ret;

	if (flag) {
		dev_set_t10_wwn_model_alias(dev);
	} else {
		strncpy(&dev->t10_wwn.model[0],
			dev->transport->inquiry_prod, 16);
	}
	da->emulate_model_alias = flag;
	return count;
}

static ssize_t emulate_write_cache_store(struct config_item *item,
		const char *page, size_t count)
{
	struct se_dev_attrib *da = to_attrib(item);
	bool flag;
	int ret;

	ret = strtobool(page, &flag);
	if (ret < 0)
		return ret;

	if (flag && da->da_dev->transport->get_write_cache) {
		pr_err("emulate_write_cache not supported for this device\n");
		return -EINVAL;
	}

	da->emulate_write_cache = flag;
	pr_debug("dev[%p]: SE Device WRITE_CACHE_EMULATION flag: %d\n",
			da->da_dev, flag);
	return count;
}

static ssize_t emulate_ua_intlck_ctrl_store(struct config_item *item,
		const char *page, size_t count)
{
	struct se_dev_attrib *da = to_attrib(item);
	u32 val;
	int ret;

	ret = kstrtou32(page, 0, &val);
	if (ret < 0)
		return ret;

	if (val != 0 && val != 1 && val != 2) {
		pr_err("Illegal value %d\n", val);
		return -EINVAL;
	}

	if (da->da_dev->export_count) {
		pr_err("dev[%p]: Unable to change SE Device"
			" UA_INTRLCK_CTRL while export_count is %d\n",
			da->da_dev, da->da_dev->export_count);
		return -EINVAL;
	}
	da->emulate_ua_intlck_ctrl = val;
	pr_debug("dev[%p]: SE Device UA_INTRLCK_CTRL flag: %d\n",
		da->da_dev, val);
	return count;
}

static ssize_t emulate_tas_store(struct config_item *item,
		const char *page, size_t count)
{
	struct se_dev_attrib *da = to_attrib(item);
	bool flag;
	int ret;

	ret = strtobool(page, &flag);
	if (ret < 0)
		return ret;

	if (da->da_dev->export_count) {
		pr_err("dev[%p]: Unable to change SE Device TAS while"
			" export_count is %d\n",
			da->da_dev, da->da_dev->export_count);
		return -EINVAL;
	}
	da->emulate_tas = flag;
	pr_debug("dev[%p]: SE Device TASK_ABORTED status bit: %s\n",
		da->da_dev, flag ? "Enabled" : "Disabled");

	return count;
}

static ssize_t emulate_tpu_store(struct config_item *item,
		const char *page, size_t count)
{
	struct se_dev_attrib *da = to_attrib(item);
	bool flag;
	int ret;

	ret = strtobool(page, &flag);
	if (ret < 0)
		return ret;

	/*
	 * We expect this value to be non-zero when generic Block Layer
	 * Discard supported is detected iblock_create_virtdevice().
	 */
	if (flag && !da->max_unmap_block_desc_count) {
		pr_err("Generic Block Discard not supported\n");
		return -ENOSYS;
	}

	da->emulate_tpu = flag;
	pr_debug("dev[%p]: SE Device Thin Provisioning UNMAP bit: %d\n",
		da->da_dev, flag);
	return count;
}

static ssize_t emulate_tpws_store(struct config_item *item,
		const char *page, size_t count)
{
	struct se_dev_attrib *da = to_attrib(item);
	bool flag;
	int ret;

	ret = strtobool(page, &flag);
	if (ret < 0)
		return ret;

	/*
	 * We expect this value to be non-zero when generic Block Layer
	 * Discard supported is detected iblock_create_virtdevice().
	 */
	if (flag && !da->max_unmap_block_desc_count) {
		pr_err("Generic Block Discard not supported\n");
		return -ENOSYS;
	}

	da->emulate_tpws = flag;
	pr_debug("dev[%p]: SE Device Thin Provisioning WRITE_SAME: %d\n",
				da->da_dev, flag);
	return count;
}

static ssize_t pi_prot_type_store(struct config_item *item,
		const char *page, size_t count)
{
	struct se_dev_attrib *da = to_attrib(item);
	int old_prot = da->pi_prot_type, ret;
	struct se_device *dev = da->da_dev;
	u32 flag;

	ret = kstrtou32(page, 0, &flag);
	if (ret < 0)
		return ret;

	if (flag != 0 && flag != 1 && flag != 2 && flag != 3) {
		pr_err("Illegal value %d for pi_prot_type\n", flag);
		return -EINVAL;
	}
	if (flag == 2) {
		pr_err("DIF TYPE2 protection currently not supported\n");
		return -ENOSYS;
	}
	if (da->hw_pi_prot_type) {
		pr_warn("DIF protection enabled on underlying hardware,"
			" ignoring\n");
		return count;
	}
	if (!dev->transport->init_prot || !dev->transport->free_prot) {
		/* 0 is only allowed value for non-supporting backends */
		if (flag == 0)
			return count;

		pr_err("DIF protection not supported by backend: %s\n",
		       dev->transport->name);
		return -ENOSYS;
	}
	if (!(dev->dev_flags & DF_CONFIGURED)) {
		pr_err("DIF protection requires device to be configured\n");
		return -ENODEV;
	}
	if (dev->export_count) {
		pr_err("dev[%p]: Unable to change SE Device PROT type while"
		       " export_count is %d\n", dev, dev->export_count);
		return -EINVAL;
	}

	da->pi_prot_type = flag;

	if (flag && !old_prot) {
		ret = dev->transport->init_prot(dev);
		if (ret) {
			da->pi_prot_type = old_prot;
			da->pi_prot_verify = (bool) da->pi_prot_type;
			return ret;
		}

	} else if (!flag && old_prot) {
		dev->transport->free_prot(dev);
	}

	da->pi_prot_verify = (bool) da->pi_prot_type;
	pr_debug("dev[%p]: SE Device Protection Type: %d\n", dev, flag);
	return count;
}

static ssize_t pi_prot_format_store(struct config_item *item,
		const char *page, size_t count)
{
	struct se_dev_attrib *da = to_attrib(item);
	struct se_device *dev = da->da_dev;
	bool flag;
	int ret;

	ret = strtobool(page, &flag);
	if (ret < 0)
		return ret;

	if (!flag)
		return count;

	if (!dev->transport->format_prot) {
		pr_err("DIF protection format not supported by backend %s\n",
		       dev->transport->name);
		return -ENOSYS;
	}
	if (!(dev->dev_flags & DF_CONFIGURED)) {
		pr_err("DIF protection format requires device to be configured\n");
		return -ENODEV;
	}
	if (dev->export_count) {
		pr_err("dev[%p]: Unable to format SE Device PROT type while"
		       " export_count is %d\n", dev, dev->export_count);
		return -EINVAL;
	}

	ret = dev->transport->format_prot(dev);
	if (ret)
		return ret;

	pr_debug("dev[%p]: SE Device Protection Format complete\n", dev);
	return count;
}

static ssize_t pi_prot_verify_store(struct config_item *item,
		const char *page, size_t count)
{
	struct se_dev_attrib *da = to_attrib(item);
	bool flag;
	int ret;

	ret = strtobool(page, &flag);
	if (ret < 0)
		return ret;

	if (!flag) {
		da->pi_prot_verify = flag;
		return count;
	}
	if (da->hw_pi_prot_type) {
		pr_warn("DIF protection enabled on underlying hardware,"
			" ignoring\n");
		return count;
	}
	if (!da->pi_prot_type) {
		pr_warn("DIF protection not supported by backend, ignoring\n");
		return count;
	}
	da->pi_prot_verify = flag;

	return count;
}

static ssize_t force_pr_aptpl_store(struct config_item *item,
		const char *page, size_t count)
{
	struct se_dev_attrib *da = to_attrib(item);
	bool flag;
	int ret;

	ret = strtobool(page, &flag);
	if (ret < 0)
		return ret;
	if (da->da_dev->export_count) {
		pr_err("dev[%p]: Unable to set force_pr_aptpl while"
		       " export_count is %d\n",
		       da->da_dev, da->da_dev->export_count);
		return -EINVAL;
	}

	da->force_pr_aptpl = flag;
	pr_debug("dev[%p]: SE Device force_pr_aptpl: %d\n", da->da_dev, flag);
	return count;
}

static ssize_t emulate_rest_reord_store(struct config_item *item,
		const char *page, size_t count)
{
	struct se_dev_attrib *da = to_attrib(item);
	bool flag;
	int ret;

	ret = strtobool(page, &flag);
	if (ret < 0)
		return ret;

	if (flag != 0) {
		printk(KERN_ERR "dev[%p]: SE Device emulation of restricted"
			" reordering not implemented\n", da->da_dev);
		return -ENOSYS;
	}
	da->emulate_rest_reord = flag;
	pr_debug("dev[%p]: SE Device emulate_rest_reord: %d\n",
		da->da_dev, flag);
	return count;
}

static ssize_t unmap_zeroes_data_store(struct config_item *item,
		const char *page, size_t count)
{
	struct se_dev_attrib *da = to_attrib(item);
	bool flag;
	int ret;

	ret = strtobool(page, &flag);
	if (ret < 0)
		return ret;

	if (da->da_dev->export_count) {
		pr_err("dev[%p]: Unable to change SE Device"
		       " unmap_zeroes_data while export_count is %d\n",
		       da->da_dev, da->da_dev->export_count);
		return -EINVAL;
	}
	/*
	 * We expect this value to be non-zero when generic Block Layer
	 * Discard supported is detected iblock_configure_device().
	 */
	if (flag && !da->max_unmap_block_desc_count) {
		pr_err("dev[%p]: Thin Provisioning LBPRZ will not be set"
		       " because max_unmap_block_desc_count is zero\n",
		       da->da_dev);
		return -ENOSYS;
	}
	da->unmap_zeroes_data = flag;
	pr_debug("dev[%p]: SE Device Thin Provisioning LBPRZ bit: %d\n",
		 da->da_dev, flag);
	return count;
}

/*
 * Note, this can only be called on unexported SE Device Object.
 */
static ssize_t queue_depth_store(struct config_item *item,
		const char *page, size_t count)
{
	struct se_dev_attrib *da = to_attrib(item);
	struct se_device *dev = da->da_dev;
	u32 val;
	int ret;

	ret = kstrtou32(page, 0, &val);
	if (ret < 0)
		return ret;

	if (dev->export_count) {
		pr_err("dev[%p]: Unable to change SE Device TCQ while"
			" export_count is %d\n",
			dev, dev->export_count);
		return -EINVAL;
	}
	if (!val) {
		pr_err("dev[%p]: Illegal ZERO value for queue_depth\n", dev);
		return -EINVAL;
	}

	if (val > dev->dev_attrib.queue_depth) {
		if (val > dev->dev_attrib.hw_queue_depth) {
			pr_err("dev[%p]: Passed queue_depth:"
				" %u exceeds TCM/SE_Device MAX"
				" TCQ: %u\n", dev, val,
				dev->dev_attrib.hw_queue_depth);
			return -EINVAL;
		}
	}
	da->queue_depth = dev->queue_depth = val;
	pr_debug("dev[%p]: SE Device TCQ Depth changed to: %u\n", dev, val);
	return count;
}

static ssize_t optimal_sectors_store(struct config_item *item,
		const char *page, size_t count)
{
	struct se_dev_attrib *da = to_attrib(item);
	u32 val;
	int ret;

	ret = kstrtou32(page, 0, &val);
	if (ret < 0)
		return ret;

	if (da->da_dev->export_count) {
		pr_err("dev[%p]: Unable to change SE Device"
			" optimal_sectors while export_count is %d\n",
			da->da_dev, da->da_dev->export_count);
		return -EINVAL;
	}
	if (val > da->hw_max_sectors) {
		pr_err("dev[%p]: Passed optimal_sectors %u cannot be"
			" greater than hw_max_sectors: %u\n",
			da->da_dev, val, da->hw_max_sectors);
		return -EINVAL;
	}

	da->optimal_sectors = val;
	pr_debug("dev[%p]: SE Device optimal_sectors changed to %u\n",
			da->da_dev, val);
	return count;
}

static ssize_t block_size_store(struct config_item *item,
		const char *page, size_t count)
{
	struct se_dev_attrib *da = to_attrib(item);
	u32 val;
	int ret;

	ret = kstrtou32(page, 0, &val);
	if (ret < 0)
		return ret;

	if (da->da_dev->export_count) {
		pr_err("dev[%p]: Unable to change SE Device block_size"
			" while export_count is %d\n",
			da->da_dev, da->da_dev->export_count);
		return -EINVAL;
	}

	if (val != 512 && val != 1024 && val != 2048 && val != 4096) {
		pr_err("dev[%p]: Illegal value for block_device: %u"
			" for SE device, must be 512, 1024, 2048 or 4096\n",
			da->da_dev, val);
		return -EINVAL;
	}

	da->block_size = val;
	if (da->max_bytes_per_io)
		da->hw_max_sectors = da->max_bytes_per_io / val;

	pr_debug("dev[%p]: SE Device block_size changed to %u\n",
			da->da_dev, val);
	return count;
}

static ssize_t alua_support_show(struct config_item *item, char *page)
{
	struct se_dev_attrib *da = to_attrib(item);
	u8 flags = da->da_dev->transport->transport_flags;

	return snprintf(page, PAGE_SIZE, "%d\n",
			flags & TRANSPORT_FLAG_PASSTHROUGH_ALUA ? 0 : 1);
}

static ssize_t pgr_support_show(struct config_item *item, char *page)
{
	struct se_dev_attrib *da = to_attrib(item);
	u8 flags = da->da_dev->transport->transport_flags;

	return snprintf(page, PAGE_SIZE, "%d\n",
			flags & TRANSPORT_FLAG_PASSTHROUGH_PGR ? 0 : 1);
}

CONFIGFS_ATTR(, emulate_model_alias);
CONFIGFS_ATTR(, emulate_dpo);
CONFIGFS_ATTR(, emulate_fua_write);
CONFIGFS_ATTR(, emulate_fua_read);
CONFIGFS_ATTR(, emulate_write_cache);
CONFIGFS_ATTR(, emulate_ua_intlck_ctrl);
CONFIGFS_ATTR(, emulate_tas);
CONFIGFS_ATTR(, emulate_tpu);
CONFIGFS_ATTR(, emulate_tpws);
CONFIGFS_ATTR(, emulate_caw);
CONFIGFS_ATTR(, emulate_3pc);
CONFIGFS_ATTR(, pi_prot_type);
CONFIGFS_ATTR_RO(, hw_pi_prot_type);
CONFIGFS_ATTR(, pi_prot_format);
CONFIGFS_ATTR(, pi_prot_verify);
CONFIGFS_ATTR(, enforce_pr_isids);
CONFIGFS_ATTR(, is_nonrot);
CONFIGFS_ATTR(, emulate_rest_reord);
CONFIGFS_ATTR(, force_pr_aptpl);
CONFIGFS_ATTR_RO(, hw_block_size);
CONFIGFS_ATTR(, block_size);
CONFIGFS_ATTR_RO(, hw_max_sectors);
CONFIGFS_ATTR(, optimal_sectors);
CONFIGFS_ATTR_RO(, hw_queue_depth);
CONFIGFS_ATTR(, queue_depth);
CONFIGFS_ATTR(, max_unmap_lba_count);
CONFIGFS_ATTR(, max_unmap_block_desc_count);
CONFIGFS_ATTR(, unmap_granularity);
CONFIGFS_ATTR(, unmap_granularity_alignment);
CONFIGFS_ATTR(, unmap_zeroes_data);
CONFIGFS_ATTR(, max_write_same_len);
CONFIGFS_ATTR_RO(, alua_support);
CONFIGFS_ATTR_RO(, pgr_support);

/*
 * dev_attrib attributes for devices using the target core SBC/SPC
 * interpreter.  Any backend using spc_parse_cdb should be using
 * these.
 */
struct configfs_attribute *sbc_attrib_attrs[] = {
	&attr_emulate_model_alias,
	&attr_emulate_dpo,
	&attr_emulate_fua_write,
	&attr_emulate_fua_read,
	&attr_emulate_write_cache,
	&attr_emulate_ua_intlck_ctrl,
	&attr_emulate_tas,
	&attr_emulate_tpu,
	&attr_emulate_tpws,
	&attr_emulate_caw,
	&attr_emulate_3pc,
	&attr_pi_prot_type,
	&attr_hw_pi_prot_type,
	&attr_pi_prot_format,
	&attr_pi_prot_verify,
	&attr_enforce_pr_isids,
	&attr_is_nonrot,
	&attr_emulate_rest_reord,
	&attr_force_pr_aptpl,
	&attr_hw_block_size,
	&attr_block_size,
	&attr_hw_max_sectors,
	&attr_optimal_sectors,
	&attr_hw_queue_depth,
	&attr_queue_depth,
	&attr_max_unmap_lba_count,
	&attr_max_unmap_block_desc_count,
	&attr_unmap_granularity,
	&attr_unmap_granularity_alignment,
	&attr_unmap_zeroes_data,
	&attr_max_write_same_len,
	&attr_alua_support,
	&attr_pgr_support,
	NULL,
};
EXPORT_SYMBOL(sbc_attrib_attrs);

/*
 * Minimal dev_attrib attributes for devices passing through CDBs.
 * In this case we only provide a few read-only attributes for
 * backwards compatibility.
 */
struct configfs_attribute *passthrough_attrib_attrs[] = {
	&attr_hw_pi_prot_type,
	&attr_hw_block_size,
	&attr_hw_max_sectors,
	&attr_hw_queue_depth,
	&attr_alua_support,
	&attr_pgr_support,
	NULL,
};
EXPORT_SYMBOL(passthrough_attrib_attrs);

TB_CIT_SETUP_DRV(dev_attrib, NULL, NULL);
TB_CIT_SETUP_DRV(dev_action, NULL, NULL);

/* End functions for struct config_item_type tb_dev_attrib_cit */

/*  Start functions for struct config_item_type tb_dev_wwn_cit */

static struct t10_wwn *to_t10_wwn(struct config_item *item)
{
	return container_of(to_config_group(item), struct t10_wwn, t10_wwn_group);
}

/*
 * VPD page 0x80 Unit serial
 */
static ssize_t target_wwn_vpd_unit_serial_show(struct config_item *item,
		char *page)
{
	return sprintf(page, "T10 VPD Unit Serial Number: %s\n",
		&to_t10_wwn(item)->unit_serial[0]);
}

static ssize_t target_wwn_vpd_unit_serial_store(struct config_item *item,
		const char *page, size_t count)
{
	struct t10_wwn *t10_wwn = to_t10_wwn(item);
	struct se_device *dev = t10_wwn->t10_dev;
	unsigned char buf[INQUIRY_VPD_SERIAL_LEN];

	/*
	 * If Linux/SCSI subsystem_api_t plugin got a VPD Unit Serial
	 * from the struct scsi_device level firmware, do not allow
	 * VPD Unit Serial to be emulated.
	 *
	 * Note this struct scsi_device could also be emulating VPD
	 * information from its drivers/scsi LLD.  But for now we assume
	 * it is doing 'the right thing' wrt a world wide unique
	 * VPD Unit Serial Number that OS dependent multipath can depend on.
	 */
	if (dev->dev_flags & DF_FIRMWARE_VPD_UNIT_SERIAL) {
		pr_err("Underlying SCSI device firmware provided VPD"
			" Unit Serial, ignoring request\n");
		return -EOPNOTSUPP;
	}

	if (strlen(page) >= INQUIRY_VPD_SERIAL_LEN) {
		pr_err("Emulated VPD Unit Serial exceeds"
		" INQUIRY_VPD_SERIAL_LEN: %d\n", INQUIRY_VPD_SERIAL_LEN);
		return -EOVERFLOW;
	}
	/*
	 * Check to see if any active $FABRIC_MOD exports exist.  If they
	 * do exist, fail here as changing this information on the fly
	 * (underneath the initiator side OS dependent multipath code)
	 * could cause negative effects.
	 */
	if (dev->export_count) {
		pr_err("Unable to set VPD Unit Serial while"
			" active %d $FABRIC_MOD exports exist\n",
			dev->export_count);
		return -EINVAL;
	}

	/*
	 * This currently assumes ASCII encoding for emulated VPD Unit Serial.
	 *
	 * Also, strip any newline added from the userspace
	 * echo $UUID > $TARGET/$HBA/$STORAGE_OBJECT/wwn/vpd_unit_serial
	 */
	memset(buf, 0, INQUIRY_VPD_SERIAL_LEN);
	snprintf(buf, INQUIRY_VPD_SERIAL_LEN, "%s", page);
	snprintf(dev->t10_wwn.unit_serial, INQUIRY_VPD_SERIAL_LEN,
			"%s", strstrip(buf));
	dev->dev_flags |= DF_EMULATED_VPD_UNIT_SERIAL;

	pr_debug("Target_Core_ConfigFS: Set emulated VPD Unit Serial:"
			" %s\n", dev->t10_wwn.unit_serial);

	return count;
}

/*
 * VPD page 0x83 Protocol Identifier
 */
static ssize_t target_wwn_vpd_protocol_identifier_show(struct config_item *item,
		char *page)
{
	struct t10_wwn *t10_wwn = to_t10_wwn(item);
	struct t10_vpd *vpd;
	unsigned char buf[VPD_TMP_BUF_SIZE];
	ssize_t len = 0;

	memset(buf, 0, VPD_TMP_BUF_SIZE);

	spin_lock(&t10_wwn->t10_vpd_lock);
	list_for_each_entry(vpd, &t10_wwn->t10_vpd_list, vpd_list) {
		if (!vpd->protocol_identifier_set)
			continue;

		transport_dump_vpd_proto_id(vpd, buf, VPD_TMP_BUF_SIZE);

		if (len + strlen(buf) >= PAGE_SIZE)
			break;

		len += sprintf(page+len, "%s", buf);
	}
	spin_unlock(&t10_wwn->t10_vpd_lock);

	return len;
}

/*
 * Generic wrapper for dumping VPD identifiers by association.
 */
#define DEF_DEV_WWN_ASSOC_SHOW(_name, _assoc)				\
static ssize_t target_wwn_##_name##_show(struct config_item *item,	\
		char *page)						\
{									\
	struct t10_wwn *t10_wwn = to_t10_wwn(item);			\
	struct t10_vpd *vpd;						\
	unsigned char buf[VPD_TMP_BUF_SIZE];				\
	ssize_t len = 0;						\
									\
	spin_lock(&t10_wwn->t10_vpd_lock);				\
	list_for_each_entry(vpd, &t10_wwn->t10_vpd_list, vpd_list) {	\
		if (vpd->association != _assoc)				\
			continue;					\
									\
		memset(buf, 0, VPD_TMP_BUF_SIZE);			\
		transport_dump_vpd_assoc(vpd, buf, VPD_TMP_BUF_SIZE);	\
		if (len + strlen(buf) >= PAGE_SIZE)			\
			break;						\
		len += sprintf(page+len, "%s", buf);			\
									\
		memset(buf, 0, VPD_TMP_BUF_SIZE);			\
		transport_dump_vpd_ident_type(vpd, buf, VPD_TMP_BUF_SIZE); \
		if (len + strlen(buf) >= PAGE_SIZE)			\
			break;						\
		len += sprintf(page+len, "%s", buf);			\
									\
		memset(buf, 0, VPD_TMP_BUF_SIZE);			\
		transport_dump_vpd_ident(vpd, buf, VPD_TMP_BUF_SIZE); \
		if (len + strlen(buf) >= PAGE_SIZE)			\
			break;						\
		len += sprintf(page+len, "%s", buf);			\
	}								\
	spin_unlock(&t10_wwn->t10_vpd_lock);				\
									\
	return len;							\
}

/* VPD page 0x83 Association: Logical Unit */
DEF_DEV_WWN_ASSOC_SHOW(vpd_assoc_logical_unit, 0x00);
/* VPD page 0x83 Association: Target Port */
DEF_DEV_WWN_ASSOC_SHOW(vpd_assoc_target_port, 0x10);
/* VPD page 0x83 Association: SCSI Target Device */
DEF_DEV_WWN_ASSOC_SHOW(vpd_assoc_scsi_target_device, 0x20);

CONFIGFS_ATTR(target_wwn_, vpd_unit_serial);
CONFIGFS_ATTR_RO(target_wwn_, vpd_protocol_identifier);
CONFIGFS_ATTR_RO(target_wwn_, vpd_assoc_logical_unit);
CONFIGFS_ATTR_RO(target_wwn_, vpd_assoc_target_port);
CONFIGFS_ATTR_RO(target_wwn_, vpd_assoc_scsi_target_device);

static struct configfs_attribute *target_core_dev_wwn_attrs[] = {
	&target_wwn_attr_vpd_unit_serial,
	&target_wwn_attr_vpd_protocol_identifier,
	&target_wwn_attr_vpd_assoc_logical_unit,
	&target_wwn_attr_vpd_assoc_target_port,
	&target_wwn_attr_vpd_assoc_scsi_target_device,
	NULL,
};

TB_CIT_SETUP(dev_wwn, NULL, NULL, target_core_dev_wwn_attrs);

/*  End functions for struct config_item_type tb_dev_wwn_cit */

/*  Start functions for struct config_item_type tb_dev_pr_cit */

static struct se_device *pr_to_dev(struct config_item *item)
{
	return container_of(to_config_group(item), struct se_device,
			dev_pr_group);
}

static ssize_t target_core_dev_pr_show_spc3_res(struct se_device *dev,
		char *page)
{
	struct se_node_acl *se_nacl;
	struct t10_pr_registration *pr_reg;
	char i_buf[PR_REG_ISID_ID_LEN];

	memset(i_buf, 0, PR_REG_ISID_ID_LEN);

	pr_reg = dev->dev_pr_res_holder;
	if (!pr_reg)
		return sprintf(page, "No SPC-3 Reservation holder\n");

	se_nacl = pr_reg->pr_reg_nacl;
	core_pr_dump_initiator_port(pr_reg, i_buf, PR_REG_ISID_ID_LEN);

	return sprintf(page, "SPC-3 Reservation: %s Initiator: %s%s\n",
		se_nacl->se_tpg->se_tpg_tfo->get_fabric_name(),
		se_nacl->initiatorname, i_buf);
}

static ssize_t target_core_dev_pr_show_spc2_res(struct se_device *dev,
		char *page)
{
	struct se_node_acl *se_nacl;
	ssize_t len;

	se_nacl = dev->dev_reserved_node_acl;
	if (se_nacl) {
		len = sprintf(page,
			      "SPC-2 Reservation: %s Initiator: %s\n",
			      se_nacl->se_tpg->se_tpg_tfo->get_fabric_name(),
			      se_nacl->initiatorname);
	} else {
		len = sprintf(page, "No SPC-2 Reservation holder\n");
	}
	return len;
}

static ssize_t target_pr_res_holder_show(struct config_item *item, char *page)
{
	struct se_device *dev = pr_to_dev(item);
	int ret;

	if (dev->transport->transport_flags & TRANSPORT_FLAG_PASSTHROUGH_PGR)
		return sprintf(page, "Passthrough\n");

	spin_lock(&dev->dev_reservation_lock);
	if (dev->dev_reservation_flags & DRF_SPC2_RESERVATIONS)
		ret = target_core_dev_pr_show_spc2_res(dev, page);
	else
		ret = target_core_dev_pr_show_spc3_res(dev, page);
	spin_unlock(&dev->dev_reservation_lock);
	return ret;
}

static ssize_t target_pr_res_pr_all_tgt_pts_show(struct config_item *item,
		char *page)
{
	struct se_device *dev = pr_to_dev(item);
	ssize_t len = 0;

	spin_lock(&dev->dev_reservation_lock);
	if (!dev->dev_pr_res_holder) {
		len = sprintf(page, "No SPC-3 Reservation holder\n");
	} else if (dev->dev_pr_res_holder->pr_reg_all_tg_pt) {
		len = sprintf(page, "SPC-3 Reservation: All Target"
			" Ports registration\n");
	} else {
		len = sprintf(page, "SPC-3 Reservation: Single"
			" Target Port registration\n");
	}

	spin_unlock(&dev->dev_reservation_lock);
	return len;
}

static ssize_t target_pr_res_pr_generation_show(struct config_item *item,
		char *page)
{
	return sprintf(page, "0x%08x\n", pr_to_dev(item)->t10_pr.pr_generation);
}


static ssize_t target_pr_res_pr_holder_tg_port_show(struct config_item *item,
		char *page)
{
	struct se_device *dev = pr_to_dev(item);
	struct se_node_acl *se_nacl;
	struct se_portal_group *se_tpg;
	struct t10_pr_registration *pr_reg;
	const struct target_core_fabric_ops *tfo;
	ssize_t len = 0;

	spin_lock(&dev->dev_reservation_lock);
	pr_reg = dev->dev_pr_res_holder;
	if (!pr_reg) {
		len = sprintf(page, "No SPC-3 Reservation holder\n");
		goto out_unlock;
	}

	se_nacl = pr_reg->pr_reg_nacl;
	se_tpg = se_nacl->se_tpg;
	tfo = se_tpg->se_tpg_tfo;

	len += sprintf(page+len, "SPC-3 Reservation: %s"
		" Target Node Endpoint: %s\n", tfo->get_fabric_name(),
		tfo->tpg_get_wwn(se_tpg));
	len += sprintf(page+len, "SPC-3 Reservation: Relative Port"
		" Identifier Tag: %hu %s Portal Group Tag: %hu"
		" %s Logical Unit: %llu\n", pr_reg->tg_pt_sep_rtpi,
		tfo->get_fabric_name(), tfo->tpg_get_tag(se_tpg),
		tfo->get_fabric_name(), pr_reg->pr_aptpl_target_lun);

out_unlock:
	spin_unlock(&dev->dev_reservation_lock);
	return len;
}


static ssize_t target_pr_res_pr_registered_i_pts_show(struct config_item *item,
		char *page)
{
	struct se_device *dev = pr_to_dev(item);
	const struct target_core_fabric_ops *tfo;
	struct t10_pr_registration *pr_reg;
	unsigned char buf[384];
	char i_buf[PR_REG_ISID_ID_LEN];
	ssize_t len = 0;
	int reg_count = 0;

	len += sprintf(page+len, "SPC-3 PR Registrations:\n");

	spin_lock(&dev->t10_pr.registration_lock);
	list_for_each_entry(pr_reg, &dev->t10_pr.registration_list,
			pr_reg_list) {

		memset(buf, 0, 384);
		memset(i_buf, 0, PR_REG_ISID_ID_LEN);
		tfo = pr_reg->pr_reg_nacl->se_tpg->se_tpg_tfo;
		core_pr_dump_initiator_port(pr_reg, i_buf,
					PR_REG_ISID_ID_LEN);
		sprintf(buf, "%s Node: %s%s Key: 0x%016Lx PRgen: 0x%08x\n",
			tfo->get_fabric_name(),
			pr_reg->pr_reg_nacl->initiatorname, i_buf, pr_reg->pr_res_key,
			pr_reg->pr_res_generation);

		if (len + strlen(buf) >= PAGE_SIZE)
			break;

		len += sprintf(page+len, "%s", buf);
		reg_count++;
	}
	spin_unlock(&dev->t10_pr.registration_lock);

	if (!reg_count)
		len += sprintf(page+len, "None\n");

	return len;
}

static ssize_t target_pr_res_pr_type_show(struct config_item *item, char *page)
{
	struct se_device *dev = pr_to_dev(item);
	struct t10_pr_registration *pr_reg;
	ssize_t len = 0;

	spin_lock(&dev->dev_reservation_lock);
	pr_reg = dev->dev_pr_res_holder;
	if (pr_reg) {
		len = sprintf(page, "SPC-3 Reservation Type: %s\n",
			core_scsi3_pr_dump_type(pr_reg->pr_res_type));
	} else {
		len = sprintf(page, "No SPC-3 Reservation holder\n");
	}

	spin_unlock(&dev->dev_reservation_lock);
	return len;
}

static ssize_t target_pr_res_type_show(struct config_item *item, char *page)
{
	struct se_device *dev = pr_to_dev(item);

	if (dev->transport->transport_flags & TRANSPORT_FLAG_PASSTHROUGH_PGR)
		return sprintf(page, "SPC_PASSTHROUGH\n");
	else if (dev->dev_reservation_flags & DRF_SPC2_RESERVATIONS)
		return sprintf(page, "SPC2_RESERVATIONS\n");
	else
		return sprintf(page, "SPC3_PERSISTENT_RESERVATIONS\n");
}

static ssize_t target_pr_res_aptpl_active_show(struct config_item *item,
		char *page)
{
	struct se_device *dev = pr_to_dev(item);

	if (dev->transport->transport_flags & TRANSPORT_FLAG_PASSTHROUGH_PGR)
		return 0;

	return sprintf(page, "APTPL Bit Status: %s\n",
		(dev->t10_pr.pr_aptpl_active) ? "Activated" : "Disabled");
}

static ssize_t target_pr_res_aptpl_metadata_show(struct config_item *item,
		char *page)
{
	struct se_device *dev = pr_to_dev(item);

	if (dev->transport->transport_flags & TRANSPORT_FLAG_PASSTHROUGH_PGR)
		return 0;

	return sprintf(page, "Ready to process PR APTPL metadata..\n");
}

enum {
	Opt_initiator_fabric, Opt_initiator_node, Opt_initiator_sid,
	Opt_sa_res_key, Opt_res_holder, Opt_res_type, Opt_res_scope,
	Opt_res_all_tg_pt, Opt_mapped_lun, Opt_target_fabric,
	Opt_target_node, Opt_tpgt, Opt_port_rtpi, Opt_target_lun, Opt_err
};

static match_table_t tokens = {
	{Opt_initiator_fabric, "initiator_fabric=%s"},
	{Opt_initiator_node, "initiator_node=%s"},
	{Opt_initiator_sid, "initiator_sid=%s"},
	{Opt_sa_res_key, "sa_res_key=%s"},
	{Opt_res_holder, "res_holder=%d"},
	{Opt_res_type, "res_type=%d"},
	{Opt_res_scope, "res_scope=%d"},
	{Opt_res_all_tg_pt, "res_all_tg_pt=%d"},
	{Opt_mapped_lun, "mapped_lun=%u"},
	{Opt_target_fabric, "target_fabric=%s"},
	{Opt_target_node, "target_node=%s"},
	{Opt_tpgt, "tpgt=%d"},
	{Opt_port_rtpi, "port_rtpi=%d"},
	{Opt_target_lun, "target_lun=%u"},
	{Opt_err, NULL}
};

static ssize_t target_pr_res_aptpl_metadata_store(struct config_item *item,
		const char *page, size_t count)
{
	struct se_device *dev = pr_to_dev(item);
	unsigned char *i_fabric = NULL, *i_port = NULL, *isid = NULL;
	unsigned char *t_fabric = NULL, *t_port = NULL;
	char *orig, *ptr, *opts;
	substring_t args[MAX_OPT_ARGS];
	unsigned long long tmp_ll;
	u64 sa_res_key = 0;
	u64 mapped_lun = 0, target_lun = 0;
	int ret = -1, res_holder = 0, all_tg_pt = 0, arg, token;
	u16 tpgt = 0;
	u8 type = 0;

	if (dev->transport->transport_flags & TRANSPORT_FLAG_PASSTHROUGH_PGR)
		return count;
	if (dev->dev_reservation_flags & DRF_SPC2_RESERVATIONS)
		return count;

	if (dev->export_count) {
		pr_debug("Unable to process APTPL metadata while"
			" active fabric exports exist\n");
		return -EINVAL;
	}

	opts = kstrdup(page, GFP_KERNEL);
	if (!opts)
		return -ENOMEM;

	orig = opts;
	while ((ptr = strsep(&opts, ",\n")) != NULL) {
		if (!*ptr)
			continue;

		token = match_token(ptr, tokens, args);
		switch (token) {
		case Opt_initiator_fabric:
			i_fabric = match_strdup(args);
			if (!i_fabric) {
				ret = -ENOMEM;
				goto out;
			}
			break;
		case Opt_initiator_node:
			i_port = match_strdup(args);
			if (!i_port) {
				ret = -ENOMEM;
				goto out;
			}
			if (strlen(i_port) >= PR_APTPL_MAX_IPORT_LEN) {
				pr_err("APTPL metadata initiator_node="
					" exceeds PR_APTPL_MAX_IPORT_LEN: %d\n",
					PR_APTPL_MAX_IPORT_LEN);
				ret = -EINVAL;
				break;
			}
			break;
		case Opt_initiator_sid:
			isid = match_strdup(args);
			if (!isid) {
				ret = -ENOMEM;
				goto out;
			}
			if (strlen(isid) >= PR_REG_ISID_LEN) {
				pr_err("APTPL metadata initiator_isid"
					"= exceeds PR_REG_ISID_LEN: %d\n",
					PR_REG_ISID_LEN);
				ret = -EINVAL;
				break;
			}
			break;
		case Opt_sa_res_key:
			ret = match_u64(args,  &tmp_ll);
			if (ret < 0) {
				pr_err("kstrtoull() failed for sa_res_key=\n");
				goto out;
			}
			sa_res_key = (u64)tmp_ll;
			break;
		/*
		 * PR APTPL Metadata for Reservation
		 */
		case Opt_res_holder:
			ret = match_int(args, &arg);
			if (ret)
				goto out;
			res_holder = arg;
			break;
		case Opt_res_type:
			ret = match_int(args, &arg);
			if (ret)
				goto out;
			type = (u8)arg;
			break;
		case Opt_res_scope:
			ret = match_int(args, &arg);
			if (ret)
				goto out;
			break;
		case Opt_res_all_tg_pt:
			ret = match_int(args, &arg);
			if (ret)
				goto out;
			all_tg_pt = (int)arg;
			break;
		case Opt_mapped_lun:
			ret = match_u64(args, &tmp_ll);
			if (ret)
				goto out;
			mapped_lun = (u64)tmp_ll;
			break;
		/*
		 * PR APTPL Metadata for Target Port
		 */
		case Opt_target_fabric:
			t_fabric = match_strdup(args);
			if (!t_fabric) {
				ret = -ENOMEM;
				goto out;
			}
			break;
		case Opt_target_node:
			t_port = match_strdup(args);
			if (!t_port) {
				ret = -ENOMEM;
				goto out;
			}
			if (strlen(t_port) >= PR_APTPL_MAX_TPORT_LEN) {
				pr_err("APTPL metadata target_node="
					" exceeds PR_APTPL_MAX_TPORT_LEN: %d\n",
					PR_APTPL_MAX_TPORT_LEN);
				ret = -EINVAL;
				break;
			}
			break;
		case Opt_tpgt:
			ret = match_int(args, &arg);
			if (ret)
				goto out;
			tpgt = (u16)arg;
			break;
		case Opt_port_rtpi:
			ret = match_int(args, &arg);
			if (ret)
				goto out;
			break;
		case Opt_target_lun:
			ret = match_u64(args, &tmp_ll);
			if (ret)
				goto out;
			target_lun = (u64)tmp_ll;
			break;
		default:
			break;
		}
	}

	if (!i_port || !t_port || !sa_res_key) {
		pr_err("Illegal parameters for APTPL registration\n");
		ret = -EINVAL;
		goto out;
	}

	if (res_holder && !(type)) {
		pr_err("Illegal PR type: 0x%02x for reservation"
				" holder\n", type);
		ret = -EINVAL;
		goto out;
	}

	ret = core_scsi3_alloc_aptpl_registration(&dev->t10_pr, sa_res_key,
			i_port, isid, mapped_lun, t_port, tpgt, target_lun,
			res_holder, all_tg_pt, type);
out:
	kfree(i_fabric);
	kfree(i_port);
	kfree(isid);
	kfree(t_fabric);
	kfree(t_port);
	kfree(orig);
	return (ret == 0) ? count : ret;
}


CONFIGFS_ATTR_RO(target_pr_, res_holder);
CONFIGFS_ATTR_RO(target_pr_, res_pr_all_tgt_pts);
CONFIGFS_ATTR_RO(target_pr_, res_pr_generation);
CONFIGFS_ATTR_RO(target_pr_, res_pr_holder_tg_port);
CONFIGFS_ATTR_RO(target_pr_, res_pr_registered_i_pts);
CONFIGFS_ATTR_RO(target_pr_, res_pr_type);
CONFIGFS_ATTR_RO(target_pr_, res_type);
CONFIGFS_ATTR_RO(target_pr_, res_aptpl_active);
CONFIGFS_ATTR(target_pr_, res_aptpl_metadata);

static struct configfs_attribute *target_core_dev_pr_attrs[] = {
	&target_pr_attr_res_holder,
	&target_pr_attr_res_pr_all_tgt_pts,
	&target_pr_attr_res_pr_generation,
	&target_pr_attr_res_pr_holder_tg_port,
	&target_pr_attr_res_pr_registered_i_pts,
	&target_pr_attr_res_pr_type,
	&target_pr_attr_res_type,
	&target_pr_attr_res_aptpl_active,
	&target_pr_attr_res_aptpl_metadata,
	NULL,
};

TB_CIT_SETUP(dev_pr, NULL, NULL, target_core_dev_pr_attrs);

/*  End functions for struct config_item_type tb_dev_pr_cit */

/*  Start functions for struct config_item_type tb_dev_cit */

static inline struct se_device *to_device(struct config_item *item)
{
	return container_of(to_config_group(item), struct se_device, dev_group);
}

static ssize_t target_dev_info_show(struct config_item *item, char *page)
{
	struct se_device *dev = to_device(item);
	int bl = 0;
	ssize_t read_bytes = 0;

	transport_dump_dev_state(dev, page, &bl);
	read_bytes += bl;
	read_bytes += dev->transport->show_configfs_dev_params(dev,
			page+read_bytes);
	return read_bytes;
}

static ssize_t target_dev_control_store(struct config_item *item,
		const char *page, size_t count)
{
	struct se_device *dev = to_device(item);

	return dev->transport->set_configfs_dev_params(dev, page, count);
}

static ssize_t target_dev_alias_show(struct config_item *item, char *page)
{
	struct se_device *dev = to_device(item);

	if (!(dev->dev_flags & DF_USING_ALIAS))
		return 0;

	return snprintf(page, PAGE_SIZE, "%s\n", dev->dev_alias);
}

static ssize_t target_dev_alias_store(struct config_item *item,
		const char *page, size_t count)
{
	struct se_device *dev = to_device(item);
	struct se_hba *hba = dev->se_hba;
	ssize_t read_bytes;

	if (count > (SE_DEV_ALIAS_LEN-1)) {
		pr_err("alias count: %d exceeds"
			" SE_DEV_ALIAS_LEN-1: %u\n", (int)count,
			SE_DEV_ALIAS_LEN-1);
		return -EINVAL;
	}

	read_bytes = snprintf(&dev->dev_alias[0], SE_DEV_ALIAS_LEN, "%s", page);
	if (!read_bytes)
		return -EINVAL;
	if (dev->dev_alias[read_bytes - 1] == '\n')
		dev->dev_alias[read_bytes - 1] = '\0';

	dev->dev_flags |= DF_USING_ALIAS;

	pr_debug("Target_Core_ConfigFS: %s/%s set alias: %s\n",
		config_item_name(&hba->hba_group.cg_item),
		config_item_name(&dev->dev_group.cg_item),
		dev->dev_alias);

	return read_bytes;
}

static ssize_t target_dev_udev_path_show(struct config_item *item, char *page)
{
	struct se_device *dev = to_device(item);

	if (!(dev->dev_flags & DF_USING_UDEV_PATH))
		return 0;

	return snprintf(page, PAGE_SIZE, "%s\n", dev->udev_path);
}

static ssize_t target_dev_udev_path_store(struct config_item *item,
		const char *page, size_t count)
{
	struct se_device *dev = to_device(item);
	struct se_hba *hba = dev->se_hba;
	ssize_t read_bytes;

	if (count > (SE_UDEV_PATH_LEN-1)) {
		pr_err("udev_path count: %d exceeds"
			" SE_UDEV_PATH_LEN-1: %u\n", (int)count,
			SE_UDEV_PATH_LEN-1);
		return -EINVAL;
	}

	read_bytes = snprintf(&dev->udev_path[0], SE_UDEV_PATH_LEN,
			"%s", page);
	if (!read_bytes)
		return -EINVAL;
	if (dev->udev_path[read_bytes - 1] == '\n')
		dev->udev_path[read_bytes - 1] = '\0';

	dev->dev_flags |= DF_USING_UDEV_PATH;

	pr_debug("Target_Core_ConfigFS: %s/%s set udev_path: %s\n",
		config_item_name(&hba->hba_group.cg_item),
		config_item_name(&dev->dev_group.cg_item),
		dev->udev_path);

	return read_bytes;
}

static ssize_t target_dev_enable_show(struct config_item *item, char *page)
{
	struct se_device *dev = to_device(item);

	return snprintf(page, PAGE_SIZE, "%d\n", !!(dev->dev_flags & DF_CONFIGURED));
}

static ssize_t target_dev_enable_store(struct config_item *item,
		const char *page, size_t count)
{
	struct se_device *dev = to_device(item);
	char *ptr;
	int ret;

	ptr = strstr(page, "1");
	if (!ptr) {
		pr_err("For dev_enable ops, only valid value"
				" is \"1\"\n");
		return -EINVAL;
	}

	ret = target_configure_device(dev);
	if (ret)
		return ret;
	return count;
}

static ssize_t target_dev_alua_lu_gp_show(struct config_item *item, char *page)
{
	struct se_device *dev = to_device(item);
	struct config_item *lu_ci;
	struct t10_alua_lu_gp *lu_gp;
	struct t10_alua_lu_gp_member *lu_gp_mem;
	ssize_t len = 0;

	lu_gp_mem = dev->dev_alua_lu_gp_mem;
	if (!lu_gp_mem)
		return 0;

	spin_lock(&lu_gp_mem->lu_gp_mem_lock);
	lu_gp = lu_gp_mem->lu_gp;
	if (lu_gp) {
		lu_ci = &lu_gp->lu_gp_group.cg_item;
		len += sprintf(page, "LU Group Alias: %s\nLU Group ID: %hu\n",
			config_item_name(lu_ci), lu_gp->lu_gp_id);
	}
	spin_unlock(&lu_gp_mem->lu_gp_mem_lock);

	return len;
}

static ssize_t target_dev_alua_lu_gp_store(struct config_item *item,
		const char *page, size_t count)
{
	struct se_device *dev = to_device(item);
	struct se_hba *hba = dev->se_hba;
	struct t10_alua_lu_gp *lu_gp = NULL, *lu_gp_new = NULL;
	struct t10_alua_lu_gp_member *lu_gp_mem;
	unsigned char buf[LU_GROUP_NAME_BUF];
	int move = 0;

	lu_gp_mem = dev->dev_alua_lu_gp_mem;
	if (!lu_gp_mem)
		return count;

	if (count > LU_GROUP_NAME_BUF) {
		pr_err("ALUA LU Group Alias too large!\n");
		return -EINVAL;
	}
	memset(buf, 0, LU_GROUP_NAME_BUF);
	memcpy(buf, page, count);
	/*
	 * Any ALUA logical unit alias besides "NULL" means we will be
	 * making a new group association.
	 */
	if (strcmp(strstrip(buf), "NULL")) {
		/*
		 * core_alua_get_lu_gp_by_name() will increment reference to
		 * struct t10_alua_lu_gp.  This reference is released with
		 * core_alua_get_lu_gp_by_name below().
		 */
		lu_gp_new = core_alua_get_lu_gp_by_name(strstrip(buf));
		if (!lu_gp_new)
			return -ENODEV;
	}

	spin_lock(&lu_gp_mem->lu_gp_mem_lock);
	lu_gp = lu_gp_mem->lu_gp;
	if (lu_gp) {
		/*
		 * Clearing an existing lu_gp association, and replacing
		 * with NULL
		 */
		if (!lu_gp_new) {
			pr_debug("Target_Core_ConfigFS: Releasing %s/%s"
				" from ALUA LU Group: core/alua/lu_gps/%s, ID:"
				" %hu\n",
				config_item_name(&hba->hba_group.cg_item),
				config_item_name(&dev->dev_group.cg_item),
				config_item_name(&lu_gp->lu_gp_group.cg_item),
				lu_gp->lu_gp_id);

			__core_alua_drop_lu_gp_mem(lu_gp_mem, lu_gp);
			spin_unlock(&lu_gp_mem->lu_gp_mem_lock);

			return count;
		}
		/*
		 * Removing existing association of lu_gp_mem with lu_gp
		 */
		__core_alua_drop_lu_gp_mem(lu_gp_mem, lu_gp);
		move = 1;
	}
	/*
	 * Associate lu_gp_mem with lu_gp_new.
	 */
	__core_alua_attach_lu_gp_mem(lu_gp_mem, lu_gp_new);
	spin_unlock(&lu_gp_mem->lu_gp_mem_lock);

	pr_debug("Target_Core_ConfigFS: %s %s/%s to ALUA LU Group:"
		" core/alua/lu_gps/%s, ID: %hu\n",
		(move) ? "Moving" : "Adding",
		config_item_name(&hba->hba_group.cg_item),
		config_item_name(&dev->dev_group.cg_item),
		config_item_name(&lu_gp_new->lu_gp_group.cg_item),
		lu_gp_new->lu_gp_id);

	core_alua_put_lu_gp_from_name(lu_gp_new);
	return count;
}

static ssize_t target_dev_lba_map_show(struct config_item *item, char *page)
{
	struct se_device *dev = to_device(item);
	struct t10_alua_lba_map *map;
	struct t10_alua_lba_map_member *mem;
	char *b = page;
	int bl = 0;
	char state;

	spin_lock(&dev->t10_alua.lba_map_lock);
	if (!list_empty(&dev->t10_alua.lba_map_list))
	    bl += sprintf(b + bl, "%u %u\n",
			  dev->t10_alua.lba_map_segment_size,
			  dev->t10_alua.lba_map_segment_multiplier);
	list_for_each_entry(map, &dev->t10_alua.lba_map_list, lba_map_list) {
		bl += sprintf(b + bl, "%llu %llu",
			      map->lba_map_first_lba, map->lba_map_last_lba);
		list_for_each_entry(mem, &map->lba_map_mem_list,
				    lba_map_mem_list) {
			switch (mem->lba_map_mem_alua_state) {
			case ALUA_ACCESS_STATE_ACTIVE_OPTIMIZED:
				state = 'O';
				break;
			case ALUA_ACCESS_STATE_ACTIVE_NON_OPTIMIZED:
				state = 'A';
				break;
			case ALUA_ACCESS_STATE_STANDBY:
				state = 'S';
				break;
			case ALUA_ACCESS_STATE_UNAVAILABLE:
				state = 'U';
				break;
			default:
				state = '.';
				break;
			}
			bl += sprintf(b + bl, " %d:%c",
				      mem->lba_map_mem_alua_pg_id, state);
		}
		bl += sprintf(b + bl, "\n");
	}
	spin_unlock(&dev->t10_alua.lba_map_lock);
	return bl;
}

static ssize_t target_dev_lba_map_store(struct config_item *item,
		const char *page, size_t count)
{
	struct se_device *dev = to_device(item);
	struct t10_alua_lba_map *lba_map = NULL;
	struct list_head lba_list;
	char *map_entries, *orig, *ptr;
	char state;
	int pg_num = -1, pg;
	int ret = 0, num = 0, pg_id, alua_state;
	unsigned long start_lba = -1, end_lba = -1;
	unsigned long segment_size = -1, segment_mult = -1;

	orig = map_entries = kstrdup(page, GFP_KERNEL);
	if (!map_entries)
		return -ENOMEM;

	INIT_LIST_HEAD(&lba_list);
	while ((ptr = strsep(&map_entries, "\n")) != NULL) {
		if (!*ptr)
			continue;

		if (num == 0) {
			if (sscanf(ptr, "%lu %lu\n",
				   &segment_size, &segment_mult) != 2) {
				pr_err("Invalid line %d\n", num);
				ret = -EINVAL;
				break;
			}
			num++;
			continue;
		}
		if (sscanf(ptr, "%lu %lu", &start_lba, &end_lba) != 2) {
			pr_err("Invalid line %d\n", num);
			ret = -EINVAL;
			break;
		}
		ptr = strchr(ptr, ' ');
		if (!ptr) {
			pr_err("Invalid line %d, missing end lba\n", num);
			ret = -EINVAL;
			break;
		}
		ptr++;
		ptr = strchr(ptr, ' ');
		if (!ptr) {
			pr_err("Invalid line %d, missing state definitions\n",
			       num);
			ret = -EINVAL;
			break;
		}
		ptr++;
		lba_map = core_alua_allocate_lba_map(&lba_list,
						     start_lba, end_lba);
		if (IS_ERR(lba_map)) {
			ret = PTR_ERR(lba_map);
			break;
		}
		pg = 0;
		while (sscanf(ptr, "%d:%c", &pg_id, &state) == 2) {
			switch (state) {
			case 'O':
				alua_state = ALUA_ACCESS_STATE_ACTIVE_OPTIMIZED;
				break;
			case 'A':
				alua_state = ALUA_ACCESS_STATE_ACTIVE_NON_OPTIMIZED;
				break;
			case 'S':
				alua_state = ALUA_ACCESS_STATE_STANDBY;
				break;
			case 'U':
				alua_state = ALUA_ACCESS_STATE_UNAVAILABLE;
				break;
			default:
				pr_err("Invalid ALUA state '%c'\n", state);
				ret = -EINVAL;
				goto out;
			}

			ret = core_alua_allocate_lba_map_mem(lba_map,
							     pg_id, alua_state);
			if (ret) {
				pr_err("Invalid target descriptor %d:%c "
				       "at line %d\n",
				       pg_id, state, num);
				break;
			}
			pg++;
			ptr = strchr(ptr, ' ');
			if (ptr)
				ptr++;
			else
				break;
		}
		if (pg_num == -1)
		    pg_num = pg;
		else if (pg != pg_num) {
			pr_err("Only %d from %d port groups definitions "
			       "at line %d\n", pg, pg_num, num);
			ret = -EINVAL;
			break;
		}
		num++;
	}
out:
	if (ret) {
		core_alua_free_lba_map(&lba_list);
		count = ret;
	} else
		core_alua_set_lba_map(dev, &lba_list,
				      segment_size, segment_mult);
	kfree(orig);
	return count;
}

CONFIGFS_ATTR_RO(target_dev_, info);
CONFIGFS_ATTR_WO(target_dev_, control);
CONFIGFS_ATTR(target_dev_, alias);
CONFIGFS_ATTR(target_dev_, udev_path);
CONFIGFS_ATTR(target_dev_, enable);
CONFIGFS_ATTR(target_dev_, alua_lu_gp);
CONFIGFS_ATTR(target_dev_, lba_map);

static struct configfs_attribute *target_core_dev_attrs[] = {
	&target_dev_attr_info,
	&target_dev_attr_control,
	&target_dev_attr_alias,
	&target_dev_attr_udev_path,
	&target_dev_attr_enable,
	&target_dev_attr_alua_lu_gp,
	&target_dev_attr_lba_map,
	NULL,
};

static void target_core_dev_release(struct config_item *item)
{
	struct config_group *dev_cg = to_config_group(item);
	struct se_device *dev =
		container_of(dev_cg, struct se_device, dev_group);

	target_free_device(dev);
}

/*
 * Used in target_core_fabric_configfs.c to verify valid se_device symlink
 * within target_fabric_port_link()
 */
struct configfs_item_operations target_core_dev_item_ops = {
	.release		= target_core_dev_release,
};

TB_CIT_SETUP(dev, &target_core_dev_item_ops, NULL, target_core_dev_attrs);

/* End functions for struct config_item_type tb_dev_cit */

/* Start functions for struct config_item_type target_core_alua_lu_gp_cit */

static inline struct t10_alua_lu_gp *to_lu_gp(struct config_item *item)
{
	return container_of(to_config_group(item), struct t10_alua_lu_gp,
			lu_gp_group);
}

static ssize_t target_lu_gp_lu_gp_id_show(struct config_item *item, char *page)
{
	struct t10_alua_lu_gp *lu_gp = to_lu_gp(item);

	if (!lu_gp->lu_gp_valid_id)
		return 0;
	return sprintf(page, "%hu\n", lu_gp->lu_gp_id);
}

static ssize_t target_lu_gp_lu_gp_id_store(struct config_item *item,
		const char *page, size_t count)
{
	struct t10_alua_lu_gp *lu_gp = to_lu_gp(item);
	struct config_group *alua_lu_gp_cg = &lu_gp->lu_gp_group;
	unsigned long lu_gp_id;
	int ret;

	ret = kstrtoul(page, 0, &lu_gp_id);
	if (ret < 0) {
		pr_err("kstrtoul() returned %d for"
			" lu_gp_id\n", ret);
		return ret;
	}
	if (lu_gp_id > 0x0000ffff) {
		pr_err("ALUA lu_gp_id: %lu exceeds maximum:"
			" 0x0000ffff\n", lu_gp_id);
		return -EINVAL;
	}

	ret = core_alua_set_lu_gp_id(lu_gp, (u16)lu_gp_id);
	if (ret < 0)
		return -EINVAL;

	pr_debug("Target_Core_ConfigFS: Set ALUA Logical Unit"
		" Group: core/alua/lu_gps/%s to ID: %hu\n",
		config_item_name(&alua_lu_gp_cg->cg_item),
		lu_gp->lu_gp_id);

	return count;
}

static ssize_t target_lu_gp_members_show(struct config_item *item, char *page)
{
	struct t10_alua_lu_gp *lu_gp = to_lu_gp(item);
	struct se_device *dev;
	struct se_hba *hba;
	struct t10_alua_lu_gp_member *lu_gp_mem;
	ssize_t len = 0, cur_len;
	unsigned char buf[LU_GROUP_NAME_BUF];

	memset(buf, 0, LU_GROUP_NAME_BUF);

	spin_lock(&lu_gp->lu_gp_lock);
	list_for_each_entry(lu_gp_mem, &lu_gp->lu_gp_mem_list, lu_gp_mem_list) {
		dev = lu_gp_mem->lu_gp_mem_dev;
		hba = dev->se_hba;

		cur_len = snprintf(buf, LU_GROUP_NAME_BUF, "%s/%s\n",
			config_item_name(&hba->hba_group.cg_item),
			config_item_name(&dev->dev_group.cg_item));
		cur_len++; /* Extra byte for NULL terminator */

		if ((cur_len + len) > PAGE_SIZE) {
			pr_warn("Ran out of lu_gp_show_attr"
				"_members buffer\n");
			break;
		}
		memcpy(page+len, buf, cur_len);
		len += cur_len;
	}
	spin_unlock(&lu_gp->lu_gp_lock);

	return len;
}

CONFIGFS_ATTR(target_lu_gp_, lu_gp_id);
CONFIGFS_ATTR_RO(target_lu_gp_, members);

static struct configfs_attribute *target_core_alua_lu_gp_attrs[] = {
	&target_lu_gp_attr_lu_gp_id,
	&target_lu_gp_attr_members,
	NULL,
};

static void target_core_alua_lu_gp_release(struct config_item *item)
{
	struct t10_alua_lu_gp *lu_gp = container_of(to_config_group(item),
			struct t10_alua_lu_gp, lu_gp_group);

	core_alua_free_lu_gp(lu_gp);
}

static struct configfs_item_operations target_core_alua_lu_gp_ops = {
	.release		= target_core_alua_lu_gp_release,
};

static const struct config_item_type target_core_alua_lu_gp_cit = {
	.ct_item_ops		= &target_core_alua_lu_gp_ops,
	.ct_attrs		= target_core_alua_lu_gp_attrs,
	.ct_owner		= THIS_MODULE,
};

/* End functions for struct config_item_type target_core_alua_lu_gp_cit */

/* Start functions for struct config_item_type target_core_alua_lu_gps_cit */

static struct config_group *target_core_alua_create_lu_gp(
	struct config_group *group,
	const char *name)
{
	struct t10_alua_lu_gp *lu_gp;
	struct config_group *alua_lu_gp_cg = NULL;
	struct config_item *alua_lu_gp_ci = NULL;

	lu_gp = core_alua_allocate_lu_gp(name, 0);
	if (IS_ERR(lu_gp))
		return NULL;

	alua_lu_gp_cg = &lu_gp->lu_gp_group;
	alua_lu_gp_ci = &alua_lu_gp_cg->cg_item;

	config_group_init_type_name(alua_lu_gp_cg, name,
			&target_core_alua_lu_gp_cit);

	pr_debug("Target_Core_ConfigFS: Allocated ALUA Logical Unit"
		" Group: core/alua/lu_gps/%s\n",
		config_item_name(alua_lu_gp_ci));

	return alua_lu_gp_cg;

}

static void target_core_alua_drop_lu_gp(
	struct config_group *group,
	struct config_item *item)
{
	struct t10_alua_lu_gp *lu_gp = container_of(to_config_group(item),
			struct t10_alua_lu_gp, lu_gp_group);

	pr_debug("Target_Core_ConfigFS: Releasing ALUA Logical Unit"
		" Group: core/alua/lu_gps/%s, ID: %hu\n",
		config_item_name(item), lu_gp->lu_gp_id);
	/*
	 * core_alua_free_lu_gp() is called from target_core_alua_lu_gp_ops->release()
	 * -> target_core_alua_lu_gp_release()
	 */
	config_item_put(item);
}

static struct configfs_group_operations target_core_alua_lu_gps_group_ops = {
	.make_group		= &target_core_alua_create_lu_gp,
	.drop_item		= &target_core_alua_drop_lu_gp,
};

static const struct config_item_type target_core_alua_lu_gps_cit = {
	.ct_item_ops		= NULL,
	.ct_group_ops		= &target_core_alua_lu_gps_group_ops,
	.ct_owner		= THIS_MODULE,
};

/* End functions for struct config_item_type target_core_alua_lu_gps_cit */

/* Start functions for struct config_item_type target_core_alua_tg_pt_gp_cit */

static inline struct t10_alua_tg_pt_gp *to_tg_pt_gp(struct config_item *item)
{
	return container_of(to_config_group(item), struct t10_alua_tg_pt_gp,
			tg_pt_gp_group);
}

static ssize_t target_tg_pt_gp_alua_access_state_show(struct config_item *item,
		char *page)
{
	return sprintf(page, "%d\n",
		       to_tg_pt_gp(item)->tg_pt_gp_alua_access_state);
}

static ssize_t target_tg_pt_gp_alua_access_state_store(struct config_item *item,
		const char *page, size_t count)
{
	struct t10_alua_tg_pt_gp *tg_pt_gp = to_tg_pt_gp(item);
	struct se_device *dev = tg_pt_gp->tg_pt_gp_dev;
	unsigned long tmp;
	int new_state, ret;

	if (!tg_pt_gp->tg_pt_gp_valid_id) {
		pr_err("Unable to do implicit ALUA on non valid"
			" tg_pt_gp ID: %hu\n", tg_pt_gp->tg_pt_gp_valid_id);
		return -EINVAL;
	}
	if (!(dev->dev_flags & DF_CONFIGURED)) {
		pr_err("Unable to set alua_access_state while device is"
		       " not configured\n");
		return -ENODEV;
	}

	ret = kstrtoul(page, 0, &tmp);
	if (ret < 0) {
		pr_err("Unable to extract new ALUA access state from"
				" %s\n", page);
		return ret;
	}
	new_state = (int)tmp;

	if (!(tg_pt_gp->tg_pt_gp_alua_access_type & TPGS_IMPLICIT_ALUA)) {
		pr_err("Unable to process implicit configfs ALUA"
			" transition while TPGS_IMPLICIT_ALUA is disabled\n");
		return -EINVAL;
	}
	if (tg_pt_gp->tg_pt_gp_alua_access_type & TPGS_EXPLICIT_ALUA &&
	    new_state == ALUA_ACCESS_STATE_LBA_DEPENDENT) {
		/* LBA DEPENDENT is only allowed with implicit ALUA */
		pr_err("Unable to process implicit configfs ALUA transition"
		       " while explicit ALUA management is enabled\n");
		return -EINVAL;
	}

	ret = core_alua_do_port_transition(tg_pt_gp, dev,
					NULL, NULL, new_state, 0);
	return (!ret) ? count : -EINVAL;
}

static ssize_t target_tg_pt_gp_alua_access_status_show(struct config_item *item,
		char *page)
{
	struct t10_alua_tg_pt_gp *tg_pt_gp = to_tg_pt_gp(item);
	return sprintf(page, "%s\n",
		core_alua_dump_status(tg_pt_gp->tg_pt_gp_alua_access_status));
}

static ssize_t target_tg_pt_gp_alua_access_status_store(
		struct config_item *item, const char *page, size_t count)
{
	struct t10_alua_tg_pt_gp *tg_pt_gp = to_tg_pt_gp(item);
	unsigned long tmp;
	int new_status, ret;

	if (!tg_pt_gp->tg_pt_gp_valid_id) {
		pr_err("Unable to do set ALUA access status on non"
			" valid tg_pt_gp ID: %hu\n",
			tg_pt_gp->tg_pt_gp_valid_id);
		return -EINVAL;
	}

	ret = kstrtoul(page, 0, &tmp);
	if (ret < 0) {
		pr_err("Unable to extract new ALUA access status"
				" from %s\n", page);
		return ret;
	}
	new_status = (int)tmp;

	if ((new_status != ALUA_STATUS_NONE) &&
	    (new_status != ALUA_STATUS_ALTERED_BY_EXPLICIT_STPG) &&
	    (new_status != ALUA_STATUS_ALTERED_BY_IMPLICIT_ALUA)) {
		pr_err("Illegal ALUA access status: 0x%02x\n",
				new_status);
		return -EINVAL;
	}

	tg_pt_gp->tg_pt_gp_alua_access_status = new_status;
	return count;
}

static ssize_t target_tg_pt_gp_alua_access_type_show(struct config_item *item,
		char *page)
{
	return core_alua_show_access_type(to_tg_pt_gp(item), page);
}

static ssize_t target_tg_pt_gp_alua_access_type_store(struct config_item *item,
		const char *page, size_t count)
{
	return core_alua_store_access_type(to_tg_pt_gp(item), page, count);
}

#define ALUA_SUPPORTED_STATE_ATTR(_name, _bit)				\
static ssize_t target_tg_pt_gp_alua_support_##_name##_show(		\
		struct config_item *item, char *p)			\
{									\
	struct t10_alua_tg_pt_gp *t = to_tg_pt_gp(item);		\
	return sprintf(p, "%d\n",					\
		!!(t->tg_pt_gp_alua_supported_states & _bit));		\
}									\
									\
static ssize_t target_tg_pt_gp_alua_support_##_name##_store(		\
		struct config_item *item, const char *p, size_t c)	\
{									\
	struct t10_alua_tg_pt_gp *t = to_tg_pt_gp(item);		\
	unsigned long tmp;						\
	int ret;							\
									\
	if (!t->tg_pt_gp_valid_id) {					\
		pr_err("Unable to do set " #_name " ALUA state on non"	\
		       " valid tg_pt_gp ID: %hu\n",			\
		       t->tg_pt_gp_valid_id);				\
		return -EINVAL;						\
	}								\
									\
	ret = kstrtoul(p, 0, &tmp);					\
	if (ret < 0) {							\
		pr_err("Invalid value '%s', must be '0' or '1'\n", p);	\
		return -EINVAL;						\
	}								\
	if (tmp > 1) {							\
		pr_err("Invalid value '%ld', must be '0' or '1'\n", tmp); \
		return -EINVAL;						\
	}								\
	if (tmp)							\
		t->tg_pt_gp_alua_supported_states |= _bit;		\
	else								\
		t->tg_pt_gp_alua_supported_states &= ~_bit;		\
									\
	return c;							\
}

ALUA_SUPPORTED_STATE_ATTR(transitioning, ALUA_T_SUP);
ALUA_SUPPORTED_STATE_ATTR(offline, ALUA_O_SUP);
ALUA_SUPPORTED_STATE_ATTR(lba_dependent, ALUA_LBD_SUP);
ALUA_SUPPORTED_STATE_ATTR(unavailable, ALUA_U_SUP);
ALUA_SUPPORTED_STATE_ATTR(standby, ALUA_S_SUP);
ALUA_SUPPORTED_STATE_ATTR(active_optimized, ALUA_AO_SUP);
ALUA_SUPPORTED_STATE_ATTR(active_nonoptimized, ALUA_AN_SUP);

static ssize_t target_tg_pt_gp_alua_write_metadata_show(
		struct config_item *item, char *page)
{
	return sprintf(page, "%d\n",
		to_tg_pt_gp(item)->tg_pt_gp_write_metadata);
}

static ssize_t target_tg_pt_gp_alua_write_metadata_store(
		struct config_item *item, const char *page, size_t count)
{
	struct t10_alua_tg_pt_gp *tg_pt_gp = to_tg_pt_gp(item);
	unsigned long tmp;
	int ret;

	ret = kstrtoul(page, 0, &tmp);
	if (ret < 0) {
		pr_err("Unable to extract alua_write_metadata\n");
		return ret;
	}

	if ((tmp != 0) && (tmp != 1)) {
		pr_err("Illegal value for alua_write_metadata:"
			" %lu\n", tmp);
		return -EINVAL;
	}
	tg_pt_gp->tg_pt_gp_write_metadata = (int)tmp;

	return count;
}

static ssize_t target_tg_pt_gp_nonop_delay_msecs_show(struct config_item *item,
		char *page)
{
	return core_alua_show_nonop_delay_msecs(to_tg_pt_gp(item), page);
}

static ssize_t target_tg_pt_gp_nonop_delay_msecs_store(struct config_item *item,
		const char *page, size_t count)
{
	return core_alua_store_nonop_delay_msecs(to_tg_pt_gp(item), page,
			count);
}

static ssize_t target_tg_pt_gp_trans_delay_msecs_show(struct config_item *item,
		char *page)
{
	return core_alua_show_trans_delay_msecs(to_tg_pt_gp(item), page);
}

static ssize_t target_tg_pt_gp_trans_delay_msecs_store(struct config_item *item,
		const char *page, size_t count)
{
	return core_alua_store_trans_delay_msecs(to_tg_pt_gp(item), page,
			count);
}

static ssize_t target_tg_pt_gp_implicit_trans_secs_show(
		struct config_item *item, char *page)
{
	return core_alua_show_implicit_trans_secs(to_tg_pt_gp(item), page);
}

static ssize_t target_tg_pt_gp_implicit_trans_secs_store(
		struct config_item *item, const char *page, size_t count)
{
	return core_alua_store_implicit_trans_secs(to_tg_pt_gp(item), page,
			count);
}

static ssize_t target_tg_pt_gp_preferred_show(struct config_item *item,
		char *page)
{
	return core_alua_show_preferred_bit(to_tg_pt_gp(item), page);
}

static ssize_t target_tg_pt_gp_preferred_store(struct config_item *item,
		const char *page, size_t count)
{
	return core_alua_store_preferred_bit(to_tg_pt_gp(item), page, count);
}

static ssize_t target_tg_pt_gp_tg_pt_gp_id_show(struct config_item *item,
		char *page)
{
	struct t10_alua_tg_pt_gp *tg_pt_gp = to_tg_pt_gp(item);

	if (!tg_pt_gp->tg_pt_gp_valid_id)
		return 0;
	return sprintf(page, "%hu\n", tg_pt_gp->tg_pt_gp_id);
}

static ssize_t target_tg_pt_gp_tg_pt_gp_id_store(struct config_item *item,
		const char *page, size_t count)
{
	struct t10_alua_tg_pt_gp *tg_pt_gp = to_tg_pt_gp(item);
	struct config_group *alua_tg_pt_gp_cg = &tg_pt_gp->tg_pt_gp_group;
	unsigned long tg_pt_gp_id;
	int ret;

	ret = kstrtoul(page, 0, &tg_pt_gp_id);
	if (ret < 0) {
		pr_err("ALUA tg_pt_gp_id: invalid value '%s' for tg_pt_gp_id\n",
		       page);
		return ret;
	}
	if (tg_pt_gp_id > 0x0000ffff) {
		pr_err("ALUA tg_pt_gp_id: %lu exceeds maximum: 0x0000ffff\n",
		       tg_pt_gp_id);
		return -EINVAL;
	}

	ret = core_alua_set_tg_pt_gp_id(tg_pt_gp, (u16)tg_pt_gp_id);
	if (ret < 0)
		return -EINVAL;

	pr_debug("Target_Core_ConfigFS: Set ALUA Target Port Group: "
		"core/alua/tg_pt_gps/%s to ID: %hu\n",
		config_item_name(&alua_tg_pt_gp_cg->cg_item),
		tg_pt_gp->tg_pt_gp_id);

	return count;
}

static ssize_t target_tg_pt_gp_members_show(struct config_item *item,
		char *page)
{
	struct t10_alua_tg_pt_gp *tg_pt_gp = to_tg_pt_gp(item);
	struct se_lun *lun;
	ssize_t len = 0, cur_len;
	unsigned char buf[TG_PT_GROUP_NAME_BUF];

	memset(buf, 0, TG_PT_GROUP_NAME_BUF);

	spin_lock(&tg_pt_gp->tg_pt_gp_lock);
	list_for_each_entry(lun, &tg_pt_gp->tg_pt_gp_lun_list,
			lun_tg_pt_gp_link) {
		struct se_portal_group *tpg = lun->lun_tpg;

		cur_len = snprintf(buf, TG_PT_GROUP_NAME_BUF, "%s/%s/tpgt_%hu"
			"/%s\n", tpg->se_tpg_tfo->get_fabric_name(),
			tpg->se_tpg_tfo->tpg_get_wwn(tpg),
			tpg->se_tpg_tfo->tpg_get_tag(tpg),
			config_item_name(&lun->lun_group.cg_item));
		cur_len++; /* Extra byte for NULL terminator */

		if ((cur_len + len) > PAGE_SIZE) {
			pr_warn("Ran out of lu_gp_show_attr"
				"_members buffer\n");
			break;
		}
		memcpy(page+len, buf, cur_len);
		len += cur_len;
	}
	spin_unlock(&tg_pt_gp->tg_pt_gp_lock);

	return len;
}

CONFIGFS_ATTR(target_tg_pt_gp_, alua_access_state);
CONFIGFS_ATTR(target_tg_pt_gp_, alua_access_status);
CONFIGFS_ATTR(target_tg_pt_gp_, alua_access_type);
CONFIGFS_ATTR(target_tg_pt_gp_, alua_support_transitioning);
CONFIGFS_ATTR(target_tg_pt_gp_, alua_support_offline);
CONFIGFS_ATTR(target_tg_pt_gp_, alua_support_lba_dependent);
CONFIGFS_ATTR(target_tg_pt_gp_, alua_support_unavailable);
CONFIGFS_ATTR(target_tg_pt_gp_, alua_support_standby);
CONFIGFS_ATTR(target_tg_pt_gp_, alua_support_active_optimized);
CONFIGFS_ATTR(target_tg_pt_gp_, alua_support_active_nonoptimized);
CONFIGFS_ATTR(target_tg_pt_gp_, alua_write_metadata);
CONFIGFS_ATTR(target_tg_pt_gp_, nonop_delay_msecs);
CONFIGFS_ATTR(target_tg_pt_gp_, trans_delay_msecs);
CONFIGFS_ATTR(target_tg_pt_gp_, implicit_trans_secs);
CONFIGFS_ATTR(target_tg_pt_gp_, preferred);
CONFIGFS_ATTR(target_tg_pt_gp_, tg_pt_gp_id);
CONFIGFS_ATTR_RO(target_tg_pt_gp_, members);

static struct configfs_attribute *target_core_alua_tg_pt_gp_attrs[] = {
	&target_tg_pt_gp_attr_alua_access_state,
	&target_tg_pt_gp_attr_alua_access_status,
	&target_tg_pt_gp_attr_alua_access_type,
	&target_tg_pt_gp_attr_alua_support_transitioning,
	&target_tg_pt_gp_attr_alua_support_offline,
	&target_tg_pt_gp_attr_alua_support_lba_dependent,
	&target_tg_pt_gp_attr_alua_support_unavailable,
	&target_tg_pt_gp_attr_alua_support_standby,
	&target_tg_pt_gp_attr_alua_support_active_nonoptimized,
	&target_tg_pt_gp_attr_alua_support_active_optimized,
	&target_tg_pt_gp_attr_alua_write_metadata,
	&target_tg_pt_gp_attr_nonop_delay_msecs,
	&target_tg_pt_gp_attr_trans_delay_msecs,
	&target_tg_pt_gp_attr_implicit_trans_secs,
	&target_tg_pt_gp_attr_preferred,
	&target_tg_pt_gp_attr_tg_pt_gp_id,
	&target_tg_pt_gp_attr_members,
	NULL,
};

static void target_core_alua_tg_pt_gp_release(struct config_item *item)
{
	struct t10_alua_tg_pt_gp *tg_pt_gp = container_of(to_config_group(item),
			struct t10_alua_tg_pt_gp, tg_pt_gp_group);

	core_alua_free_tg_pt_gp(tg_pt_gp);
}

static struct configfs_item_operations target_core_alua_tg_pt_gp_ops = {
	.release		= target_core_alua_tg_pt_gp_release,
};

static const struct config_item_type target_core_alua_tg_pt_gp_cit = {
	.ct_item_ops		= &target_core_alua_tg_pt_gp_ops,
	.ct_attrs		= target_core_alua_tg_pt_gp_attrs,
	.ct_owner		= THIS_MODULE,
};

/* End functions for struct config_item_type target_core_alua_tg_pt_gp_cit */

/* Start functions for struct config_item_type tb_alua_tg_pt_gps_cit */

static struct config_group *target_core_alua_create_tg_pt_gp(
	struct config_group *group,
	const char *name)
{
	struct t10_alua *alua = container_of(group, struct t10_alua,
					alua_tg_pt_gps_group);
	struct t10_alua_tg_pt_gp *tg_pt_gp;
	struct config_group *alua_tg_pt_gp_cg = NULL;
	struct config_item *alua_tg_pt_gp_ci = NULL;

	tg_pt_gp = core_alua_allocate_tg_pt_gp(alua->t10_dev, name, 0);
	if (!tg_pt_gp)
		return NULL;

	alua_tg_pt_gp_cg = &tg_pt_gp->tg_pt_gp_group;
	alua_tg_pt_gp_ci = &alua_tg_pt_gp_cg->cg_item;

	config_group_init_type_name(alua_tg_pt_gp_cg, name,
			&target_core_alua_tg_pt_gp_cit);

	pr_debug("Target_Core_ConfigFS: Allocated ALUA Target Port"
		" Group: alua/tg_pt_gps/%s\n",
		config_item_name(alua_tg_pt_gp_ci));

	return alua_tg_pt_gp_cg;
}

static void target_core_alua_drop_tg_pt_gp(
	struct config_group *group,
	struct config_item *item)
{
	struct t10_alua_tg_pt_gp *tg_pt_gp = container_of(to_config_group(item),
			struct t10_alua_tg_pt_gp, tg_pt_gp_group);

	pr_debug("Target_Core_ConfigFS: Releasing ALUA Target Port"
		" Group: alua/tg_pt_gps/%s, ID: %hu\n",
		config_item_name(item), tg_pt_gp->tg_pt_gp_id);
	/*
	 * core_alua_free_tg_pt_gp() is called from target_core_alua_tg_pt_gp_ops->release()
	 * -> target_core_alua_tg_pt_gp_release().
	 */
	config_item_put(item);
}

static struct configfs_group_operations target_core_alua_tg_pt_gps_group_ops = {
	.make_group		= &target_core_alua_create_tg_pt_gp,
	.drop_item		= &target_core_alua_drop_tg_pt_gp,
};

TB_CIT_SETUP(dev_alua_tg_pt_gps, NULL, &target_core_alua_tg_pt_gps_group_ops, NULL);

/* End functions for struct config_item_type tb_alua_tg_pt_gps_cit */

/* Start functions for struct config_item_type target_core_alua_cit */

/*
 * target_core_alua_cit is a ConfigFS group that lives under
 * /sys/kernel/config/target/core/alua.  There are default groups
 * core/alua/lu_gps and core/alua/tg_pt_gps that are attached to
 * target_core_alua_cit in target_core_init_configfs() below.
 */
static const struct config_item_type target_core_alua_cit = {
	.ct_item_ops		= NULL,
	.ct_attrs		= NULL,
	.ct_owner		= THIS_MODULE,
};

/* End functions for struct config_item_type target_core_alua_cit */

/* Start functions for struct config_item_type tb_dev_stat_cit */

static struct config_group *target_core_stat_mkdir(
	struct config_group *group,
	const char *name)
{
	return ERR_PTR(-ENOSYS);
}

static void target_core_stat_rmdir(
	struct config_group *group,
	struct config_item *item)
{
	return;
}

static struct configfs_group_operations target_core_stat_group_ops = {
	.make_group		= &target_core_stat_mkdir,
	.drop_item		= &target_core_stat_rmdir,
};

TB_CIT_SETUP(dev_stat, NULL, &target_core_stat_group_ops, NULL);

/* End functions for struct config_item_type tb_dev_stat_cit */

/* Start functions for struct config_item_type target_core_hba_cit */

static struct config_group *target_core_make_subdev(
	struct config_group *group,
	const char *name)
{
	struct t10_alua_tg_pt_gp *tg_pt_gp;
	struct config_item *hba_ci = &group->cg_item;
	struct se_hba *hba = item_to_hba(hba_ci);
	struct target_backend *tb = hba->backend;
	struct se_device *dev;
	int errno = -ENOMEM, ret;

	ret = mutex_lock_interruptible(&hba->hba_access_mutex);
	if (ret)
		return ERR_PTR(ret);

	dev = target_alloc_device(hba, name);
	if (!dev)
		goto out_unlock;

	config_group_init_type_name(&dev->dev_group, name, &tb->tb_dev_cit);

	config_group_init_type_name(&dev->dev_action_group, "action",
			&tb->tb_dev_action_cit);
	configfs_add_default_group(&dev->dev_action_group, &dev->dev_group);

	config_group_init_type_name(&dev->dev_attrib.da_group, "attrib",
			&tb->tb_dev_attrib_cit);
	configfs_add_default_group(&dev->dev_attrib.da_group, &dev->dev_group);

	config_group_init_type_name(&dev->dev_pr_group, "pr",
			&tb->tb_dev_pr_cit);
	configfs_add_default_group(&dev->dev_pr_group, &dev->dev_group);

	config_group_init_type_name(&dev->t10_wwn.t10_wwn_group, "wwn",
			&tb->tb_dev_wwn_cit);
	configfs_add_default_group(&dev->t10_wwn.t10_wwn_group,
			&dev->dev_group);

	config_group_init_type_name(&dev->t10_alua.alua_tg_pt_gps_group,
			"alua", &tb->tb_dev_alua_tg_pt_gps_cit);
	configfs_add_default_group(&dev->t10_alua.alua_tg_pt_gps_group,
			&dev->dev_group);

	config_group_init_type_name(&dev->dev_stat_grps.stat_group,
			"statistics", &tb->tb_dev_stat_cit);
	configfs_add_default_group(&dev->dev_stat_grps.stat_group,
			&dev->dev_group);

	/*
	 * Add core/$HBA/$DEV/alua/default_tg_pt_gp
	 */
	tg_pt_gp = core_alua_allocate_tg_pt_gp(dev, "default_tg_pt_gp", 1);
	if (!tg_pt_gp)
		goto out_free_device;
	dev->t10_alua.default_tg_pt_gp = tg_pt_gp;

	config_group_init_type_name(&tg_pt_gp->tg_pt_gp_group,
			"default_tg_pt_gp", &target_core_alua_tg_pt_gp_cit);
	configfs_add_default_group(&tg_pt_gp->tg_pt_gp_group,
			&dev->t10_alua.alua_tg_pt_gps_group);

	/*
	 * Add core/$HBA/$DEV/statistics/ default groups
	 */
	target_stat_setup_dev_default_groups(dev);

	mutex_unlock(&hba->hba_access_mutex);
	return &dev->dev_group;

out_free_device:
	target_free_device(dev);
out_unlock:
	mutex_unlock(&hba->hba_access_mutex);
	return ERR_PTR(errno);
}

static void target_core_drop_subdev(
	struct config_group *group,
	struct config_item *item)
{
	struct config_group *dev_cg = to_config_group(item);
	struct se_device *dev =
		container_of(dev_cg, struct se_device, dev_group);
	struct se_hba *hba;

	hba = item_to_hba(&dev->se_hba->hba_group.cg_item);

	mutex_lock(&hba->hba_access_mutex);

	configfs_remove_default_groups(&dev->dev_stat_grps.stat_group);
	configfs_remove_default_groups(&dev->t10_alua.alua_tg_pt_gps_group);

	/*
	 * core_alua_free_tg_pt_gp() is called from ->default_tg_pt_gp
	 * directly from target_core_alua_tg_pt_gp_release().
	 */
	dev->t10_alua.default_tg_pt_gp = NULL;

	configfs_remove_default_groups(dev_cg);

	/*
	 * se_dev is released from target_core_dev_item_ops->release()
	 */
	config_item_put(item);
	mutex_unlock(&hba->hba_access_mutex);
}

static struct configfs_group_operations target_core_hba_group_ops = {
	.make_group		= target_core_make_subdev,
	.drop_item		= target_core_drop_subdev,
};


static inline struct se_hba *to_hba(struct config_item *item)
{
	return container_of(to_config_group(item), struct se_hba, hba_group);
}

static ssize_t target_hba_info_show(struct config_item *item, char *page)
{
	struct se_hba *hba = to_hba(item);

	return sprintf(page, "HBA Index: %d plugin: %s version: %s\n",
			hba->hba_id, hba->backend->ops->name,
			TARGET_CORE_VERSION);
}

static ssize_t target_hba_mode_show(struct config_item *item, char *page)
{
	struct se_hba *hba = to_hba(item);
	int hba_mode = 0;

	if (hba->hba_flags & HBA_FLAGS_PSCSI_MODE)
		hba_mode = 1;

	return sprintf(page, "%d\n", hba_mode);
}

static ssize_t target_hba_mode_store(struct config_item *item,
		const char *page, size_t count)
{
	struct se_hba *hba = to_hba(item);
	unsigned long mode_flag;
	int ret;

	if (hba->backend->ops->pmode_enable_hba == NULL)
		return -EINVAL;

	ret = kstrtoul(page, 0, &mode_flag);
	if (ret < 0) {
		pr_err("Unable to extract hba mode flag: %d\n", ret);
		return ret;
	}

	if (hba->dev_count) {
		pr_err("Unable to set hba_mode with active devices\n");
		return -EINVAL;
	}

	ret = hba->backend->ops->pmode_enable_hba(hba, mode_flag);
	if (ret < 0)
		return -EINVAL;
	if (ret > 0)
		hba->hba_flags |= HBA_FLAGS_PSCSI_MODE;
	else if (ret == 0)
		hba->hba_flags &= ~HBA_FLAGS_PSCSI_MODE;

	return count;
}

CONFIGFS_ATTR_RO(target_, hba_info);
CONFIGFS_ATTR(target_, hba_mode);

static void target_core_hba_release(struct config_item *item)
{
	struct se_hba *hba = container_of(to_config_group(item),
				struct se_hba, hba_group);
	core_delete_hba(hba);
}

static struct configfs_attribute *target_core_hba_attrs[] = {
	&target_attr_hba_info,
	&target_attr_hba_mode,
	NULL,
};

static struct configfs_item_operations target_core_hba_item_ops = {
	.release		= target_core_hba_release,
};

static const struct config_item_type target_core_hba_cit = {
	.ct_item_ops		= &target_core_hba_item_ops,
	.ct_group_ops		= &target_core_hba_group_ops,
	.ct_attrs		= target_core_hba_attrs,
	.ct_owner		= THIS_MODULE,
};

static struct config_group *target_core_call_addhbatotarget(
	struct config_group *group,
	const char *name)
{
	char *se_plugin_str, *str, *str2;
	struct se_hba *hba;
	char buf[TARGET_CORE_NAME_MAX_LEN];
	unsigned long plugin_dep_id = 0;
	int ret;

	memset(buf, 0, TARGET_CORE_NAME_MAX_LEN);
	if (strlen(name) >= TARGET_CORE_NAME_MAX_LEN) {
		pr_err("Passed *name strlen(): %d exceeds"
			" TARGET_CORE_NAME_MAX_LEN: %d\n", (int)strlen(name),
			TARGET_CORE_NAME_MAX_LEN);
		return ERR_PTR(-ENAMETOOLONG);
	}
	snprintf(buf, TARGET_CORE_NAME_MAX_LEN, "%s", name);

	str = strstr(buf, "_");
	if (!str) {
		pr_err("Unable to locate \"_\" for $SUBSYSTEM_PLUGIN_$HOST_ID\n");
		return ERR_PTR(-EINVAL);
	}
	se_plugin_str = buf;
	/*
	 * Special case for subsystem plugins that have "_" in their names.
	 * Namely rd_direct and rd_mcp..
	 */
	str2 = strstr(str+1, "_");
	if (str2) {
		*str2 = '\0'; /* Terminate for *se_plugin_str */
		str2++; /* Skip to start of plugin dependent ID */
		str = str2;
	} else {
		*str = '\0'; /* Terminate for *se_plugin_str */
		str++; /* Skip to start of plugin dependent ID */
	}

	ret = kstrtoul(str, 0, &plugin_dep_id);
	if (ret < 0) {
		pr_err("kstrtoul() returned %d for"
				" plugin_dep_id\n", ret);
		return ERR_PTR(ret);
	}
	/*
	 * Load up TCM subsystem plugins if they have not already been loaded.
	 */
	transport_subsystem_check_init();

	hba = core_alloc_hba(se_plugin_str, plugin_dep_id, 0);
	if (IS_ERR(hba))
		return ERR_CAST(hba);

	config_group_init_type_name(&hba->hba_group, name,
			&target_core_hba_cit);

	return &hba->hba_group;
}

static void target_core_call_delhbafromtarget(
	struct config_group *group,
	struct config_item *item)
{
	/*
	 * core_delete_hba() is called from target_core_hba_item_ops->release()
	 * -> target_core_hba_release()
	 */
	config_item_put(item);
}

static struct configfs_group_operations target_core_group_ops = {
	.make_group	= target_core_call_addhbatotarget,
	.drop_item	= target_core_call_delhbafromtarget,
};

static const struct config_item_type target_core_cit = {
	.ct_item_ops	= NULL,
	.ct_group_ops	= &target_core_group_ops,
	.ct_attrs	= NULL,
	.ct_owner	= THIS_MODULE,
};

/* Stop functions for struct config_item_type target_core_hba_cit */

void target_setup_backend_cits(struct target_backend *tb)
{
	target_core_setup_dev_cit(tb);
	target_core_setup_dev_action_cit(tb);
	target_core_setup_dev_attrib_cit(tb);
	target_core_setup_dev_pr_cit(tb);
	target_core_setup_dev_wwn_cit(tb);
	target_core_setup_dev_alua_tg_pt_gps_cit(tb);
	target_core_setup_dev_stat_cit(tb);
}

static void target_init_dbroot(void)
{
	struct file *fp;

	snprintf(db_root_stage, DB_ROOT_LEN, DB_ROOT_PREFERRED);
	fp = filp_open(db_root_stage, O_RDONLY, 0);
	if (IS_ERR(fp)) {
		pr_err("db_root: cannot open: %s\n", db_root_stage);
		return;
	}
	if (!S_ISDIR(file_inode(fp)->i_mode)) {
		filp_close(fp, NULL);
		pr_err("db_root: not a valid directory: %s\n", db_root_stage);
		return;
	}
	filp_close(fp, NULL);

	strncpy(db_root, db_root_stage, DB_ROOT_LEN);
	pr_debug("Target_Core_ConfigFS: db_root set to %s\n", db_root);
}

static int __init target_core_init_configfs(void)
{
	struct configfs_subsystem *subsys = &target_core_fabrics;
	struct t10_alua_lu_gp *lu_gp;
	int ret;

	pr_debug("TARGET_CORE[0]: Loading Generic Kernel Storage"
		" Engine: %s on %s/%s on "UTS_RELEASE"\n",
		TARGET_CORE_VERSION, utsname()->sysname, utsname()->machine);

	config_group_init(&subsys->su_group);
	mutex_init(&subsys->su_mutex);

	ret = init_se_kmem_caches();
	if (ret < 0)
		return ret;
	/*
	 * Create $CONFIGFS/target/core default group for HBA <-> Storage Object
	 * and ALUA Logical Unit Group and Target Port Group infrastructure.
	 */
	config_group_init_type_name(&target_core_hbagroup, "core",
			&target_core_cit);
	configfs_add_default_group(&target_core_hbagroup, &subsys->su_group);

	/*
	 * Create ALUA infrastructure under /sys/kernel/config/target/core/alua/
	 */
	config_group_init_type_name(&alua_group, "alua", &target_core_alua_cit);
	configfs_add_default_group(&alua_group, &target_core_hbagroup);

	/*
	 * Add ALUA Logical Unit Group and Target Port Group ConfigFS
	 * groups under /sys/kernel/config/target/core/alua/
	 */
	config_group_init_type_name(&alua_lu_gps_group, "lu_gps",
			&target_core_alua_lu_gps_cit);
	configfs_add_default_group(&alua_lu_gps_group, &alua_group);

	/*
	 * Add core/alua/lu_gps/default_lu_gp
	 */
	lu_gp = core_alua_allocate_lu_gp("default_lu_gp", 1);
	if (IS_ERR(lu_gp)) {
		ret = -ENOMEM;
		goto out_global;
	}

	config_group_init_type_name(&lu_gp->lu_gp_group, "default_lu_gp",
				&target_core_alua_lu_gp_cit);
	configfs_add_default_group(&lu_gp->lu_gp_group, &alua_lu_gps_group);

	default_lu_gp = lu_gp;

	/*
	 * Register the target_core_mod subsystem with configfs.
	 */
	ret = configfs_register_subsystem(subsys);
	if (ret < 0) {
		pr_err("Error %d while registering subsystem %s\n",
			ret, subsys->su_group.cg_item.ci_namebuf);
		goto out_global;
	}
	pr_debug("TARGET_CORE[0]: Initialized ConfigFS Fabric"
		" Infrastructure: "TARGET_CORE_VERSION" on %s/%s"
		" on "UTS_RELEASE"\n", utsname()->sysname, utsname()->machine);
	/*
	 * Register built-in RAMDISK subsystem logic for virtual LUN 0
	 */
	ret = rd_module_init();
	if (ret < 0)
		goto out;

	ret = core_dev_setup_virtual_lun0();
	if (ret < 0)
		goto out;

	ret = target_xcopy_setup_pt();
	if (ret < 0)
		goto out;

	target_init_dbroot();

	return 0;

out:
	configfs_unregister_subsystem(subsys);
	core_dev_release_virtual_lun0();
	rd_module_exit();
out_global:
	if (default_lu_gp) {
		core_alua_free_lu_gp(default_lu_gp);
		default_lu_gp = NULL;
	}
	release_se_kmem_caches();
	return ret;
}

static void __exit target_core_exit_configfs(void)
{
	configfs_remove_default_groups(&alua_lu_gps_group);
	configfs_remove_default_groups(&alua_group);
	configfs_remove_default_groups(&target_core_hbagroup);

	/*
	 * We expect subsys->su_group.default_groups to be released
	 * by configfs subsystem provider logic..
	 */
	configfs_unregister_subsystem(&target_core_fabrics);

	core_alua_free_lu_gp(default_lu_gp);
	default_lu_gp = NULL;

	pr_debug("TARGET_CORE[0]: Released ConfigFS Fabric"
			" Infrastructure\n");

	core_dev_release_virtual_lun0();
	rd_module_exit();
	target_xcopy_release_pt();
	release_se_kmem_caches();
}

MODULE_DESCRIPTION("Target_Core_Mod/ConfigFS");
MODULE_AUTHOR("nab@Linux-iSCSI.org");
MODULE_LICENSE("GPL");

module_init(target_core_init_configfs);
module_exit(target_core_exit_configfs);

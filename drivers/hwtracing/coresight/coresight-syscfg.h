/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Coresight system configuration driver.
 */

#ifndef CORESIGHT_SYSCFG_H
#define CORESIGHT_SYSCFG_H

#include <linux/configfs.h>
#include <linux/coresight.h>
#include <linux/device.h>

#include "coresight-config.h"

/**
 * System configuration manager device.
 *
 * Contains lists of the loaded configurations and features, plus a list of CoreSight devices
 * registered with the system as supporting configuration management.
 *
 * Need a device to 'own' some coresight system wide sysfs entries in
 * perf events, configfs etc.
 *
 * @dev:		The device.
 * @csdev_desc_list:	List of coresight devices registered with the configuration manager.
 * @feat_desc_list:	List of feature descriptors to load into registered devices.
 * @config_desc_list:	List of system configuration descriptors to load into registered devices.
 * @load_order_list:    Ordered list of owners for dynamically loaded configurations.
 * @sys_active_cnt:	Total number of active config descriptor references.
 * @cfgfs_subsys:	configfs subsystem used to manage configurations.
 * @sysfs_active_config:Active config hash used if CoreSight controlled from sysfs.
 * @sysfs_active_preset:Active preset index used if CoreSight controlled from sysfs.
 */
struct cscfg_manager {
	struct device dev;
	struct list_head csdev_desc_list;
	struct list_head feat_desc_list;
	struct list_head config_desc_list;
	struct list_head load_order_list;
	atomic_t sys_active_cnt;
	struct configfs_subsystem cfgfs_subsys;
	u32 sysfs_active_config;
	int sysfs_active_preset;
};

/* get reference to dev in cscfg_manager */
struct device *cscfg_device(void);

/**
 * List entry for Coresight devices that are registered as supporting complex
 * config operations.
 *
 * @csdev:	 The registered device.
 * @match_flags: The matching type information for adding features.
 * @ops:	 Operations supported by the registered device.
 * @item:	 list entry.
 */
struct cscfg_registered_csdev {
	struct coresight_device *csdev;
	u32 match_flags;
	struct cscfg_csdev_feat_ops ops;
	struct list_head item;
};

/* owner types for loading and unloading of config and feature sets */
enum cscfg_load_owner_type {
	CSCFG_OWNER_PRELOAD,
	CSCFG_OWNER_MODULE,
};

/**
 * Load item - item to add to the load order list allowing dynamic load and
 *             unload of configurations and features. Caller loading a config
 *	       set provides a context handle for unload. API ensures that
 *	       items unloaded strictly in reverse order from load to ensure
 *	       dependencies are respected.
 *
 * @item:		list entry for load order list.
 * @type:		type of owner - allows interpretation of owner_handle.
 * @owner_handle:	load context - handle for owner of loaded configs.
 */
struct cscfg_load_owner_info {
	struct list_head item;
	int type;
	void *owner_handle;
};

/* internal core operations for cscfg */
int __init cscfg_init(void);
void cscfg_exit(void);
int cscfg_preload(void *owner_handle);
const struct cscfg_feature_desc *cscfg_get_named_feat_desc(const char *name);
int cscfg_update_feat_param_val(struct cscfg_feature_desc *feat_desc,
				int param_idx, u64 value);
int cscfg_config_sysfs_activate(struct cscfg_config_desc *cfg_desc, bool activate);
void cscfg_config_sysfs_set_preset(int preset);

/* syscfg manager external API */
int cscfg_load_config_sets(struct cscfg_config_desc **cfg_descs,
			   struct cscfg_feature_desc **feat_descs,
			   struct cscfg_load_owner_info *owner_info);
int cscfg_unload_config_sets(struct cscfg_load_owner_info *owner_info);
int cscfg_register_csdev(struct coresight_device *csdev, u32 match_flags,
			 struct cscfg_csdev_feat_ops *ops);
void cscfg_unregister_csdev(struct coresight_device *csdev);
int cscfg_activate_config(unsigned long cfg_hash);
void cscfg_deactivate_config(unsigned long cfg_hash);
void cscfg_csdev_reset_feats(struct coresight_device *csdev);
int cscfg_csdev_enable_active_config(struct coresight_device *csdev,
				     unsigned long cfg_hash, int preset);
void cscfg_csdev_disable_active_config(struct coresight_device *csdev);
void cscfg_config_sysfs_get_active_cfg(unsigned long *cfg_hash, int *preset);

#endif /* CORESIGHT_SYSCFG_H */

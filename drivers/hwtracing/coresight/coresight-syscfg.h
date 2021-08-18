/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Coresight system configuration driver.
 */

#ifndef CORESIGHT_SYSCFG_H
#define CORESIGHT_SYSCFG_H

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
 * @sys_active_cnt:	Total number of active config descriptor references.
 */
struct cscfg_manager {
	struct device dev;
	struct list_head csdev_desc_list;
	struct list_head feat_desc_list;
	struct list_head config_desc_list;
	atomic_t sys_active_cnt;
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

/* internal core operations for cscfg */
int __init cscfg_init(void);
void cscfg_exit(void);
int cscfg_preload(void);

/* syscfg manager external API */
int cscfg_load_config_sets(struct cscfg_config_desc **cfg_descs,
			   struct cscfg_feature_desc **feat_descs);
int cscfg_register_csdev(struct coresight_device *csdev, u32 match_flags,
			 struct cscfg_csdev_feat_ops *ops);
void cscfg_unregister_csdev(struct coresight_device *csdev);
int cscfg_activate_config(unsigned long cfg_hash);
void cscfg_deactivate_config(unsigned long cfg_hash);
void cscfg_csdev_reset_feats(struct coresight_device *csdev);
int cscfg_csdev_enable_active_config(struct coresight_device *csdev,
				     unsigned long cfg_hash, int preset);
void cscfg_csdev_disable_active_config(struct coresight_device *csdev);

#endif /* CORESIGHT_SYSCFG_H */

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
 */
struct cscfg_manager {
	struct device dev;
	struct list_head csdev_desc_list;
	struct list_head feat_desc_list;
	struct list_head config_desc_list;
};

/* get reference to dev in cscfg_manager */
struct device *cscfg_device(void);

/* internal core operations for cscfg */
int __init cscfg_init(void);
void cscfg_exit(void);

/* syscfg manager external API */
int cscfg_load_config_sets(struct cscfg_config_desc **cfg_descs,
			   struct cscfg_feature_desc **feat_descs);

#endif /* CORESIGHT_SYSCFG_H */

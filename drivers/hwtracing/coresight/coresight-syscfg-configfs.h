/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Coresight system configuration driver - support for configfs.
 */

#ifndef CORESIGHT_SYSCFG_CONFIGFS_H
#define CORESIGHT_SYSCFG_CONFIGFS_H

#include <linux/configfs.h>
#include "coresight-syscfg.h"

#define CSCFG_FS_SUBSYS_NAME "cs-syscfg"

/* container for configuration view */
struct cscfg_fs_config {
	struct cscfg_config_desc *config_desc;
	struct config_group group;
	bool active;
	int preset;
};

/* container for feature view */
struct cscfg_fs_feature {
	struct cscfg_feature_desc *feat_desc;
	struct config_group group;
};

/* container for parameter view */
struct cscfg_fs_param {
	int param_idx;
	struct cscfg_feature_desc *feat_desc;
	struct config_group group;
};

/* container for preset view */
struct cscfg_fs_preset {
	int preset_num;
	struct cscfg_config_desc *config_desc;
	struct config_group group;
};

int cscfg_configfs_init(struct cscfg_manager *cscfg_mgr);
void cscfg_configfs_release(struct cscfg_manager *cscfg_mgr);
int cscfg_configfs_add_config(struct cscfg_config_desc *config_desc);
int cscfg_configfs_add_feature(struct cscfg_feature_desc *feat_desc);
void cscfg_configfs_del_config(struct cscfg_config_desc *config_desc);
void cscfg_configfs_del_feature(struct cscfg_feature_desc *feat_desc);

#endif /* CORESIGHT_SYSCFG_CONFIGFS_H */

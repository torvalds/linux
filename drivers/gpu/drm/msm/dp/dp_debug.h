/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2017-2020, The Linux Foundation. All rights reserved.
 */

#ifndef _DP_DEBUG_H_
#define _DP_DEBUG_H_

#include "dp_panel.h"
#include "dp_link.h"

#if defined(CONFIG_DEBUG_FS)

/**
 * dp_debug_get() - configure and get the DisplayPlot debug module data
 *
 * @dev: device instance of the caller
 * @panel: instance of panel module
 * @link: instance of link module
 * @connector: double pointer to display connector
 * @root: connector's debugfs root
 * @is_edp: set for eDP connectors / panels
 * return: pointer to allocated debug module data
 *
 * This function sets up the debug module and provides a way
 * for debugfs input to be communicated with existing modules
 */
int dp_debug_init(struct device *dev, struct dp_panel *panel,
		  struct dp_link *link,
		  struct drm_connector *connector,
		  struct dentry *root,
		  bool is_edp);

#else

static inline
int dp_debug_init(struct device *dev, struct dp_panel *panel,
		  struct dp_link *link,
		  struct drm_connector *connector,
		  struct dentry *root,
		  bool is_edp)
{
	return -EINVAL;
}

#endif /* defined(CONFIG_DEBUG_FS) */

#endif /* _DP_DEBUG_H_ */

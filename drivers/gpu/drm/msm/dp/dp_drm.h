/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2017-2020, The Linux Foundation. All rights reserved.
 */

#ifndef _DP_DRM_H_
#define _DP_DRM_H_

#include <linux/types.h>
#include <drm/drm_bridge.h>

#include "msm_drv.h"
#include "dp_display.h"

struct msm_dp_bridge {
	struct drm_bridge bridge;
	struct msm_dp *dp_display;
};

#define to_dp_bridge(x)     container_of((x), struct msm_dp_bridge, bridge)

struct drm_connector *dp_drm_connector_init(struct msm_dp *dp_display, struct drm_encoder *encoder);
struct drm_bridge *dp_bridge_init(struct msm_dp *dp_display, struct drm_device *dev,
			struct drm_encoder *encoder);

void dp_bridge_enable(struct drm_bridge *drm_bridge);
void dp_bridge_disable(struct drm_bridge *drm_bridge);
void dp_bridge_post_disable(struct drm_bridge *drm_bridge);
enum drm_mode_status dp_bridge_mode_valid(struct drm_bridge *bridge,
					  const struct drm_display_info *info,
					  const struct drm_display_mode *mode);
void dp_bridge_mode_set(struct drm_bridge *drm_bridge,
			const struct drm_display_mode *mode,
			const struct drm_display_mode *adjusted_mode);

#endif /* _DP_DRM_H_ */

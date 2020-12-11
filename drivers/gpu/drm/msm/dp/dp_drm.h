/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2017-2020, The Linux Foundation. All rights reserved.
 */

#ifndef _DP_DRM_H_
#define _DP_DRM_H_

#include <linux/types.h>
#include <drm/drm_crtc.h>
#include <drm/drm_crtc_helper.h>

#include "msm_drv.h"
#include "dp_display.h"

struct drm_connector *dp_drm_connector_init(struct msm_dp *dp_display);

#endif /* _DP_DRM_H_ */

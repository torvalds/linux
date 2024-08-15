/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020 Unisoc Inc.
 */

#ifndef _SPRD_DRM_H_
#define _SPRD_DRM_H_

#include <drm/drm_atomic.h>
#include <drm/drm_print.h>

struct sprd_drm {
	struct drm_device drm;
};

extern struct platform_driver sprd_dpu_driver;
extern struct platform_driver sprd_dsi_driver;

#endif /* _SPRD_DRM_H_ */

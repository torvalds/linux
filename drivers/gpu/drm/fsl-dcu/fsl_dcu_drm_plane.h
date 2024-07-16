/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright 2015 Freescale Semiconductor, Inc.
 *
 * Freescale DCU drm device driver
 */

#ifndef __FSL_DCU_DRM_PLANE_H__
#define __FSL_DCU_DRM_PLANE_H__

void fsl_dcu_drm_init_planes(struct drm_device *dev);
struct drm_plane *fsl_dcu_drm_primary_create_plane(struct drm_device *dev);

#endif /* __FSL_DCU_DRM_PLANE_H__ */

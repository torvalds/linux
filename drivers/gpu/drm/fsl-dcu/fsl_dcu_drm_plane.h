/*
 * Copyright 2015 Freescale Semiconductor, Inc.
 *
 * Freescale DCU drm device driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef __FSL_DCU_DRM_PLANE_H__
#define __FSL_DCU_DRM_PLANE_H__

void fsl_dcu_drm_init_planes(struct drm_device *dev);
struct drm_plane *fsl_dcu_drm_primary_create_plane(struct drm_device *dev);

#endif /* __FSL_DCU_DRM_PLANE_H__ */

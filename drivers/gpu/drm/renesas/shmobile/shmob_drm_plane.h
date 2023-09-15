/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * shmob_drm_plane.h  --  SH Mobile DRM Planes
 *
 * Copyright (C) 2012 Renesas Electronics Corporation
 *
 * Laurent Pinchart (laurent.pinchart@ideasonboard.com)
 */

#ifndef __SHMOB_DRM_PLANE_H__
#define __SHMOB_DRM_PLANE_H__

struct drm_plane;
struct shmob_drm_device;

struct drm_plane *shmob_drm_plane_create(struct shmob_drm_device *sdev,
					 enum drm_plane_type type,
					 unsigned int index);

#endif /* __SHMOB_DRM_PLANE_H__ */

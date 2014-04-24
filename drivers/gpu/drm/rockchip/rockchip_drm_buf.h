/*
 * Copyright (C) ROCKCHIP, Inc.
 * Author:yzq<yzq@rock-chips.com>
 *
 * based on exynos_drm_buf.h
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#ifndef _ROCKCHIP_DRM_BUF_H_
#define _ROCKCHIP_DRM_BUF_H_

/* create and initialize buffer object. */
struct rockchip_drm_gem_buf *rockchip_drm_init_buf(struct drm_device *dev,
						unsigned int size);

/* destroy buffer object. */
void rockchip_drm_fini_buf(struct drm_device *dev,
				struct rockchip_drm_gem_buf *buffer);

/* allocate physical memory region and setup sgt. */
int rockchip_drm_alloc_buf(struct drm_device *dev,
				struct rockchip_drm_gem_buf *buf,
				unsigned int flags);

/* release physical memory region, and sgt. */
void rockchip_drm_free_buf(struct drm_device *dev,
				unsigned int flags,
				struct rockchip_drm_gem_buf *buffer);

#endif

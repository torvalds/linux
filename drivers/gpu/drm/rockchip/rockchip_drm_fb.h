/*
 * Copyright (C) ROCKCHIP, Inc.
 * Author:yzq<yzq@rock-chips.com>
 *
 * based on exynos_drm_fb.h
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
#ifndef _ROCKCHIP_DRM_FB_H_
#define _ROCKCHIP_DRM_FB_H

struct drm_framebuffer *
rockchip_drm_framebuffer_init(struct drm_device *dev,
			    struct drm_mode_fb_cmd2 *mode_cmd,
			    struct drm_gem_object *obj);

/* get memory information of a drm framebuffer */
struct rockchip_drm_gem_buf *rockchip_drm_fb_buffer(struct drm_framebuffer *fb,
						 int index);

void rockchip_drm_mode_config_init(struct drm_device *dev);

/* set a buffer count to drm framebuffer. */
void rockchip_drm_fb_set_buf_cnt(struct drm_framebuffer *fb,
						unsigned int cnt);

/* get a buffer count to drm framebuffer. */
unsigned int rockchip_drm_fb_get_buf_cnt(struct drm_framebuffer *fb);

#endif

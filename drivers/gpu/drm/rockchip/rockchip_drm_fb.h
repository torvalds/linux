/*
 * Copyright (C) Fuzhou Rockchip Electronics Co.Ltd
 * Author:Mark Yao <mark.yao@rock-chips.com>
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

#ifndef _ROCKCHIP_DRM_FB_H
#define _ROCKCHIP_DRM_FB_H

bool rockchip_fb_is_logo(struct drm_framebuffer *fb);
struct drm_framebuffer *
rockchip_drm_framebuffer_init(struct drm_device *dev,
			      struct drm_mode_fb_cmd2 *mode_cmd,
			      struct drm_gem_object *obj);
void rockchip_drm_framebuffer_fini(struct drm_framebuffer *fb);

void rockchip_drm_mode_config_init(struct drm_device *dev);

struct drm_framebuffer *
rockchip_fb_alloc(struct drm_device *dev, struct drm_mode_fb_cmd2 *mode_cmd,
		  struct drm_gem_object **obj, struct rockchip_logo *logo,
		  unsigned int num_planes);

dma_addr_t rockchip_fb_get_dma_addr(struct drm_framebuffer *fb,
				    unsigned int plane);
void *rockchip_fb_get_kvaddr(struct drm_framebuffer *fb, unsigned int plane);

#define to_rockchip_fb(x) container_of(x, struct rockchip_drm_fb, fb)

struct rockchip_drm_fb {
	struct drm_framebuffer fb;
	dma_addr_t dma_addr[ROCKCHIP_MAX_FB_BUFFER];
	void *kvaddr[ROCKCHIP_MAX_FB_BUFFER];
	struct drm_gem_object *obj[ROCKCHIP_MAX_FB_BUFFER];
	struct rockchip_logo *logo;
};
#endif /* _ROCKCHIP_DRM_FB_H */

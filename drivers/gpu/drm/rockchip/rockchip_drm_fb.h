/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) Fuzhou Rockchip Electronics Co.Ltd
 * Author:Mark Yao <mark.yao@rock-chips.com>
 */

#ifndef _ROCKCHIP_DRM_FB_H
#define _ROCKCHIP_DRM_FB_H

#include "rockchip_drm_gem.h"

#define ROCKCHIP_DRM_MODE_LOGO_FB	(1<<31) /* used for kernel logo, follow the define: DRM_MODE_FB_MODIFIERS at drm_mode.h */

struct drm_framebuffer *
rockchip_drm_framebuffer_init(struct drm_device *dev,
			      const struct drm_mode_fb_cmd2 *mode_cmd,
			      struct drm_gem_object *obj);
void rockchip_drm_framebuffer_fini(struct drm_framebuffer *fb);

void rockchip_drm_mode_config_init(struct drm_device *dev);
struct drm_framebuffer *
rockchip_drm_logo_fb_alloc(struct drm_device *dev, const struct drm_mode_fb_cmd2 *mode_cmd,
			   struct rockchip_logo *logo);
struct drm_framebuffer *
rockchip_fb_alloc(struct drm_device *dev, const struct drm_mode_fb_cmd2 *mode_cmd,
		  struct drm_gem_object **obj, unsigned int num_planes);

#define to_rockchip_logo_fb(x) container_of(x, struct rockchip_drm_logo_fb, fb)

struct rockchip_drm_logo_fb {
	struct drm_framebuffer fb;
	struct rockchip_logo *logo;
	struct rockchip_gem_object rk_obj;
	/*
	 * Used for delayed logo fb release
	 */
	struct delayed_work destroy_work;
};

#endif /* _ROCKCHIP_DRM_FB_H */

/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) Fuzhou Rockchip Electronics Co.Ltd
 * Author:Mark Yao <mark.yao@rock-chips.com>
 */

#ifndef _ROCKCHIP_DRM_FB_H
#define _ROCKCHIP_DRM_FB_H

struct drm_framebuffer *
rockchip_drm_framebuffer_init(struct drm_device *dev,
			      const struct drm_mode_fb_cmd2 *mode_cmd,
			      struct drm_gem_object *obj);
void rockchip_drm_framebuffer_fini(struct drm_framebuffer *fb);

void rockchip_drm_mode_config_init(struct drm_device *dev);
#endif /* _ROCKCHIP_DRM_FB_H */

/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * omap_fb.h -- OMAP DRM Framebuffer
 *
 * Copyright (C) 2011 Texas Instruments
 * Author: Rob Clark <rob@ti.com>
 */

#ifndef __OMAPDRM_FB_H__
#define __OMAPDRM_FB_H__

struct drm_connector;
struct drm_device;
struct drm_file;
struct drm_framebuffer;
struct drm_gem_object;
struct drm_mode_fb_cmd2;
struct drm_plane_state;
struct omap_overlay_info;
struct seq_file;

struct drm_framebuffer *omap_framebuffer_create(struct drm_device *dev,
		struct drm_file *file, const struct drm_mode_fb_cmd2 *mode_cmd);
struct drm_framebuffer *omap_framebuffer_init(struct drm_device *dev,
		const struct drm_mode_fb_cmd2 *mode_cmd, struct drm_gem_object **bos);
int omap_framebuffer_pin(struct drm_framebuffer *fb);
void omap_framebuffer_unpin(struct drm_framebuffer *fb);
void omap_framebuffer_update_scanout(struct drm_framebuffer *fb,
		struct drm_plane_state *state, struct omap_overlay_info *info);
bool omap_framebuffer_supports_rotation(struct drm_framebuffer *fb);
void omap_framebuffer_describe(struct drm_framebuffer *fb, struct seq_file *m);

#endif /* __OMAPDRM_FB_H__ */

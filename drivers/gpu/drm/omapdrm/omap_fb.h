/*
 * omap_fb.h -- OMAP DRM Framebuffer
 *
 * Copyright (C) 2011 Texas Instruments
 * Author: Rob Clark <rob@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
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
struct drm_connector *omap_framebuffer_get_next_connector(
		struct drm_framebuffer *fb, struct drm_connector *from);
bool omap_framebuffer_supports_rotation(struct drm_framebuffer *fb);
void omap_framebuffer_describe(struct drm_framebuffer *fb, struct seq_file *m);

#endif /* __OMAPDRM_FB_H__ */

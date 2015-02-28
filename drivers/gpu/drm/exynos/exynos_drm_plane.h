/*
 * Copyright (C) 2011 Samsung Electronics Co.Ltd
 * Authors: Joonyoung Shim <jy0922.shim@samsung.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 */

int exynos_check_plane(struct drm_plane *plane, struct drm_framebuffer *fb);
void exynos_plane_mode_set(struct drm_plane *plane, struct drm_crtc *crtc,
			   struct drm_framebuffer *fb, int crtc_x, int crtc_y,
			   unsigned int crtc_w, unsigned int crtc_h,
			   uint32_t src_x, uint32_t src_y,
			   uint32_t src_w, uint32_t src_h);
int exynos_update_plane(struct drm_plane *plane, struct drm_crtc *crtc,
			struct drm_framebuffer *fb, int crtc_x, int crtc_y,
			unsigned int crtc_w, unsigned int crtc_h,
			uint32_t src_x, uint32_t src_y,
			uint32_t src_w, uint32_t src_h);
struct drm_plane *exynos_plane_init(struct drm_device *dev,
				    unsigned long possible_crtcs,
				    enum drm_plane_type type);

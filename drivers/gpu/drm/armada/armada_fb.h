/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2012 Russell King
 */
#ifndef ARMADA_FB_H
#define ARMADA_FB_H

#include <drm/drm_framebuffer.h>

struct armada_framebuffer {
	struct drm_framebuffer	fb;
	uint8_t			fmt;
	uint8_t			mod;
};
#define drm_fb_to_armada_fb(dfb) \
	container_of(dfb, struct armada_framebuffer, fb)
#define drm_fb_obj(fb) drm_to_armada_gem((fb)->obj[0])

struct armada_framebuffer *armada_framebuffer_create(struct drm_device *,
	const struct drm_format_info *info,
	const struct drm_mode_fb_cmd2 *, struct armada_gem_object *);
struct drm_framebuffer *armada_fb_create(struct drm_device *dev,
	struct drm_file *dfile, const struct drm_format_info *info,
	const struct drm_mode_fb_cmd2 *mode);
#endif

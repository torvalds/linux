/*
 * Copyright (C) 2012 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef ARMADA_FB_H
#define ARMADA_FB_H

struct armada_framebuffer {
	struct drm_framebuffer	fb;
	uint8_t			fmt;
	uint8_t			mod;
};
#define drm_fb_to_armada_fb(dfb) \
	container_of(dfb, struct armada_framebuffer, fb)
#define drm_fb_obj(fb) drm_to_armada_gem((fb)->obj[0])

struct armada_framebuffer *armada_framebuffer_create(struct drm_device *,
	const struct drm_mode_fb_cmd2 *, struct armada_gem_object *);

#endif

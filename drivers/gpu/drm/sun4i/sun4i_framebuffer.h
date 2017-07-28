/*
 * Copyright (C) 2015 Free Electrons
 * Copyright (C) 2015 NextThing Co
 *
 * Maxime Ripard <maxime.ripard@free-electrons.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 */

#ifndef _SUN4I_FRAMEBUFFER_H_
#define _SUN4I_FRAMEBUFFER_H_

struct drm_fbdev_cma *sun4i_framebuffer_init(struct drm_device *drm);
void sun4i_framebuffer_free(struct drm_device *drm);

#endif /* _SUN4I_FRAMEBUFFER_H_ */

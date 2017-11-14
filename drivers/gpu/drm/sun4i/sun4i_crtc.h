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

#ifndef _SUN4I_CRTC_H_
#define _SUN4I_CRTC_H_

struct sun4i_crtc {
	struct drm_crtc			crtc;
	struct drm_pending_vblank_event	*event;

	struct sunxi_engine		*engine;
	struct sun4i_tcon		*tcon;
};

static inline struct sun4i_crtc *drm_crtc_to_sun4i_crtc(struct drm_crtc *crtc)
{
	return container_of(crtc, struct sun4i_crtc, crtc);
}

struct sun4i_crtc *sun4i_crtc_init(struct drm_device *drm,
				   struct sunxi_engine *engine,
				   struct sun4i_tcon *tcon);

#endif /* _SUN4I_CRTC_H_ */

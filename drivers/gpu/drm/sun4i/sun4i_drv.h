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

#ifndef _SUN4I_DRV_H_
#define _SUN4I_DRV_H_

#include <linux/clk.h>
#include <linux/regmap.h>

struct sun4i_drv {
	struct sun4i_backend	*backend;
	struct sun4i_tcon	*tcon;

	struct drm_fbdev_cma	*fbdev;
};

#endif /* _SUN4I_DRV_H_ */

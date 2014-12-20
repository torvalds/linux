/*
 * rcar_du_lvdsenc.h  --  R-Car Display Unit LVDS Encoder
 *
 * Copyright (C) 2013-2014 Renesas Electronics Corporation
 *
 * Contact: Laurent Pinchart (laurent.pinchart@ideasonboard.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef __RCAR_DU_LVDSENC_H__
#define __RCAR_DU_LVDSENC_H__

#include <linux/io.h>
#include <linux/module.h>

struct rcar_drm_crtc;
struct rcar_du_lvdsenc;

enum rcar_lvds_input {
	RCAR_LVDS_INPUT_DU0,
	RCAR_LVDS_INPUT_DU1,
	RCAR_LVDS_INPUT_DU2,
};

#if IS_ENABLED(CONFIG_DRM_RCAR_LVDS)
int rcar_du_lvdsenc_init(struct rcar_du_device *rcdu);
int rcar_du_lvdsenc_dpms(struct rcar_du_lvdsenc *lvds,
			 struct drm_crtc *crtc, int mode);
#else
static inline int rcar_du_lvdsenc_init(struct rcar_du_device *rcdu)
{
	return 0;
}
static inline int rcar_du_lvdsenc_dpms(struct rcar_du_lvdsenc *lvds,
				       struct drm_crtc *crtc, int mode)
{
	return 0;
}
#endif

#endif /* __RCAR_DU_LVDSENC_H__ */

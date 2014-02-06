/*
 * rcar_du_plane.h  --  R-Car Display Unit Planes
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

#ifndef __RCAR_DU_PLANE_H__
#define __RCAR_DU_PLANE_H__

#include <linux/mutex.h>

#include <drm/drmP.h>
#include <drm/drm_crtc.h>

struct rcar_du_format_info;
struct rcar_du_group;

/* The RCAR DU has 8 hardware planes, shared between KMS planes and CRTCs. As
 * using KMS planes requires at least one of the CRTCs being enabled, no more
 * than 7 KMS planes can be available. We thus create 7 KMS planes and
 * 9 software planes (one for each KMS planes and one for each CRTC).
 */

#define RCAR_DU_NUM_KMS_PLANES		7
#define RCAR_DU_NUM_HW_PLANES		8
#define RCAR_DU_NUM_SW_PLANES		9

struct rcar_du_plane {
	struct rcar_du_group *group;
	struct drm_crtc *crtc;

	bool enabled;

	int hwindex;		/* 0-based, -1 means unused */
	unsigned int alpha;
	unsigned int colorkey;
	unsigned int zpos;

	const struct rcar_du_format_info *format;

	unsigned long dma[2];
	unsigned int pitch;

	unsigned int width;
	unsigned int height;

	unsigned int src_x;
	unsigned int src_y;
	unsigned int dst_x;
	unsigned int dst_y;
};

struct rcar_du_planes {
	struct rcar_du_plane planes[RCAR_DU_NUM_SW_PLANES];
	unsigned int free;
	struct mutex lock;

	struct drm_property *alpha;
	struct drm_property *colorkey;
	struct drm_property *zpos;
};

int rcar_du_planes_init(struct rcar_du_group *rgrp);
int rcar_du_planes_register(struct rcar_du_group *rgrp);

void rcar_du_plane_setup(struct rcar_du_plane *plane);
void rcar_du_plane_update_base(struct rcar_du_plane *plane);
void rcar_du_plane_compute_base(struct rcar_du_plane *plane,
				struct drm_framebuffer *fb);
int rcar_du_plane_reserve(struct rcar_du_plane *plane,
			  const struct rcar_du_format_info *format);
void rcar_du_plane_release(struct rcar_du_plane *plane);

#endif /* __RCAR_DU_PLANE_H__ */

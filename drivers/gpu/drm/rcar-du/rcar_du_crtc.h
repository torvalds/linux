/*
 * rcar_du_crtc.h  --  R-Car Display Unit CRTCs
 *
 * Copyright (C) 2013 Renesas Corporation
 *
 * Contact: Laurent Pinchart (laurent.pinchart@ideasonboard.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef __RCAR_DU_CRTC_H__
#define __RCAR_DU_CRTC_H__

#include <linux/mutex.h>

#include <drm/drmP.h>
#include <drm/drm_crtc.h>

struct rcar_du_group;
struct rcar_du_plane;

struct rcar_du_crtc {
	struct drm_crtc crtc;

	struct clk *clock;
	unsigned int mmio_offset;
	unsigned int index;
	bool started;

	struct drm_pending_vblank_event *event;
	unsigned int outputs;
	int dpms;

	struct rcar_du_group *group;
	struct rcar_du_plane *plane;
};

int rcar_du_crtc_create(struct rcar_du_group *rgrp, unsigned int index);
void rcar_du_crtc_enable_vblank(struct rcar_du_crtc *rcrtc, bool enable);
void rcar_du_crtc_cancel_page_flip(struct rcar_du_crtc *rcrtc,
				   struct drm_file *file);
void rcar_du_crtc_suspend(struct rcar_du_crtc *rcrtc);
void rcar_du_crtc_resume(struct rcar_du_crtc *rcrtc);

void rcar_du_crtc_route_output(struct drm_crtc *crtc, unsigned int output);
void rcar_du_crtc_update_planes(struct drm_crtc *crtc);

#endif /* __RCAR_DU_CRTC_H__ */

/*
 * rcar_du_vsp.h  --  R-Car Display Unit VSP-Based Compositor
 *
 * Copyright (C) 2015 Renesas Electronics Corporation
 *
 * Contact: Laurent Pinchart (laurent.pinchart@ideasonboard.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef __RCAR_DU_VSP_H__
#define __RCAR_DU_VSP_H__

#include <drm/drmP.h>
#include <drm/drm_crtc.h>

struct rcar_du_format_info;
struct rcar_du_vsp;

struct rcar_du_vsp_plane {
	struct drm_plane plane;
	struct rcar_du_vsp *vsp;
	unsigned int index;
};

struct rcar_du_vsp {
	unsigned int index;
	struct device *vsp;
	struct rcar_du_device *dev;
	struct rcar_du_vsp_plane *planes;
	unsigned int num_planes;
};

static inline struct rcar_du_vsp_plane *to_rcar_vsp_plane(struct drm_plane *p)
{
	return container_of(p, struct rcar_du_vsp_plane, plane);
}

/**
 * struct rcar_du_vsp_plane_state - Driver-specific plane state
 * @state: base DRM plane state
 * @format: information about the pixel format used by the plane
 * @alpha: value of the plane alpha property
 * @zpos: value of the plane zpos property
 */
struct rcar_du_vsp_plane_state {
	struct drm_plane_state state;

	const struct rcar_du_format_info *format;

	unsigned int alpha;
	unsigned int zpos;
};

static inline struct rcar_du_vsp_plane_state *
to_rcar_vsp_plane_state(struct drm_plane_state *state)
{
	return container_of(state, struct rcar_du_vsp_plane_state, state);
}

#ifdef CONFIG_DRM_RCAR_VSP
int rcar_du_vsp_init(struct rcar_du_vsp *vsp);
void rcar_du_vsp_enable(struct rcar_du_crtc *crtc);
void rcar_du_vsp_disable(struct rcar_du_crtc *crtc);
void rcar_du_vsp_atomic_begin(struct rcar_du_crtc *crtc);
void rcar_du_vsp_atomic_flush(struct rcar_du_crtc *crtc);
#else
static inline int rcar_du_vsp_init(struct rcar_du_vsp *vsp) { return 0; };
static inline void rcar_du_vsp_enable(struct rcar_du_crtc *crtc) { };
static inline void rcar_du_vsp_disable(struct rcar_du_crtc *crtc) { };
static inline void rcar_du_vsp_atomic_begin(struct rcar_du_crtc *crtc) { };
static inline void rcar_du_vsp_atomic_flush(struct rcar_du_crtc *crtc) { };
#endif

#endif /* __RCAR_DU_VSP_H__ */

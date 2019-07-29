/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * rcar_du_plane.h  --  R-Car Display Unit Planes
 *
 * Copyright (C) 2013-2014 Renesas Electronics Corporation
 *
 * Contact: Laurent Pinchart (laurent.pinchart@ideasonboard.com)
 */

#ifndef __RCAR_DU_PLANE_H__
#define __RCAR_DU_PLANE_H__

#include <drm/drmP.h>
#include <drm/drm_crtc.h>

struct rcar_du_format_info;
struct rcar_du_group;

/*
 * The RCAR DU has 8 hardware planes, shared between primary and overlay planes.
 * As using overlay planes requires at least one of the CRTCs being enabled, no
 * more than 7 overlay planes can be available. We thus create 1 primary plane
 * per CRTC and 7 overlay planes, for a total of up to 9 KMS planes.
 */
#define RCAR_DU_NUM_KMS_PLANES		9
#define RCAR_DU_NUM_HW_PLANES		8

enum rcar_du_plane_source {
	RCAR_DU_PLANE_MEMORY,
	RCAR_DU_PLANE_VSPD0,
	RCAR_DU_PLANE_VSPD1,
};

struct rcar_du_plane {
	struct drm_plane plane;
	struct rcar_du_group *group;
};

static inline struct rcar_du_plane *to_rcar_plane(struct drm_plane *plane)
{
	return container_of(plane, struct rcar_du_plane, plane);
}

/**
 * struct rcar_du_plane_state - Driver-specific plane state
 * @state: base DRM plane state
 * @format: information about the pixel format used by the plane
 * @hwindex: 0-based hardware plane index, -1 means unused
 * @colorkey: value of the plane colorkey property
 */
struct rcar_du_plane_state {
	struct drm_plane_state state;

	const struct rcar_du_format_info *format;
	int hwindex;
	enum rcar_du_plane_source source;

	unsigned int colorkey;
};

static inline struct rcar_du_plane_state *
to_rcar_plane_state(struct drm_plane_state *state)
{
	return container_of(state, struct rcar_du_plane_state, state);
}

int rcar_du_atomic_check_planes(struct drm_device *dev,
				struct drm_atomic_state *state);

int __rcar_du_plane_atomic_check(struct drm_plane *plane,
				 struct drm_plane_state *state,
				 const struct rcar_du_format_info **format);

int rcar_du_planes_init(struct rcar_du_group *rgrp);

void __rcar_du_plane_setup(struct rcar_du_group *rgrp,
			   const struct rcar_du_plane_state *state);

static inline void rcar_du_plane_setup(struct rcar_du_plane *plane)
{
	struct rcar_du_plane_state *state =
		to_rcar_plane_state(plane->plane.state);

	return __rcar_du_plane_setup(plane->group, state);
}

#endif /* __RCAR_DU_PLANE_H__ */

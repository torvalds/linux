/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * rcar_du_vsp.h  --  R-Car Display Unit VSP-Based Compositor
 *
 * Copyright (C) 2015 Renesas Electronics Corporation
 *
 * Contact: Laurent Pinchart (laurent.pinchart@ideasonboard.com)
 */

#ifndef __RCAR_DU_VSP_H__
#define __RCAR_DU_VSP_H__

#include <drm/drm_plane.h>

struct drm_framebuffer;
struct rcar_du_format_info;
struct rcar_du_vsp;
struct sg_table;

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
 * @sg_tables: scatter-gather tables for the frame buffer memory
 */
struct rcar_du_vsp_plane_state {
	struct drm_plane_state state;

	const struct rcar_du_format_info *format;
	struct sg_table sg_tables[3];
};

static inline struct rcar_du_vsp_plane_state *
to_rcar_vsp_plane_state(struct drm_plane_state *state)
{
	return container_of(state, struct rcar_du_vsp_plane_state, state);
}

#ifdef CONFIG_DRM_RCAR_VSP
int rcar_du_vsp_init(struct rcar_du_vsp *vsp, struct device_node *np,
		     unsigned int crtcs);
void rcar_du_vsp_enable(struct rcar_du_crtc *crtc);
void rcar_du_vsp_disable(struct rcar_du_crtc *crtc);
void rcar_du_vsp_atomic_begin(struct rcar_du_crtc *crtc);
void rcar_du_vsp_atomic_flush(struct rcar_du_crtc *crtc);
int rcar_du_vsp_map_fb(struct rcar_du_vsp *vsp, struct drm_framebuffer *fb,
		       struct sg_table sg_tables[3]);
void rcar_du_vsp_unmap_fb(struct rcar_du_vsp *vsp, struct drm_framebuffer *fb,
			  struct sg_table sg_tables[3]);
#else
static inline int rcar_du_vsp_init(struct rcar_du_vsp *vsp,
				   struct device_node *np,
				   unsigned int crtcs)
{
	return -ENXIO;
}
static inline void rcar_du_vsp_enable(struct rcar_du_crtc *crtc) { };
static inline void rcar_du_vsp_disable(struct rcar_du_crtc *crtc) { };
static inline void rcar_du_vsp_atomic_begin(struct rcar_du_crtc *crtc) { };
static inline void rcar_du_vsp_atomic_flush(struct rcar_du_crtc *crtc) { };
static inline int rcar_du_vsp_map_fb(struct rcar_du_vsp *vsp,
				     struct drm_framebuffer *fb,
				     struct sg_table sg_tables[3])
{
	return -ENXIO;
}
static inline void rcar_du_vsp_unmap_fb(struct rcar_du_vsp *vsp,
					struct drm_framebuffer *fb,
					struct sg_table sg_tables[3])
{
}
#endif

#endif /* __RCAR_DU_VSP_H__ */

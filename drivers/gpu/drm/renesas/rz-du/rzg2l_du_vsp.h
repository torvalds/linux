/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * RZ/G2L Display Unit VSP-Based Compositor
 *
 * Copyright (C) 2023 Renesas Electronics Corporation
 *
 * Based on rcar_du_vsp.h
 */

#ifndef __RZG2L_DU_VSP_H__
#define __RZG2L_DU_VSP_H__

#include <drm/drm_plane.h>
#include <linux/container_of.h>
#include <linux/scatterlist.h>

struct device;
struct drm_framebuffer;
struct rzg2l_du_device;
struct rzg2l_du_format_info;
struct rzg2l_du_vsp;

struct rzg2l_du_vsp_plane {
	struct drm_plane plane;
	struct rzg2l_du_vsp *vsp;
	unsigned int index;
};

struct rzg2l_du_vsp {
	unsigned int index;
	struct device *vsp;
	struct rzg2l_du_device *dev;
};

static inline struct rzg2l_du_vsp_plane *to_rzg2l_vsp_plane(struct drm_plane *p)
{
	return container_of(p, struct rzg2l_du_vsp_plane, plane);
}

/**
 * struct rzg2l_du_vsp_plane_state - Driver-specific plane state
 * @state: base DRM plane state
 * @format: information about the pixel format used by the plane
 */
struct rzg2l_du_vsp_plane_state {
	struct drm_plane_state state;

	const struct rzg2l_du_format_info *format;
};

static inline struct rzg2l_du_vsp_plane_state *
to_rzg2l_vsp_plane_state(struct drm_plane_state *state)
{
	return container_of(state, struct rzg2l_du_vsp_plane_state, state);
}

#if IS_ENABLED(CONFIG_VIDEO_RENESAS_VSP1)
int rzg2l_du_vsp_init(struct rzg2l_du_vsp *vsp, struct device_node *np,
		      unsigned int crtcs);
void rzg2l_du_vsp_enable(struct rzg2l_du_crtc *crtc);
void rzg2l_du_vsp_disable(struct rzg2l_du_crtc *crtc);
void rzg2l_du_vsp_atomic_flush(struct rzg2l_du_crtc *crtc);
struct drm_plane *rzg2l_du_vsp_get_drm_plane(struct rzg2l_du_crtc *crtc,
					     unsigned int pipe_index);
#else
static inline int rzg2l_du_vsp_init(struct rzg2l_du_vsp *vsp, struct device_node *np,
				    unsigned int crtcs)
{
	return -ENXIO;
}

static inline void rzg2l_du_vsp_enable(struct rzg2l_du_crtc *crtc) { };
static inline void rzg2l_du_vsp_disable(struct rzg2l_du_crtc *crtc) { };
static inline void rzg2l_du_vsp_atomic_flush(struct rzg2l_du_crtc *crtc) { };
static inline struct drm_plane *rzg2l_du_vsp_get_drm_plane(struct rzg2l_du_crtc *crtc,
							   unsigned int pipe_index)
{
	return ERR_PTR(-ENXIO);
}
#endif

#endif /* __RZG2L_DU_VSP_H__ */

/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */

#ifndef __INTEL_DP_TUNNEL_H__
#define __INTEL_DP_TUNNEL_H__

#include <linux/errno.h>
#include <linux/types.h>

struct drm_connector_state;
struct drm_modeset_acquire_ctx;
struct intel_atomic_state;
struct intel_connector;
struct intel_crtc;
struct intel_crtc_state;
struct intel_display;
struct intel_dp;
struct intel_encoder;
struct intel_link_bw_limits;

#if (IS_ENABLED(CONFIG_DRM_I915_DP_TUNNEL) && defined(I915)) || \
	(IS_ENABLED(CONFIG_DRM_XE_DP_TUNNEL) && !defined(I915))

int intel_dp_tunnel_detect(struct intel_dp *intel_dp, struct drm_modeset_acquire_ctx *ctx);
void intel_dp_tunnel_disconnect(struct intel_dp *intel_dp);
void intel_dp_tunnel_destroy(struct intel_dp *intel_dp);
void intel_dp_tunnel_resume(struct intel_dp *intel_dp,
			    const struct intel_crtc_state *crtc_state,
			    bool dpcd_updated);
void intel_dp_tunnel_suspend(struct intel_dp *intel_dp);

bool intel_dp_tunnel_bw_alloc_is_enabled(struct intel_dp *intel_dp);

void
intel_dp_tunnel_atomic_cleanup_inherited_state(struct intel_atomic_state *state);

int intel_dp_tunnel_atomic_compute_stream_bw(struct intel_atomic_state *state,
					     struct intel_dp *intel_dp,
					     const struct intel_connector *connector,
					     struct intel_crtc_state *crtc_state);
void intel_dp_tunnel_atomic_clear_stream_bw(struct intel_atomic_state *state,
					    struct intel_crtc_state *crtc_state);

int intel_dp_tunnel_atomic_add_state_for_crtc(struct intel_atomic_state *state,
					      struct intel_crtc *crtc);
int intel_dp_tunnel_atomic_check_link(struct intel_atomic_state *state,
				      struct intel_link_bw_limits *limits);
int intel_dp_tunnel_atomic_check_state(struct intel_atomic_state *state,
				       struct intel_dp *intel_dp,
				       struct intel_connector *connector);

void intel_dp_tunnel_atomic_alloc_bw(struct intel_atomic_state *state);

int intel_dp_tunnel_mgr_init(struct intel_display *display);
void intel_dp_tunnel_mgr_cleanup(struct intel_display *display);

#else

static inline int
intel_dp_tunnel_detect(struct intel_dp *intel_dp, struct drm_modeset_acquire_ctx *ctx)
{
	return -EOPNOTSUPP;
}

static inline void intel_dp_tunnel_disconnect(struct intel_dp *intel_dp) {}
static inline void intel_dp_tunnel_destroy(struct intel_dp *intel_dp) {}
static inline void intel_dp_tunnel_resume(struct intel_dp *intel_dp,
					  const struct intel_crtc_state *crtc_state,
					  bool dpcd_updated) {}
static inline void intel_dp_tunnel_suspend(struct intel_dp *intel_dp) {}

static inline bool intel_dp_tunnel_bw_alloc_is_enabled(struct intel_dp *intel_dp)
{
	return false;
}

static inline void
intel_dp_tunnel_atomic_cleanup_inherited_state(struct intel_atomic_state *state) {}

static inline int
intel_dp_tunnel_atomic_compute_stream_bw(struct intel_atomic_state *state,
					 struct intel_dp *intel_dp,
					 const struct intel_connector *connector,
					 struct intel_crtc_state *crtc_state)
{
	return 0;
}

static inline void
intel_dp_tunnel_atomic_clear_stream_bw(struct intel_atomic_state *state,
				       struct intel_crtc_state *crtc_state) {}

static inline int
intel_dp_tunnel_atomic_add_state_for_crtc(struct intel_atomic_state *state,
					  struct intel_crtc *crtc)
{
	return 0;
}

static inline int
intel_dp_tunnel_atomic_check_link(struct intel_atomic_state *state,
				  struct intel_link_bw_limits *limits)
{
	return 0;
}

static inline int
intel_dp_tunnel_atomic_check_state(struct intel_atomic_state *state,
				   struct intel_dp *intel_dp,
				   struct intel_connector *connector)
{
	return 0;
}

static inline int
intel_dp_tunnel_atomic_alloc_bw(struct intel_atomic_state *state)
{
	return 0;
}

static inline int
intel_dp_tunnel_mgr_init(struct intel_display *display)
{
	return 0;
}

static inline void intel_dp_tunnel_mgr_cleanup(struct intel_display *display) {}

#endif /* CONFIG_DRM_I915_DP_TUNNEL || CONFIG_DRM_XE_DP_TUNNEL */

#endif /* __INTEL_DP_TUNNEL_H__ */

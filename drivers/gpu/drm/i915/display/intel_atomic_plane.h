/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2019 Intel Corporation
 */

#ifndef __INTEL_ATOMIC_PLANE_H__
#define __INTEL_ATOMIC_PLANE_H__

#include <linux/types.h>

struct drm_plane;
struct drm_property;
struct drm_rect;
struct intel_atomic_state;
struct intel_crtc;
struct intel_crtc_state;
struct intel_dsb;
struct intel_plane;
struct intel_plane_state;
enum plane_id;

struct intel_plane *
intel_crtc_get_plane(struct intel_crtc *crtc, enum plane_id plane_id);
bool intel_plane_can_async_flip(struct intel_plane *plane, u64 modifier);
unsigned int intel_adjusted_rate(const struct drm_rect *src,
				 const struct drm_rect *dst,
				 unsigned int rate);
unsigned int intel_plane_pixel_rate(const struct intel_crtc_state *crtc_state,
				    const struct intel_plane_state *plane_state);

unsigned int intel_plane_data_rate(const struct intel_crtc_state *crtc_state,
				   const struct intel_plane_state *plane_state,
				   int color_plane);
void intel_plane_copy_uapi_to_hw_state(struct intel_plane_state *plane_state,
				       const struct intel_plane_state *from_plane_state,
				       struct intel_crtc *crtc);
void intel_plane_copy_hw_state(struct intel_plane_state *plane_state,
			       const struct intel_plane_state *from_plane_state);
void intel_plane_async_flip(struct intel_dsb *dsb,
			    struct intel_plane *plane,
			    const struct intel_crtc_state *crtc_state,
			    const struct intel_plane_state *plane_state,
			    bool async_flip);
void intel_plane_update_noarm(struct intel_dsb *dsb,
			      struct intel_plane *plane,
			      const struct intel_crtc_state *crtc_state,
			      const struct intel_plane_state *plane_state);
void intel_plane_update_arm(struct intel_dsb *dsb,
			    struct intel_plane *plane,
			    const struct intel_crtc_state *crtc_state,
			    const struct intel_plane_state *plane_state);
void intel_plane_disable_arm(struct intel_dsb *dsb,
			     struct intel_plane *plane,
			     const struct intel_crtc_state *crtc_state);
struct intel_plane *intel_plane_alloc(void);
void intel_plane_free(struct intel_plane *plane);
void intel_plane_destroy(struct drm_plane *plane);
struct drm_plane_state *intel_plane_duplicate_state(struct drm_plane *plane);
void intel_plane_destroy_state(struct drm_plane *plane,
			       struct drm_plane_state *state);
void intel_crtc_planes_update_noarm(struct intel_dsb *dsb,
				    struct intel_atomic_state *state,
				    struct intel_crtc *crtc);
void intel_crtc_planes_update_arm(struct intel_dsb *dsbx,
				  struct intel_atomic_state *state,
				  struct intel_crtc *crtc);
int intel_plane_atomic_check_with_state(const struct intel_crtc_state *old_crtc_state,
					struct intel_crtc_state *crtc_state,
					const struct intel_plane_state *old_plane_state,
					struct intel_plane_state *intel_state);
int intel_plane_atomic_check(struct intel_atomic_state *state,
			     struct intel_plane *plane);
int intel_plane_calc_min_cdclk(struct intel_atomic_state *state,
			       struct intel_plane *plane,
			       bool *need_cdclk_calc);
int intel_atomic_plane_check_clipping(struct intel_plane_state *plane_state,
				      struct intel_crtc_state *crtc_state,
				      int min_scale, int max_scale,
				      bool can_position);
int intel_plane_check_src_coordinates(struct intel_plane_state *plane_state);
void intel_plane_set_invisible(struct intel_crtc_state *crtc_state,
			       struct intel_plane_state *plane_state);
void intel_plane_helper_add(struct intel_plane *plane);
bool intel_plane_needs_physical(struct intel_plane *plane);
void intel_plane_init_cursor_vblank_work(struct intel_plane_state *old_plane_state,
					 struct intel_plane_state *new_plane_state);
int intel_atomic_add_affected_planes(struct intel_atomic_state *state,
				     struct intel_crtc *crtc);
int intel_atomic_check_planes(struct intel_atomic_state *state);

u32 intel_plane_ggtt_offset(const struct intel_plane_state *plane_state);

#endif /* __INTEL_ATOMIC_PLANE_H__ */

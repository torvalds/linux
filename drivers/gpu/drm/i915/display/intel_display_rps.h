/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */

#ifndef __INTEL_DISPLAY_RPS_H__
#define __INTEL_DISPLAY_RPS_H__

#include <linux/types.h>

struct dma_fence;
struct drm_crtc;
struct intel_atomic_state;
struct intel_display;

#ifdef I915
void intel_display_rps_boost_after_vblank(struct drm_crtc *crtc,
					  struct dma_fence *fence);
void intel_display_rps_mark_interactive(struct intel_display *display,
					struct intel_atomic_state *state,
					bool interactive);
void ilk_display_rps_enable(struct intel_display *display);
void ilk_display_rps_disable(struct intel_display *display);
void ilk_display_rps_irq_handler(struct intel_display *display);
#else
static inline void intel_display_rps_boost_after_vblank(struct drm_crtc *crtc,
							struct dma_fence *fence)
{
}
static inline void intel_display_rps_mark_interactive(struct intel_display *display,
						      struct intel_atomic_state *state,
						      bool interactive)
{
}
static inline void ilk_display_rps_enable(struct intel_display *display)
{
}
static inline void ilk_display_rps_disable(struct intel_display *display)
{
}
static inline void ilk_display_rps_irq_handler(struct intel_display *display)
{
}
#endif

#endif /* __INTEL_DISPLAY_RPS_H__ */

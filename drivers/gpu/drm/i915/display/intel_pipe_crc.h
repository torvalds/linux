/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2019 Intel Corporation
 */

#ifndef __INTEL_PIPE_CRC_H__
#define __INTEL_PIPE_CRC_H__

#include <linux/types.h>

struct drm_crtc;
struct drm_i915_private;
struct intel_crtc;

#ifdef CONFIG_DEBUG_FS
void intel_display_crc_init(struct drm_i915_private *dev_priv);
int intel_crtc_set_crc_source(struct drm_crtc *crtc, const char *source_name);
int intel_crtc_verify_crc_source(struct drm_crtc *crtc,
				 const char *source_name, size_t *values_cnt);
const char *const *intel_crtc_get_crc_sources(struct drm_crtc *crtc,
					      size_t *count);
void intel_crtc_disable_pipe_crc(struct intel_crtc *crtc);
void intel_crtc_enable_pipe_crc(struct intel_crtc *crtc);
#else
static inline void intel_display_crc_init(struct drm_i915_private *dev_priv) {}
#define intel_crtc_set_crc_source NULL
#define intel_crtc_verify_crc_source NULL
#define intel_crtc_get_crc_sources NULL
static inline void intel_crtc_disable_pipe_crc(struct intel_crtc *crtc)
{
}

static inline void intel_crtc_enable_pipe_crc(struct intel_crtc *crtc)
{
}
#endif

#endif /* __INTEL_PIPE_CRC_H__ */

/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2019 Intel Corporation
 */

#ifndef __INTEL_LPE_AUDIO_H__
#define __INTEL_LPE_AUDIO_H__

#include <linux/types.h>

enum port;
enum transcoder;
struct drm_i915_private;

#ifdef I915
int  intel_lpe_audio_init(struct drm_i915_private *dev_priv);
void intel_lpe_audio_teardown(struct drm_i915_private *dev_priv);
void intel_lpe_audio_irq_handler(struct drm_i915_private *dev_priv);
void intel_lpe_audio_notify(struct drm_i915_private *dev_priv,
			    enum transcoder cpu_transcoder, enum port port,
			    const void *eld, int ls_clock, bool dp_output);
#else
static inline int intel_lpe_audio_init(struct drm_i915_private *dev_priv)
{
	return -ENODEV;
}
static inline void intel_lpe_audio_teardown(struct drm_i915_private *dev_priv)
{
}
static inline void intel_lpe_audio_irq_handler(struct drm_i915_private *dev_priv)
{
}
static inline void intel_lpe_audio_notify(struct drm_i915_private *dev_priv,
					  enum transcoder cpu_transcoder, enum port port,
					  const void *eld, int ls_clock, bool dp_output)
{
}
#endif

#endif /* __INTEL_LPE_AUDIO_H__ */

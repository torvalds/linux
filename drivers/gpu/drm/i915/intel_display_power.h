/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2019 Intel Corporation
 */

#ifndef __INTEL_DISPLAY_POWER_H__
#define __INTEL_DISPLAY_POWER_H__

#include "intel_display.h"
#include "intel_runtime_pm.h"

struct drm_i915_private;
struct intel_encoder;

void skl_enable_dc6(struct drm_i915_private *dev_priv);
void gen9_sanitize_dc_state(struct drm_i915_private *dev_priv);
void bxt_enable_dc9(struct drm_i915_private *dev_priv);
void bxt_disable_dc9(struct drm_i915_private *dev_priv);
void gen9_enable_dc5(struct drm_i915_private *dev_priv);

int intel_power_domains_init(struct drm_i915_private *dev_priv);
void intel_power_domains_cleanup(struct drm_i915_private *dev_priv);
void intel_power_domains_init_hw(struct drm_i915_private *dev_priv, bool resume);
void intel_power_domains_fini_hw(struct drm_i915_private *dev_priv);
void icl_display_core_init(struct drm_i915_private *dev_priv, bool resume);
void icl_display_core_uninit(struct drm_i915_private *dev_priv);
void intel_power_domains_enable(struct drm_i915_private *dev_priv);
void intel_power_domains_disable(struct drm_i915_private *dev_priv);
void intel_power_domains_suspend(struct drm_i915_private *dev_priv,
				 enum i915_drm_suspend_mode);
void intel_power_domains_resume(struct drm_i915_private *dev_priv);
void hsw_enable_pc8(struct drm_i915_private *dev_priv);
void hsw_disable_pc8(struct drm_i915_private *dev_priv);
void bxt_display_core_init(struct drm_i915_private *dev_priv, bool resume);
void bxt_display_core_uninit(struct drm_i915_private *dev_priv);

const char *
intel_display_power_domain_str(enum intel_display_power_domain domain);

bool intel_display_power_is_enabled(struct drm_i915_private *dev_priv,
				    enum intel_display_power_domain domain);
bool __intel_display_power_is_enabled(struct drm_i915_private *dev_priv,
				      enum intel_display_power_domain domain);
intel_wakeref_t intel_display_power_get(struct drm_i915_private *dev_priv,
					enum intel_display_power_domain domain);
intel_wakeref_t
intel_display_power_get_if_enabled(struct drm_i915_private *dev_priv,
				   enum intel_display_power_domain domain);
void intel_display_power_put_unchecked(struct drm_i915_private *dev_priv,
				       enum intel_display_power_domain domain);
void __intel_display_power_put_async(struct drm_i915_private *i915,
				     enum intel_display_power_domain domain,
				     intel_wakeref_t wakeref);
void intel_display_power_flush_work(struct drm_i915_private *i915);
#if IS_ENABLED(CONFIG_DRM_I915_DEBUG_RUNTIME_PM)
void intel_display_power_put(struct drm_i915_private *dev_priv,
			     enum intel_display_power_domain domain,
			     intel_wakeref_t wakeref);
static inline void
intel_display_power_put_async(struct drm_i915_private *i915,
			      enum intel_display_power_domain domain,
			      intel_wakeref_t wakeref)
{
	__intel_display_power_put_async(i915, domain, wakeref);
}
#else
static inline void
intel_display_power_put(struct drm_i915_private *i915,
			enum intel_display_power_domain domain,
			intel_wakeref_t wakeref)
{
	intel_display_power_put_unchecked(i915, domain);
}

static inline void
intel_display_power_put_async(struct drm_i915_private *i915,
			      enum intel_display_power_domain domain,
			      intel_wakeref_t wakeref)
{
	__intel_display_power_put_async(i915, domain, -1);
}
#endif

#define with_intel_display_power(i915, domain, wf) \
	for ((wf) = intel_display_power_get((i915), (domain)); (wf); \
	     intel_display_power_put_async((i915), (domain), (wf)), (wf) = 0)

void icl_dbuf_slices_update(struct drm_i915_private *dev_priv,
			    u8 req_slices);

void chv_phy_powergate_lanes(struct intel_encoder *encoder,
			     bool override, unsigned int mask);
bool chv_phy_powergate_ch(struct drm_i915_private *dev_priv, enum dpio_phy phy,
			  enum dpio_channel ch, bool override);

#endif /* __INTEL_DISPLAY_POWER_H__ */

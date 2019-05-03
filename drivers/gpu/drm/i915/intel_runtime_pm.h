/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2019 Intel Corporation
 */

#ifndef __INTEL_RUNTIME_PM_H__
#define __INTEL_RUNTIME_PM_H__

#include <linux/stackdepot.h>
#include <linux/types.h>

struct drm_i915_private;

typedef depot_stack_handle_t intel_wakeref_t;

enum i915_drm_suspend_mode {
	I915_DRM_SUSPEND_IDLE,
	I915_DRM_SUSPEND_MEM,
	I915_DRM_SUSPEND_HIBERNATE,
};

void skl_enable_dc6(struct drm_i915_private *dev_priv);
void gen9_sanitize_dc_state(struct drm_i915_private *dev_priv);
void bxt_enable_dc9(struct drm_i915_private *dev_priv);
void bxt_disable_dc9(struct drm_i915_private *dev_priv);
void gen9_enable_dc5(struct drm_i915_private *dev_priv);

void intel_runtime_pm_init_early(struct drm_i915_private *dev_priv);
int intel_power_domains_init(struct drm_i915_private *);
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
void intel_runtime_pm_enable(struct drm_i915_private *dev_priv);
void intel_runtime_pm_disable(struct drm_i915_private *dev_priv);
void intel_runtime_pm_cleanup(struct drm_i915_private *dev_priv);

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
#if IS_ENABLED(CONFIG_DRM_I915_DEBUG_RUNTIME_PM)
void intel_display_power_put(struct drm_i915_private *dev_priv,
			     enum intel_display_power_domain domain,
			     intel_wakeref_t wakeref);
#else
#define intel_display_power_put(i915, domain, wakeref) \
	intel_display_power_put_unchecked(i915, domain)
#endif
void icl_dbuf_slices_update(struct drm_i915_private *dev_priv,
			    u8 req_slices);

intel_wakeref_t intel_runtime_pm_get(struct drm_i915_private *i915);
intel_wakeref_t intel_runtime_pm_get_if_in_use(struct drm_i915_private *i915);
intel_wakeref_t intel_runtime_pm_get_noresume(struct drm_i915_private *i915);

#define with_intel_runtime_pm(i915, wf) \
	for ((wf) = intel_runtime_pm_get(i915); (wf); \
	     intel_runtime_pm_put((i915), (wf)), (wf) = 0)

#define with_intel_runtime_pm_if_in_use(i915, wf) \
	for ((wf) = intel_runtime_pm_get_if_in_use(i915); (wf); \
	     intel_runtime_pm_put((i915), (wf)), (wf) = 0)

void intel_runtime_pm_put_unchecked(struct drm_i915_private *i915);
#if IS_ENABLED(CONFIG_DRM_I915_DEBUG_RUNTIME_PM)
void intel_runtime_pm_put(struct drm_i915_private *i915, intel_wakeref_t wref);
#else
#define intel_runtime_pm_put(i915, wref) intel_runtime_pm_put_unchecked(i915)
#endif

#if IS_ENABLED(CONFIG_DRM_I915_DEBUG_RUNTIME_PM)
void print_intel_runtime_pm_wakeref(struct drm_i915_private *i915,
				    struct drm_printer *p);
#else
static inline void print_intel_runtime_pm_wakeref(struct drm_i915_private *i915,
						  struct drm_printer *p)
{
}
#endif

void chv_phy_powergate_lanes(struct intel_encoder *encoder,
			     bool override, unsigned int mask);
bool chv_phy_powergate_ch(struct drm_i915_private *dev_priv, enum dpio_phy phy,
			  enum dpio_channel ch, bool override);

#endif /* __INTEL_RUNTIME_PM_H__ */

/* SPDX-License-Identifier: MIT */

#ifndef _INTEL_SIDEBAND_H_
#define _INTEL_SIDEBAND_H_

#include <linux/types.h>

struct drm_i915_private;

enum intel_sbi_destination {
	SBI_ICLK,
	SBI_MPHY,
};

u32 intel_sbi_read(struct drm_i915_private *i915, u16 reg,
		   enum intel_sbi_destination destination);
void intel_sbi_write(struct drm_i915_private *i915, u16 reg, u32 value,
		     enum intel_sbi_destination destination);

#endif /* _INTEL_SIDEBAND_H */

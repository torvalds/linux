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

int sandybridge_pcode_read(struct drm_i915_private *i915, u32 mbox,
			   u32 *val, u32 *val1);
int sandybridge_pcode_write_timeout(struct drm_i915_private *i915, u32 mbox,
				    u32 val, int fast_timeout_us,
				    int slow_timeout_ms);
#define sandybridge_pcode_write(i915, mbox, val)	\
	sandybridge_pcode_write_timeout(i915, mbox, val, 500, 0)

int skl_pcode_request(struct drm_i915_private *i915, u32 mbox, u32 request,
		      u32 reply_mask, u32 reply, int timeout_base_ms);

int intel_pcode_init(struct drm_i915_private *i915);

#endif /* _INTEL_SIDEBAND_H */

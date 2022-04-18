/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2013-2021 Intel Corporation
 */

#ifndef _INTEL_PCODE_H_
#define _INTEL_PCODE_H_

#include <linux/types.h>

struct drm_i915_private;

int snb_pcode_read(struct drm_i915_private *i915, u32 mbox, u32 *val, u32 *val1);
int snb_pcode_write_timeout(struct drm_i915_private *i915, u32 mbox, u32 val,
			    int fast_timeout_us, int slow_timeout_ms);
#define snb_pcode_write(i915, mbox, val)			\
	snb_pcode_write_timeout(i915, mbox, val, 500, 0)

int skl_pcode_request(struct drm_i915_private *i915, u32 mbox, u32 request,
		      u32 reply_mask, u32 reply, int timeout_base_ms);

int intel_pcode_init(struct drm_i915_private *i915);

#endif /* _INTEL_PCODE_H */

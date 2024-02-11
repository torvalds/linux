/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */

#ifndef _I915_GPU_ERROR_H_
#define _I915_GPU_ERROR_H_

struct drm_i915_error_state_buf;

__printf(2, 3)
static inline void
i915_error_printf(struct drm_i915_error_state_buf *e, const char *f, ...)
{
}

#endif

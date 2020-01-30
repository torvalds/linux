/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2019 Intel Corporation
 */

#ifndef __I915_MEMCPY_H__
#define __I915_MEMCPY_H__

#include <linux/types.h>

struct drm_i915_private;

void i915_memcpy_init_early(struct drm_i915_private *i915);

bool i915_memcpy_from_wc(void *dst, const void *src, unsigned long len);
void i915_unaligned_memcpy_from_wc(void *dst, void *src, unsigned long len);

/* The movntdqa instructions used for memcpy-from-wc require 16-byte alignment,
 * as well as SSE4.1 support. i915_memcpy_from_wc() will report if it cannot
 * perform the operation. To check beforehand, pass in the parameters to
 * to i915_can_memcpy_from_wc() - since we only care about the low 4 bits,
 * you only need to pass in the minor offsets, page-aligned pointers are
 * always valid.
 *
 * For just checking for SSE4.1, in the foreknowledge that the future use
 * will be correctly aligned, just use i915_has_memcpy_from_wc().
 */
#define i915_can_memcpy_from_wc(dst, src, len) \
	i915_memcpy_from_wc((void *)((unsigned long)(dst) | (unsigned long)(src) | (len)), NULL, 0)

#define i915_has_memcpy_from_wc() \
	i915_memcpy_from_wc(NULL, NULL, 0)

#endif /* __I915_MEMCPY_H__ */

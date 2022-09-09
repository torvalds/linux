/*
 * Copyright(c) 2011-2016 Intel Corporation. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef _INTEL_GVT_H_
#define _INTEL_GVT_H_

#include <linux/types.h>

struct drm_i915_private;

#ifdef CONFIG_DRM_I915_GVT

struct intel_gvt_mmio_table_iter {
	struct drm_i915_private *i915;
	void *data;
	int (*handle_mmio_cb)(struct intel_gvt_mmio_table_iter *iter,
			      u32 offset, u32 size);
};

int intel_gvt_init(struct drm_i915_private *dev_priv);
void intel_gvt_driver_remove(struct drm_i915_private *dev_priv);
int intel_gvt_init_host(void);
void intel_gvt_resume(struct drm_i915_private *dev_priv);
int intel_gvt_iterate_mmio_table(struct intel_gvt_mmio_table_iter *iter);

struct intel_vgpu_ops {
	int (*init_device)(struct drm_i915_private *dev_priv);
	void (*clean_device)(struct drm_i915_private *dev_priv);
	void (*pm_resume)(struct drm_i915_private *i915);
};

int intel_gvt_set_ops(const struct intel_vgpu_ops *ops);
void intel_gvt_clear_ops(const struct intel_vgpu_ops *ops);

#else
static inline int intel_gvt_init(struct drm_i915_private *dev_priv)
{
	return 0;
}

static inline void intel_gvt_driver_remove(struct drm_i915_private *dev_priv)
{
}

static inline void intel_gvt_resume(struct drm_i915_private *dev_priv)
{
}

struct intel_gvt_mmio_table_iter {
};

static inline int intel_gvt_iterate_mmio_table(struct intel_gvt_mmio_table_iter *iter)
{
	return 0;
}
#endif

#endif /* _INTEL_GVT_H_ */

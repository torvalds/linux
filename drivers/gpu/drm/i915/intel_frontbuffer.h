/*
 * Copyright (c) 2014-2016 Intel Corporation
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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#ifndef __INTEL_FRONTBUFFER_H__
#define __INTEL_FRONTBUFFER_H__

struct drm_i915_private;
struct drm_i915_gem_object;

void intel_frontbuffer_flip_prepare(struct drm_i915_private *dev_priv,
				    unsigned frontbuffer_bits);
void intel_frontbuffer_flip_complete(struct drm_i915_private *dev_priv,
				     unsigned frontbuffer_bits);
void intel_frontbuffer_flip(struct drm_i915_private *dev_priv,
			    unsigned frontbuffer_bits);

void __intel_fb_obj_invalidate(struct drm_i915_gem_object *obj,
			       enum fb_op_origin origin,
			       unsigned int frontbuffer_bits);
void __intel_fb_obj_flush(struct drm_i915_gem_object *obj,
			  bool retire,
			  enum fb_op_origin origin,
			  unsigned int frontbuffer_bits);

/**
 * intel_fb_obj_invalidate - invalidate frontbuffer object
 * @obj: GEM object to invalidate
 * @origin: which operation caused the invalidation
 *
 * This function gets called every time rendering on the given object starts and
 * frontbuffer caching (fbc, low refresh rate for DRRS, panel self refresh) must
 * be invalidated. For ORIGIN_CS any subsequent invalidation will be delayed
 * until the rendering completes or a flip on this frontbuffer plane is
 * scheduled.
 */
static inline bool intel_fb_obj_invalidate(struct drm_i915_gem_object *obj,
					   enum fb_op_origin origin)
{
	unsigned int frontbuffer_bits;

	frontbuffer_bits = atomic_read(&obj->frontbuffer_bits);
	if (!frontbuffer_bits)
		return false;

	__intel_fb_obj_invalidate(obj, origin, frontbuffer_bits);
	return true;
}

/**
 * intel_fb_obj_flush - flush frontbuffer object
 * @obj: GEM object to flush
 * @retire: set when retiring asynchronous rendering
 * @origin: which operation caused the flush
 *
 * This function gets called every time rendering on the given object has
 * completed and frontbuffer caching can be started again. If @retire is true
 * then any delayed flushes will be unblocked.
 */
static inline void intel_fb_obj_flush(struct drm_i915_gem_object *obj,
				      bool retire,
				      enum fb_op_origin origin)
{
	unsigned int frontbuffer_bits;

	frontbuffer_bits = atomic_read(&obj->frontbuffer_bits);
	if (!frontbuffer_bits)
		return;

	__intel_fb_obj_flush(obj, retire, origin, frontbuffer_bits);
}

#endif /* __INTEL_FRONTBUFFER_H__ */

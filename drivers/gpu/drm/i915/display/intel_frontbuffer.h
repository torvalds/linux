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

#include <linux/atomic.h>
#include <linux/bits.h>
#include <linux/kref.h>

#include "i915_active_types.h"

struct drm_i915_private;

enum fb_op_origin {
	ORIGIN_CPU = 0,
	ORIGIN_CS,
	ORIGIN_FLIP,
	ORIGIN_DIRTYFB,
	ORIGIN_CURSOR_UPDATE,
};

struct intel_frontbuffer {
	struct kref ref;
	atomic_t bits;
	struct i915_active write;
	struct drm_i915_gem_object *obj;
	struct rcu_head rcu;
};

/*
 * Frontbuffer tracking bits. Set in obj->frontbuffer_bits while a gem bo is
 * considered to be the frontbuffer for the given plane interface-wise. This
 * doesn't mean that the hw necessarily already scans it out, but that any
 * rendering (by the cpu or gpu) will land in the frontbuffer eventually.
 *
 * We have one bit per pipe and per scanout plane type.
 */
#define INTEL_FRONTBUFFER_BITS_PER_PIPE 8
#define INTEL_FRONTBUFFER(pipe, plane_id) \
	BIT((plane_id) + INTEL_FRONTBUFFER_BITS_PER_PIPE * (pipe));
#define INTEL_FRONTBUFFER_OVERLAY(pipe) \
	BIT(INTEL_FRONTBUFFER_BITS_PER_PIPE - 1 + INTEL_FRONTBUFFER_BITS_PER_PIPE * (pipe))
#define INTEL_FRONTBUFFER_ALL_MASK(pipe) \
	GENMASK(INTEL_FRONTBUFFER_BITS_PER_PIPE * ((pipe) + 1) - 1,	\
		INTEL_FRONTBUFFER_BITS_PER_PIPE * (pipe))

void intel_frontbuffer_flip_prepare(struct drm_i915_private *i915,
				    unsigned frontbuffer_bits);
void intel_frontbuffer_flip_complete(struct drm_i915_private *i915,
				     unsigned frontbuffer_bits);
void intel_frontbuffer_flip(struct drm_i915_private *i915,
			    unsigned frontbuffer_bits);

void intel_frontbuffer_put(struct intel_frontbuffer *front);

struct intel_frontbuffer *
intel_frontbuffer_get(struct drm_i915_gem_object *obj);

void __intel_fb_invalidate(struct intel_frontbuffer *front,
			   enum fb_op_origin origin,
			   unsigned int frontbuffer_bits);

/**
 * intel_frontbuffer_invalidate - invalidate frontbuffer object
 * @front: GEM object to invalidate
 * @origin: which operation caused the invalidation
 *
 * This function gets called every time rendering on the given object starts and
 * frontbuffer caching (fbc, low refresh rate for DRRS, panel self refresh) must
 * be invalidated. For ORIGIN_CS any subsequent invalidation will be delayed
 * until the rendering completes or a flip on this frontbuffer plane is
 * scheduled.
 */
static inline bool intel_frontbuffer_invalidate(struct intel_frontbuffer *front,
						enum fb_op_origin origin)
{
	unsigned int frontbuffer_bits;

	if (!front)
		return false;

	frontbuffer_bits = atomic_read(&front->bits);
	if (!frontbuffer_bits)
		return false;

	__intel_fb_invalidate(front, origin, frontbuffer_bits);
	return true;
}

void __intel_fb_flush(struct intel_frontbuffer *front,
		      enum fb_op_origin origin,
		      unsigned int frontbuffer_bits);

/**
 * intel_frontbuffer_flush - flush frontbuffer object
 * @front: GEM object to flush
 * @origin: which operation caused the flush
 *
 * This function gets called every time rendering on the given object has
 * completed and frontbuffer caching can be started again.
 */
static inline void intel_frontbuffer_flush(struct intel_frontbuffer *front,
					   enum fb_op_origin origin)
{
	unsigned int frontbuffer_bits;

	if (!front)
		return;

	frontbuffer_bits = atomic_read(&front->bits);
	if (!frontbuffer_bits)
		return;

	__intel_fb_flush(front, origin, frontbuffer_bits);
}

void intel_frontbuffer_track(struct intel_frontbuffer *old,
			     struct intel_frontbuffer *new,
			     unsigned int frontbuffer_bits);

#endif /* __INTEL_FRONTBUFFER_H__ */

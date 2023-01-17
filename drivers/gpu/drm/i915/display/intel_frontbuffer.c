/*
 * Copyright © 2014 Intel Corporation
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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *	Daniel Vetter <daniel.vetter@ffwll.ch>
 */

/**
 * DOC: frontbuffer tracking
 *
 * Many features require us to track changes to the currently active
 * frontbuffer, especially rendering targeted at the frontbuffer.
 *
 * To be able to do so we track frontbuffers using a bitmask for all possible
 * frontbuffer slots through intel_frontbuffer_track(). The functions in this
 * file are then called when the contents of the frontbuffer are invalidated,
 * when frontbuffer rendering has stopped again to flush out all the changes
 * and when the frontbuffer is exchanged with a flip. Subsystems interested in
 * frontbuffer changes (e.g. PSR, FBC, DRRS) should directly put their callbacks
 * into the relevant places and filter for the frontbuffer slots that they are
 * interested int.
 *
 * On a high level there are two types of powersaving features. The first one
 * work like a special cache (FBC and PSR) and are interested when they should
 * stop caching and when to restart caching. This is done by placing callbacks
 * into the invalidate and the flush functions: At invalidate the caching must
 * be stopped and at flush time it can be restarted. And maybe they need to know
 * when the frontbuffer changes (e.g. when the hw doesn't initiate an invalidate
 * and flush on its own) which can be achieved with placing callbacks into the
 * flip functions.
 *
 * The other type of display power saving feature only cares about busyness
 * (e.g. DRRS). In that case all three (invalidate, flush and flip) indicate
 * busyness. There is no direct way to detect idleness. Instead an idle timer
 * work delayed work should be started from the flush and flip functions and
 * cancelled as soon as busyness is detected.
 */

#include "i915_drv.h"
#include "intel_display_trace.h"
#include "intel_display_types.h"
#include "intel_dp.h"
#include "intel_drrs.h"
#include "intel_fbc.h"
#include "intel_frontbuffer.h"
#include "intel_psr.h"

/**
 * frontbuffer_flush - flush frontbuffer
 * @i915: i915 device
 * @frontbuffer_bits: frontbuffer plane tracking bits
 * @origin: which operation caused the flush
 *
 * This function gets called every time rendering on the given planes has
 * completed and frontbuffer caching can be started again. Flushes will get
 * delayed if they're blocked by some outstanding asynchronous rendering.
 *
 * Can be called without any locks held.
 */
static void frontbuffer_flush(struct drm_i915_private *i915,
			      unsigned int frontbuffer_bits,
			      enum fb_op_origin origin)
{
	/* Delay flushing when rings are still busy.*/
	spin_lock(&i915->display.fb_tracking.lock);
	frontbuffer_bits &= ~i915->display.fb_tracking.busy_bits;
	spin_unlock(&i915->display.fb_tracking.lock);

	if (!frontbuffer_bits)
		return;

	trace_intel_frontbuffer_flush(i915, frontbuffer_bits, origin);

	might_sleep();
	intel_drrs_flush(i915, frontbuffer_bits);
	intel_psr_flush(i915, frontbuffer_bits, origin);
	intel_fbc_flush(i915, frontbuffer_bits, origin);
}

/**
 * intel_frontbuffer_flip_prepare - prepare asynchronous frontbuffer flip
 * @i915: i915 device
 * @frontbuffer_bits: frontbuffer plane tracking bits
 *
 * This function gets called after scheduling a flip on @obj. The actual
 * frontbuffer flushing will be delayed until completion is signalled with
 * intel_frontbuffer_flip_complete. If an invalidate happens in between this
 * flush will be cancelled.
 *
 * Can be called without any locks held.
 */
void intel_frontbuffer_flip_prepare(struct drm_i915_private *i915,
				    unsigned frontbuffer_bits)
{
	spin_lock(&i915->display.fb_tracking.lock);
	i915->display.fb_tracking.flip_bits |= frontbuffer_bits;
	/* Remove stale busy bits due to the old buffer. */
	i915->display.fb_tracking.busy_bits &= ~frontbuffer_bits;
	spin_unlock(&i915->display.fb_tracking.lock);
}

/**
 * intel_frontbuffer_flip_complete - complete asynchronous frontbuffer flip
 * @i915: i915 device
 * @frontbuffer_bits: frontbuffer plane tracking bits
 *
 * This function gets called after the flip has been latched and will complete
 * on the next vblank. It will execute the flush if it hasn't been cancelled yet.
 *
 * Can be called without any locks held.
 */
void intel_frontbuffer_flip_complete(struct drm_i915_private *i915,
				     unsigned frontbuffer_bits)
{
	spin_lock(&i915->display.fb_tracking.lock);
	/* Mask any cancelled flips. */
	frontbuffer_bits &= i915->display.fb_tracking.flip_bits;
	i915->display.fb_tracking.flip_bits &= ~frontbuffer_bits;
	spin_unlock(&i915->display.fb_tracking.lock);

	if (frontbuffer_bits)
		frontbuffer_flush(i915, frontbuffer_bits, ORIGIN_FLIP);
}

/**
 * intel_frontbuffer_flip - synchronous frontbuffer flip
 * @i915: i915 device
 * @frontbuffer_bits: frontbuffer plane tracking bits
 *
 * This function gets called after scheduling a flip on @obj. This is for
 * synchronous plane updates which will happen on the next vblank and which will
 * not get delayed by pending gpu rendering.
 *
 * Can be called without any locks held.
 */
void intel_frontbuffer_flip(struct drm_i915_private *i915,
			    unsigned frontbuffer_bits)
{
	spin_lock(&i915->display.fb_tracking.lock);
	/* Remove stale busy bits due to the old buffer. */
	i915->display.fb_tracking.busy_bits &= ~frontbuffer_bits;
	spin_unlock(&i915->display.fb_tracking.lock);

	frontbuffer_flush(i915, frontbuffer_bits, ORIGIN_FLIP);
}

void __intel_fb_invalidate(struct intel_frontbuffer *front,
			   enum fb_op_origin origin,
			   unsigned int frontbuffer_bits)
{
	struct drm_i915_private *i915 = to_i915(front->obj->base.dev);

	if (origin == ORIGIN_CS) {
		spin_lock(&i915->display.fb_tracking.lock);
		i915->display.fb_tracking.busy_bits |= frontbuffer_bits;
		i915->display.fb_tracking.flip_bits &= ~frontbuffer_bits;
		spin_unlock(&i915->display.fb_tracking.lock);
	}

	trace_intel_frontbuffer_invalidate(i915, frontbuffer_bits, origin);

	might_sleep();
	intel_psr_invalidate(i915, frontbuffer_bits, origin);
	intel_drrs_invalidate(i915, frontbuffer_bits);
	intel_fbc_invalidate(i915, frontbuffer_bits, origin);
}

void __intel_fb_flush(struct intel_frontbuffer *front,
		      enum fb_op_origin origin,
		      unsigned int frontbuffer_bits)
{
	struct drm_i915_private *i915 = to_i915(front->obj->base.dev);

	if (origin == ORIGIN_CS) {
		spin_lock(&i915->display.fb_tracking.lock);
		/* Filter out new bits since rendering started. */
		frontbuffer_bits &= i915->display.fb_tracking.busy_bits;
		i915->display.fb_tracking.busy_bits &= ~frontbuffer_bits;
		spin_unlock(&i915->display.fb_tracking.lock);
	}

	if (frontbuffer_bits)
		frontbuffer_flush(i915, frontbuffer_bits, origin);
}

static int frontbuffer_active(struct i915_active *ref)
{
	struct intel_frontbuffer *front =
		container_of(ref, typeof(*front), write);

	kref_get(&front->ref);
	return 0;
}

static void frontbuffer_retire(struct i915_active *ref)
{
	struct intel_frontbuffer *front =
		container_of(ref, typeof(*front), write);

	intel_frontbuffer_flush(front, ORIGIN_CS);
	intel_frontbuffer_put(front);
}

static void frontbuffer_release(struct kref *ref)
	__releases(&to_i915(front->obj->base.dev)->display.fb_tracking.lock)
{
	struct intel_frontbuffer *front =
		container_of(ref, typeof(*front), ref);
	struct drm_i915_gem_object *obj = front->obj;
	struct i915_vma *vma;

	drm_WARN_ON(obj->base.dev, atomic_read(&front->bits));

	spin_lock(&obj->vma.lock);
	for_each_ggtt_vma(vma, obj) {
		i915_vma_clear_scanout(vma);
		vma->display_alignment = I915_GTT_MIN_ALIGNMENT;
	}
	spin_unlock(&obj->vma.lock);

	RCU_INIT_POINTER(obj->frontbuffer, NULL);
	spin_unlock(&to_i915(obj->base.dev)->display.fb_tracking.lock);

	i915_active_fini(&front->write);

	i915_gem_object_put(obj);
	kfree_rcu(front, rcu);
}

struct intel_frontbuffer *
intel_frontbuffer_get(struct drm_i915_gem_object *obj)
{
	struct drm_i915_private *i915 = to_i915(obj->base.dev);
	struct intel_frontbuffer *front;

	front = __intel_frontbuffer_get(obj);
	if (front)
		return front;

	front = kmalloc(sizeof(*front), GFP_KERNEL);
	if (!front)
		return NULL;

	front->obj = obj;
	kref_init(&front->ref);
	atomic_set(&front->bits, 0);
	i915_active_init(&front->write,
			 frontbuffer_active,
			 frontbuffer_retire,
			 I915_ACTIVE_RETIRE_SLEEPS);

	spin_lock(&i915->display.fb_tracking.lock);
	if (rcu_access_pointer(obj->frontbuffer)) {
		kfree(front);
		front = rcu_dereference_protected(obj->frontbuffer, true);
		kref_get(&front->ref);
	} else {
		i915_gem_object_get(obj);
		rcu_assign_pointer(obj->frontbuffer, front);
	}
	spin_unlock(&i915->display.fb_tracking.lock);

	return front;
}

void intel_frontbuffer_put(struct intel_frontbuffer *front)
{
	kref_put_lock(&front->ref,
		      frontbuffer_release,
		      &to_i915(front->obj->base.dev)->display.fb_tracking.lock);
}

/**
 * intel_frontbuffer_track - update frontbuffer tracking
 * @old: current buffer for the frontbuffer slots
 * @new: new buffer for the frontbuffer slots
 * @frontbuffer_bits: bitmask of frontbuffer slots
 *
 * This updates the frontbuffer tracking bits @frontbuffer_bits by clearing them
 * from @old and setting them in @new. Both @old and @new can be NULL.
 */
void intel_frontbuffer_track(struct intel_frontbuffer *old,
			     struct intel_frontbuffer *new,
			     unsigned int frontbuffer_bits)
{
	/*
	 * Control of individual bits within the mask are guarded by
	 * the owning plane->mutex, i.e. we can never see concurrent
	 * manipulation of individual bits. But since the bitfield as a whole
	 * is updated using RMW, we need to use atomics in order to update
	 * the bits.
	 */
	BUILD_BUG_ON(INTEL_FRONTBUFFER_BITS_PER_PIPE * I915_MAX_PIPES >
		     BITS_PER_TYPE(atomic_t));
	BUILD_BUG_ON(INTEL_FRONTBUFFER_BITS_PER_PIPE * I915_MAX_PIPES > 32);
	BUILD_BUG_ON(I915_MAX_PLANES > INTEL_FRONTBUFFER_BITS_PER_PIPE);

	if (old) {
		drm_WARN_ON(old->obj->base.dev,
			    !(atomic_read(&old->bits) & frontbuffer_bits));
		atomic_andnot(frontbuffer_bits, &old->bits);
	}

	if (new) {
		drm_WARN_ON(new->obj->base.dev,
			    atomic_read(&new->bits) & frontbuffer_bits);
		atomic_or(frontbuffer_bits, &new->bits);
	}
}

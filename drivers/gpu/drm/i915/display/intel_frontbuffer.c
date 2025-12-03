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

#include <drm/drm_gem.h>
#include <drm/drm_print.h>

#include "intel_bo.h"
#include "intel_display_trace.h"
#include "intel_display_types.h"
#include "intel_dp.h"
#include "intel_drrs.h"
#include "intel_fbc.h"
#include "intel_frontbuffer.h"
#include "intel_psr.h"
#include "intel_tdf.h"

/**
 * frontbuffer_flush - flush frontbuffer
 * @display: display device
 * @frontbuffer_bits: frontbuffer plane tracking bits
 * @origin: which operation caused the flush
 *
 * This function gets called every time rendering on the given planes has
 * completed and frontbuffer caching can be started again. Flushes will get
 * delayed if they're blocked by some outstanding asynchronous rendering.
 *
 * Can be called without any locks held.
 */
static void frontbuffer_flush(struct intel_display *display,
			      unsigned int frontbuffer_bits,
			      enum fb_op_origin origin)
{
	/* Delay flushing when rings are still busy.*/
	spin_lock(&display->fb_tracking.lock);
	frontbuffer_bits &= ~display->fb_tracking.busy_bits;
	spin_unlock(&display->fb_tracking.lock);

	if (!frontbuffer_bits)
		return;

	trace_intel_frontbuffer_flush(display, frontbuffer_bits, origin);

	might_sleep();
	intel_td_flush(display);
	intel_drrs_flush(display, frontbuffer_bits);
	intel_psr_flush(display, frontbuffer_bits, origin);
	intel_fbc_flush(display, frontbuffer_bits, origin);
}

/**
 * intel_frontbuffer_flip - synchronous frontbuffer flip
 * @display: display device
 * @frontbuffer_bits: frontbuffer plane tracking bits
 *
 * This function gets called after scheduling a flip on @obj. This is for
 * synchronous plane updates which will happen on the next vblank and which will
 * not get delayed by pending gpu rendering.
 *
 * Can be called without any locks held.
 */
void intel_frontbuffer_flip(struct intel_display *display,
			    unsigned frontbuffer_bits)
{
	spin_lock(&display->fb_tracking.lock);
	/* Remove stale busy bits due to the old buffer. */
	display->fb_tracking.busy_bits &= ~frontbuffer_bits;
	spin_unlock(&display->fb_tracking.lock);

	frontbuffer_flush(display, frontbuffer_bits, ORIGIN_FLIP);
}

void __intel_fb_invalidate(struct intel_frontbuffer *front,
			   enum fb_op_origin origin,
			   unsigned int frontbuffer_bits)
{
	struct intel_display *display = front->display;

	if (origin == ORIGIN_CS) {
		spin_lock(&display->fb_tracking.lock);
		display->fb_tracking.busy_bits |= frontbuffer_bits;
		spin_unlock(&display->fb_tracking.lock);
	}

	trace_intel_frontbuffer_invalidate(display, frontbuffer_bits, origin);

	might_sleep();
	intel_psr_invalidate(display, frontbuffer_bits, origin);
	intel_drrs_invalidate(display, frontbuffer_bits);
	intel_fbc_invalidate(display, frontbuffer_bits, origin);
}

void __intel_fb_flush(struct intel_frontbuffer *front,
		      enum fb_op_origin origin,
		      unsigned int frontbuffer_bits)
{
	struct intel_display *display = front->display;

	if (origin == ORIGIN_DIRTYFB)
		intel_bo_frontbuffer_flush_for_display(front);

	if (origin == ORIGIN_CS) {
		spin_lock(&display->fb_tracking.lock);
		/* Filter out new bits since rendering started. */
		frontbuffer_bits &= display->fb_tracking.busy_bits;
		display->fb_tracking.busy_bits &= ~frontbuffer_bits;
		spin_unlock(&display->fb_tracking.lock);
	}

	if (frontbuffer_bits)
		frontbuffer_flush(display, frontbuffer_bits, origin);
}

static void intel_frontbuffer_ref(struct intel_frontbuffer *front)
{
	intel_bo_frontbuffer_ref(front);
}

static void intel_frontbuffer_flush_work(struct work_struct *work)
{
	struct intel_frontbuffer *front =
		container_of(work, struct intel_frontbuffer, flush_work);

	intel_frontbuffer_flush(front, ORIGIN_DIRTYFB);
	intel_frontbuffer_put(front);
}

/**
 * intel_frontbuffer_queue_flush - queue flushing frontbuffer object
 * @front: GEM object to flush
 *
 * This function is targeted for our dirty callback for queueing flush when
 * dma fence is signals
 */
void intel_frontbuffer_queue_flush(struct intel_frontbuffer *front)
{
	if (!front)
		return;

	intel_frontbuffer_ref(front);
	if (!schedule_work(&front->flush_work))
		intel_frontbuffer_put(front);
}

void intel_frontbuffer_init(struct intel_frontbuffer *front, struct drm_device *drm)
{
	front->display = to_intel_display(drm);
	atomic_set(&front->bits, 0);
	INIT_WORK(&front->flush_work, intel_frontbuffer_flush_work);
}

void intel_frontbuffer_fini(struct intel_frontbuffer *front)
{
	drm_WARN_ON(front->display->drm, atomic_read(&front->bits));
}

struct intel_frontbuffer *intel_frontbuffer_get(struct drm_gem_object *obj)
{
	return intel_bo_frontbuffer_get(obj);
}

void intel_frontbuffer_put(struct intel_frontbuffer *front)
{
	intel_bo_frontbuffer_put(front);
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
		drm_WARN_ON(old->display->drm,
			    !(atomic_read(&old->bits) & frontbuffer_bits));
		atomic_andnot(frontbuffer_bits, &old->bits);
	}

	if (new) {
		drm_WARN_ON(new->display->drm,
			    atomic_read(&new->bits) & frontbuffer_bits);
		atomic_or(frontbuffer_bits, &new->bits);
	}
}

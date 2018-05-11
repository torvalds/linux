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
 * To be able to do so GEM tracks frontbuffers using a bitmask for all possible
 * frontbuffer slots through i915_gem_track_fb(). The function in this file are
 * then called when the contents of the frontbuffer are invalidated, when
 * frontbuffer rendering has stopped again to flush out all the changes and when
 * the frontbuffer is exchanged with a flip. Subsystems interested in
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
 *
 * Note that there's also an older frontbuffer activity tracking scheme which
 * just tracks general activity. This is done by the various mark_busy and
 * mark_idle functions. For display power management features using these
 * functions is deprecated and should be avoided.
 */

#include <drm/drmP.h>

#include "intel_drv.h"
#include "intel_frontbuffer.h"
#include "i915_drv.h"

void __intel_fb_obj_invalidate(struct drm_i915_gem_object *obj,
			       enum fb_op_origin origin,
			       unsigned int frontbuffer_bits)
{
	struct drm_i915_private *dev_priv = to_i915(obj->base.dev);

	if (origin == ORIGIN_CS) {
		spin_lock(&dev_priv->fb_tracking.lock);
		dev_priv->fb_tracking.busy_bits |= frontbuffer_bits;
		dev_priv->fb_tracking.flip_bits &= ~frontbuffer_bits;
		spin_unlock(&dev_priv->fb_tracking.lock);
	}

	might_sleep();
	intel_psr_invalidate(dev_priv, frontbuffer_bits, origin);
	intel_edp_drrs_invalidate(dev_priv, frontbuffer_bits);
	intel_fbc_invalidate(dev_priv, frontbuffer_bits, origin);
}

/**
 * intel_frontbuffer_flush - flush frontbuffer
 * @dev_priv: i915 device
 * @frontbuffer_bits: frontbuffer plane tracking bits
 * @origin: which operation caused the flush
 *
 * This function gets called every time rendering on the given planes has
 * completed and frontbuffer caching can be started again. Flushes will get
 * delayed if they're blocked by some outstanding asynchronous rendering.
 *
 * Can be called without any locks held.
 */
static void intel_frontbuffer_flush(struct drm_i915_private *dev_priv,
				    unsigned frontbuffer_bits,
				    enum fb_op_origin origin)
{
	/* Delay flushing when rings are still busy.*/
	spin_lock(&dev_priv->fb_tracking.lock);
	frontbuffer_bits &= ~dev_priv->fb_tracking.busy_bits;
	spin_unlock(&dev_priv->fb_tracking.lock);

	if (!frontbuffer_bits)
		return;

	might_sleep();
	intel_edp_drrs_flush(dev_priv, frontbuffer_bits);
	intel_psr_flush(dev_priv, frontbuffer_bits, origin);
	intel_fbc_flush(dev_priv, frontbuffer_bits, origin);
}

void __intel_fb_obj_flush(struct drm_i915_gem_object *obj,
			  enum fb_op_origin origin,
			  unsigned int frontbuffer_bits)
{
	struct drm_i915_private *dev_priv = to_i915(obj->base.dev);

	if (origin == ORIGIN_CS) {
		spin_lock(&dev_priv->fb_tracking.lock);
		/* Filter out new bits since rendering started. */
		frontbuffer_bits &= dev_priv->fb_tracking.busy_bits;
		dev_priv->fb_tracking.busy_bits &= ~frontbuffer_bits;
		spin_unlock(&dev_priv->fb_tracking.lock);
	}

	if (frontbuffer_bits)
		intel_frontbuffer_flush(dev_priv, frontbuffer_bits, origin);
}

/**
 * intel_frontbuffer_flip_prepare - prepare asynchronous frontbuffer flip
 * @dev_priv: i915 device
 * @frontbuffer_bits: frontbuffer plane tracking bits
 *
 * This function gets called after scheduling a flip on @obj. The actual
 * frontbuffer flushing will be delayed until completion is signalled with
 * intel_frontbuffer_flip_complete. If an invalidate happens in between this
 * flush will be cancelled.
 *
 * Can be called without any locks held.
 */
void intel_frontbuffer_flip_prepare(struct drm_i915_private *dev_priv,
				    unsigned frontbuffer_bits)
{
	spin_lock(&dev_priv->fb_tracking.lock);
	dev_priv->fb_tracking.flip_bits |= frontbuffer_bits;
	/* Remove stale busy bits due to the old buffer. */
	dev_priv->fb_tracking.busy_bits &= ~frontbuffer_bits;
	spin_unlock(&dev_priv->fb_tracking.lock);
}

/**
 * intel_frontbuffer_flip_complete - complete asynchronous frontbuffer flip
 * @dev_priv: i915 device
 * @frontbuffer_bits: frontbuffer plane tracking bits
 *
 * This function gets called after the flip has been latched and will complete
 * on the next vblank. It will execute the flush if it hasn't been cancelled yet.
 *
 * Can be called without any locks held.
 */
void intel_frontbuffer_flip_complete(struct drm_i915_private *dev_priv,
				     unsigned frontbuffer_bits)
{
	spin_lock(&dev_priv->fb_tracking.lock);
	/* Mask any cancelled flips. */
	frontbuffer_bits &= dev_priv->fb_tracking.flip_bits;
	dev_priv->fb_tracking.flip_bits &= ~frontbuffer_bits;
	spin_unlock(&dev_priv->fb_tracking.lock);

	if (frontbuffer_bits)
		intel_frontbuffer_flush(dev_priv,
					frontbuffer_bits, ORIGIN_FLIP);
}

/**
 * intel_frontbuffer_flip - synchronous frontbuffer flip
 * @dev_priv: i915 device
 * @frontbuffer_bits: frontbuffer plane tracking bits
 *
 * This function gets called after scheduling a flip on @obj. This is for
 * synchronous plane updates which will happen on the next vblank and which will
 * not get delayed by pending gpu rendering.
 *
 * Can be called without any locks held.
 */
void intel_frontbuffer_flip(struct drm_i915_private *dev_priv,
			    unsigned frontbuffer_bits)
{
	spin_lock(&dev_priv->fb_tracking.lock);
	/* Remove stale busy bits due to the old buffer. */
	dev_priv->fb_tracking.busy_bits &= ~frontbuffer_bits;
	spin_unlock(&dev_priv->fb_tracking.lock);

	intel_frontbuffer_flush(dev_priv, frontbuffer_bits, ORIGIN_FLIP);
}

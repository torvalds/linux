// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023 Intel Corporation
 */

#include <linux/dma-fence.h>

#include <drm/drm_crtc.h>
#include <drm/drm_vblank.h>

#include "i915_reg.h"
#include "intel_display_core.h"
#include "intel_display_irq.h"
#include "intel_display_rps.h"
#include "intel_display_types.h"
#include "intel_parent.h"

struct wait_rps_boost {
	struct wait_queue_entry wait;

	struct drm_crtc *crtc;
	struct dma_fence *fence;
};

static int do_rps_boost(struct wait_queue_entry *_wait,
			unsigned mode, int sync, void *key)
{
	struct wait_rps_boost *wait = container_of(_wait, typeof(*wait), wait);
	struct intel_display *display = to_intel_display(wait->crtc->dev);

	/*
	 * If we missed the vblank, but the request is already running it
	 * is reasonable to assume that it will complete before the next
	 * vblank without our intervention, so leave RPS alone if not started.
	 */
	intel_parent_rps_boost_if_not_started(display, wait->fence);

	dma_fence_put(wait->fence);

	drm_crtc_vblank_put(wait->crtc);

	list_del(&wait->wait.entry);
	kfree(wait);
	return 1;
}

void intel_display_rps_boost_after_vblank(struct drm_crtc *crtc,
					  struct dma_fence *fence)
{
	struct intel_display *display = to_intel_display(crtc->dev);
	struct wait_rps_boost *wait;

	if (!intel_parent_rps_available(display))
		return;

	if (DISPLAY_VER(display) < 6)
		return;

	if (drm_crtc_vblank_get(crtc))
		return;

	wait = kmalloc(sizeof(*wait), GFP_KERNEL);
	if (!wait) {
		drm_crtc_vblank_put(crtc);
		return;
	}

	wait->fence = dma_fence_get(fence);
	wait->crtc = crtc;

	wait->wait.func = do_rps_boost;
	wait->wait.flags = 0;

	add_wait_queue(drm_crtc_vblank_waitqueue(crtc), &wait->wait);
}

void intel_display_rps_mark_interactive(struct intel_display *display,
					struct intel_atomic_state *state,
					bool interactive)
{
	if (!intel_parent_rps_available(display))
		return;

	if (state->rps_interactive == interactive)
		return;

	intel_parent_rps_mark_interactive(display, interactive);

	state->rps_interactive = interactive;
}

void ilk_display_rps_enable(struct intel_display *display)
{
	spin_lock(&display->irq.lock);
	ilk_enable_display_irq(display, DE_PCU_EVENT);
	spin_unlock(&display->irq.lock);
}

void ilk_display_rps_disable(struct intel_display *display)
{
	spin_lock(&display->irq.lock);
	ilk_disable_display_irq(display, DE_PCU_EVENT);
	spin_unlock(&display->irq.lock);
}

void ilk_display_rps_irq_handler(struct intel_display *display)
{
	intel_parent_rps_ilk_irq_handler(display);
}

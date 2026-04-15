// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023 Intel Corporation
 */

#include "intel_de.h"
#include "intel_display_regs.h"
#include "intel_display_types.h"
#include "intel_dpt.h"
#include "intel_parent.h"
#include "skl_universal_plane_regs.h"

void intel_dpt_configure(struct intel_crtc *crtc)
{
	struct intel_display *display = to_intel_display(crtc);

	if (DISPLAY_VER(display) == 14) {
		enum pipe pipe = crtc->pipe;
		enum plane_id plane_id;

		for_each_plane_id_on_crtc(crtc, plane_id) {
			if (plane_id == PLANE_CURSOR)
				continue;

			intel_de_rmw(display, PLANE_CHICKEN(pipe, plane_id),
				     PLANE_CHICKEN_DISABLE_DPT,
				     display->params.enable_dpt ? 0 :
				     PLANE_CHICKEN_DISABLE_DPT);
		}
	} else if (DISPLAY_VER(display) == 13) {
		intel_de_rmw(display, CHICKEN_MISC_2,
			     CHICKEN_MISC_DISABLE_DPT,
			     display->params.enable_dpt ? 0 :
			     CHICKEN_MISC_DISABLE_DPT);
	}
}

/**
 * intel_dpt_suspend - suspend the memory mapping for all DPT FBs during system suspend
 * @display: display device instance
 *
 * Suspend the memory mapping during system suspend for all framebuffers which
 * are mapped to HW via a GGTT->DPT page table.
 *
 * This function must be called before the mappings in GGTT are suspended calling
 * i915_ggtt_suspend().
 */
void intel_dpt_suspend(struct intel_display *display)
{
	struct drm_framebuffer *drm_fb;

	if (!HAS_DISPLAY(display))
		return;

	mutex_lock(&display->drm->mode_config.fb_lock);

	drm_for_each_fb(drm_fb, display->drm) {
		struct intel_framebuffer *fb = to_intel_framebuffer(drm_fb);

		if (fb->dpt)
			intel_parent_dpt_suspend(display, fb->dpt);
	}

	mutex_unlock(&display->drm->mode_config.fb_lock);
}

/**
 * intel_dpt_resume - restore the memory mapping for all DPT FBs during system resume
 * @display: display device instance
 *
 * Restore the memory mapping during system resume for all framebuffers which
 * are mapped to HW via a GGTT->DPT page table. The content of these page
 * tables are not stored in the hibernation image during S4 and S3RST->S4
 * transitions, so here we reprogram the PTE entries in those tables.
 *
 * This function must be called after the mappings in GGTT have been restored calling
 * i915_ggtt_resume().
 */
void intel_dpt_resume(struct intel_display *display)
{
	struct drm_framebuffer *drm_fb;

	if (!HAS_DISPLAY(display))
		return;

	mutex_lock(&display->drm->mode_config.fb_lock);
	drm_for_each_fb(drm_fb, display->drm) {
		struct intel_framebuffer *fb = to_intel_framebuffer(drm_fb);

		if (fb->dpt)
			intel_parent_dpt_resume(display, fb->dpt);
	}
	mutex_unlock(&display->drm->mode_config.fb_lock);
}

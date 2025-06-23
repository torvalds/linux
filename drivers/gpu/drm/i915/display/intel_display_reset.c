// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023 Intel Corporation
 */

#include <drm/drm_atomic_helper.h>

#include "i915_drv.h"
#include "intel_clock_gating.h"
#include "intel_cx0_phy.h"
#include "intel_display_core.h"
#include "intel_display_driver.h"
#include "intel_display_reset.h"
#include "intel_display_types.h"
#include "intel_hotplug.h"
#include "intel_pps.h"

bool intel_display_reset_test(struct intel_display *display)
{
	return display->params.force_reset_modeset_test;
}

/* returns true if intel_display_reset_finish() needs to be called */
bool intel_display_reset_prepare(struct intel_display *display,
				 modeset_stuck_fn modeset_stuck, void *context)
{
	struct drm_modeset_acquire_ctx *ctx = &display->restore.reset_ctx;
	struct drm_atomic_state *state;
	int ret;

	if (!HAS_DISPLAY(display))
		return false;

	if (atomic_read(&display->restore.pending_fb_pin)) {
		drm_dbg_kms(display->drm,
			    "Modeset potentially stuck, unbreaking through wedging\n");
		modeset_stuck(context);
	}

	/*
	 * Need mode_config.mutex so that we don't
	 * trample ongoing ->detect() and whatnot.
	 */
	mutex_lock(&display->drm->mode_config.mutex);
	drm_modeset_acquire_init(ctx, 0);
	while (1) {
		ret = drm_modeset_lock_all_ctx(display->drm, ctx);
		if (ret != -EDEADLK)
			break;

		drm_modeset_backoff(ctx);
	}
	/*
	 * Disabling the crtcs gracefully seems nicer. Also the
	 * g33 docs say we should at least disable all the planes.
	 */
	state = drm_atomic_helper_duplicate_state(display->drm, ctx);
	if (IS_ERR(state)) {
		ret = PTR_ERR(state);
		drm_err(display->drm, "Duplicating state failed with %i\n",
			ret);
		return true;
	}

	ret = drm_atomic_helper_disable_all(display->drm, ctx);
	if (ret) {
		drm_err(display->drm, "Suspending crtc's failed with %i\n",
			ret);
		drm_atomic_state_put(state);
		return true;
	}

	display->restore.modeset_state = state;
	state->acquire_ctx = ctx;

	return true;
}

void intel_display_reset_finish(struct intel_display *display, bool test_only)
{
	struct drm_i915_private *i915 = to_i915(display->drm);
	struct drm_modeset_acquire_ctx *ctx = &display->restore.reset_ctx;
	struct drm_atomic_state *state;
	int ret;

	if (!HAS_DISPLAY(display))
		return;

	state = fetch_and_zero(&display->restore.modeset_state);
	if (!state)
		goto unlock;

	/* reset doesn't touch the display */
	if (test_only) {
		/* for testing only restore the display */
		ret = drm_atomic_helper_commit_duplicated_state(state, ctx);
		if (ret) {
			drm_WARN_ON(display->drm, ret == -EDEADLK);
			drm_err(display->drm,
				"Restoring old state failed with %i\n", ret);
		}
	} else {
		/*
		 * The display has been reset as well,
		 * so need a full re-initialization.
		 */
		intel_pps_unlock_regs_wa(display);
		intel_display_driver_init_hw(display);
		intel_clock_gating_init(i915);
		intel_cx0_pll_power_save_wa(display);
		intel_hpd_init(display);

		ret = __intel_display_driver_resume(display, state, ctx);
		if (ret)
			drm_err(display->drm,
				"Restoring old state failed with %i\n", ret);

		intel_hpd_poll_disable(display);
	}

	drm_atomic_state_put(state);
unlock:
	drm_modeset_drop_locks(ctx);
	drm_modeset_acquire_fini(ctx);
	mutex_unlock(&display->drm->mode_config.mutex);
}

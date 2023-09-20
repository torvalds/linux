// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023 Intel Corporation
 */

#include <drm/drm_atomic_helper.h>

#include "i915_drv.h"
#include "intel_clock_gating.h"
#include "intel_display_driver.h"
#include "intel_display_reset.h"
#include "intel_display_types.h"
#include "intel_hotplug.h"
#include "intel_pps.h"

static bool gpu_reset_clobbers_display(struct drm_i915_private *dev_priv)
{
	return (INTEL_INFO(dev_priv)->gpu_reset_clobbers_display &&
		intel_has_gpu_reset(to_gt(dev_priv)));
}

void intel_display_reset_prepare(struct drm_i915_private *dev_priv)
{
	struct drm_modeset_acquire_ctx *ctx = &dev_priv->display.restore.reset_ctx;
	struct drm_atomic_state *state;
	int ret;

	if (!HAS_DISPLAY(dev_priv))
		return;

	/* reset doesn't touch the display */
	if (!dev_priv->params.force_reset_modeset_test &&
	    !gpu_reset_clobbers_display(dev_priv))
		return;

	/* We have a modeset vs reset deadlock, defensively unbreak it. */
	set_bit(I915_RESET_MODESET, &to_gt(dev_priv)->reset.flags);
	smp_mb__after_atomic();
	wake_up_bit(&to_gt(dev_priv)->reset.flags, I915_RESET_MODESET);

	if (atomic_read(&dev_priv->gpu_error.pending_fb_pin)) {
		drm_dbg_kms(&dev_priv->drm,
			    "Modeset potentially stuck, unbreaking through wedging\n");
		intel_gt_set_wedged(to_gt(dev_priv));
	}

	/*
	 * Need mode_config.mutex so that we don't
	 * trample ongoing ->detect() and whatnot.
	 */
	mutex_lock(&dev_priv->drm.mode_config.mutex);
	drm_modeset_acquire_init(ctx, 0);
	while (1) {
		ret = drm_modeset_lock_all_ctx(&dev_priv->drm, ctx);
		if (ret != -EDEADLK)
			break;

		drm_modeset_backoff(ctx);
	}
	/*
	 * Disabling the crtcs gracefully seems nicer. Also the
	 * g33 docs say we should at least disable all the planes.
	 */
	state = drm_atomic_helper_duplicate_state(&dev_priv->drm, ctx);
	if (IS_ERR(state)) {
		ret = PTR_ERR(state);
		drm_err(&dev_priv->drm, "Duplicating state failed with %i\n",
			ret);
		return;
	}

	ret = drm_atomic_helper_disable_all(&dev_priv->drm, ctx);
	if (ret) {
		drm_err(&dev_priv->drm, "Suspending crtc's failed with %i\n",
			ret);
		drm_atomic_state_put(state);
		return;
	}

	dev_priv->display.restore.modeset_state = state;
	state->acquire_ctx = ctx;
}

void intel_display_reset_finish(struct drm_i915_private *i915)
{
	struct drm_modeset_acquire_ctx *ctx = &i915->display.restore.reset_ctx;
	struct drm_atomic_state *state;
	int ret;

	if (!HAS_DISPLAY(i915))
		return;

	/* reset doesn't touch the display */
	if (!test_bit(I915_RESET_MODESET, &to_gt(i915)->reset.flags))
		return;

	state = fetch_and_zero(&i915->display.restore.modeset_state);
	if (!state)
		goto unlock;

	/* reset doesn't touch the display */
	if (!gpu_reset_clobbers_display(i915)) {
		/* for testing only restore the display */
		ret = drm_atomic_helper_commit_duplicated_state(state, ctx);
		if (ret) {
			drm_WARN_ON(&i915->drm, ret == -EDEADLK);
			drm_err(&i915->drm,
				"Restoring old state failed with %i\n", ret);
		}
	} else {
		/*
		 * The display has been reset as well,
		 * so need a full re-initialization.
		 */
		intel_pps_unlock_regs_wa(i915);
		intel_display_driver_init_hw(i915);
		intel_clock_gating_init(i915);
		intel_hpd_init(i915);

		ret = __intel_display_driver_resume(i915, state, ctx);
		if (ret)
			drm_err(&i915->drm,
				"Restoring old state failed with %i\n", ret);

		intel_hpd_poll_disable(i915);
	}

	drm_atomic_state_put(state);
unlock:
	drm_modeset_drop_locks(ctx);
	drm_modeset_acquire_fini(ctx);
	mutex_unlock(&i915->drm.mode_config.mutex);

	clear_bit_unlock(I915_RESET_MODESET, &to_gt(i915)->reset.flags);
}

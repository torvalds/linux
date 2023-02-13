// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023 Intel Corporation
 */

#include "i915_drv.h"
#include "i9xx_wm.h"
#include "intel_display_types.h"
#include "intel_wm.h"
#include "skl_watermark.h"

/**
 * intel_update_watermarks - update FIFO watermark values based on current modes
 * @dev_priv: i915 device
 *
 * Calculate watermark values for the various WM regs based on current mode
 * and plane configuration.
 *
 * There are several cases to deal with here:
 *   - normal (i.e. non-self-refresh)
 *   - self-refresh (SR) mode
 *   - lines are large relative to FIFO size (buffer can hold up to 2)
 *   - lines are small relative to FIFO size (buffer can hold more than 2
 *     lines), so need to account for TLB latency
 *
 *   The normal calculation is:
 *     watermark = dotclock * bytes per pixel * latency
 *   where latency is platform & configuration dependent (we assume pessimal
 *   values here).
 *
 *   The SR calculation is:
 *     watermark = (trunc(latency/line time)+1) * surface width *
 *       bytes per pixel
 *   where
 *     line time = htotal / dotclock
 *     surface width = hdisplay for normal plane and 64 for cursor
 *   and latency is assumed to be high, as above.
 *
 * The final value programmed to the register should always be rounded up,
 * and include an extra 2 entries to account for clock crossings.
 *
 * We don't use the sprite, so we can ignore that.  And on Crestline we have
 * to set the non-SR watermarks to 8.
 */
void intel_update_watermarks(struct drm_i915_private *i915)
{
	if (i915->display.funcs.wm->update_wm)
		i915->display.funcs.wm->update_wm(i915);
}

int intel_compute_pipe_wm(struct intel_atomic_state *state,
			  struct intel_crtc *crtc)
{
	struct drm_i915_private *i915 = to_i915(state->base.dev);

	if (i915->display.funcs.wm->compute_pipe_wm)
		return i915->display.funcs.wm->compute_pipe_wm(state, crtc);

	return 0;
}

int intel_compute_intermediate_wm(struct intel_atomic_state *state,
				  struct intel_crtc *crtc)
{
	struct drm_i915_private *i915 = to_i915(state->base.dev);

	if (!i915->display.funcs.wm->compute_intermediate_wm)
		return 0;

	if (drm_WARN_ON(&i915->drm, !i915->display.funcs.wm->compute_pipe_wm))
		return 0;

	return i915->display.funcs.wm->compute_intermediate_wm(state, crtc);
}

bool intel_initial_watermarks(struct intel_atomic_state *state,
			      struct intel_crtc *crtc)
{
	struct drm_i915_private *i915 = to_i915(state->base.dev);

	if (i915->display.funcs.wm->initial_watermarks) {
		i915->display.funcs.wm->initial_watermarks(state, crtc);
		return true;
	}

	return false;
}

void intel_atomic_update_watermarks(struct intel_atomic_state *state,
				    struct intel_crtc *crtc)
{
	struct drm_i915_private *i915 = to_i915(state->base.dev);

	if (i915->display.funcs.wm->atomic_update_watermarks)
		i915->display.funcs.wm->atomic_update_watermarks(state, crtc);
}

void intel_optimize_watermarks(struct intel_atomic_state *state,
			       struct intel_crtc *crtc)
{
	struct drm_i915_private *i915 = to_i915(state->base.dev);

	if (i915->display.funcs.wm->optimize_watermarks)
		i915->display.funcs.wm->optimize_watermarks(state, crtc);
}

int intel_compute_global_watermarks(struct intel_atomic_state *state)
{
	struct drm_i915_private *i915 = to_i915(state->base.dev);

	if (i915->display.funcs.wm->compute_global_watermarks)
		return i915->display.funcs.wm->compute_global_watermarks(state);

	return 0;
}

void intel_wm_get_hw_state(struct drm_i915_private *i915)
{
	if (i915->display.funcs.wm->get_hw_state)
		return i915->display.funcs.wm->get_hw_state(i915);
}

bool intel_wm_plane_visible(const struct intel_crtc_state *crtc_state,
			    const struct intel_plane_state *plane_state)
{
	struct intel_plane *plane = to_intel_plane(plane_state->uapi.plane);

	/* FIXME check the 'enable' instead */
	if (!crtc_state->hw.active)
		return false;

	/*
	 * Treat cursor with fb as always visible since cursor updates
	 * can happen faster than the vrefresh rate, and the current
	 * watermark code doesn't handle that correctly. Cursor updates
	 * which set/clear the fb or change the cursor size are going
	 * to get throttled by intel_legacy_cursor_update() to work
	 * around this problem with the watermark code.
	 */
	if (plane->id == PLANE_CURSOR)
		return plane_state->hw.fb != NULL;
	else
		return plane_state->uapi.visible;
}

void intel_print_wm_latency(struct drm_i915_private *dev_priv,
			    const char *name, const u16 wm[])
{
	int level;

	for (level = 0; level < dev_priv->display.wm.num_levels; level++) {
		unsigned int latency = wm[level];

		if (latency == 0) {
			drm_dbg_kms(&dev_priv->drm,
				    "%s WM%d latency not provided\n",
				    name, level);
			continue;
		}

		/*
		 * - latencies are in us on gen9.
		 * - before then, WM1+ latency values are in 0.5us units
		 */
		if (DISPLAY_VER(dev_priv) >= 9)
			latency *= 10;
		else if (level > 0)
			latency *= 5;

		drm_dbg_kms(&dev_priv->drm,
			    "%s WM%d latency %u (%u.%u usec)\n", name, level,
			    wm[level], latency / 10, latency % 10);
	}
}

void intel_wm_init(struct drm_i915_private *i915)
{
	if (DISPLAY_VER(i915) >= 9)
		skl_wm_init(i915);
	else
		i9xx_wm_init(i915);
}

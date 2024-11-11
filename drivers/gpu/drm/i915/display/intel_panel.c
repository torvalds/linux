/*
 * Copyright © 2006-2010 Intel Corporation
 * Copyright (c) 2006 Dave Airlie <airlied@linux.ie>
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
 *	Eric Anholt <eric@anholt.net>
 *      Dave Airlie <airlied@linux.ie>
 *      Jesse Barnes <jesse.barnes@intel.com>
 *      Chris Wilson <chris@chris-wilson.co.uk>
 */

#include <linux/kernel.h>
#include <linux/pwm.h>

#include <drm/drm_edid.h>

#include "i915_drv.h"
#include "intel_backlight.h"
#include "intel_connector.h"
#include "intel_display_core.h"
#include "intel_display_driver.h"
#include "intel_display_types.h"
#include "intel_drrs.h"
#include "intel_panel.h"
#include "intel_quirks.h"
#include "intel_vrr.h"

bool intel_panel_use_ssc(struct intel_display *display)
{
	if (display->params.panel_use_ssc >= 0)
		return display->params.panel_use_ssc != 0;
	return display->vbt.lvds_use_ssc &&
		!intel_has_quirk(display, QUIRK_LVDS_SSC_DISABLE);
}

const struct drm_display_mode *
intel_panel_preferred_fixed_mode(struct intel_connector *connector)
{
	return list_first_entry_or_null(&connector->panel.fixed_modes,
					struct drm_display_mode, head);
}

static bool is_best_fixed_mode(struct intel_connector *connector,
			       int vrefresh, int fixed_mode_vrefresh,
			       const struct drm_display_mode *best_mode)
{
	/* we want to always return something */
	if (!best_mode)
		return true;

	/*
	 * With VRR always pick a mode with equal/higher than requested
	 * vrefresh, which we can then reduce to match the requested
	 * vrefresh by extending the vblank length.
	 */
	if (intel_vrr_is_in_range(connector, vrefresh) &&
	    intel_vrr_is_in_range(connector, fixed_mode_vrefresh) &&
	    fixed_mode_vrefresh < vrefresh)
		return false;

	/* pick the fixed_mode that is closest in terms of vrefresh */
	return abs(fixed_mode_vrefresh - vrefresh) <
		abs(drm_mode_vrefresh(best_mode) - vrefresh);
}

const struct drm_display_mode *
intel_panel_fixed_mode(struct intel_connector *connector,
		       const struct drm_display_mode *mode)
{
	const struct drm_display_mode *fixed_mode, *best_mode = NULL;
	int vrefresh = drm_mode_vrefresh(mode);

	list_for_each_entry(fixed_mode, &connector->panel.fixed_modes, head) {
		int fixed_mode_vrefresh = drm_mode_vrefresh(fixed_mode);

		if (is_best_fixed_mode(connector, vrefresh,
				       fixed_mode_vrefresh, best_mode))
			best_mode = fixed_mode;
	}

	return best_mode;
}

static bool is_alt_drrs_mode(const struct drm_display_mode *mode,
			     const struct drm_display_mode *preferred_mode)
{
	return drm_mode_match(mode, preferred_mode,
			      DRM_MODE_MATCH_TIMINGS |
			      DRM_MODE_MATCH_FLAGS |
			      DRM_MODE_MATCH_3D_FLAGS) &&
		mode->clock != preferred_mode->clock;
}

static bool is_alt_fixed_mode(const struct drm_display_mode *mode,
			      const struct drm_display_mode *preferred_mode)
{
	u32 sync_flags = DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_NHSYNC |
		DRM_MODE_FLAG_PVSYNC | DRM_MODE_FLAG_NVSYNC;

	return (mode->flags & ~sync_flags) == (preferred_mode->flags & ~sync_flags) &&
		mode->hdisplay == preferred_mode->hdisplay &&
		mode->vdisplay == preferred_mode->vdisplay;
}

const struct drm_display_mode *
intel_panel_downclock_mode(struct intel_connector *connector,
			   const struct drm_display_mode *adjusted_mode)
{
	const struct drm_display_mode *fixed_mode, *best_mode = NULL;
	int min_vrefresh = connector->panel.vbt.seamless_drrs_min_refresh_rate;
	int max_vrefresh = drm_mode_vrefresh(adjusted_mode);

	/* pick the fixed_mode with the lowest refresh rate */
	list_for_each_entry(fixed_mode, &connector->panel.fixed_modes, head) {
		int vrefresh = drm_mode_vrefresh(fixed_mode);

		if (is_alt_drrs_mode(fixed_mode, adjusted_mode) &&
		    vrefresh >= min_vrefresh && vrefresh < max_vrefresh) {
			max_vrefresh = vrefresh;
			best_mode = fixed_mode;
		}
	}

	return best_mode;
}

const struct drm_display_mode *
intel_panel_highest_mode(struct intel_connector *connector,
			 const struct drm_display_mode *adjusted_mode)
{
	const struct drm_display_mode *fixed_mode, *best_mode = adjusted_mode;

	/* pick the fixed_mode that has the highest clock */
	list_for_each_entry(fixed_mode, &connector->panel.fixed_modes, head) {
		if (fixed_mode->clock > best_mode->clock)
			best_mode = fixed_mode;
	}

	return best_mode;
}

int intel_panel_get_modes(struct intel_connector *connector)
{
	const struct drm_display_mode *fixed_mode;
	int num_modes = 0;

	list_for_each_entry(fixed_mode, &connector->panel.fixed_modes, head) {
		struct drm_display_mode *mode;

		mode = drm_mode_duplicate(connector->base.dev, fixed_mode);
		if (mode) {
			drm_mode_probed_add(&connector->base, mode);
			num_modes++;
		}
	}

	return num_modes;
}

static bool has_drrs_modes(struct intel_connector *connector)
{
	const struct drm_display_mode *mode1;

	list_for_each_entry(mode1, &connector->panel.fixed_modes, head) {
		const struct drm_display_mode *mode2 = mode1;

		list_for_each_entry_continue(mode2, &connector->panel.fixed_modes, head) {
			if (is_alt_drrs_mode(mode1, mode2))
				return true;
		}
	}

	return false;
}

enum drrs_type intel_panel_drrs_type(struct intel_connector *connector)
{
	return connector->panel.vbt.drrs_type;
}

int intel_panel_compute_config(struct intel_connector *connector,
			       struct drm_display_mode *adjusted_mode)
{
	const struct drm_display_mode *fixed_mode =
		intel_panel_fixed_mode(connector, adjusted_mode);
	int vrefresh, fixed_mode_vrefresh;
	bool is_vrr;

	if (!fixed_mode)
		return 0;

	vrefresh = drm_mode_vrefresh(adjusted_mode);
	fixed_mode_vrefresh = drm_mode_vrefresh(fixed_mode);

	/*
	 * Assume that we shouldn't muck about with the
	 * timings if they don't land in the VRR range.
	 */
	is_vrr = intel_vrr_is_in_range(connector, vrefresh) &&
		intel_vrr_is_in_range(connector, fixed_mode_vrefresh);

	if (!is_vrr) {
		/*
		 * We don't want to lie too much to the user about the refresh
		 * rate they're going to get. But we have to allow a bit of latitude
		 * for Xorg since it likes to automagically cook up modes with slightly
		 * off refresh rates.
		 */
		if (abs(vrefresh - fixed_mode_vrefresh) > 1) {
			drm_dbg_kms(connector->base.dev,
				    "[CONNECTOR:%d:%s] Requested mode vrefresh (%d Hz) does not match fixed mode vrefresh (%d Hz)\n",
				    connector->base.base.id, connector->base.name,
				    vrefresh, fixed_mode_vrefresh);

			return -EINVAL;
		}
	}

	drm_mode_copy(adjusted_mode, fixed_mode);

	if (is_vrr && fixed_mode_vrefresh != vrefresh)
		adjusted_mode->vtotal =
			DIV_ROUND_CLOSEST(adjusted_mode->clock * 1000,
					  adjusted_mode->htotal * vrefresh);

	drm_mode_set_crtcinfo(adjusted_mode, 0);

	return 0;
}

static void intel_panel_add_edid_alt_fixed_modes(struct intel_connector *connector)
{
	struct intel_display *display = to_intel_display(connector);
	const struct drm_display_mode *preferred_mode =
		intel_panel_preferred_fixed_mode(connector);
	struct drm_display_mode *mode, *next;

	list_for_each_entry_safe(mode, next, &connector->base.probed_modes, head) {
		if (!is_alt_fixed_mode(mode, preferred_mode))
			continue;

		drm_dbg_kms(display->drm,
			    "[CONNECTOR:%d:%s] using alternate EDID fixed mode: " DRM_MODE_FMT "\n",
			    connector->base.base.id, connector->base.name,
			    DRM_MODE_ARG(mode));

		list_move_tail(&mode->head, &connector->panel.fixed_modes);
	}
}

static void intel_panel_add_edid_preferred_mode(struct intel_connector *connector)
{
	struct intel_display *display = to_intel_display(connector);
	struct drm_display_mode *scan, *fixed_mode = NULL;

	if (list_empty(&connector->base.probed_modes))
		return;

	/* make sure the preferred mode is first */
	list_for_each_entry(scan, &connector->base.probed_modes, head) {
		if (scan->type & DRM_MODE_TYPE_PREFERRED) {
			fixed_mode = scan;
			break;
		}
	}

	if (!fixed_mode)
		fixed_mode = list_first_entry(&connector->base.probed_modes,
					      typeof(*fixed_mode), head);

	drm_dbg_kms(display->drm,
		    "[CONNECTOR:%d:%s] using %s EDID fixed mode: " DRM_MODE_FMT "\n",
		    connector->base.base.id, connector->base.name,
		    fixed_mode->type & DRM_MODE_TYPE_PREFERRED ? "preferred" : "first",
		    DRM_MODE_ARG(fixed_mode));

	fixed_mode->type |= DRM_MODE_TYPE_PREFERRED;

	list_move_tail(&fixed_mode->head, &connector->panel.fixed_modes);
}

static void intel_panel_destroy_probed_modes(struct intel_connector *connector)
{
	struct intel_display *display = to_intel_display(connector);
	struct drm_display_mode *mode, *next;

	list_for_each_entry_safe(mode, next, &connector->base.probed_modes, head) {
		drm_dbg_kms(display->drm,
			    "[CONNECTOR:%d:%s] not using EDID mode: " DRM_MODE_FMT "\n",
			    connector->base.base.id, connector->base.name,
			    DRM_MODE_ARG(mode));
		list_del(&mode->head);
		drm_mode_destroy(display->drm, mode);
	}
}

void intel_panel_add_edid_fixed_modes(struct intel_connector *connector,
				      bool use_alt_fixed_modes)
{
	intel_panel_add_edid_preferred_mode(connector);
	if (intel_panel_preferred_fixed_mode(connector) && use_alt_fixed_modes)
		intel_panel_add_edid_alt_fixed_modes(connector);
	intel_panel_destroy_probed_modes(connector);
}

static void intel_panel_add_fixed_mode(struct intel_connector *connector,
				       struct drm_display_mode *fixed_mode,
				       const char *type)
{
	struct intel_display *display = to_intel_display(connector);
	struct drm_display_info *info = &connector->base.display_info;

	if (!fixed_mode)
		return;

	fixed_mode->type |= DRM_MODE_TYPE_PREFERRED | DRM_MODE_TYPE_DRIVER;

	info->width_mm = fixed_mode->width_mm;
	info->height_mm = fixed_mode->height_mm;

	drm_dbg_kms(display->drm, "[CONNECTOR:%d:%s] using %s fixed mode: " DRM_MODE_FMT "\n",
		    connector->base.base.id, connector->base.name, type,
		    DRM_MODE_ARG(fixed_mode));

	list_add_tail(&fixed_mode->head, &connector->panel.fixed_modes);
}

void intel_panel_add_vbt_lfp_fixed_mode(struct intel_connector *connector)
{
	struct intel_display *display = to_intel_display(connector);
	const struct drm_display_mode *mode;

	mode = connector->panel.vbt.lfp_vbt_mode;
	if (!mode)
		return;

	intel_panel_add_fixed_mode(connector,
				   drm_mode_duplicate(display->drm, mode),
				   "VBT LFP");
}

void intel_panel_add_vbt_sdvo_fixed_mode(struct intel_connector *connector)
{
	struct intel_display *display = to_intel_display(connector);
	const struct drm_display_mode *mode;

	mode = connector->panel.vbt.sdvo_lvds_vbt_mode;
	if (!mode)
		return;

	intel_panel_add_fixed_mode(connector,
				   drm_mode_duplicate(display->drm, mode),
				   "VBT SDVO");
}

void intel_panel_add_encoder_fixed_mode(struct intel_connector *connector,
					struct intel_encoder *encoder)
{
	intel_panel_add_fixed_mode(connector,
				   intel_encoder_current_mode(encoder),
				   "current (BIOS)");
}

enum drm_connector_status
intel_panel_detect(struct drm_connector *connector, bool force)
{
	struct intel_display *display = to_intel_display(connector->dev);
	struct drm_i915_private *i915 = to_i915(connector->dev);

	if (!intel_display_device_enabled(display))
		return connector_status_disconnected;

	if (!intel_display_driver_check_access(i915))
		return connector->status;

	return connector_status_connected;
}

enum drm_mode_status
intel_panel_mode_valid(struct intel_connector *connector,
		       const struct drm_display_mode *mode)
{
	const struct drm_display_mode *fixed_mode =
		intel_panel_fixed_mode(connector, mode);

	if (!fixed_mode)
		return MODE_OK;

	if (mode->hdisplay != fixed_mode->hdisplay)
		return MODE_PANEL;

	if (mode->vdisplay != fixed_mode->vdisplay)
		return MODE_PANEL;

	if (drm_mode_vrefresh(mode) != drm_mode_vrefresh(fixed_mode))
		return MODE_PANEL;

	return MODE_OK;
}

void intel_panel_init_alloc(struct intel_connector *connector)
{
	struct intel_panel *panel = &connector->panel;

	connector->panel.vbt.panel_type = -1;
	connector->panel.vbt.backlight.controller = -1;
	INIT_LIST_HEAD(&panel->fixed_modes);
}

int intel_panel_init(struct intel_connector *connector,
		     const struct drm_edid *fixed_edid)
{
	struct intel_panel *panel = &connector->panel;

	panel->fixed_edid = fixed_edid;

	intel_backlight_init_funcs(panel);

	if (!has_drrs_modes(connector))
		connector->panel.vbt.drrs_type = DRRS_TYPE_NONE;

	drm_dbg_kms(connector->base.dev,
		    "[CONNECTOR:%d:%s] DRRS type: %s\n",
		    connector->base.base.id, connector->base.name,
		    intel_drrs_type_str(intel_panel_drrs_type(connector)));

	return 0;
}

void intel_panel_fini(struct intel_connector *connector)
{
	struct intel_panel *panel = &connector->panel;
	struct drm_display_mode *fixed_mode, *next;

	if (!IS_ERR_OR_NULL(panel->fixed_edid))
		drm_edid_free(panel->fixed_edid);

	intel_backlight_destroy(panel);

	intel_bios_fini_panel(panel);

	list_for_each_entry_safe(fixed_mode, next, &panel->fixed_modes, head) {
		list_del(&fixed_mode->head);
		drm_mode_destroy(connector->base.dev, fixed_mode);
	}
}

/*
 * Copyright © 2016 Intel Corporation
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
 *
 */
#include <drm/drm_edid.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_dp_dual_mode_helper.h>
#include "intel_drv.h"

static struct intel_dp *lspcon_to_intel_dp(struct intel_lspcon *lspcon)
{
	struct intel_digital_port *dig_port =
		container_of(lspcon, struct intel_digital_port, lspcon);

	return &dig_port->dp;
}

static enum drm_lspcon_mode lspcon_get_current_mode(struct intel_lspcon *lspcon)
{
	enum drm_lspcon_mode current_mode = DRM_LSPCON_MODE_INVALID;
	struct i2c_adapter *adapter = &lspcon_to_intel_dp(lspcon)->aux.ddc;

	if (drm_lspcon_get_mode(adapter, &current_mode))
		DRM_ERROR("Error reading LSPCON mode\n");
	else
		DRM_DEBUG_KMS("Current LSPCON mode %s\n",
			current_mode == DRM_LSPCON_MODE_PCON ? "PCON" : "LS");
	return current_mode;
}

static int lspcon_change_mode(struct intel_lspcon *lspcon,
	enum drm_lspcon_mode mode, bool force)
{
	int err;
	enum drm_lspcon_mode current_mode;
	struct i2c_adapter *adapter = &lspcon_to_intel_dp(lspcon)->aux.ddc;

	err = drm_lspcon_get_mode(adapter, &current_mode);
	if (err) {
		DRM_ERROR("Error reading LSPCON mode\n");
		return err;
	}

	if (current_mode == mode) {
		DRM_DEBUG_KMS("Current mode = desired LSPCON mode\n");
		return 0;
	}

	err = drm_lspcon_set_mode(adapter, mode);
	if (err < 0) {
		DRM_ERROR("LSPCON mode change failed\n");
		return err;
	}

	lspcon->mode = mode;
	DRM_DEBUG_KMS("LSPCON mode changed done\n");
	return 0;
}

static bool lspcon_probe(struct intel_lspcon *lspcon)
{
	enum drm_dp_dual_mode_type adaptor_type;
	struct i2c_adapter *adapter = &lspcon_to_intel_dp(lspcon)->aux.ddc;

	/* Lets probe the adaptor and check its type */
	adaptor_type = drm_dp_dual_mode_detect(adapter);
	if (adaptor_type != DRM_DP_DUAL_MODE_LSPCON) {
		DRM_DEBUG_KMS("No LSPCON detected, found %s\n",
			drm_dp_get_dual_mode_type_name(adaptor_type));
		return false;
	}

	/* Yay ... got a LSPCON device */
	DRM_DEBUG_KMS("LSPCON detected\n");
	lspcon->mode = lspcon_get_current_mode(lspcon);
	lspcon->active = true;
	return true;
}

static void lspcon_resume_in_pcon_wa(struct intel_lspcon *lspcon)
{
	struct intel_dp *intel_dp = lspcon_to_intel_dp(lspcon);
	unsigned long start = jiffies;

	if (!lspcon->desc_valid)
		return;

	while (1) {
		struct intel_dp_desc desc;

		/*
		 * The w/a only applies in PCON mode and we don't expect any
		 * AUX errors.
		 */
		if (!__intel_dp_read_desc(intel_dp, &desc))
			return;

		if (!memcmp(&intel_dp->desc, &desc, sizeof(desc))) {
			DRM_DEBUG_KMS("LSPCON recovering in PCON mode after %u ms\n",
				      jiffies_to_msecs(jiffies - start));
			return;
		}

		if (time_after(jiffies, start + msecs_to_jiffies(1000)))
			break;

		usleep_range(10000, 15000);
	}

	DRM_DEBUG_KMS("LSPCON DP descriptor mismatch after resume\n");
}

void lspcon_resume(struct intel_lspcon *lspcon)
{
	lspcon_resume_in_pcon_wa(lspcon);

	if (lspcon_change_mode(lspcon, DRM_LSPCON_MODE_PCON, true))
		DRM_ERROR("LSPCON resume failed\n");
	else
		DRM_DEBUG_KMS("LSPCON resume success\n");
}

bool lspcon_init(struct intel_digital_port *intel_dig_port)
{
	struct intel_dp *dp = &intel_dig_port->dp;
	struct intel_lspcon *lspcon = &intel_dig_port->lspcon;
	struct drm_device *dev = intel_dig_port->base.base.dev;
	struct drm_i915_private *dev_priv = to_i915(dev);

	if (!IS_GEN9(dev_priv)) {
		DRM_ERROR("LSPCON is supported on GEN9 only\n");
		return false;
	}

	lspcon->active = false;
	lspcon->mode = DRM_LSPCON_MODE_INVALID;

	if (!lspcon_probe(lspcon)) {
		DRM_ERROR("Failed to probe lspcon\n");
		return false;
	}

	/*
	* In the SW state machine, lets Put LSPCON in PCON mode only.
	* In this way, it will work with both HDMI 1.4 sinks as well as HDMI
	* 2.0 sinks.
	*/
	if (lspcon->active && lspcon->mode != DRM_LSPCON_MODE_PCON) {
		if (lspcon_change_mode(lspcon, DRM_LSPCON_MODE_PCON,
			true) < 0) {
			DRM_ERROR("LSPCON mode change to PCON failed\n");
			return false;
		}
	}

	if (!intel_dp_read_dpcd(dp)) {
		DRM_ERROR("LSPCON DPCD read failed\n");
		return false;
	}

	lspcon->desc_valid = intel_dp_read_desc(dp);

	DRM_DEBUG_KMS("Success: LSPCON init\n");
	return true;
}

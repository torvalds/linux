/*
 * Copyright © 2015 Intel Corporation
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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 */

#ifndef _I915_PARAMS_H_
#define _I915_PARAMS_H_

#include <linux/bitops.h>
#include <linux/cache.h> /* for __read_mostly */

struct drm_printer;

#define ENABLE_GUC_SUBMISSION		BIT(0)
#define ENABLE_GUC_LOAD_HUC		BIT(1)

/*
 * Invoke param, a function-like macro, for each i915 param, with arguments:
 *
 * param(type, name, value)
 *
 * type: parameter type, one of {bool, int, unsigned int, char *}
 * name: name of the parameter
 * value: initial/default value of the parameter
 */
#define I915_PARAMS_FOR_EACH(param) \
	param(char *, vbt_firmware, NULL) \
	param(int, modeset, -1) \
	param(int, lvds_channel_mode, 0) \
	param(int, panel_use_ssc, -1) \
	param(int, vbt_sdvo_panel_type, -1) \
	param(int, enable_dc, -1) \
	param(int, enable_fbc, -1) \
	param(int, enable_psr, -1) \
	param(int, disable_power_well, -1) \
	param(int, enable_ips, 1) \
	param(int, invert_brightness, 0) \
	param(int, enable_guc, -1) \
	param(int, guc_log_level, -1) \
	param(char *, guc_firmware_path, NULL) \
	param(char *, huc_firmware_path, NULL) \
	param(char *, dmc_firmware_path, NULL) \
	param(int, mmio_debug, -IS_ENABLED(CONFIG_DRM_I915_DEBUG_MMIO)) \
	param(int, edp_vswing, 0) \
	param(int, reset, 2) \
	param(unsigned int, inject_load_failure, 0) \
	param(int, fastboot, -1) \
	param(int, enable_dpcd_backlight, 0) \
	param(char *, force_probe, CONFIG_DRM_I915_FORCE_PROBE) \
	/* leave bools at the end to not create holes */ \
	param(bool, alpha_support, IS_ENABLED(CONFIG_DRM_I915_ALPHA_SUPPORT)) \
	param(bool, enable_hangcheck, true) \
	param(bool, prefault_disable, false) \
	param(bool, load_detect_test, false) \
	param(bool, force_reset_modeset_test, false) \
	param(bool, error_capture, true) \
	param(bool, disable_display, false) \
	param(bool, verbose_state_checks, true) \
	param(bool, nuclear_pageflip, false) \
	param(bool, enable_dp_mst, true) \
	param(bool, enable_gvt, false)

#define MEMBER(T, member, ...) T member;
struct i915_params {
	I915_PARAMS_FOR_EACH(MEMBER);
};
#undef MEMBER

extern struct i915_params i915_modparams __read_mostly;

void i915_params_dump(const struct i915_params *params, struct drm_printer *p);
void i915_params_copy(struct i915_params *dest, const struct i915_params *src);
void i915_params_free(struct i915_params *params);

#endif


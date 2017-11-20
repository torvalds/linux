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

#include <linux/cache.h> /* for __read_mostly */

#define I915_PARAMS_FOR_EACH(param) \
	param(char *, vbt_firmware, NULL) \
	param(int, modeset, -1) \
	param(int, panel_ignore_lid, 1) \
	param(int, semaphores, -1) \
	param(int, lvds_channel_mode, 0) \
	param(int, panel_use_ssc, -1) \
	param(int, vbt_sdvo_panel_type, -1) \
	param(int, enable_rc6, -1) \
	param(int, enable_dc, -1) \
	param(int, enable_fbc, -1) \
	param(int, enable_ppgtt, -1) \
	param(int, enable_psr, -1) \
	param(int, disable_power_well, -1) \
	param(int, enable_ips, 1) \
	param(int, invert_brightness, 0) \
	param(int, enable_guc_loading, 0) \
	param(int, enable_guc_submission, 0) \
	param(int, guc_log_level, -1) \
	param(char *, guc_firmware_path, NULL) \
	param(char *, huc_firmware_path, NULL) \
	param(int, mmio_debug, 0) \
	param(int, edp_vswing, 0) \
	param(int, reset, 2) \
	param(unsigned int, inject_load_failure, 0) \
	/* leave bools at the end to not create holes */ \
	param(bool, alpha_support, IS_ENABLED(CONFIG_DRM_I915_ALPHA_SUPPORT)) \
	param(bool, enable_cmd_parser, true) \
	param(bool, enable_hangcheck, true) \
	param(bool, fastboot, false) \
	param(bool, prefault_disable, false) \
	param(bool, load_detect_test, false) \
	param(bool, force_reset_modeset_test, false) \
	param(bool, error_capture, true) \
	param(bool, disable_display, false) \
	param(bool, verbose_state_checks, true) \
	param(bool, nuclear_pageflip, false) \
	param(bool, enable_dp_mst, true) \
	param(bool, enable_dpcd_backlight, false) \
	param(bool, enable_gvt, false)

#define MEMBER(T, member, ...) T member;
struct i915_params {
	I915_PARAMS_FOR_EACH(MEMBER);
};
#undef MEMBER

extern struct i915_params i915_modparams __read_mostly;

#endif


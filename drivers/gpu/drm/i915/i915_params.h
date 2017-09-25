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
	param(char *, vbt_firmware) \
	param(int, modeset) \
	param(int, panel_ignore_lid) \
	param(int, semaphores) \
	param(int, lvds_channel_mode) \
	param(int, panel_use_ssc) \
	param(int, vbt_sdvo_panel_type) \
	param(int, enable_rc6) \
	param(int, enable_dc) \
	param(int, enable_fbc) \
	param(int, enable_ppgtt) \
	param(int, enable_execlists) \
	param(int, enable_psr) \
	param(int, disable_power_well) \
	param(int, enable_ips) \
	param(int, invert_brightness) \
	param(int, enable_guc_loading) \
	param(int, enable_guc_submission) \
	param(int, guc_log_level) \
	param(char *, guc_firmware_path) \
	param(char *, huc_firmware_path) \
	param(int, use_mmio_flip) \
	param(int, mmio_debug) \
	param(int, edp_vswing) \
	param(int, reset) \
	param(unsigned int, inject_load_failure) \
	/* leave bools at the end to not create holes */ \
	param(bool, alpha_support) \
	param(bool, enable_cmd_parser) \
	param(bool, enable_hangcheck) \
	param(bool, fastboot) \
	param(bool, prefault_disable) \
	param(bool, load_detect_test) \
	param(bool, force_reset_modeset_test) \
	param(bool, error_capture) \
	param(bool, disable_display) \
	param(bool, verbose_state_checks) \
	param(bool, nuclear_pageflip) \
	param(bool, enable_dp_mst) \
	param(bool, enable_dpcd_backlight) \
	param(bool, enable_gvt)

#define MEMBER(T, member) T member;
struct i915_params {
	I915_PARAMS_FOR_EACH(MEMBER);
};
#undef MEMBER

extern struct i915_params i915_modparams __read_mostly;

#endif


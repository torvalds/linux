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

#define I915_PARAMS_FOR_EACH(func) \
	func(char *, vbt_firmware); \
	func(int, modeset); \
	func(int, panel_ignore_lid); \
	func(int, semaphores); \
	func(int, lvds_channel_mode); \
	func(int, panel_use_ssc); \
	func(int, vbt_sdvo_panel_type); \
	func(int, enable_rc6); \
	func(int, enable_dc); \
	func(int, enable_fbc); \
	func(int, enable_ppgtt); \
	func(int, enable_execlists); \
	func(int, enable_psr); \
	func(int, disable_power_well); \
	func(int, enable_ips); \
	func(int, invert_brightness); \
	func(int, enable_guc_loading); \
	func(int, enable_guc_submission); \
	func(int, guc_log_level); \
	func(char *, guc_firmware_path); \
	func(char *, huc_firmware_path); \
	func(int, use_mmio_flip); \
	func(int, mmio_debug); \
	func(int, edp_vswing); \
	func(int, reset); \
	func(unsigned int, inject_load_failure); \
	/* leave bools at the end to not create holes */ \
	func(bool, alpha_support); \
	func(bool, enable_cmd_parser); \
	func(bool, enable_hangcheck); \
	func(bool, fastboot); \
	func(bool, prefault_disable); \
	func(bool, load_detect_test); \
	func(bool, force_reset_modeset_test); \
	func(bool, error_capture); \
	func(bool, disable_display); \
	func(bool, verbose_state_checks); \
	func(bool, nuclear_pageflip); \
	func(bool, enable_dp_mst); \
	func(bool, enable_dpcd_backlight); \
	func(bool, enable_gvt)

#define MEMBER(T, member) T member
struct i915_params {
	I915_PARAMS_FOR_EACH(MEMBER);
};
#undef MEMBER

extern struct i915_params i915 __read_mostly;

#endif


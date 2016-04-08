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

struct i915_params {
	int modeset;
	int panel_ignore_lid;
	int semaphores;
	int lvds_channel_mode;
	int panel_use_ssc;
	int vbt_sdvo_panel_type;
	int enable_rc6;
	int enable_dc;
	int enable_fbc;
	int enable_ppgtt;
	int enable_execlists;
	int enable_psr;
	unsigned int preliminary_hw_support;
	int disable_power_well;
	int enable_ips;
	int invert_brightness;
	int enable_cmd_parser;
	int guc_log_level;
	int use_mmio_flip;
	int mmio_debug;
	int edp_vswing;
	/* leave bools at the end to not create holes */
	bool enable_hangcheck;
	bool fastboot;
	bool prefault_disable;
	bool load_detect_test;
	bool reset;
	bool disable_display;
	bool enable_guc_submission;
	bool verbose_state_checks;
	bool nuclear_pageflip;
};

extern struct i915_params i915 __read_mostly;

#endif


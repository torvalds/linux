/*
 * Copyright © 2014 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include "i915_params.h"
#include "i915_drv.h"

struct i915_params i915 __read_mostly = {
	.modeset = -1,
	.panel_ignore_lid = 1,
	.semaphores = -1,
	.lvds_channel_mode = 0,
	.panel_use_ssc = -1,
	.vbt_sdvo_panel_type = -1,
	.enable_rc6 = -1,
	.enable_dc = -1,
	.enable_fbc = -1,
	.enable_execlists = -1,
	.enable_hangcheck = true,
	.enable_ppgtt = -1,
	.enable_psr = -1,
	.preliminary_hw_support = IS_ENABLED(CONFIG_DRM_I915_PRELIMINARY_HW_SUPPORT),
	.disable_power_well = -1,
	.enable_ips = 1,
	.fastboot = 0,
	.prefault_disable = 0,
	.load_detect_test = 0,
	.reset = true,
	.invert_brightness = 0,
	.disable_display = 0,
	.enable_cmd_parser = 1,
	.use_mmio_flip = 0,
	.mmio_debug = 0,
	.verbose_state_checks = 1,
	.nuclear_pageflip = 0,
	.edp_vswing = 0,
	.enable_guc_loading = 0,
	.enable_guc_submission = 0,
	.guc_log_level = -1,
	.enable_dp_mst = true,
	.inject_load_failure = 0,
	.enable_dpcd_backlight = false,
	.enable_gvt = false,
};

module_param_named(modeset, i915.modeset, int, 0400);
MODULE_PARM_DESC(modeset,
	"Use kernel modesetting [KMS] (0=disable, "
	"1=on, -1=force vga console preference [default])");

module_param_named_unsafe(panel_ignore_lid, i915.panel_ignore_lid, int, 0600);
MODULE_PARM_DESC(panel_ignore_lid,
	"Override lid status (0=autodetect, 1=autodetect disabled [default], "
	"-1=force lid closed, -2=force lid open)");

module_param_named_unsafe(semaphores, i915.semaphores, int, 0400);
MODULE_PARM_DESC(semaphores,
	"Use semaphores for inter-ring sync "
	"(default: -1 (use per-chip defaults))");

module_param_named_unsafe(enable_rc6, i915.enable_rc6, int, 0400);
MODULE_PARM_DESC(enable_rc6,
	"Enable power-saving render C-state 6. "
	"Different stages can be selected via bitmask values "
	"(0 = disable; 1 = enable rc6; 2 = enable deep rc6; 4 = enable deepest rc6). "
	"For example, 3 would enable rc6 and deep rc6, and 7 would enable everything. "
	"default: -1 (use per-chip default)");

module_param_named_unsafe(enable_dc, i915.enable_dc, int, 0400);
MODULE_PARM_DESC(enable_dc,
	"Enable power-saving display C-states. "
	"(-1=auto [default]; 0=disable; 1=up to DC5; 2=up to DC6)");

module_param_named_unsafe(enable_fbc, i915.enable_fbc, int, 0600);
MODULE_PARM_DESC(enable_fbc,
	"Enable frame buffer compression for power savings "
	"(default: -1 (use per-chip default))");

module_param_named_unsafe(lvds_channel_mode, i915.lvds_channel_mode, int, 0400);
MODULE_PARM_DESC(lvds_channel_mode,
	 "Specify LVDS channel mode "
	 "(0=probe BIOS [default], 1=single-channel, 2=dual-channel)");

module_param_named_unsafe(lvds_use_ssc, i915.panel_use_ssc, int, 0600);
MODULE_PARM_DESC(lvds_use_ssc,
	"Use Spread Spectrum Clock with panels [LVDS/eDP] "
	"(default: auto from VBT)");

module_param_named_unsafe(vbt_sdvo_panel_type, i915.vbt_sdvo_panel_type, int, 0400);
MODULE_PARM_DESC(vbt_sdvo_panel_type,
	"Override/Ignore selection of SDVO panel mode in the VBT "
	"(-2=ignore, -1=auto [default], index in VBT BIOS table)");

module_param_named_unsafe(reset, i915.reset, bool, 0600);
MODULE_PARM_DESC(reset, "Attempt GPU resets (default: true)");

module_param_named_unsafe(enable_hangcheck, i915.enable_hangcheck, bool, 0644);
MODULE_PARM_DESC(enable_hangcheck,
	"Periodically check GPU activity for detecting hangs. "
	"WARNING: Disabling this can cause system wide hangs. "
	"(default: true)");

module_param_named_unsafe(enable_ppgtt, i915.enable_ppgtt, int, 0400);
MODULE_PARM_DESC(enable_ppgtt,
	"Override PPGTT usage. "
	"(-1=auto [default], 0=disabled, 1=aliasing, 2=full, 3=full with extended address space)");

module_param_named_unsafe(enable_execlists, i915.enable_execlists, int, 0400);
MODULE_PARM_DESC(enable_execlists,
	"Override execlists usage. "
	"(-1=auto [default], 0=disabled, 1=enabled)");

module_param_named_unsafe(enable_psr, i915.enable_psr, int, 0600);
MODULE_PARM_DESC(enable_psr, "Enable PSR "
		 "(0=disabled, 1=enabled - link mode chosen per-platform, 2=force link-standby mode, 3=force link-off mode) "
		 "Default: -1 (use per-chip default)");

module_param_named_unsafe(preliminary_hw_support, i915.preliminary_hw_support, int, 0400);
MODULE_PARM_DESC(preliminary_hw_support,
	"Enable preliminary hardware support.");

module_param_named_unsafe(disable_power_well, i915.disable_power_well, int, 0400);
MODULE_PARM_DESC(disable_power_well,
	"Disable display power wells when possible "
	"(-1=auto [default], 0=power wells always on, 1=power wells disabled when possible)");

module_param_named_unsafe(enable_ips, i915.enable_ips, int, 0600);
MODULE_PARM_DESC(enable_ips, "Enable IPS (default: true)");

module_param_named(fastboot, i915.fastboot, bool, 0600);
MODULE_PARM_DESC(fastboot,
	"Try to skip unnecessary mode sets at boot time (default: false)");

module_param_named_unsafe(prefault_disable, i915.prefault_disable, bool, 0600);
MODULE_PARM_DESC(prefault_disable,
	"Disable page prefaulting for pread/pwrite/reloc (default:false). "
	"For developers only.");

module_param_named_unsafe(load_detect_test, i915.load_detect_test, bool, 0600);
MODULE_PARM_DESC(load_detect_test,
	"Force-enable the VGA load detect code for testing (default:false). "
	"For developers only.");

module_param_named_unsafe(invert_brightness, i915.invert_brightness, int, 0600);
MODULE_PARM_DESC(invert_brightness,
	"Invert backlight brightness "
	"(-1 force normal, 0 machine defaults, 1 force inversion), please "
	"report PCI device ID, subsystem vendor and subsystem device ID "
	"to dri-devel@lists.freedesktop.org, if your machine needs it. "
	"It will then be included in an upcoming module version.");

module_param_named(disable_display, i915.disable_display, bool, 0400);
MODULE_PARM_DESC(disable_display, "Disable display (default: false)");

module_param_named_unsafe(enable_cmd_parser, i915.enable_cmd_parser, int, 0600);
MODULE_PARM_DESC(enable_cmd_parser,
		 "Enable command parsing (1=enabled [default], 0=disabled)");

module_param_named_unsafe(use_mmio_flip, i915.use_mmio_flip, int, 0600);
MODULE_PARM_DESC(use_mmio_flip,
		 "use MMIO flips (-1=never, 0=driver discretion [default], 1=always)");

module_param_named(mmio_debug, i915.mmio_debug, int, 0600);
MODULE_PARM_DESC(mmio_debug,
	"Enable the MMIO debug code for the first N failures (default: off). "
	"This may negatively affect performance.");

module_param_named(verbose_state_checks, i915.verbose_state_checks, bool, 0600);
MODULE_PARM_DESC(verbose_state_checks,
	"Enable verbose logs (ie. WARN_ON()) in case of unexpected hw state conditions.");

module_param_named_unsafe(nuclear_pageflip, i915.nuclear_pageflip, bool, 0600);
MODULE_PARM_DESC(nuclear_pageflip,
		 "Force atomic modeset functionality; asynchronous mode is not yet supported. (default: false).");

/* WA to get away with the default setting in VBT for early platforms.Will be removed */
module_param_named_unsafe(edp_vswing, i915.edp_vswing, int, 0400);
MODULE_PARM_DESC(edp_vswing,
		 "Ignore/Override vswing pre-emph table selection from VBT "
		 "(0=use value from vbt [default], 1=low power swing(200mV),"
		 "2=default swing(400mV))");

module_param_named_unsafe(enable_guc_loading, i915.enable_guc_loading, int, 0400);
MODULE_PARM_DESC(enable_guc_loading,
		"Enable GuC firmware loading "
		"(-1=auto, 0=never [default], 1=if available, 2=required)");

module_param_named_unsafe(enable_guc_submission, i915.enable_guc_submission, int, 0400);
MODULE_PARM_DESC(enable_guc_submission,
		"Enable GuC submission "
		"(-1=auto, 0=never [default], 1=if available, 2=required)");

module_param_named(guc_log_level, i915.guc_log_level, int, 0400);
MODULE_PARM_DESC(guc_log_level,
	"GuC firmware logging level (-1:disabled (default), 0-3:enabled)");

module_param_named_unsafe(enable_dp_mst, i915.enable_dp_mst, bool, 0600);
MODULE_PARM_DESC(enable_dp_mst,
	"Enable multi-stream transport (MST) for new DisplayPort sinks. (default: true)");
module_param_named_unsafe(inject_load_failure, i915.inject_load_failure, uint, 0400);
MODULE_PARM_DESC(inject_load_failure,
	"Force an error after a number of failure check points (0:disabled (default), N:force failure at the Nth failure check point)");
module_param_named(enable_dpcd_backlight, i915.enable_dpcd_backlight, bool, 0600);
MODULE_PARM_DESC(enable_dpcd_backlight,
	"Enable support for DPCD backlight control (default:false)");

module_param_named(enable_gvt, i915.enable_gvt, bool, 0400);
MODULE_PARM_DESC(enable_gvt,
	"Enable support for Intel GVT-g graphics virtualization host support(default:false)");

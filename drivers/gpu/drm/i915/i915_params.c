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

#include "i915_drv.h"

struct i915_params i915 __read_mostly = {
	.modeset = -1,
	.panel_ignore_lid = 1,
	.powersave = 1,
	.semaphores = -1,
	.lvds_downclock = 0,
	.lvds_channel_mode = 0,
	.panel_use_ssc = -1,
	.vbt_sdvo_panel_type = -1,
	.enable_rc6 = -1,
	.enable_fbc = -1,
	.enable_hangcheck = true,
	.enable_ppgtt = -1,
	.enable_psr = 0,
	.preliminary_hw_support = IS_ENABLED(CONFIG_DRM_I915_PRELIMINARY_HW_SUPPORT),
	.disable_power_well = 1,
	.enable_ips = 1,
	.fastboot = 0,
	.prefault_disable = 0,
	.reset = true,
	.invert_brightness = 0,
	.disable_display = 0,
	.enable_cmd_parser = 1,
	.disable_vtd_wa = 0,
	.use_mmio_flip = 0,
};

module_param_named(modeset, i915.modeset, int, 0400);
MODULE_PARM_DESC(modeset,
	"Use kernel modesetting [KMS] (0=DRM_I915_KMS from .config, "
	"1=on, -1=force vga console preference [default])");

module_param_named(panel_ignore_lid, i915.panel_ignore_lid, int, 0600);
MODULE_PARM_DESC(panel_ignore_lid,
	"Override lid status (0=autodetect, 1=autodetect disabled [default], "
	"-1=force lid closed, -2=force lid open)");

module_param_named(powersave, i915.powersave, int, 0600);
MODULE_PARM_DESC(powersave,
	"Enable powersavings, fbc, downclocking, etc. (default: true)");

module_param_named(semaphores, i915.semaphores, int, 0400);
MODULE_PARM_DESC(semaphores,
	"Use semaphores for inter-ring sync "
	"(default: -1 (use per-chip defaults))");

module_param_named(enable_rc6, i915.enable_rc6, int, 0400);
MODULE_PARM_DESC(enable_rc6,
	"Enable power-saving render C-state 6. "
	"Different stages can be selected via bitmask values "
	"(0 = disable; 1 = enable rc6; 2 = enable deep rc6; 4 = enable deepest rc6). "
	"For example, 3 would enable rc6 and deep rc6, and 7 would enable everything. "
	"default: -1 (use per-chip default)");

module_param_named(enable_fbc, i915.enable_fbc, int, 0600);
MODULE_PARM_DESC(enable_fbc,
	"Enable frame buffer compression for power savings "
	"(default: -1 (use per-chip default))");

module_param_named(lvds_downclock, i915.lvds_downclock, int, 0400);
MODULE_PARM_DESC(lvds_downclock,
	"Use panel (LVDS/eDP) downclocking for power savings "
	"(default: false)");

module_param_named(lvds_channel_mode, i915.lvds_channel_mode, int, 0600);
MODULE_PARM_DESC(lvds_channel_mode,
	 "Specify LVDS channel mode "
	 "(0=probe BIOS [default], 1=single-channel, 2=dual-channel)");

module_param_named(lvds_use_ssc, i915.panel_use_ssc, int, 0600);
MODULE_PARM_DESC(lvds_use_ssc,
	"Use Spread Spectrum Clock with panels [LVDS/eDP] "
	"(default: auto from VBT)");

module_param_named(vbt_sdvo_panel_type, i915.vbt_sdvo_panel_type, int, 0600);
MODULE_PARM_DESC(vbt_sdvo_panel_type,
	"Override/Ignore selection of SDVO panel mode in the VBT "
	"(-2=ignore, -1=auto [default], index in VBT BIOS table)");

module_param_named(reset, i915.reset, bool, 0600);
MODULE_PARM_DESC(reset, "Attempt GPU resets (default: true)");

module_param_named(enable_hangcheck, i915.enable_hangcheck, bool, 0644);
MODULE_PARM_DESC(enable_hangcheck,
	"Periodically check GPU activity for detecting hangs. "
	"WARNING: Disabling this can cause system wide hangs. "
	"(default: true)");

module_param_named(enable_ppgtt, i915.enable_ppgtt, int, 0400);
MODULE_PARM_DESC(enable_ppgtt,
	"Override PPGTT usage. "
	"(-1=auto [default], 0=disabled, 1=aliasing, 2=full)");

module_param_named(enable_psr, i915.enable_psr, int, 0600);
MODULE_PARM_DESC(enable_psr, "Enable PSR (default: false)");

module_param_named(preliminary_hw_support, i915.preliminary_hw_support, int, 0600);
MODULE_PARM_DESC(preliminary_hw_support,
	"Enable preliminary hardware support.");

module_param_named(disable_power_well, i915.disable_power_well, int, 0600);
MODULE_PARM_DESC(disable_power_well,
	"Disable the power well when possible (default: true)");

module_param_named(enable_ips, i915.enable_ips, int, 0600);
MODULE_PARM_DESC(enable_ips, "Enable IPS (default: true)");

module_param_named(fastboot, i915.fastboot, bool, 0600);
MODULE_PARM_DESC(fastboot,
	"Try to skip unnecessary mode sets at boot time (default: false)");

module_param_named(prefault_disable, i915.prefault_disable, bool, 0600);
MODULE_PARM_DESC(prefault_disable,
	"Disable page prefaulting for pread/pwrite/reloc (default:false). "
	"For developers only.");

module_param_named(invert_brightness, i915.invert_brightness, int, 0600);
MODULE_PARM_DESC(invert_brightness,
	"Invert backlight brightness "
	"(-1 force normal, 0 machine defaults, 1 force inversion), please "
	"report PCI device ID, subsystem vendor and subsystem device ID "
	"to dri-devel@lists.freedesktop.org, if your machine needs it. "
	"It will then be included in an upcoming module version.");

module_param_named(disable_display, i915.disable_display, bool, 0600);
MODULE_PARM_DESC(disable_display, "Disable display (default: false)");

module_param_named(disable_vtd_wa, i915.disable_vtd_wa, bool, 0600);
MODULE_PARM_DESC(disable_vtd_wa, "Disable all VT-d workarounds (default: false)");

module_param_named(enable_cmd_parser, i915.enable_cmd_parser, int, 0600);
MODULE_PARM_DESC(enable_cmd_parser,
		 "Enable command parsing (1=enabled [default], 0=disabled)");

module_param_named(use_mmio_flip, i915.use_mmio_flip, int, 0600);
MODULE_PARM_DESC(use_mmio_flip,
		 "use MMIO flips (-1=never, 0=driver discretion [default], 1=always)");

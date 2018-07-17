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

#include <drm/drm_print.h>

#include "i915_params.h"
#include "i915_drv.h"

#define i915_param_named(name, T, perm, desc) \
	module_param_named(name, i915_modparams.name, T, perm); \
	MODULE_PARM_DESC(name, desc)
#define i915_param_named_unsafe(name, T, perm, desc) \
	module_param_named_unsafe(name, i915_modparams.name, T, perm); \
	MODULE_PARM_DESC(name, desc)

struct i915_params i915_modparams __read_mostly = {
#define MEMBER(T, member, value) .member = (value),
	I915_PARAMS_FOR_EACH(MEMBER)
#undef MEMBER
};

i915_param_named(modeset, int, 0400,
	"Use kernel modesetting [KMS] (0=disable, "
	"1=on, -1=force vga console preference [default])");

i915_param_named_unsafe(enable_dc, int, 0400,
	"Enable power-saving display C-states. "
	"(-1=auto [default]; 0=disable; 1=up to DC5; 2=up to DC6)");

i915_param_named_unsafe(enable_fbc, int, 0600,
	"Enable frame buffer compression for power savings "
	"(default: -1 (use per-chip default))");

i915_param_named_unsafe(lvds_channel_mode, int, 0400,
	 "Specify LVDS channel mode "
	 "(0=probe BIOS [default], 1=single-channel, 2=dual-channel)");

i915_param_named_unsafe(panel_use_ssc, int, 0600,
	"Use Spread Spectrum Clock with panels [LVDS/eDP] "
	"(default: auto from VBT)");

i915_param_named_unsafe(vbt_sdvo_panel_type, int, 0400,
	"Override/Ignore selection of SDVO panel mode in the VBT "
	"(-2=ignore, -1=auto [default], index in VBT BIOS table)");

i915_param_named_unsafe(reset, int, 0600,
	"Attempt GPU resets (0=disabled, 1=full gpu reset, 2=engine reset [default])");

i915_param_named_unsafe(vbt_firmware, charp, 0400,
	"Load VBT from specified file under /lib/firmware");

#if IS_ENABLED(CONFIG_DRM_I915_CAPTURE_ERROR)
i915_param_named(error_capture, bool, 0600,
	"Record the GPU state following a hang. "
	"This information in /sys/class/drm/card<N>/error is vital for "
	"triaging and debugging hangs.");
#endif

i915_param_named_unsafe(enable_hangcheck, bool, 0644,
	"Periodically check GPU activity for detecting hangs. "
	"WARNING: Disabling this can cause system wide hangs. "
	"(default: true)");

i915_param_named_unsafe(enable_ppgtt, int, 0400,
	"Override PPGTT usage. "
	"(-1=auto [default], 0=disabled, 1=aliasing, 2=full, 3=full with extended address space)");

i915_param_named_unsafe(enable_psr, int, 0600,
	"Enable PSR "
	"(0=disabled, 1=enabled) "
	"Default: -1 (use per-chip default)");

i915_param_named_unsafe(alpha_support, bool, 0400,
	"Enable alpha quality driver support for latest hardware. "
	"See also CONFIG_DRM_I915_ALPHA_SUPPORT.");

i915_param_named_unsafe(disable_power_well, int, 0400,
	"Disable display power wells when possible "
	"(-1=auto [default], 0=power wells always on, 1=power wells disabled when possible)");

i915_param_named_unsafe(enable_ips, int, 0600, "Enable IPS (default: true)");

i915_param_named(fastboot, bool, 0600,
	"Try to skip unnecessary mode sets at boot time (default: false)");

i915_param_named_unsafe(prefault_disable, bool, 0600,
	"Disable page prefaulting for pread/pwrite/reloc (default:false). "
	"For developers only.");

i915_param_named_unsafe(load_detect_test, bool, 0600,
	"Force-enable the VGA load detect code for testing (default:false). "
	"For developers only.");

i915_param_named_unsafe(force_reset_modeset_test, bool, 0600,
	"Force a modeset during gpu reset for testing (default:false). "
	"For developers only.");

i915_param_named_unsafe(invert_brightness, int, 0600,
	"Invert backlight brightness "
	"(-1 force normal, 0 machine defaults, 1 force inversion), please "
	"report PCI device ID, subsystem vendor and subsystem device ID "
	"to dri-devel@lists.freedesktop.org, if your machine needs it. "
	"It will then be included in an upcoming module version.");

i915_param_named(disable_display, bool, 0400,
	"Disable display (default: false)");

i915_param_named(mmio_debug, int, 0600,
	"Enable the MMIO debug code for the first N failures (default: off). "
	"This may negatively affect performance.");

i915_param_named(verbose_state_checks, bool, 0600,
	"Enable verbose logs (ie. WARN_ON()) in case of unexpected hw state conditions.");

i915_param_named_unsafe(nuclear_pageflip, bool, 0400,
	"Force enable atomic functionality on platforms that don't have full support yet.");

/* WA to get away with the default setting in VBT for early platforms.Will be removed */
i915_param_named_unsafe(edp_vswing, int, 0400,
	"Ignore/Override vswing pre-emph table selection from VBT "
	"(0=use value from vbt [default], 1=low power swing(200mV),"
	"2=default swing(400mV))");

i915_param_named_unsafe(enable_guc, int, 0400,
	"Enable GuC load for GuC submission and/or HuC load. "
	"Required functionality can be selected using bitmask values. "
	"(-1=auto, 0=disable [default], 1=GuC submission, 2=HuC load)");

i915_param_named(guc_log_level, int, 0400,
	"GuC firmware logging level. Requires GuC to be loaded. "
	"(-1=auto [default], 0=disable, 1..4=enable with verbosity min..max)");

i915_param_named_unsafe(guc_firmware_path, charp, 0400,
	"GuC firmware path to use instead of the default one");

i915_param_named_unsafe(huc_firmware_path, charp, 0400,
	"HuC firmware path to use instead of the default one");

i915_param_named_unsafe(dmc_firmware_path, charp, 0400,
	"DMC firmware path to use instead of the default one");

i915_param_named_unsafe(enable_dp_mst, bool, 0600,
	"Enable multi-stream transport (MST) for new DisplayPort sinks. (default: true)");

#if IS_ENABLED(CONFIG_DRM_I915_DEBUG)
i915_param_named_unsafe(inject_load_failure, uint, 0400,
	"Force an error after a number of failure check points (0:disabled (default), N:force failure at the Nth failure check point)");
#endif

i915_param_named(enable_dpcd_backlight, bool, 0600,
	"Enable support for DPCD backlight control (default:false)");

i915_param_named(enable_gvt, bool, 0400,
	"Enable support for Intel GVT-g graphics virtualization host support(default:false)");

static __always_inline void _print_param(struct drm_printer *p,
					 const char *name,
					 const char *type,
					 const void *x)
{
	if (!__builtin_strcmp(type, "bool"))
		drm_printf(p, "i915.%s=%s\n", name, yesno(*(const bool *)x));
	else if (!__builtin_strcmp(type, "int"))
		drm_printf(p, "i915.%s=%d\n", name, *(const int *)x);
	else if (!__builtin_strcmp(type, "unsigned int"))
		drm_printf(p, "i915.%s=%u\n", name, *(const unsigned int *)x);
	else if (!__builtin_strcmp(type, "char *"))
		drm_printf(p, "i915.%s=%s\n", name, *(const char **)x);
	else
		BUILD_BUG();
}

/**
 * i915_params_dump - dump i915 modparams
 * @params: i915 modparams
 * @p: the &drm_printer
 *
 * Pretty printer for i915 modparams.
 */
void i915_params_dump(const struct i915_params *params, struct drm_printer *p)
{
#define PRINT(T, x, ...) _print_param(p, #x, #T, &params->x);
	I915_PARAMS_FOR_EACH(PRINT);
#undef PRINT
}

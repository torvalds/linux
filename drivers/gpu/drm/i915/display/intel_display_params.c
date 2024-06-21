// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023 Intel Corporation
 */

#include "intel_display_params.h"
#include "i915_drv.h"

#define intel_display_param_named(name, T, perm, desc) \
	module_param_named(name, intel_display_modparams.name, T, perm); \
	MODULE_PARM_DESC(name, desc)
#define intel_display_param_named_unsafe(name, T, perm, desc) \
	module_param_named_unsafe(name, intel_display_modparams.name, T, perm); \
	MODULE_PARM_DESC(name, desc)

static struct intel_display_params intel_display_modparams __read_mostly = {
#define MEMBER(T, member, value, ...) .member = (value),
	INTEL_DISPLAY_PARAMS_FOR_EACH(MEMBER)
#undef MEMBER
};
/*
 * Note: As a rule, keep module parameter sysfs permissions read-only
 * 0400. Runtime changes are only supported through i915 debugfs.
 *
 * For any exceptions requiring write access and runtime changes through module
 * parameter sysfs, prevent debugfs file creation by setting the parameter's
 * debugfs mode to 0.
 */

intel_display_param_named_unsafe(dmc_firmware_path, charp, 0400,
	"DMC firmware path to use instead of the default one. "
	"Use /dev/null to disable DMC and runtime PM.");

intel_display_param_named_unsafe(vbt_firmware, charp, 0400,
	"Load VBT from specified file under /lib/firmware");

intel_display_param_named_unsafe(lvds_channel_mode, int, 0400,
	 "Specify LVDS channel mode "
	 "(0=probe BIOS [default], 1=single-channel, 2=dual-channel)");

intel_display_param_named_unsafe(panel_use_ssc, int, 0400,
	"Use Spread Spectrum Clock with panels [LVDS/eDP] "
	"(default: auto from VBT)");

intel_display_param_named_unsafe(vbt_sdvo_panel_type, int, 0400,
	"Override/Ignore selection of SDVO panel mode in the VBT "
	"(-2=ignore, -1=auto [default], index in VBT BIOS table)");

intel_display_param_named_unsafe(enable_dc, int, 0400,
	"Enable power-saving display C-states. "
	"(-1=auto [default]; 0=disable; 1=up to DC5; 2=up to DC6; "
	"3=up to DC5 with DC3CO; 4=up to DC6 with DC3CO)");

intel_display_param_named_unsafe(enable_dpt, bool, 0400,
	"Enable display page table (DPT) (default: true)");

intel_display_param_named_unsafe(enable_sagv, bool, 0400,
	"Enable system agent voltage/frequency scaling (SAGV) (default: true)");

intel_display_param_named_unsafe(disable_power_well, int, 0400,
	"Disable display power wells when possible "
	"(-1=auto [default], 0=power wells always on, 1=power wells disabled when possible)");

intel_display_param_named_unsafe(enable_ips, bool, 0400, "Enable IPS (default: true)");

intel_display_param_named_unsafe(invert_brightness, int, 0400,
	"Invert backlight brightness "
	"(-1 force normal, 0 machine defaults, 1 force inversion), please "
	"report PCI device ID, subsystem vendor and subsystem device ID "
	"to dri-devel@lists.freedesktop.org, if your machine needs it. "
	"It will then be included in an upcoming module version.");

/* WA to get away with the default setting in VBT for early platforms.Will be removed */
intel_display_param_named_unsafe(edp_vswing, int, 0400,
	"Ignore/Override vswing pre-emph table selection from VBT "
	"(0=use value from vbt [default], 1=low power swing(200mV),"
	"2=default swing(400mV))");

intel_display_param_named(enable_dpcd_backlight, int, 0400,
	"Enable support for DPCD backlight control"
	"(-1=use per-VBT LFP backlight type setting [default], 0=disabled, 1=enable, 2=force VESA interface, 3=force Intel interface)");

intel_display_param_named_unsafe(load_detect_test, bool, 0400,
	"Force-enable the VGA load detect code for testing (default:false). "
	"For developers only.");

intel_display_param_named_unsafe(force_reset_modeset_test, bool, 0400,
	"Force a modeset during gpu reset for testing (default:false). "
	"For developers only.");

intel_display_param_named(disable_display, bool, 0400,
	"Disable display (default: false)");

intel_display_param_named(verbose_state_checks, bool, 0400,
	"Enable verbose logs (ie. WARN_ON()) in case of unexpected hw state conditions.");

intel_display_param_named_unsafe(nuclear_pageflip, bool, 0400,
	"Force enable atomic functionality on platforms that don't have full support yet.");

intel_display_param_named_unsafe(enable_dp_mst, bool, 0400,
	"Enable multi-stream transport (MST) for new DisplayPort sinks. (default: true)");

intel_display_param_named_unsafe(enable_fbc, int, 0400,
	"Enable frame buffer compression for power savings "
	"(default: -1 (use per-chip default))");

intel_display_param_named_unsafe(enable_psr, int, 0400,
	"Enable PSR "
	"(0=disabled, 1=enable up to PSR1, 2=enable up to PSR2) "
	"Default: -1 (use per-chip default)");

intel_display_param_named(psr_safest_params, bool, 0400,
	"Replace PSR VBT parameters by the safest and not optimal ones. This "
	"is helpful to detect if PSR issues are related to bad values set in "
	" VBT. (0=use VBT parameters, 1=use safest parameters)"
	"Default: 0");

intel_display_param_named_unsafe(enable_psr2_sel_fetch, bool, 0400,
	"Enable PSR2 and Panel Replay selective fetch "
	"(0=disabled, 1=enabled) "
	"Default: 1");

intel_display_param_named_unsafe(enable_dmc_wl, bool, 0400,
	"Enable DMC wakelock "
	"(0=disabled, 1=enabled) "
	"Default: 0");

__maybe_unused
static void _param_print_bool(struct drm_printer *p, const char *driver_name,
			      const char *name, bool val)
{
	drm_printf(p, "%s.%s=%s\n", driver_name, name, str_yes_no(val));
}

__maybe_unused
static void _param_print_int(struct drm_printer *p, const char *driver_name,
			     const char *name, int val)
{
	drm_printf(p, "%s.%s=%d\n", driver_name, name, val);
}

__maybe_unused
static void _param_print_uint(struct drm_printer *p, const char *driver_name,
			      const char *name, unsigned int val)
{
	drm_printf(p, "%s.%s=%u\n", driver_name, name, val);
}

__maybe_unused
static void _param_print_ulong(struct drm_printer *p, const char *driver_name,
			       const char *name, unsigned long val)
{
	drm_printf(p, "%s.%s=%lu\n", driver_name, name, val);
}

__maybe_unused
static void _param_print_charp(struct drm_printer *p, const char *driver_name,
			       const char *name, const char *val)
{
	drm_printf(p, "%s.%s=%s\n", driver_name, name, val);
}

#define _param_print(p, driver_name, name, val)			\
	_Generic(val,						\
		 bool : _param_print_bool,			\
		 int : _param_print_int,			\
		 unsigned int : _param_print_uint,		\
		 unsigned long : _param_print_ulong,		\
		 char * : _param_print_charp)(p, driver_name, name, val)

/**
 * intel_display_params_dump - dump intel display modparams
 * @i915: i915 device
 * @p: the &drm_printer
 *
 * Pretty printer for i915 modparams.
 */
void intel_display_params_dump(struct drm_i915_private *i915, struct drm_printer *p)
{
#define PRINT(T, x, ...) _param_print(p, i915->drm.driver->name, #x, i915->display.params.x);
	INTEL_DISPLAY_PARAMS_FOR_EACH(PRINT);
#undef PRINT
}

__maybe_unused static void _param_dup_charp(char **valp)
{
	*valp = kstrdup(*valp ? *valp : "", GFP_ATOMIC);
}

__maybe_unused static void _param_nop(void *valp)
{
}

#define _param_dup(valp)				\
	_Generic(valp,					\
		 char ** : _param_dup_charp,		\
		 default : _param_nop)			\
		(valp)

void intel_display_params_copy(struct intel_display_params *dest)
{
	*dest = intel_display_modparams;
#define DUP(T, x, ...) _param_dup(&dest->x);
	INTEL_DISPLAY_PARAMS_FOR_EACH(DUP);
#undef DUP
}

__maybe_unused static void _param_free_charp(char **valp)
{
	kfree(*valp);
	*valp = NULL;
}

#define _param_free(valp)				\
	_Generic(valp,					\
		 char ** : _param_free_charp,		\
		 default : _param_nop)			\
		(valp)

/* free the allocated members, *not* the passed in params itself */
void intel_display_params_free(struct intel_display_params *params)
{
#define FREE(T, x, ...) _param_free(&params->x);
	INTEL_DISPLAY_PARAMS_FOR_EACH(FREE);
#undef FREE
}

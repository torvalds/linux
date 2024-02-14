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

#include <linux/string_helpers.h>

#include <drm/drm_print.h>

#include "i915_params.h"
#include "i915_drv.h"

DECLARE_DYNDBG_CLASSMAP(drm_debug_classes, DD_CLASS_TYPE_DISJOINT_BITS, 0,
			"DRM_UT_CORE",
			"DRM_UT_DRIVER",
			"DRM_UT_KMS",
			"DRM_UT_PRIME",
			"DRM_UT_ATOMIC",
			"DRM_UT_VBL",
			"DRM_UT_STATE",
			"DRM_UT_LEASE",
			"DRM_UT_DP",
			"DRM_UT_DRMRES");

#define i915_param_named(name, T, perm, desc) \
	module_param_named(name, i915_modparams.name, T, perm); \
	MODULE_PARM_DESC(name, desc)
#define i915_param_named_unsafe(name, T, perm, desc) \
	module_param_named_unsafe(name, i915_modparams.name, T, perm); \
	MODULE_PARM_DESC(name, desc)

struct i915_params i915_modparams __read_mostly = {
#define MEMBER(T, member, value, ...) .member = (value),
	I915_PARAMS_FOR_EACH(MEMBER)
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

i915_param_named(modeset, int, 0400,
	"Use kernel modesetting [KMS] (0=disable, "
	"1=on, -1=force vga console preference [default])");

i915_param_named_unsafe(reset, uint, 0400,
	"Attempt GPU resets (0=disabled, 1=full gpu reset, 2=engine reset [default])");

#if IS_ENABLED(CONFIG_DRM_I915_CAPTURE_ERROR)
i915_param_named(error_capture, bool, 0400,
	"Record the GPU state following a hang. "
	"This information in /sys/class/drm/card<N>/error is vital for "
	"triaging and debugging hangs.");
#endif

i915_param_named_unsafe(enable_hangcheck, bool, 0400,
	"Periodically check GPU activity for detecting hangs. "
	"WARNING: Disabling this can cause system wide hangs. "
	"(default: true)");

i915_param_named_unsafe(force_probe, charp, 0400,
	"Force probe options for specified supported devices. "
	"See CONFIG_DRM_I915_FORCE_PROBE for details.");

i915_param_named(memtest, bool, 0400,
	"Perform a read/write test of all device memory on module load (default: off)");

i915_param_named(mmio_debug, int, 0400,
	"Enable the MMIO debug code for the first N failures (default: off). "
	"This may negatively affect performance.");

i915_param_named_unsafe(enable_guc, int, 0400,
	"Enable GuC load for GuC submission and/or HuC load. "
	"Required functionality can be selected using bitmask values. "
	"(-1=auto [default], 0=disable, 1=GuC submission, 2=HuC load)");

i915_param_named(guc_log_level, int, 0400,
	"GuC firmware logging level. Requires GuC to be loaded. "
	"(-1=auto [default], 0=disable, 1..4=enable with verbosity min..max)");

i915_param_named_unsafe(guc_firmware_path, charp, 0400,
	"GuC firmware path to use instead of the default one");

i915_param_named_unsafe(huc_firmware_path, charp, 0400,
	"HuC firmware path to use instead of the default one");

i915_param_named_unsafe(dmc_firmware_path, charp, 0400,
	"DMC firmware path to use instead of the default one");

i915_param_named_unsafe(gsc_firmware_path, charp, 0400,
	"GSC firmware path to use instead of the default one");

#if IS_ENABLED(CONFIG_DRM_I915_DEBUG)
i915_param_named_unsafe(inject_probe_failure, uint, 0400,
	"Force an error after a number of failure check points (0:disabled (default), N:force failure at the Nth failure check point)");
#endif

#if IS_ENABLED(CONFIG_DRM_I915_GVT)
i915_param_named(enable_gvt, bool, 0400,
	"Enable support for Intel GVT-g graphics virtualization host support(default:false)");
#endif

#if CONFIG_DRM_I915_REQUEST_TIMEOUT
i915_param_named_unsafe(request_timeout_ms, uint, 0600,
			"Default request/fence/batch buffer expiration timeout.");
#endif

i915_param_named_unsafe(lmem_size, uint, 0400,
			"Set the lmem size(in MiB) for each region. (default: 0, all memory)");
i915_param_named_unsafe(lmem_bar_size, uint, 0400,
			"Set the lmem bar size(in MiB).");

static void _param_print_bool(struct drm_printer *p, const char *name,
			      bool val)
{
	drm_printf(p, "i915.%s=%s\n", name, str_yes_no(val));
}

static void _param_print_int(struct drm_printer *p, const char *name,
			     int val)
{
	drm_printf(p, "i915.%s=%d\n", name, val);
}

static void _param_print_uint(struct drm_printer *p, const char *name,
			      unsigned int val)
{
	drm_printf(p, "i915.%s=%u\n", name, val);
}

static void _param_print_ulong(struct drm_printer *p, const char *name,
			       unsigned long val)
{
	drm_printf(p, "i915.%s=%lu\n", name, val);
}

static void _param_print_charp(struct drm_printer *p, const char *name,
			       const char *val)
{
	drm_printf(p, "i915.%s=%s\n", name, val);
}

#define _param_print(p, name, val)				\
	_Generic(val,						\
		 bool: _param_print_bool,			\
		 int: _param_print_int,				\
		 unsigned int: _param_print_uint,		\
		 unsigned long: _param_print_ulong,		\
		 char *: _param_print_charp)(p, name, val)

/**
 * i915_params_dump - dump i915 modparams
 * @params: i915 modparams
 * @p: the &drm_printer
 *
 * Pretty printer for i915 modparams.
 */
void i915_params_dump(const struct i915_params *params, struct drm_printer *p)
{
#define PRINT(T, x, ...) _param_print(p, #x, params->x);
	I915_PARAMS_FOR_EACH(PRINT);
#undef PRINT
}

static void _param_dup_charp(char **valp)
{
	*valp = kstrdup(*valp, GFP_ATOMIC);
}

static void _param_nop(void *valp)
{
}

#define _param_dup(valp)				\
	_Generic(valp,					\
		 char **: _param_dup_charp,		\
		 default: _param_nop)(valp)

void i915_params_copy(struct i915_params *dest, const struct i915_params *src)
{
	*dest = *src;
#define DUP(T, x, ...) _param_dup(&dest->x);
	I915_PARAMS_FOR_EACH(DUP);
#undef DUP
}

static void _param_free_charp(char **valp)
{
	kfree(*valp);
	*valp = NULL;
}

#define _param_free(valp)				\
	_Generic(valp,					\
		 char **: _param_free_charp,		\
		 default: _param_nop)(valp)

/* free the allocated members, *not* the passed in params itself */
void i915_params_free(struct i915_params *params)
{
#define FREE(T, x, ...) _param_free(&params->x);
	I915_PARAMS_FOR_EACH(FREE);
#undef FREE
}

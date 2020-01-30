// SPDX-License-Identifier: MIT
/*
 * Copyright © 2019 Intel Corporation
 */

#include <drm/drm_drv.h>

#include "i915_drv.h"
#include "i915_utils.h"

#define FDO_BUG_URL "https://bugs.freedesktop.org/enter_bug.cgi?product=DRI"
#define FDO_BUG_MSG "Please file a bug at " FDO_BUG_URL " against DRM/Intel " \
		    "providing the dmesg log by booting with drm.debug=0xf"

void
__i915_printk(struct drm_i915_private *dev_priv, const char *level,
	      const char *fmt, ...)
{
	static bool shown_bug_once;
	struct device *kdev = dev_priv->drm.dev;
	bool is_error = level[1] <= KERN_ERR[1];
	bool is_debug = level[1] == KERN_DEBUG[1];
	struct va_format vaf;
	va_list args;

	if (is_debug && !(drm_debug & DRM_UT_DRIVER))
		return;

	va_start(args, fmt);

	vaf.fmt = fmt;
	vaf.va = &args;

	if (is_error)
		dev_printk(level, kdev, "%pV", &vaf);
	else
		dev_printk(level, kdev, "[" DRM_NAME ":%ps] %pV",
			   __builtin_return_address(0), &vaf);

	va_end(args);

	if (is_error && !shown_bug_once) {
		/*
		 * Ask the user to file a bug report for the error, except
		 * if they may have caused the bug by fiddling with unsafe
		 * module parameters.
		 */
		if (!test_taint(TAINT_USER))
			dev_notice(kdev, "%s", FDO_BUG_MSG);
		shown_bug_once = true;
	}
}

#if IS_ENABLED(CONFIG_DRM_I915_DEBUG)
static unsigned int i915_probe_fail_count;

int __i915_inject_load_error(struct drm_i915_private *i915, int err,
			     const char *func, int line)
{
	if (i915_probe_fail_count >= i915_modparams.inject_load_failure)
		return 0;

	if (++i915_probe_fail_count < i915_modparams.inject_load_failure)
		return 0;

	__i915_printk(i915, KERN_INFO,
		      "Injecting failure %d at checkpoint %u [%s:%d]\n",
		      err, i915_modparams.inject_load_failure, func, line);
	i915_modparams.inject_load_failure = 0;
	return err;
}

bool i915_error_injected(void)
{
	return i915_probe_fail_count && !i915_modparams.inject_load_failure;
}

#endif

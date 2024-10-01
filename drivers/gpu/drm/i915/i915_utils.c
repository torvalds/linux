// SPDX-License-Identifier: MIT
/*
 * Copyright © 2019 Intel Corporation
 */

#include <linux/device.h>

#include <drm/drm_drv.h>

#include "i915_drv.h"
#include "i915_reg.h"
#include "i915_utils.h"

void add_taint_for_CI(struct drm_i915_private *i915, unsigned int taint)
{
	drm_notice(&i915->drm, "CI tainted: %#x by %pS\n",
		   taint, __builtin_return_address(0));

	/* Failures that occur during fault injection testing are expected */
	if (!i915_error_injected())
		__add_taint_for_CI(taint);
}

#if IS_ENABLED(CONFIG_DRM_I915_DEBUG)
static unsigned int i915_probe_fail_count;

int __i915_inject_probe_error(struct drm_i915_private *i915, int err,
			      const char *func, int line)
{
	if (i915_probe_fail_count >= i915_modparams.inject_probe_failure)
		return 0;

	if (++i915_probe_fail_count < i915_modparams.inject_probe_failure)
		return 0;

	drm_info(&i915->drm, "Injecting failure %d at checkpoint %u [%s:%d]\n",
		 err, i915_modparams.inject_probe_failure, func, line);

	i915_modparams.inject_probe_failure = 0;
	return err;
}

bool i915_error_injected(void)
{
	return i915_probe_fail_count && !i915_modparams.inject_probe_failure;
}

#endif

void cancel_timer(struct timer_list *t)
{
	if (!timer_active(t))
		return;

	del_timer(t);
	WRITE_ONCE(t->expires, 0);
}

void set_timer_ms(struct timer_list *t, unsigned long timeout)
{
	if (!timeout) {
		cancel_timer(t);
		return;
	}

	timeout = msecs_to_jiffies(timeout);

	/*
	 * Paranoia to make sure the compiler computes the timeout before
	 * loading 'jiffies' as jiffies is volatile and may be updated in
	 * the background by a timer tick. All to reduce the complexity
	 * of the addition and reduce the risk of losing a jiffy.
	 */
	barrier();

	/* Keep t->expires = 0 reserved to indicate a canceled timer. */
	mod_timer(t, jiffies + timeout ?: 1);
}

bool i915_vtd_active(struct drm_i915_private *i915)
{
	if (device_iommu_mapped(i915->drm.dev))
		return true;

	/* Running as a guest, we assume the host is enforcing VT'd */
	return i915_run_as_guest();
}

bool i915_direct_stolen_access(struct drm_i915_private *i915)
{
	/*
	 * Wa_22018444074
	 *
	 * Access via BAR can hang MTL, go directly to GSM/DSM,
	 * except for VM guests which won't have access to it.
	 *
	 * Normally this would not work but on MTL the system firmware
	 * should have relaxed the access permissions sufficiently.
	 * 0x138914==0x1 indicates that the firmware has done its job.
	 */
	return IS_METEORLAKE(i915) && !i915_run_as_guest() &&
		intel_uncore_read(&i915->uncore, MTL_PCODE_STOLEN_ACCESS) == STOLEN_ACCESS_ALLOWED;
}

// SPDX-License-Identifier: MIT
/* Copyright © 2025 Intel Corporation */

#include <drm/intel/display_parent_interface.h>

#include "intel_display_core.h"
#include "intel_display_rpm.h"
#include "xe_device.h"
#include "xe_device_types.h"
#include "xe_pm.h"

/* -ENOENT means we got the ref, but there's no tracking */
#define INTEL_WAKEREF_DEF ERR_PTR(-ENOENT)

static struct ref_tracker *xe_display_rpm_get(const struct drm_device *drm)
{
	return xe_pm_runtime_resume_and_get(to_xe_device(drm)) ? INTEL_WAKEREF_DEF : NULL;
}

static struct ref_tracker *xe_display_rpm_get_if_in_use(const struct drm_device *drm)
{
	return xe_pm_runtime_get_if_in_use(to_xe_device(drm)) ? INTEL_WAKEREF_DEF : NULL;
}

static struct ref_tracker *xe_display_rpm_get_noresume(const struct drm_device *drm)
{
	xe_pm_runtime_get_noresume(to_xe_device(drm));

	return INTEL_WAKEREF_DEF;
}

static void xe_display_rpm_put(const struct drm_device *drm, struct ref_tracker *wakeref)
{
	if (wakeref)
		xe_pm_runtime_put(to_xe_device(drm));
}

static void xe_display_rpm_put_unchecked(const struct drm_device *drm)
{
	xe_pm_runtime_put(to_xe_device(drm));
}

static bool xe_display_rpm_suspended(const struct drm_device *drm)
{
	struct xe_device *xe = to_xe_device(drm);

	return pm_runtime_suspended(xe->drm.dev);
}

static void xe_display_rpm_assert_held(const struct drm_device *drm)
{
	/* FIXME */
}

static void xe_display_rpm_assert_block(const struct drm_device *drm)
{
	/* FIXME */
}

static void xe_display_rpm_assert_unblock(const struct drm_device *drm)
{
	/* FIXME */
}

const struct intel_display_rpm_interface xe_display_rpm_interface = {
	.get = xe_display_rpm_get,
	.get_raw = xe_display_rpm_get,
	.get_if_in_use = xe_display_rpm_get_if_in_use,
	.get_noresume = xe_display_rpm_get_noresume,
	.put = xe_display_rpm_put,
	.put_raw = xe_display_rpm_put,
	.put_unchecked = xe_display_rpm_put_unchecked,
	.suspended = xe_display_rpm_suspended,
	.assert_held = xe_display_rpm_assert_held,
	.assert_block = xe_display_rpm_assert_block,
	.assert_unblock = xe_display_rpm_assert_unblock
};

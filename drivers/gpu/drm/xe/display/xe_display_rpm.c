// SPDX-License-Identifier: MIT
/* Copyright © 2025 Intel Corporation */

#include "intel_display_core.h"
#include "intel_display_rpm.h"
#include "xe_device.h"
#include "xe_device_types.h"
#include "xe_pm.h"

static struct xe_device *display_to_xe(struct intel_display *display)
{
	return to_xe_device(display->drm);
}

struct ref_tracker *intel_display_rpm_get_raw(struct intel_display *display)
{
	return intel_display_rpm_get(display);
}

void intel_display_rpm_put_raw(struct intel_display *display, struct ref_tracker *wakeref)
{
	intel_display_rpm_put(display, wakeref);
}

struct ref_tracker *intel_display_rpm_get(struct intel_display *display)
{
	return xe_pm_runtime_resume_and_get(display_to_xe(display)) ? INTEL_WAKEREF_DEF : NULL;
}

struct ref_tracker *intel_display_rpm_get_if_in_use(struct intel_display *display)
{
	return xe_pm_runtime_get_if_in_use(display_to_xe(display)) ? INTEL_WAKEREF_DEF : NULL;
}

struct ref_tracker *intel_display_rpm_get_noresume(struct intel_display *display)
{
	xe_pm_runtime_get_noresume(display_to_xe(display));

	return INTEL_WAKEREF_DEF;
}

void intel_display_rpm_put(struct intel_display *display, struct ref_tracker *wakeref)
{
	if (wakeref)
		xe_pm_runtime_put(display_to_xe(display));
}

void intel_display_rpm_put_unchecked(struct intel_display *display)
{
	xe_pm_runtime_put(display_to_xe(display));
}

bool intel_display_rpm_suspended(struct intel_display *display)
{
	struct xe_device *xe = display_to_xe(display);

	return pm_runtime_suspended(xe->drm.dev);
}

void assert_display_rpm_held(struct intel_display *display)
{
	/* FIXME */
}

void intel_display_rpm_assert_block(struct intel_display *display)
{
	/* FIXME */
}

void intel_display_rpm_assert_unblock(struct intel_display *display)
{
	/* FIXME */
}

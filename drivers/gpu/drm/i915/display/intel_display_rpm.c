// SPDX-License-Identifier: MIT
/* Copyright © 2025 Intel Corporation */

#include "i915_drv.h"
#include "intel_display_core.h"
#include "intel_display_rpm.h"
#include "intel_runtime_pm.h"

static struct intel_runtime_pm *display_to_rpm(struct intel_display *display)
{
	struct drm_i915_private *i915 = to_i915(display->drm);

	return &i915->runtime_pm;
}

struct ref_tracker *intel_display_rpm_get_raw(struct intel_display *display)
{
	return intel_runtime_pm_get_raw(display_to_rpm(display));
}

void intel_display_rpm_put_raw(struct intel_display *display, struct ref_tracker *wakeref)
{
	intel_runtime_pm_put_raw(display_to_rpm(display), wakeref);
}

struct ref_tracker *intel_display_rpm_get(struct intel_display *display)
{
	return intel_runtime_pm_get(display_to_rpm(display));
}

struct ref_tracker *intel_display_rpm_get_if_in_use(struct intel_display *display)
{
	return intel_runtime_pm_get_if_in_use(display_to_rpm(display));
}

struct ref_tracker *intel_display_rpm_get_noresume(struct intel_display *display)
{
	return intel_runtime_pm_get_noresume(display_to_rpm(display));
}

void intel_display_rpm_put(struct intel_display *display, struct ref_tracker *wakeref)
{
	intel_runtime_pm_put(display_to_rpm(display), wakeref);
}

void intel_display_rpm_put_unchecked(struct intel_display *display)
{
	intel_runtime_pm_put_unchecked(display_to_rpm(display));
}

bool intel_display_rpm_suspended(struct intel_display *display)
{
	return intel_runtime_pm_suspended(display_to_rpm(display));
}

void assert_display_rpm_held(struct intel_display *display)
{
	assert_rpm_wakelock_held(display_to_rpm(display));
}

void intel_display_rpm_assert_block(struct intel_display *display)
{
	disable_rpm_wakeref_asserts(display_to_rpm(display));
}

void intel_display_rpm_assert_unblock(struct intel_display *display)
{
	enable_rpm_wakeref_asserts(display_to_rpm(display));
}

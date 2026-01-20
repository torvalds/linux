// SPDX-License-Identifier: MIT
/* Copyright © 2025 Intel Corporation */

#include <drm/intel/display_parent_interface.h>

#include "intel_display_core.h"
#include "intel_display_rpm.h"

struct ref_tracker *intel_display_rpm_get_raw(struct intel_display *display)
{
	return display->parent->rpm->get_raw(display->drm);
}

void intel_display_rpm_put_raw(struct intel_display *display, struct ref_tracker *wakeref)
{
	display->parent->rpm->put_raw(display->drm, wakeref);
}

struct ref_tracker *intel_display_rpm_get(struct intel_display *display)
{
	return display->parent->rpm->get(display->drm);
}

struct ref_tracker *intel_display_rpm_get_if_in_use(struct intel_display *display)
{
	return display->parent->rpm->get_if_in_use(display->drm);
}

struct ref_tracker *intel_display_rpm_get_noresume(struct intel_display *display)
{
	return display->parent->rpm->get_noresume(display->drm);
}

void intel_display_rpm_put(struct intel_display *display, struct ref_tracker *wakeref)
{
	display->parent->rpm->put(display->drm, wakeref);
}

void intel_display_rpm_put_unchecked(struct intel_display *display)
{
	display->parent->rpm->put_unchecked(display->drm);
}

bool intel_display_rpm_suspended(struct intel_display *display)
{
	return display->parent->rpm->suspended(display->drm);
}

void assert_display_rpm_held(struct intel_display *display)
{
	display->parent->rpm->assert_held(display->drm);
}

void intel_display_rpm_assert_block(struct intel_display *display)
{
	display->parent->rpm->assert_block(display->drm);
}

void intel_display_rpm_assert_unblock(struct intel_display *display)
{
	display->parent->rpm->assert_unblock(display->drm);
}

/* SPDX-License-Identifier: MIT */
/* Copyright © 2025 Intel Corporation */

#ifndef __INTEL_DISPLAY_RPM__
#define __INTEL_DISPLAY_RPM__

#include <linux/types.h>

struct intel_display;
struct ref_tracker;

struct ref_tracker *intel_display_rpm_get(struct intel_display *display);
void intel_display_rpm_put(struct intel_display *display, struct ref_tracker *wakeref);

#define __with_intel_display_rpm(__display, __wakeref) \
	for (struct ref_tracker *(__wakeref) = intel_display_rpm_get(__display); (__wakeref); \
	     intel_display_rpm_put((__display), (__wakeref)), (__wakeref) = NULL)

#define with_intel_display_rpm(__display) \
	__with_intel_display_rpm((__display), __UNIQUE_ID(wakeref))

/* Only for special cases. */
bool intel_display_rpm_suspended(struct intel_display *display);

void assert_display_rpm_held(struct intel_display *display);
void intel_display_rpm_assert_block(struct intel_display *display);
void intel_display_rpm_assert_unblock(struct intel_display *display);

/* Only for display power implementation. */
struct ref_tracker *intel_display_rpm_get_raw(struct intel_display *display);
void intel_display_rpm_put_raw(struct intel_display *display, struct ref_tracker *wakeref);

struct ref_tracker *intel_display_rpm_get_if_in_use(struct intel_display *display);
struct ref_tracker *intel_display_rpm_get_noresume(struct intel_display *display);
void intel_display_rpm_put_unchecked(struct intel_display *display);

#endif /* __INTEL_DISPLAY_RPM__ */

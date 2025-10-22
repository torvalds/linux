/* SPDX-License-Identifier: MIT */
/* Copyright © 2025 Intel Corporation */

#ifndef __INTEL_DISPLAY_UTILS__
#define __INTEL_DISPLAY_UTILS__

#include <linux/bug.h>
#include <linux/types.h>

struct intel_display;

#ifndef MISSING_CASE
#define MISSING_CASE(x) WARN(1, "Missing case (%s == %ld)\n", \
			     __stringify(x), (long)(x))
#endif

#ifndef fetch_and_zero
#define fetch_and_zero(ptr) ({						\
	typeof(*ptr) __T = *(ptr);					\
	*(ptr) = (typeof(*ptr))0;					\
	__T;								\
})
#endif

#define KHz(x) (1000 * (x))
#define MHz(x) KHz(1000 * (x))

bool intel_display_run_as_guest(struct intel_display *display);
bool intel_display_vtd_active(struct intel_display *display);

#endif /* __INTEL_DISPLAY_UTILS__ */

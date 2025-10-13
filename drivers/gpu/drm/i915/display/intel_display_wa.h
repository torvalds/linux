/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */

#ifndef __INTEL_DISPLAY_WA_H__
#define __INTEL_DISPLAY_WA_H__

#include <linux/types.h>

struct intel_display;

void intel_display_wa_apply(struct intel_display *display);

#ifdef I915
static inline bool intel_display_needs_wa_16023588340(struct intel_display *display)
{
	return false;
}
#else
bool intel_display_needs_wa_16023588340(struct intel_display *display);
#endif

enum intel_display_wa {
	INTEL_DISPLAY_WA_16023588340,
	INTEL_DISPLAY_WA_16025573575,
	INTEL_DISPLAY_WA_14011503117,
};

bool __intel_display_wa(struct intel_display *display, enum intel_display_wa wa, const char *name);

#define intel_display_wa(__display, __wa) \
	__intel_display_wa((__display), INTEL_DISPLAY_WA_##__wa, __stringify(__wa))

#endif
